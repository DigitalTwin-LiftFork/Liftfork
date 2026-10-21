#include "Forklift/ForkliftGlobalPathPlannerComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Forklift/AutomatedForklift.h"
#include "Forklift/ForkliftSlamComponent.h"
#include "Algo/Reverse.h"

#include <queue>
#include <vector>

namespace
{
	struct FOpenNode
	{
		int32 State = INDEX_NONE;
		float FScore = 0.f;
	};
	struct FOpenNodeCompare
	{
		bool operator()(const FOpenNode& A, const FOpenNode& B) const { return A.FScore > B.FScore; }
	};

	constexpr int32 DirectionX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
	constexpr int32 DirectionY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
	constexpr float DirectionCost[8] = { 1.f, 1.41421356f, 1.f, 1.41421356f, 1.f, 1.41421356f, 1.f, 1.41421356f };
}

UForkliftGlobalPathPlannerComponent::UForkliftGlobalPathPlannerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UForkliftGlobalPathPlannerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		SlamComponent = Owner->FindComponentByClass<UForkliftSlamComponent>();
		if (AAutomatedForklift* Forklift = Cast<AAutomatedForklift>(Owner))
		{
			const float HalfLength = Forklift->Dimensions.OverallLengthCm * .5f;
			const float HalfWidth = Forklift->Dimensions.MaximumWidthCm * .5f;
			// Circumscribed radius safely covers the complete body at any heading.
			FootprintRadiusCm = FMath::Sqrt(HalfLength * HalfLength + HalfWidth * HalfWidth) + SafetyMarginCm;
		}
	}
	if (SlamComponent) PrimaryComponentTick.AddPrerequisite(SlamComponent, SlamComponent->PrimaryComponentTick);
	else SetPlannerStatus(TEXT("SLAM component missing"));
}

void UForkliftGlobalPathPlannerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!SlamComponent) return;
	RetryElapsedSeconds += DeltaTime;
	const uint64 ScanSequence = SlamComponent->GetConsumedScanSequence();
	if (bPlanOnBeginPlay && ScanSequence > 0 && ScanSequence != LastSeenScanSequence)
	{
		LastSeenScanSequence = ScanSequence;
		if (RetryElapsedSeconds >= 1.0 && LastAttemptedScanSequence != ScanSequence)
		{
			RetryElapsedSeconds = 0.0;
			RequestPathPlan();
		}
	}
	if (bDrawPath && PlannedPath.Num() > 1) DrawPlannedRoute();
}

bool UForkliftGlobalPathPlannerComponent::SetGoalAndPlan(FVector2D NewGoalMapCm, float NewGoalYawMapDegrees)
{
	GoalMapCm = NewGoalMapCm;
	GoalYawMapDegrees = FMath::UnwindDegrees(NewGoalYawMapDegrees);
	return RequestPathPlan();
}

bool UForkliftGlobalPathPlannerComponent::RequestPathPlan()
{
	if (!SlamComponent)
	{
		SetPlannerStatus(TEXT("SLAM component missing"));
		return false;
	}
	const FForkliftSlamPose& Pose = SlamComponent->EstimatedPose;
	if (!Pose.bInitialized || !Pose.bTrackingValid)
	{
		LastAttemptedScanSequence = SlamComponent->GetConsumedScanSequence();
		PlannedPath.Reset();
		SetPlannerStatus(TEXT("Waiting: SLAM pose is not tracking-valid"));
		return false;
	}

	const float Resolution = SlamComponent->MapResolutionCm;
	const int32 Width = SlamComponent->GetMapWidth();
	if (Resolution <= 0.f || Width <= 0)
	{
		SetPlannerStatus(TEXT("SLAM map is not initialized"));
		return false;
	}
	const float HalfMapSize = Width * Resolution * .5f;
	const auto WorldToCell = [Resolution, HalfMapSize, Width](const FVector2D& Point, int32& OutX, int32& OutY)
	{
		OutX = FMath::FloorToInt((Point.X + HalfMapSize) / Resolution);
		OutY = FMath::FloorToInt((Point.Y + HalfMapSize) / Resolution);
		return OutX >= 0 && OutX < Width && OutY >= 0 && OutY < Width;
	};
	int32 StartX = 0, StartY = 0, GoalX = 0, GoalY = 0;
	if (!WorldToCell(Pose.PositionMapCm, StartX, StartY) || !WorldToCell(GoalMapCm, GoalX, GoalY))
	{
		LastAttemptedScanSequence = SlamComponent->GetConsumedScanSequence();
		PlannedPath.Reset();
		SetPlannerStatus(TEXT("Start or goal lies outside the SLAM map"));
		return false;
	}
	const float DeltaX = FMath::Abs(GoalMapCm.X - Pose.PositionMapCm.X);
	const float DeltaY = FMath::Abs(GoalMapCm.Y - Pose.PositionMapCm.Y);
	const float Padding = FMath::Max(SearchPaddingCm, FootprintRadiusCm + Resolution * 2.f);
	const float SearchWidthCm = DeltaX + 2.f * Padding;
	const float SearchHeightCm = DeltaY + 2.f * Padding;
	if (SearchWidthCm > MaximumSearchAreaCm || SearchHeightCm > MaximumSearchAreaCm)
	{
		LastAttemptedScanSequence = SlamComponent->GetConsumedScanSequence();
		PlannedPath.Reset();
		SetPlannerStatus(TEXT("Search window exceeds MaximumSearchAreaCm"));
		return false;
	}

	TArray<FIntPoint> CellPath;
	const bool bRouteFound = BuildAStarPath(StartX, StartY, GoalX, GoalY, Width, Resolution, CellPath);
	LastAttemptedScanSequence = SlamComponent->GetConsumedScanSequence();
	PlannedPath.Reset();
	if (!bRouteFound)
	{
		SetPlannerStatus(TEXT("No route through known free space; unknown/occupied cells are blocked"));
		return false;
	}

	PlannedPath.Reserve(CellPath.Num());
	for (int32 Index = 0; Index < CellPath.Num(); ++Index)
	{
		const FIntPoint Cell = CellPath[Index];
		const FVector2D Position((Cell.X + .5f) * Resolution - HalfMapSize, (Cell.Y + .5f) * Resolution - HalfMapSize);
		float Yaw = GoalYawMapDegrees;
		if (Index + 1 < CellPath.Num())
		{
			const FIntPoint Next = CellPath[Index + 1];
			Yaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Next.Y - Cell.Y), static_cast<float>(Next.X - Cell.X)));
		}
		FForkliftGlobalPathWaypoint& Waypoint = PlannedPath.AddDefaulted_GetRef();
		Waypoint.PositionMapCm = Position;
		Waypoint.YawMapDegrees = Yaw;
	}
	SetPlannerStatus(FString::Printf(TEXT("Path ready: %d waypoints; goal yaw %.1f deg"), PlannedPath.Num(), GoalYawMapDegrees));
	return true;
}

bool UForkliftGlobalPathPlannerComponent::BuildAStarPath(int32 StartX, int32 StartY, int32 GoalX, int32 GoalY, int32 MapWidth, float ResolutionCm, TArray<FIntPoint>& OutCells)
{
	OutCells.Reset();
	ExpandedNodeCount = 0;
	const float HalfMapSize = MapWidth * ResolutionCm * .5f;
	const int32 PadCells = FMath::Max(2, FMath::CeilToInt(FMath::Max(SearchPaddingCm, FootprintRadiusCm + ResolutionCm * 2.f) / ResolutionCm));
	const int32 MinX = FMath::Max(0, FMath::Min(StartX, GoalX) - PadCells);
	const int32 MaxX = FMath::Min(MapWidth - 1, FMath::Max(StartX, GoalX) + PadCells);
	const int32 MinY = FMath::Max(0, FMath::Min(StartY, GoalY) - PadCells);
	const int32 MaxY = FMath::Min(MapWidth - 1, FMath::Max(StartY, GoalY) + PadCells);
	const int32 LocalWidth = MaxX - MinX + 1;
	const int32 LocalHeight = MaxY - MinY + 1;
	const int64 LocalCount64 = static_cast<int64>(LocalWidth) * LocalHeight;
	if (LocalCount64 <= 0 || LocalCount64 > 1000000) return false;
	const int32 LocalCount = static_cast<int32>(LocalCount64);
	const auto LocalIndex = [LocalWidth](int32 X, int32 Y) { return Y * LocalWidth + X; };
	const auto ToLocal = [MinX, MinY, LocalWidth, LocalHeight](int32 X, int32 Y, int32& OutIndex)
	{
		const int32 LX = X - MinX, LY = Y - MinY;
		if (LX < 0 || LY < 0 || LX >= LocalWidth || LY >= LocalHeight) return false;
		OutIndex = LY * LocalWidth + LX;
		return true;
	};
	TArray<uint8> bBlocked;
	bBlocked.SetNumUninitialized(LocalCount);
	const auto ToMapPosition = [ResolutionCm, HalfMapSize](int32 X, int32 Y)
	{
		return FVector2D((X + .5f) * ResolutionCm - HalfMapSize, (Y + .5f) * ResolutionCm - HalfMapSize);
	};
	for (int32 Y = 0; Y < LocalHeight; ++Y)
	{
		for (int32 X = 0; X < LocalWidth; ++X)
		{
			const FVector2D MapPosition = ToMapPosition(MinX + X, MinY + Y);
			bBlocked[LocalIndex(X, Y)] = SlamComponent->GetMapCellAt(MapPosition) != 1 ? 1 : 0;
		}
	}
	const auto CellBlocked = [&bBlocked, &ToLocal](int32 X, int32 Y)
	{
		int32 Index = INDEX_NONE;
		return !ToLocal(X, Y, Index) || bBlocked[Index] != 0;
	};
	if (CellBlocked(StartX, StartY) || CellBlocked(GoalX, GoalY)) return false;

	// Two-pass chamfer distance transform inflates unknown/occupied cells by the
	// forklift's circumscribed body radius plus a configurable safety margin.
	const float Infinity = TNumericLimits<float>::Max() * .25f;
	TArray<float> Clearance;
	Clearance.SetNum(LocalCount);
	for (int32 Index = 0; Index < LocalCount; ++Index) Clearance[Index] = bBlocked[Index] ? 0.f : Infinity;
	const float DiagonalCost = 1.41421356f;
	for (int32 Y = 0; Y < LocalHeight; ++Y) for (int32 X = 0; X < LocalWidth; ++X)
	{
		const int32 Index = LocalIndex(X, Y);
		float& Value = Clearance[Index];
		if (X > 0) Value = FMath::Min(Value, Clearance[LocalIndex(X - 1, Y)] + 1.f);
		if (Y > 0) Value = FMath::Min(Value, Clearance[LocalIndex(X, Y - 1)] + 1.f);
		if (X > 0 && Y > 0) Value = FMath::Min(Value, Clearance[LocalIndex(X - 1, Y - 1)] + DiagonalCost);
		if (X + 1 < LocalWidth && Y > 0) Value = FMath::Min(Value, Clearance[LocalIndex(X + 1, Y - 1)] + DiagonalCost);
	}
	for (int32 Y = LocalHeight - 1; Y >= 0; --Y) for (int32 X = LocalWidth - 1; X >= 0; --X)
	{
		const int32 Index = LocalIndex(X, Y);
		float& Value = Clearance[Index];
		if (X + 1 < LocalWidth) Value = FMath::Min(Value, Clearance[LocalIndex(X + 1, Y)] + 1.f);
		if (Y + 1 < LocalHeight) Value = FMath::Min(Value, Clearance[LocalIndex(X, Y + 1)] + 1.f);
		if (X + 1 < LocalWidth && Y + 1 < LocalHeight) Value = FMath::Min(Value, Clearance[LocalIndex(X + 1, Y + 1)] + DiagonalCost);
		if (X > 0 && Y + 1 < LocalHeight) Value = FMath::Min(Value, Clearance[LocalIndex(X - 1, Y + 1)] + DiagonalCost);
	}
	for (int32 Index = 0; Index < LocalCount; ++Index)
	{
		if (Clearance[Index] * ResolutionCm < FootprintRadiusCm) bBlocked[Index] = 1;
	}
	if (CellBlocked(StartX, StartY) || CellBlocked(GoalX, GoalY)) return false;

	const int32 StartLocalIndex = LocalIndex(StartX - MinX, StartY - MinY);
	const int32 GoalLocalIndex = LocalIndex(GoalX - MinX, GoalY - MinY);
	constexpr int32 HeadingCount = 8;
	const int32 StateCount = LocalCount * HeadingCount;
	if (StateCount > 8000000) return false;
	const int32 StartHeading = ((FMath::RoundToInt(FMath::UnwindDegrees(SlamComponent->EstimatedPose.YawMapDegrees) / 45.f) % 8) + 8) % 8;
	const int32 StartState = StartLocalIndex * HeadingCount + StartHeading;
	const int32 GoalHeading = ((FMath::RoundToInt(FMath::UnwindDegrees(GoalYawMapDegrees) / 45.f) % 8) + 8) % 8;
	TArray<float> GScore;
	GScore.Init(Infinity, StateCount);
	TArray<int32> CameFrom;
	CameFrom.Init(INDEX_NONE, StateCount);
	std::priority_queue<FOpenNode, std::vector<FOpenNode>, FOpenNodeCompare> Open;
	auto Heuristic = [GoalX, GoalY](int32 X, int32 Y)
	{
		const int32 DX = FMath::Abs(GoalX - X), DY = FMath::Abs(GoalY - Y);
		return static_cast<float>(FMath::Max(DX, DY)) + .41421356f * static_cast<float>(FMath::Min(DX, DY));
	};
	GScore[StartState] = 0.f;
	Open.push({ StartState, Heuristic(StartX, StartY) });
	int32 GoalState = INDEX_NONE;
	const float TurnCostPer45 = TurnPenaltyCmPerRadian * PI / 4.f / ResolutionCm;
	while (!Open.empty())
	{
		const FOpenNode Current = Open.top();
		Open.pop();
		if (Current.FScore > GScore[Current.State] + Heuristic(MinX + (Current.State / HeadingCount) % LocalWidth, MinY + (Current.State / HeadingCount) / LocalWidth) + KINDA_SMALL_NUMBER) continue;
		const int32 CurrentCell = Current.State / HeadingCount;
		const int32 CurrentHeading = Current.State % HeadingCount;
		const int32 LocalX = CurrentCell % LocalWidth, LocalY = CurrentCell / LocalWidth;
		const int32 MapX = MinX + LocalX, MapY = MinY + LocalY;
		++ExpandedNodeCount;
		if (CurrentCell == GoalLocalIndex && CurrentHeading == GoalHeading)
		{
			GoalState = Current.State;
			break;
		}
		for (int32 Direction = 0; Direction < HeadingCount; ++Direction)
		{
			const int32 NextX = MapX + DirectionX[Direction], NextY = MapY + DirectionY[Direction];
			int32 NextLocalIndex = INDEX_NONE;
			if (!ToLocal(NextX, NextY, NextLocalIndex) || bBlocked[NextLocalIndex]) continue;
			if (DirectionX[Direction] != 0 && DirectionY[Direction] != 0 &&
				(CellBlocked(MapX + DirectionX[Direction], MapY) || CellBlocked(MapX, MapY + DirectionY[Direction]))) continue;
			const int32 HeadingDelta = FMath::Min((Direction - CurrentHeading + 8) % 8, (CurrentHeading - Direction + 8) % 8);
			const float TurnPenalty = TurnCostPer45 * HeadingDelta;
			const float CandidateG = GScore[Current.State] + DirectionCost[Direction] + TurnPenalty;
			const int32 NextState = NextLocalIndex * HeadingCount + Direction;
			if (CandidateG + KINDA_SMALL_NUMBER >= GScore[NextState]) continue;
			GScore[NextState] = CandidateG;
			CameFrom[NextState] = Current.State;
			Open.push({ NextState, CandidateG + Heuristic(NextX, NextY) });
		}
	}
	if (GoalState == INDEX_NONE) return false;
	TArray<int32> ReversedStates;
	for (int32 State = GoalState; State != INDEX_NONE; State = CameFrom[State]) ReversedStates.Add(State);
	Algo::Reverse(ReversedStates);
	OutCells.Reserve(ReversedStates.Num());
	for (int32 State : ReversedStates)
	{
		const int32 LocalCell = State / HeadingCount;
		OutCells.Emplace(MinX + LocalCell % LocalWidth, MinY + LocalCell / LocalWidth);
	}
	return !OutCells.IsEmpty();
}

void UForkliftGlobalPathPlannerComponent::SetPlannerStatus(const FString& NewStatus)
{
	PlannerStatus = NewStatus;
	if (LastLoggedStatus != NewStatus)
	{
		LastLoggedStatus = NewStatus;
		UE_LOG(LogTemp, Log, TEXT("[FORKLIFT_GLOBAL_PLANNER] %s status='%s' expanded=%d"), *GetNameSafe(GetOwner()), *PlannerStatus, ExpandedNodeCount);
	}
}

void UForkliftGlobalPathPlannerComponent::DrawPlannedRoute() const
{
	if (!GetWorld() || !SlamComponent) return;
	for (int32 Index = 1; Index < PlannedPath.Num(); ++Index)
	{
		const FVector2D A = PlannedPath[Index - 1].PositionMapCm;
		const FVector2D B = PlannedPath[Index].PositionMapCm;
		const FVector WorldA = SlamComponent->MapToWorldForDebug.TransformPosition(FVector(A.X, A.Y, DebugPathZOffsetCm));
		const FVector WorldB = SlamComponent->MapToWorldForDebug.TransformPosition(FVector(B.X, B.Y, DebugPathZOffsetCm));
		DrawDebugLine(GetWorld(), WorldA, WorldB, FColor::Cyan, false, 0.f, 0, 6.f);
	}
	if (!PlannedPath.IsEmpty())
	{
		const FForkliftGlobalPathWaypoint& Goal = PlannedPath.Last();
		const FVector GoalMap(Goal.PositionMapCm.X, Goal.PositionMapCm.Y, DebugPathZOffsetCm);
		const FVector GoalWorld = SlamComponent->MapToWorldForDebug.TransformPosition(GoalMap);
		const FVector GoalForwardMap(FMath::Cos(FMath::DegreesToRadians(GoalYawMapDegrees)) * 60.f, FMath::Sin(FMath::DegreesToRadians(GoalYawMapDegrees)) * 60.f, 0.f);
		const FVector GoalForwardWorld = SlamComponent->MapToWorldForDebug.TransformPosition(GoalMap + GoalForwardMap);
		DrawDebugSphere(GetWorld(), GoalWorld, 22.f, 12, FColor::Yellow, false, 0.f, 0, 3.f);
		DrawDebugDirectionalArrow(GetWorld(), GoalWorld, GoalForwardWorld, 12.f, FColor::Yellow, false, 0.f, 0, 3.f);
	}
}
