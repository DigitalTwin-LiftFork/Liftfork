// C++ only virtual navigation sensor: one body-fixed LiDAR plus ideal IMU.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ForkliftNavigationSensorComponent.generated.h"

UENUM()
enum class ELidarReturnKind : uint8
{
	NoReturn,
	SelfOccluded,
	ExternalHit
};

/** One ray in the sensor's local UE frame: X forward, Y right, Z up; cm. */
USTRUCT()
struct FLidarRaySample
{
	GENERATED_BODY()
	UPROPERTY() FVector LocalDirection = FVector::ForwardVector;
	UPROPERTY() float DistanceCm = 0.f;
	UPROPERTY() FVector LocalPointCm = FVector::ZeroVector;
	UPROPERTY() bool bValid = false;
	UPROPERTY() ELidarReturnKind ReturnKind = ELidarReturnKind::NoReturn;
};

/** Snapshot intentionally contains no actor/world pose used as a SLAM estimate. */
USTRUCT()
struct FForkliftLidarScan
{
	GENERATED_BODY()
	UPROPERTY() double TimestampSeconds = 0.0;
	UPROPERTY() FTransform SensorToBody = FTransform::Identity;
	UPROPERTY() TArray<FLidarRaySample> Samples;
};

/** Ideal IMU in the same local sensor frame; cm/s, cm/s² and rad/s. */
USTRUCT()
struct FForkliftImuSample
{
	GENERATED_BODY()
	UPROPERTY() double TimestampSeconds = 0.0;
	UPROPERTY() FVector LinearVelocityCmPerSec = FVector::ZeroVector;
	UPROPERTY() FVector LinearAccelerationCmPerSecSq = FVector::ZeroVector;
	UPROPERTY() FVector AngularVelocityRadPerSec = FVector::ZeroVector;
	UPROPERTY() bool bIncludesGravity = false;
	UPROPERTY() bool bInitialized = false;
};

/**
 * The component is attached to BodyCollision, never the mast/fork chain.
 * It supplies measurement data only; DebugSensorWorldTransform is deliberately
 * separate so consumers cannot mistake simulation truth for a SLAM pose.
 */
UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent))
class DIGITALTWIN_API UForkliftNavigationSensorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UForkliftNavigationSensorComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category="LiDAR|Scan") bool bScanEnabled = true;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="0.02")) float ScanIntervalSeconds = 0.10f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1.0", ClampMax="360.0")) float HorizontalFieldOfViewDegrees = 360.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1", ClampMax="720")) int32 HorizontalSamples = 120;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1.0", ClampMax="180.0")) float VerticalFieldOfViewDegrees = 50.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1", ClampMax="64")) int32 VerticalChannels = 8;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan") float VerticalCenterDegrees = -10.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="0.0")) float MinimumRangeCm = 10.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1.0")) float MaximumRangeCm = 3000.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Debug") bool bDebugVisible = false;
	UPROPERTY(EditAnywhere, Category="LiDAR|Debug", meta=(ClampMin="1")) int32 DebugRayStride = 40;
	UPROPERTY(EditAnywhere, Category="IMU") bool bImuIncludesGravity = false;
	UPROPERTY(EditAnywhere, Category="IMU", meta=(ClampMin="1.0")) float TeleportResetDistanceCm = 250.f;
	UPROPERTY(EditAnywhere, Category="IMU", meta=(ClampMin="1.0", ClampMax="180.0")) float TeleportResetRotationDegrees = 60.f;

	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="120", ClampMax="720")) int32 MappingHorizontalSamples = 360;
	const FForkliftLidarScan& GetLatestScan() const { return LatestScan; }
	const FForkliftImuSample& GetLatestImu() const { return LatestImu; }
	/** Debug/simulation truth only. Do not use as an estimated vehicle pose. */
	FTransform GetDebugSensorWorldTransform() const { return GetComponentTransform(); }
	void ToggleDebugVisible();

private:
	FForkliftLidarScan LatestScan;
	FForkliftImuSample LatestImu;
	float ScanElapsedSeconds = 0.f;
	bool bHasPreviousImuPose = false;
	FVector PreviousWorldLocation = FVector::ZeroVector;
	FQuat PreviousWorldRotation = FQuat::Identity;
	FVector PreviousWorldVelocity = FVector::ZeroVector;
	double PreviousImuTimeSeconds = 0.0;

	void PerformScan();
	void UpdateImu();
	void DrawLidarDebug() const;
};
