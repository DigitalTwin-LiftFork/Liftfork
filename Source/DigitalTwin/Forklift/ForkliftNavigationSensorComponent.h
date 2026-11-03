// C++ virtual navigation sensor: one rotating LiDAR head plus ideal IMU.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ForkliftNavigationSensorComponent.generated.h"

UENUM()
enum class ELidarReturnKind : uint8
{
	NoReturn,
	SelfOccluded,
	ExternalHit,
	Dropped
};

/** One ray in the sensor's local UE frame: X forward, Y right, Z up; cm. */
USTRUCT()
struct FLidarRaySample
{
	GENERATED_BODY()
	UPROPERTY() FVector LocalDirection = FVector::ForwardVector;
	UPROPERTY() float DistanceCm = 0.f;
	UPROPERTY() FVector LocalPointCm = FVector::ZeroVector;
	UPROPERTY() FVector LocalNormal = FVector::ZeroVector;
	UPROPERTY() bool bValid = false;
	UPROPERTY() ELidarReturnKind ReturnKind = ELidarReturnKind::NoReturn;
};

/** One complete 360-degree rotating sweep, deskewed to its end pose. */
USTRUCT()
struct FForkliftLidarScan
{
	GENERATED_BODY()
	UPROPERTY() double TimestampSeconds = 0.0;
	UPROPERTY() uint64 SequenceNumber = 0;
	UPROPERTY() FName SensorFrameId = TEXT("lidar_link");
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
	/** This simulator derives ideal IMU values from the component world transform. */
	UPROPERTY() bool bUsesGroundTruthKinematics = true;
	UPROPERTY() bool bInitialized = false;
};

/**
 * One body-mounted sensor module with a virtual spinning LiDAR head and IMU.
 * The scan samples are accumulated over a full horizontal rotation.
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
	/** Time between successive azimuth columns. At 0.002 s and 100 samples, a revolution takes 0.2 seconds. */
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="0.001")) float ScanIntervalSeconds = 0.002f;
	/** Number of azimuth columns in each full 360-degree rotation. */
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1", ClampMax="720")) int32 HorizontalSamples = 100;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1.0", ClampMax="180.0")) float VerticalFieldOfViewDegrees = 120.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1", ClampMax="64")) int32 VerticalChannels = 9;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan") float VerticalCenterDegrees = -30.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="0.0")) float MinimumRangeCm = 10.f;

	UPROPERTY(EditAnywhere, Category="LiDAR|Scan", meta=(ClampMin="1.0")) float MaximumRangeCm = 1500.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Quality") bool bApplyMeasurementNoise = false;
	UPROPERTY(EditAnywhere, Category="LiDAR|Quality", meta=(ClampMin="0.0")) float RangeBiasCm = 0.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Quality", meta=(ClampMin="0.0")) float RangeNoiseStdDevCm = 0.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Quality", meta=(ClampMin="0.0", ClampMax="1.0")) float DropoutProbability = 0.f;
	UPROPERTY(EditAnywhere, Category="LiDAR|Quality") int32 NoiseRandomSeed = 1701;
	UPROPERTY(EditAnywhere, Category="LiDAR|Frames") FName SensorFrameId = TEXT("lidar_link");

	UPROPERTY(EditAnywhere, Category="LiDAR|Debug") bool bDebugVisible = false;
	UPROPERTY(EditAnywhere, Category="IMU") bool bImuIncludesGravity = false;
	UPROPERTY(EditAnywhere, Category="IMU", meta=(ClampMin="1.0")) float TeleportResetDistanceCm = 250.f;
	UPROPERTY(EditAnywhere, Category="IMU", meta=(ClampMin="1.0", ClampMax="180.0")) float TeleportResetRotationDegrees = 60.f;

	const FForkliftLidarScan& GetLatestScan() const { return LatestScan; }
	/** Flat factory calibration in the chassis frame, independent of mount height. */
	float GetGroundHeightInBody() const;
	const FForkliftImuSample& GetLatestImu() const { return LatestImu; }
	/** Debug/simulation truth only. Do not use as an estimated vehicle pose. */
	FTransform GetDebugSensorWorldTransform() const { return GetComponentTransform(); }
	float GetScanTimeout() const { return FMath::Clamp(ScanIntervalSeconds * HorizontalSamples * 2.f + .1f, .3f, 1.25f); }
	float GetScannerYawDegrees() const { return ScannerYawDegrees; }
	void ToggleDebugVisible();

private:
	FForkliftLidarScan LatestScan;
	FForkliftImuSample LatestImu;
	TArray<FLidarRaySample> SweepSamples;
	TArray<FLidarRaySample> LatestBeamSamples;
	TArray<FVector> LatestBeamWorldEnds;
	FTransform SweepStartTransform = FTransform::Identity;
	FVector LatestBeamWorldStart = FVector::ZeroVector;
	FVector LatestBeamWorldForward = FVector::ForwardVector;
	float ScannerYawDegrees = 0.f;
	float ScanElapsedSeconds = 0.f;
	bool bSweepReferenceInitialized = false;
	bool bHasPreviousImuPose = false;
	FVector PreviousWorldLocation = FVector::ZeroVector;
	FQuat PreviousWorldRotation = FQuat::Identity;
	FVector PreviousWorldVelocity = FVector::ZeroVector;
	double PreviousImuTimeSeconds = 0.0;
	uint64 NextScanSequenceNumber = 1;
	FRandomStream MeasurementRandomStream;

	void PerformScan();
	void UpdateImu();
	void DrawLidarDebug() const;
	float SampleStandardNormal();
};
