// R 16 HD automated config. 4 - dimensional digital twin.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AutomatedForklift.generated.h"

class UBoxComponent;
class UCameraComponent;
class USpringArmComponent;
class UInputComponent;
class UStaticMeshComponent;
class USceneComponent;
class APallet;

/** All dimensions use Unreal centimetres (the source specification is mm / m). */
USTRUCT(BlueprintType)
struct FForkliftDimensions
{
	GENERATED_BODY()

	// Verified specification values: 2,659 mm, 1,290 / 1,570 mm, 1,453 mm.
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float OverallLengthCm = 265.9f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float BodyWidthCm = 129.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float MaximumWidthCm = 157.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float WheelbaseCm = 145.3f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float ForkThicknessCm = 4.5f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float ForkWidthCm = 10.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float ForkLengthCm = 115.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float ForkSideShiftCm = 8.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified dimensions") float ReachTravelCm = 39.3f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified wheel layout") float DriveWheelRadiusCm = 18.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified wheel layout") float DriveWheelWidthCm = 13.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified wheel layout") float LoadWheelRadiusCm = 14.25f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified wheel layout") float LoadWheelWidthCm = 10.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified wheel layout") float DriveWheelXcm = -72.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified wheel layout") float LoadWheelLateralOffsetCm = 64.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified capacity") float RatedLoadKg = 1600.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Verified capacity") float LoadCenterCm = 60.0f;

	// TEMP: exact values were not supplied in the available specification/table.
	UPROPERTY(EditAnywhere, Category="R16 HD|TEMP - confirm from drawing") float BodyHeightCm = 95.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|TEMP - confirm from drawing") float WheelRadiusCm = 18.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|TEMP - confirm from drawing") float WheelWidthCm = 12.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|TEMP - confirm from drawing") float MaximumLiftHeightCm = 500.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|TEMP - confirm from drawing") float MastWidthCm = 90.0f;
};

USTRUCT(BlueprintType)
struct FForkliftMotionSettings
{
	GENERATED_BODY()

	// Converted from m/s to cm/s; these are the source specification values.
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float ForwardSpeedCmPerSec = 195.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float ReverseSpeedCmPerSec = 75.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float LoadedLiftSpeedCmPerSec = 52.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float UnloadedLiftSpeedCmPerSec = 66.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float LoadedLowerSpeedCmPerSec = 55.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float UnloadedLowerSpeedCmPerSec = 44.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float ReachSpeedCmPerSec = 20.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float SideShiftSpeedCmPerSec = 10.0f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float AutoFrontTurningRadiusCm = 180.8f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float AutoRearTurningRadius800Cm = 113.7f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float AutoRearTurningRadius1000Cm = 118.4f;
	UPROPERTY(EditAnywhere, Category="R16 HD|Motion") float AutoRearTurningRadius1200Cm = 118.4f;
	UPROPERTY(EditAnywhere, Category="R16 HD|TEMP - control feel") float SteeringRateDegPerSec = 60.0f;
};

/** C++ only, mesh-replaceable reach-truck proxy. Visual meshes never own collision. */
UCLASS()
class DIGITALTWIN_API AAutomatedForklift : public APawn
{
	GENERATED_BODY()

public:
	AAutomatedForklift();
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Geometry and travel limits; enter centimetres. */
	UPROPERTY(EditAnywhere, Category="R16 HD") FForkliftDimensions Dimensions;
	/** Rates and turning radii; enter cm/s and cm. */
	UPROPERTY(EditAnywhere, Category="R16 HD") FForkliftMotionSettings Motion;
	UPROPERTY(EditAnywhere, Category="R16 HD") bool bIsLoaded = false;
	UPROPERTY(EditAnywhere, Category="Drive debug") bool bEnableDriveDebug = false;
	UPROPERTY(EditAnywhere, Category="Drive debug", meta=(ClampMin="0.05")) float DriveDebugIntervalSeconds = 0.25f;
	UPROPERTY(EditAnywhere, Category="Drive debug") bool bDrawDriveDebug = false;

	// Replace a visual's Static Mesh without changing the collision / kinematic components below.
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> BodyVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> MastOuterVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> MastInnerVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> MastLiftVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> LeftForkVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> RightForkVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> UnderframeVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> DriveWheelVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> LeftLoadWheelVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> RightLoadWheelVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> ReachVisual;
	UPROPERTY(VisibleAnywhere, Category="Replaceable visual slots") TObjectPtr<UStaticMeshComponent> ForkCarriageVisual;

	UPROPERTY(VisibleAnywhere, Category="Collision") TObjectPtr<UBoxComponent> BodyCollision;
	UPROPERTY(VisibleAnywhere, Category="Collision") TObjectPtr<UBoxComponent> ForkCollision;
	UPROPERTY(VisibleAnywhere, Category="Collision") TObjectPtr<UBoxComponent> LeftForkCollision;
	UPROPERTY(VisibleAnywhere, Category="Collision") TObjectPtr<UBoxComponent> RightForkCollision;

	UPROPERTY(VisibleAnywhere, Category="Kinematics") TObjectPtr<USceneComponent> MastAssembly;
	UPROPERTY(VisibleAnywhere, Category="Kinematics") TObjectPtr<USceneComponent> LiftCarriage;
	UPROPERTY(VisibleAnywhere, Category="Kinematics") TObjectPtr<USceneComponent> ReachCarriage;
	UPROPERTY(VisibleAnywhere, Category="Kinematics") TObjectPtr<USceneComponent> ForkCarriage;

	/** Rear three-quarter camera. It follows the entire pawn, including its lift/fork kinematics. */
	UPROPERTY(VisibleAnywhere, Category="Forklift camera") TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere, Category="Forklift camera") TObjectPtr<UCameraComponent> FollowCamera;
	UPROPERTY(EditAnywhere, Category="Forklift camera") float CameraMinPitchDeg = -65.f;
	UPROPERTY(EditAnywhere, Category="Forklift camera") float CameraMaxPitchDeg = -10.f;
	UPROPERTY(EditAnywhere, Category="Forklift camera") float CameraMinDistanceCm = 350.f;
	UPROPERTY(EditAnywhere, Category="Forklift camera") float CameraMaxDistanceCm = 1000.f;
	UPROPERTY(EditAnywhere, Category="Forklift camera") float CameraZoomStepCm = 75.f;

	/** Reapplies sizes after editing Dimensions in the Details panel (safe during construction). */
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

private:
	float DriveInput = 0.f, SteerInput = 0.f, LiftInput = 0.f, ReachInput = 0.f, SideShiftInput = 0.f;
	float LiftCm = 0.f, ReachCm = 0.f, SideShiftCm = 0.f;
	float TargetForwardSpeedCmPerSec = 0.f, CurrentForwardSpeedCmPerSec = 0.f, DriveDebugElapsed = 0.f;
	float CameraPitchDeg = -24.f, CameraYawDeg = -35.f, CameraDistanceCm = 700.f;
	bool bCameraOrbiting = false, bWasDriving = false;
	void Drive(float Value); void Steer(float Value); void Lift(float Value); void Reach(float Value); void SideShift(float Value);
	void CameraYaw(float Value); void CameraPitch(float Value); void CameraZoom(float Value);
	void BeginCameraOrbit(); void EndCameraOrbit(); void ResetCamera();
	void ApplyDimensions();
	void SpawnPallet();
	void InteractWithPallet();
	void EmitDriveDiagnostic(const FVector& StartLocation, const FVector& EndLocation, const FHitResult& Hit);
	void LogInitialDriveOverlaps();
	UStaticMeshComponent* CreateVisual(const TCHAR* Name, USceneComponent* Parent);
	UPROPERTY() TObjectPtr<APallet> CarriedPallet;
};
