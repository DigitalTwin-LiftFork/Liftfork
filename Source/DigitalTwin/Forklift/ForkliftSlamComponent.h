#pragma once



#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ForkliftSlamComponent.generated.h"

class UForkliftNavigationSensorComponent;


/** Planar local SLAM prototype. Map frame starts at the first LiDAR pose.
 * Uses local ray measurements and gyro only, never the actor world pose or
 * the simulated IMU's ground-truth linear velocity. No loop closure. */

/** Estimated planar vehicle pose in the SLAM map frame (centimetres/degrees). */
USTRUCT(BlueprintType)
struct FForkliftSlamPose
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SLAM") FVector2D PositionMapCm = FVector2D::ZeroVector;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SLAM") float YawMapDegrees = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SLAM") float MatchedReturnFraction = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SLAM") double TimestampSeconds = 0.0;
	UPROPERTY(VisibleAnywhere, Category="SLAM") uint64 ScanSequenceNumber = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SLAM") bool bInitialized = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SLAM") bool bTrackingValid = false;
};

/**
 * Incremental 2D LiDAR SLAM: correlative scan-to-map pose matching followed
 * by log-odds occupancy updates. The map frame starts at an arbitrary origin.
 * This component never reads its owner's world transform for pose estimation.
 */

UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent))
class DIGITALTWIN_API UForkliftSlamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UForkliftSlamComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Function) override;
	void ResetMap();
	void ToggleMap() { bShowMap = !bShowMap; }
	bool bShowMap = true;
	const TMap<FIntPoint, int8>& GetCells() const { return Cells; }
	FVector2D GetPosition() const { return Position; }
	float GetYaw() const { return Yaw; }
	const FString& GetStatus() const { return Status; }
	float GetConfidence() const { return Confidence; }
	int32 GetScanCount() const { return ScanCount; }
	int32 GetMapUpdateCount() const { return MapUpdateCount; }
		int32 GetMapGeneration() const { return MapGeneration; }
	bool IsTrackingFresh(double Now) const { return bInitialized && StableScans >= 3 && LastAcceptedScan == LastScan && Now-LastAcceptedScan < .3 && Confidence >= .45f; }
	static constexpr float CellCm = 20.f;
private:
	UPROPERTY(Transient) TObjectPtr<UForkliftNavigationSensorComponent> Sensor;
	TMap<FIntPoint, int8> Cells;
	TMap<FIntPoint,float> MatchField;
	int32 StableScans = 0;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Velocity = FVector2D::ZeroVector;
	float Yaw = 0.f, PendingYaw = 0.f, Confidence = 0.f;
	double LastScan = 0.0, LastAcceptedScan = -1.0;
	int32 MapGeneration = 0;
	bool bInitialized = false;
	int32 ScanCount = 0, MapUpdateCount = 0;
	FString Status = TEXT("Waiting for LiDAR");
	void ProcessScan();
	float Score(const TArray<FVector2D>& Points, FVector2D P, float Angle) const;
	void UpdateCell(FIntPoint Cell, int32 Delta);

public:
	UForkliftSlamComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Map", meta=(ClampMin="1000.0")) float MapSizeCm = 20000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Map", meta=(ClampMin="2.0", ClampMax="50.0")) float MapResolutionCm = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Map", meta=(ClampMin="1", ClampMax="100")) int32 OccupiedLogOddsThreshold = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Map", meta=(ClampMin="1", ClampMax="100")) int32 FreeLogOddsThreshold = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Scan matching", meta=(ClampMin="1.0", ClampMax="200.0")) float SearchTranslationCm = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Scan matching", meta=(ClampMin="1.0", ClampMax="45.0")) float SearchYawDegrees = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Scan matching", meta=(ClampMin="1.0", ClampMax="50.0")) float TranslationStepCm = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Scan matching", meta=(ClampMin="0.25", ClampMax="10.0")) float YawStepDegrees = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Scan matching", meta=(ClampMin="1", ClampMax="1000")) int32 MaximumScanPoints = 180;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Scan matching", meta=(ClampMin="1", ClampMax="100")) int32 MinimumMatchedPoints = 5;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Scan matching", meta=(ClampMin="0.0", ClampMax="1.0")) float MinimumMatchedFraction = 0.10f;
	// The lowest configured LiDAR channel is about -27.9 degrees. Include it so
	// pallet-height obstacles are visible; the ground-plane filter below still
	// rejects returns from the floor itself.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Filtering", meta=(ClampMin="1.0", ClampMax="45.0")) float MaximumRayElevationDegrees = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Filtering") float GroundPlaneZBodyCm = -49.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Filtering", meta=(ClampMin="0.0")) float GroundPlaneToleranceCm = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Debug") bool bDrawMap = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Debug") bool bDrawEstimatedPose = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Debug", meta=(ClampMin="1")) int32 DebugCellStride = 4;
	/** Rendering alignment only; it is not read by scan matching or map updates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SLAM|Debug") FTransform MapToWorldForDebug = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SLAM|Status") FForkliftSlamPose EstimatedPose;

	const TArray<int8>& GetLogOddsMap() const { return LogOddsMap; }
	int32 GetMapWidth() const { return MapWidth; }
	uint64 GetConsumedScanSequence() const { return ConsumedScanSequence; }
	/** Returns 0 unknown, 1 free, or 100 occupied. */
	int8 GetMapCellAt(const FVector2D& MapPositionCm) const;

private:
	struct FPlanarScanRay
	{
		FVector2D EndBodyCm = FVector2D::ZeroVector;
		bool bOccupiedEndpoint = false;
	};

	UPROPERTY(Transient) TObjectPtr<UForkliftNavigationSensorComponent> SensorComponent;
	UPROPERTY(Transient) TArray<int8> LogOddsMap;
	UPROPERTY(Transient) TArray<int32> EverOccupiedCellIndices;
	UPROPERTY(Transient) TArray<uint8> bEverOccupiedCell;
	int32 MapWidth = 0;
	uint64 ConsumedScanSequence = 0;
	float LastProcessedTimestamp = 0.f;
	FVector2D TranslationPriorPerScan = FVector2D::ZeroVector;
	float YawPriorPerScanDegrees = 0.f;

	void ConsumeLatestScan();
	void ExtractBodyScanPoints(const struct FForkliftLidarScan& Scan, TArray<FVector2D>& OutHits, TArray<FPlanarScanRay>& OutRays) const;
	bool MatchScanToMap(const TArray<FVector2D>& BodyHits, const FVector2D& PredictedPosition, float PredictedYawDegrees, FVector2D& OutPosition, float& OutYawDegrees, int32& OutMatchedPoints, float& OutMatchedFraction) const;
	float ScoreCandidate(const TArray<FVector2D>& BodyHits, const FVector2D& Position, float YawDegrees, int32& OutMatchedPoints) const;
	void IntegrateScan(const struct FForkliftLidarScan& Scan, const TArray<FPlanarScanRay>& Rays);
	void ApplyEvidence(int32 Index, int8 Delta);
	void TraceMapRay(const FVector2D& Start, const FVector2D& End, bool bOccupiedEndpoint);
	bool MapPointToCell(const FVector2D& Position, int32& OutX, int32& OutY) const;
	int32 CellIndex(int32 X, int32 Y) const { return Y * MapWidth + X; }
	void DrawSlamDebug() const;

};
