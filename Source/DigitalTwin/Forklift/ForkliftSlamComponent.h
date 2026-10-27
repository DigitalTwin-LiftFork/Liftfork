#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ForkliftSlamComponent.generated.h"

class UForkliftNavigationSensorComponent;

/** Planar local SLAM prototype. Map frame starts at the first LiDAR pose.
 * Uses local ray measurements and gyro only, never the actor world pose or
 * the simulated IMU's ground-truth linear velocity. No loop closure. */
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
	bool IsTrackingFresh(double Now) const { return bInitialized && StableScans >= 3 && LastAcceptedScan == LastScan && Now-LastAcceptedScan < 1.25 && Confidence >= .45f; }
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
};
