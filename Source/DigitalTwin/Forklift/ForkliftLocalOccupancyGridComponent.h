#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ForkliftLocalOccupancyGridComponent.generated.h"

class UForkliftNavigationSensorComponent;

/** One scan-derived obstacle cluster in the forklift body frame. */
USTRUCT(BlueprintType)
struct FForkliftObstacleObservation
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perception") FVector2D CenterBodyCm = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perception") FVector2D BoundsMinBodyCm = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perception") FVector2D BoundsMaxBodyCm = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Perception") int32 OccupiedCellCount = 0;
};

/**
 * A short-lived, body-centric 2D occupancy snapshot built only from LiDAR
 * returns. It is not a persistent map and does not estimate vehicle pose.
 */
UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent))
class DIGITALTWIN_API UForkliftLocalOccupancyGridComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UForkliftLocalOccupancyGridComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Local occupancy", meta=(ClampMin="100.0")) float GridSizeCm = 2000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Local occupancy", meta=(ClampMin="2.0", ClampMax="100.0")) float CellSizeCm = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Local occupancy", meta=(ClampMin="1.0", ClampMax="45.0")) float MaximumRayElevationDegrees = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Local occupancy") float GroundPlaneZBodyCm = -49.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Local occupancy", meta=(ClampMin="0.0")) float GroundPlaneToleranceCm = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Local occupancy", meta=(ClampMin="1", ClampMax="10000")) int32 MinimumClusterCells = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Local occupancy|Debug") bool bDrawOccupiedCells = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Local occupancy|Debug", meta=(ClampMin="1")) int32 DebugCellStride = 2;

	const TArray<int8>& GetCells() const { return Cells; }
	const TArray<FForkliftObstacleObservation>& GetObstacleObservations() const { return ObstacleObservations; }
	int32 GetGridWidth() const { return GridWidth; }
	int32 GetGridHeight() const { return GridHeight; }
	uint64 GetConsumedScanSequence() const { return ConsumedScanSequence; }
	/** Returns -1 unknown, 0 free or 100 occupied. */
	int8 GetCellAtBodyLocation(const FVector2D& BodyLocationCm) const;

private:
	UPROPERTY(Transient) TObjectPtr<UForkliftNavigationSensorComponent> SensorComponent;
	UPROPERTY(Transient) TArray<int8> Cells;
	UPROPERTY(Transient) TArray<FForkliftObstacleObservation> ObstacleObservations;
	int32 GridWidth = 0;
	int32 GridHeight = 0;
	uint64 ConsumedScanSequence = 0;

	void ConsumeLatestScan();
	void ClearGrid();
	bool BodyPointToCell(const FVector2D& Point, int32& OutX, int32& OutY) const;
	int32 CellIndex(int32 X, int32 Y) const { return Y * GridWidth + X; }
	void TraceFreeCells(int32 X0, int32 Y0, int32 X1, int32 Y1, bool bLeaveEndpointUnknown);
	void ExtractObstacleClusters();
	void DrawGridDebug() const;
};
