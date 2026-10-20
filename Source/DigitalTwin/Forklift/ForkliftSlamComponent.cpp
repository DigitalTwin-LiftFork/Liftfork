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
	if (GetWorld()->GetTimeSeconds() - LastScan > .5)
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
	TArray<FVector2D> Points;
	// Only near-horizontal rays define the 2D map. Sloped rays hitting floors
	// and shelf tops would otherwise shift their endpoints as the truck moves.
	for (const auto& Ray : Scan.Samples)
		if (Ray.bValid && Ray.ReturnKind == ELidarReturnKind::ExternalHit &&
			FMath::Abs(Ray.LocalDirection.Z) < .015f && Ray.DistanceCm > 30.f)
			Points.Add(FVector2D(Ray.LocalPointCm.X, Ray.LocalPointCm.Y));
	if (Points.Num() < 20 || Dt <= 0.f)
	{
		StableScans = 0;
		Status = FString::Printf(TEXT("측정점 부족: %d/20, 범위 %.0fm"), Points.Num(), Sensor->MaximumRangeCm/100.f);
		Velocity = FVector2D::ZeroVector;
		Confidence = 0.f;
		return;
	}
	if (bInitialized)
	{
		// Build a distance-like lookup once per scan, rather than querying
		// 25 neighbours for every point in every pose candidate.
		MatchField.Reset();
		for (const auto& Entry : Cells)
		{
			if (Entry.Value < 3) continue;
			const FVector2D Centre((Entry.Key.X+.5)*CellCm,(Entry.Key.Y+.5)*CellCm);
			if (FVector2D::Distance(Centre,Position)>Sensor->MaximumRangeCm+200.f) continue;
			for (int32 X=-2; X<=2; ++X) for (int32 Y=-2; Y<=2; ++Y)
			{
				float& Value=MatchField.FindOrAdd(Entry.Key+FIntPoint(X,Y));
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
		if (BestScore < .45f || FVector2D::Distance(BestP, Position) > 150.f)
		{
			StableScans = 0;
		Status = FString::Printf(TEXT("스캔 정합 대기: 일치 %.0f%%, 측정점 %d"), BestScore*100.f, Points.Num());
			Velocity = FVector2D::ZeroVector;

			return;
		}
		Velocity = .5f * Velocity + .5f * (BestP - Position) / FMath::Max(Dt, .02f);
		Velocity = Velocity.GetClampedToMaxSize(500.f);
		Position = BestP;
		Yaw = FMath::UnwindRadians(BestYaw);
	}
	else
	{
		bInitialized = true;
		Confidence = 1.f;
	}
	PendingYaw = 0.f;
	// Ray carving: observed free space is distinct from unknown cells.
	TSet<FIntPoint> FreeCells, HitCells;
	for (const FVector2D& Point : Points)
	{
		const FVector2D End = Position + Rotate2(Point, Yaw);
		const int32 Steps = FMath::CeilToInt(Point.Size() / (CellCm * .5f));
		const FIntPoint Hit = CellAt(End);
		HitCells.Add(Hit);
		for (int32 I = 0; I < Steps; ++I)
		{
			const FIntPoint C = CellAt(FMath::Lerp(Position, End, static_cast<float>(I) / Steps));
			if (C != Hit) FreeCells.Add(C);
		}
	}
	for (const FIntPoint& C : FreeCells) if (!HitCells.Contains(C)) UpdateCell(C, -1);
	for (const FIntPoint& C : HitCells) UpdateCell(C, 4);
	++MapUpdateCount;
	++StableScans;
	LastAcceptedScan = Scan.TimestampSeconds;
	Status = FString::Printf(TEXT("SLAM 추적: 측정점 %d / 안정 스캔 %d/3"), Points.Num(), FMath::Min(StableScans,3));
}
