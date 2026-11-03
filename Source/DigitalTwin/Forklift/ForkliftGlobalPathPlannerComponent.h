#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ForkliftGlobalPathPlannerComponent.generated.h"

class UForkliftSlamComponent;

/** A waypoint in the arbitrary SLAM map frame, in Unreal centimetres. */
USTRUCT(BlueprintType)
struct FForkliftGlobalPathWaypoint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") FVector2D PositionMapCm = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Navigation") float YawMapDegrees = 0.f;
};

/**
 * Stage 5 global route planner. It searches only SLAM cells classified as free;
 * unknown cells and occupied cells are blocked. It computes a route but never
 * sends motion commands to the forklift.
 */
UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent))
class DIGITALTWIN_API UForkliftGlobalPathPlannerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UForkliftGlobalPathPlannerComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global planner|Goal") FVector2D GoalMapCm = FVector2D(1000.f, 0.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global planner|Goal", meta=(ClampMin="-180.0", ClampMax="180.0")) float GoalYawMapDegrees = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Global planner|Footprint", meta=(ClampMin="0.0")) float SafetyMarginCm = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Global planner|Search", meta=(ClampMin="0.0")) float SearchPaddingCm = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Global planner|Search", meta=(ClampMin="1000.0")) float MaximumSearchAreaCm = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Global planner|Search", meta=(ClampMin="0.0")) float TurnPenaltyCmPerRadian = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Global planner|Search") bool bPlanOnBeginPlay = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Global planner|Debug") bool bDrawPath = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Global planner|Debug", meta=(ClampMin="0.0")) float DebugPathZOffsetCm = 12.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Global planner|Status") FString PlannerStatus = TEXT("Waiting for SLAM");
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Global planner|Status") int32 ExpandedNodeCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Global planner|Status") TArray<FForkliftGlobalPathWaypoint> PlannedPath;

	/** Request a fresh route using the current GoalMapCm and GoalYawMapDegrees. */
	UFUNCTION(BlueprintCallable, Category="Global planner") bool RequestPathPlan();
	/** Change the goal and immediately request a new route. */
	UFUNCTION(BlueprintCallable, Category="Global planner") bool SetGoalAndPlan(FVector2D NewGoalMapCm, float NewGoalYawMapDegrees = 0.f);

private:
	UPROPERTY(Transient) TObjectPtr<UForkliftSlamComponent> SlamComponent;
	double RetryElapsedSeconds = 0.0;
	uint64 LastAttemptedScanSequence = 0;
	uint64 LastSeenScanSequence = 0;
	FString LastLoggedStatus;
	float FootprintRadiusCm = 0.f;

	void SetPlannerStatus(const FString& NewStatus);
	void DrawPlannedRoute() const;
	bool BuildAStarPath(int32 StartX, int32 StartY, int32 GoalX, int32 GoalY, int32 MapWidth, float ResolutionCm, TArray<FIntPoint>& OutCells);
};
