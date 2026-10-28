#include "Forklift/ForkliftSlamComponent.h"
#include "Forklift/ForkliftNavigationSensorComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

namespace
{
	FVector2D Rotate2(FVector2D P, float A)
	{
		const float C = FMath::Cos(A), S = FMath::Sin(A);
		return FVector2D(C * P.X - S * P.Y, S * P.X + C * P.Y);
	}
	FIntPoint CellAt(FVector2D P)
	{
		return FIntPoint(FMath::FloorToInt(P.X / UForkliftSlamComponent::CellCm),
			FMath::FloorToInt(P.Y / UForkliftSlamComponent::CellCm));
	}
}

UForkliftSlamComponent::UForkliftSlamComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UForkliftSlamComponent::BeginPlay()
{
	Super::BeginPlay();
	Sensor = GetOwner()->FindComponentByClass<UForkliftNavigationSensorComponent>();
	if (Sensor) AddTickPrerequisiteComponent(Sensor);
	ResetMap();
}

void UForkliftSlamComponent::ResetMap()
{
	Cells.Reset();
	LocalizationCells.Reset();
	MarginalScans=0;
	bLastPoseQualityAccepted=false;
	MatchField.Reset();
	StableScans = 0;
	++MapGeneration;
	LastAcceptedScan = -1.0;
	Position = Velocity = FVector2D::ZeroVector;
	Yaw = PendingYaw = Confidence = 0.f;
	bInitialized = false;
	ScanCount = MapUpdateCount = 0;
	LastScan = Sensor ? Sensor->GetLatestScan().TimestampSeconds : 0.0;
	Status = TEXT("Waiting for LiDAR");
}

void UForkliftSlamComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, Type, Function);
	if (!Sensor || !Sensor->bScanEnabled)
	{
		Status = TEXT("LiDAR disabled - map paused");
		PendingYaw = 0.f;
		return;
	}
	const auto& Imu = Sensor->GetLatestImu();
	if (Imu.bInitialized && Dt > 0.f && Dt < .25f)
		PendingYaw += Imu.AngularVelocityRadPerSec.Z * Dt;
	if (Sensor->GetLatestScan().TimestampSeconds > LastScan) ProcessScan();
	if (GetWorld()->GetTimeSeconds() - LastScan > 1.25)
		Status = TEXT("Stale scan - map paused");
}

float UForkliftSlamComponent::Score(const TArray<FVector2D>& Points, FVector2D P, float Angle) const
{
	float Sum = 0.f;
	for (const FVector2D& Point : Points)
	{
		const FIntPoint C = CellAt(P + Rotate2(Point, Angle));
		Sum += MatchField.FindRef(C);
	}
	return Points.IsEmpty() ? 0.f : Sum / Points.Num();
}

void UForkliftSlamComponent::UpdateCell(FIntPoint Cell, int32 Delta)
{
	// Fixed 200 m square bounds keep memory bounded even after tracking loss.
	if (FMath::Abs(Cell.X) >= 500 || FMath::Abs(Cell.Y) >= 500) return;
	int8& Value = Cells.FindOrAdd(Cell);
	Value = static_cast<int8>(FMath::Clamp(static_cast<int32>(Value) + Delta, -12, 12));
}

void UForkliftSlamComponent::ProcessScan()
{
	const auto& Scan = Sensor->GetLatestScan();
	const float Dt = static_cast<float>(Scan.TimestampSeconds - LastScan);
	LastScan = Scan.TimestampSeconds;
	++ScanCount;
	bLastPoseQualityAccepted=false;
	TArray<FVector2D> Points;
	TSet<FIntPoint> PointCells;
	// Accept returns from every elevation when they hit vertical surfaces.
	// Surface normals reject floor and ceiling hits that would destabilize 2D SLAM.
	for (const auto& Ray : Scan.Samples)
		if (Ray.bValid && Ray.ReturnKind == ELidarReturnKind::ExternalHit &&
			!Ray.LocalNormal.IsNearlyZero() && FMath::Abs(Ray.LocalNormal.Z) < .55f && Ray.DistanceCm > 30.f)
					{
			const FVector2D P(Ray.LocalPointCm.X, Ray.LocalPointCm.Y);
			if (!PointCells.Contains(CellAt(P))) { PointCells.Add(CellAt(P)); Points.Add(P); }
		}
	// Sparse returns are normal in wide warehouse aisles. Five hit points are
	// enough to seed/track a local scan; no-return beams still contribute free
	// space below, but are deliberately excluded from scan matching.
	if (Points.Num() < 5 || Dt <= 0.f)
	{
		StableScans = 0;
		MarginalScans=0;
		Status = FString::Printf(TEXT("측정점 부족: %d/5, 범위 %.0fm"), Points.Num(), Sensor->MaximumRangeCm/100.f);
		Velocity = FVector2D::ZeroVector;
		Confidence = 0.f;
		return;
	}
	bool bPoseAccepted = true;
	if (bInitialized)
	{
		// Build a distance-like lookup once per scan, rather than querying
		// 25 neighbours for every point in every pose candidate.
		MatchField.Reset();
		for (const FIntPoint& Cell : LocalizationCells)
		{
			const FVector2D Centre((Cell.X+.5)*CellCm,(Cell.Y+.5)*CellCm);
			if (FVector2D::Distance(Centre,Position)>Sensor->MaximumRangeCm+200.f) continue;
			for (int32 X=-2; X<=2; ++X) for (int32 Y=-2; Y<=2; ++Y)
			{
				float& Value=MatchField.FindOrAdd(Cell+FIntPoint(X,Y));
				Value=FMath::Max(Value,FMath::Exp(-.5f*(X*X+Y*Y)));
			}
		}
	}
	if (bInitialized)
	{
		const FVector2D Prior = Position + Velocity * FMath::Min(Dt, .2f);
		const float PriorYaw = Yaw + PendingYaw;
		FVector2D BestP = Prior;
		float BestYaw = PriorYaw;
		float BestScore = Score(Points, BestP, BestYaw);
		// Coarse-to-fine correlative scan-to-map matching, bounded per scan.
		for (int32 Level = 0; Level < 3; ++Level)
		{
			const float Step = 40.f / (1 << Level);
			const float Turn = FMath::DegreesToRadians(2.f / (1 << Level));
			const FVector2D Centre = BestP;
			const float CentreYaw = BestYaw;
			for (int32 X = -2; X <= 2; ++X)
				for (int32 Y = -2; Y <= 2; ++Y)
					for (int32 A = -1; A <= 1; ++A)
					{
						const FVector2D Candidate = Centre + FVector2D(X * Step, Y * Step);
						const float Angle = CentreYaw + A * Turn;
						const float S = Score(Points, Candidate, Angle);
						// Strict improvement preserves the motion prior in ties.
						if (S > BestScore + .002f) { BestScore = S; BestP = Candidate; BestYaw = Angle; }
					}
		}
		Confidence = BestScore;
		// Borderline matches must have spatial support and a repeatable small
        // correction; score alone cannot authorize continued motion.
        TSet<FIntPoint> UniqueSupport;
        TSet<int32> BearingSectors;
        int32 Inliers=0;
        for (const FVector2D& Point:Points)
        {
            const FIntPoint Cell=CellAt(BestP+Rotate2(Point,BestYaw));
            if (MatchField.FindRef(Cell)>=.36f)
            {
                ++Inliers;
                UniqueSupport.Add(Cell);
                BearingSectors.Add(FMath::FloorToInt((FMath::Atan2(Point.Y,Point.X)+PI)/(PI/4.f)));
            }
        }
        const bool Supported=UniqueSupport.Num()>=12 && BearingSectors.Num()>=2 &&
            Inliers>=FMath::CeilToInt(Points.Num()*.60f);
        const bool SmallCorrection=FVector2D::Distance(BestP,Prior)<=20.f &&
            FMath::Abs(FMath::UnwindRadians(BestYaw-PriorYaw))<=FMath::DegreesToRadians(2.f);
        const bool Repeatable=MarginalScans==0 ||
            (FVector2D::Distance(BestP,MarginalPosition)<=25.f &&
             FMath::Abs(FMath::UnwindRadians(BestYaw-MarginalYaw))<=FMath::DegreesToRadians(5.f));
        const bool Marginal=BestScore>=.40f && BestScore<.45f && Supported && SmallCorrection;
        MarginalScans=Marginal ? (Repeatable ? FMath::Min(MarginalScans+1,5) : 1) : 0;
        MarginalPosition=BestP;
        MarginalYaw=BestYaw;
        const bool QualityOK=BestScore>=.45f || (Marginal && MarginalScans>=5);
        if (!QualityOK || FVector2D::Distance(BestP, Position) > 150.f)
		{
			StableScans = 0;
			Status = FString::Printf(TEXT("정합 %.1f%% | 경계 안정 %d/5 | 지지점 %d | 방향 %d"), BestScore*100.f, MarginalScans, UniqueSupport.Num(), BearingSectors.Num());
			Velocity = FVector2D::ZeroVector;
			bPoseAccepted = false;
		}
		else
		{
			Velocity = .5f * Velocity + .5f * (BestP - Position) / FMath::Max(Dt, .02f);
			Velocity = Velocity.GetClampedToMaxSize(500.f);
			Position = BestP;
			Yaw = FMath::UnwindRadians(BestYaw);
		}
	}
	else
	{
		bInitialized = true;
		Confidence = 1.f;
	}
	// Never corrupt the map using a rejected pose.
	if (!bPoseAccepted) return;
	PendingYaw = 0.f;
	// Separate localization landmarks from the traversability map.
	// Only accepted poses may insert either type of evidence.
	for (const FVector2D& P : Points)
	{
		const FIntPoint C=CellAt(Position+Rotate2(P,Yaw));
		if (FMath::Abs(C.X)<500 && FMath::Abs(C.Y)<500) LocalizationCells.Add(C);
	}
	TSet<FIntPoint> FreeCells, HitCells;
	const float GroundZ=Sensor->GetGroundHeightInBody();
	for (const auto& Ray : Scan.Samples)
	{
		if (!Ray.bValid || Ray.ReturnKind!=ELidarReturnKind::ExternalHit) continue;
		const FVector BodyPoint=Scan.SensorToBody.TransformPosition(Ray.LocalPointCm);
		const FVector BodyNormal=Scan.SensorToBody.TransformVectorNoScale(Ray.LocalNormal);
		const FIntPoint C=CellAt(Position+Rotate2(FVector2D(Ray.LocalPointCm.X,Ray.LocalPointCm.Y),Yaw));
		// A high horizontal/no-return ray says nothing about floor clearance.
		// Observe only a small patch around a measured floor endpoint. Never
		// clear the whole projected ray: it may pass above a pallet or shelf.
		const float Height=BodyPoint.Z-GroundZ;
		if (BodyNormal.Z>.85f && Height>=-12.f && Height<=6.f)
		{
			for (int32 X=-1; X<=1; ++X) for (int32 Y=-1; Y<=1; ++Y)
				FreeCells.Add(C+FIntPoint(X,Y));
		}
		else if (Height>6.f && Height<550.f)
		{
			// Includes pallet/box tops, not just vertical wall normals.
			HitCells.Add(C);
		}
	}
	for (const FIntPoint& C : FreeCells) if (!HitCells.Contains(C)) UpdateCell(C,-1);
	for (const FIntPoint& C : HitCells) UpdateCell(C,4);
	++MapUpdateCount;
	if (bPoseAccepted)
	{
		++StableScans;
		LastAcceptedScan = Scan.TimestampSeconds;
		bLastPoseQualityAccepted=true;
		Status = FString::Printf(TEXT("SLAM %d점 | 바닥 %d / 장애물 %d | 안정 %d/3"), Points.Num(), FreeCells.Num(), HitCells.Num(), FMath::Min(StableScans,3));
	}
}

FVector2D UForkliftSlamComponent::GetPredictedPosition(double Now) const
{
    // Extrapolate the scan-matched velocity, never simulation world velocity.
    const double Age = FMath::Clamp(Now - LastAcceptedScan, 0.0, .3);
    return Position + Velocity * Age;
}