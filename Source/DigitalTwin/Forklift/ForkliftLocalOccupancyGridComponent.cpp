#include "Forklift/ForkliftLocalOccupancyGridComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Forklift/ForkliftNavigationSensorComponent.h"

UForkliftLocalOccupancyGridComponent::UForkliftLocalOccupancyGridComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UForkliftLocalOccupancyGridComponent::BeginPlay()
{
	Super::BeginPlay();
	const float SafeCellSize = FMath::Max(2.f, CellSizeCm);
	GridWidth = FMath::Max(1, FMath::CeilToInt(GridSizeCm / SafeCellSize));
	GridHeight = GridWidth;
	Cells.SetNum(GridWidth * GridHeight);
	ClearGrid();
	if (AActor* Owner = GetOwner())
	{
		SensorComponent = Owner->FindComponentByClass<UForkliftNavigationSensorComponent>();
		if (SensorComponent) PrimaryComponentTick.AddPrerequisite(SensorComponent, SensorComponent->PrimaryComponentTick);
	}
	if (!SensorComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[FORKLIFT_LOCAL_GRID] LiDAR component missing on %s"), *GetNameSafe(GetOwner()));
	}
}

void UForkliftLocalOccupancyGridComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ConsumeLatestScan();
	if (bDrawOccupiedCells) DrawGridDebug();
}

int8 UForkliftLocalOccupancyGridComponent::GetCellAtBodyLocation(const FVector2D& BodyLocationCm) const
{
	int32 X = 0;
	int32 Y = 0;
	return BodyPointToCell(BodyLocationCm, X, Y) && Cells.IsValidIndex(CellIndex(X, Y)) ? Cells[CellIndex(X, Y)] : -1;
}

void UForkliftLocalOccupancyGridComponent::ClearGrid()
{
	for (int8& Cell : Cells) Cell = -1;
	ObstacleObservations.Reset();
}

bool UForkliftLocalOccupancyGridComponent::BodyPointToCell(const FVector2D& Point, int32& OutX, int32& OutY) const
{
	if (GridWidth <= 0 || CellSizeCm <= 0.f) return false;
	const float ActualSize = GridWidth * CellSizeCm;
	OutX = FMath::FloorToInt((Point.X + ActualSize * .5f) / CellSizeCm);
	OutY = FMath::FloorToInt((Point.Y + ActualSize * .5f) / CellSizeCm);
	return OutX >= 0 && OutX < GridWidth && OutY >= 0 && OutY < GridHeight;
}

void UForkliftLocalOccupancyGridComponent::TraceFreeCells(int32 X0, int32 Y0, int32 X1, int32 Y1, bool bLeaveEndpointUnknown)
{
	const int32 DeltaX = FMath::Abs(X1 - X0);
	const int32 StepX = X0 < X1 ? 1 : -1;
	const int32 DeltaY = -FMath::Abs(Y1 - Y0);
	const int32 StepY = Y0 < Y1 ? 1 : -1;
	int32 Error = DeltaX + DeltaY;
	int32 X = X0;
	int32 Y = Y0;
	while (true)
	{
		const bool bAtEndpoint = X == X1 && Y == Y1;
		if ((!bAtEndpoint || !bLeaveEndpointUnknown) && X >= 0 && X < GridWidth && Y >= 0 && Y < GridHeight)
		{
			int8& Cell = Cells[CellIndex(X, Y)];
			if (Cell != 100) Cell = 0;
		}
		if (bAtEndpoint) break;
		const int32 TwiceError = 2 * Error;
		if (TwiceError >= DeltaY) { Error += DeltaY; X += StepX; }
		if (TwiceError <= DeltaX) { Error += DeltaX; Y += StepY; }
	}
}

void UForkliftLocalOccupancyGridComponent::ConsumeLatestScan()
{
	if (!SensorComponent) return;
	const FForkliftLidarScan& Scan = SensorComponent->GetLatestScan();
	if (Scan.SequenceNumber == 0 || Scan.SequenceNumber == ConsumedScanSequence) return;
	ConsumedScanSequence = Scan.SequenceNumber;
	ClearGrid();
	int32 SensorOriginX = 0;
	int32 SensorOriginY = 0;
	const FVector SensorOriginBody = Scan.SensorToBody.GetLocation();
	if (!BodyPointToCell(FVector2D(SensorOriginBody.X, SensorOriginBody.Y), SensorOriginX, SensorOriginY)) return;
	for (const FLidarRaySample& Sample : Scan.Samples)
	{
		if (Sample.ReturnKind == ELidarReturnKind::Dropped || Sample.ReturnKind == ELidarReturnKind::SelfOccluded) continue;
		const float RangeCm = Sample.ReturnKind == ELidarReturnKind::ExternalHit && Sample.bValid
			? Sample.DistanceCm : SensorComponent->MaximumRangeCm;
		const FVector EndpointSensor = Sample.ReturnKind == ELidarReturnKind::ExternalHit && Sample.bValid
			? Sample.LocalPointCm : Sample.LocalDirection * RangeCm;
		const FVector EndpointBody = Scan.SensorToBody.TransformPosition(EndpointSensor);
		const float ElevationDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Sample.LocalDirection.Z, -1.f, 1.f)));
		if (FMath::Abs(ElevationDegrees) > MaximumRayElevationDegrees) continue;
		if (Sample.ReturnKind == ELidarReturnKind::ExternalHit && EndpointBody.Z <= GroundPlaneZBodyCm + GroundPlaneToleranceCm) continue;
		int32 EndX = 0;
		int32 EndY = 0;
		const FVector2D SensorOrigin2D(SensorOriginBody.X, SensorOriginBody.Y);
		FVector2D Endpoint2D(EndpointBody.X, EndpointBody.Y);
		const float ActualGridSize = GridWidth * CellSizeCm;
		const float HalfGridSize = ActualGridSize * .5f;
		const bool bEndpointInside = BodyPointToCell(Endpoint2D, EndX, EndY);
		if (!bEndpointInside)
		{
			const FVector2D Delta = Endpoint2D - SensorOrigin2D;
			float ScaleToEdge = 1.f;
			if (FMath::Abs(Delta.X) > SMALL_NUMBER)
			{
				const float DistanceToEdge = Delta.X > 0.f ? HalfGridSize - SensorOrigin2D.X : HalfGridSize + SensorOrigin2D.X;
				ScaleToEdge = FMath::Min(ScaleToEdge, DistanceToEdge / FMath::Abs(Delta.X));
			}
			if (FMath::Abs(Delta.Y) > SMALL_NUMBER)
			{
				const float DistanceToEdge = Delta.Y > 0.f ? HalfGridSize - SensorOrigin2D.Y : HalfGridSize + SensorOrigin2D.Y;
				ScaleToEdge = FMath::Min(ScaleToEdge, DistanceToEdge / FMath::Abs(Delta.Y));
			}
			Endpoint2D = SensorOrigin2D + Delta * FMath::Max(0.f, ScaleToEdge - KINDA_SMALL_NUMBER);
			if (!BodyPointToCell(Endpoint2D, EndX, EndY)) continue;
		}
		const bool bHit = bEndpointInside && Sample.ReturnKind == ELidarReturnKind::ExternalHit && Sample.bValid;
		TraceFreeCells(SensorOriginX, SensorOriginY, EndX, EndY, bHit);
		if (bHit) Cells[CellIndex(EndX, EndY)] = 100;
	}
	ExtractObstacleClusters();
	int32 KnownCellCount = 0;
	for (const int8 Cell : Cells) if (Cell >= 0) ++KnownCellCount;
	UE_LOG(LogTemp, VeryVerbose, TEXT("[FORKLIFT_LOCAL_GRID] scan=%llu known=%d obstacles=%d"), Scan.SequenceNumber, KnownCellCount, ObstacleObservations.Num());
}

void UForkliftLocalOccupancyGridComponent::ExtractObstacleClusters()
{
	TArray<uint8> Visited;
	Visited.Init(0, Cells.Num());
	TArray<int32> Pending;
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		if (Cells[Index] != 100 || Visited[Index]) continue;
		Pending.Reset();
		Pending.Add(Index);
		Visited[Index] = 1;
		FVector2D Sum = FVector2D::ZeroVector;
		FVector2D Minimum(FLT_MAX, FLT_MAX);
		FVector2D Maximum(-FLT_MAX, -FLT_MAX);
		int32 Count = 0;
		while (!Pending.IsEmpty())
		{
			const int32 Current = Pending.Pop(EAllowShrinking::No);
			const int32 X = Current % GridWidth;
			const int32 Y = Current / GridWidth;
			const float ActualSize = GridWidth * CellSizeCm;
			const FVector2D Center((X + .5f) * CellSizeCm - ActualSize * .5f, (Y + .5f) * CellSizeCm - ActualSize * .5f);
			Sum += Center;
			Minimum.X = FMath::Min(Minimum.X, Center.X - CellSizeCm * .5f);
			Minimum.Y = FMath::Min(Minimum.Y, Center.Y - CellSizeCm * .5f);
			Maximum.X = FMath::Max(Maximum.X, Center.X + CellSizeCm * .5f);
			Maximum.Y = FMath::Max(Maximum.Y, Center.Y + CellSizeCm * .5f);
			++Count;
			for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
			{
				for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
				{
					const int32 NeighborX = X + OffsetX;
					const int32 NeighborY = Y + OffsetY;
					if ((OffsetX == 0 && OffsetY == 0) || NeighborX < 0 || NeighborY < 0 || NeighborX >= GridWidth || NeighborY >= GridHeight) continue;
					const int32 Neighbor = CellIndex(NeighborX, NeighborY);
					if (Cells[Neighbor] == 100 && !Visited[Neighbor]) { Visited[Neighbor] = 1; Pending.Add(Neighbor); }
				}
			}
		}
		if (Count < FMath::Max(1, MinimumClusterCells)) continue;
		FForkliftObstacleObservation& Observation = ObstacleObservations.AddDefaulted_GetRef();
		Observation.CenterBodyCm = Sum / static_cast<float>(Count);
		Observation.BoundsMinBodyCm = Minimum;
		Observation.BoundsMaxBodyCm = Maximum;
		Observation.OccupiedCellCount = Count;
	}
}

void UForkliftLocalOccupancyGridComponent::DrawGridDebug() const
{
	UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner) return;
	const FTransform BodyWorld = Owner->GetActorTransform();
	const int32 Stride = FMath::Max(1, DebugCellStride);
	for (int32 Y = 0; Y < GridHeight; Y += Stride)
	{
		for (int32 X = 0; X < GridWidth; X += Stride)
		{
			if (Cells.IsValidIndex(CellIndex(X, Y)) && Cells[CellIndex(X, Y)] == 100)
			{
				const float ActualSize = GridWidth * CellSizeCm;
				const FVector BodyPoint((X + .5f) * CellSizeCm - ActualSize * .5f, (Y + .5f) * CellSizeCm - ActualSize * .5f, 15.f);
				DrawDebugPoint(World, BodyWorld.TransformPosition(BodyPoint), 8.f, FColor::Red, false, 0.f, 0);
			}
		}
	}
}
