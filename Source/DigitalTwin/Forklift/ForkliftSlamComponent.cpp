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


#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Forklift/ForkliftNavigationSensorComponent.h"


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

	MapWidth = FMath::Max(1, FMath::CeilToInt(MapSizeCm / FMath::Max(2.f, MapResolutionCm)));
	LogOddsMap.SetNumZeroed(MapWidth * MapWidth);
	bEverOccupiedCell.Init(0, LogOddsMap.Num());
	if (AActor* Owner = GetOwner())
	{
		SensorComponent = Owner->FindComponentByClass<UForkliftNavigationSensorComponent>();
		if (SensorComponent) PrimaryComponentTick.AddPrerequisite(SensorComponent, SensorComponent->PrimaryComponentTick);
	}
	if (!SensorComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FORKLIFT_SLAM] LiDAR component missing on %s"), *GetNameSafe(GetOwner()));
	}
}

void UForkliftSlamComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ConsumeLatestScan();
	if (bDrawMap || bDrawEstimatedPose) DrawSlamDebug();
}

int8 UForkliftSlamComponent::GetMapCellAt(const FVector2D& MapPositionCm) const
{
	int32 X = 0;
	int32 Y = 0;
	if (!MapPointToCell(MapPositionCm, X, Y) || !LogOddsMap.IsValidIndex(CellIndex(X, Y))) return 0;
	const int8 Value = LogOddsMap[CellIndex(X, Y)];
	if (Value >= FMath::Max(1, OccupiedLogOddsThreshold)) return 100;
	if (Value <= -FMath::Max(1, FreeLogOddsThreshold)) return 1;
	return 0;
}

bool UForkliftSlamComponent::MapPointToCell(const FVector2D& Position, int32& OutX, int32& OutY) const
{
	if (MapWidth <= 0 || MapResolutionCm <= 0.f) return false;
	const float ActualSizeCm = MapWidth * MapResolutionCm;
	OutX = FMath::FloorToInt((Position.X + ActualSizeCm * .5f) / MapResolutionCm);
	OutY = FMath::FloorToInt((Position.Y + ActualSizeCm * .5f) / MapResolutionCm);
	return OutX >= 0 && OutX < MapWidth && OutY >= 0 && OutY < MapWidth;
}

void UForkliftSlamComponent::ConsumeLatestScan()
{
	if (!SensorComponent) return;
	const FForkliftLidarScan& Scan = SensorComponent->GetLatestScan();
	if (Scan.SequenceNumber == 0 || Scan.SequenceNumber == ConsumedScanSequence) return;
	ConsumedScanSequence = Scan.SequenceNumber;
	TArray<FVector2D> BodyHits;
	TArray<FPlanarScanRay> Rays;
	ExtractBodyScanPoints(Scan, BodyHits, Rays);
	const FVector2D PreviousPosition = EstimatedPose.PositionMapCm;
	const float PreviousYaw = EstimatedPose.YawMapDegrees;
	FVector2D PredictedPosition = PreviousPosition + TranslationPriorPerScan;
	float PredictedYaw = PreviousYaw + YawPriorPerScanDegrees;
	int32 MatchedPoints = 0;
	float MatchedFraction = 0.f;
	bool bCanIntegrate = !EstimatedPose.bInitialized;
	if (!EstimatedPose.bInitialized)
	{
		// SLAM has no known starting world pose: define the first sensor body pose
		// as (0,0,0) in an arbitrary map frame.
		EstimatedPose.PositionMapCm = FVector2D::ZeroVector;
		EstimatedPose.YawMapDegrees = 0.f;
		EstimatedPose.bInitialized = true;
		EstimatedPose.bTrackingValid = false;
		bCanIntegrate = true;
	}
	else
	{
		FVector2D MatchedPosition = PreviousPosition;
		float MatchedYaw = PreviousYaw;
		const bool bMatched = MatchScanToMap(BodyHits, PredictedPosition, PredictedYaw, MatchedPosition, MatchedYaw, MatchedPoints, MatchedFraction);
		EstimatedPose.bTrackingValid = bMatched;
		EstimatedPose.MatchedReturnFraction = MatchedFraction;
		if (bMatched)
		{
			EstimatedPose.PositionMapCm = MatchedPosition;
			EstimatedPose.YawMapDegrees = MatchedYaw;
			TranslationPriorPerScan = (MatchedPosition - PreviousPosition) * .5f;
			YawPriorPerScanDegrees = FMath::FindDeltaAngleDegrees(PreviousYaw, MatchedYaw) * .5f;
			bCanIntegrate = true;
		}
		else
		{
			// Do not smear a scan into the map when localization confidence is low.
			TranslationPriorPerScan *= .5f;
			YawPriorPerScanDegrees *= .5f;
		}
	}
	EstimatedPose.TimestampSeconds = Scan.TimestampSeconds;
	EstimatedPose.ScanSequenceNumber = Scan.SequenceNumber;
	if (bCanIntegrate) IntegrateScan(Scan, Rays);
	LastProcessedTimestamp = static_cast<float>(Scan.TimestampSeconds);
	UE_LOG(LogTemp, Log, TEXT("[FORKLIFT_SLAM] scan=%llu pose=(%.1f,%.1f,%.1fdeg) match=%d matched=%.2f mapCells=%d"),
		Scan.SequenceNumber, EstimatedPose.PositionMapCm.X, EstimatedPose.PositionMapCm.Y, EstimatedPose.YawMapDegrees,
		EstimatedPose.bTrackingValid, EstimatedPose.MatchedReturnFraction, EverOccupiedCellIndices.Num());
}

void UForkliftSlamComponent::ExtractBodyScanPoints(const FForkliftLidarScan& Scan, TArray<FVector2D>& OutHits, TArray<FPlanarScanRay>& OutRays) const
{
	OutHits.Reset();
	OutRays.Reset();
	const float MaxVerticalSin = FMath::Sin(FMath::DegreesToRadians(MaximumRayElevationDegrees));
	for (const FLidarRaySample& Sample : Scan.Samples)
	{
		if (Sample.ReturnKind == ELidarReturnKind::Dropped || Sample.ReturnKind == ELidarReturnKind::SelfOccluded) continue;
		if (FMath::Abs(Sample.LocalDirection.Z) > MaxVerticalSin) continue;
		const bool bHit = Sample.ReturnKind == ELidarReturnKind::ExternalHit && Sample.bValid;
		const FVector SensorPoint = bHit ? Sample.LocalPointCm : Sample.LocalDirection * SensorComponent->MaximumRangeCm;
		const FVector BodyPoint = Scan.SensorToBody.TransformPosition(SensorPoint);
		if (bHit && BodyPoint.Z <= GroundPlaneZBodyCm + GroundPlaneToleranceCm) continue;
		FPlanarScanRay& Ray = OutRays.AddDefaulted_GetRef();
		Ray.EndBodyCm = FVector2D(BodyPoint.X, BodyPoint.Y);
		Ray.bOccupiedEndpoint = bHit;
		if (bHit) OutHits.Emplace(BodyPoint.X, BodyPoint.Y);
	}
	if (OutHits.Num() > MaximumScanPoints && MaximumScanPoints > 0)
	{
		const TArray<FVector2D> FullHits = MoveTemp(OutHits);
		OutHits.Reserve(MaximumScanPoints);
		for (int32 Index = 0; Index < MaximumScanPoints; ++Index)
		{
			const int32 SourceIndex = FMath::Min(FullHits.Num() - 1, Index * FullHits.Num() / MaximumScanPoints);
			OutHits.Add(FullHits[SourceIndex]);
		}
	}
}

bool UForkliftSlamComponent::MatchScanToMap(const TArray<FVector2D>& BodyHits, const FVector2D& PredictedPosition, float PredictedYawDegrees,
	FVector2D& OutPosition, float& OutYawDegrees, int32& OutMatchedPoints, float& OutMatchedFraction) const
{
	OutPosition = PredictedPosition;
	OutYawDegrees = PredictedYawDegrees;
	OutMatchedPoints = 0;
	OutMatchedFraction = 0.f;
	if (BodyHits.Num() < MinimumMatchedPoints || LogOddsMap.IsEmpty()) return false;
	const int32 TranslationSteps = FMath::Max(1, FMath::CeilToInt(SearchTranslationCm / FMath::Max(1.f, TranslationStepCm)));
	const int32 YawSteps = FMath::Max(1, FMath::CeilToInt(SearchYawDegrees / FMath::Max(.25f, YawStepDegrees)));
	float BestScore = -FLT_MAX;
	float BestPriorDistance = FLT_MAX;
	for (int32 YawIndex = -YawSteps; YawIndex <= YawSteps; ++YawIndex)
	{
		const float CandidateYaw = PredictedYawDegrees + YawIndex * YawStepDegrees;
		const float Radians = FMath::DegreesToRadians(CandidateYaw);
		const float CosYaw = FMath::Cos(Radians);
		const float SinYaw = FMath::Sin(Radians);
		for (int32 YIndex = -TranslationSteps; YIndex <= TranslationSteps; ++YIndex)
		{
			for (int32 XIndex = -TranslationSteps; XIndex <= TranslationSteps; ++XIndex)
			{
				const FVector2D CandidatePosition = PredictedPosition + FVector2D(XIndex * TranslationStepCm, YIndex * TranslationStepCm);
				int32 CandidateMatches = 0;
				float CandidateScore = 0.f;
				for (const FVector2D& BodyPoint : BodyHits)
				{
					const FVector2D MapPoint(CandidatePosition.X + CosYaw * BodyPoint.X - SinYaw * BodyPoint.Y,
						CandidatePosition.Y + SinYaw * BodyPoint.X + CosYaw * BodyPoint.Y);
					int32 CellX = 0;
					int32 CellY = 0;
					if (!MapPointToCell(MapPoint, CellX, CellY)) { CandidateScore -= .25f; continue; }
					const int8 LogOdds = LogOddsMap[CellIndex(CellX, CellY)];
					if (LogOdds >= OccupiedLogOddsThreshold) { CandidateScore += 1.f; ++CandidateMatches; }
					else if (LogOdds <= -FreeLogOddsThreshold) CandidateScore -= .20f;
				}
				const float YawOffsetCm = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(PredictedYawDegrees, CandidateYaw)) * 50.f;
				const float PriorDistance = FVector2D::DistSquared(CandidatePosition, PredictedPosition) + FMath::Square(YawOffsetCm);
				if (CandidateScore > BestScore || (FMath::IsNearlyEqual(CandidateScore, BestScore) && PriorDistance < BestPriorDistance))
				{
					BestScore = CandidateScore;
					BestPriorDistance = PriorDistance;
					OutPosition = CandidatePosition;
					OutYawDegrees = CandidateYaw;
					OutMatchedPoints = CandidateMatches;
				}
			}
		}
	}
	OutMatchedFraction = static_cast<float>(OutMatchedPoints) / static_cast<float>(BodyHits.Num());
	return OutMatchedPoints >= MinimumMatchedPoints && OutMatchedFraction >= MinimumMatchedFraction;
}

void UForkliftSlamComponent::IntegrateScan(const FForkliftLidarScan& Scan, const TArray<FPlanarScanRay>& Rays)
{
	const float YawRadians = FMath::DegreesToRadians(EstimatedPose.YawMapDegrees);
	const float CosYaw = FMath::Cos(YawRadians);
	const float SinYaw = FMath::Sin(YawRadians);
	const FVector SensorOriginBody = Scan.SensorToBody.GetLocation();
	const FVector2D SensorOriginMap(EstimatedPose.PositionMapCm.X + CosYaw * SensorOriginBody.X - SinYaw * SensorOriginBody.Y,
		EstimatedPose.PositionMapCm.Y + SinYaw * SensorOriginBody.X + CosYaw * SensorOriginBody.Y);
	for (const FPlanarScanRay& Ray : Rays)
	{
		const FVector2D EndMap(EstimatedPose.PositionMapCm.X + CosYaw * Ray.EndBodyCm.X - SinYaw * Ray.EndBodyCm.Y,
			EstimatedPose.PositionMapCm.Y + SinYaw * Ray.EndBodyCm.X + CosYaw * Ray.EndBodyCm.Y);
		TraceMapRay(SensorOriginMap, EndMap, Ray.bOccupiedEndpoint);
	}
}

void UForkliftSlamComponent::ApplyEvidence(int32 Index, int8 Delta)
{
	if (!LogOddsMap.IsValidIndex(Index)) return;
	const int8 OldValue = LogOddsMap[Index];
	const int8 NewValue = static_cast<int8>(FMath::Clamp(static_cast<int32>(OldValue) + Delta, -20, 20));
	LogOddsMap[Index] = NewValue;
	if (OldValue < OccupiedLogOddsThreshold && NewValue >= OccupiedLogOddsThreshold && !bEverOccupiedCell[Index])
	{
		bEverOccupiedCell[Index] = 1;
		EverOccupiedCellIndices.Add(Index);
	}
}

void UForkliftSlamComponent::TraceMapRay(const FVector2D& Start, const FVector2D& End, bool bOccupiedEndpoint)
{
	int32 StartX = 0;
	int32 StartY = 0;
	if (!MapPointToCell(Start, StartX, StartY)) return;
	int32 EndX = 0;
	int32 EndY = 0;
	FVector2D ClippedEnd = End;
	const bool bEndpointInside = MapPointToCell(End, EndX, EndY);
	if (!bEndpointInside)
	{
		const float HalfSize = MapWidth * MapResolutionCm * .5f;
		const FVector2D Delta = End - Start;
		float ScaleToEdge = 1.f;
		if (FMath::Abs(Delta.X) > SMALL_NUMBER)
		{
			const float DistanceToEdge = Delta.X > 0.f ? HalfSize - Start.X : HalfSize + Start.X;
			ScaleToEdge = FMath::Min(ScaleToEdge, DistanceToEdge / FMath::Abs(Delta.X));
		}
		if (FMath::Abs(Delta.Y) > SMALL_NUMBER)
		{
			const float DistanceToEdge = Delta.Y > 0.f ? HalfSize - Start.Y : HalfSize + Start.Y;
			ScaleToEdge = FMath::Min(ScaleToEdge, DistanceToEdge / FMath::Abs(Delta.Y));
		}
		ClippedEnd = Start + Delta * FMath::Max(0.f, ScaleToEdge - KINDA_SMALL_NUMBER);
		if (!MapPointToCell(ClippedEnd, EndX, EndY)) return;
	}
	const int32 DeltaX = FMath::Abs(EndX - StartX);
	const int32 StepX = StartX < EndX ? 1 : -1;
	const int32 DeltaY = -FMath::Abs(EndY - StartY);
	const int32 StepY = StartY < EndY ? 1 : -1;
	int32 Error = DeltaX + DeltaY;
	int32 X = StartX;
	int32 Y = StartY;
	while (true)
	{
		const bool bAtEnd = X == EndX && Y == EndY;
		if (!bAtEnd || !bEndpointInside || !bOccupiedEndpoint) ApplyEvidence(CellIndex(X, Y), -1);
		if (bAtEnd) break;
		const int32 TwiceError = 2 * Error;
		if (TwiceError >= DeltaY) { Error += DeltaY; X += StepX; }
		if (TwiceError <= DeltaX) { Error += DeltaX; Y += StepY; }
	}
	if (bEndpointInside && bOccupiedEndpoint) ApplyEvidence(CellIndex(EndX, EndY), 3);
}

void UForkliftSlamComponent::DrawSlamDebug() const
{
	UWorld* World = GetWorld();
	if (!World || !EstimatedPose.bInitialized) return;
	if (bDrawMap)
	{
		const int32 Stride = FMath::Max(1, DebugCellStride);
		for (int32 Index : EverOccupiedCellIndices)
		{
			if (!LogOddsMap.IsValidIndex(Index) || LogOddsMap[Index] < OccupiedLogOddsThreshold || Index % Stride != 0) continue;
			const int32 X = Index % MapWidth;
			const int32 Y = Index / MapWidth;
			const float ActualSize = MapWidth * MapResolutionCm;
			const FVector MapPoint((X + .5f) * MapResolutionCm - ActualSize * .5f, (Y + .5f) * MapResolutionCm - ActualSize * .5f, 10.f);
			DrawDebugPoint(World, MapToWorldForDebug.TransformPosition(MapPoint), 7.f, FColor::Red, false, 0.f, 0);
		}
	}
	if (bDrawEstimatedPose)
	{
		const FVector EstimatedMapPoint(EstimatedPose.PositionMapCm.X, EstimatedPose.PositionMapCm.Y, 20.f);
		const FVector WorldPoint = MapToWorldForDebug.TransformPosition(EstimatedMapPoint);
		const FVector MapForward(FMath::Cos(FMath::DegreesToRadians(EstimatedPose.YawMapDegrees)), FMath::Sin(FMath::DegreesToRadians(EstimatedPose.YawMapDegrees)), 0.f);
		DrawDebugCoordinateSystem(World, WorldPoint, MapToWorldForDebug.TransformRotation(FRotator(0.f, EstimatedPose.YawMapDegrees, 0.f).Quaternion()).Rotator(), 40.f, false, 0.f, 0, 1.5f);
		DrawDebugDirectionalArrow(World, WorldPoint, MapToWorldForDebug.TransformPosition(EstimatedMapPoint + MapForward * 100.f), 16.f,
			EstimatedPose.bTrackingValid ? FColor::Green : FColor::Yellow, false, 0.f, 0, 2.f);
	}

}
