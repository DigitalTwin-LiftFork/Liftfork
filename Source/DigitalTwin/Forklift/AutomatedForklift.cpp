#include "Forklift/AutomatedForklift.h"
#include "Forklift/Pallet.h"
#include "Forklift/WoodPallet.h"
#include "Forklift/ForkliftNavigationSensorComponent.h"
#include "Forklift/ForkliftSlamComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/EngineTypes.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace Forklift
{
	static UStaticMesh* Body() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/Body_Actual.Body_Actual")); return M.Object; }
	static UStaticMesh* Underframe() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/Underframe_Actual.Underframe_Actual")); return M.Object; }
	static UStaticMesh* DriveWheel() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/DriveSteerWheel_Actual.DriveSteerWheel_Actual")); return M.Object; }
	static UStaticMesh* LoadWheel() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/LoadWheel_Actual.LoadWheel_Actual")); return M.Object; }
	static UStaticMesh* MastOuter() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/MastOuter_Actual.MastOuter_Actual")); return M.Object; }
	static UStaticMesh* MastInner() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/MastInner_Actual.MastInner_Actual")); return M.Object; }
	static UStaticMesh* MastLift() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/MastLift_Actual.MastLift_Actual")); return M.Object; }
	static UStaticMesh* Reach() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/ReachCarriage_Actual.ReachCarriage_Actual")); return M.Object; }
	static UStaticMesh* Carriage() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/ForkCarriage_Actual.ForkCarriage_Actual")); return M.Object; }
	static UStaticMesh* LeftFork() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/ForkLeft_Actual.ForkLeft_Actual")); return M.Object; }
	static UStaticMesh* RightFork() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Game/DigitalTwin/Art/R16HD/Imported/ForkRight_Actual.ForkRight_Actual")); return M.Object; }
	static UStaticMesh* SensorHousing() { static ConstructorHelpers::FObjectFinder<UStaticMesh> M(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); return M.Object; }
}

UStaticMeshComponent* AAutomatedForklift::CreateVisual(const TCHAR* Name, USceneComponent* Parent)
{
	UStaticMeshComponent* Result = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Result->SetupAttachment(Parent);
	Result->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Result->SetGenerateOverlapEvents(false);
	return Result;
}

AAutomatedForklift::AAutomatedForklift()
{
	PrimaryActorTick.bCanEverTick = true;
	BodyCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("BodyCollision"));
	SetRootComponent(BodyCollision);
	BodyCollision->SetCollisionProfileName(TEXT("Pawn"));

	BodyCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	NavigationSensor = CreateDefaultSubobject<UForkliftNavigationSensorComponent>(TEXT("RearBodyLidarImu"));
	NavigationSensor->SetupAttachment(BodyCollision);

	// PhysicsActor pallets use ECC_PhysicsBody. The broad chassis proxy ends
	// 10 cm behind the tine roots (the tine roots start at x=+20 cm from the
	// mast), so blocking pallets here stops chassis penetration while leaving
	// the two fork insertion corridors open.
	BodyCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	// One rear-body LiDAR/IMU and one SLAM consumer.  The components are kept
	// independent of the moving fork RGB-D camera.
	SlamComponent = CreateDefaultSubobject<UForkliftSlamComponent>(TEXT("LidarSlam"));

// LiDAR support pole
// 50 cm enters the chassis, with the remaining pole above the body.
constexpr float LidarPoleHeightCm = 750.f;
const float SensorColumnX = Dimensions.OverallLengthCm * .5f - 10.f;
const float PoleBottomZ = Dimensions.BodyHeightCm * .5f - 50.f;
const float SensorZ = PoleBottomZ + LidarPoleHeightCm;

LidarPole7mRebuilt = CreateVisual(TEXT("LidarPole7mRebuilt"), BodyCollision);
LidarPole7mRebuilt->SetStaticMesh(Forklift::SensorHousing());
LidarPole7mRebuilt->SetRelativeLocation(
    FVector(SensorColumnX, 0.f, PoleBottomZ + LidarPoleHeightCm * .5f)
);
LidarPole7mRebuilt->SetRelativeScale3D(
    FVector(.08f, .08f, LidarPoleHeightCm / 100.f)
);

NavigationSensor->SetRelativeLocation(
    FVector(SensorColumnX, 0.f, SensorZ)
);
	NavigationSensor->SetRelativeRotation(FRotator::ZeroRotator);
	NavigationSensorVisual = CreateVisual(TEXT("RearBodyLidarImuHousing"), NavigationSensor);
	NavigationSensorVisual->SetStaticMesh(Forklift::SensorHousing());
	NavigationSensorVisual->SetRelativeScale3D(FVector(.14f, .14f, .08f));
	NavigationSensorVisual->SetRelativeLocation(FVector::ZeroVector);
	LidarBodyOccluder = CreateDefaultSubobject<UBoxComponent>(TEXT("LidarBodyOccluder"));
	LidarBodyOccluder->SetupAttachment(BodyCollision);
	LidarMastOccluder = CreateDefaultSubobject<UBoxComponent>(TEXT("LidarMastOccluder"));
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("ForkliftRearThreeQuarterCameraBoom"));
	CameraBoom->SetupAttachment(BodyCollision);
	CameraBoom->TargetArmLength = 700.f;
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	CameraBoom->SetRelativeRotation(FRotator(-24.f, -35.f, 0.f));
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bDoCollisionTest = true;
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ForkliftRearThreeQuarterCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	BodyVisual = CreateVisual(TEXT("BodyVisual_ReplaceMesh"), BodyCollision);
	UnderframeVisual = CreateVisual(TEXT("UnderframeVisual_ReplaceMesh"), BodyCollision);
	DriveWheelVisual = CreateVisual(TEXT("DriveWheelVisual_ReplaceMesh"), BodyCollision);
	LeftLoadWheelVisual = CreateVisual(TEXT("LeftLoadWheelVisual_ReplaceMesh"), BodyCollision);
	RightLoadWheelVisual = CreateVisual(TEXT("RightLoadWheelVisual_ReplaceMesh"), BodyCollision);
	LiftPhysicsBody = CreateDefaultSubobject<UBoxComponent>(TEXT("LiftPhysicsBody")); LiftPhysicsBody->SetupAttachment(BodyCollision);
	ReachPhysicsBody = CreateDefaultSubobject<UBoxComponent>(TEXT("ReachPhysicsBody")); ReachPhysicsBody->SetupAttachment(LiftPhysicsBody);
	ForkPhysicsBody = CreateDefaultSubobject<UBoxComponent>(TEXT("ForkPhysicsBody")); ForkPhysicsBody->SetupAttachment(ReachPhysicsBody);
	LiftConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("LiftConstraint_Z")); LiftConstraint->SetupAttachment(BodyCollision);
	ReachConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("ReachConstraint_X")); ReachConstraint->SetupAttachment(LiftPhysicsBody);
	SideShiftConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("SideShiftConstraint_Y")); SideShiftConstraint->SetupAttachment(ReachPhysicsBody);
	LeftForkFixedConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("LeftForkFixedConstraint")); LeftForkFixedConstraint->SetupAttachment(ForkPhysicsBody);
	RightForkFixedConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("RightForkFixedConstraint")); RightForkFixedConstraint->SetupAttachment(ForkPhysicsBody);
	MastAssembly = CreateDefaultSubobject<USceneComponent>(TEXT("MastAssembly")); MastAssembly->SetupAttachment(BodyCollision);
	LidarMastOccluder->SetupAttachment(MastAssembly);
	LeftMastRailCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("LeftMastRailCollision"));
	RightMastRailCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("RightMastRailCollision"));
	MastTopCrossbarCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("MastTopCrossbarCollision"));
	for (UBoxComponent* MastPart : {LeftMastRailCollision.Get(), RightMastRailCollision.Get(), MastTopCrossbarCollision.Get()})
	{
		MastPart->SetupAttachment(MastAssembly);
		MastPart->SetCollisionProfileName(TEXT("BlockAll"));
		MastPart->SetCollisionObjectType(ECC_WorldDynamic);
		MastPart->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MastPart->SetSimulatePhysics(false);
		MastPart->SetEnableGravity(false);
	}

	MastOuterVisual = CreateVisual(TEXT("MastOuterVisual_ReplaceMesh"), MastAssembly);
	MastInnerVisual = CreateVisual(TEXT("MastInnerVisual_ReplaceMesh"), MastAssembly);
	MastLiftVisual = CreateVisual(TEXT("MastLiftVisual_ReplaceMesh"), MastAssembly);
	LiftCarriage = CreateDefaultSubobject<USceneComponent>(TEXT("LiftCarriage")); LiftCarriage->SetupAttachment(LiftPhysicsBody);
	ReachCarriage = CreateDefaultSubobject<USceneComponent>(TEXT("ReachCarriage")); ReachCarriage->SetupAttachment(ReachPhysicsBody);
	ReachVisual = CreateVisual(TEXT("ReachVisual_ReplaceMesh"), ReachCarriage);
	ForkCarriage = CreateDefaultSubobject<USceneComponent>(TEXT("ForkCarriage")); ForkCarriage->SetupAttachment(ForkPhysicsBody);
	PalletSensorCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PalletInsertionSensorCapture"));
	PalletSensorCapture->SetupAttachment(ForkCarriage);
	PalletSensorCapture->bCaptureEveryFrame = false;
	PalletSensorCapture->bCaptureOnMovement = false;
	// The RGB detector consumes FColor pixels.  Capture final LDR scene colour
	// into the RGBA8 target explicitly; relying on the component default can
	// select HDR scene colour and clamp most of the image white on readback.
	PalletSensorCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	PalletSensorCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	PalletSensorCapture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	PalletSensorCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
	PalletSensorCapture->PostProcessSettings.AutoExposureBias = PalletSensorExposureBias;
	PalletSensorCapture->ShowFlags.SetEyeAdaptation(false);
	// The sensor target is source imagery only. HUD content is drawn after
	// capture, and editor-style debug primitives are excluded here.
	PalletSensorCapture->ShowFlags.SetOnScreenDebug(false);
	PalletSensorCapture->ShowFlags.SetCompositeDebugPrimitives(false);

	PalletSensorRenderTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("PalletInsertionSensorRenderTarget"));
	PalletSensorRenderTarget->RenderTargetFormat = RTF_RGBA8;
	PalletSensorRenderTarget->ClearColor = FLinearColor::Black;
	PalletSensorRenderTarget->bForceLinearGamma = false;
	// FinalColorLDR must use a real 8-bit sRGB-like target.  InitAutoFormat can
	// select an HDR resource from inherited defaults, then DrawTexture/ReadPixels
	// each apply a different conversion and the preview looks clipped white.
	PalletSensorRenderTarget->InitCustomFormat(960, 540, PF_B8G8R8A8, false);
	PalletSensorRenderTarget->TargetGamma = 2.2f;
	PalletSensorCapture->TextureTarget = PalletSensorRenderTarget;
	PalletSensorDepthCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("PalletInsertionSensorDepthCapture"));
	PalletSensorDepthCapture->SetupAttachment(ForkCarriage);
	PalletSensorDepthCapture->bCaptureEveryFrame = false;
	PalletSensorDepthCapture->bCaptureOnMovement = false;
	PalletSensorDepthCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	PalletSensorDepthCapture->ShowFlags.SetOnScreenDebug(false);
	PalletSensorDepthCapture->ShowFlags.SetCompositeDebugPrimitives(false);
	PalletSensorDepthRenderTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("PalletInsertionSensorDepthRenderTarget"));
	// SceneDepth is stored in float R.  PF_FloatRGBA is used rather than a
	// single-channel target because UE's CPU ReadFloat16Pixels path supports it
	// consistently across D3D render-target readback implementations.
	PalletSensorDepthRenderTarget->RenderTargetFormat = RTF_RGBA16f;
	PalletSensorDepthRenderTarget->ClearColor = FLinearColor::Black;
	PalletSensorDepthRenderTarget->InitCustomFormat(960, 540, PF_FloatRGBA, false);
	PalletSensorDepthCapture->TextureTarget = PalletSensorDepthRenderTarget;

	ForkCarriageVisual = CreateVisual(TEXT("ForkCarriageVisual_ReplaceMesh"), ForkCarriage);
	LeftForkVisual = CreateVisual(TEXT("LeftForkVisual_ReplaceMesh"), ForkCarriage);
	RightForkVisual = CreateVisual(TEXT("RightForkVisual_ReplaceMesh"), ForkCarriage);
	ForkCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("ForkCollision")); ForkCollision->SetupAttachment(ForkCarriage);
	// This is only the broad pickup-query volume; tine collision stays separated
	// so the forks can pass through the pallet's two lower openings.
	ForkCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftForkCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("LeftForkCollision")); LeftForkCollision->SetupAttachment(ForkCarriage);
	RightForkCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("RightForkCollision")); RightForkCollision->SetupAttachment(ForkCarriage);
	for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get()})
	{
		Tine->SetCollisionProfileName(TEXT("BlockAll"));
		Tine->SetCollisionObjectType(ECC_WorldDynamic);
	}

	// Match the two fixed red support legs in Underframe_Actual.  The root body
	// intentionally ignores physics bodies so forks can enter a pallet; these
	// narrow shapes provide the missing lower-frame obstruction test instead.

	// Match the two fixed red support legs in Underframe_Actual.

	LeftSupportLegCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("LeftSupportLegCollision"));
	RightSupportLegCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("RightSupportLegCollision"));
	for (UBoxComponent* SupportLeg : {LeftSupportLegCollision.Get(), RightSupportLegCollision.Get()})
	{
		SupportLeg->SetupAttachment(BodyCollision);
		SupportLeg->SetCollisionProfileName(TEXT("BlockAll"));
		SupportLeg->SetCollisionObjectType(ECC_WorldDynamic);
	}
	for (UBoxComponent* Occluder : {LidarBodyOccluder.Get(), LidarMastOccluder.Get()})
	{
		Occluder->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Occluder->SetCollisionResponseToAllChannels(ECR_Ignore);
		Occluder->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Occluder->SetGenerateOverlapEvents(false);
	}

	BodyVisual->SetStaticMesh(Forklift::Body()); UnderframeVisual->SetStaticMesh(Forklift::Underframe());
	DriveWheelVisual->SetStaticMesh(Forklift::DriveWheel()); LeftLoadWheelVisual->SetStaticMesh(Forklift::LoadWheel()); RightLoadWheelVisual->SetStaticMesh(Forklift::LoadWheel());
	MastOuterVisual->SetStaticMesh(Forklift::MastOuter()); MastInnerVisual->SetStaticMesh(Forklift::MastInner()); MastLiftVisual->SetStaticMesh(Forklift::MastLift());
	ReachVisual->SetStaticMesh(Forklift::Reach()); ForkCarriageVisual->SetStaticMesh(Forklift::Carriage());
	LeftForkVisual->SetStaticMesh(Forklift::LeftFork()); RightForkVisual->SetStaticMesh(Forklift::RightFork());
	AutoPossessAI = EAutoPossessAI::Disabled;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void AAutomatedForklift::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); ApplyDimensions(); }
void AAutomatedForklift::BeginPlay()
{
	Super::BeginPlay();

	bAutoApproachStraightDiagnostic = false; // diagnostics never persist into normal auto pickup

	// Keep the paired constructor-owned render targets.  develop2 recreated only
	// the RGB target here, leaving depth on its original target; that split their
	// lifetime/configuration after the merge and made RGB capture depend on an
	// implicit SceneCapture default.  Both streams now retain their explicit
	// format/source configuration and are rebound together below.
	UpdatePalletSensorSettings();
	// The root collision is centred on the actor. Map1 actors authored at floor Z=0 must be lifted to their collision centre.
	const float MinimumCenterHeight = Dimensions.BodyHeightCm * .5f + 2.f;
	if (GetActorLocation().Z < MinimumCenterHeight)
	{
		const FVector CorrectedLocation(GetActorLocation().X, GetActorLocation().Y, MinimumCenterHeight);
		if (bEnableDriveDebug) UE_LOG(LogTemp, Warning, TEXT("Forklift corrected floor placement from %s to %s"), *GetActorLocation().ToString(), *CorrectedLocation.ToString());
		SetActorLocation(CorrectedLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	// Constraints must see the final chassis pose. Configuring them before this
	// placement correction created an implicit first-frame displacement.
	ConfigureKinematicForkMechanism();
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_FIX_V2] BeginPlay forklift=%s class=%s path=%s controller=%s locallyControlled=%d version=2026-10-12-v4"),
		*GetName(), *GetClass()->GetName(), *GetPathName(), *GetNameSafe(GetController()), IsLocallyControlled());
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_SENSOR] carriage-mounted sensor parent=%s relative=%s rotation=%s fov=%.1f (drawing offsets H=%.1f L=%.1f B=%.1f cm)"),
		*GetNameSafe(PalletSensorCapture ? PalletSensorCapture->GetAttachParent() : nullptr),
		PalletSensorCapture ? *PalletSensorCapture->GetRelativeLocation().ToString() : TEXT("(missing)"),
		PalletSensorCapture ? *PalletSensorCapture->GetRelativeRotation().ToString() : TEXT("(missing)"), PalletSensorFieldOfViewDeg,
		PalletSensorHeightAboveForkTopCm, PalletSensorLateralOffsetFromCenterCm, PalletSensorForwardOfBackrestCm);
}

void AAutomatedForklift::ApplyDimensions()
{
	// These are component-local model pivots, in centimetres.  They match the
	// R16HD FBX source assembly: the kinematic nodes remain at their original
	// travel origins, while the mesh pivots place the reach and fork hardware.
	constexpr float ReachVisualBaseHeightCm = 35.f;
	constexpr float ForkMountForwardOffsetCm = 20.f;
	constexpr float ForkCarriageBaseHeightCm = 10.f;
	// Remove stale support parts from previously saved Blueprints.
	TArray<UStaticMeshComponent*> ExistingSensorMeshes;
	GetComponents<UStaticMeshComponent>(ExistingSensorMeshes);
	for (UStaticMeshComponent* Mesh : ExistingSensorMeshes)
	{
		if (Mesh && (Mesh->GetName().StartsWith(TEXT("LidarPole6m")) ||
			Mesh->GetName().StartsWith(TEXT("NavigationSensorSupportColumn")) ||
			Mesh->GetName().StartsWith(TEXT("NavigationSensorSupportBase")) ||
			Mesh->GetName().StartsWith(TEXT("LidarPoleFoot6m"))))
		{
			Mesh->DestroyComponent();
		}
	}
	if (!IsValid(LidarPole7mRebuilt))
	{
		LidarPole7mRebuilt = NewObject<UStaticMeshComponent>(this, TEXT("LidarPole7mRebuilt_Runtime"));
		if (LidarPole7mRebuilt)
		{
			LidarPole7mRebuilt->SetupAttachment(BodyCollision);
			LidarPole7mRebuilt->SetStaticMesh(Forklift::SensorHousing());
			LidarPole7mRebuilt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			LidarPole7mRebuilt->RegisterComponent();
		}
	}
	BodyCollision->SetBoxExtent(FVector(Dimensions.OverallLengthCm * .5f, Dimensions.MaximumWidthCm * .5f, Dimensions.BodyHeightCm * .5f));
	// 50 cm enters the chassis so the pole joins cleanly; 700 cm remains
	// visible above the body roof.
	constexpr float LidarPoleHeightCm = 750.f;
	const float PoleBottomZ = Dimensions.BodyHeightCm * .5f - 50.f;
	const float SensorColumnX = Dimensions.OverallLengthCm * .5f - 10.f;
	const float SensorZ = PoleBottomZ + LidarPoleHeightCm;
	NavigationSensor->SetRelativeLocation(FVector(SensorColumnX, 0.f, SensorZ));
	if (IsValid(LidarPole7mRebuilt))
	{
		LidarPole7mRebuilt->AttachToComponent(BodyCollision, FAttachmentTransformRules::KeepRelativeTransform);
		LidarPole7mRebuilt->SetStaticMesh(Forklift::SensorHousing());
		LidarPole7mRebuilt->SetRelativeLocation(FVector(SensorColumnX, 0.f, PoleBottomZ + LidarPoleHeightCm * .5f));
		LidarPole7mRebuilt->SetRelativeScale3D(FVector(.08f, .08f, LidarPoleHeightCm / 100.f));
	}
	// Query-only representations preserve self-occlusion for line traces without
	// changing the chassis/mast's existing physics collision responses.
	LidarBodyOccluder->SetRelativeLocation(FVector::ZeroVector);
	LidarBodyOccluder->SetBoxExtent(BodyCollision->GetUnscaledBoxExtent());
	LidarMastOccluder->SetRelativeLocation(FVector(0.f, 0.f, Dimensions.MaximumLiftHeightCm * .5f));
	LidarMastOccluder->SetBoxExtent(FVector(12.f, Dimensions.MastWidthCm * .5f, Dimensions.MaximumLiftHeightCm * .5f));
	// The imported mast visuals have collision disabled so they never interfere
	// with the fork camera.  Keep their physical representation narrow and on
	// the actual two uprights; do not close the central fork/carriage passage.
	const float MastRailHalfThicknessCm = 6.f;
	const float MastRailHalfWidthCm = 6.f;
	const float MastRailYcm = FMath::Max(0.f, Dimensions.MastWidthCm * .5f - MastRailHalfWidthCm);
	const float MastRailHalfHeightCm = Dimensions.MaximumLiftHeightCm * .5f;
	LeftMastRailCollision->SetRelativeLocation(FVector(0.f, -MastRailYcm, MastRailHalfHeightCm));
	RightMastRailCollision->SetRelativeLocation(FVector(0.f, MastRailYcm, MastRailHalfHeightCm));
	LeftMastRailCollision->SetBoxExtent(FVector(MastRailHalfThicknessCm, MastRailHalfWidthCm, MastRailHalfHeightCm));
	RightMastRailCollision->SetBoxExtent(FVector(MastRailHalfThicknessCm, MastRailHalfWidthCm, MastRailHalfHeightCm));
	MastTopCrossbarCollision->SetRelativeLocation(FVector(0.f, 0.f, Dimensions.MaximumLiftHeightCm - MastRailHalfWidthCm));
	MastTopCrossbarCollision->SetBoxExtent(FVector(MastRailHalfThicknessCm, Dimensions.MastWidthCm * .5f, MastRailHalfWidthCm));
	// The original lowest fork datum is MastAssembly, not the body origin or
	// a physics-body centre. Preserve it exactly for LiftCm=0.
	LiftPhysicsBody->SetRelativeLocation(FVector(Dimensions.OverallLengthCm * .5f - 10.f, 0.f, -Dimensions.BodyHeightCm * .5f)); LiftPhysicsBody->SetBoxExtent(FVector(35.f, Dimensions.MastWidthCm * .5f, 80.f));
	ReachPhysicsBody->SetRelativeLocation(FVector::ZeroVector); ReachPhysicsBody->SetBoxExtent(FVector(35.f, Dimensions.MastWidthCm * .5f, 35.f));
	// The fork carrier's own shape is only a tiny non-contact physics host.
	// The welded left/right tine boxes below are the real, separated contact
	// geometry; do not fill the opening between forks with one large box.
	ForkPhysicsBody->SetRelativeLocation(FVector::ZeroVector); ForkPhysicsBody->SetBoxExtent(FVector(2.f, 2.f, 2.f));
	BodyVisual->SetRelativeLocation(FVector::ZeroVector);
	// Imported FBXs are already centimetre-correct. Do not reuse the BasicShapes cube scaling.
	BodyVisual->SetRelativeScale3D(FVector::OneVector); UnderframeVisual->SetRelativeScale3D(FVector::OneVector);
	MastAssembly->SetRelativeLocation(FVector(Dimensions.OverallLengthCm * .5f - 10.f, 0, -Dimensions.BodyHeightCm * .5f));
	MastOuterVisual->SetRelativeLocation(FVector(0, 0, Dimensions.MaximumLiftHeightCm * .5f));
	MastOuterVisual->SetRelativeScale3D(FVector::OneVector);
	MastInnerVisual->SetRelativeLocation(FVector(-8.f, 0, Dimensions.MaximumLiftHeightCm * .45f));
	MastInnerVisual->SetRelativeScale3D(FVector::OneVector);
	MastLiftVisual->SetRelativeLocation(FVector(-14.f, 0, Dimensions.MaximumLiftHeightCm * .38f));
	MastLiftVisual->SetRelativeScale3D(FVector::OneVector);
	LeftForkVisual->SetRelativeLocation(FVector(ForkMountForwardOffsetCm + Dimensions.ForkLengthCm * .5f, -30.f, Dimensions.ForkThicknessCm * .5f));
	RightForkVisual->SetRelativeLocation(FVector(ForkMountForwardOffsetCm + Dimensions.ForkLengthCm * .5f, 30.f, Dimensions.ForkThicknessCm * .5f));
	DriveWheelVisual->SetRelativeLocation(FVector(Dimensions.DriveWheelXcm, 0.f, Dimensions.DriveWheelRadiusCm - Dimensions.BodyHeightCm * .5f));
	LeftLoadWheelVisual->SetRelativeLocation(FVector(Dimensions.DriveWheelXcm + Dimensions.WheelbaseCm, Dimensions.LoadWheelLateralOffsetCm, Dimensions.LoadWheelRadiusCm - Dimensions.BodyHeightCm * .5f));
	RightLoadWheelVisual->SetRelativeLocation(FVector(Dimensions.DriveWheelXcm + Dimensions.WheelbaseCm, -Dimensions.LoadWheelLateralOffsetCm, Dimensions.LoadWheelRadiusCm - Dimensions.BodyHeightCm * .5f));
	DriveWheelVisual->SetRelativeScale3D(FVector::OneVector); LeftLoadWheelVisual->SetRelativeScale3D(FVector::OneVector); RightLoadWheelVisual->SetRelativeScale3D(FVector::OneVector);
	ReachVisual->SetRelativeLocation(FVector(0.f, 0.f, ReachVisualBaseHeightCm)); ReachVisual->SetRelativeScale3D(FVector::OneVector);
	ForkCarriageVisual->SetRelativeLocation(FVector(ForkMountForwardOffsetCm, 0.f, ForkCarriageBaseHeightCm)); ForkCarriageVisual->SetRelativeScale3D(FVector::OneVector);
	LeftForkVisual->SetRelativeScale3D(FVector::OneVector); RightForkVisual->SetRelativeScale3D(FVector::OneVector);
	ForkCollision->SetRelativeLocation(FVector(ForkMountForwardOffsetCm + Dimensions.ForkLengthCm * .5f, 0, Dimensions.ForkThicknessCm * .5f));
	ForkCollision->SetBoxExtent(FVector(Dimensions.ForkLengthCm * .5f, 35.f, Dimensions.ForkThicknessCm * .5f));
	for (const TPair<UBoxComponent*, float>& Tine : {TPair<UBoxComponent*, float>(LeftForkCollision, -30.f), TPair<UBoxComponent*, float>(RightForkCollision, 30.f)})
	{
		Tine.Key->SetRelativeLocation(FVector(ForkMountForwardOffsetCm + Dimensions.ForkLengthCm * .5f, Tine.Value, Dimensions.ForkThicknessCm * .5f));
		Tine.Key->SetBoxExtent(FVector(Dimensions.ForkLengthCm * .5f, Dimensions.ForkWidthCm * .5f, Dimensions.ForkThicknessCm * .5f));
	}
	for (const TPair<UBoxComponent*, float>& SupportLeg : {TPair<UBoxComponent*, float>(LeftSupportLegCollision, 57.f), TPair<UBoxComponent*, float>(RightSupportLegCollision, -57.f)})
	{
		SupportLeg.Key->SetRelativeLocation(FVector(48.f, SupportLeg.Value, -36.5f));
		SupportLeg.Key->SetBoxExtent(FVector(71.f, 10.f, 7.f));
	}
	UpdatePalletSensorSettings();
}

void AAutomatedForklift::ConfigureKinematicForkMechanism()
{
	// The chassis and all forklift mechanisms are kinematic C++ hierarchy.
	// Only the pallet remains a dynamic Chaos body. This eliminates pendulum
	// degrees of freedom without changing the visible, separately shaped forks.
	BodyCollision->SetSimulatePhysics(false);
	for (UBoxComponent* Proxy : {LiftPhysicsBody.Get(), ReachPhysicsBody.Get(), ForkPhysicsBody.Get()})
	{
		Proxy->SetSimulatePhysics(false);
		Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	for (UPhysicsConstraintComponent* Constraint : {LiftConstraint.Get(), ReachConstraint.Get(), SideShiftConstraint.Get(), LeftForkFixedConstraint.Get(), RightForkFixedConstraint.Get()})
	{
		Constraint->TermComponentConstraint();
		Constraint->SetComponentTickEnabled(false);
	}
	LiftCarriage->AttachToComponent(MastAssembly, FAttachmentTransformRules::KeepRelativeTransform);
	ReachCarriage->AttachToComponent(LiftCarriage, FAttachmentTransformRules::KeepRelativeTransform);
	ForkCarriage->AttachToComponent(ReachCarriage, FAttachmentTransformRules::KeepRelativeTransform);
	for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get()})
	{
		Tine->SetSimulatePhysics(false);
		Tine->SetEnableGravity(false);
		Tine->AttachToComponent(ForkCarriage, FAttachmentTransformRules::KeepRelativeTransform);
		Tine->SetCollisionProfileName(TEXT("BlockAll"));
		Tine->SetCollisionObjectType(ECC_WorldDynamic);
		Tine->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Tine->SetUseCCD(true);
	}
	ApplyDimensions();
	ConfigureLiftHeightReference();
	LiftCm = ReachCm = SideShiftCm = 0.f;
	bChaosConfigured = false;
	PalletSupportDebugStatus = TEXT("Pallet: dynamic Chaos contact only (no attachment)");
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_KINEMATIC_FORK_V7] Lift->Reach->Fork scene hierarchy active; dynamic fork constraints disabled; tine collision=QueryAndPhysics, pallet remains Chaos."));
}

void AAutomatedForklift::SetConstraintFramesAtPose(UPhysicsConstraintComponent* Constraint, UPrimitiveComponent* Body1, UPrimitiveComponent* Body2, const FTransform& JointWorld, const FVector& Frame2Offset)
{
	if (!Constraint || !Body1 || !Body2) return;
	const FQuat JointRotation = JointWorld.GetRotation();
	auto MakeLocalFrame = [&JointRotation](const UPrimitiveComponent* Body, const FVector& WorldLocation)
	{
		const FTransform& BodyWorld = Body->GetComponentTransform();
		return FTransform(BodyWorld.GetRotation().Inverse() * JointRotation, BodyWorld.InverseTransformPosition(WorldLocation));
	};
	Constraint->SetConstraintReferenceFrame(EConstraintFrame::Frame1, MakeLocalFrame(Body1, JointWorld.GetLocation()));
	const FVector Frame2WorldLocation = JointWorld.TransformPosition(Frame2Offset);
	Constraint->SetConstraintReferenceFrame(EConstraintFrame::Frame2, MakeLocalFrame(Body2, Frame2WorldLocation));
}

FVector AAutomatedForklift::GetConstraintFrameDelta(const UPhysicsConstraintComponent* Constraint, const UPrimitiveComponent* Body1, const UPrimitiveComponent* Body2) const
{
	if (!Constraint || !Body1 || !Body2) return FVector::ZeroVector;
	const FTransform Frame1World = Constraint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1) * Body1->GetComponentTransform();
	const FTransform Frame2World = Constraint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2) * Body2->GetComponentTransform();
	return Frame1World.InverseTransformPosition(Frame2World.GetLocation());
}

float AAutomatedForklift::GetForkTopHeightAboveGroundCm() const
{
	if (!BodyCollision || !LeftForkCollision || !RightForkCollision) return 0.f;
	const FVector GroundWorld = BodyCollision->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, GroundReferenceLocalZcm));
	const FVector Up = BodyCollision->GetUpVector();
	const auto TopAlongTruckUp = [&Up, &GroundWorld](const UBoxComponent* Tine)
	{
		return FVector::DotProduct(Tine->GetComponentLocation() - GroundWorld, Up) + Tine->GetScaledBoxExtent().Z;
	};
	return FMath::Min(TopAlongTruckUp(LeftForkCollision), TopAlongTruckUp(RightForkCollision));
}

void AAutomatedForklift::ConfigureLiftHeightReference()
{
	// The existing floor-placement rule is body bottom + 2cm clearance. Keep
	// the reference in vehicle-local space so elevated Map1 geometry and actor
	// placement never change the measured fork height.
	GroundReferenceLocalZcm = -Dimensions.BodyHeightCm * .5f - 2.f;
	LowestForkTopHeightAboveGroundCm = GetForkTopHeightAboveGroundCm();
	const float LegacyMaximumForkTopCm = LowestForkTopHeightAboveGroundCm + Dimensions.MaximumLiftHeightCm;
	RequiredLiftStrokeCm = FMath::Max(0.f, MaximumForkTopHeightAboveGroundCm - LowestForkTopHeightAboveGroundCm);
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_HEIGHT_V4] groundLocalZ=%.2f lowestForkTop=%.2f legacyStroke=%.2f legacyForkTop=%.2f targetForkTop=%.2f requiredStroke=%.2f"),
		GroundReferenceLocalZcm, LowestForkTopHeightAboveGroundCm, Dimensions.MaximumLiftHeightCm, LegacyMaximumForkTopCm, MaximumForkTopHeightAboveGroundCm, RequiredLiftStrokeCm);
}

void AAutomatedForklift::UpdateMastVisuals(float ActualLiftCm)
{
	// Mesh pivots are based on the imported, lowest-pose assembly.  Move the
	// two telescopic stages at different ratios so the carriage never appears
	// detached while preserving their original FBX scales.
	const float ClampedLift = FMath::Clamp(ActualLiftCm, 0.f, RequiredLiftStrokeCm);
	    MastInnerVisual->SetRelativeLocation(FVector(-8.f, 0.f, Dimensions.MaximumLiftHeightCm * .45f + ClampedLift * .5f));
	MastLiftVisual->SetRelativeLocation(FVector(-14.f, 0.f, Dimensions.MaximumLiftHeightCm * .38f + ClampedLift));
}

void AAutomatedForklift::UpdatePalletSensorSettings()
{

	if (!PalletSensorCapture || !PalletSensorRenderTarget || !PalletSensorDepthCapture || !PalletSensorDepthRenderTarget) return;

	// ForkCarriage's local origin is the side-shifting carriage.  The drawing
	// offsets are referenced to physical fork/backrest geometry, so convert them
	// here instead of treating 19/6.3/2.6 as arbitrary local XYZ values.
	constexpr float BackrestPlaneLocalXcm = 20.f; // ForkCarriageVisual mount plane.
	const float ForkTopLocalZcm = Dimensions.ForkThicknessCm;
	const FVector CarriageLocalLocation(
		BackrestPlaneLocalXcm + FMath::Max(PalletSensorForwardOfBackrestCm, PalletSensorBackrestClearanceCm),
		PalletSensorLateralOffsetFromCenterCm,
		ForkTopLocalZcm + PalletSensorHeightAboveForkTopCm);
	PalletSensorCapture->SetRelativeLocation(CarriageLocalLocation);
	PalletSensorCapture->SetRelativeRotation(PalletSensorRelativeRotation);
	PalletSensorCapture->FOVAngle = PalletSensorFieldOfViewDeg;
	PalletSensorCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	PalletSensorCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	PalletSensorCapture->PostProcessSettings.AutoExposureMethod = bPalletSensorUseManualExposure ? EAutoExposureMethod::AEM_Manual : EAutoExposureMethod::AEM_Histogram;
	PalletSensorCapture->PostProcessSettings.bOverride_AutoExposureBias = bPalletSensorUseManualExposure;
	PalletSensorCapture->PostProcessSettings.AutoExposureBias = PalletSensorExposureBias;
	PalletSensorCapture->ShowFlags.SetEyeAdaptation(!bPalletSensorUseManualExposure);
	PalletSensorCapture->TextureTarget = PalletSensorRenderTarget;

	PalletSensorDepthCapture->SetRelativeLocation(CarriageLocalLocation);
	PalletSensorDepthCapture->SetRelativeRotation(PalletSensorRelativeRotation);
	PalletSensorDepthCapture->FOVAngle = PalletSensorFieldOfViewDeg;
	PalletSensorDepthCapture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	PalletSensorDepthCapture->TextureTarget = PalletSensorDepthRenderTarget;

	if (PalletSensorRenderTarget->SizeX != PalletSensorWidthPx || PalletSensorRenderTarget->SizeY != PalletSensorHeightPx || PalletSensorRenderTarget->GetFormat() != PF_B8G8R8A8)
	{
		PalletSensorRenderTarget->InitCustomFormat(PalletSensorWidthPx, PalletSensorHeightPx, PF_B8G8R8A8, false);
	}
	PalletSensorRenderTarget->RenderTargetFormat = RTF_RGBA8;
	PalletSensorRenderTarget->bForceLinearGamma = false;
	PalletSensorRenderTarget->TargetGamma = 2.2f;

	if (PalletSensorDepthRenderTarget->SizeX != PalletSensorWidthPx || PalletSensorDepthRenderTarget->SizeY != PalletSensorHeightPx)
	{
		PalletSensorDepthRenderTarget->ResizeTarget(PalletSensorWidthPx, PalletSensorHeightPx);
	}

}

void AAutomatedForklift::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_FIX_V2] SetupPlayerInputComponent forklift=%s input=%s; binding ForkliftMoveForward and raw K"), *GetName(), *GetNameSafe(Input));
	Input->BindAxis(TEXT("ForkliftMoveForward"), this, &AAutomatedForklift::Drive);
	Input->BindAxis(TEXT("ForkliftSteer"), this, &AAutomatedForklift::Steer);
	Input->BindAxis(TEXT("ForkliftLift"), this, &AAutomatedForklift::Lift);
	Input->BindAxis(TEXT("ForkliftReach"), this, &AAutomatedForklift::Reach);
	Input->BindAxis(TEXT("ForkliftSideShift"), this, &AAutomatedForklift::SideShift);
	Input->BindAxis(TEXT("ForkliftCameraYaw"), this, &AAutomatedForklift::CameraYaw);
	Input->BindAxis(TEXT("ForkliftCameraPitch"), this, &AAutomatedForklift::CameraPitch);
	Input->BindAxis(TEXT("ForkliftCameraZoom"), this, &AAutomatedForklift::CameraZoom);
	Input->BindAction(TEXT("ForkliftCameraOrbit"), IE_Pressed, this, &AAutomatedForklift::BeginCameraOrbit);
	Input->BindAction(TEXT("ForkliftCameraOrbit"), IE_Released, this, &AAutomatedForklift::EndCameraOrbit);
	Input->BindAction(TEXT("ForkliftCameraReset"), IE_Pressed, this, &AAutomatedForklift::ResetCamera);
	Input->BindAction(TEXT("ForkliftSpawnPallet"), IE_Pressed, this, &AAutomatedForklift::SpawnPallet);
	Input->BindAction(TEXT("ForkliftPalletInteract"), IE_Pressed, this, &AAutomatedForklift::InteractWithPallet);
	Input->BindKey(EKeys::K, IE_Pressed, this, &AAutomatedForklift::ToggleCollisionDebug);
	Input->BindKey(EKeys::V, IE_Pressed, this, &AAutomatedForklift::TogglePalletSensorDisplay);
	Input->BindKey(EKeys::N, IE_Pressed, this, &AAutomatedForklift::TogglePalletSensorCapture);
	Input->BindKey(EKeys::O, IE_Pressed, this, &AAutomatedForklift::ToggleVisionDebugView);

	Input->BindKey(EKeys::U, IE_Pressed, this, &AAutomatedForklift::ToggleDepthDebugView);
	Input->BindKey(EKeys::P, IE_Pressed, this, &AAutomatedForklift::ToggleAutoApproachStraightDiagnostic);
	Input->BindKey(EKeys::J, IE_Pressed, this, &AAutomatedForklift::ToggleDetailedSensorDiagnostics);

	Input->BindKey(EKeys::I, IE_Pressed, this, &AAutomatedForklift::ToggleSensorOcclusionDiagnostic);
	Input->BindKey(EKeys::L, IE_Pressed, this, &AAutomatedForklift::ToggleLidarDebug);

	if (UForkliftSlamComponent* Slam = FindComponentByClass<UForkliftSlamComponent>())
	{
		Input->BindKey(EKeys::B, IE_Pressed, Slam, &UForkliftSlamComponent::ResetMap);
	}

	Input->BindKey(EKeys::M, IE_Pressed, this, &AAutomatedForklift::ToggleSlamMapDebug);


}
#if 0 // duplicate input implementation from the pre-navigation merge branch
void AAutomatedForklift::LogInputInvocation(const TCHAR* InputName, float Value)
{
	if (!GetWorld() || FMath::IsNearlyZero(Value)) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastInputLogTime >= .25f)
	{
		LastInputLogTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_FIX_V2] Input invoked: forklift=%s input=%s value=%.3f carried=%s lift=%.2f actorLocation=%s"),
			*GetName(), InputName, Value, *GetNameSafe(CarriedPallet), LiftCm, *GetActorLocation().ToString());
	}
}

void AAutomatedForklift::Drive(float V) { if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 전후진 입력"), false); DriveInput = V; LogInputInvocation(TEXT("ForkliftMoveForward(W/S)"), V); } void AAutomatedForklift::Steer(float V) { if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 조향 입력"), false); SteerInput = V; }
void AAutomatedForklift::Lift(float V) { if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 인양 입력"), false); LiftInput = V; LogInputInvocation(TEXT("ForkliftLift(E/Q)"), V); }
void AAutomatedForklift::Reach(float V) { if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 리치 입력"), false); ReachInput = V; LogInputInvocation(TEXT("ForkliftReach(R/F)"), V); }
void AAutomatedForklift::SideShift(float V) { if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 좌우 이동 입력"), false); SideShiftInput = V; LogInputInvocation(TEXT("ForkliftSideShift(Z/C)"), V); }

void AAutomatedForklift::Drive(float V) { if (!FMath::IsNearlyZero(V) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 전후진 입력"), false); DriveInput = V; LogInputInvocation(TEXT("ForkliftMoveForward(W/S)"), V); } void AAutomatedForklift::Steer(float V) { if (!FMath::IsNearlyZero(V) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 조향 입력"), false); SteerInput = V; }
void AAutomatedForklift::Lift(float V) { if (!FMath::IsNearlyZero(V) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 인양 입력"), false); LiftInput = V; LogInputInvocation(TEXT("ForkliftLift(E/Q)"), V); }
void AAutomatedForklift::Reach(float V) { if (!FMath::IsNearlyZero(V) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 리치 입력"), false); ReachInput = V; LogInputInvocation(TEXT("ForkliftReach(R/F)"), V); }
void AAutomatedForklift::SideShift(float V) { if (!FMath::IsNearlyZero(V) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(V) && AutoPickupState > EAutoPickupState::Idle && AutoPickupState < EAutoPickupState::Complete) CancelAutoPickup(TEXT("수동 좌우 이동 입력"), false); SideShiftInput = V; LogInputInvocation(TEXT("ForkliftSideShift(Z/C)"), V); }

#endif
void AAutomatedForklift::LogInputInvocation(const TCHAR* InputName, float Value)
{
	if (!GetWorld() || FMath::IsNearlyZero(Value)) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastInputLogTime >= .25f)
	{
		LastInputLogTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_INPUT] forklift=%s input=%s value=%.3f carried=%s lift=%.2f"), *GetName(), InputName, Value, *GetNameSafe(CarriedPallet), LiftCm);
	}
}
void AAutomatedForklift::Drive(float Value) { if (!FMath::IsNearlyZero(Value) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(Value) && IsAutoPickupMotionActive()) CancelAutoPickup(TEXT("manual drive input"), false); DriveInput = Value; LogInputInvocation(TEXT("ForkliftMoveForward(W/S)"), Value); }
void AAutomatedForklift::Steer(float Value) { if (!FMath::IsNearlyZero(Value) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(Value) && IsAutoPickupMotionActive()) CancelAutoPickup(TEXT("manual steering input"), false); SteerInput = Value; }
void AAutomatedForklift::Lift(float Value) { if (!FMath::IsNearlyZero(Value) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(Value) && IsAutoPickupMotionActive()) CancelAutoPickup(TEXT("manual lift input"), false); LiftInput = Value; LogInputInvocation(TEXT("ForkliftLift(E/Q)"), Value); }
void AAutomatedForklift::Reach(float Value) { if (!FMath::IsNearlyZero(Value) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(Value) && IsAutoPickupMotionActive()) CancelAutoPickup(TEXT("manual reach input"), false); ReachInput = Value; LogInputInvocation(TEXT("ForkliftReach(R/F)"), Value); }
void AAutomatedForklift::SideShift(float Value) { if (!FMath::IsNearlyZero(Value) && bNavigationActive) StopNavigation(); if (!FMath::IsNearlyZero(Value) && IsAutoPickupMotionActive()) CancelAutoPickup(TEXT("manual side-shift input"), false); SideShiftInput = Value; LogInputInvocation(TEXT("ForkliftSideShift(Z/C)"), Value); }

void AAutomatedForklift::BeginCameraOrbit() { bCameraOrbiting = true; }
void AAutomatedForklift::EndCameraOrbit() { bCameraOrbiting = false; }
void AAutomatedForklift::ResetCamera() { CameraPitchDeg = -24.f; CameraYawDeg = -35.f; CameraDistanceCm = 700.f; CameraBoom->SetRelativeRotation(FRotator(CameraPitchDeg, CameraYawDeg, 0.f)); CameraBoom->TargetArmLength = CameraDistanceCm; }
void AAutomatedForklift::ToggleCollisionDebug()
{
	bCollisionDebugVisible = !bCollisionDebugVisible;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_FIX_V2] K ToggleCollisionDebug invoked: forklift=%s debugVisible=%d tickEnabled=%d"), *GetName(), bCollisionDebugVisible, IsActorTickEnabled());
	if (GEngine) GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), 2.f, bCollisionDebugVisible ? FColor::Green : FColor::Silver,
		bCollisionDebugVisible ? TEXT("Forklift collision debug: ON") : TEXT("Forklift collision debug: OFF"));
}
void AAutomatedForklift::TogglePalletSensorDisplay()
{
	// V owns one paired RGB+Depth preview.  A merge left a second 0/1/2 mode
	// increment below this binary toggle, which turned 0 -> 2 and 1 -> 2 so the
	// HUD could never return to its LiDAR/SLAM-only base view.
	PalletSensorDisplayMode = PalletSensorDisplayMode == 0 ? 1 : 0;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_SENSOR] V paired RGB+Depth preview=%s captureActive=%d"),
		PalletSensorDisplayMode != 0 ? TEXT("ON") : TEXT("OFF"), bPalletSensorCaptureActive);
}
void AAutomatedForklift::TogglePalletSensorCapture()
{
	bPalletSensorCaptureActive = !bPalletSensorCaptureActive;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_SENSOR] N capture active=%d displayMode=%d"), bPalletSensorCaptureActive, PalletSensorDisplayMode);
}
void AAutomatedForklift::ToggleVisionDebugView()
{

	VisionDebugViewMode = (VisionDebugViewMode + 1) % 6;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_VISION] O debug view=%d (0=RT source, 1=colour region, 2=RGB body, 3=depth/refined body, 4=opening, 5=CPU FColor)"), VisionDebugViewMode);
}
void AAutomatedForklift::ToggleDepthDebugView()
{
	DepthDebugViewMode = (DepthDebugViewMode + 1) % 4;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_RGBD] U depth view=%d (0=close grey, 1=close colour, 2=wide grey, 3=wide colour)"), DepthDebugViewMode);
}
void AAutomatedForklift::ToggleDetailedSensorDiagnostics()
{
	bDetailedSensorDiagnostics = !bDetailedSensorDiagnostics;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_VISION] J detailed HUD=%d"), bDetailedSensorDiagnostics);
}
void AAutomatedForklift::ToggleAutoApproachStraightDiagnostic()
{
	bAutoApproachStraightDiagnostic = !bAutoApproachStraightDiagnostic;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_APPROACH] straight-diagnostic=%d (P); approach steering is %s"), bAutoApproachStraightDiagnostic, bAutoApproachStraightDiagnostic ? TEXT("disabled") : TEXT("enabled"));

	VisionDebugViewMode = (VisionDebugViewMode + 1) % 2;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_VISION] O debug view=%d (0=source, 1=binary mask)"), VisionDebugViewMode);

}
void AAutomatedForklift::ToggleSensorOcclusionDiagnostic()
{
	bSensorOcclusionDiagnosticHideForks = !bSensorOcclusionDiagnosticHideForks;
	if (!PalletSensorCapture || !PalletSensorDepthCapture) return;
	// RGB and depth must keep the same visibility set.  Hiding fork geometry in
	// only RGB creates a false image/depth correspondence during diagnostics.
	PalletSensorCapture->ClearHiddenComponents();
	PalletSensorDepthCapture->ClearHiddenComponents();
	if (bSensorOcclusionDiagnosticHideForks)
	{
		for (UPrimitiveComponent* Part : {static_cast<UPrimitiveComponent*>(LeftForkVisual.Get()), static_cast<UPrimitiveComponent*>(RightForkVisual.Get()), static_cast<UPrimitiveComponent*>(ForkCarriageVisual.Get()), static_cast<UPrimitiveComponent*>(ReachVisual.Get())})
		{
			if (!Part) continue;
			PalletSensorCapture->HideComponent(Part);
			PalletSensorDepthCapture->HideComponent(Part);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_VISION] I paired RGB-D fork/carriage occlusion diagnostic=%d; this does not change vehicle physics."), bSensorOcclusionDiagnosticHideForks);
}
void AAutomatedForklift::ToggleLidarDebug()
{
	if (NavigationSensor) NavigationSensor->ToggleDebugVisible();
}

void AAutomatedForklift::ToggleSlamMapDebug()
{
	if (SlamComponent) SlamComponent->ToggleMap();
}

void AAutomatedForklift::CameraYaw(float V) { if (bCameraOrbiting && !FMath::IsNearlyZero(V)) { CameraYawDeg += V * 2.f; CameraBoom->SetRelativeRotation(FRotator(CameraPitchDeg, CameraYawDeg, 0.f)); } }
void AAutomatedForklift::CameraPitch(float V) { if (bCameraOrbiting && !FMath::IsNearlyZero(V)) { CameraPitchDeg = FMath::Clamp(CameraPitchDeg + V * 2.f, CameraMinPitchDeg, CameraMaxPitchDeg); CameraBoom->SetRelativeRotation(FRotator(CameraPitchDeg, CameraYawDeg, 0.f)); } }
void AAutomatedForklift::CameraZoom(float V) { if (!FMath::IsNearlyZero(V)) { CameraDistanceCm = FMath::Clamp(CameraDistanceCm - V * CameraZoomStepCm, CameraMinDistanceCm, CameraMaxDistanceCm); CameraBoom->TargetArmLength = CameraDistanceCm; } }

bool AAutomatedForklift::IsMovementDeeperIntoInitialOverlap(const FHitResult& Hit, const FVector& WorldDelta) const
{
	if (!Hit.bStartPenetrating) return true;
	FVector EscapeNormal = Hit.Normal.GetSafeNormal();
	if (EscapeNormal.IsNearlyZero()) EscapeNormal = Hit.ImpactNormal.GetSafeNormal();
	// Sweep MTD normals point out of the overlap. Moving against that direction
	// deepens it; moving with it is an allowed escape attempt.
	// A tangential move (dot ~= 0) cannot make a penetration deeper.  This is
	// important for a raised load whose deck has a stale floor overlap at the
	// start of a horizontal sweep: W/S must still permit it to escape/slide.
	return EscapeNormal.IsNearlyZero() || FVector::DotProduct(WorldDelta, EscapeNormal) < -KINDA_SMALL_NUMBER;
}

void AAutomatedForklift::RecordCollision(const TCHAR* Operation, UPrimitiveComponent* SourceComponent, const FHitResult& Hit, const FVector& WorldDelta, bool bBlocked)
{
	LastCollisionHit = Hit;
	LastBlockingComponent = SourceComponent;
	LastCollisionHitExpireTime = GetWorld() ? GetWorld()->GetTimeSeconds() + 1.f : 0.f;
	const FString Signature = FString::Printf(TEXT("%s|%s|%s|%s|%d"), Operation, *GetNameSafe(SourceComponent), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), Hit.bStartPenetrating);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (Signature != LastCollisionLogSignature || Now - LastCollisionLogTime >= .75f)
	{
		LastCollisionLogSignature = Signature;
		LastCollisionLogTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_FIX_V2] Forklift collision %s: operation=%s source=%s sourceOwner=%s sourceLocation=%s otherActor=%s otherComponent=%s direction=%s impact=%s normal=%s blocking=%d startPenetrating=%d time=%.3f"),
			bBlocked ? TEXT("BLOCK") : TEXT("ESCAPE/OBSERVE"), Operation, *GetNameSafe(SourceComponent),
			*GetNameSafe(SourceComponent ? SourceComponent->GetOwner() : nullptr), SourceComponent ? *SourceComponent->GetComponentLocation().ToString() : TEXT("(none)"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
			*WorldDelta.GetSafeNormal().ToString(), *Hit.ImpactPoint.ToString(), *Hit.ImpactNormal.ToString(), Hit.bBlockingHit, Hit.bStartPenetrating, Hit.Time);
	}
	CollisionDebugStatus = FString::Printf(TEXT("%s: %s vs %s (%s)%s"), Operation, *GetNameSafe(SourceComponent), *GetNameSafe(Hit.GetComponent()),
		*GetNameSafe(Hit.GetActor()), Hit.bStartPenetrating ? TEXT(" [initial overlap]") : TEXT(""));
}

bool AAutomatedForklift::SweepForkliftPart(UBoxComponent* Component, const FVector& WorldDelta, const TCHAR* Operation, FHitResult& OutHit, const AActor* ExtraIgnoredActor)
{
	if (!Component || WorldDelta.IsNearlyZero()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ForkliftPartSweep), false, this);
	if (CarriedPallet) Params.AddIgnoredActor(CarriedPallet);
	if (ExtraIgnoredActor) Params.AddIgnoredActor(ExtraIgnoredActor);
	if (!GetWorld()->SweepSingleByChannel(OutHit, Component->GetComponentLocation(), Component->GetComponentLocation() + WorldDelta,
		Component->GetComponentQuat(), ECC_WorldDynamic, FCollisionShape::MakeBox(Component->GetScaledBoxExtent()), Params) || !OutHit.bBlockingHit) return false;
	const bool bBlocked = !OutHit.bStartPenetrating || IsMovementDeeperIntoInitialOverlap(OutHit, WorldDelta);
	RecordCollision(Operation, Component, OutHit, WorldDelta, bBlocked);
	return bBlocked;
}

bool AAutomatedForklift::SweepForkPartAgainstFixedObstacle(UBoxComponent* Component, const FVector& WorldDelta, const TCHAR* Operation, FHitResult& OutHit)
{
	if (!Component || WorldDelta.IsNearlyZero()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ForkFixedObstacleSweep), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	if (!GetWorld()->SweepSingleByObjectType(OutHit, Component->GetComponentLocation(), Component->GetComponentLocation() + WorldDelta,
		Component->GetComponentQuat(), Objects, FCollisionShape::MakeBox(Component->GetScaledBoxExtent()), Params) || !OutHit.bBlockingHit) return false;
	const bool bBlocked = !OutHit.bStartPenetrating || IsMovementDeeperIntoInitialOverlap(OutHit, WorldDelta);
	RecordCollision(Operation, Component, OutHit, WorldDelta, bBlocked);
	return bBlocked;
}

bool AAutomatedForklift::IsForkAxisMotionBlockedByFixedObstacle(const FVector& WorldDelta, const TCHAR* Operation)
{
	for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get()})
	{
		FHitResult Hit;
		if (SweepForkPartAgainstFixedObstacle(Tine, WorldDelta, Operation, Hit)) return true;
	}
	return false;
}

bool AAutomatedForklift::WouldForkRotationHitFixedObstacle(float DeltaYawDegrees) const
{
	if (FMath::IsNearlyZero(DeltaYawDegrees) || !GetWorld()) return false;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ForkFixedRotationOverlap), false, this);
	const FVector Pivot = GetActorLocation();
	// A single end-pose overlap can miss a long fork sweeping through a rack at
	// a low frame rate.  Test short yaw increments, not just the final pose.
	const int32 StepCount = FMath::Max(1, FMath::CeilToInt(FMath::Abs(DeltaYawDegrees) / 2.5f));
	// Check the actual outer envelope for a steering step.  The support legs
	// are deliberately included: the root box alone does not cover their full
	// lateral sweep, and the fork tines can be raised or extended beyond it.
	for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get(), LeftSupportLegCollision.Get(), RightSupportLegCollision.Get(), LeftMastRailCollision.Get(), RightMastRailCollision.Get(), MastTopCrossbarCollision.Get()})
	{
		if (!Tine) continue;
		const bool bCurrentOverlap = GetWorld()->OverlapAnyTestByObjectType(Tine->GetComponentLocation(), Tine->GetComponentQuat(), Objects, FCollisionShape::MakeBox(Tine->GetScaledBoxExtent()), Params);
		for (int32 StepIndex = 1; StepIndex <= StepCount; ++StepIndex)
		{
			const float StepYaw = DeltaYawDegrees * static_cast<float>(StepIndex) / static_cast<float>(StepCount);
			const FQuat RotationDelta(FVector::UpVector, FMath::DegreesToRadians(StepYaw));
			const FVector CandidateLocation = Pivot + RotationDelta.RotateVector(Tine->GetComponentLocation() - Pivot);
			const FQuat CandidateRotation = RotationDelta * Tine->GetComponentQuat();
			const bool bCandidateOverlap = GetWorld()->OverlapAnyTestByObjectType(CandidateLocation, CandidateRotation, Objects, FCollisionShape::MakeBox(Tine->GetScaledBoxExtent()), Params);
			if (bCandidateOverlap && !bCurrentOverlap)
			{
				UE_LOG(LogTemp, Warning, TEXT("[R16HD_KINEMATIC_FORK_V7] steering limited by fixed obstacle: part=%s yawDelta=%.2f step=%d/%d"), *GetNameSafe(Tine), DeltaYawDegrees, StepIndex, StepCount);
				return true;
			}
		}
	}
	return false;
}

bool AAutomatedForklift::SweepCarriedPallet(const FVector& WorldDelta, const TCHAR* Operation, FHitResult& OutHit)
{
	return SweepPalletAgainstExternal(CarriedPallet, WorldDelta, Operation, OutHit);
}

bool AAutomatedForklift::SweepPalletAgainstExternal(APallet* Pallet, const FVector& WorldDelta, const TCHAR* Operation, FHitResult& OutHit)
{
	if (!Pallet || WorldDelta.IsNearlyZero()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ForkliftCarriedPalletSweep), false, this);
	// Actor ignores cover the normal case.  Add every participating component as
	// well, so another collision component on this forklift or this same pallet
	// can never be reported as an external carried-load obstacle.
	Params.AddIgnoredActor(Pallet);
	TArray<UPrimitiveComponent*> ForkliftPrimitives;
	GetComponents<UPrimitiveComponent>(ForkliftPrimitives);
	for (UPrimitiveComponent* Primitive : ForkliftPrimitives) Params.AddIgnoredComponent(Primitive);
	for (UBoxComponent* PalletShape : Pallet->GetCollisionShapes()) if (PalletShape) Params.AddIgnoredComponent(PalletShape);
	FCollisionObjectQueryParams ObjectParams(FCollisionObjectQueryParams::AllObjects);
	for (UBoxComponent* Shape : Pallet->GetCollisionShapes())
	{
		if (!Shape) continue;
		FHitResult ShapeHit;
		if (GetWorld()->SweepSingleByObjectType(ShapeHit, Shape->GetComponentLocation(), Shape->GetComponentLocation() + WorldDelta,
			Shape->GetComponentQuat(), ObjectParams, FCollisionShape::MakeBox(Shape->GetScaledBoxExtent()), Params) && ShapeHit.bBlockingHit)
		{
			const bool bBlocked = !ShapeHit.bStartPenetrating || IsMovementDeeperIntoInitialOverlap(ShapeHit, WorldDelta);
			RecordCollision(Operation, Shape, ShapeHit, WorldDelta, bBlocked);
			if (bBlocked) { OutHit = ShapeHit; return true; }
		}
	}
	return false;
}

void AAutomatedForklift::DrawCollisionDebug()
{
	if (!bCollisionDebugVisible || !GetWorld()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (bEnableCollisionDebugHeartbeatLog && Now - LastCollisionDebugHeartbeatTime >= 1.f)
	{
		LastCollisionDebugHeartbeatTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_DEBUG_HEARTBEAT] forklift=%s body=%s leftFork=%s rightFork=%s legs=(%s,%s) carried=%s support=%s"),
			*GetName(), *BodyCollision->GetComponentLocation().ToString(), *LeftForkCollision->GetComponentLocation().ToString(), *RightForkCollision->GetComponentLocation().ToString(),
			*LeftSupportLegCollision->GetComponentLocation().ToString(), *RightSupportLegCollision->GetComponentLocation().ToString(), *GetNameSafe(CarriedPallet), *PalletSupportDebugStatus);
	}
	const auto DrawBox = [this](UBoxComponent* Component)
	{
		if (!Component) return;
		const bool bHighlighted = Component == LastBlockingComponent.Get() && GetWorld()->GetTimeSeconds() <= LastCollisionHitExpireTime;
		DrawDebugBox(GetWorld(), Component->GetComponentLocation(), Component->GetScaledBoxExtent(), Component->GetComponentQuat(), bHighlighted ? FColor::Yellow : FColor::Red, false, 0.f, 0, 1.5f);
	};
	DrawBox(BodyCollision); DrawBox(LiftPhysicsBody); DrawBox(ReachPhysicsBody); DrawBox(ForkPhysicsBody); DrawBox(LeftForkCollision); DrawBox(RightForkCollision); DrawBox(LeftSupportLegCollision); DrawBox(RightSupportLegCollision); DrawBox(LeftMastRailCollision); DrawBox(RightMastRailCollision); DrawBox(MastTopCrossbarCollision);
	for (TActorIterator<APallet> It(GetWorld()); It; ++It)
	{
		for (UBoxComponent* Shape : It->GetCollisionShapes()) DrawBox(Shape);
		FHitResult PhysicsHit;
		UPrimitiveComponent* PhysicsOther = nullptr;
		if (It->GetRecentPhysicsContact(1.f, PhysicsHit, PhysicsOther))
		{
			const FVector Point = PhysicsHit.ImpactPoint;
			DrawDebugSphere(GetWorld(), Point, 8.f, 10, FColor::Cyan, false, 0.f, 0, 1.25f);
			DrawDebugDirectionalArrow(GetWorld(), Point, Point + PhysicsHit.ImpactNormal.GetSafeNormal() * 35.f, 10.f, FColor::Cyan, false, 0.f, 0, 1.25f);
		}
	}
	for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It)
	{
		for (UBoxComponent* Shape : It->GetCollisionShapes()) DrawBox(Shape);
		UPrimitiveComponent* PhysicsOther = nullptr;
		if (It->GetRecentForkContact(1.f, PhysicsOther) && PhysicsOther)
		{
			DrawDebugSphere(GetWorld(), PhysicsOther->GetComponentLocation(), 7.f, 10, FColor::Orange, false, 0.f, 0, 1.25f);
		}
	}
	if (GetWorld()->GetTimeSeconds() <= LastCollisionHitExpireTime && LastCollisionHit.bBlockingHit)
	{
		const FVector Point = LastCollisionHit.ImpactPoint;
		const FVector Normal = LastCollisionHit.ImpactNormal.GetSafeNormal();
		DrawDebugSphere(GetWorld(), Point, 10.f, 10, FColor::Yellow, false, 0.f, 0, 1.5f);
		DrawDebugDirectionalArrow(GetWorld(), Point, Point + Normal * 45.f, 12.f, FColor::Yellow, false, 0.f, 0, 1.5f);
	}
	if (GEngine) GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this) + 1, .1f, FColor::Yellow,
		FString::Printf(TEXT("%s | %s"), CollisionDebugStatus.IsEmpty() ? TEXT("Collision: no blocking hit") : *CollisionDebugStatus,
			PalletSupportDebugStatus.IsEmpty() ? TEXT("Pallet: not supported") : *PalletSupportDebugStatus));
	if (GEngine) GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this) + 2, .1f, FColor::Cyan,
		ActuatorDebugStatus.IsEmpty() ? TEXT("Chaos actuators: awaiting input") : ActuatorDebugStatus);
}

void AAutomatedForklift::DrawPalletSensorDebug() const
{
	if (!bCollisionDebugVisible || !GetWorld() || !PalletSensorCapture) return;
	const FVector Location = PalletSensorCapture->GetComponentLocation();
	const FVector Forward = PalletSensorCapture->GetForwardVector();
	const float HalfAngleRad = FMath::DegreesToRadians(PalletSensorFieldOfViewDeg * .5f);
	DrawDebugSphere(GetWorld(), Location, 7.f, 10, FColor::Cyan, false, 0.f, 0, 1.5f);
	DrawDebugDirectionalArrow(GetWorld(), Location, Location + Forward * 90.f, 16.f, FColor::Cyan, false, 0.f, 0, 2.f);
	DrawDebugCone(GetWorld(), Location, Forward, 180.f, HalfAngleRad, HalfAngleRad, 12, FColor::Cyan, false, 0.f, 0, 1.f);
}

void AAutomatedForklift::SpawnPallet()
{
	if (!GetWorld()) return;
	const FVector SpawnLocation = GetActorLocation() + GetActorForwardVector() * 300.f;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<APallet>(APallet::StaticClass(), FVector(SpawnLocation.X, SpawnLocation.Y, 1.f), GetActorRotation(), Params);
}

void AAutomatedForklift::InteractWithPallet()
{
	// Transport is intentionally contact-only.  Keeping this input as a visible
	// diagnostic prevents an old debug attachment path from silently replacing
	// the Chaos pallet response during camera/automation development.
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_KINEMATIC_FORK_V7] G attachment is disabled: lift the dynamic pallet by physical fork contact."));
	PalletSupportDebugStatus = TEXT("Pallet: physical fork contact (G attachment disabled)");
}

void AAutomatedForklift::UpdateSupportedLoadState()
{
	SupportedLoadKg = 0.f;
	ActiveLoadFriction = 0.f;
	ActiveLoadCollisionStatus = TEXT("No supported load");
	if (!GetWorld() || !LeftForkCollision || !RightForkCollision) { CurrentDriveSpeedMultiplier = 1.f; return; }

	// APallet has exact opening/insertion validation.  This keeps a nearby pallet
	// from being counted until both physical tines actually support it.
	for (TActorIterator<APallet> It(GetWorld()); It; ++It)
	{
		APallet* Pallet = *It;
		FString Reason;
		const float ForkTopZ = FMath::Min(
			LeftForkCollision->GetComponentLocation().Z + LeftForkCollision->GetScaledBoxExtent().Z,
			RightForkCollision->GetComponentLocation().Z + RightForkCollision->GetScaledBoxExtent().Z);
		if (Pallet && Pallet->CanSupportFromForks(LeftForkCollision, RightForkCollision, MinimumPalletSupportInsertionCm, Reason)
			&& ForkTopZ >= Pallet->GetForkSupportSurfaceWorldZ() - PalletSupportContactToleranceCm)
		{
			SupportedLoadKg += Pallet->GetConfiguredMassKg();
			ActiveLoadFriction = FMath::Max(ActiveLoadFriction, Pallet->GetSurfaceFriction());
			ActiveLoadCollisionStatus = TEXT("Forks supporting pallet");
		}
	}

	// Spawned wood pallets have separated collision rails.  Their support is
	// measured in the fork-carriage frame, after a recent physical fork contact.
	for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It)
	{
		AWoodPallet* Pallet = *It;
		if (Pallet && Pallet->CanSupportFromForks(LeftForkCollision, RightForkCollision, MinimumPalletSupportInsertionCm, PalletSupportContactToleranceCm))
		{
			SupportedLoadKg += Pallet->GetConfiguredMassKg();
			ActiveLoadFriction = FMath::Max(ActiveLoadFriction, Pallet->GetSurfaceFriction());
			ActiveLoadCollisionStatus = TEXT("Forks supporting wood pallet");
		}
	}

	bIsLoaded = SupportedLoadKg > KINDA_SMALL_NUMBER;
	const float LoadRatio = FMath::Clamp(SupportedLoadKg / FMath::Max(1.f, Dimensions.RatedLoadKg), 0.f, 1.f);
	CurrentDriveSpeedMultiplier = FMath::Lerp(1.f, RatedLoadSpeedMultiplier, LoadRatio);
}

void AAutomatedForklift::ApplyKinematicForkControls(float Dt)
{

	const bool bAuto = IsAutoPickupMotionActive();
	LastAppliedLiftInput = bAuto ? AutoLiftInput : LiftInput;
	LastAppliedReachInput = bAuto ? AutoReachInput : ReachInput;
	LastAppliedSideShiftInput = bAuto ? AutoSideShiftInput : SideShiftInput;
	const float LiftRate = LastAppliedLiftInput >= 0.f ? (bIsLoaded ? Motion.LoadedLiftSpeedCmPerSec : Motion.UnloadedLiftSpeedCmPerSec) : (bIsLoaded ? Motion.LoadedLowerSpeedCmPerSec : Motion.UnloadedLowerSpeedCmPerSec);
	float DesiredLift = FMath::Clamp(LiftCm + LastAppliedLiftInput * LiftRate * Dt, 0.f, RequiredLiftStrokeCm);
	float DesiredReach = FMath::Clamp(ReachCm + LastAppliedReachInput * Motion.ReachSpeedCmPerSec * Dt, 0.f, Dimensions.ReachTravelCm);
	float DesiredSide = FMath::Clamp(SideShiftCm + LastAppliedSideShiftInput * Motion.SideShiftSpeedCmPerSec * Dt, -Dimensions.ForkSideShiftCm, Dimensions.ForkSideShiftCm);

	if (IsForkAxisMotionBlockedByFixedObstacle(GetActorUpVector() * (DesiredLift - LiftCm), TEXT("kinematic lift fixed-obstacle sweep"))) DesiredLift = LiftCm;
	if (IsForkAxisMotionBlockedByFixedObstacle(GetActorForwardVector() * (DesiredReach - ReachCm), TEXT("kinematic reach fixed-obstacle sweep"))) DesiredReach = ReachCm;
	if (IsForkAxisMotionBlockedByFixedObstacle(GetActorRightVector() * (DesiredSide - SideShiftCm), TEXT("kinematic side-shift fixed-obstacle sweep"))) DesiredSide = SideShiftCm;
	LiftCm = DesiredLift;
	ReachCm = DesiredReach;
	SideShiftCm = DesiredSide;
	LiftCarriage->SetRelativeLocation(FVector(0.f, 0.f, LiftCm));
	ReachCarriage->SetRelativeLocation(FVector(ReachCm, 0.f, 0.f));
	ForkCarriage->SetRelativeLocation(FVector(0.f, SideShiftCm, 0.f));
	UpdateMastVisuals(LiftCm);
	ActuatorDebugStatus = FString::Printf(TEXT("KINEMATIC | ForkTop %.1f / %.1fcm | E/Q %.1f | R/F %.1f / %.1f | Z/C %.1f"), GetForkTopHeightAboveGroundCm(), MaximumForkTopHeightAboveGroundCm, LiftCm, ReachCm, Dimensions.ReachTravelCm, SideShiftCm);
}

FString AAutomatedForklift::AutoPickupStateToString(EAutoPickupState State)
{

	switch (State) { case EAutoPickupState::Idle: return TEXT("대기"); case EAutoPickupState::Detecting: return TEXT("인식 중"); case EAutoPickupState::Approaching: return TEXT("접근·방향 정렬 중"); case EAutoPickupState::Aligning: return TEXT("포크 정렬 중"); case EAutoPickupState::Inserting: return TEXT("삽입 중"); case EAutoPickupState::Lifting: return TEXT("인양 중"); case EAutoPickupState::Complete: return TEXT("완료"); case EAutoPickupState::Stopped: return TEXT("중지"); default: return TEXT("실패"); }
}

bool AAutomatedForklift::IsAutoPickupMotionActive() const
{
	return AutoPickupState == EAutoPickupState::Approaching || AutoPickupState == EAutoPickupState::Aligning || AutoPickupState == EAutoPickupState::Inserting || AutoPickupState == EAutoPickupState::Lifting;
}

float AAutomatedForklift::GetForkTipAheadOfSensorCm() const
{
	if (!LeftForkCollision || !RightForkCollision || !PalletSensorCapture) return 0.f;
	const FVector LeftTip = LeftForkCollision->GetComponentTransform().TransformPosition(FVector(LeftForkCollision->GetScaledBoxExtent().X, 0.f, 0.f));
	const FVector RightTip = RightForkCollision->GetComponentTransform().TransformPosition(FVector(RightForkCollision->GetScaledBoxExtent().X, 0.f, 0.f));
	return FVector::DotProduct((LeftTip + RightTip) * .5f - PalletSensorCapture->GetComponentLocation(), PalletSensorCapture->GetForwardVector());
}

bool AAutomatedForklift::ComputeAutoInsertionGeometry(const FOpeningDetection& Detection, float& OutForkTipToFrontCm, float& OutRemainingForkAdvanceCm, float& OutReachRemainingCm, float& OutChassisAdvanceCm, float& OutLateralErrorCm, float& OutLiftCorrectionCm) const
{
	OutForkTipToFrontCm = OutRemainingForkAdvanceCm = OutReachRemainingCm = OutChassisAdvanceCm = OutLateralErrorCm = OutLiftCorrectionCm = -BIG_NUMBER;
	if (!Detection.bDepthValid || !ForkCarriage || !LeftForkCollision || !RightForkCollision) return false;
	const FTransform ForkFrame = ForkCarriage->GetComponentTransform();
	const auto ToFork = [&ForkFrame](const FVector& World) { return ForkFrame.InverseTransformPosition(World); };
	const FVector LeftTip = ToFork(LeftForkCollision->GetComponentTransform().TransformPosition(FVector(LeftForkCollision->GetScaledBoxExtent().X, 0.f, 0.f)));
	const FVector RightTip = ToFork(RightForkCollision->GetComponentTransform().TransformPosition(FVector(RightForkCollision->GetScaledBoxExtent().X, 0.f, 0.f)));
	const FVector LeftTop = ToFork(LeftForkCollision->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, LeftForkCollision->GetScaledBoxExtent().Z)));
	const FVector RightTop = ToFork(RightForkCollision->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, RightForkCollision->GetScaledBoxExtent().Z)));
	const FVector TipMid = (LeftTip + RightTip) * .5f;
	const float ForkTopZ = (LeftTop.Z + RightTop.Z) * .5f;

	// All values here are ForkCarriage-local centimetres.  The optical-axis
	// SceneDepth was already unprojected into this frame by
	// UpdateDepthForOpeningDetection, so there is no camera-axis/reach-axis mix.
	OutForkTipToFrontCm = Detection.OpeningCenterInForkCm.X - TipMid.X;
	OutRemainingForkAdvanceCm = FMath::Max(0.f, OutForkTipToFrontCm + AutoRequiredInsertionCm);
	OutReachRemainingCm = FMath::Max(0.f, Dimensions.ReachTravelCm - ReachCm);
	OutChassisAdvanceCm = FMath::Max(0.f, OutRemainingForkAdvanceCm - OutReachRemainingCm);
	OutLateralErrorCm = Detection.OpeningCenterInForkCm.Y - TipMid.Y;
	const float LeftHeight = Detection.LeftMeasuredOpeningHeightCm > 1.f ? Detection.LeftMeasuredOpeningHeightCm : (Detection.MeasuredOpeningHeightCm > 1.f ? Detection.MeasuredOpeningHeightCm : VisionKnownOpeningHeightCm);
	const float RightHeight = Detection.RightMeasuredOpeningHeightCm > 1.f ? Detection.RightMeasuredOpeningHeightCm : (Detection.MeasuredOpeningHeightCm > 1.f ? Detection.MeasuredOpeningHeightCm : VisionKnownOpeningHeightCm);
	const FVector LeftCenter = Detection.LeftMeasuredOpeningHeightCm > 1.f ? Detection.LeftOpeningCenterInForkCm : Detection.OpeningCenterInForkCm;
	const FVector RightCenter = Detection.RightMeasuredOpeningHeightCm > 1.f ? Detection.RightOpeningCenterInForkCm : Detection.OpeningCenterInForkCm;
	// Fork-top bounds are evaluated in the same moving carriage frame as the
	// actual tine collision shapes.  Intersect both apertures; do not average a
	// narrow/noisy opening into a range the other tine cannot enter.
	const float AllowedMin = FMath::Max(LeftCenter.Z - LeftHeight*.5f + AutoForkOpeningBottomClearanceCm + Dimensions.ForkThicknessCm, RightCenter.Z - RightHeight*.5f + AutoForkOpeningBottomClearanceCm + Dimensions.ForkThicknessCm);
	const float AllowedMax = FMath::Min(LeftCenter.Z + LeftHeight*.5f - AutoForkOpeningBottomClearanceCm, RightCenter.Z + RightHeight*.5f - AutoForkOpeningBottomClearanceCm);
	const float DesiredForkTopZ = AllowedMin <= AllowedMax ? (AllowedMin + AllowedMax) * .5f : ForkTopZ;
	OutLiftCorrectionCm = DesiredForkTopZ - ForkTopZ;
	return true;
}

void AAutomatedForklift::ClearAutomaticCommands()
{
	AutoDriveInput = AutoSteerInput = AutoLiftInput = AutoReachInput = AutoSideShiftInput = 0.f;
}
void AAutomatedForklift::SetAutoPickupState(EAutoPickupState NewState, const FString& Status)
{
	AutoPickupState = NewState; AutoPickupStatus = Status; AutoPickupStateElapsed = 0.f;
	if (NewState == EAutoPickupState::Aligning) { AlignmentStableFrames = 0; LastStableAlignmentVisionFrame = -1; }
	if (NewState == EAutoPickupState::Approaching && GetWorld())
	{
		AutoApproachStartLocation = GetActorLocation();
		AutoApproachStartDistanceCm = OpeningDetection.EstimatedDistanceCm;
		LastAutoApproachTravelCm = 0.f;
		LastAutoCameraDistanceCm = OpeningDetection.EstimatedDistanceCm;
		LastAutoForkTipRemainingCm = OpeningDetection.EstimatedDistanceCm - GetForkTipAheadOfSensorCm();
		// The chassis is not driven to a magic camera/fork clearance.  It only
		// closes the distance that the remaining reach stroke cannot cover after
		// the requested physical insertion depth has been reserved.
		float TipToFront, RemainingAdvance, ReachRemaining, ChassisAdvance, Lateral, LiftCorrection;
		if (ComputeAutoInsertionGeometry(OpeningDetection, TipToFront, RemainingAdvance, ReachRemaining, ChassisAdvance, Lateral, LiftCorrection))
		{
			LastAutoForkTipRemainingCm = TipToFront;
			AutoRequiredForkAdvanceCm = RemainingAdvance;
			AutoChassisAdvanceNeededCm = ChassisAdvance;
			AutoApproachRequiredTravelCm = ChassisAdvance;
		}
		else { AutoRequiredForkAdvanceCm = AutoChassisAdvanceNeededCm = AutoApproachRequiredTravelCm = -BIG_NUMBER; }
		AutoApproachPlannedTravelCm = FMath::Max(0.f, AutoChassisAdvanceNeededCm);
		AutoApproachTravelBudgetCm = AutoApproachPlannedTravelCm + 35.f;
		AutoApproachStartForward = GetActorForwardVector();
		AutoApproachReplanCount = 0;
		AutoReachTargetCm = -BIG_NUMBER;
		AutoInsertionStartReachCm = -BIG_NUMBER;
		AutoApproachLastProgressDistanceCm = OpeningDetection.EstimatedDistanceCm;
		AutoApproachLastProgressTime = GetWorld()->GetTimeSeconds();
		bLastAutoApproachBlocked = false;
		FilteredApproachLateralCm = PreviousApproachLateralCm = OpeningDetection.EstimatedLateralCm;
		LastAutoApproachTravelForHeadingCm = 0.f; AutoApproachHeadingErrorRad = 0.f; LastAutoYawDeltaDeg = 0.f;
		bApproachSteeringInDeadband = FMath::Abs(FilteredApproachLateralCm) <= AutoApproachLateralDeadbandCm;
	}
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_PICK] state=%s reason=%s"), *AutoPickupStateToString(NewState), *Status);
}
void AAutomatedForklift::CancelAutoPickup(const FString& Reason, bool bFailure)
{
	ClearAutomaticCommands();
	if (bFailure)
	{
		LastAutoFailureReason = Reason;
		LastAutoFailureSensorFrameId = SensorFrameId;
		LastAutoFailureTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -BIG_NUMBER;
	}
	SetAutoPickupState(bFailure ? EAutoPickupState::Failed : EAutoPickupState::Stopped, Reason);
}
void AAutomatedForklift::ToggleAutoPickup()
{
	LastButtonInput = TEXT("시작"); LastButtonClickTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (AutoPickupState == EAutoPickupState::Detecting || AutoPickupState == EAutoPickupState::Approaching || AutoPickupState == EAutoPickupState::Aligning || AutoPickupState == EAutoPickupState::Inserting || AutoPickupState == EAutoPickupState::Lifting) { LastButtonInput = TEXT("중지"); CancelAutoPickup(TEXT("사용자가 작업 중지 버튼을 선택"), false); return; }
	if (!bPalletSensorCaptureActive) { SetAutoPickupState(EAutoPickupState::Failed, TEXT("카메라 캡처가 꺼져 있음 — N 키 또는 설정 확인 필요")); return; }
	// Start always permits RGB-D observation.  A non-zero fork height is a
	// measurement/alignment problem, not a reason to suppress the camera path.
	// If a fork is already inside or touching an object, later safety gates hold
	// motion and report that condition rather than lowering blindly.
	if (ReachCm > 3.f) UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_PICK] partial-reach restart: currentReach=%.1fcm; remaining insertion will be recomputed"), ReachCm);
	++AutoPickupAttemptId;
	ClearAutomaticCommands();
	LastAutoFailureReason.Reset(); LastAutoFailureSensorFrameId = -1; LastAutoFailureTime = -BIG_NUMBER;
	bSavedLastValidVisionArtifact = bSavedFirstVisionFailureArtifact = false; LastSuccessfulVisionPixels.Reset();
	LastTrackedOpeningDetection = FOpeningDetection(); LastTrackedOpeningTime = -BIG_NUMBER; LastTrackedActorLocation = FVector::ZeroVector; LastTrackedOpeningSeparationPx = 0.f; LockedTargetMissFrames = 0;
	FilteredApproachLateralCm = PreviousApproachLateralCm = AutoApproachHeadingErrorRad = 0.f; LastAutoApproachTravelForHeadingCm = 0.f; LastAutoYawDeltaDeg = AutoApproachAccumulatedYawDeg = 0.f; bApproachSteeringInDeadband = false; LastStableOpeningVisionFrame = LastStableAlignmentVisionFrame = -1; InvalidHeightRangeVisionFrames = 0; LastVisionTotalMs = LastVisionMaxMs = 0.f;
	OpeningStableFrames = AlignmentStableFrames = 0; LastVisionCandidateCount = -1; LastVisionFrameNumber = 0; LastProcessedSensorFrameId = -1; LastVisionSuccessSensorFrameId = FirstVisionFailureAfterSuccessSensorFrameId = -1; LastVisionProcessTime = -BIG_NUMBER; bLastVisionReadSucceeded = bLastVisionHadValidPair = false; LastPixelReadStatus = TEXT("카메라 프레임 대기"); LastAlignmentLateralErrorCm = LastAlignmentLiftCorrectionCm = LastMeasuredOpeningBottomCm = LastMeasuredOpeningTopCm = LastAllowedForkTopMinCm = LastAllowedForkTopMaxCm = LastForkTopLocalCm = LastHeightTargetLiftCm = LastAutoInsertionTravelCm = LastAutoApproachTravelCm = LastAutoCameraDistanceCm = LastAutoForkTipRemainingCm = AutoApproachRequiredTravelCm = AutoApproachPlannedTravelCm = AutoApproachTravelBudgetCm = AutoRequiredForkAdvanceCm = AutoChassisAdvanceNeededCm = AutoReachTargetCm = AutoInsertionStartReachCm = LastPhysicalPalletRiseCm = AutoRequiredForwardTravelCm = AutoMaximumForwardTravelCm = -BIG_NUMBER; bPreApproachHeightAlignment = false; AutoApproachReplanCount = 0; bLastAutoDriveBlocked = bLastAutoApproachBlocked = bLastPhysicalPalletContact = false;
	SetAutoPickupState(EAutoPickupState::Detecting, TEXT("시작 버튼 입력됨 — 안정적인 삽입구 쌍 영상 대기 중"));
}

void AAutomatedForklift::NotifyAutoPickupButtonClick(bool bInsideButton, const FVector2D& ScreenPosition)
{
	LastButtonClickTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	LastButtonInput = bInsideButton ? TEXT("버튼 영역 클릭") : TEXT("버튼 밖 좌클릭");
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_PICK_UI] forklift=%s click=(%.0f,%.0f) inside=%d state=%s"), *GetName(), ScreenPosition.X, ScreenPosition.Y, bInsideButton, *AutoPickupStateToString(AutoPickupState));
	if (bInsideButton) ToggleAutoPickup();
}

void AAutomatedForklift::GetAutoPickupDiagnosticLines(TArray<FString>& OutLines) const
{
	OutLines.Reset(); const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const auto AgeText = [Now](float Time) { return Time <= -BIG_NUMBER * .5f ? FString(TEXT("미측정")) : FString::Printf(TEXT("%.1fs"), FMath::Max(0.f, Now - Time)); };
	const auto ValueText = [](float Value, const TCHAR* Unit) { return Value <= -BIG_NUMBER * .5f ? FString(TEXT("미측정")) : FString::Printf(TEXT("%.1f%s"), Value, Unit); };
	OutLines.Add(FString::Printf(TEXT("상태: %s"), *AutoPickupStateToString(AutoPickupState)));
	OutLines.Add(FString::Printf(TEXT("시도: #%d / 센서 frame %d / 분석 frame %d / 성공 %d / 첫실패 %d"), AutoPickupAttemptId, SensorFrameId, LastProcessedSensorFrameId, LastVisionSuccessSensorFrameId, FirstVisionFailureAfterSuccessSensorFrameId));
	OutLines.Add(FString::Printf(TEXT("버튼: %s (%s 전)"), *LastButtonInput, *AgeText(LastButtonClickTime)));
	OutLines.Add(FString::Printf(TEXT("단계: %.1fs — %s"), AutoPickupStateElapsed, *AutoPickupStatus));
	OutLines.Add(FString::Printf(TEXT("영상: 캡처 %s / RT %s / 처리 #%d (%s 전) / 픽셀 %s"), bPalletSensorCaptureActive ? TEXT("켜짐") : TEXT("꺼짐"), PalletSensorRenderTarget ? TEXT("유효") : TEXT("없음"), LastVisionFrameNumber, *AgeText(LastVisionProcessTime), *LastPixelReadStatus));
	OutLines.Add(LastRgbReadStatistics);
	OutLines.Add(FString::Printf(TEXT("Depth: %s / frame %d / 유효픽셀 %d / 범위 %s..%s / %s"), bDepthFrameValid ? TEXT("수신됨") : TEXT("미수신"), DepthFrameId, DepthValidPixelCount, *ValueText(DepthObservedMinimumCm,TEXT("cm")), *ValueText(DepthObservedMaximumCm,TEXT("cm")), *LastDepthReadStatus));
	OutLines.Add(FString::Printf(TEXT("탐색: 16x16 색상 영역 병합 / 원본 2px 정밀 / 후보 ROI 시작 %s"), LastVisionSearchStartY >= 0 ? *FString::FromInt(LastVisionSearchStartY * 2) : TEXT("미측정")));
	OutLines.Add(FString::Printf(TEXT("인식: 후보 %s / 쌍 %s / 신뢰도 %s / 안정 %d/3"), LastVisionCandidateCount >= 0 ? *FString::FromInt(LastVisionCandidateCount) : TEXT("미측정"), bLastVisionHadValidPair ? TEXT("유효") : TEXT("없음"), OpeningDetection.TimestampSeconds > -BIG_NUMBER * .5f ? *FString::Printf(TEXT("%.2f"), OpeningDetection.Confidence) : TEXT("미측정"), OpeningStableFrames));
	OutLines.Add(FString::Printf(TEXT("Gates: RGB pair=%s | depth/front=%s | tracked=%s | height range=%s | motion=%s"), bLastVisionHadValidPair ? TEXT("valid") : TEXT("missing"), OpeningDetection.bDepthValid ? TEXT("valid") : TEXT("missing"), LastTrackedOpeningDetection.bValid ? TEXT("locked") : TEXT("none"), LastAllowedForkTopMinCm <= LastAllowedForkTopMaxCm ? TEXT("feasible") : TEXT("re-measure"), IsAutoPickupMotionActive() ? TEXT("permitted") : TEXT("held")));
	OutLines.Add(FString::Printf(TEXT("Depth pixels: valid=%d invalid=%d below=%d above=%d"), DepthValidPixelCount, DepthInvalidPixelCount, DepthBelowRangePixelCount, DepthAboveRangePixelCount));
	OutLines.Add(FString::Printf(TEXT("RGB-D sync: cycle RGB %d / depth %d / captured pose delta %.3fcm %.3fdeg"),
		RgbCaptureId, DepthCaptureId, LastRgbDepthCapturePositionDeltaCm, LastRgbDepthCaptureRotationDeltaDeg));
	OutLines.Add(FString::Printf(TEXT("Vision ms: total %.1f rgb %.1f depth %.1f coarse %.1f fine %.1f max %.1f"), LastVisionTotalMs, LastVisionRgbReadMs, LastVisionDepthReadMs, LastVisionCoarseMs, LastVisionFineMs, LastVisionMaxMs));
	if (!LastAutoFailureReason.IsEmpty()) OutLines.Add(FString::Printf(TEXT("Last auto failure: frame %d age %s — %s"), LastAutoFailureSensorFrameId, *AgeText(LastAutoFailureTime), *LastAutoFailureReason));
	const bool bObservationFresh = OpeningDetection.bValid && OpeningDetection.bDepthValid && OpeningDetection.TimestampSeconds > -BIG_NUMBER*.5f && Now-OpeningDetection.TimestampSeconds <= VisionMaximumFrameAgeSeconds;
	OutLines.Add(FString::Printf(TEXT("관측: %s / 현재 프레임 경과 %s / 마지막 잠금 경과 %s"), bObservationFresh ? TEXT("현재 RGB-D") : TEXT("오래됨/미측정"), *AgeText(OpeningDetection.TimestampSeconds), *AgeText(LastTrackedOpeningTime)));
	OutLines.Add(FString::Printf(TEXT("검출 단계: %s"), *LastVisionStageStatus));
	if (VisionPairDiagnostics.IsEmpty()) OutLines.Add(TEXT("쌍 판정: 미측정 또는 후보 부족"));
	else for (int32 Index = FMath::Max(0, VisionPairDiagnostics.Num()-2); Index < VisionPairDiagnostics.Num(); ++Index) OutLines.Add(VisionPairDiagnostics[Index]);
	OutLines.Add(FString::Printf(TEXT("정렬: 좌우 %s / 높이 %s / 명령 E/Q %.2f Z/C %.2f / 실제 높이 %.1f 좌우 %.1f"), *ValueText(LastAlignmentLateralErrorCm, TEXT("cm")), *ValueText(LastAlignmentLiftCorrectionCm, TEXT("cm")), LiftInput, SideShiftInput, LiftCm, SideShiftCm));
	OutLines.Add(FString::Printf(TEXT("접근: 명령 W/S %.2f 조향 %.2f / 실제속도 %.1fcm/s / 누적 %s / 시작계획 %s / 현재잔여 %s / 한계 %s / 재계획 %d / 차단 %s"), LastAppliedDriveInput, LastAppliedSteerInput, CurrentForwardSpeedCmPerSec, *ValueText(LastAutoApproachTravelCm, TEXT("cm")), *ValueText(AutoApproachPlannedTravelCm, TEXT("cm")), *ValueText(AutoApproachRequiredTravelCm, TEXT("cm")), *ValueText(AutoApproachTravelBudgetCm, TEXT("cm")), AutoApproachReplanCount, bLastAutoApproachBlocked ? TEXT("예") : TEXT("아니오")));
	const float ReachRemaining = FMath::Max(0.f, Dimensions.ReachTravelCm - ReachCm);
	OutLines.Add(FString::Printf(TEXT("거리: 카메라 %s / 포크끝-전면 %s"), *ValueText(LastAutoCameraDistanceCm, TEXT("cm")), *ValueText(LastAutoForkTipRemainingCm, TEXT("cm"))));
	OutLines.Add(FString::Printf(TEXT("계획: 필요 포크전진 %s (잔여+삽입) / R/F 잔여 %.1fcm / 차체 추가접근 %s"), *ValueText(AutoRequiredForkAdvanceCm, TEXT("cm")), ReachRemaining, *ValueText(AutoChassisAdvanceNeededCm, TEXT("cm"))));
	OutLines.Add(FString::Printf(TEXT("정렬 RGB-D: 좌우 %s / 높이 %s / 전면방향 %.1fdeg (허용 %.1fdeg) / 포크폭 %.1f < 입구 %.1f-여유 %.1f"), *ValueText(LastAlignmentLateralErrorCm, TEXT("cm")), *ValueText(LastAlignmentLiftCorrectionCm, TEXT("cm")), OpeningDetection.bDepthValid ? FMath::RadiansToDegrees(OpeningDetection.FrontYawErrorRad) : -999.f, AutoDirectionToleranceDeg, Dimensions.ForkWidthCm, VisionKnownOpeningWidthCm, AutoForkOpeningSideClearanceCm * 2.f));
	OutLines.Add(FString::Printf(TEXT("좌표: RGB-D 현재 ForkCarriage local=(X %.1f,Y %.1f,Z %.1f) / 삽입깊이 진행 %s / 남은 %s"), OpeningDetection.bDepthValid ? OpeningDetection.OpeningCenterInForkCm.X : -999.f, OpeningDetection.bDepthValid ? OpeningDetection.OpeningCenterInForkCm.Y : -999.f, OpeningDetection.bDepthValid ? OpeningDetection.OpeningCenterInForkCm.Z : -999.f, *ValueText(LastAutoInsertionTravelCm, TEXT("cm")), *ValueText(AutoRequiredForkAdvanceCm > -BIG_NUMBER*.5f && LastAutoInsertionTravelCm > -BIG_NUMBER*.5f ? FMath::Max(0.f, AutoRequiredForkAdvanceCm-LastAutoInsertionTravelCm) : -BIG_NUMBER, TEXT("cm"))));
	OutLines.Add(FString::Printf(TEXT("높이: 포크상단 local %s / 입구 하·상 %s..%s / 허용 포크상단 %s..%s / 목표 인양 %s / E/Q %.2f"), *ValueText(LastForkTopLocalCm,TEXT("cm")), *ValueText(LastMeasuredOpeningBottomCm,TEXT("cm")), *ValueText(LastMeasuredOpeningTopCm,TEXT("cm")), *ValueText(LastAllowedForkTopMinCm,TEXT("cm")), *ValueText(LastAllowedForkTopMaxCm,TEXT("cm")), *ValueText(LastHeightTargetLiftCm,TEXT("cm")), LastAppliedLiftInput));
	OutLines.Add(FString::Printf(TEXT("RGB-D: frame %d %s / mono %.1fcm / front Z %.1fcm ray %.1fcm / hole contrast %.1fcm yaw %.3frad"), DepthFrameId, OpeningDetection.bDepthValid ? TEXT("valid") : TEXT("invalid"), OpeningDetection.MonocularDistanceCm, OpeningDetection.FrontOpticalDepthCm, OpeningDetection.FrontRayDistanceCm, OpeningDetection.DepthOpeningContrastCm, OpeningDetection.FrontYawErrorRad));
	OutLines.Add(FString::Printf(TEXT("추적: 목표 1 / 재탐색 %d회 / 마지막 유효 %s 전 / 좌우 필터 %.1fcm / 영상 기울기 %.3frad(조향 미사용)"), LockedTargetMissFrames, *AgeText(LastTrackedOpeningTime), FilteredApproachLateralCm, AutoApproachHeadingErrorRad));
	OutLines.Add(FString::Printf(TEXT("조향: P 직진진단 %s / 이번 Tick %.2fdeg / 접근 누적 %.2fdeg"), bAutoApproachStraightDiagnostic ? TEXT("ON") : TEXT("OFF"), LastAutoYawDeltaDeg, AutoApproachAccumulatedYawDeg));
	OutLines.Add(FString::Printf(TEXT("삽입: R/F 명령 %.2f / 실제 리치 %s / 목표 %s / 차단 %s"), LastAppliedReachInput, *ValueText(LastAutoInsertionTravelCm, TEXT("cm")), *ValueText(AutoReachTargetCm, TEXT("cm")), bLastAutoDriveBlocked ? TEXT("예") : TEXT("아니오")));
	OutLines.Add(LastCollisionHitExpireTime > Now ? FString::Printf(TEXT("최근 실제 충돌: %s ↔ %s / 표시 %.1fs 남음"), *GetNameSafe(LastBlockingComponent.Get()), *GetNameSafe(LastCollisionHit.GetComponent()), FMath::Max(0.f, LastCollisionHitExpireTime-Now)) : TEXT("최근 실제 충돌: 없음 (화면 와이어는 이전 표시일 수 있음)"));
	OutLines.Add(FString::Printf(TEXT("인양: 명령 E/Q %.2f / 파레트 접촉 %s / 상승 %s"), LastAppliedLiftInput, bLastPhysicalPalletContact ? TEXT("확인") : TEXT("미확인"), *ValueText(LastPhysicalPalletRiseCm, TEXT("cm"))));
}

#if 0 // Replaced by colour-agnostic coarse-to-fine segmentation below.
void AAutomatedForklift::ProcessOpeningVision()
{
	if (!PalletSensorRenderTarget || !GetWorld()) return;
	if (!(AutoPickupState == EAutoPickupState::Detecting || AutoPickupState == EAutoPickupState::Approaching || AutoPickupState == EAutoPickupState::Aligning || AutoPickupState == EAutoPickupState::Inserting)) return;
	VisionProcessElapsed += GetWorld()->GetDeltaSeconds();
	if (VisionProcessElapsed < VisionProcessIntervalSeconds) return;
	VisionProcessElapsed = 0.f;
	++LastVisionFrameNumber; LastVisionProcessTime = GetWorld()->GetTimeSeconds(); LastVisionCandidateCount = -1; bLastVisionHadValidPair = false; OpeningDetection = FOpeningDetection(); VisionCandidates.Reset(); VisionPairDiagnostics.Reset(); LastVisionFailureReason.Reset();
	FTextureRenderTargetResource* Resource = PalletSensorRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != PalletSensorWidthPx * PalletSensorHeightPx) { OpeningDetection.bValid = false; bLastVisionReadSucceeded = false; LastPixelReadStatus = TEXT("실패"); return; }
	bLastVisionReadSucceeded = true; LastPixelReadStatus = TEXT("성공");
	const int32 W = PalletSensorWidthPx, H = PalletSensorHeightPx, Step = 2, SW = W / Step, SH = H / Step;
	// Stage 1: preserve colour and use a 16x16 red-coverage grid to find a
	// pallet candidate.  This does not assume a fixed screen position.
	const auto IsRedPalletPixel = [this](const FColor& C)
	{
		const float R = C.R / 255.f, G = C.G / 255.f, B = C.B / 255.f;
		const float High = FMath::Max3(R, G, B), Low = FMath::Min3(R, G, B), Delta = High - Low;
		if (High < VisionRedMinimumValue || Delta <= KINDA_SMALL_NUMBER) return false;
		float Hue = High == R ? 60.f * FMath::Fmod((G - B) / Delta, 6.f) : (High == G ? 60.f * ((B - R) / Delta + 2.f) : 60.f * ((R - G) / Delta + 4.f));
		if (Hue < 0.f) Hue += 360.f;
		return Delta / High >= VisionRedMinimumSaturation && (Hue <= VisionRedHueHalfRangeDeg || Hue >= 360.f - VisionRedHueHalfRangeDeg);
	};
	VisionCoarseColumns = FMath::Max(1, VisionCoarseGridColumns); VisionCoarseRows = FMath::Max(1, VisionCoarseGridRows);
	const auto FindRedGridBounds = [&](int32 Columns, int32 Rows, float MinFraction, FIntRect& OutBounds)
	{
		int32 MinCellX = Columns, MinCellY = Rows, MaxCellX = -1, MaxCellY = -1;
		for (int32 CellY = 0; CellY < Rows; ++CellY) for (int32 CellX = 0; CellX < Columns; ++CellX)
		{
			const int32 X0 = CellX * W / Columns, X1 = (CellX + 1) * W / Columns, Y0 = CellY * H / Rows, Y1 = (CellY + 1) * H / Rows;
			int32 RedPixels = 0, Samples = 0;
			for (int32 Y = Y0; Y < Y1; Y += 2) for (int32 X = X0; X < X1; X += 2) { ++Samples; RedPixels += IsRedPalletPixel(Pixels[Y * W + X]) ? 1 : 0; }
			if (Samples > 0 && static_cast<float>(RedPixels) / Samples >= MinFraction) { MinCellX = FMath::Min(MinCellX, CellX); MaxCellX = FMath::Max(MaxCellX, CellX); MinCellY = FMath::Min(MinCellY, CellY); MaxCellY = FMath::Max(MaxCellY, CellY); }
		}
		if (MaxCellX < MinCellX) return false;
		OutBounds = FIntRect(MinCellX * W / Columns, MinCellY * H / Rows, (MaxCellX + 1) * W / Columns, (MaxCellY + 1) * H / Rows); return true;
	};
	FIntRect RedCoarseBounds;
	bool bFoundRedCoarse = FindRedGridBounds(VisionCoarseColumns, VisionCoarseRows, VisionCoarseRedFraction, RedCoarseBounds);
	if (!bFoundRedCoarse) bFoundRedCoarse = FindRedGridBounds(32, 32, VisionCoarseRedFraction * .5f, RedCoarseBounds);
	if (!bFoundRedCoarse) { LastVisionFailureReason = TEXT("No red pallet candidate in coarse grid"); VisionPairDiagnostics.Add(LastVisionFailureReason); return; }
	RedCoarseBounds.Min.X = FMath::Max(0, RedCoarseBounds.Min.X - VisionFineSearchPaddingPx); RedCoarseBounds.Min.Y = FMath::Max(0, RedCoarseBounds.Min.Y - VisionFineSearchPaddingPx);
	RedCoarseBounds.Max.X = FMath::Min(W, RedCoarseBounds.Max.X + VisionFineSearchPaddingPx); RedCoarseBounds.Max.Y = FMath::Min(H, RedCoarseBounds.Max.Y + VisionFineSearchPaddingPx);
	VisionRedMask.SetNumZeroed(SW * SH); VisionBodyMask.SetNumZeroed(SW * SH);
	for (int32 Y = 0; Y < SH; ++Y) for (int32 X = 0; X < SW; ++X)
	{
		const int32 PX = X * Step, PY = Y * Step;
		if (PX >= RedCoarseBounds.Min.X && PX < RedCoarseBounds.Max.X && PY >= RedCoarseBounds.Min.Y && PY < RedCoarseBounds.Max.Y && IsRedPalletPixel(Pixels[PY * W + PX])) VisionRedMask[Y * SW + X] = VisionBodyMask[Y * SW + X] = 1;
	}
	VisionBodyBounds = FIntRect(RedCoarseBounds.Min.X / Step, RedCoarseBounds.Min.Y / Step, RedCoarseBounds.Max.X / Step, RedCoarseBounds.Max.Y / Step);
	// Stage 2: inspect original-image samples only inside/around the red body
	// candidate, rather than a screen-height band or a predicted opening ratio.
	const int32 SearchLeft = FMath::Clamp(VisionBodyBounds.Min.X, 0, SW - 1), SearchRight = FMath::Clamp(VisionBodyBounds.Max.X, SearchLeft + 1, SW);
	const int32 SearchTop = FMath::Clamp(VisionBodyBounds.Min.Y, 0, SH - 1), SearchBottom = FMath::Clamp(VisionBodyBounds.Max.Y, SearchTop + 1, SH);
	LastVisionSearchStartY = SearchTop;
	TArray<uint8> Dark; Dark.SetNumZeroed(SW * SH);
	for (int32 Y = SearchTop; Y < SearchBottom; ++Y) for (int32 X = SearchLeft; X < SearchRight; ++X)
	{
		const FColor C = Pixels[(Y * Step) * W + X * Step];
		const int32 Luma = (static_cast<int32>(C.R) * 54 + static_cast<int32>(C.G) * 183 + static_cast<int32>(C.B) * 19) >> 8;
		Dark[Y * SW + X] = !VisionRedMask[Y * SW + X] && Luma <= VisionOpeningMaximumLuma ? 1 : 0;
	}
	VisionMask = Dark; VisionMaskWidth = SW; VisionMaskHeight = SH;
	TArray<uint8> WorkMask = Dark;
	struct FCandidate { FVector2D C, E; int32 Area, RedBoundaryContacts; bool bTouchesBoundary; }; TArray<FCandidate> Candidates; TArray<int32> Queue;
	for (int32 Seed = 0; Seed < WorkMask.Num(); ++Seed) if (WorkMask[Seed])
	{
		Queue.Reset(); Queue.Add(Seed); WorkMask[Seed] = 0; int32 MinX = SW, MaxX = 0, MinY = SH, MaxY = 0;
		for (int32 I = 0; I < Queue.Num(); ++I) { const int32 P = Queue[I], X = P % SW, Y = P / SW; MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X); MinY = FMath::Min(MinY, Y); MaxY = FMath::Max(MaxY, Y);
			for (int32 DY = -1; DY <= 1; ++DY) for (int32 DX = -1; DX <= 1; ++DX) { const int32 NX = X + DX, NY = Y + DY; if (NX >= SearchLeft && NX < SearchRight && NY >= SearchTop && NY < SearchBottom && WorkMask[NY * SW + NX]) { WorkMask[NY * SW + NX] = 0; Queue.Add(NY * SW + NX); } }
		}
		const float BW = (MaxX - MinX + 1) * Step, BH = (MaxY - MinY + 1) * Step;
		int32 RedBoundaryContacts = 0;
		for (int32 X = MinX; X <= MaxX; ++X) { RedBoundaryContacts += VisionRedMask[FMath::Max(SearchTop, MinY - 1) * SW + X]; RedBoundaryContacts += VisionRedMask[FMath::Min(SearchBottom - 1, MaxY + 1) * SW + X]; }
		for (int32 Y = MinY; Y <= MaxY; ++Y) { RedBoundaryContacts += VisionRedMask[Y * SW + FMath::Max(SearchLeft, MinX - 1)]; RedBoundaryContacts += VisionRedMask[Y * SW + FMath::Min(SearchRight - 1, MaxX + 1)]; }
		const bool bTouchesBoundary = MinX <= SearchLeft || MaxX >= SearchRight - 1 || MinY <= SearchTop || MaxY >= SearchBottom - 1;
		if (Queue.Num() >= 12 && BW >= 12.f && BH >= 8.f && BW / BH >= .25f && BW / BH <= 6.f && RedBoundaryContacts >= 2) Candidates.Add({ FVector2D((MinX + MaxX + 1) * Step * .5f, (MinY + MaxY + 1) * Step * .5f), FVector2D(BW * .5f, BH * .5f), Queue.Num(), RedBoundaryContacts, bTouchesBoundary });
	}
	LastVisionCandidateCount = Candidates.Num(); OpeningDetection.bValid = false; float Best = 0.f; int32 BestA = INDEX_NONE, BestB = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index) VisionCandidates.Add({ Candidates[Index].C, Candidates[Index].E, Candidates[Index].Area, false, Candidates[Index].bTouchesBoundary, Candidates[Index].bTouchesBoundary ? TEXT("red-body search boundary") : FString::Printf(TEXT("red boundary contacts %d"), Candidates[Index].RedBoundaryContacts) });
	for (int32 A = 0; A < Candidates.Num(); ++A) for (int32 B = A + 1; B < Candidates.Num(); ++B)
	{
		int32 LeftIndex = A, RightIndex = B; FCandidate L = Candidates[A], R = Candidates[B]; if (L.C.X > R.C.X) { Swap(L, R); Swap(LeftIndex, RightIndex); }
		const float Separation = R.C.X - L.C.X, AverageWidth = (L.E.X + R.E.X), AverageHeight = (L.E.Y + R.E.Y);
		const float SizeDifference = FMath::Abs(L.E.X - R.E.X) / FMath::Max(1.f, FMath::Max(L.E.X, R.E.X));
		const float HeightDelta = FMath::Abs(L.C.Y - R.C.Y);
		// The known opening-centre spacing versus opening width is scale invariant.
		// It remains meaningful as the truck approaches, unlike a fixed image fraction.
		const float SpacingToWidth = Separation / FMath::Max(1.f, AverageWidth);
		const float MaxHeightDelta = FMath::Max(12.f, AverageHeight * .75f);
		const bool bSizeOK = SizeDifference <= .55f;
		const bool bLevelOK = HeightDelta <= MaxHeightDelta;
		// Spacing remains a plausibility check only; red-body bounded edges are
		// the primary evidence for an opening pair.
		const bool bSpacingOK = SpacingToWidth >= .35f && SpacingToWidth <= 4.50f;
		const bool bBoundaryOK = !Candidates[LeftIndex].bTouchesBoundary && !Candidates[RightIndex].bTouchesBoundary;
		const bool bRedStructureOK = Candidates[LeftIndex].RedBoundaryContacts >= 2 && Candidates[RightIndex].RedBoundaryContacts >= 2;
		const float SizeScore = 1.f - SizeDifference, LevelScore = 1.f - HeightDelta / FMath::Max(1.f, MaxHeightDelta);
		const float ExpectedSpacingToWidth = VisionKnownOpeningCenterSpacingCm / FMath::Max(1.f, VisionKnownOpeningWidthCm);
		const float SpacingScore = 1.f - FMath::Abs(SpacingToWidth - ExpectedSpacingToWidth) / ExpectedSpacingToWidth;
		const float StructureScore = FMath::Min(1.f, (Candidates[LeftIndex].RedBoundaryContacts + Candidates[RightIndex].RedBoundaryContacts) / 12.f);
		const float Score = FMath::Clamp((SizeScore + LevelScore + SpacingScore + StructureScore) * .25f, 0.f, 1.f);
		const bool bConfidenceOK = Score >= MinimumOpeningConfidence;
		const bool bPairOK = bSizeOK && bLevelOK && bSpacingOK && bBoundaryOK && bRedStructureOK && bConfidenceOK;
		const FString Reason = FString::Printf(TEXT("pair %d-%d: red %d/%d, size %.0f%%, level %.0fpx, spacing/width %.2f, score %.2f%s"), LeftIndex + 1, RightIndex + 1, Candidates[LeftIndex].RedBoundaryContacts, Candidates[RightIndex].RedBoundaryContacts, SizeDifference * 100.f, HeightDelta, SpacingToWidth, Score, bPairOK ? TEXT("") : TEXT(" rejected"));
		VisionPairDiagnostics.Add(Reason);
		if (bPairOK && Score > Best) { Best = Score; BestA = LeftIndex; BestB = RightIndex; OpeningDetection.LeftCenter = L.C; OpeningDetection.RightCenter = R.C; OpeningDetection.LeftExtent = L.E; OpeningDetection.RightExtent = R.E; }
	}
	if (Best >= MinimumOpeningConfidence)
	{
		const float Separation = OpeningDetection.RightCenter.X - OpeningDetection.LeftCenter.X;
		const float Focal = W / (2.f * FMath::Tan(FMath::DegreesToRadians(PalletSensorFieldOfViewDeg * .5f)));
		OpeningDetection.bValid = true; OpeningDetection.Confidence = Best; OpeningDetection.TimestampSeconds = GetWorld()->GetTimeSeconds();
		bLastVisionHadValidPair = true;
		OpeningDetection.EstimatedDistanceCm = VisionKnownOpeningCenterSpacingCm * Focal / FMath::Max(1.f, Separation);
		const float CenterX = (OpeningDetection.LeftCenter.X + OpeningDetection.RightCenter.X) * .5f;
		OpeningDetection.EstimatedLateralCm = PalletSensorLateralOffsetFromCenterCm + (CenterX - W * .5f) * OpeningDetection.EstimatedDistanceCm / Focal;
		if (VisionCandidates.IsValidIndex(BestA)) { VisionCandidates[BestA].bSelected = true; VisionCandidates[BestA].Reason = TEXT("최종 삽입구 쌍 선택"); }
		if (VisionCandidates.IsValidIndex(BestB)) { VisionCandidates[BestB].bSelected = true; VisionCandidates[BestB].Reason = TEXT("최종 삽입구 쌍 선택"); }
	}
	else
	{
		for (FVisionCandidateDebug& Candidate : VisionCandidates) Candidate.Reason = VisionPairDiagnostics.IsEmpty() ? TEXT("짝 후보 없음") : TEXT("쌍 조건 탈락 — 아래 쌍 진단 참조");
	}
}

#endif

void AAutomatedForklift::SaveVisionTransitionArtifacts(const TArray<FColor>& Pixels, bool bValidFrame)
{
	// PPM is deliberately used here: it needs no editor asset or Blueprint and
	// can be opened by common image viewers/converters.  These files are only
	// diagnostic evidence, never fed back into recognition.
	const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AutoPickupVision"));
	IFileManager::Get().MakeDirectory(*Root, true);
	const FString Prefix = bValidFrame ? TEXT("last_valid") : TEXT("first_failed");
	const auto SavePpm = [&Root, &Prefix](const FString& Suffix, int32 Width, int32 Height, const TArray<FColor>& Image)
	{
		if (Width <= 0 || Height <= 0 || Image.Num() != Width * Height) return;
		FTCHARToUTF8 Header(*FString::Printf(TEXT("P6\n%d %d\n255\n"), Width, Height));
		TArray<uint8> Bytes; Bytes.Append(reinterpret_cast<const uint8*>(Header.Get()), Header.Length()); Bytes.Reserve(Bytes.Num() + Image.Num() * 3);
		for (const FColor& Pixel : Image) { Bytes.Add(Pixel.R); Bytes.Add(Pixel.G); Bytes.Add(Pixel.B); }
		FFileHelper::SaveArrayToFile(Bytes, *FPaths::Combine(Root, Prefix + TEXT("_") + Suffix + TEXT(".ppm")));
	};
	SavePpm(TEXT("source"), PalletSensorWidthPx, PalletSensorHeightPx, Pixels);
	if (LatestDepthCm.Num() == PalletSensorWidthPx * PalletSensorHeightPx)
	{
		TArray<FColor> DepthImage; DepthImage.SetNumUninitialized(LatestDepthCm.Num());
		for (int32 Index=0; Index<LatestDepthCm.Num(); ++Index) { const float D=LatestDepthCm[Index]; const bool bValid=FMath::IsFinite(D)&&D>=DepthMinimumCm&&D<=DepthMaximumCm; const uint8 V=bValid?static_cast<uint8>(255.f*FMath::Clamp((D-DepthMinimumCm)/FMath::Max(1.f,DepthMaximumCm-DepthMinimumCm),0.f,1.f)):0; DepthImage[Index]=bValid?FColor(V,V,V):FColor(120,0,180); }
		SavePpm(TEXT("scene_depth"), PalletSensorWidthPx, PalletSensorHeightPx, DepthImage);
	}
	const int32 MW = VisionMaskWidth, MH = VisionMaskHeight;
	const auto SaveMask = [&SavePpm, MW, MH](const FString& Name, const TArray<uint8>& Mask, const FColor& On)
	{
		if (MW <= 0 || MH <= 0 || Mask.Num() != MW * MH) return;
		TArray<FColor> Image; Image.SetNumUninitialized(Mask.Num());
		for (int32 Index=0; Index<Mask.Num(); ++Index) Image[Index] = Mask[Index] ? On : FColor::Black;
		SavePpm(Name, MW, MH, Image);
	};
	SaveMask(TEXT("rgb_body"), VisionRgbBodyMask, FColor(255, 180, 60));
	SaveMask(TEXT("hsv_body"), VisionHsvBodyMask, FColor(80, 220, 255));
	SaveMask(TEXT("depth_refined_body"), VisionBodyMask, FColor(255, 90, 60));
	SaveMask(TEXT("quadtree_refined_body"), VisionQuadTreeBodyMask, FColor(180, 80, 255));
	SaveMask(TEXT("openings"), VisionMask, FColor::White);
	if (!bValidFrame && VisionCoarseColumns > 0 && VisionCoarseRows > 0 && VisionCoarseLabels.Num() == VisionCoarseColumns * VisionCoarseRows)
	{
		TArray<FColor> Coarse; Coarse.SetNumUninitialized(VisionCoarseLabels.Num());
		for (int32 Index=0; Index<Coarse.Num(); ++Index) { const int32 Label=VisionCoarseLabels[Index]; Coarse[Index] = Label < 0 ? FColor::Black : FLinearColor::MakeFromHSV8(static_cast<uint8>((Label*67)%255), 200, 255).ToFColor(false); }
		SavePpm(TEXT("coarse_regions"), VisionCoarseColumns, VisionCoarseRows, Coarse);
	}
	FString Metadata = FString::Printf(TEXT("frame=%d valid=%d state=%s\nreason=%s\n"), LastVisionFrameNumber, bValidFrame, *AutoPickupStateToString(AutoPickupState), *LastVisionFailureReason);
	for (const FString& Line : VisionPairDiagnostics) Metadata += Line + TEXT("\n");
	FFileHelper::SaveStringToFile(Metadata, *FPaths::Combine(Root, Prefix + TEXT("_diagnostics.txt")));
	UE_LOG(LogTemp, Log, TEXT("[R16HD_AUTO_VISION] saved %s source/masks/diagnostics to %s"), *Prefix, *Root);
}

bool AAutomatedForklift::ReadSynchronizedDepthFrame()
{
	bDepthFrameValid = false; DepthValidPixelCount = 0; DepthInvalidPixelCount = DepthBelowRangePixelCount = DepthAboveRangePixelCount = 0; DepthObservedMinimumCm = DepthObservedMaximumCm = -BIG_NUMBER;
	if (!PalletSensorDepthRenderTarget || !PalletSensorCapture || !PalletSensorDepthCapture || SensorFrameId <= 0) return false;
	const bool bMatchingCaptureIds = RgbCaptureId == SensorFrameId && DepthCaptureId == SensorFrameId;
	const float PositionDeltaCm = FVector::Dist(RgbFrameCameraTransform.GetLocation(), DepthFrameCameraTransform.GetLocation());
	const float RotationDeltaDeg = RgbFrameCameraTransform.GetRotation().AngularDistance(DepthFrameCameraTransform.GetRotation()) * (180.f / PI);
	const bool bSameCapturePose = PositionDeltaCm <= .01f && RotationDeltaDeg <= .01f;
	if (!bMatchingCaptureIds || !bSameCapturePose)
	{
		LastDepthReadStatus = FString::Printf(TEXT("depth sync rejected: rgb cycle %d depth cycle %d current %d, pose delta %.3fcm %.3fdeg"),
			RgbCaptureId, DepthCaptureId, SensorFrameId, PositionDeltaCm, RotationDeltaDeg);
		return false;
	}
	FTextureRenderTargetResource* Resource = PalletSensorDepthRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FFloat16Color> Raw;
	bool bRead = Resource && Resource->ReadFloat16Pixels(Raw);
	if (!bRead || Raw.Num() != PalletSensorWidthPx * PalletSensorHeightPx)
	{
		// Keep a separate fallback for drivers that expose the float target through
		// the linear-color readback path rather than float16 surface readback.
		TArray<FLinearColor> Linear;
		bRead = Resource && Resource->ReadLinearColorPixels(Linear, FReadSurfaceDataFlags(RCM_MinMax));
		if (!bRead || Linear.Num() != PalletSensorWidthPx * PalletSensorHeightPx)
		{
			LastDepthReadStatus = FString::Printf(TEXT("depth read failed: float16=%d count=%d, linear=%d count=%d expected=%d"), Raw.Num()>0, Raw.Num(), bRead, Linear.Num(), PalletSensorWidthPx*PalletSensorHeightPx);
			return false;
		}
		Raw.SetNumUninitialized(Linear.Num());
		for (int32 Index=0; Index<Linear.Num(); ++Index) Raw[Index] = FFloat16Color(Linear[Index]);
	}
	LatestDepthCm.SetNumUninitialized(Raw.Num());
	for (int32 Index=0; Index<Raw.Num(); ++Index)
	{
		// SCS_SceneDepth supplies SceneDepth in R.  It is optical-axis/view-Z in
		// Unreal cm; DeviceDepth is intentionally not used here.
		const float Depth = Raw[Index].R.GetFloat();
		LatestDepthCm[Index] = Depth;
		if (!FMath::IsFinite(Depth) || Depth < DepthSensorMinimumCm || Depth > DepthSensorMaximumCm) { ++DepthInvalidPixelCount; continue; }
		// Do not discard valid distant SceneDepth values.  These counters only
		// describe whether a value is outside the automatic pickup work range.
		if (Depth < DepthMinimumCm) ++DepthBelowRangePixelCount;
		if (Depth > DepthMaximumCm) ++DepthAboveRangePixelCount;
		++DepthValidPixelCount; DepthObservedMinimumCm = DepthObservedMinimumCm <= -BIG_NUMBER*.5f ? Depth : FMath::Min(DepthObservedMinimumCm,Depth); DepthObservedMaximumCm = DepthObservedMaximumCm <= -BIG_NUMBER*.5f ? Depth : FMath::Max(DepthObservedMaximumCm,Depth);
	}
	DepthFrameId = DepthCaptureId;
	LastDepthReadTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -BIG_NUMBER;
	bDepthFrameValid = DepthValidPixelCount > Raw.Num() / 100;
	LastDepthReadStatus = FString::Printf(TEXT("depth read %s: %d/%d sensor-valid, invalid=%d work-below=%d work-above=%d raw %.1f..%.1f cm"), bDepthFrameValid ? TEXT("succeeded") : TEXT("no valid values"), DepthValidPixelCount, Raw.Num(), DepthInvalidPixelCount, DepthBelowRangePixelCount, DepthAboveRangePixelCount, DepthObservedMinimumCm, DepthObservedMaximumCm);
	return bDepthFrameValid;
}

void AAutomatedForklift::UpdateDepthForOpeningDetection()
{
	if (!OpeningDetection.bValid || !bDepthFrameValid || DepthFrameId != SensorFrameId || LatestDepthCm.Num() != PalletSensorWidthPx*PalletSensorHeightPx) return;
	const int32 W=PalletSensorWidthPx,H=PalletSensorHeightPx;
	const auto DepthAt=[this,W,H](int32 X,int32 Y)->float { X=FMath::Clamp(X,0,W-1);Y=FMath::Clamp(Y,0,H-1);return LatestDepthCm[Y*W+X]; };
	TArray<float> FrontSamples, HoleSamples;
	const auto Collect=[&](const FVector2D& C,const FVector2D& E)
	{
		for(int32 S=-3;S<=3;++S)
		{
			// Outside left/right and above/below opening: likely front rails/blocks,
			// not the rear scene seen through the opening.
			FrontSamples.Add(DepthAt(FMath::RoundToInt(C.X-E.X-5),FMath::RoundToInt(C.Y+S*E.Y/3)));
			FrontSamples.Add(DepthAt(FMath::RoundToInt(C.X+E.X+5),FMath::RoundToInt(C.Y+S*E.Y/3)));
			FrontSamples.Add(DepthAt(FMath::RoundToInt(C.X+S*E.X/3),FMath::RoundToInt(C.Y-E.Y-5)));
			HoleSamples.Add(DepthAt(FMath::RoundToInt(C.X),FMath::RoundToInt(C.Y+S*E.Y/4)));
		}
	};
	Collect(OpeningDetection.LeftCenter,OpeningDetection.LeftExtent); Collect(OpeningDetection.RightCenter,OpeningDetection.RightExtent);
	FrontSamples.RemoveAll([this](float D){return !FMath::IsFinite(D)||D<DepthMinimumCm||D>DepthMaximumCm;});
	HoleSamples.RemoveAll([this](float D){return !FMath::IsFinite(D)||D<DepthMinimumCm||D>DepthMaximumCm;});
	if(FrontSamples.Num()<8) return;
	FrontSamples.Sort(); HoleSamples.Sort();
	const float Optical=FrontSamples[FrontSamples.Num()/2];
	const float Hole=HoleSamples.Num()?HoleSamples[HoleSamples.Num()/2]:Optical;
	const float Focal=W/(2.f*FMath::Tan(FMath::DegreesToRadians(PalletSensorFieldOfViewDeg*.5f)));
	const FVector2D Centre=(OpeningDetection.LeftCenter+OpeningDetection.RightCenter)*.5f;
	const float NX=(Centre.X-W*.5f)/Focal, NY=(Centre.Y-H*.5f)/Focal;
	// UE camera local axes: +X forward, +Y right, +Z up. Image Y grows down.
	const FVector CameraPoint(Optical, NX*Optical, -NY*Optical);
	const FVector WorldPoint=DepthFrameCameraTransform.TransformPosition(CameraPoint);
	const FVector LocalToFork=ForkCarriage ? ForkCarriage->GetComponentTransform().InverseTransformPosition(WorldPoint) : CameraPoint;
	OpeningDetection.bDepthValid=true; OpeningDetection.FrontOpticalDepthCm=Optical;
	OpeningDetection.FrontRayDistanceCm=CameraPoint.Size();
	OpeningDetection.DepthOpeningContrastCm=Hole-Optical;
	OpeningDetection.EstimatedDistanceCm=Optical; // control uses front plane Z, not hole/rear depth
	OpeningDetection.EstimatedLateralCm=LocalToFork.Y;
	OpeningDetection.EstimatedHeightCm=LocalToFork.Z;
	OpeningDetection.OpeningCenterInForkCm=LocalToFork;
	const float AverageHalfHeightPx=(OpeningDetection.LeftExtent.Y+OpeningDetection.RightExtent.Y)*.5f;
	const float AverageHalfWidthPx=(OpeningDetection.LeftExtent.X+OpeningDetection.RightExtent.X)*.5f;
	OpeningDetection.MeasuredOpeningHeightCm=FMath::Max(0.f,2.f*AverageHalfHeightPx*Optical/Focal);
	OpeningDetection.MeasuredOpeningWidthCm=FMath::Max(0.f,2.f*AverageHalfWidthPx*Optical/Focal);
	const auto ReconstructOpening = [&](const FVector2D& Pixel, const FVector2D& Extent, FVector& OutCenter, float& OutHeight, float& OutWidth)
	{
		const float PX = (Pixel.X-W*.5f)/Focal, PY = (Pixel.Y-H*.5f)/Focal;
		const FVector Point(Optical, PX*Optical, -PY*Optical);
		OutCenter = ForkCarriage ? ForkCarriage->GetComponentTransform().InverseTransformPosition(DepthFrameCameraTransform.TransformPosition(Point)) : Point;
		OutHeight = FMath::Max(0.f, 2.f*Extent.Y*Optical/Focal);
		OutWidth = FMath::Max(0.f, 2.f*Extent.X*Optical/Focal);
	};
	ReconstructOpening(OpeningDetection.LeftCenter, OpeningDetection.LeftExtent, OpeningDetection.LeftOpeningCenterInForkCm, OpeningDetection.LeftMeasuredOpeningHeightCm, OpeningDetection.LeftMeasuredOpeningWidthCm);
	ReconstructOpening(OpeningDetection.RightCenter, OpeningDetection.RightExtent, OpeningDetection.RightOpeningCenterInForkCm, OpeningDetection.RightMeasuredOpeningHeightCm, OpeningDetection.RightMeasuredOpeningWidthCm);
	// A real plane fit needs more non-collinear front samples; depth variation
	// across symmetric opening borders is retained as a confidence diagnostic.
	const float LeftDepth=DepthAt(FMath::RoundToInt(OpeningDetection.LeftCenter.X-OpeningDetection.LeftExtent.X-5),FMath::RoundToInt(OpeningDetection.LeftCenter.Y));
	const float RightDepth=DepthAt(FMath::RoundToInt(OpeningDetection.RightCenter.X+OpeningDetection.RightExtent.X+5),FMath::RoundToInt(OpeningDetection.RightCenter.Y));
	OpeningDetection.FrontYawErrorRad=FMath::Atan2(RightDepth-LeftDepth,FMath::Max(1.f,(OpeningDetection.RightCenter.X-OpeningDetection.LeftCenter.X)*Optical/Focal));
}

void AAutomatedForklift::ProcessOpeningVision()
{
	if (!PalletSensorRenderTarget || !GetWorld()) return;
	const bool bAutoNeedsAnalysis = AutoPickupState == EAutoPickupState::Detecting || AutoPickupState == EAutoPickupState::Approaching || AutoPickupState == EAutoPickupState::Aligning || AutoPickupState == EAutoPickupState::Inserting;
	const bool bPreviewNeedsAnalysis = PalletSensorDisplayMode != 0;
	if (!bAutoNeedsAnalysis && !bPreviewNeedsAnalysis) return;
	VisionProcessElapsed += GetWorld()->GetDeltaSeconds(); if (VisionProcessElapsed < VisionProcessIntervalSeconds) return; VisionProcessElapsed = 0.f;
	if (SensorFrameId <= 0 || SensorFrameId == LastProcessedSensorFrameId) { LastPixelReadStatus = SensorFrameId <= 0 ? TEXT("capture frame not available") : TEXT("waiting for next capture frame"); return; }
	LastProcessedSensorFrameId = SensorFrameId;
	const double VisionStartSeconds = FPlatformTime::Seconds();
	++LastVisionFrameNumber; LastVisionProcessTime = GetWorld()->GetTimeSeconds(); LastVisionCandidateCount = -1; bLastVisionHadValidPair = false; OpeningDetection = FOpeningDetection(); VisionCandidates.Reset(); VisionPairDiagnostics.Reset(); VisionSubdivisionCells.Reset(); VisionSubdivisionDepths.Reset(); LastVisionFailureReason.Reset(); LastVisionStageStatus = TEXT("RGB coarse/merge pending");
	FTextureRenderTargetResource* Resource = PalletSensorRenderTarget->GameThread_GetRenderTargetResource(); TArray<FColor> Pixels;
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != PalletSensorWidthPx * PalletSensorHeightPx) { bLastVisionReadSucceeded = false; LastPixelReadStatus = TEXT("read failed"); LastVisionFailureReason = TEXT("Camera render target pixels unavailable"); return; }
	LastVisionRgbReadMs = static_cast<float>((FPlatformTime::Seconds()-VisionStartSeconds)*1000.0);
	// Preserve exactly what the detector consumes.  It is never converted back
	// into the render target; O mode 5 only visualizes this CPU copy for a direct
	// RT -> ReadPixels -> HUD comparison.
	LatestRgbCpuPixels = Pixels;
	uint8 MinR = 255, MinG = 255, MinB = 255, MaxR = 0, MaxG = 0, MaxB = 0;
	uint64 SumR = 0, SumG = 0, SumB = 0;
	int32 WhiteSaturated = 0;
	for (const FColor& Pixel : Pixels)
	{
		MinR = FMath::Min(MinR, Pixel.R); MinG = FMath::Min(MinG, Pixel.G); MinB = FMath::Min(MinB, Pixel.B);
		MaxR = FMath::Max(MaxR, Pixel.R); MaxG = FMath::Max(MaxG, Pixel.G); MaxB = FMath::Max(MaxB, Pixel.B);
		SumR += Pixel.R; SumG += Pixel.G; SumB += Pixel.B;
		if (Pixel.R >= 250 && Pixel.G >= 250 && Pixel.B >= 250) ++WhiteSaturated;
	}
	const float PixelCount = static_cast<float>(FMath::Max(1, Pixels.Num()));
	LastRgbReadStatistics = FString::Printf(TEXT("RGB CPU frame %d: R %d..%d/%.0f G %d..%d/%.0f B %d..%d/%.0f white %.1f%%"),
		SensorFrameId, MinR, MaxR, SumR / PixelCount, MinG, MaxG, SumG / PixelCount, MinB, MaxB, SumB / PixelCount,
		100.f * static_cast<float>(WhiteSaturated) / PixelCount);
	bLastVisionReadSucceeded = true; LastPixelReadStatus = TEXT("RGB read succeeded");
	// RGB diagnostics deliberately continue without depth.  Automatic motion
	// later requires bDepthValid, but coarse regions/candidates remain visible
	// so a depth transport failure cannot hide the RGB diagnosis.
	const double DepthReadStartSeconds = FPlatformTime::Seconds();
	const bool bDepthAvailable = ReadSynchronizedDepthFrame();
	LastVisionDepthReadMs = static_cast<float>((FPlatformTime::Seconds()-DepthReadStartSeconds)*1000.0);
	const double CoarseStartSeconds = FPlatformTime::Seconds();
	const int32 W=PalletSensorWidthPx,H=PalletSensorHeightPx,Step=2,SW=W/Step,SH=H/Step;
	// HSV chroma is intentionally independent of value: a cast shadow changes V
	// far more than H/S.  Low-saturation pixels carry no unstable hue signal and
	// are resolved by luminance, RGB edges and depth later in the pipeline.
	const auto Feature=[](const FColor& C)
	{
		const float R=C.R/255.f,G=C.G/255.f,B=C.B/255.f, Hi=FMath::Max3(R,G,B), Lo=FMath::Min3(R,G,B), Delta=Hi-Lo;
		const float Sat=Hi>SMALL_NUMBER ? Delta/Hi : 0.f;
		float Hue=0.f;
		if (Sat>.08f) { if (Hi==R) Hue=FMath::Fmod((G-B)/FMath::Max(Delta,SMALL_NUMBER),6.f); else if (Hi==G) Hue=(B-R)/FMath::Max(Delta,SMALL_NUMBER)+2.f; else Hue=(R-G)/FMath::Max(Delta,SMALL_NUMBER)+4.f; Hue*=PI/3.f; }
		return Sat>.08f ? FVector(FMath::Cos(Hue)*Sat,FMath::Sin(Hue)*Sat,Sat) : FVector::ZeroVector;
	};
	const auto Bright=[](const FColor& C){return (C.R*.2126f+C.G*.7152f+C.B*.0722f)/255.f;};
	const auto Similar=[this](const FVector&A,float AL,const FVector&B,float BL,float CD,float LD){return FVector::Dist(A,B)<=CD&&FMath::Abs(AL-BL)<=LD;};
	// 1) 16x16 colour distribution analysis. Each cell retains a representative
	// colour and variation; no material hue is converted into a binary class.
	const int32 CW=FMath::Max(1,VisionCoarseGridColumns),CH=FMath::Max(1,VisionCoarseGridRows); VisionCoarseColumns=CW;VisionCoarseRows=CH;
	struct FCell{FVector C=FVector::ZeroVector;float L=0.f,V=0.f;int32 Label=-1;}; TArray<FCell> Cells;Cells.SetNum(CW*CH);VisionCoarseLabels.SetNumZeroed(CW*CH);
	for(int32 CY=0;CY<CH;++CY)for(int32 CX=0;CX<CW;++CX){FCell&Cell=Cells[CY*CW+CX];int32 N=0;const int32 X0=CX*W/CW,X1=(CX+1)*W/CW,Y0=CY*H/CH,Y1=(CY+1)*H/CH;for(int32 Y=Y0;Y<Y1;Y+=3)for(int32 X=X0;X<X1;X+=3){Cell.C+=Feature(Pixels[Y*W+X]);Cell.L+=Bright(Pixels[Y*W+X]);++N;}Cell.C/=FMath::Max(1,N);Cell.L/=FMath::Max(1,N);for(int32 Y=Y0;Y<Y1;Y+=4)for(int32 X=X0;X<X1;X+=4)Cell.V+=FVector::Dist(Feature(Pixels[Y*W+X]),Cell.C)+FMath::Abs(Bright(Pixels[Y*W+X])-Cell.L);Cell.V/=FMath::Max(1,N/2);}
	struct FRegion{int32 Label=-1,Count=0;FIntRect B;FVector C=FVector::ZeroVector;float L=0.f,T=0.f;};TArray<FRegion> Regions;TArray<int32> Queue;
	for(int32 Seed=0;Seed<Cells.Num();++Seed)if(Cells[Seed].Label<0){FRegion R;R.Label=Regions.Num();R.B=FIntRect(CW,CH,0,0);Queue.Reset();Queue.Add(Seed);Cells[Seed].Label=R.Label;for(int32 I=0;I<Queue.Num();++I){const int32 P=Queue[I],X=P%CW,Y=P/CW;const FCell&Cur=Cells[P];++R.Count;R.C+=Cur.C;R.L+=Cur.L;R.T+=Cur.V;R.B.Min.X=FMath::Min(R.B.Min.X,X);R.B.Min.Y=FMath::Min(R.B.Min.Y,Y);R.B.Max.X=FMath::Max(R.B.Max.X,X+1);R.B.Max.Y=FMath::Max(R.B.Max.Y,Y+1);for(const FIntPoint D:{FIntPoint(-1,0),FIntPoint(1,0),FIntPoint(0,-1),FIntPoint(0,1)}){const int32 NX=X+D.X,NY=Y+D.Y;if(NX<0||NX>=CW||NY<0||NY>=CH)continue;FCell&Next=Cells[NY*CW+NX];if(Next.Label<0&&Similar(Cur.C,Cur.L,Next.C,Next.L,VisionCoarseColorDistance,VisionCoarseBrightnessDistance)){Next.Label=R.Label;Queue.Add(NY*CW+NX);}}}R.C/=FMath::Max(1,R.Count);R.L/=FMath::Max(1,R.Count);R.T/=FMath::Max(1,R.Count);Regions.Add(R);}for(int32 I=0;I<Cells.Num();++I)VisionCoarseLabels[I]=Cells[I].Label;
	// Selective-Search style bottom-up proposals: retain every merge level,
	// scoring colour, luminance, texture, size and spatial adjacency.
	TArray<FRegion> CandidateRegions=Regions; TSet<uint64> UsedMergePairs;
	for(int32 Pass=0;Pass<32;++Pass){int32 BA=INDEX_NONE,BB=INDEX_NONE;uint64 BestKey=0;float BestMerge=0.f;for(int32 A=0;A<Regions.Num();++A)for(int32 B=A+1;B<Regions.Num();++B){const uint64 Key=(static_cast<uint64>(A)<<32)|static_cast<uint32>(B);if(UsedMergePairs.Contains(Key))continue;const FRegion&AReg=Regions[A],&BReg=Regions[B];const bool Adjacent=!(AReg.B.Max.X<BReg.B.Min.X-1||BReg.B.Max.X<AReg.B.Min.X-1||AReg.B.Max.Y<BReg.B.Min.Y-1||BReg.B.Max.Y<AReg.B.Min.Y-1);if(!Adjacent)continue;const float Colour=1.f-FMath::Clamp(FVector::Dist(AReg.C,BReg.C)/VisionCoarseColorDistance,0.f,1.f),Light=1.f-FMath::Clamp(FMath::Abs(AReg.L-BReg.L)/VisionCoarseBrightnessDistance,0.f,1.f),Texture=1.f-FMath::Clamp(FMath::Abs(AReg.T-BReg.T)/.20f,0.f,1.f),Size=FMath::Min(AReg.Count,BReg.Count)/static_cast<float>(FMath::Max(AReg.Count,BReg.Count));const float Score=(Colour+Light+Texture+Size)*.25f;if(Score>BestMerge){BestMerge=Score;BA=A;BB=B;BestKey=Key;}}if(BA==INDEX_NONE||BestMerge<.58f)break;UsedMergePairs.Add(BestKey);const FRegion&AReg=Regions[BA],&BReg=Regions[BB];FRegion M;M.Label=CandidateRegions.Num();M.Count=AReg.Count+BReg.Count;M.C=(AReg.C*AReg.Count+BReg.C*BReg.Count)/M.Count;M.L=(AReg.L*AReg.Count+BReg.L*BReg.Count)/M.Count;M.T=(AReg.T*AReg.Count+BReg.T*BReg.Count)/M.Count;M.B=FIntRect(FMath::Min(AReg.B.Min.X,BReg.B.Min.X),FMath::Min(AReg.B.Min.Y,BReg.B.Min.Y),FMath::Max(AReg.B.Max.X,BReg.B.Max.X),FMath::Max(AReg.B.Max.Y,BReg.B.Max.Y));Regions.Add(M);CandidateRegions.Add(M);VisionPairDiagnostics.Add(FString::Printf(TEXT("merge proposal %d: score %.2f, %d cells"),M.Label,BestMerge,M.Count));}
	LastVisionCoarseMs = static_cast<float>((FPlatformTime::Seconds()-CoarseStartSeconds)*1000.0);
	// Once a pair has been locked, preserve a *search* region around the last
	// observed pair.  This uses only previous image measurements; it is not a
	// synthetic detection and all opening boundaries below are still re-read
	// from the current source pixels.  The ROI expands after misses so a close
	// pallet cannot be rejected merely because a coarse cell clipped its edge.
	if ((AutoPickupState == EAutoPickupState::Approaching || AutoPickupState == EAutoPickupState::Aligning) && LastTrackedOpeningDetection.bValid)
	{
		const FVector2D Centre = (LastTrackedOpeningDetection.LeftCenter + LastTrackedOpeningDetection.RightCenter) * .5f;
		const FVector2D Extent = LastTrackedOpeningDetection.LeftExtent + LastTrackedOpeningDetection.RightExtent;
		const float Expand = 2.5f + FMath::Min(2.f, LockedTargetMissFrames * .5f);
		const int32 MinPX = FMath::Clamp(FMath::FloorToInt(Centre.X - Extent.X * Expand - VisionFineSearchPaddingPx * 2), 0, W - 1);
		const int32 MaxPX = FMath::Clamp(FMath::CeilToInt(Centre.X + Extent.X * Expand + VisionFineSearchPaddingPx * 2), MinPX + 1, W);
		const int32 MinPY = FMath::Clamp(FMath::FloorToInt(Centre.Y - Extent.Y * Expand - VisionFineSearchPaddingPx * 2), 0, H - 1);
		const int32 MaxPY = FMath::Clamp(FMath::CeilToInt(Centre.Y + Extent.Y * Expand + VisionFineSearchPaddingPx * 2), MinPY + 1, H);
		FRegion Tracking; Tracking.Label = -100 - LockedTargetMissFrames; Tracking.B = FIntRect(MinPX * CW / W, MinPY * CH / H, FMath::Max(MinPX * CW / W + 1, MaxPX * CW / W), FMath::Max(MinPY * CH / H + 1, MaxPY * CH / H));
		// Do not average the opening interiors, floor or rear background into the
		// tracked body's representative colour.  Sample only just outside the
		// previously measured opening edges in this *current* RGB frame.
		for (const FVector2D& OpeningCenter : {LastTrackedOpeningDetection.LeftCenter, LastTrackedOpeningDetection.RightCenter})
		{
			const FVector2D OpeningExtent = OpeningCenter == LastTrackedOpeningDetection.LeftCenter ? LastTrackedOpeningDetection.LeftExtent : LastTrackedOpeningDetection.RightExtent;
			for (int32 Row=-2; Row<=2; ++Row) for (int32 Side : {-1, 1})
			{
				const int32 PX=FMath::Clamp(FMath::RoundToInt(OpeningCenter.X+Side*(OpeningExtent.X+6.f)),MinPX,MaxPX-1), PY=FMath::Clamp(FMath::RoundToInt(OpeningCenter.Y+Row*OpeningExtent.Y*.35f),MinPY,MaxPY-1);
				Tracking.C += Feature(Pixels[PY*W+PX]); Tracking.L += Bright(Pixels[PY*W+PX]); ++Tracking.Count;
			}
		}
		Tracking.C /= FMath::Max(1, Tracking.Count); Tracking.L /= FMath::Max(1, Tracking.Count); CandidateRegions.Add(Tracking);
		VisionPairDiagnostics.Add(FString::Printf(TEXT("tracked ROI: %dx%d px, miss %d"), MaxPX-MinPX, MaxPY-MinPY, LockedTargetMissFrames));
	}
	// A full-resolution body/opening pass is expensive.  During tracking, execute
	// the measured ROI first and defer the global proposals to a bounded fallback
	// set.  This prevents dozens of full-size temporary masks from stalling motion.
	TArray<FRegion> FineCandidates;
	for (const FRegion& Candidate : CandidateRegions) if (Candidate.Label < 0 && Candidate.Count >= 2) FineCandidates.Add(Candidate);
	const bool bHaveTrackedRoi = FineCandidates.Num() > 0;
	const bool bDoGlobalFallback = !bHaveTrackedRoi || LockedTargetMissFrames > 0 || (SensorFrameId % 5) == 0;
	for (const FRegion& Candidate : CandidateRegions)
	{
		if (!bDoGlobalFallback || Candidate.Label < 0 || Candidate.Count < 2 || FineCandidates.Num() >= VisionMaximumFineRegions) continue;
		FineCandidates.Add(Candidate);
	}
	LastVisionCandidateCount = FineCandidates.Num();
	const double FineStartSeconds = FPlatformTime::Seconds();
	float Best=0.f;int32 TotalOpeningComponents=0, UsableOpeningComponents=0, TestedOpeningPairs=0;FVector2D BestLC=FVector2D::ZeroVector,BestRC=FVector2D::ZeroVector,BestLE=FVector2D::ZeroVector,BestRE=FVector2D::ZeroVector;TArray<uint8> BestRegion,BestBody,BestOpen;FIntRect BestBounds;
	for(const FRegion&R:FineCandidates){if(R.Count<2)continue;FIntRect B(R.B.Min.X*W/CW,R.B.Min.Y*H/CH,R.B.Max.X*W/CW,R.B.Max.Y*H/CH);B.Min.X=FMath::Max(0,B.Min.X-VisionFineSearchPaddingPx);B.Min.Y=FMath::Max(0,B.Min.Y-VisionFineSearchPaddingPx);B.Max.X=FMath::Min(W,B.Max.X+VisionFineSearchPaddingPx);B.Max.Y=FMath::Min(H,B.Max.Y+VisionFineSearchPaddingPx);const int32 LX=B.Min.X/Step,RX=FMath::Min(SW,B.Max.X/Step),TY=B.Min.Y/Step,BY=FMath::Min(SH,B.Max.Y/Step);TArray<uint8> Reg;Reg.SetNumZeroed(SW*SH);for(int32 Y=TY;Y<BY;++Y)for(int32 X=LX;X<RX;++X){const FColor&C=Pixels[(Y*Step)*W+X*Step];if(Similar(Feature(C),Bright(C),R.C,R.L,VisionFineColorDistance,VisionFineBrightnessDistance))Reg[Y*SW+X]=1;}VisionRgbBodyMask=Reg;VisionHsvBodyMask=Reg;VisionMaskWidth=SW;VisionMaskHeight=SH;
		// RGB proposes the body.  Depth then removes only pixels that are clearly
		// behind/in front of that surface, preventing similarly coloured floor or
		// rear scene from being merged into the pallet body.
		TArray<float> BodyDepths; if (bDepthAvailable) for(int32 Y=TY;Y<BY;++Y)for(int32 X=LX;X<RX;++X)if(Reg[Y*SW+X]){const float D=LatestDepthCm[(Y*Step)*W+X*Step];if(FMath::IsFinite(D)&&D>=DepthMinimumCm&&D<=DepthMaximumCm)BodyDepths.Add(D);} float BodyDepth=-1.f; if(BodyDepths.Num()>=12){BodyDepths.Sort();BodyDepth=BodyDepths[BodyDepths.Num()/2];int32 SurfaceSupport=0;for(const float D:BodyDepths)if(FMath::Abs(D-BodyDepth)<=VisionDepthBodyContinuityCm)++SurfaceSupport;const bool bRepresentativeSurface=SurfaceSupport*100>=BodyDepths.Num()*55;if(bRepresentativeSurface)for(int32 Y=TY;Y<BY;++Y)for(int32 X=LX;X<RX;++X)if(Reg[Y*SW+X]&&FMath::Abs(LatestDepthCm[(Y*Step)*W+X*Step]-BodyDepth)>VisionDepthBodyContinuityCm)Reg[Y*SW+X]=0;else { BodyDepth=-1.f; if(VisionPairDiagnostics.Num()<48)VisionPairDiagnostics.Add(FString::Printf(TEXT("region %d depth skipped: mixed surface support %d/%d"),R.Label,SurfaceSupport,BodyDepths.Num())); }}
		// 2) Actual 2x2 refinement on original pixels, bounded by depth and budget.
		const int32 SubdivisionStart=VisionSubdivisionCells.Num();TFunction<void(FIntRect,int32)> Split=[&](FIntRect Cell,int32 Depth){if(VisionSubdivisionCells.Num()>=VisionMaximumFineCells)return;VisionSubdivisionCells.Add(Cell);VisionSubdivisionDepths.Add(Depth);if(Depth>=VisionMaximumSubdivisionDepth||Cell.Width()<=VisionMinimumSubdivisionCellPx||Cell.Height()<=VisionMinimumSubdivisionCellPx)return;FVector Mean=FVector::ZeroVector;float MeanL=0.f;int32 N=0;for(int32 Y=Cell.Min.Y;Y<Cell.Max.Y;Y+=4)for(int32 X=Cell.Min.X;X<Cell.Max.X;X+=4){Mean+=Feature(Pixels[Y*W+X]);MeanL+=Bright(Pixels[Y*W+X]);++N;}Mean/=FMath::Max(1,N);MeanL/=FMath::Max(1,N);float Var=0.f;for(int32 Y=Cell.Min.Y;Y<Cell.Max.Y;Y+=5)for(int32 X=Cell.Min.X;X<Cell.Max.X;X+=5)Var+=FVector::Dist(Feature(Pixels[Y*W+X]),Mean)+FMath::Abs(Bright(Pixels[Y*W+X])-MeanL);if(Var/FMath::Max(1,N/2)<.10f)return;const int32 MX=(Cell.Min.X+Cell.Max.X)/2,MY=(Cell.Min.Y+Cell.Max.Y)/2;Split(FIntRect(Cell.Min.X,Cell.Min.Y,MX,MY),Depth+1);Split(FIntRect(MX,Cell.Min.Y,Cell.Max.X,MY),Depth+1);Split(FIntRect(Cell.Min.X,MY,MX,Cell.Max.Y),Depth+1);Split(FIntRect(MX,MY,Cell.Max.X,Cell.Max.Y),Depth+1);};Split(B,0);
		// The quadtree is not merely debug output: every subdivided cell is
		// re-evaluated from the original render-target pixels and replaces the
		// coarse body decision at the corresponding Step=2 samples.  Mixed edge
		// cells therefore change the downstream opening components/pairs.
		for(int32 CellIndex=SubdivisionStart;CellIndex<VisionSubdivisionCells.Num();++CellIndex){if(VisionSubdivisionDepths[CellIndex]<=0)continue;const FIntRect&Cell=VisionSubdivisionCells[CellIndex];const int32 CX0=FMath::Clamp(Cell.Min.X/Step,LX,RX),CX1=FMath::Clamp((Cell.Max.X+Step-1)/Step,LX,RX),CY0=FMath::Clamp(Cell.Min.Y/Step,TY,BY),CY1=FMath::Clamp((Cell.Max.Y+Step-1)/Step,TY,BY);for(int32 Y=CY0;Y<CY1;++Y)for(int32 X=CX0;X<CX1;++X){int32 BodyVotes=0,Samples=0;for(int32 PY=Y*Step;PY<FMath::Min(H,Y*Step+Step);++PY)for(int32 PX=X*Step;PX<FMath::Min(W,X*Step+Step);++PX){const FColor&PC=Pixels[PY*W+PX];const float PD=bDepthAvailable?LatestDepthCm[PY*W+PX]:BodyDepth;const bool bColour=Similar(Feature(PC),Bright(PC),R.C,R.L,VisionFineColorDistance,VisionFineBrightnessDistance);const bool bDepthOK=BodyDepth<=0.f||!FMath::IsFinite(PD)||FMath::Abs(PD-BodyDepth)<=VisionDepthBodyContinuityCm;if(bColour&&bDepthOK)++BodyVotes;++Samples;}Reg[Y*SW+X]=(BodyVotes*2>=Samples)?1:0;}}VisionQuadTreeBodyMask=Reg;
		TArray<uint8> Open;Open.SetNumZeroed(SW*SH);for(int32 Y=TY;Y<BY;++Y)for(int32 X=LX;X<RX;++X)if(!Reg[Y*SW+X]){const FColor&C=Pixels[(Y*Step)*W+X*Step];const bool bRgbContrast=FVector::Dist(Feature(C),R.C)+FMath::Abs(Bright(C)-R.L)>=VisionOpeningMinimumContrast;const float D=bDepthAvailable?LatestDepthCm[(Y*Step)*W+X*Step]:-1.f;const bool bDepthContradiction=BodyDepth>0.f&&FMath::IsFinite(D)&&D<BodyDepth-VisionDepthOpeningContrastCm*.5f;/* Depth supports a recess, but cannot erase a real RGB opening merely because it sees a crossrail at the front. */Open[Y*SW+X]=(bRgbContrast&&!bDepthContradiction)?1:0;}
		// Retain the current candidate stages even when pair validation later
		// fails; this makes the first failure artifact inspectable rather than
		// leaving only a stale last-success mask on the HUD/disk.
		VisionRegionMask=Reg; VisionBodyMask=Reg; VisionMask=Open; VisionMaskWidth=SW; VisionMaskHeight=SH; VisionBodyBounds=FIntRect(LX,TY,RX,BY);
		struct FC{FVector2D C,E;int32 A,Contact;bool Edge;};TArray<FC> Cs;TArray<uint8> Work=Open;
		for(int32 Seed=0;Seed<Work.Num();++Seed)if(Work[Seed]){Queue.Reset();Queue.Add(Seed);Work[Seed]=0;int32 MinX=SW,MaxX=0,MinY=SH,MaxY=0;for(int32 I=0;I<Queue.Num();++I){const int32 P=Queue[I],X=P%SW,Y=P/SW;MinX=FMath::Min(MinX,X);MaxX=FMath::Max(MaxX,X);MinY=FMath::Min(MinY,Y);MaxY=FMath::Max(MaxY,Y);for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX){const int32 NX=X+DX,NY=Y+DY;if(NX>=LX&&NX<RX&&NY>=TY&&NY<BY&&Work[NY*SW+NX]){Work[NY*SW+NX]=0;Queue.Add(NY*SW+NX);}}}int32 Contact=0;for(int32 X=MinX;X<=MaxX;++X){Contact+=Reg[FMath::Max(TY,MinY-1)*SW+X];Contact+=Reg[FMath::Min(BY-1,MaxY+1)*SW+X];}for(int32 Y=MinY;Y<=MaxY;++Y){Contact+=Reg[Y*SW+FMath::Max(LX,MinX-1)];Contact+=Reg[Y*SW+FMath::Min(RX-1,MaxX+1)];}const float BW=(MaxX-MinX+1)*Step,BH=(MaxY-MinY+1)*Step;const bool Edge=MinX<=0||MaxX>=SW-1||MinY<=0||MaxY>=SH-1;const bool bRoiClipped=MinX<=LX||MaxX>=RX-1||MinY<=TY||MaxY>=BY-1;++TotalOpeningComponents;const bool bUsable=Queue.Num()>=12&&BW>=12&&BH>=8&&Contact>=2&&!Edge;if(bUsable){++UsableOpeningComponents;if(bRoiClipped&&VisionPairDiagnostics.Num()<48)VisionPairDiagnostics.Add(FString::Printf(TEXT("region %d opening touches ROI boundary; retained for expanded/global search"),R.Label));Cs.Add({FVector2D((MinX+MaxX+1)*Step*.5f,(MinY+MaxY+1)*Step*.5f),FVector2D(BW*.5f,BH*.5f),Queue.Num(),Contact,Edge});}else if(VisionPairDiagnostics.Num()<48)VisionPairDiagnostics.Add(FString::Printf(TEXT("region %d opening reject: area=%d size=%.0fx%.0f contact=%d edge=%d"),R.Label,Queue.Num(),BW,BH,Contact,Edge));}
		// Depth geometry rejects dark regions painted on the floor.  The four
		// material-border samples are unprojected to world space; a floor patch has
		// a normal close to world up, while a pallet front is approximately vertical.
		const float GeometryFocal=W/(2.f*FMath::Tan(FMath::DegreesToRadians(PalletSensorFieldOfViewDeg*.5f)));
		const auto PixelToWorld=[&](int32 PX,int32 PY,FVector& Out)->bool
		{
			if (!bDepthAvailable) return false; PX=FMath::Clamp(PX,0,W-1); PY=FMath::Clamp(PY,0,H-1);
			const float D=LatestDepthCm[PY*W+PX]; if (!FMath::IsFinite(D)||D<DepthMinimumCm||D>DepthMaximumCm) return false;
			const FVector CameraPoint(D,(PX-W*.5f)*D/GeometryFocal,-(PY-H*.5f)*D/GeometryFocal);
			Out=DepthFrameCameraTransform.TransformPosition(CameraPoint); return true;
		};
		const auto IsFloorPatch=[&](const FC& Candidate,float& OutUpDot,bool& bFrontMeasured)->bool
		{
			OutUpDot=0.f; bFrontMeasured=false; FVector Left,Right,Top,Bottom;
			const int32 X0=FMath::RoundToInt(Candidate.C.X-Candidate.E.X-5.f), X1=FMath::RoundToInt(Candidate.C.X+Candidate.E.X+5.f), Y0=FMath::RoundToInt(Candidate.C.Y-Candidate.E.Y-5.f), Y1=FMath::RoundToInt(Candidate.C.Y+Candidate.E.Y+5.f), MX=FMath::RoundToInt(Candidate.C.X), MY=FMath::RoundToInt(Candidate.C.Y);
			if (!PixelToWorld(X0,MY,Left)||!PixelToWorld(X1,MY,Right)||!PixelToWorld(MX,Y0,Top)||!PixelToWorld(MX,Y1,Bottom)) return false;
			bFrontMeasured=true;
			const FVector Normal=FVector::CrossProduct(Right-Left,Bottom-Top).GetSafeNormal();
			if (Normal.IsNearlyZero()) return false; OutUpDot=FMath::Abs(FVector::DotProduct(Normal,FVector::UpVector)); return OutUpDot>.65f;
		};
		TArray<FC> FrontComponents; FrontComponents.Reserve(Cs.Num());
		for (const FC& Candidate : Cs)
		{
			float UpDot=0.f; bool bFrontMeasured=false;
			if (!bDepthAvailable || !IsFloorPatch(Candidate,UpDot,bFrontMeasured) && !bFrontMeasured) { if(VisionPairDiagnostics.Num()<48) VisionPairDiagnostics.Add(FString::Printf(TEXT("region %d opening reject: no valid front-rim depth"),R.Label)); }
			else if (IsFloorPatch(Candidate,UpDot,bFrontMeasured)) { if(VisionPairDiagnostics.Num()<48) VisionPairDiagnostics.Add(FString::Printf(TEXT("region %d opening reject: depth front is floor plane upDot %.2f"),R.Label,UpDot)); }
			else FrontComponents.Add(Candidate);
		}
		Cs=MoveTemp(FrontComponents);
		for(int32 A=0;A<Cs.Num();++A)for(int32 D=A+1;D<Cs.Num();++D){++TestedOpeningPairs;FC L=Cs[A],RR=Cs[D];if(L.C.X>RR.C.X)Swap(L,RR);const float Sep=RR.C.X-L.C.X,Width=L.E.X+RR.E.X,HD=FMath::Abs(L.C.Y-RR.C.Y),SD=FMath::Abs(L.E.X-RR.E.X)/FMath::Max(1.f,FMath::Max(L.E.X,RR.E.X)),Ratio=Sep/FMath::Max(1.f,Width),Expected=VisionKnownOpeningCenterSpacingCm/FMath::Max(1.f,VisionKnownOpeningWidthCm),MaxHD=FMath::Max(12.f,(L.E.Y+RR.E.Y)*.75f);const bool Good=SD<=.55f&&HD<=MaxHD&&Ratio>=.35f&&Ratio<=4.5f;const float Score=FMath::Clamp(((1.f-SD)+(1.f-HD/MaxHD)+(1.f-FMath::Abs(Ratio-Expected)/Expected)+FMath::Min(1.f,(L.Contact+RR.Contact)/12.f))*.25f,0.f,1.f);VisionPairDiagnostics.Add(FString::Printf(TEXT("region %d pair: contact %d/%d widthDiff %.2f level %.1f/%.1f ratio %.2f score %.2f%s"),R.Label,L.Contact,RR.Contact,SD,HD,MaxHD,Ratio,Score,Good?TEXT(""):TEXT(" rejected")));if(Good&&Score>=MinimumOpeningConfidence&&Score>Best){Best=Score;BestLC=L.C;BestRC=RR.C;BestLE=L.E;BestRE=RR.E;BestRegion=Reg;BestBody=Reg;BestOpen=Open;BestBounds=FIntRect(LX,TY,RX,BY);}}
	}
	if(Best<MinimumOpeningConfidence)
	{
		++LockedTargetMissFrames;
		if (LastVisionSuccessSensorFrameId >= 0 && FirstVisionFailureAfterSuccessSensorFrameId < 0) FirstVisionFailureAfterSuccessSensorFrameId = SensorFrameId;
		LastVisionFineMs = static_cast<float>((FPlatformTime::Seconds()-FineStartSeconds)*1000.0);
		LastVisionTotalMs = static_cast<float>((FPlatformTime::Seconds()-VisionStartSeconds)*1000.0); LastVisionMaxMs = FMath::Max(LastVisionMaxMs, LastVisionTotalMs);
		LastVisionStageStatus=FString::Printf(TEXT("fine regions=%d/%d; opening components=%d usable=%d pairs=%d; depth=%s"), FineCandidates.Num(), CandidateRegions.Num(), TotalOpeningComponents, UsableOpeningComponents, TestedOpeningPairs, bDepthAvailable?TEXT("available"):TEXT("unavailable"));
		LastVisionFailureReason=FString::Printf(TEXT("No verified opening pair (%s; tracked target miss %d; full/coarse search retained)"), *LastVisionStageStatus, LockedTargetMissFrames);
	if (bSavedLastValidVisionArtifact && !bSavedFirstVisionFailureArtifact) { SaveVisionTransitionArtifacts(LastSuccessfulVisionPixels, true); SaveVisionTransitionArtifacts(Pixels, false); bSavedFirstVisionFailureArtifact = true; }
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_VISION] target=locked frame=%d result=failed misses=%d lastAge=%.2fs reason=%s"), LastVisionFrameNumber, LockedTargetMissFrames, LastTrackedOpeningTime > -BIG_NUMBER*.5f ? GetWorld()->GetTimeSeconds()-LastTrackedOpeningTime : -1.f, *LastVisionFailureReason);
		return;
	}
	LastVisionFineMs = static_cast<float>((FPlatformTime::Seconds()-FineStartSeconds)*1000.0);
	LastVisionTotalMs = static_cast<float>((FPlatformTime::Seconds()-VisionStartSeconds)*1000.0); LastVisionMaxMs = FMath::Max(LastVisionMaxMs, LastVisionTotalMs);
	VisionRegionMask=MoveTemp(BestRegion);VisionBodyMask=MoveTemp(BestBody);VisionMask=MoveTemp(BestOpen);VisionMaskWidth=SW;VisionMaskHeight=SH;VisionBodyBounds=BestBounds;VisionCandidates.Add({BestLC,BestLE,0,true,false,TEXT("selected opening")});VisionCandidates.Add({BestRC,BestRE,0,true,false,TEXT("selected opening")});
	const float Sep=BestRC.X-BestLC.X,Focal=W/(2.f*FMath::Tan(FMath::DegreesToRadians(PalletSensorFieldOfViewDeg*.5f)));OpeningDetection.bValid=true;OpeningDetection.Confidence=Best;OpeningDetection.TimestampSeconds=GetWorld()->GetTimeSeconds();bLastVisionHadValidPair=true;OpeningDetection.LeftCenter=BestLC;OpeningDetection.RightCenter=BestRC;OpeningDetection.LeftExtent=BestLE;OpeningDetection.RightExtent=BestRE;OpeningDetection.EstimatedDistanceCm=VisionKnownOpeningCenterSpacingCm*Focal/FMath::Max(1.f,Sep);OpeningDetection.MonocularDistanceCm=OpeningDetection.EstimatedDistanceCm;const float CenterX=(BestLC.X+BestRC.X)*.5f;OpeningDetection.EstimatedLateralCm=PalletSensorLateralOffsetFromCenterCm+(CenterX-W*.5f)*OpeningDetection.EstimatedDistanceCm/Focal;if (bDepthAvailable) UpdateDepthForOpeningDetection(); if (!OpeningDetection.bDepthValid || FMath::Abs(OpeningDetection.FrontOpticalDepthCm-OpeningDetection.MonocularDistanceCm)>DepthRgbDisagreementCm) { LastVisionFailureReason=FString::Printf(TEXT("RGB pair valid; depth unavailable/discordant — automatic motion held: mono %.1fcm depth %s"),OpeningDetection.MonocularDistanceCm,OpeningDetection.bDepthValid ? *FString::Printf(TEXT("%.1fcm"),OpeningDetection.FrontOpticalDepthCm) : TEXT("unmeasured")); }
	// Reject a discontinuous candidate rather than silently switching targets.
	if ((AutoPickupState == EAutoPickupState::Approaching || AutoPickupState == EAutoPickupState::Aligning) && LastTrackedOpeningDetection.bValid)
	{
		const FVector2D OldCentre=(LastTrackedOpeningDetection.LeftCenter+LastTrackedOpeningDetection.RightCenter)*.5f;
		const FVector2D NewCentre=(BestLC+BestRC)*.5f;
		const float AllowedShift=FMath::Max(90.f, (LastTrackedOpeningDetection.LeftExtent.X+LastTrackedOpeningDetection.RightExtent.X)*3.f);
		const float ForwardTravel = FVector::DotProduct(GetActorLocation()-LastTrackedActorLocation, GetActorForwardVector());
		const float ExpectedDistance = FMath::Max(30.f, LastTrackedOpeningDetection.EstimatedDistanceCm-FMath::Max(0.f,ForwardTravel));
		const float ExpectedSeparation = LastTrackedOpeningSeparationPx * LastTrackedOpeningDetection.EstimatedDistanceCm / ExpectedDistance;
		const float SeparationRatio = LastTrackedOpeningSeparationPx > SMALL_NUMBER ? Sep / LastTrackedOpeningSeparationPx : 1.f;
		const bool bDistanceRegressedWhileForward = ForwardTravel > 5.f && OpeningDetection.EstimatedDistanceCm > LastTrackedOpeningDetection.EstimatedDistanceCm + 12.f;
		const bool bScaleInconsistent = ForwardTravel > 5.f && (Sep < ExpectedSeparation*.60f || Sep > ExpectedSeparation*1.70f);
		if (FVector2D::Distance(OldCentre,NewCentre)>AllowedShift)
		{
			OpeningDetection.bValid=false; bLastVisionHadValidPair=false; ++LockedTargetMissFrames;
			LastVisionFailureReason=FString::Printf(TEXT("candidate changed target: %.0fpx > %.0fpx"), FVector2D::Distance(OldCentre,NewCentre), AllowedShift);
			if (bSavedLastValidVisionArtifact && !bSavedFirstVisionFailureArtifact) { SaveVisionTransitionArtifacts(LastSuccessfulVisionPixels, true); SaveVisionTransitionArtifacts(Pixels, false); bSavedFirstVisionFailureArtifact = true; }
			UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_VISION] target continuity rejected: %s"), *LastVisionFailureReason);
			return;
		}
		if (bDistanceRegressedWhileForward || bScaleInconsistent)
		{
			OpeningDetection.bValid=false; bLastVisionHadValidPair=false; ++LockedTargetMissFrames;
			LastVisionFailureReason=FString::Printf(TEXT("tracked pair scale/range inconsistent: forward %.1fcm sep %.1f/%.1f ratio %.2f distance %.1f<- %.1f"), ForwardTravel, Sep, ExpectedSeparation, SeparationRatio, OpeningDetection.EstimatedDistanceCm, LastTrackedOpeningDetection.EstimatedDistanceCm);
			if (bSavedLastValidVisionArtifact && !bSavedFirstVisionFailureArtifact) { SaveVisionTransitionArtifacts(LastSuccessfulVisionPixels, true); SaveVisionTransitionArtifacts(Pixels, false); bSavedFirstVisionFailureArtifact = true; }
			UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_VISION] target measurement rejected: %s"), *LastVisionFailureReason);
			return;
		}
	}
	LastVisionStageStatus=FString::Printf(TEXT("RGB body/opening/pair accepted; depth %s"), OpeningDetection.bDepthValid?TEXT("front verified"):TEXT("not usable for control")); LastTrackedOpeningDetection=OpeningDetection; LastTrackedOpeningTime=OpeningDetection.TimestampSeconds; LastTrackedActorLocation=GetActorLocation(); LastTrackedOpeningSeparationPx=Sep; LockedTargetMissFrames=0; LastVisionSuccessSensorFrameId=SensorFrameId; FirstVisionFailureAfterSuccessSensorFrameId=-1;
	LastSuccessfulVisionPixels = Pixels; bSavedLastValidVisionArtifact = true; bSavedFirstVisionFailureArtifact = false;
	UE_LOG(LogTemp, Log, TEXT("[R16HD_AUTO_VISION] target=locked frame=%d pair=(%.0f,%.0f)-(%.0f,%.0f) dist=%.1f lateral=%.1f confidence=%.2f"), LastVisionFrameNumber, BestLC.X, BestLC.Y, BestRC.X, BestRC.Y, OpeningDetection.EstimatedDistanceCm, OpeningDetection.EstimatedLateralCm, Best);
}
#if 0 // orphaned state-string tail from merge

	switch (State) { case EAutoPickupState::Idle: return TEXT("대기"); case EAutoPickupState::Detecting: return TEXT("인식 중"); case EAutoPickupState::Aligning: return TEXT("정렬 중"); case EAutoPickupState::Inserting: return TEXT("삽입 중"); case EAutoPickupState::Lifting: return TEXT("인양 중"); case EAutoPickupState::Complete: return TEXT("완료"); case EAutoPickupState::Stopped: return TEXT("중지"); default: return TEXT("실패"); }
}

#endif
#if 0 // duplicate pre-RGB-D auto-pickup/HUD/vision branch
void AAutomatedForklift::ClearAutomaticCommands() { DriveInput = SteerInput = LiftInput = ReachInput = SideShiftInput = 0.f; }
void AAutomatedForklift::SetAutoPickupState(EAutoPickupState NewState, const FString& Status)
{
	AutoPickupState = NewState; AutoPickupStatus = Status; AutoPickupStateElapsed = 0.f;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_PICK] state=%s reason=%s"), *AutoPickupStateToString(NewState), *Status);
}
void AAutomatedForklift::CancelAutoPickup(const FString& Reason, bool bFailure)
{
	ClearAutomaticCommands();
	SetAutoPickupState(bFailure ? EAutoPickupState::Failed : EAutoPickupState::Stopped, Reason);
}
void AAutomatedForklift::ToggleAutoPickup()
{
	StopNavigation();
	LastButtonInput = TEXT("시작"); LastButtonClickTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (AutoPickupState == EAutoPickupState::Detecting || AutoPickupState == EAutoPickupState::Aligning || AutoPickupState == EAutoPickupState::Inserting || AutoPickupState == EAutoPickupState::Lifting) { LastButtonInput = TEXT("중지"); CancelAutoPickup(TEXT("사용자가 작업 중지 버튼을 선택"), false); return; }
	if (!bPalletSensorCaptureActive) { SetAutoPickupState(EAutoPickupState::Failed, TEXT("카메라 캡처가 꺼져 있음 — N 키 또는 설정 확인 필요")); return; }
	if (LiftCm > 3.f || ReachCm > 3.f) { SetAutoPickupState(EAutoPickupState::Failed, TEXT("수동 초기화 필요 — 포크 하강 및 리치 후퇴")); return; }
	OpeningStableFrames = AlignmentStableFrames = 0; LastVisionCandidateCount = -1; LastVisionFrameNumber = 0; LastVisionProcessTime = -BIG_NUMBER; bLastVisionReadSucceeded = bLastVisionHadValidPair = false; LastPixelReadStatus = TEXT("카메라 프레임 대기"); LastAlignmentLateralErrorCm = LastAlignmentLiftCorrectionCm = LastAutoInsertionTravelCm = LastPhysicalPalletRiseCm = AutoRequiredForwardTravelCm = AutoMaximumForwardTravelCm = -BIG_NUMBER; bLastAutoDriveBlocked = bLastPhysicalPalletContact = false;
	SetAutoPickupState(EAutoPickupState::Detecting, TEXT("시작 버튼 입력됨 — 안정적인 삽입구 쌍 영상 대기 중"));
}

void AAutomatedForklift::NotifyAutoPickupButtonClick(bool bInsideButton, const FVector2D& ScreenPosition)
{
	LastButtonClickTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	LastButtonInput = bInsideButton ? TEXT("버튼 영역 클릭") : TEXT("버튼 밖 좌클릭");
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_PICK_UI] forklift=%s click=(%.0f,%.0f) inside=%d state=%s"), *GetName(), ScreenPosition.X, ScreenPosition.Y, bInsideButton, *AutoPickupStateToString(AutoPickupState));
	if (bInsideButton) ToggleAutoPickup();
}

void AAutomatedForklift::GetAutoPickupDiagnosticLines(TArray<FString>& OutLines) const
{
	OutLines.Reset(); const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const auto AgeText = [Now](float Time) { return Time <= -BIG_NUMBER * .5f ? FString(TEXT("미측정")) : FString::Printf(TEXT("%.1fs"), FMath::Max(0.f, Now - Time)); };
	const auto ValueText = [](float Value, const TCHAR* Unit) { return Value <= -BIG_NUMBER * .5f ? FString(TEXT("미측정")) : FString::Printf(TEXT("%.1f%s"), Value, Unit); };
	OutLines.Add(FString::Printf(TEXT("상태: %s"), *AutoPickupStateToString(AutoPickupState)));
	OutLines.Add(FString::Printf(TEXT("버튼: %s (%s 전)"), *LastButtonInput, *AgeText(LastButtonClickTime)));
	OutLines.Add(FString::Printf(TEXT("단계: %.1fs — %s"), AutoPickupStateElapsed, *AutoPickupStatus));
	OutLines.Add(FString::Printf(TEXT("영상: 캡처 %s / RT %s / 처리 #%d (%s 전) / 픽셀 %s"), bPalletSensorCaptureActive ? TEXT("켜짐") : TEXT("꺼짐"), PalletSensorRenderTarget ? TEXT("유효") : TEXT("없음"), LastVisionFrameNumber, *AgeText(LastVisionProcessTime), *LastPixelReadStatus));
	OutLines.Add(FString::Printf(TEXT("이진화: 밝기<58 / 4px 축소 / 동적 ROI 시작 %s"), LastVisionSearchStartY >= 0 ? *FString::FromInt(LastVisionSearchStartY * 4) : TEXT("미측정")));
	OutLines.Add(FString::Printf(TEXT("인식: 후보 %s / 쌍 %s / 신뢰도 %s / 안정 %d/3"), LastVisionCandidateCount >= 0 ? *FString::FromInt(LastVisionCandidateCount) : TEXT("미측정"), bLastVisionHadValidPair ? TEXT("유효") : TEXT("없음"), OpeningDetection.TimestampSeconds > -BIG_NUMBER * .5f ? *FString::Printf(TEXT("%.2f"), OpeningDetection.Confidence) : TEXT("미측정"), OpeningStableFrames));
	if (VisionPairDiagnostics.IsEmpty()) OutLines.Add(TEXT("쌍 판정: 미측정 또는 후보 부족"));
	else for (int32 Index = 0; Index < FMath::Min(2, VisionPairDiagnostics.Num()); ++Index) OutLines.Add(VisionPairDiagnostics[Index]);
	OutLines.Add(FString::Printf(TEXT("정렬: 좌우 %s / 높이 %s / 명령 E/Q %.2f Z/C %.2f / 실제 높이 %.1f 좌우 %.1f"), *ValueText(LastAlignmentLateralErrorCm, TEXT("cm")), *ValueText(LastAlignmentLiftCorrectionCm, TEXT("cm")), LiftInput, SideShiftInput, LiftCm, SideShiftCm));
	OutLines.Add(FString::Printf(TEXT("삽입: W/S %.2f / 이동 %s / 목표 %s / 차단 %s"), DriveInput, *ValueText(LastAutoInsertionTravelCm, TEXT("cm")), *ValueText(AutoRequiredForwardTravelCm, TEXT("cm")), bLastAutoDriveBlocked ? TEXT("예") : TEXT("아니오")));
	OutLines.Add(FString::Printf(TEXT("인양: E/Q %.2f / 파레트 접촉 %s / 상승 %s"), LiftInput, bLastPhysicalPalletContact ? TEXT("확인") : TEXT("미확인"), *ValueText(LastPhysicalPalletRiseCm, TEXT("cm"))));
}

void AAutomatedForklift::ProcessOpeningVision()
{
	if (!PalletSensorRenderTarget || !GetWorld()) return;
	if (!(AutoPickupState == EAutoPickupState::Detecting || AutoPickupState == EAutoPickupState::Aligning || AutoPickupState == EAutoPickupState::Inserting)) return;
	VisionProcessElapsed += GetWorld()->GetDeltaSeconds();
	if (VisionProcessElapsed < VisionProcessIntervalSeconds) return;
	VisionProcessElapsed = 0.f;
	++LastVisionFrameNumber; LastVisionProcessTime = GetWorld()->GetTimeSeconds(); LastVisionCandidateCount = -1; bLastVisionHadValidPair = false; OpeningDetection = FOpeningDetection(); VisionCandidates.Reset(); VisionPairDiagnostics.Reset();
	FTextureRenderTargetResource* Resource = PalletSensorRenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != PalletSensorWidthPx * PalletSensorHeightPx) { OpeningDetection.bValid = false; bLastVisionReadSucceeded = false; LastPixelReadStatus = TEXT("실패"); return; }
	bLastVisionReadSucceeded = true; LastPixelReadStatus = TEXT("성공");
	const int32 W = PalletSensorWidthPx, H = PalletSensorHeightPx, Step = 4, SW = W / Step, SH = H / Step;
	TArray<float> RowLuma; RowLuma.SetNumZeroed(SH);
	for (int32 Y = 0; Y < SH; ++Y) for (int32 X = 0; X < SW; ++X)
	{
		const FColor C = Pixels[(Y * Step) * W + X * Step];
		RowLuma[Y] += (static_cast<float>(C.R) * .2126f + static_cast<float>(C.G) * .7152f + static_cast<float>(C.B) * .0722f) / SW;
	}
	int32 StrongestBoundaryRow = static_cast<int32>(SH * .30f); float StrongestGradient = 0.f;
	for (int32 Y = static_cast<int32>(SH * .20f); Y < static_cast<int32>(SH * .70f); ++Y)
	{
		const float Gradient = FMath::Abs(RowLuma[Y] - RowLuma[Y - 1]); if (Gradient > StrongestGradient) { StrongestGradient = Gradient; StrongestBoundaryRow = Y; }
	}
	const int32 SearchTop = FMath::Clamp(FMath::Max(static_cast<int32>(SH * .30f), StrongestBoundaryRow + 2), 0, static_cast<int32>(SH * .65f));
	LastVisionSearchStartY = SearchTop;
	TArray<uint8> Dark; Dark.SetNumZeroed(SW * SH);
	for (int32 Y = SearchTop; Y < SH * 9 / 10; ++Y) for (int32 X = 0; X < SW; ++X)
	{
		const FColor C = Pixels[(Y * Step) * W + X * Step];
		const int32 Luma = (static_cast<int32>(C.R) * 54 + static_cast<int32>(C.G) * 183 + static_cast<int32>(C.B) * 19) >> 8;
		Dark[Y * SW + X] = Luma < 58 ? 1 : 0;
	}
	VisionMask = Dark; VisionMaskWidth = SW; VisionMaskHeight = SH;
	TArray<uint8> WorkMask = Dark;
	struct FCandidate { FVector2D C, E; int32 Area; bool bTouchesBoundary; }; TArray<FCandidate> Candidates; TArray<int32> Queue;
	for (int32 Seed = 0; Seed < WorkMask.Num(); ++Seed) if (WorkMask[Seed])
	{
		Queue.Reset(); Queue.Add(Seed); WorkMask[Seed] = 0; int32 MinX = SW, MaxX = 0, MinY = SH, MaxY = 0;
		for (int32 I = 0; I < Queue.Num(); ++I) { const int32 P = Queue[I], X = P % SW, Y = P / SW; MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X); MinY = FMath::Min(MinY, Y); MaxY = FMath::Max(MaxY, Y);
			for (int32 DY = -1; DY <= 1; ++DY) for (int32 DX = -1; DX <= 1; ++DX) { const int32 NX = X + DX, NY = Y + DY; if (NX >= 0 && NX < SW && NY >= 0 && NY < SH && WorkMask[NY * SW + NX]) { WorkMask[NY * SW + NX] = 0; Queue.Add(NY * SW + NX); } }
		}
		const float BW = (MaxX - MinX + 1) * Step, BH = (MaxY - MinY + 1) * Step;
		const bool bTouchesBoundary = MinX == 0 || MaxX == SW - 1 || MinY <= SearchTop || MaxY >= SH * 9 / 10 - 1;
		if (Queue.Num() >= 16 && BW >= 18.f && BH >= 10.f && BW / BH >= .5f && BW / BH <= 5.f) Candidates.Add({ FVector2D((MinX + MaxX + 1) * Step * .5f, (MinY + MaxY + 1) * Step * .5f), FVector2D(BW * .5f, BH * .5f), Queue.Num(), bTouchesBoundary });
	}
	LastVisionCandidateCount = Candidates.Num(); OpeningDetection.bValid = false; float Best = 0.f; int32 BestA = INDEX_NONE, BestB = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index) VisionCandidates.Add({ Candidates[Index].C, Candidates[Index].E, Candidates[Index].Area, false, Candidates[Index].bTouchesBoundary, Candidates[Index].bTouchesBoundary ? TEXT("검색 경계와 연결됨 — 자기 가림/그림자 의심") : TEXT("쌍 평가 대기") });
	for (int32 A = 0; A < Candidates.Num(); ++A) for (int32 B = A + 1; B < Candidates.Num(); ++B)
	{
		int32 LeftIndex = A, RightIndex = B; FCandidate L = Candidates[A], R = Candidates[B]; if (L.C.X > R.C.X) { Swap(L, R); Swap(LeftIndex, RightIndex); }
		const float Separation = R.C.X - L.C.X, AverageWidth = (L.E.X + R.E.X), AverageHeight = (L.E.Y + R.E.Y);
		const float SizeDifference = FMath::Abs(L.E.X - R.E.X) / FMath::Max(1.f, FMath::Max(L.E.X, R.E.X));
		const float HeightDelta = FMath::Abs(L.C.Y - R.C.Y);
		// The known opening-centre spacing versus opening width is scale invariant.
		// It remains meaningful as the truck approaches, unlike a fixed image fraction.
		const float SpacingToWidth = Separation / FMath::Max(1.f, AverageWidth);
		const float MaxHeightDelta = FMath::Max(12.f, AverageHeight * .75f);
		const bool bSizeOK = SizeDifference <= .45f;
		const bool bLevelOK = HeightDelta <= MaxHeightDelta;
		const bool bSpacingOK = SpacingToWidth >= .80f && SpacingToWidth <= 3.20f;
		const bool bBoundaryOK = !Candidates[LeftIndex].bTouchesBoundary && !Candidates[RightIndex].bTouchesBoundary;
		const float SizeScore = 1.f - SizeDifference, LevelScore = 1.f - HeightDelta / FMath::Max(1.f, MaxHeightDelta), SpacingScore = 1.f - FMath::Abs(SpacingToWidth - 1.67f) / 1.67f;
		const float ExpectedSpacingToWidth = VisionKnownOpeningCenterSpacingCm / FMath::Max(1.f, VisionKnownOpeningWidthCm);
		const float Score = FMath::Clamp((SizeScore + LevelScore + (1.f - FMath::Abs(SpacingToWidth - ExpectedSpacingToWidth) / ExpectedSpacingToWidth)) / 3.f, 0.f, 1.f);
		const bool bConfidenceOK = Score >= MinimumOpeningConfidence;
		const bool bPairOK = bSizeOK && bLevelOK && bSpacingOK && bBoundaryOK && bConfidenceOK;
		const FString Reason = FString::Printf(TEXT("쌍 %d-%d: 폭차 %.0f%% (<=45), 높이차 %.0fpx (<=%.0f), 간격/폭 %.2f (0.80~3.20), 경계 %s, 점수 %.2f (>=%.2f)%s"), LeftIndex + 1, RightIndex + 1, SizeDifference * 100.f, HeightDelta, MaxHeightDelta, SpacingToWidth, bBoundaryOK ? TEXT("정상") : TEXT("접촉"), Score, MinimumOpeningConfidence, bPairOK ? TEXT("") : TEXT(" — 탈락"));
		VisionPairDiagnostics.Add(Reason);
		if (bPairOK && Score > Best) { Best = Score; BestA = LeftIndex; BestB = RightIndex; OpeningDetection.LeftCenter = L.C; OpeningDetection.RightCenter = R.C; OpeningDetection.LeftExtent = L.E; OpeningDetection.RightExtent = R.E; }
	}
	if (Best >= MinimumOpeningConfidence)
	{
		const float Separation = OpeningDetection.RightCenter.X - OpeningDetection.LeftCenter.X;
		const float Focal = W / (2.f * FMath::Tan(FMath::DegreesToRadians(PalletSensorFieldOfViewDeg * .5f)));
		OpeningDetection.bValid = true; OpeningDetection.Confidence = Best; OpeningDetection.TimestampSeconds = GetWorld()->GetTimeSeconds();
		bLastVisionHadValidPair = true;
		OpeningDetection.EstimatedDistanceCm = VisionKnownOpeningCenterSpacingCm * Focal / FMath::Max(1.f, Separation);
		const float CenterX = (OpeningDetection.LeftCenter.X + OpeningDetection.RightCenter.X) * .5f;
		OpeningDetection.EstimatedLateralCm = PalletSensorLateralOffsetFromCenterCm + (CenterX - W * .5f) * OpeningDetection.EstimatedDistanceCm / Focal;
		if (VisionCandidates.IsValidIndex(BestA)) { VisionCandidates[BestA].bSelected = true; VisionCandidates[BestA].Reason = TEXT("최종 삽입구 쌍 선택"); }
		if (VisionCandidates.IsValidIndex(BestB)) { VisionCandidates[BestB].bSelected = true; VisionCandidates[BestB].Reason = TEXT("최종 삽입구 쌍 선택"); }
	}
	else
	{
		for (FVisionCandidateDebug& Candidate : VisionCandidates) Candidate.Reason = VisionPairDiagnostics.IsEmpty() ? TEXT("짝 후보 없음") : TEXT("쌍 조건 탈락 — 아래 쌍 진단 참조");
	}

}

bool AAutomatedForklift::HasPhysicalPalletLift(float& OutPalletRiseCm) const
{
	OutPalletRiseCm = 0.f;
	for (TActorIterator<APallet> It(GetWorld()); It; ++It) { FHitResult Hit; UPrimitiveComponent* Other = nullptr; if ((*It)->GetRecentPhysicsContact(.75f, Hit, Other) && (Other == LeftForkCollision || Other == RightForkCollision)) { OutPalletRiseCm = (*It)->GetActorLocation().Z - AutoLiftPalletStartZ; return OutPalletRiseCm > 2.f; } }
	for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It) { UPrimitiveComponent* Other = nullptr; if ((*It)->GetRecentForkContact(.75f, Other) && (Other == LeftForkCollision || Other == RightForkCollision)) { OutPalletRiseCm = (*It)->GetActorLocation().Z - AutoLiftPalletStartZ; return OutPalletRiseCm > 2.f; } }
	return false;
}

#endif
bool AAutomatedForklift::HasPhysicalPalletLift(float& OutPalletRiseCm) const
{
	OutPalletRiseCm = 0.f;
	for (TActorIterator<APallet> It(GetWorld()); It; ++It)
	{
		FHitResult Hit; UPrimitiveComponent* Other = nullptr;
		if ((*It)->GetRecentPhysicsContact(.75f, Hit, Other) && (Other == LeftForkCollision || Other == RightForkCollision))
		{
			OutPalletRiseCm = (*It)->GetActorLocation().Z - AutoLiftPalletStartZ;
			return OutPalletRiseCm > 2.f;
		}
	}
	for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It)
	{
		UPrimitiveComponent* Other = nullptr;
		if ((*It)->GetRecentForkContact(.75f, Other) && (Other == LeftForkCollision || Other == RightForkCollision))
		{
			OutPalletRiseCm = (*It)->GetActorLocation().Z - AutoLiftPalletStartZ;
			return OutPalletRiseCm > 2.f;
		}
	}
	return false;
}
void AAutomatedForklift::UpdateAutoPickup(float DeltaSeconds)
{
	if (AutoPickupState == EAutoPickupState::Idle || AutoPickupState == EAutoPickupState::Stopped || AutoPickupState == EAutoPickupState::Complete || AutoPickupState == EAutoPickupState::Failed) return;

	// Rebuilt every Tick; unlike the manual axis variables these commands cannot
	// be erased by the input system's per-frame zero-valued axis callbacks.
	ClearAutomaticCommands();
	AutoPickupStateElapsed += DeltaSeconds;
	const bool bFresh = OpeningDetection.bValid && OpeningDetection.bDepthValid && GetWorld()->GetTimeSeconds() - OpeningDetection.TimestampSeconds <= VisionMaximumFrameAgeSeconds;
	if (AutoPickupState == EAutoPickupState::Detecting) { if (bFresh) { if (LastStableOpeningVisionFrame != LastVisionFrameNumber) { ++OpeningStableFrames; LastStableOpeningVisionFrame = LastVisionFrameNumber; } } else { OpeningStableFrames = 0; LastStableOpeningVisionFrame = -1; } AutoPickupStatus = !OpeningDetection.bValid ? FString::Printf(TEXT("RGB 삽입구 후보 %d개 — 유효한 쌍 없음"), LastVisionCandidateCount) : (!OpeningDetection.bDepthValid ? FString::Printf(TEXT("RGB 쌍은 확인됨 — Depth 대기/실패: %s"), *LastDepthReadStatus) : FString::Printf(TEXT("삽입구 쌍 확인 — 서로 다른 프레임 안정 %d/3"), OpeningStableFrames)); if (OpeningStableFrames >= 3) { LockedOpeningDetection = OpeningDetection; bPreApproachHeightAlignment = true; SetAutoPickupState(EAutoPickupState::Aligning, TEXT("안정 RGB-D 쌍 잠금 — 차체 정지 상태에서 포크 높이·좌우 사전 정렬")); } else if (AutoPickupStateElapsed > 5.f) CancelAutoPickup(OpeningDetection.bValid && !OpeningDetection.bDepthValid ? TEXT("RGB 쌍은 있으나 유효한 SceneDepth가 없음 — 자동 제어 보류") : TEXT("유효한 두 삽입구를 찾지 못함 — 수동 접근 위치 조정 필요"), true); return; }
	if (AutoPickupState == EAutoPickupState::Approaching)
	{
		if (!bFresh)
		{
			// Stop first.  The next vision passes use the expanding tracked ROI and
			// still run the global proposal search; no old detection is used to drive.
			AutoPickupStatus = FString::Printf(TEXT("접근 중 현재 관측 상실 — ROI 확대 재탐색 %d/6 (마지막 유효 %.1fs 전)"), LockedTargetMissFrames, LastTrackedOpeningTime > -BIG_NUMBER*.5f ? GetWorld()->GetTimeSeconds()-LastTrackedOpeningTime : -1.f);
			if (LockedTargetMissFrames > 6) CancelAutoPickup(TEXT("근거리 재탐색과 전체 탐색 모두에서 삽입구 쌍을 다시 찾지 못함"), true);
			return;
		}
		const float RawLateral = OpeningDetection.EstimatedLateralCm;
		// Never let an old, opposite-side measurement steer the vehicle.  A large
		// discontinuity is a target/measurement event and resets the filter rather
		// than making its delayed value cross zero on a later frame.
		if (AutoPickupStateElapsed < .15f || FMath::Abs(RawLateral-FilteredApproachLateralCm)>10.f || RawLateral*FilteredApproachLateralCm<0.f && FMath::Abs(RawLateral)>3.f) FilteredApproachLateralCm = RawLateral;
		else FilteredApproachLateralCm = FMath::Lerp(FilteredApproachLateralCm, RawLateral, .35f);
		float ForkTipRemaining, RemainingAdvance, ReachRemaining, ChassisAdvance, ForkLateralError, ForkLiftCorrection;
		if (!ComputeAutoInsertionGeometry(OpeningDetection, ForkTipRemaining, RemainingAdvance, ReachRemaining, ChassisAdvance, ForkLateralError, ForkLiftCorrection))
		{
			CancelAutoPickup(TEXT("접근 기하 계산 실패 — 현재 ForkCarriage RGB-D 좌표 미측정"), true); return;
		}
		AutoRequiredForkAdvanceCm = RemainingAdvance;
		AutoChassisAdvanceNeededCm = ChassisAdvance;
		AutoApproachRequiredTravelCm = ChassisAdvance;
		LastAutoCameraDistanceCm = OpeningDetection.EstimatedDistanceCm;
		LastAutoForkTipRemainingCm = ForkTipRemaining;
		LastAutoApproachTravelCm = FVector::DotProduct(GetActorLocation() - AutoApproachStartLocation, AutoApproachStartForward);
		const float DirectionErrorDeg = FMath::Abs(FMath::RadiansToDegrees(OpeningDetection.FrontYawErrorRad));
		const bool bDirectionNeedsChassis = DirectionErrorDeg > AutoDirectionToleranceDeg;
		const bool bChassisCloseEnough = AutoChassisAdvanceNeededCm <= AutoChassisApproachToleranceCm;
		const bool bWithinForkRange = FMath::Abs(ForkLateralError) <= Dimensions.ForkSideShiftCm + AutoApproachLateralToleranceCm;
		if (bChassisCloseEnough && bWithinForkRange && !bDirectionNeedsChassis)
		{
			SetAutoPickupState(EAutoPickupState::Aligning, TEXT("차체 접근 완료 — R/F 행정과 포크 높이·좌우 정렬 시작"));
			return;
		}
		// Movement beyond this plan means either the observation changed or a
		// physical obstacle/sweep prevented the predicted approach.  Stop rather
		// than converting the old 14 cm fixed target into an overrun.
		// A fresh RGB-D observation can require a genuine replan (for example
		// after direction correction), but only then do we rebase the travel
		// origin/budget.  Cumulative travel is never compared with a decreasing
		// "current remaining" distance.
		if (AutoChassisAdvanceNeededCm > FMath::Max(5.f, AutoApproachTravelBudgetCm - LastAutoApproachTravelCm))
		{
			AutoApproachStartLocation = GetActorLocation(); AutoApproachStartForward = GetActorForwardVector();
			AutoApproachPlannedTravelCm = AutoChassisAdvanceNeededCm; AutoApproachTravelBudgetCm = AutoApproachPlannedTravelCm + 35.f;
			LastAutoApproachTravelCm = 0.f; ++AutoApproachReplanCount;
			UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_APPROACH] replan=%d remaining=%.1fcm reason=fresh RGB-D geometry changed"), AutoApproachReplanCount, AutoChassisAdvanceNeededCm);
		}
		if (ForkTipRemaining < -AutoRequiredInsertionCm || LastAutoApproachTravelCm > AutoApproachTravelBudgetCm)
		{
			CancelAutoPickup(TEXT("접근 계획 범위 초과 — 포크끝/리치/삽입량 재측정 필요"), true); return;
		}
		if (FMath::Abs(FilteredApproachLateralCm) <= AutoApproachLateralDeadbandCm) bApproachSteeringInDeadband = true;
		else if (FMath::Abs(FilteredApproachLateralCm) >= AutoApproachLateralHysteresisCm) bApproachSteeringInDeadband = false;
		const float TravelDelta = LastAutoApproachTravelCm - LastAutoApproachTravelForHeadingCm;
		// This is a diagnostic image-motion slope, not pallet-heading estimation:
		// camera yaw and perspective both affect it.  It is deliberately excluded
		// from the steering command until a true orientation measurement exists.
		if (TravelDelta > 10.f) { AutoApproachHeadingErrorRad = FMath::Atan2(FilteredApproachLateralCm - PreviousApproachLateralCm, TravelDelta); PreviousApproachLateralCm = FilteredApproachLateralCm; LastAutoApproachTravelForHeadingCm = LastAutoApproachTravelCm; }
		const float LateralSteer = bApproachSteeringInDeadband ? 0.f : FilteredApproachLateralCm * AutoApproachLateralSteerGain;
		const float HeadingSteer = OpeningDetection.bDepthValid ? FMath::Clamp(-OpeningDetection.FrontYawErrorRad * AutoApproachHeadingSteerGain, -.12f, .12f) : 0.f;
		const float DesiredSteer = bAutoApproachStraightDiagnostic ? 0.f : FMath::Clamp(LateralSteer + HeadingSteer, -AutoApproachMaximumSteer, AutoApproachMaximumSteer);
		AutoSteerInput = FMath::FInterpConstantTo(LastAppliedSteerInput, DesiredSteer, DeltaSeconds, AutoApproachSteerRatePerSecond);
		const float NearScale = FMath::Clamp(AutoChassisAdvanceNeededCm / 90.f, .25f, 1.f);
		AutoDriveInput = AutoApproachSpeedScale * NearScale;
		const float Now = GetWorld()->GetTimeSeconds();
		if (OpeningDetection.EstimatedDistanceCm < AutoApproachLastProgressDistanceCm - 4.f) { AutoApproachLastProgressDistanceCm = OpeningDetection.EstimatedDistanceCm; AutoApproachLastProgressTime = Now; }
		bLastAutoApproachBlocked = AutoPickupStateElapsed > 1.0f && LastAutoApproachTravelCm < 2.f;
		AutoPickupStatus = FString::Printf(TEXT("접근 중 — 필요 포크전진 %.0fcm, R/F 잔여 %.0fcm, 차체 추가 %.0fcm, 방향 %.1fdeg"), AutoRequiredForkAdvanceCm, ReachRemaining, AutoChassisAdvanceNeededCm, DirectionErrorDeg);
		UE_LOG(LogTemp, Log, TEXT("[R16HD_AUTO_APPROACH] rawLat=%.2f filteredLat=%.2f heading=%.3frad lateralCmd=%.3f headingCmd=%.3f steer=%.3f yaw=%.3f travel=%.1f dist=%.1f"), RawLateral, FilteredApproachLateralCm, AutoApproachHeadingErrorRad, LateralSteer, HeadingSteer, AutoSteerInput, LastAutoYawDeltaDeg, LastAutoApproachTravelCm, OpeningDetection.EstimatedDistanceCm);
		if (bLastAutoApproachBlocked) CancelAutoPickup(TEXT("접근 명령은 적용됐으나 차체가 이동하지 않음 — Sweep/충돌 로그 확인 필요"), true);
		else if (LastAutoApproachTravelCm > 35.f && Now - AutoApproachLastProgressTime > 2.5f) CancelAutoPickup(TEXT("차체는 이동했지만 영상 거리 갱신이 없음 — 같은 목표 재인식 필요"), true);
		else
		{
			// Time is a diagnostic guard derived from *remaining* chassis distance
			// and the current commanded (decelerated) speed.  It cannot fail a
			// moving approach merely because its original range estimate was wrong.
			const float CommandSpeed = FMath::Max(1.f, Motion.ForwardSpeedCmPerSec * FMath::Abs(AutoDriveInput));
			const float DynamicBudget = FMath::Max(4.f, AutoChassisAdvanceNeededCm / CommandSpeed + 3.f);
			if (AutoPickupStateElapsed > DynamicBudget && Now - AutoApproachLastProgressTime > 1.5f)
				CancelAutoPickup(TEXT("접근 진행 정지 — 실제 이동/깊이 거리 갱신이 계획과 일치하지 않음"), true);
		}
		return;
	}
	if (AutoPickupState == EAutoPickupState::Aligning)
	{
		if (!bFresh) { CancelAutoPickup(TEXT("정렬 중 현재 RGB-D 측정 상실 — 오래된 구멍 좌표로 포크를 움직이지 않음"), true); return; }
		const float DirectionErrorDeg = FMath::Abs(FMath::RadiansToDegrees(OpeningDetection.FrontYawErrorRad));
		if (DirectionErrorDeg > AutoDirectionToleranceDeg) { SetAutoPickupState(EAutoPickupState::Approaching, TEXT("전면 방향 오차는 사이드시프트로 보정 불가 — 차체 방향 재정렬")); return; }
		const float UsableOpeningWidth = VisionKnownOpeningWidthCm - 2.f * AutoForkOpeningSideClearanceCm;
		if (Dimensions.ForkWidthCm > UsableOpeningWidth) { CancelAutoPickup(FString::Printf(TEXT("포크 폭 %.1fcm이 입구 유효폭 %.1fcm보다 큼"), Dimensions.ForkWidthCm, UsableOpeningWidth), true); return; }
		float ForkTipToFront, RemainingAdvance, ReachRemaining, ChassisAdvance, LateralError, LiftCorrection;
		if (!ComputeAutoInsertionGeometry(OpeningDetection, ForkTipToFront, RemainingAdvance, ReachRemaining, ChassisAdvance, LateralError, LiftCorrection))
		{
			CancelAutoPickup(TEXT("정렬 기하 계산 실패 — 현재 ForkCarriage 좌표 미측정"), true); return;
		}
		LastAlignmentLateralErrorCm = LateralError; LastAlignmentLiftCorrectionCm = LiftCorrection;
		const float LeftHeightCm = OpeningDetection.LeftMeasuredOpeningHeightCm > 1.f ? OpeningDetection.LeftMeasuredOpeningHeightCm : (OpeningDetection.MeasuredOpeningHeightCm > 1.f ? OpeningDetection.MeasuredOpeningHeightCm : VisionKnownOpeningHeightCm);
		const float RightHeightCm = OpeningDetection.RightMeasuredOpeningHeightCm > 1.f ? OpeningDetection.RightMeasuredOpeningHeightCm : (OpeningDetection.MeasuredOpeningHeightCm > 1.f ? OpeningDetection.MeasuredOpeningHeightCm : VisionKnownOpeningHeightCm);
		const FVector LeftOpening = OpeningDetection.LeftMeasuredOpeningHeightCm > 1.f ? OpeningDetection.LeftOpeningCenterInForkCm : OpeningDetection.OpeningCenterInForkCm;
		const FVector RightOpening = OpeningDetection.RightMeasuredOpeningHeightCm > 1.f ? OpeningDetection.RightOpeningCenterInForkCm : OpeningDetection.OpeningCenterInForkCm;
		const float LeftBottom = LeftOpening.Z - LeftHeightCm * .5f, LeftTop = LeftOpening.Z + LeftHeightCm * .5f;
		const float RightBottom = RightOpening.Z - RightHeightCm * .5f, RightTop = RightOpening.Z + RightHeightCm * .5f;
		LastMeasuredOpeningBottomCm = FMath::Max(LeftBottom, RightBottom);
		LastMeasuredOpeningTopCm = FMath::Min(LeftTop, RightTop);
		// Fork top: bottom clearance and its physical thickness occupy the lower
		// aperture limit; the upper clearance occupies the upper limit once.
		LastAllowedForkTopMinCm = LastMeasuredOpeningBottomCm + AutoForkOpeningBottomClearanceCm + Dimensions.ForkThicknessCm;
		LastAllowedForkTopMaxCm = LastMeasuredOpeningTopCm - AutoForkOpeningBottomClearanceCm;
		LastForkTopLocalCm = (LastAllowedForkTopMinCm + LastAllowedForkTopMaxCm) * .5f - LiftCorrection;
		LastHeightTargetLiftCm = LiftCm + LiftCorrection;
		if (LastAllowedForkTopMinCm > LastAllowedForkTopMaxCm)
		{
			if (LastStableAlignmentVisionFrame != LastVisionFrameNumber) { ++InvalidHeightRangeVisionFrames; LastStableAlignmentVisionFrame=LastVisionFrameNumber; }
			AutoPickupStatus=FString::Printf(TEXT("입구 높이 범위 역전 재측정 %d/3 — 하단 %.1f 상단 %.1f, 포크/여유 필요 %.1f"),InvalidHeightRangeVisionFrames,LastMeasuredOpeningBottomCm,LastMeasuredOpeningTopCm,Dimensions.ForkThicknessCm+2.f*AutoForkOpeningBottomClearanceCm);
			if (InvalidHeightRangeVisionFrames >= 3) CancelAutoPickup(TEXT("서로 다른 RGB-D 프레임 3개에서 삽입구 높이가 포크 두께와 여유를 수용하지 못함"), true);
			return;
		}
		InvalidHeightRangeVisionFrames=0;
		if (FMath::Abs(LateralError) > Dimensions.ForkSideShiftCm) { SetAutoPickupState(EAutoPickupState::Approaching, TEXT("포크 좌우 행정을 초과 — 차체 접근·방향 정렬로 복귀")); return; }
		if (LiftCm + LiftCorrection < -AutoVerticalToleranceCm || LiftCm + LiftCorrection > RequiredLiftStrokeCm + AutoVerticalToleranceCm) { CancelAutoPickup(TEXT("포크 인양 범위를 벗어남 — 수동 높이 조정 필요"), true); return; }
		AutoSideShiftInput = FMath::IsNearlyZero(LateralError, 1.2f) ? 0.f : FMath::Sign(LateralError) * .7f;
		AutoLiftInput = FMath::IsNearlyZero(LiftCorrection, AutoVerticalToleranceCm) ? 0.f : FMath::Sign(LiftCorrection) * .15f;
		if (FMath::Abs(LateralError) <= 1.2f && FMath::Abs(LiftCorrection) <= AutoVerticalToleranceCm) { if (LastStableAlignmentVisionFrame != LastVisionFrameNumber) { ++AlignmentStableFrames; LastStableAlignmentVisionFrame = LastVisionFrameNumber; } } else { AlignmentStableFrames = 0; LastStableAlignmentVisionFrame = -1; }
		AutoPickupStatus = FString::Printf(TEXT("정렬 중 — 포크기준 좌우 %.1fcm, 높이 %.1fcm, 방향 %.1fdeg"), LateralError, LiftCorrection, DirectionErrorDeg);
		if (AlignmentStableFrames >= 3)
		{
			AutoSideShiftInput = AutoLiftInput = 0.f; LockedOpeningDetection = OpeningDetection;
			if (bPreApproachHeightAlignment)
			{
				bPreApproachHeightAlignment = false;
				SetAutoPickupState(EAutoPickupState::Approaching, TEXT("포크 높이·좌우 사전 정렬 안정 — 차체 접근 시작")); return;
			}
			float ForkTipToPalletFrontCm, ReachNeededCm, ReachAvailableCm, ChassisNeeded, VerifiedLateral, VerifiedLift;
			if (!ComputeAutoInsertionGeometry(LockedOpeningDetection, ForkTipToPalletFrontCm, ReachNeededCm, ReachAvailableCm, ChassisNeeded, VerifiedLateral, VerifiedLift)) { CancelAutoPickup(TEXT("삽입 전 최종 기하 검증 실패"), true); return; }
			if (ReachNeededCm > ReachAvailableCm + AutoChassisApproachToleranceCm)
			{
				SetAutoPickupState(EAutoPickupState::Approaching, FString::Printf(TEXT("R/F 잔여 %.1fcm < 필요 %.1fcm — 차체 %.1fcm 추가 접근"), ReachAvailableCm, ReachNeededCm, ReachNeededCm-ReachAvailableCm)); return;
			}
			AutoRequiredForkAdvanceCm = ReachNeededCm;
			AutoReachTargetCm = FMath::Clamp(ReachCm + ReachNeededCm, 0.f, Dimensions.ReachTravelCm);
			AutoInsertionStartReachCm = ReachCm;
			AutoRequiredForwardTravelCm = ReachNeededCm;
			AutoMaximumForwardTravelCm = FMath::Min(ReachAvailableCm, FMath::Max(ReachNeededCm, ForkTipToPalletFrontCm + AutoMaximumInsertionCm));
			SetAutoPickupState(EAutoPickupState::Inserting, FString::Printf(TEXT("정렬 완료 — 차체 정지, R/F %.1fcm 전진 후 물리 인양"), ReachNeededCm));
		}
		if (AutoPickupStateElapsed > 6.f) CancelAutoPickup(TEXT("인양·좌우 정렬이 안정화되지 않음"), true); return;
	}
	if (AutoPickupState == EAutoPickupState::Inserting)
	{
		// The chassis stays still here.  R/F alone advances the real fork
		// collision bodies; the dynamic pallet is not pre-blocked, so its lift
		// remains a physical contact result rather than an attachment shortcut.
		AutoDriveInput = 0.f;
		AutoSteerInput = 0.f;
		AutoReachInput = AutoInsertSpeedScale;
		const float Travel = FMath::Max(0.f, ReachCm - AutoInsertionStartReachCm);
		LastAutoInsertionTravelCm = Travel;
		// Before the tips have entered, losing the newly observed target is not
		// an occlusion and must stop the operation.  After a short verified entry,
		// the fork/camera may hide the openings; only the prevalidated straight
		// path and measured reach are then allowed, never a stale image position.
		const bool bFreshInsertObservation = bFresh;
		const bool bExpectedForkOcclusion = Travel >= FMath::Min(5.f, AutoRequiredForkAdvanceCm * .20f) && LockedOpeningDetection.bDepthValid;
		if (!bFreshInsertObservation && !bExpectedForkOcclusion)
		{
			CancelAutoPickup(TEXT("삽입 전/중 현재 관측 상실 — 검증된 가림 조건을 만족하지 않아 정지"), true); return;
		}
		if (bFreshInsertObservation)
		{
			float TipToFront, RemainingAdvance, ReachRemaining, ChassisAdvance, LateralError, LiftCorrection;
			if (!ComputeAutoInsertionGeometry(OpeningDetection, TipToFront, RemainingAdvance, ReachRemaining, ChassisAdvance, LateralError, LiftCorrection) || FMath::Abs(LateralError) > AutoForkOpeningSideClearanceCm + 1.f || FMath::Abs(LiftCorrection) > AutoVerticalToleranceCm * 2.f)
			{
				CancelAutoPickup(TEXT("삽입 중 현재 RGB-D 정렬 오차가 허용 범위를 벗어남"), true); return;
			}
		}
		const bool bReachAtTarget = ReachCm >= AutoReachTargetCm - .5f;
		bLastAutoDriveBlocked = AutoPickupStateElapsed > 1.f && Travel < 1.f;
		AutoPickupStatus = bLastAutoDriveBlocked
			? TEXT("삽입 중 — R/F 명령은 있으나 실제 리치 이동 없음; 접촉 위치/정렬 오차 기록")
			: FString::Printf(TEXT("삽입 중 — 차체 정지, R/F 실제 %.1f / 목표 %.1fcm"), Travel, AutoRequiredForkAdvanceCm);
		if (bReachAtTarget)
		{
			AutoLiftStartCm = LiftCm; bool bBaselineSet = false;
			for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It) { AutoLiftPalletStartZ = (*It)->GetActorLocation().Z; bBaselineSet = true; break; }
			if (!bBaselineSet) for (TActorIterator<APallet> It(GetWorld()); It; ++It) { AutoLiftPalletStartZ = (*It)->GetActorLocation().Z; break; }
			SetAutoPickupState(EAutoPickupState::Lifting, TEXT("R/F 삽입 깊이 도달 — 물리 접촉 인양 시작"));
		}
		else if (AutoPickupStateElapsed > 10.f || bLastAutoDriveBlocked)
		{
			CancelAutoPickup(FString::Printf(TEXT("R/F 삽입 걸림 — 리치 %.1f/%.1fcm, 좌우 %.1fcm, 높이 %.1fcm"), Travel, AutoRequiredForkAdvanceCm, LastAlignmentLateralErrorCm, LastAlignmentLiftCorrectionCm), true);
		}
		return;
	}
	if (AutoPickupState == EAutoPickupState::Lifting) { AutoLiftInput = AutoLiftSpeedScale; float Rise = 0.f; bLastPhysicalPalletContact = HasPhysicalPalletLift(Rise); LastPhysicalPalletRiseCm = Rise; AutoPickupStatus = bLastPhysicalPalletContact ? TEXT("인양 중 — 파레트 물리 접촉 확인") : TEXT("인양 중 — 파레트 지지 접촉 확인 대기"); if (bLastPhysicalPalletContact) { AutoLiftInput = 0.f; SetAutoPickupState(EAutoPickupState::Complete, FString::Printf(TEXT("파레트 물리 접촉 및 상승 %.1fcm 확인"), Rise)); } else if (LiftCm >= AutoLiftStartCm + AutoTestLiftCm || AutoPickupStateElapsed > 5.f) CancelAutoPickup(TEXT("포크는 상승했으나 파레트 상승을 확인하지 못함"), true); }

	// The RGB-D/reach implementation above is the active state machine.
}
#if 0 // duplicate pre-RGB-D state-machine tail from merge
	AutoPickupStateElapsed += DeltaSeconds;
	const bool bFresh = OpeningDetection.bValid && GetWorld()->GetTimeSeconds() - OpeningDetection.TimestampSeconds <= VisionMaximumFrameAgeSeconds;
	if (AutoPickupState == EAutoPickupState::Detecting) { if (bFresh) ++OpeningStableFrames; else OpeningStableFrames = 0; AutoPickupStatus = bFresh ? FString::Printf(TEXT("삽입구 쌍 확인 — 안정 프레임 %d/3"), OpeningStableFrames) : FString::Printf(TEXT("삽입구 후보 %d개 — 유효한 쌍 없음"), LastVisionCandidateCount); if (OpeningStableFrames >= 3) { LockedOpeningDetection = OpeningDetection; SetAutoPickupState(EAutoPickupState::Aligning, TEXT("안정적인 삽입구 쌍 잠금")); } else if (AutoPickupStateElapsed > 5.f) CancelAutoPickup(TEXT("유효한 두 삽입구를 찾지 못함 — 수동 접근 위치 조정 필요"), true); return; }
	if (AutoPickupState == EAutoPickupState::Aligning)
	{
		if (!bFresh || OpeningDetection.EstimatedDistanceCm < 70.f || OpeningDetection.EstimatedDistanceCm > 260.f) { CancelAutoPickup(TEXT("영상 거리 또는 신뢰도가 범위를 벗어남 — 수동 접근 위치 조정 필요"), true); return; }
		const float LateralError = OpeningDetection.EstimatedLateralCm - SideShiftCm;
		const float Focal = PalletSensorWidthPx / (2.f * FMath::Tan(FMath::DegreesToRadians(PalletSensorFieldOfViewDeg * .5f)));
		const float OpeningCenterY = (OpeningDetection.LeftCenter.Y + OpeningDetection.RightCenter.Y) * .5f;
		// Downscreen means lower in the camera image.  Moving the carriage down
		// (negative lift) moves the opening toward the calibrated target row.
		const float LiftCorrection = -(OpeningCenterY - PalletSensorHeightPx * AutoTargetOpeningCenterYRatio) * OpeningDetection.EstimatedDistanceCm / Focal;
		LastAlignmentLateralErrorCm = LateralError; LastAlignmentLiftCorrectionCm = LiftCorrection;
		if (FMath::Abs(OpeningDetection.EstimatedLateralCm) > Dimensions.ForkSideShiftCm || LiftCm + LiftCorrection < -AutoVerticalToleranceCm || LiftCm + LiftCorrection > RequiredLiftStrokeCm + AutoVerticalToleranceCm) { CancelAutoPickup(TEXT("정렬 범위 초과 — 수동 접근 위치 조정 필요"), true); return; }
		SideShiftInput = FMath::IsNearlyZero(LateralError, 1.2f) ? 0.f : FMath::Sign(LateralError) * .7f;
		LiftInput = FMath::IsNearlyZero(LiftCorrection, AutoVerticalToleranceCm) ? 0.f : FMath::Sign(LiftCorrection) * .15f;
		if (FMath::Abs(LateralError) <= 1.2f && FMath::Abs(LiftCorrection) <= AutoVerticalToleranceCm) ++AlignmentStableFrames; else AlignmentStableFrames = 0;
		AutoPickupStatus = FString::Printf(TEXT("정렬 중 — 좌우 %.1fcm, 높이 %.1fcm"), LateralError, LiftCorrection);
		if (AlignmentStableFrames >= 3)
		{
			SideShiftInput = LiftInput = 0.f; LockedOpeningDetection = OpeningDetection;
			const FVector LeftTip = LeftForkCollision->GetComponentTransform().TransformPosition(FVector(LeftForkCollision->GetScaledBoxExtent().X, 0.f, 0.f));
			const FVector RightTip = RightForkCollision->GetComponentTransform().TransformPosition(FVector(RightForkCollision->GetScaledBoxExtent().X, 0.f, 0.f));
			const float ForkTipAheadOfCameraCm = FVector::DotProduct((LeftTip + RightTip) * .5f - PalletSensorCapture->GetComponentLocation(), PalletSensorCapture->GetForwardVector());
			const float ApproachToPalletFrontCm = LockedOpeningDetection.EstimatedDistanceCm - ForkTipAheadOfCameraCm;
			if (ApproachToPalletFrontCm < 5.f || ApproachToPalletFrontCm > AutoMaximumApproachTravelCm)
			{
				CancelAutoPickup(FString::Printf(TEXT("영상 거리 %.0fcm와 포크 끝 오프셋 %.0fcm가 맞지 않음 — 수동 접근 위치 조정 필요"), LockedOpeningDetection.EstimatedDistanceCm, ForkTipAheadOfCameraCm), true); return;
			}
			AutoRequiredForwardTravelCm = ApproachToPalletFrontCm + AutoRequiredInsertionCm;
			AutoMaximumForwardTravelCm = ApproachToPalletFrontCm + AutoMaximumInsertionCm;
			AutoInsertionStartLocation = GetActorLocation();
			SetAutoPickupState(EAutoPickupState::Inserting, FString::Printf(TEXT("정렬 완료 — 전면까지 %.0fcm, 목표 삽입 깊이 %.0fcm"), ApproachToPalletFrontCm, AutoRequiredInsertionCm));
		}
		if (AutoPickupStateElapsed > 6.f) CancelAutoPickup(TEXT("인양·좌우 정렬이 안정화되지 않음"), true); return;
	}
	if (AutoPickupState == EAutoPickupState::Inserting) { DriveInput = AutoInsertSpeedScale; const float Travel = FVector::Dist(GetActorLocation(), AutoInsertionStartLocation); LastAutoInsertionTravelCm = Travel; bLastAutoDriveBlocked = AutoPickupStateElapsed > 1.f && FMath::Abs(CurrentForwardSpeedCmPerSec) < 2.f; AutoPickupStatus = bLastAutoDriveBlocked ? TEXT("삽입 중 — 전진 명령은 있으나 실제 이동 없음") : FString::Printf(TEXT("삽입 중 — 실제 전진 %.1f / 목표 %.1fcm"), Travel, AutoRequiredForwardTravelCm); if (Travel >= AutoRequiredForwardTravelCm) { DriveInput = 0.f; AutoLiftStartCm = LiftCm; bool bBaselineSet = false; for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It) { AutoLiftPalletStartZ = (*It)->GetActorLocation().Z; bBaselineSet = true; break; } if (!bBaselineSet) for (TActorIterator<APallet> It(GetWorld()); It; ++It) { AutoLiftPalletStartZ = (*It)->GetActorLocation().Z; break; } SetAutoPickupState(EAutoPickupState::Lifting, TEXT("영상 거리+포크 끝 오프셋 기준 삽입 깊이 도달 — 물리 접촉 인양 시작")); } else if (Travel > AutoMaximumForwardTravelCm || AutoPickupStateElapsed > 12.f || bLastAutoDriveBlocked) CancelAutoPickup(TEXT("삽입이 막혔거나 거리/시간 한계 도달"), true); return; }
	if (AutoPickupState == EAutoPickupState::Lifting) { LiftInput = AutoLiftSpeedScale; float Rise = 0.f; bLastPhysicalPalletContact = HasPhysicalPalletLift(Rise); LastPhysicalPalletRiseCm = Rise; AutoPickupStatus = bLastPhysicalPalletContact ? TEXT("인양 중 — 파레트 물리 접촉 확인") : TEXT("인양 중 — 파레트 지지 접촉 확인 대기"); if (bLastPhysicalPalletContact) { LiftInput = 0.f; SetAutoPickupState(EAutoPickupState::Complete, FString::Printf(TEXT("파레트 물리 접촉 및 상승 %.1fcm 확인"), Rise)); } else if (LiftCm >= AutoLiftStartCm + AutoTestLiftCm || AutoPickupStateElapsed > 5.f) CancelAutoPickup(TEXT("포크는 상승했으나 파레트 상승을 확인하지 못함"), true); }

}

#endif
void AAutomatedForklift::LogInitialDriveOverlaps()
{
	TArray<FOverlapResult> Overlaps; FCollisionQueryParams Params(SCENE_QUERY_STAT(ForkliftInitialDriveOverlap), false, this);
	GetWorld()->OverlapMultiByChannel(Overlaps, BodyCollision->GetComponentLocation(), BodyCollision->GetComponentQuat(), BodyCollision->GetCollisionObjectType(), BodyCollision->GetCollisionShape(), Params);
	if (bEnableDriveDebug) { for (const FOverlapResult& Overlap : Overlaps) { UE_LOG(LogTemp, Warning, TEXT("Forklift initial overlap: component=%s actor=%s"), *GetNameSafe(Overlap.GetComponent()), *GetNameSafe(Overlap.GetActor())); } UE_LOG(LogTemp, Log, TEXT("Forklift collision: root=%s bodyCenter=%s bodyExtent=%s groundOrigin=%s overlaps=%d"), *GetNameSafe(GetRootComponent()), *BodyCollision->GetComponentLocation().ToString(), *BodyCollision->GetScaledBoxExtent().ToString(), *GetActorLocation().ToString(), Overlaps.Num()); }
}
void AAutomatedForklift::EmitDriveDiagnostic(const FVector& StartLocation, const FVector& EndLocation, const FHitResult& Hit)
{
	if (!bEnableDriveDebug) return; DriveDebugElapsed += GetWorld()->GetDeltaSeconds(); if (DriveDebugElapsed < DriveDebugIntervalSeconds) return; DriveDebugElapsed = 0.f;
	UE_LOG(LogTemp, Log, TEXT("[R16HD_AUTO_MOTION] source=%s command=%.2f target=%.1fcm/s actual=%.1fcm/s start=%s end=%s delta=%.2f hit=%d startPenetrating=%d targetActor=%s targetComponent=%s"), IsAutoPickupMotionActive() ? TEXT("auto") : TEXT("manual"), LastAppliedDriveInput, TargetForwardSpeedCmPerSec, CurrentForwardSpeedCmPerSec, *StartLocation.ToString(), *EndLocation.ToString(), FVector::Dist(StartLocation, EndLocation), Hit.bBlockingHit, Hit.bStartPenetrating, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
	if (bDrawDriveDebug) { DrawDebugDirectionalArrow(GetWorld(), StartLocation, StartLocation + GetActorForwardVector() * TargetForwardSpeedCmPerSec * .25f, 20.f, Hit.bBlockingHit ? FColor::Red : FColor::Green, false, DriveDebugIntervalSeconds * 1.5f, 0, 2.f); if (Hit.bBlockingHit) DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 18.f, 12, FColor::Red, false, DriveDebugIntervalSeconds * 1.5f); }
}
#if 0 // merged duplicate Tick: replaced below by the single control/capture path
void AAutomatedForklift::Tick(float Dt)
{
	Super::Tick(Dt);

	// Capture/process first, then create automatic commands before the movement
	// integration of this same Tick.  Manual axis callbacks may provide zero
	// every frame; the selected automatic channel remains authoritative only
	// while an automatic state is active.
	if (bPalletSensorCaptureActive && PalletSensorCapture && PalletSensorDepthCapture)
	{
		PalletSensorCaptureElapsed += Dt;
		if (PalletSensorCaptureElapsed >= PalletSensorCaptureIntervalSeconds)
		{
			PalletSensorCaptureElapsed = 0.f;
			// Both captures are issued from the identical carriage pose in this
			// Tick.  ProcessOpeningVision refuses a mismatched frame id/transform.
			++SensorFrameId;
			PalletSensorDepthCapture->SetWorldTransform(PalletSensorCapture->GetComponentTransform(), false, nullptr, ETeleportType::None);
			DepthFrameCameraTransform = PalletSensorCapture->GetComponentTransform();
			DepthFrameTimestampSeconds = GetWorld()->GetTimeSeconds();
			PalletSensorCapture->CaptureScene();
			PalletSensorDepthCapture->CaptureScene();
		}
	}
	ProcessOpeningVision();
	UpdateAutoPickup(Dt);
	const bool bAutoMotion = IsAutoPickupMotionActive();
	LastAppliedDriveInput = bAutoMotion ? AutoDriveInput : DriveInput;
	LastAppliedSteerInput = bAutoMotion ? AutoSteerInput : SteerInput;
	const float Speed = LastAppliedDriveInput >= 0.f ? Motion.ForwardSpeedCmPerSec : Motion.ReverseSpeedCmPerSec;
	TargetForwardSpeedCmPerSec = LastAppliedDriveInput * Speed;

	UpdateNavigation(Dt);
	const float Speed = DriveInput >= 0.f ? Motion.ForwardSpeedCmPerSec : Motion.ReverseSpeedCmPerSec;
	TargetForwardSpeedCmPerSec = DriveInput * Speed;

	const FVector StartLocation = GetActorLocation(); FHitResult DriveHit;
	FVector DriveDelta = GetActorForwardVector() * TargetForwardSpeedCmPerSec * Dt;
	float EarliestPartHit = 1.f;
	for (UBoxComponent* Part : {LeftSupportLegCollision.Get(), RightSupportLegCollision.Get(), LeftMastRailCollision.Get(), RightMastRailCollision.Get(), MastTopCrossbarCollision.Get()})
	{
		FHitResult PartHit;
		if (SweepForkliftPart(Part, DriveDelta, TEXT("drive part sweep"), PartHit)) EarliestPartHit = FMath::Min(EarliestPartHit, PartHit.Time);
	}
	// The chassis root does not cover the raised/extended fork tips. Sweep the
	// real tine shapes against fixed geometry before moving the kinematic body;
	// pallets are intentionally excluded and remain Chaos contact loads.
	for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get()})
	{
		FHitResult TineHit;
		if (SweepForkPartAgainstFixedObstacle(Tine, DriveDelta, TEXT("drive fork fixed-obstacle sweep"), TineHit)) EarliestPartHit = FMath::Min(EarliestPartHit, TineHit.Time);
	}
	FHitResult CarriedLoadHit;
	if (SweepCarriedPallet(DriveDelta, TEXT("carried pallet drive sweep"), CarriedLoadHit)) EarliestPartHit = FMath::Min(EarliestPartHit, CarriedLoadHit.Time);
	if (EarliestPartHit < 1.f) DriveDelta *= FMath::Max(0.f, EarliestPartHit - .01f);

	if (!FMath::IsNearlyZero(LastAppliedDriveInput) && EarliestPartHit < 1.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_FIX_V2] Drive sweep reduced: forklift=%s input=%.2f earliestTime=%.3f resultingDelta=%s"), *GetName(), LastAppliedDriveInput, EarliestPartHit, *DriveDelta.ToString());

	if (!FMath::IsNearlyZero(DriveInput) && EarliestPartHit < 1.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_FIX_V2] Drive sweep reduced: forklift=%s input=%.2f earliestTime=%.3f resultingDelta=%s"), *GetName(), DriveInput, EarliestPartHit, *DriveDelta.ToString());

	}
	AddActorWorldOffset(DriveDelta, true, &DriveHit);
	if (DriveHit.bBlockingHit) RecordCollision(TEXT("body root sweep"), BodyCollision, DriveHit, DriveDelta, true);
	const FVector EndLocation = GetActorLocation();
	CurrentForwardSpeedCmPerSec = Dt > SMALL_NUMBER ? FVector::DotProduct((EndLocation - StartLocation) / Dt, GetActorForwardVector()) : 0.f;

	const bool bIsDriving = !FMath::IsNearlyZero(LastAppliedDriveInput); if (bIsDriving && !bWasDriving) LogInitialDriveOverlaps(); if (bIsDriving) EmitDriveDiagnostic(StartLocation, EndLocation, DriveHit); bWasDriving = bIsDriving;
	const float YawDelta = LastAppliedSteerInput * Motion.SteeringRateDegPerSec * Dt;
	LastAutoYawDeltaDeg = IsAutoPickupMotionActive() ? YawDelta : 0.f;
	if (IsAutoPickupMotionActive()) AutoApproachAccumulatedYawDeg += YawDelta;

	const bool bIsDriving = !FMath::IsNearlyZero(DriveInput); if (bIsDriving && !bWasDriving) LogInitialDriveOverlaps(); if (bIsDriving) EmitDriveDiagnostic(StartLocation, EndLocation, DriveHit); bWasDriving = bIsDriving;
	const float YawDelta = SteerInput * Motion.SteeringRateDegPerSec * Dt;

	if (!WouldForkRotationHitFixedObstacle(YawDelta)) AddActorWorldRotation(FRotator(0.f, YawDelta, 0.f));
	// Fork axes are deterministic kinematic controls. Their prediction sweeps
	// use the real separated tine volumes against fixed geometry. The dynamic
	// pallet is intentionally excluded so physical fork contact can lift it.
	ApplyKinematicForkControls(Dt);

	// One GPU render target is shared by the preview HUD and future vision code.
	// No CPU readback is performed in this implementation.
	if (bPalletSensorCaptureActive && PalletSensorCapture)
	{
		PalletSensorCaptureElapsed += Dt;
		if (PalletSensorCaptureElapsed >= PalletSensorCaptureIntervalSeconds)
		{
			PalletSensorCaptureElapsed = 0.f;
			PalletSensorCapture->CaptureScene();
		}
	}
	ProcessOpeningVision();
	UpdateAutoPickup(Dt);
	DrawCollisionDebug();
	DrawPalletSensorDebug();
}
#endif

void AAutomatedForklift::Tick(float Dt)
{
	Super::Tick(Dt);

	// RGB-D capture is kept separate from the rear-body LiDAR.  Both pallet
	// captures are requested together before their one vision consumer runs.
	if (bPalletSensorCaptureActive && PalletSensorCapture && PalletSensorDepthCapture)
	{
		PalletSensorCaptureElapsed += Dt;
		if (PalletSensorCaptureElapsed >= PalletSensorCaptureIntervalSeconds)
		{
			PalletSensorCaptureElapsed = 0.f;
			++SensorFrameId;
			RgbCaptureId = SensorFrameId;
			RgbFrameCameraTransform = PalletSensorCapture->GetComponentTransform();
			PalletSensorDepthCapture->SetWorldTransform(RgbFrameCameraTransform, false, nullptr, ETeleportType::None);
			DepthCaptureId = SensorFrameId;
			DepthFrameCameraTransform = PalletSensorDepthCapture->GetComponentTransform();
			LastRgbDepthCapturePositionDeltaCm = FVector::Dist(RgbFrameCameraTransform.GetLocation(), DepthFrameCameraTransform.GetLocation());
			LastRgbDepthCaptureRotationDeltaDeg = RgbFrameCameraTransform.GetRotation().AngularDistance(DepthFrameCameraTransform.GetRotation()) * (180.f / PI);
			DepthFrameTimestampSeconds = GetWorld()->GetTimeSeconds();
			PalletSensorCapture->CaptureScene();
			PalletSensorDepthCapture->CaptureScene();
		}
	}
	ProcessOpeningVision();
	UpdateAutoPickup(Dt);
	if (!IsAutoPickupMotionActive()) UpdateNavigation(Dt);
	UpdateSupportedLoadState();

	const bool bAutoMotion = IsAutoPickupMotionActive();
	// The restored controller still writes the original command fields.  Prefer
	// the dedicated automatic channels when present, otherwise consume those
	// legacy automatic values only while the state machine owns motion.
	LastAppliedDriveInput = bAutoMotion ? (!FMath::IsNearlyZero(AutoDriveInput) ? AutoDriveInput : DriveInput) : DriveInput;
	LastAppliedSteerInput = bAutoMotion ? (!FMath::IsNearlyZero(AutoSteerInput) ? AutoSteerInput : SteerInput) : SteerInput;
	LastAppliedLiftInput = bAutoMotion ? (!FMath::IsNearlyZero(AutoLiftInput) ? AutoLiftInput : LiftInput) : LiftInput;
	LastAppliedReachInput = bAutoMotion ? (!FMath::IsNearlyZero(AutoReachInput) ? AutoReachInput : ReachInput) : ReachInput;
	LastAppliedSideShiftInput = bAutoMotion ? (!FMath::IsNearlyZero(AutoSideShiftInput) ? AutoSideShiftInput : SideShiftInput) : SideShiftInput;
	const float Speed = (LastAppliedDriveInput >= 0.f ? Motion.ForwardSpeedCmPerSec : Motion.ReverseSpeedCmPerSec) * CurrentDriveSpeedMultiplier;
	TargetForwardSpeedCmPerSec = LastAppliedDriveInput * Speed;

	const FVector StartLocation = GetActorLocation();
	FHitResult DriveHit;
	FVector DriveDelta = GetActorForwardVector() * TargetForwardSpeedCmPerSec * Dt;
	float EarliestPartHit = 1.f;
	for (UBoxComponent* Part : {LeftSupportLegCollision.Get(), RightSupportLegCollision.Get(), LeftMastRailCollision.Get(), RightMastRailCollision.Get(), MastTopCrossbarCollision.Get()})
	{
		FHitResult PartHit;
		if (SweepForkliftPart(Part, DriveDelta, TEXT("drive part sweep"), PartHit)) EarliestPartHit = FMath::Min(EarliestPartHit, PartHit.Time);
	}
	for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get()})
	{
		FHitResult TineHit;
		if (SweepForkPartAgainstFixedObstacle(Tine, DriveDelta, TEXT("drive fork fixed-obstacle sweep"), TineHit)) EarliestPartHit = FMath::Min(EarliestPartHit, TineHit.Time);
	}
	FHitResult CarriedLoadHit;
	if (SweepCarriedPallet(DriveDelta, TEXT("carried pallet drive sweep"), CarriedLoadHit)) EarliestPartHit = FMath::Min(EarliestPartHit, CarriedLoadHit.Time);
	if (EarliestPartHit < 1.f) DriveDelta *= FMath::Max(0.f, EarliestPartHit - .01f);
	AddActorWorldOffset(DriveDelta, true, &DriveHit);
	if (DriveHit.bBlockingHit) RecordCollision(TEXT("body root sweep"), BodyCollision, DriveHit, DriveDelta, true);
	const FVector EndLocation = GetActorLocation();
	CurrentForwardSpeedCmPerSec = Dt > SMALL_NUMBER ? FVector::DotProduct((EndLocation - StartLocation) / Dt, GetActorForwardVector()) : 0.f;
	const bool bIsDriving = !FMath::IsNearlyZero(LastAppliedDriveInput);
	if (bIsDriving && !bWasDriving) LogInitialDriveOverlaps();
	if (bIsDriving) EmitDriveDiagnostic(StartLocation, EndLocation, DriveHit);
	bWasDriving = bIsDriving;
	const float YawDelta = LastAppliedSteerInput * Motion.SteeringRateDegPerSec * Dt;
	LastAutoYawDeltaDeg = bAutoMotion ? YawDelta : 0.f;
	if (bAutoMotion) AutoApproachAccumulatedYawDeg += YawDelta;
	if (!WouldForkRotationHitFixedObstacle(YawDelta)) AddActorWorldRotation(FRotator(0.f, YawDelta, 0.f));
	ApplyKinematicForkControls(Dt);
	DrawCollisionDebug();
	DrawPalletSensorDebug();
}
