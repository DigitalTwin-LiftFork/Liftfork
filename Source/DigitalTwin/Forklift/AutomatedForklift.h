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
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UPhysicsConstraintComponent;
class UForkliftNavigationSensorComponent;
class APallet;

UENUM()
enum class EAutoPickupState : uint8 { Idle, Detecting, Aligning, Inserting, Lifting, Complete, Stopped, Failed };

/** Pixel-space result from the existing pallet sensor render target. */
USTRUCT()
struct FOpeningDetection
{
	GENERATED_BODY()
	bool bValid = false;
	FVector2D LeftCenter = FVector2D::ZeroVector, RightCenter = FVector2D::ZeroVector;
	FVector2D LeftExtent = FVector2D::ZeroVector, RightExtent = FVector2D::ZeroVector;
	float Confidence = 0.f, TimestampSeconds = -BIG_NUMBER;
	float EstimatedDistanceCm = 0.f, EstimatedLateralCm = 0.f;
};

/** HUD-only diagnostics for a connected dark region from the real sensor image. */
USTRUCT()
struct FVisionCandidateDebug
{
	GENERATED_BODY()
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D Extent = FVector2D::ZeroVector;
	int32 Area = 0;
	bool bSelected = false;
	bool bTouchesSearchBoundary = false;
	FString Reason;
};

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
	UPROPERTY(VisibleAnywhere, Category="Collision debug") bool bCollisionDebugVisible = false;
	UPROPERTY(EditAnywhere, Category="Collision debug") bool bEnableCollisionDebugHeartbeatLog = false;
	/** Contact-based automatic lift: each tine tip must enter this far into its own pallet opening. */
	UPROPERTY(EditAnywhere, Category="Pallet support", meta=(ClampMin="1.0", ClampMax="100.0")) float MinimumPalletSupportInsertionCm = 45.f;
	UPROPERTY(EditAnywhere, Category="Pallet support", meta=(ClampMin="0.01", ClampMax="5.0")) float PalletSupportContactToleranceCm = 1.0f;
	/** Chaos tuning values (TEMP: simulation tuning, not R16HD specifications). */
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float ChassisMassKg = 2200.f;
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float DriveForceNewton = 180000.f;
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float SteeringTorque = 550000.f;
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float ConstraintStiffness = 180000.f;
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float ConstraintDamping = 18000.f;
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float ConstraintForceLimit = 1000000.f;
	/** Simulation estimates, not manufacturer mass/inertia data. */
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float LiftAssemblyMassKg = 300.f;
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float ReachAssemblyMassKg = 140.f;
	UPROPERTY(EditAnywhere, Category="Chaos|TEMP tuning") float ForkCarrierAssemblyMassKg = 110.f;
	/** Simulation limit for the current visual model, not an R16HD manufacturer specification. */
	UPROPERTY(EditAnywhere, Category="Chaos|Lift reference") float MaximumForkTopHeightAboveGroundCm = 450.f;
	/** Actor-local ground plane. It is derived from the body bottom and the existing 2cm floor clearance; never from world Z. */
	UPROPERTY(VisibleAnywhere, Category="Chaos|Lift reference") float GroundReferenceLocalZcm = -49.5f;
	UPROPERTY(VisibleAnywhere, Category="Chaos|Lift reference") float LowestForkTopHeightAboveGroundCm = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Chaos|Lift reference") float RequiredLiftStrokeCm = 500.f;

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
	UPROPERTY(VisibleAnywhere, Category="Collision") TObjectPtr<UBoxComponent> LeftSupportLegCollision;
	UPROPERTY(VisibleAnywhere, Category="Collision") TObjectPtr<UBoxComponent> RightSupportLegCollision;
	/** Query-only self-occluders used by the LiDAR; they never participate in vehicle physics. */
	UPROPERTY(VisibleAnywhere, Category="LiDAR") TObjectPtr<UBoxComponent> LidarBodyOccluder;
	UPROPERTY(VisibleAnywhere, Category="LiDAR") TObjectPtr<UBoxComponent> LidarMastOccluder;
	UPROPERTY(VisibleAnywhere, Category="Chaos") TObjectPtr<UBoxComponent> LiftPhysicsBody;
	UPROPERTY(VisibleAnywhere, Category="Chaos") TObjectPtr<UBoxComponent> ReachPhysicsBody;
	UPROPERTY(VisibleAnywhere, Category="Chaos") TObjectPtr<UBoxComponent> ForkPhysicsBody;
	UPROPERTY(VisibleAnywhere, Category="Chaos") TObjectPtr<UPhysicsConstraintComponent> LiftConstraint;
	UPROPERTY(VisibleAnywhere, Category="Chaos") TObjectPtr<UPhysicsConstraintComponent> ReachConstraint;
	UPROPERTY(VisibleAnywhere, Category="Chaos") TObjectPtr<UPhysicsConstraintComponent> SideShiftConstraint;
	/** Fixed physical joints which make the two actual fork contact shapes part of the Chaos chain. */
	UPROPERTY(VisibleAnywhere, Category="Chaos") TObjectPtr<UPhysicsConstraintComponent> LeftForkFixedConstraint;
	UPROPERTY(VisibleAnywhere, Category="Chaos") TObjectPtr<UPhysicsConstraintComponent> RightForkFixedConstraint;

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

	/** One carriage-mounted insertion sensor. It follows lift, reach and side shift, never the user camera. */
	UPROPERTY(VisibleAnywhere, Category="Pallet sensor") TObjectPtr<USceneCaptureComponent2D> PalletSensorCapture;
	UPROPERTY(Transient, VisibleAnywhere, Category="Pallet sensor") TObjectPtr<UTextureRenderTarget2D> PalletSensorRenderTarget;
	/** Drawing-verified offsets, converted from the backrest/fork coordinate system; not raw component XYZ. */
	UPROPERTY(EditAnywhere, Category="Pallet sensor|Drawing dimensions") float PalletSensorHeightAboveForkTopCm = 19.f;
	UPROPERTY(EditAnywhere, Category="Pallet sensor|Drawing dimensions") float PalletSensorLateralOffsetFromCenterCm = 6.3f;
	UPROPERTY(EditAnywhere, Category="Pallet sensor|Drawing dimensions") float PalletSensorForwardOfBackrestCm = 2.6f;
	/** Simulation assumptions: not specified by the product drawing. */
	UPROPERTY(EditAnywhere, Category="Pallet sensor|Simulation") FRotator PalletSensorRelativeRotation = FRotator(-10.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, Category="Pallet sensor|Simulation") float PalletSensorBackrestClearanceCm = 5.f;
	UPROPERTY(EditAnywhere, Category="Pallet sensor|Simulation", meta=(ClampMin="30.0", ClampMax="140.0")) float PalletSensorFieldOfViewDeg = 78.f;
	UPROPERTY(EditAnywhere, Category="Pallet sensor", meta=(ClampMin="64", ClampMax="1920")) int32 PalletSensorWidthPx = 960;
	UPROPERTY(EditAnywhere, Category="Pallet sensor", meta=(ClampMin="64", ClampMax="1080")) int32 PalletSensorHeightPx = 540;
	UPROPERTY(EditAnywhere, Category="Pallet sensor", meta=(ClampMin="0.01", ClampMax="1.0")) float PalletSensorCaptureIntervalSeconds = 1.f / 30.f;
	UPROPERTY(EditAnywhere, Category="Pallet sensor") bool bPalletSensorCaptureActive = true;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Vision") float VisionProcessIntervalSeconds = .20f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Vision") float VisionMaximumFrameAgeSeconds = .75f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Vision") float MinimumOpeningConfidence = .62f;
	/** Known test-pallet opening geometry. Used only to convert pixel spacing to range; never reads an Actor position. */
	UPROPERTY(EditAnywhere, Category="Auto pickup|Vision|Known pallet") float VisionKnownOpeningCenterSpacingCm = 60.f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Vision|Known pallet") float VisionKnownOpeningWidthCm = 36.f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Motion") float AutoInsertSpeedScale = .20f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Motion") float AutoLiftSpeedScale = .25f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Motion") float AutoRequiredInsertionCm = 50.f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Motion") float AutoMaximumInsertionCm = 75.f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Motion") float AutoMaximumApproachTravelCm = 260.f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Motion") float AutoTestLiftCm = 15.f;
	/** Pixel target is a calibrated simulation setting for the carriage camera, not an object-coordinate lookup. */
	UPROPERTY(EditAnywhere, Category="Auto pickup|Vision") float AutoTargetOpeningCenterYRatio = .58f;
	UPROPERTY(EditAnywhere, Category="Auto pickup|Motion") float AutoVerticalToleranceCm = 1.5f;
	/** One fixed rear-body LiDAR/IMU module. It is intentionally independent of the moving fork camera. */
	UPROPERTY(VisibleAnywhere, Category="LiDAR") TObjectPtr<UForkliftNavigationSensorComponent> NavigationSensor;
	UPROPERTY(VisibleAnywhere, Category="LiDAR") TObjectPtr<UStaticMeshComponent> NavigationSensorVisual;
    UPROPERTY(VisibleAnywhere, Category="LiDAR") TObjectPtr<UStaticMeshComponent> LidarPole7mRebuilt;

	/** Reapplies sizes after editing Dimensions in the Details panel (safe during construction). */
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	UTextureRenderTarget2D* GetPalletSensorRenderTarget() const { return PalletSensorRenderTarget; }
	int32 GetPalletSensorDisplayMode() const { return PalletSensorDisplayMode; }
	const FOpeningDetection& GetOpeningDetection() const { return OpeningDetection; }
	const TArray<FVisionCandidateDebug>& GetVisionCandidates() const { return VisionCandidates; }
	const TArray<FString>& GetVisionPairDiagnostics() const { return VisionPairDiagnostics; }
	const TArray<uint8>& GetVisionMask() const { return VisionMask; }
	int32 GetVisionMaskWidth() const { return VisionMaskWidth; }
	int32 GetVisionMaskHeight() const { return VisionMaskHeight; }
	int32 GetVisionDebugViewMode() const { return VisionDebugViewMode; }
	EAutoPickupState GetAutoPickupState() const { return AutoPickupState; }
	const FString& GetAutoPickupStatus() const { return AutoPickupStatus; }
	void GetAutoPickupDiagnosticLines(TArray<FString>& OutLines) const;
	void NotifyAutoPickupButtonClick(bool bInsideButton, const FVector2D& ScreenPosition);
		void ToggleAutoPickup();
		bool IsNavigationActive() const { return bNavigationActive; }
	static FBox2D NavigationButtonRect(float Height) { return FBox2D(FVector2D(24.f,Height-154.f),FVector2D(264.f,Height-112.f)); }
	static FBox2D SlamMapRect(float Height) { const float Size=FMath::Min(300.f,Height*.40f); return FBox2D(FVector2D(20.f,100.f),FVector2D(20.f+Size,100.f+Size)); }
	bool SetNavigationGoalFromMapScreen(FVector2D ScreenPosition, float ViewportHeight);
	void ToggleNavigation();
	void StopNavigation();
	const FString& GetNavigationStatus() const { return NavigationStatus; }
	const TArray<FVector2D>& GetNavigationRoute() const { return NavigationRoute; }
	bool HasNavigationGoal() const { return bNavigationGoalSet; }
	FVector2D GetNavigationGoal() const { return NavigationGoal; }
	static FString AutoPickupStateToString(EAutoPickupState State);

private:
		void UpdateNavigation(float Dt);
		bool PlanNavigationRoute(FVector2D Start);
	bool SelectExplorationGoal(FVector2D Start);
	TArray<FVector2D> ExplorationVisited;
	TMap<FIntPoint,double> ExplorationBlockedUntil;
	int32 ExplorationBlockedGoals = 0;
	float NavigationTrackingWaitStart = -1.f;
	bool bNavigationActive = false, bNavigationGoalSet = false, bNavigationGoalIsManual = false;
	int32 NavigationMapGeneration = -1;
	FVector2D NavigationGoal = FVector2D::ZeroVector, NavigationProgressPosition = FVector2D::ZeroVector;
	TArray<FVector2D> NavigationRoute;
	float NavigationReplanTime = 0.f, NavigationProgressTime = 0.f;
	FString NavigationStatus = TEXT("하단 랜덤 순찰 시작 버튼을 누르세요");
	float DriveInput = 0.f, SteerInput = 0.f, LiftInput = 0.f, ReachInput = 0.f, SideShiftInput = 0.f;
	float LiftCm = 0.f, ReachCm = 0.f, SideShiftCm = 0.f;
	float TargetForwardSpeedCmPerSec = 0.f, CurrentForwardSpeedCmPerSec = 0.f, DriveDebugElapsed = 0.f;
	float CameraPitchDeg = -24.f, CameraYawDeg = -35.f, CameraDistanceCm = 700.f;
	float PalletSensorCaptureElapsed = 0.f;
	float VisionProcessElapsed = 0.f, AutoPickupStateElapsed = 0.f, AutoLiftStartCm = 0.f, AutoLiftPalletStartZ = 0.f, AutoRequiredForwardTravelCm = -BIG_NUMBER, AutoMaximumForwardTravelCm = -BIG_NUMBER;
	FVector AutoInsertionStartLocation = FVector::ZeroVector;
	int32 OpeningStableFrames = 0, AlignmentStableFrames = 0;
	int32 LastVisionCandidateCount = -1;
	TArray<FVisionCandidateDebug> VisionCandidates;
	TArray<FString> VisionPairDiagnostics;
	TArray<uint8> VisionMask;
	int32 VisionMaskWidth = 0, VisionMaskHeight = 0, LastVisionSearchStartY = -1;
	int32 VisionDebugViewMode = 0;
	bool bSensorOcclusionDiagnosticHideForks = false;
	int32 LastVisionFrameNumber = 0;
	bool bLastVisionReadSucceeded = false;
	bool bLastVisionHadValidPair = false;
	float LastVisionProcessTime = -BIG_NUMBER;
	float LastButtonClickTime = -BIG_NUMBER;
	FString LastButtonInput = TEXT("미입력");
	FString LastPixelReadStatus = TEXT("미처리");
	float LastAlignmentLateralErrorCm = -BIG_NUMBER;
	float LastAlignmentLiftCorrectionCm = -BIG_NUMBER;
	float LastAutoInsertionTravelCm = -BIG_NUMBER;
	bool bLastAutoDriveBlocked = false;
	bool bLastPhysicalPalletContact = false;
	float LastPhysicalPalletRiseCm = -BIG_NUMBER;
	FOpeningDetection OpeningDetection, LockedOpeningDetection;
	EAutoPickupState AutoPickupState = EAutoPickupState::Idle;
	FString AutoPickupStatus = TEXT("Idle");
	int32 PalletSensorDisplayMode = 0;
	bool bCameraOrbiting = false, bWasDriving = false;
	float LastCollisionLogTime = -BIG_NUMBER;
	float LastActuatorLogTime = -BIG_NUMBER;
	float LastInputLogTime = -BIG_NUMBER;
	float LastCollisionDebugHeartbeatTime = -BIG_NUMBER;
	float LastCollisionHitExpireTime = -BIG_NUMBER;
	/** Constraint-space centres. Linear limits are symmetric, so a one-sided user stroke is centred around these values. */
	float LiftConstraintCentreCm = 0.f;
	float ReachConstraintCentreCm = 0.f;
	float LiftNativeDriveTargetCm = 0.f;
	float ReachNativeDriveTargetCm = 0.f;
	float ReachRestRelativeXcm = 0.f;
	FString LastCollisionLogSignature;
	FString CollisionDebugStatus;
	FHitResult LastCollisionHit;
	TWeakObjectPtr<UPrimitiveComponent> LastBlockingComponent;
	void Drive(float Value); void Steer(float Value); void Lift(float Value); void Reach(float Value); void SideShift(float Value);
	void CameraYaw(float Value); void CameraPitch(float Value); void CameraZoom(float Value);
	void BeginCameraOrbit(); void EndCameraOrbit(); void ResetCamera();
	void ToggleCollisionDebug();
	void TogglePalletSensorDisplay();
	void TogglePalletSensorCapture();
	void ToggleVisionDebugView();
	void ToggleSensorOcclusionDiagnostic();
	void ToggleLidarDebug();
	void UpdatePalletSensorSettings();
	void ProcessOpeningVision();
	void UpdateAutoPickup(float DeltaSeconds);
	void SetAutoPickupState(EAutoPickupState NewState, const FString& Status);
	void CancelAutoPickup(const FString& Reason, bool bFailure);
	bool HasPhysicalPalletLift(float& OutPalletRiseCm) const;
	void ClearAutomaticCommands();
	void ConfigureKinematicForkMechanism();
	void ApplyKinematicForkControls(float DeltaSeconds);
	void SetConstraintFramesAtPose(UPhysicsConstraintComponent* Constraint, UPrimitiveComponent* Body1, UPrimitiveComponent* Body2, const FTransform& JointWorld, const FVector& Frame2Offset);
	FVector GetConstraintFrameDelta(const UPhysicsConstraintComponent* Constraint, const UPrimitiveComponent* Body1, const UPrimitiveComponent* Body2) const;
	float GetForkTopHeightAboveGroundCm() const;
	void ConfigureLiftHeightReference();
	void UpdateMastVisuals(float ActualLiftCm);
	void LogInputInvocation(const TCHAR* InputName, float Value);
	void ApplyDimensions();
	void SpawnPallet();
	void InteractWithPallet();
	bool SweepForkliftPart(UBoxComponent* Component, const FVector& WorldDelta, const TCHAR* Operation, FHitResult& OutHit, const AActor* ExtraIgnoredActor = nullptr);
	bool SweepForkPartAgainstFixedObstacle(UBoxComponent* Component, const FVector& WorldDelta, const TCHAR* Operation, FHitResult& OutHit);
	bool IsForkAxisMotionBlockedByFixedObstacle(const FVector& WorldDelta, const TCHAR* Operation);
	bool WouldForkRotationHitFixedObstacle(float DeltaYawDegrees) const;
	bool SweepCarriedPallet(const FVector& WorldDelta, const TCHAR* Operation, FHitResult& OutHit);
	bool SweepPalletAgainstExternal(APallet* Pallet, const FVector& WorldDelta, const TCHAR* Operation, FHitResult& OutHit);
	bool IsMovementDeeperIntoInitialOverlap(const FHitResult& Hit, const FVector& WorldDelta) const;
	void RecordCollision(const TCHAR* Operation, UPrimitiveComponent* SourceComponent, const FHitResult& Hit, const FVector& WorldDelta, bool bBlocked);
	void DrawCollisionDebug();
	void DrawPalletSensorDebug() const;
	void EmitDriveDiagnostic(const FVector& StartLocation, const FVector& EndLocation, const FHitResult& Hit);
	void LogInitialDriveOverlaps();
	UStaticMeshComponent* CreateVisual(const TCHAR* Name, USceneComponent* Parent);
	UPROPERTY() TObjectPtr<APallet> CarriedPallet;
	bool bPalletSupportedAutomatically = false;
	bool bCarriedPalletResting = false;
	FString PalletSupportDebugStatus;
	FString ActuatorDebugStatus;
	bool bChaosConfigured = false;
	bool bChaosInitialPoseLogged = false;
};
