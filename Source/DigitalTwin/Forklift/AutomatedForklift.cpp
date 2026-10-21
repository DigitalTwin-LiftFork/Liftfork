#include "Forklift/AutomatedForklift.h"
#include "Forklift/Pallet.h"
#include "Forklift/WoodPallet.h"
#include "Forklift/ForkliftNavigationSensorComponent.h"
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
	// Rear, above the fixed body deck: actor X is vehicle-forward, so negative
	// X is rear.  This point is outside the 95cm body shell and does not follow
	// the mast, lift, reach, fork or user camera.
	NavigationSensor->SetRelativeLocation(FVector(-72.f, 0.f, 92.f));
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
	// The sensor target is source imagery only. HUD content is drawn after
	// capture, and editor-style debug primitives are excluded here.
	PalletSensorCapture->ShowFlags.SetOnScreenDebug(false);
	PalletSensorCapture->ShowFlags.SetCompositeDebugPrimitives(false);
	PalletSensorRenderTarget = CreateDefaultSubobject<UTextureRenderTarget2D>(TEXT("PalletInsertionSensorRenderTarget"));
	PalletSensorRenderTarget->RenderTargetFormat = RTF_RGBA8;
	PalletSensorRenderTarget->ClearColor = FLinearColor::Black;
	PalletSensorRenderTarget->InitAutoFormat(960, 540);
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
	BodyCollision->SetBoxExtent(FVector(Dimensions.OverallLengthCm * .5f, Dimensions.MaximumWidthCm * .5f, Dimensions.BodyHeightCm * .5f));
	// Query-only representations preserve self-occlusion for line traces without
	// changing the chassis/mast's existing physics collision responses.
	LidarBodyOccluder->SetRelativeLocation(FVector::ZeroVector);
	LidarBodyOccluder->SetBoxExtent(BodyCollision->GetUnscaledBoxExtent());
	LidarMastOccluder->SetRelativeLocation(FVector(0.f, 0.f, Dimensions.MaximumLiftHeightCm * .5f));
	LidarMastOccluder->SetBoxExtent(FVector(12.f, Dimensions.MastWidthCm * .5f, Dimensions.MaximumLiftHeightCm * .5f));
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
	PalletSensorDepthCapture->SetRelativeLocation(CarriageLocalLocation);
	PalletSensorDepthCapture->SetRelativeRotation(PalletSensorRelativeRotation);
	PalletSensorDepthCapture->FOVAngle = PalletSensorFieldOfViewDeg;
	if (PalletSensorRenderTarget->SizeX != PalletSensorWidthPx || PalletSensorRenderTarget->SizeY != PalletSensorHeightPx)
	{
		PalletSensorRenderTarget->ResizeTarget(PalletSensorWidthPx, PalletSensorHeightPx);
	}
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
	Input->BindKey(EKeys::I, IE_Pressed, this, &AAutomatedForklift::ToggleSensorOcclusionDiagnostic);
	Input->BindKey(EKeys::L, IE_Pressed, this, &AAutomatedForklift::ToggleLidarDebug);
}
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
	PalletSensorDisplayMode = (PalletSensorDisplayMode + 1) % 3;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_SENSOR] V display mode=%d (0=off, 1=preview, 2=large) captureActive=%d"), PalletSensorDisplayMode, bPalletSensorCaptureActive);
}
void AAutomatedForklift::TogglePalletSensorCapture()
{
	bPalletSensorCaptureActive = !bPalletSensorCaptureActive;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_SENSOR] N capture active=%d displayMode=%d"), bPalletSensorCaptureActive, PalletSensorDisplayMode);
}
void AAutomatedForklift::ToggleVisionDebugView()
{
	VisionDebugViewMode = (VisionDebugViewMode + 1) % 4;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_VISION] O debug view=%d (0=source, 1=colour region, 2=body, 3=opening)"), VisionDebugViewMode);
}
void AAutomatedForklift::ToggleDepthDebugView()
{
	DepthDebugViewMode = (DepthDebugViewMode + 1) % 3;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_RGBD] U depth debug=%d (0=RGB, 1=normalized depth, 2=RGB+depth edges)"), DepthDebugViewMode);
}
void AAutomatedForklift::ToggleAutoApproachStraightDiagnostic()
{
	bAutoApproachStraightDiagnostic = !bAutoApproachStraightDiagnostic;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_APPROACH] straight-diagnostic=%d (P); approach steering is %s"), bAutoApproachStraightDiagnostic, bAutoApproachStraightDiagnostic ? TEXT("disabled") : TEXT("enabled"));
}
void AAutomatedForklift::ToggleSensorOcclusionDiagnostic()
{
	bSensorOcclusionDiagnosticHideForks = !bSensorOcclusionDiagnosticHideForks;
	if (!PalletSensorCapture) return;
	PalletSensorCapture->ClearHiddenComponents();
	if (bSensorOcclusionDiagnosticHideForks)
	{
		for (UPrimitiveComponent* Part : {static_cast<UPrimitiveComponent*>(LeftForkVisual.Get()), static_cast<UPrimitiveComponent*>(RightForkVisual.Get()), static_cast<UPrimitiveComponent*>(ForkCarriageVisual.Get()), static_cast<UPrimitiveComponent*>(ReachVisual.Get())}) if (Part) PalletSensorCapture->HideComponent(Part);
	}
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_VISION] I camera-only fork/carriage occlusion diagnostic=%d; this does not change the vehicle or final sensor mode."), bSensorOcclusionDiagnosticHideForks);
}
void AAutomatedForklift::ToggleLidarDebug()
{
	if (NavigationSensor) NavigationSensor->ToggleDebugVisible();
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
	for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get(), LeftSupportLegCollision.Get(), RightSupportLegCollision.Get()})
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
	DrawBox(BodyCollision); DrawBox(LiftPhysicsBody); DrawBox(ReachPhysicsBody); DrawBox(ForkPhysicsBody); DrawBox(LeftForkCollision); DrawBox(RightForkCollision); DrawBox(LeftSupportLegCollision); DrawBox(RightSupportLegCollision);
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

void AAutomatedForklift::ApplyKinematicForkControls(float Dt)
{
	const bool bAuto = IsAutoPickupMotionActive();
	LastAppliedLiftInput = bAuto ? AutoLiftInput : LiftInput;
	LastAppliedReachInput = bAuto ? AutoReachInput : ReachInput;
	LastAppliedSideShiftInput = bAuto ? AutoSideShiftInput : SideShiftInput;
	const float LiftRate = LastAppliedLiftInput >= 0.f ? Motion.UnloadedLiftSpeedCmPerSec : Motion.UnloadedLowerSpeedCmPerSec;
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

void AAutomatedForklift::ClearAutomaticCommands()
{
	AutoDriveInput = AutoSteerInput = AutoLiftInput = AutoReachInput = AutoSideShiftInput = 0.f;
}
void AAutomatedForklift::SetAutoPickupState(EAutoPickupState NewState, const FString& Status)
{
	AutoPickupState = NewState; AutoPickupStatus = Status; AutoPickupStateElapsed = 0.f;
	if (NewState == EAutoPickupState::Approaching && GetWorld())
	{
		AutoApproachStartLocation = GetActorLocation();
		AutoApproachStartDistanceCm = OpeningDetection.EstimatedDistanceCm;
		LastAutoApproachTravelCm = 0.f;
		LastAutoCameraDistanceCm = OpeningDetection.EstimatedDistanceCm;
		LastAutoForkTipRemainingCm = OpeningDetection.EstimatedDistanceCm - GetForkTipAheadOfSensorCm();
		AutoApproachRequiredTravelCm = FMath::Max(0.f, LastAutoForkTipRemainingCm - AutoApproachForkTipClearanceCm);
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
	SetAutoPickupState(bFailure ? EAutoPickupState::Failed : EAutoPickupState::Stopped, Reason);
}
void AAutomatedForklift::ToggleAutoPickup()
{
	LastButtonInput = TEXT("시작"); LastButtonClickTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (AutoPickupState == EAutoPickupState::Detecting || AutoPickupState == EAutoPickupState::Approaching || AutoPickupState == EAutoPickupState::Aligning || AutoPickupState == EAutoPickupState::Inserting || AutoPickupState == EAutoPickupState::Lifting) { LastButtonInput = TEXT("중지"); CancelAutoPickup(TEXT("사용자가 작업 중지 버튼을 선택"), false); return; }
	if (!bPalletSensorCaptureActive) { SetAutoPickupState(EAutoPickupState::Failed, TEXT("카메라 캡처가 꺼져 있음 — N 키 또는 설정 확인 필요")); return; }
	if (LiftCm > 3.f || ReachCm > 3.f) { SetAutoPickupState(EAutoPickupState::Failed, TEXT("수동 초기화 필요 — 포크 하강 및 리치 후퇴")); return; }
	ClearAutomaticCommands();
	bSavedLastValidVisionArtifact = bSavedFirstVisionFailureArtifact = false; LastSuccessfulVisionPixels.Reset();
	LastTrackedOpeningDetection = FOpeningDetection(); LastTrackedOpeningTime = -BIG_NUMBER; LastTrackedActorLocation = FVector::ZeroVector; LastTrackedOpeningSeparationPx = 0.f; LockedTargetMissFrames = 0;
	FilteredApproachLateralCm = PreviousApproachLateralCm = AutoApproachHeadingErrorRad = 0.f; LastAutoApproachTravelForHeadingCm = 0.f; LastAutoYawDeltaDeg = AutoApproachAccumulatedYawDeg = 0.f; bApproachSteeringInDeadband = false;
	OpeningStableFrames = AlignmentStableFrames = 0; LastVisionCandidateCount = -1; LastVisionFrameNumber = 0; LastVisionProcessTime = -BIG_NUMBER; bLastVisionReadSucceeded = bLastVisionHadValidPair = false; LastPixelReadStatus = TEXT("카메라 프레임 대기"); LastAlignmentLateralErrorCm = LastAlignmentLiftCorrectionCm = LastAutoInsertionTravelCm = LastAutoApproachTravelCm = LastAutoCameraDistanceCm = LastAutoForkTipRemainingCm = AutoApproachRequiredTravelCm = LastPhysicalPalletRiseCm = AutoRequiredForwardTravelCm = AutoMaximumForwardTravelCm = -BIG_NUMBER; bLastAutoDriveBlocked = bLastAutoApproachBlocked = bLastPhysicalPalletContact = false;
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
	OutLines.Add(FString::Printf(TEXT("Depth: %s / frame %d / 유효픽셀 %d / 범위 %s..%s / %s"), bDepthFrameValid ? TEXT("수신됨") : TEXT("미수신"), DepthFrameId, DepthValidPixelCount, *ValueText(DepthObservedMinimumCm,TEXT("cm")), *ValueText(DepthObservedMaximumCm,TEXT("cm")), *LastDepthReadStatus));
	OutLines.Add(FString::Printf(TEXT("탐색: 16x16 색상 영역 병합 / 원본 2px 정밀 / 후보 ROI 시작 %s"), LastVisionSearchStartY >= 0 ? *FString::FromInt(LastVisionSearchStartY * 2) : TEXT("미측정")));
	OutLines.Add(FString::Printf(TEXT("인식: 후보 %s / 쌍 %s / 신뢰도 %s / 안정 %d/3"), LastVisionCandidateCount >= 0 ? *FString::FromInt(LastVisionCandidateCount) : TEXT("미측정"), bLastVisionHadValidPair ? TEXT("유효") : TEXT("없음"), OpeningDetection.TimestampSeconds > -BIG_NUMBER * .5f ? *FString::Printf(TEXT("%.2f"), OpeningDetection.Confidence) : TEXT("미측정"), OpeningStableFrames));
	OutLines.Add(FString::Printf(TEXT("검출 단계: %s"), *LastVisionStageStatus));
	if (VisionPairDiagnostics.IsEmpty()) OutLines.Add(TEXT("쌍 판정: 미측정 또는 후보 부족"));
	else for (int32 Index = FMath::Max(0, VisionPairDiagnostics.Num()-2); Index < VisionPairDiagnostics.Num(); ++Index) OutLines.Add(VisionPairDiagnostics[Index]);
	OutLines.Add(FString::Printf(TEXT("정렬: 좌우 %s / 높이 %s / 명령 E/Q %.2f Z/C %.2f / 실제 높이 %.1f 좌우 %.1f"), *ValueText(LastAlignmentLateralErrorCm, TEXT("cm")), *ValueText(LastAlignmentLiftCorrectionCm, TEXT("cm")), LiftInput, SideShiftInput, LiftCm, SideShiftCm));
	OutLines.Add(FString::Printf(TEXT("접근: 명령 W/S %.2f 조향 %.2f / 실제속도 %.1fcm/s / 이동 %s / 목표 %s / 차단 %s"), LastAppliedDriveInput, LastAppliedSteerInput, CurrentForwardSpeedCmPerSec, *ValueText(LastAutoApproachTravelCm, TEXT("cm")), *ValueText(AutoApproachRequiredTravelCm, TEXT("cm")), bLastAutoApproachBlocked ? TEXT("예") : TEXT("아니오")));
	OutLines.Add(FString::Printf(TEXT("거리: 카메라 %s / 포크끝 잔여 %s / 접근 목표 %.1fcm"), *ValueText(LastAutoCameraDistanceCm, TEXT("cm")), *ValueText(LastAutoForkTipRemainingCm, TEXT("cm")), AutoApproachForkTipClearanceCm));
	OutLines.Add(FString::Printf(TEXT("RGB-D: frame %d %s / mono %.1fcm / front Z %.1fcm ray %.1fcm / hole contrast %.1fcm yaw %.3frad"), DepthFrameId, OpeningDetection.bDepthValid ? TEXT("valid") : TEXT("invalid"), OpeningDetection.MonocularDistanceCm, OpeningDetection.FrontOpticalDepthCm, OpeningDetection.FrontRayDistanceCm, OpeningDetection.DepthOpeningContrastCm, OpeningDetection.FrontYawErrorRad));
	OutLines.Add(FString::Printf(TEXT("추적: 목표 1 / 재탐색 %d회 / 마지막 유효 %s 전 / 좌우 필터 %.1fcm / 영상 기울기 %.3frad(조향 미사용)"), LockedTargetMissFrames, *AgeText(LastTrackedOpeningTime), FilteredApproachLateralCm, AutoApproachHeadingErrorRad));
	OutLines.Add(FString::Printf(TEXT("조향: P 직진진단 %s / 이번 Tick %.2fdeg / 접근 누적 %.2fdeg"), bAutoApproachStraightDiagnostic ? TEXT("ON") : TEXT("OFF"), LastAutoYawDeltaDeg, AutoApproachAccumulatedYawDeg));
	OutLines.Add(FString::Printf(TEXT("삽입: 명령 W/S %.2f / 이동 %s / 목표 %s / 차단 %s"), LastAppliedDriveInput, *ValueText(LastAutoInsertionTravelCm, TEXT("cm")), *ValueText(AutoRequiredForwardTravelCm, TEXT("cm")), bLastAutoDriveBlocked ? TEXT("예") : TEXT("아니오")));
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
	const int32 MW = VisionMaskWidth, MH = VisionMaskHeight;
	const auto SaveMask = [&SavePpm, MW, MH](const FString& Name, const TArray<uint8>& Mask, const FColor& On)
	{
		if (MW <= 0 || MH <= 0 || Mask.Num() != MW * MH) return;
		TArray<FColor> Image; Image.SetNumUninitialized(Mask.Num());
		for (int32 Index=0; Index<Mask.Num(); ++Index) Image[Index] = Mask[Index] ? On : FColor::Black;
		SavePpm(Name, MW, MH, Image);
	};
	if (bValidFrame)
	{
		SaveMask(TEXT("region"), VisionRegionMask, FColor(60, 150, 255));
		SaveMask(TEXT("body"), VisionBodyMask, FColor(255, 90, 60));
		SaveMask(TEXT("openings"), VisionMask, FColor::White);
	}
	else if (VisionCoarseColumns > 0 && VisionCoarseRows > 0 && VisionCoarseLabels.Num() == VisionCoarseColumns * VisionCoarseRows)
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
	bDepthFrameValid = false; DepthValidPixelCount = 0; DepthObservedMinimumCm = DepthObservedMaximumCm = -BIG_NUMBER;
	if (!PalletSensorDepthRenderTarget || !PalletSensorCapture || !PalletSensorDepthCapture || SensorFrameId <= 0) return false;
	const bool bSamePose = PalletSensorCapture->GetComponentTransform().Equals(DepthFrameCameraTransform, .01f);
	if (!bSamePose) { LastDepthReadStatus = TEXT("synchronization rejected: capture poses differ"); return false; }
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
		if (FMath::IsFinite(Depth) && Depth >= DepthMinimumCm && Depth <= DepthMaximumCm) { ++DepthValidPixelCount; DepthObservedMinimumCm = DepthObservedMinimumCm <= -BIG_NUMBER*.5f ? Depth : FMath::Min(DepthObservedMinimumCm,Depth); DepthObservedMaximumCm = DepthObservedMaximumCm <= -BIG_NUMBER*.5f ? Depth : FMath::Max(DepthObservedMaximumCm,Depth); }
	}
	DepthFrameId = SensorFrameId;
	LastDepthReadTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -BIG_NUMBER;
	bDepthFrameValid = DepthValidPixelCount > Raw.Num() / 100;
	LastDepthReadStatus = FString::Printf(TEXT("depth read %s: %d/%d valid, range %.1f..%.1f cm"), bDepthFrameValid ? TEXT("succeeded") : TEXT("no valid values"), DepthValidPixelCount, Raw.Num(), DepthObservedMinimumCm, DepthObservedMaximumCm);
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
	// A real plane fit needs more non-collinear front samples; depth variation
	// across symmetric opening borders is retained as a confidence diagnostic.
	const float LeftDepth=DepthAt(FMath::RoundToInt(OpeningDetection.LeftCenter.X-OpeningDetection.LeftExtent.X-5),FMath::RoundToInt(OpeningDetection.LeftCenter.Y));
	const float RightDepth=DepthAt(FMath::RoundToInt(OpeningDetection.RightCenter.X+OpeningDetection.RightExtent.X+5),FMath::RoundToInt(OpeningDetection.RightCenter.Y));
	OpeningDetection.FrontYawErrorRad=FMath::Atan2(RightDepth-LeftDepth,FMath::Max(1.f,(OpeningDetection.RightCenter.X-OpeningDetection.LeftCenter.X)*Optical/Focal));
}

void AAutomatedForklift::ProcessOpeningVision()
{
	if (!PalletSensorRenderTarget || !GetWorld()) return;
	if (!(AutoPickupState == EAutoPickupState::Detecting || AutoPickupState == EAutoPickupState::Approaching || AutoPickupState == EAutoPickupState::Aligning || AutoPickupState == EAutoPickupState::Inserting)) return;
	VisionProcessElapsed += GetWorld()->GetDeltaSeconds(); if (VisionProcessElapsed < VisionProcessIntervalSeconds) return; VisionProcessElapsed = 0.f;
	++LastVisionFrameNumber; LastVisionProcessTime = GetWorld()->GetTimeSeconds(); LastVisionCandidateCount = -1; bLastVisionHadValidPair = false; OpeningDetection = FOpeningDetection(); VisionCandidates.Reset(); VisionPairDiagnostics.Reset(); VisionSubdivisionCells.Reset(); VisionSubdivisionDepths.Reset(); LastVisionFailureReason.Reset(); LastVisionStageStatus = TEXT("RGB coarse/merge pending");
	FTextureRenderTargetResource* Resource = PalletSensorRenderTarget->GameThread_GetRenderTargetResource(); TArray<FColor> Pixels;
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != PalletSensorWidthPx * PalletSensorHeightPx) { bLastVisionReadSucceeded = false; LastPixelReadStatus = TEXT("read failed"); LastVisionFailureReason = TEXT("Camera render target pixels unavailable"); return; }
	bLastVisionReadSucceeded = true; LastPixelReadStatus = TEXT("RGB read succeeded");
	// RGB diagnostics deliberately continue without depth.  Automatic motion
	// later requires bDepthValid, but coarse regions/candidates remain visible
	// so a depth transport failure cannot hide the RGB diagnosis.
	const bool bDepthAvailable = ReadSynchronizedDepthFrame();
	const int32 W=PalletSensorWidthPx,H=PalletSensorHeightPx,Step=2,SW=W/Step,SH=H/Step;
	const auto Feature=[](const FColor& C){const float S=FMath::Max(1.f,static_cast<float>(C.R+C.G+C.B));return FVector(C.R/S,C.G/S,C.B/S);};
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
		for (int32 PY=MinPY; PY<MaxPY; PY+=4) for (int32 PX=MinPX; PX<MaxPX; PX+=4) { Tracking.C += Feature(Pixels[PY*W+PX]); Tracking.L += Bright(Pixels[PY*W+PX]); ++Tracking.Count; }
		Tracking.C /= FMath::Max(1, Tracking.Count); Tracking.L /= FMath::Max(1, Tracking.Count); CandidateRegions.Add(Tracking);
		VisionPairDiagnostics.Add(FString::Printf(TEXT("tracked ROI: %dx%d px, miss %d"), MaxPX-MinPX, MaxPY-MinPY, LockedTargetMissFrames));
	}
	float Best=0.f;FVector2D BestLC=FVector2D::ZeroVector,BestRC=FVector2D::ZeroVector,BestLE=FVector2D::ZeroVector,BestRE=FVector2D::ZeroVector;TArray<uint8> BestRegion,BestBody,BestOpen;FIntRect BestBounds;
	for(const FRegion&R:CandidateRegions){if(R.Count<2)continue;FIntRect B(R.B.Min.X*W/CW,R.B.Min.Y*H/CH,R.B.Max.X*W/CW,R.B.Max.Y*H/CH);B.Min.X=FMath::Max(0,B.Min.X-VisionFineSearchPaddingPx);B.Min.Y=FMath::Max(0,B.Min.Y-VisionFineSearchPaddingPx);B.Max.X=FMath::Min(W,B.Max.X+VisionFineSearchPaddingPx);B.Max.Y=FMath::Min(H,B.Max.Y+VisionFineSearchPaddingPx);const int32 LX=B.Min.X/Step,RX=FMath::Min(SW,B.Max.X/Step),TY=B.Min.Y/Step,BY=FMath::Min(SH,B.Max.Y/Step);TArray<uint8> Reg;Reg.SetNumZeroed(SW*SH);for(int32 Y=TY;Y<BY;++Y)for(int32 X=LX;X<RX;++X){const FColor&C=Pixels[(Y*Step)*W+X*Step];if(Similar(Feature(C),Bright(C),R.C,R.L,VisionFineColorDistance,VisionFineBrightnessDistance))Reg[Y*SW+X]=1;}
		// RGB proposes the body.  Depth then removes only pixels that are clearly
		// behind/in front of that surface, preventing similarly coloured floor or
		// rear scene from being merged into the pallet body.
		TArray<float> BodyDepths; if (bDepthAvailable) for(int32 Y=TY;Y<BY;++Y)for(int32 X=LX;X<RX;++X)if(Reg[Y*SW+X]){const float D=LatestDepthCm[(Y*Step)*W+X*Step];if(FMath::IsFinite(D)&&D>=DepthMinimumCm&&D<=DepthMaximumCm)BodyDepths.Add(D);} float BodyDepth=-1.f; if(BodyDepths.Num()>=12){BodyDepths.Sort();BodyDepth=BodyDepths[BodyDepths.Num()/2];for(int32 Y=TY;Y<BY;++Y)for(int32 X=LX;X<RX;++X)if(Reg[Y*SW+X]&&FMath::Abs(LatestDepthCm[(Y*Step)*W+X*Step]-BodyDepth)>VisionDepthBodyContinuityCm)Reg[Y*SW+X]=0;}
		// 2) Actual 2x2 refinement on original pixels, bounded by depth and budget.
		TFunction<void(FIntRect,int32)> Split=[&](FIntRect Cell,int32 Depth){if(VisionSubdivisionCells.Num()>=VisionMaximumFineCells)return;VisionSubdivisionCells.Add(Cell);VisionSubdivisionDepths.Add(Depth);if(Depth>=VisionMaximumSubdivisionDepth||Cell.Width()<=VisionMinimumSubdivisionCellPx||Cell.Height()<=VisionMinimumSubdivisionCellPx)return;FVector Mean=FVector::ZeroVector;float MeanL=0.f;int32 N=0;for(int32 Y=Cell.Min.Y;Y<Cell.Max.Y;Y+=4)for(int32 X=Cell.Min.X;X<Cell.Max.X;X+=4){Mean+=Feature(Pixels[Y*W+X]);MeanL+=Bright(Pixels[Y*W+X]);++N;}Mean/=FMath::Max(1,N);MeanL/=FMath::Max(1,N);float Var=0.f;for(int32 Y=Cell.Min.Y;Y<Cell.Max.Y;Y+=5)for(int32 X=Cell.Min.X;X<Cell.Max.X;X+=5)Var+=FVector::Dist(Feature(Pixels[Y*W+X]),Mean)+FMath::Abs(Bright(Pixels[Y*W+X])-MeanL);if(Var/FMath::Max(1,N/2)<.10f)return;const int32 MX=(Cell.Min.X+Cell.Max.X)/2,MY=(Cell.Min.Y+Cell.Max.Y)/2;Split(FIntRect(Cell.Min.X,Cell.Min.Y,MX,MY),Depth+1);Split(FIntRect(MX,Cell.Min.Y,Cell.Max.X,MY),Depth+1);Split(FIntRect(Cell.Min.X,MY,MX,Cell.Max.Y),Depth+1);Split(FIntRect(MX,MY,Cell.Max.X,Cell.Max.Y),Depth+1);};Split(B,0);
		TArray<uint8> Open;Open.SetNumZeroed(SW*SH);for(int32 Y=TY;Y<BY;++Y)for(int32 X=LX;X<RX;++X)if(!Reg[Y*SW+X]){const FColor&C=Pixels[(Y*Step)*W+X*Step];const bool bRgbContrast=FVector::Dist(Feature(C),R.C)+FMath::Abs(Bright(C)-R.L)>=VisionOpeningMinimumContrast;const float D=bDepthAvailable?LatestDepthCm[(Y*Step)*W+X*Step]:-1.f;const bool bDepthRecess=BodyDepth>0.f&&FMath::IsFinite(D)&&D-BodyDepth>=VisionDepthOpeningContrastCm;Open[Y*SW+X]=(bRgbContrast&&(!bDepthAvailable||bDepthRecess))?1:0;}
		struct FC{FVector2D C,E;int32 A,Contact;bool Edge;};TArray<FC> Cs;TArray<uint8> Work=Open;
		for(int32 Seed=0;Seed<Work.Num();++Seed)if(Work[Seed]){Queue.Reset();Queue.Add(Seed);Work[Seed]=0;int32 MinX=SW,MaxX=0,MinY=SH,MaxY=0;for(int32 I=0;I<Queue.Num();++I){const int32 P=Queue[I],X=P%SW,Y=P/SW;MinX=FMath::Min(MinX,X);MaxX=FMath::Max(MaxX,X);MinY=FMath::Min(MinY,Y);MaxY=FMath::Max(MaxY,Y);for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX){const int32 NX=X+DX,NY=Y+DY;if(NX>=LX&&NX<RX&&NY>=TY&&NY<BY&&Work[NY*SW+NX]){Work[NY*SW+NX]=0;Queue.Add(NY*SW+NX);}}}int32 Contact=0;for(int32 X=MinX;X<=MaxX;++X){Contact+=Reg[FMath::Max(TY,MinY-1)*SW+X];Contact+=Reg[FMath::Min(BY-1,MaxY+1)*SW+X];}for(int32 Y=MinY;Y<=MaxY;++Y){Contact+=Reg[Y*SW+FMath::Max(LX,MinX-1)];Contact+=Reg[Y*SW+FMath::Min(RX-1,MaxX+1)];}const float BW=(MaxX-MinX+1)*Step,BH=(MaxY-MinY+1)*Step;const bool Edge=MinX<=0||MaxX>=SW-1||MinY<=0||MaxY>=SH-1;if(Queue.Num()>=12&&BW>=12&&BH>=8&&Contact>=2&&!Edge)Cs.Add({FVector2D((MinX+MaxX+1)*Step*.5f,(MinY+MaxY+1)*Step*.5f),FVector2D(BW*.5f,BH*.5f),Queue.Num(),Contact,Edge});}
		for(int32 A=0;A<Cs.Num();++A)for(int32 D=A+1;D<Cs.Num();++D){FC L=Cs[A],RR=Cs[D];if(L.C.X>RR.C.X)Swap(L,RR);const float Sep=RR.C.X-L.C.X,Width=L.E.X+RR.E.X,HD=FMath::Abs(L.C.Y-RR.C.Y),SD=FMath::Abs(L.E.X-RR.E.X)/FMath::Max(1.f,FMath::Max(L.E.X,RR.E.X)),Ratio=Sep/FMath::Max(1.f,Width),Expected=VisionKnownOpeningCenterSpacingCm/FMath::Max(1.f,VisionKnownOpeningWidthCm),MaxHD=FMath::Max(12.f,(L.E.Y+RR.E.Y)*.75f);const bool Good=SD<=.55f&&HD<=MaxHD&&Ratio>=.35f&&Ratio<=4.5f;const float Score=FMath::Clamp(((1.f-SD)+(1.f-HD/MaxHD)+(1.f-FMath::Abs(Ratio-Expected)/Expected)+FMath::Min(1.f,(L.Contact+RR.Contact)/12.f))*.25f,0.f,1.f);VisionPairDiagnostics.Add(FString::Printf(TEXT("region %d: pair contact %d/%d score %.2f%s"),R.Label,L.Contact,RR.Contact,Score,Good?TEXT(""):TEXT(" rejected")));if(Good&&Score>=MinimumOpeningConfidence&&Score>Best){Best=Score;BestLC=L.C;BestRC=RR.C;BestLE=L.E;BestRE=RR.E;BestRegion=Reg;BestBody=Reg;BestOpen=Open;BestBounds=FIntRect(LX,TY,RX,BY);}}
	}
	if(Best<MinimumOpeningConfidence)
	{
		++LockedTargetMissFrames;
		LastVisionStageStatus=FString::Printf(TEXT("fine regions=%d; opening components/pairs=%d; depth=%s"), CandidateRegions.Num(), VisionPairDiagnostics.Num(), bDepthAvailable?TEXT("available"):TEXT("unavailable"));
		LastVisionFailureReason=FString::Printf(TEXT("No verified opening pair (%s; tracked target miss %d; full/coarse search retained)"), *LastVisionStageStatus, LockedTargetMissFrames);
	if (bSavedLastValidVisionArtifact && !bSavedFirstVisionFailureArtifact) { SaveVisionTransitionArtifacts(LastSuccessfulVisionPixels, true); SaveVisionTransitionArtifacts(Pixels, false); bSavedFirstVisionFailureArtifact = true; }
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_VISION] target=locked frame=%d result=failed misses=%d lastAge=%.2fs reason=%s"), LastVisionFrameNumber, LockedTargetMissFrames, LastTrackedOpeningTime > -BIG_NUMBER*.5f ? GetWorld()->GetTimeSeconds()-LastTrackedOpeningTime : -1.f, *LastVisionFailureReason);
		return;
	}
	VisionRegionMask=MoveTemp(BestRegion);VisionBodyMask=MoveTemp(BestBody);VisionMask=MoveTemp(BestOpen);VisionMaskWidth=SW;VisionMaskHeight=SH;VisionBodyBounds=BestBounds;LastVisionCandidateCount=2;VisionCandidates.Add({BestLC,BestLE,0,true,false,TEXT("selected opening")});VisionCandidates.Add({BestRC,BestRE,0,true,false,TEXT("selected opening")});
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
	LastVisionStageStatus=FString::Printf(TEXT("RGB body/opening/pair accepted; depth %s"), OpeningDetection.bDepthValid?TEXT("front verified"):TEXT("not usable for control")); LastTrackedOpeningDetection=OpeningDetection; LastTrackedOpeningTime=OpeningDetection.TimestampSeconds; LastTrackedActorLocation=GetActorLocation(); LastTrackedOpeningSeparationPx=Sep; LockedTargetMissFrames=0;
	LastSuccessfulVisionPixels = Pixels; bSavedLastValidVisionArtifact = true; bSavedFirstVisionFailureArtifact = false;
	UE_LOG(LogTemp, Log, TEXT("[R16HD_AUTO_VISION] target=locked frame=%d pair=(%.0f,%.0f)-(%.0f,%.0f) dist=%.1f lateral=%.1f confidence=%.2f"), LastVisionFrameNumber, BestLC.X, BestLC.Y, BestRC.X, BestRC.Y, OpeningDetection.EstimatedDistanceCm, OpeningDetection.EstimatedLateralCm, Best);
}

bool AAutomatedForklift::HasPhysicalPalletLift(float& OutPalletRiseCm) const
{
	OutPalletRiseCm = 0.f;
	for (TActorIterator<APallet> It(GetWorld()); It; ++It) { FHitResult Hit; UPrimitiveComponent* Other = nullptr; if ((*It)->GetRecentPhysicsContact(.75f, Hit, Other) && (Other == LeftForkCollision || Other == RightForkCollision)) { OutPalletRiseCm = (*It)->GetActorLocation().Z - AutoLiftPalletStartZ; return OutPalletRiseCm > 2.f; } }
	for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It) { UPrimitiveComponent* Other = nullptr; if ((*It)->GetRecentForkContact(.75f, Other) && (Other == LeftForkCollision || Other == RightForkCollision)) { OutPalletRiseCm = (*It)->GetActorLocation().Z - AutoLiftPalletStartZ; return OutPalletRiseCm > 2.f; } }
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
	if (AutoPickupState == EAutoPickupState::Detecting) { if (bFresh) ++OpeningStableFrames; else OpeningStableFrames = 0; AutoPickupStatus = !OpeningDetection.bValid ? FString::Printf(TEXT("RGB 삽입구 후보 %d개 — 유효한 쌍 없음"), LastVisionCandidateCount) : (!OpeningDetection.bDepthValid ? FString::Printf(TEXT("RGB 쌍은 확인됨 — Depth 대기/실패: %s"), *LastDepthReadStatus) : FString::Printf(TEXT("삽입구 쌍 확인 — 안정 프레임 %d/3"), OpeningStableFrames)); if (OpeningStableFrames >= 3) { LockedOpeningDetection = OpeningDetection; SetAutoPickupState(EAutoPickupState::Approaching, TEXT("안정적인 RGB-D 삽입구 쌍 잠금 — 차체 접근 및 방향 정렬 시작")); } else if (AutoPickupStateElapsed > 5.f) CancelAutoPickup(OpeningDetection.bValid && !OpeningDetection.bDepthValid ? TEXT("RGB 쌍은 있으나 유효한 SceneDepth가 없음 — 자동 제어 보류") : TEXT("유효한 두 삽입구를 찾지 못함 — 수동 접근 위치 조정 필요"), true); return; }
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
		const float ForkTipAhead = GetForkTipAheadOfSensorCm();
		const float ForkTipRemaining = OpeningDetection.EstimatedDistanceCm - ForkTipAhead;
		LastAutoCameraDistanceCm = OpeningDetection.EstimatedDistanceCm;
		LastAutoForkTipRemainingCm = ForkTipRemaining;
		LastAutoApproachTravelCm = FVector::Dist(GetActorLocation(), AutoApproachStartLocation);
		const bool bAtApproachDistance = ForkTipRemaining <= AutoApproachForkTipClearanceCm;
		const bool bWithinForkRange = FMath::Abs(FilteredApproachLateralCm) <= Dimensions.ForkSideShiftCm + AutoApproachLateralToleranceCm;
		if (bAtApproachDistance && bWithinForkRange) { SetAutoPickupState(EAutoPickupState::Aligning, TEXT("차체 접근 완료 — 포크 높이·좌우 정렬 시작")); return; }
		// The travel budget is derived from this detection, not a fixed 260 cm
		// limit.  A pallet initially visible at 339 cm legitimately needs more
		// than 260 cm of chassis travel before the fork tips reach its front.
		const float ApproachTravelLimit = AutoApproachRequiredTravelCm + 35.f;
		if (ForkTipRemaining < -10.f || LastAutoApproachTravelCm > ApproachTravelLimit) { CancelAutoPickup(TEXT("접근 거리 범위를 벗어남"), true); return; }
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
		const float NearScale = FMath::Clamp(ForkTipRemaining / 90.f, .35f, 1.f);
		AutoDriveInput = AutoApproachSpeedScale * NearScale;
		const float Now = GetWorld()->GetTimeSeconds();
		if (OpeningDetection.EstimatedDistanceCm < AutoApproachLastProgressDistanceCm - 4.f) { AutoApproachLastProgressDistanceCm = OpeningDetection.EstimatedDistanceCm; AutoApproachLastProgressTime = Now; }
		bLastAutoApproachBlocked = AutoPickupStateElapsed > 1.0f && LastAutoApproachTravelCm < 2.f;
		AutoPickupStatus = FString::Printf(TEXT("접근 중 — 카메라 %.0fcm, 포크끝 잔여 %.0fcm, 실제 이동 %.1f/%.1fcm, 조향 %.2f"), OpeningDetection.EstimatedDistanceCm, ForkTipRemaining, LastAutoApproachTravelCm, AutoApproachRequiredTravelCm, AutoSteerInput);
		UE_LOG(LogTemp, Log, TEXT("[R16HD_AUTO_APPROACH] rawLat=%.2f filteredLat=%.2f heading=%.3frad lateralCmd=%.3f headingCmd=%.3f steer=%.3f yaw=%.3f travel=%.1f dist=%.1f"), RawLateral, FilteredApproachLateralCm, AutoApproachHeadingErrorRad, LateralSteer, HeadingSteer, AutoSteerInput, LastAutoYawDeltaDeg, LastAutoApproachTravelCm, OpeningDetection.EstimatedDistanceCm);
		if (bLastAutoApproachBlocked) CancelAutoPickup(TEXT("접근 명령은 적용됐으나 차체가 이동하지 않음 — Sweep/충돌 로그 확인 필요"), true);
		else if (LastAutoApproachTravelCm > 35.f && Now - AutoApproachLastProgressTime > 2.5f) CancelAutoPickup(TEXT("차체는 이동했지만 영상 거리 갱신이 없음 — 같은 목표 재인식 필요"), true);
		else { const float ExpectedSeconds = FMath::Max(3.f, AutoApproachRequiredTravelCm / FMath::Max(1.f, Motion.ForwardSpeedCmPerSec * AutoApproachSpeedScale) + 2.f); if (AutoPickupStateElapsed > ExpectedSeconds) CancelAutoPickup(TEXT("접근 시간 예산 초과 — 실제 이동/영상 거리 진단 확인 필요"), true); }
		return;
	}
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
		if (FMath::Abs(OpeningDetection.EstimatedLateralCm) > Dimensions.ForkSideShiftCm) { SetAutoPickupState(EAutoPickupState::Approaching, TEXT("포크 좌우 행정을 초과 — 차체 접근·방향 정렬로 복귀")); return; }
		if (LiftCm + LiftCorrection < -AutoVerticalToleranceCm || LiftCm + LiftCorrection > RequiredLiftStrokeCm + AutoVerticalToleranceCm) { CancelAutoPickup(TEXT("포크 인양 범위를 벗어남 — 수동 높이 조정 필요"), true); return; }
		AutoSideShiftInput = FMath::IsNearlyZero(LateralError, 1.2f) ? 0.f : FMath::Sign(LateralError) * .7f;
		AutoLiftInput = FMath::IsNearlyZero(LiftCorrection, AutoVerticalToleranceCm) ? 0.f : FMath::Sign(LiftCorrection) * .15f;
		if (FMath::Abs(LateralError) <= 1.2f && FMath::Abs(LiftCorrection) <= AutoVerticalToleranceCm) ++AlignmentStableFrames; else AlignmentStableFrames = 0;
		AutoPickupStatus = FString::Printf(TEXT("정렬 중 — 좌우 %.1fcm, 높이 %.1fcm"), LateralError, LiftCorrection);
		if (AlignmentStableFrames >= 3)
		{
			AutoSideShiftInput = AutoLiftInput = 0.f; LockedOpeningDetection = OpeningDetection;
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
	if (AutoPickupState == EAutoPickupState::Inserting) { AutoDriveInput = AutoInsertSpeedScale; const float Travel = FVector::Dist(GetActorLocation(), AutoInsertionStartLocation); LastAutoInsertionTravelCm = Travel; bLastAutoDriveBlocked = AutoPickupStateElapsed > 1.f && FMath::Abs(CurrentForwardSpeedCmPerSec) < 2.f; AutoPickupStatus = bLastAutoDriveBlocked ? TEXT("삽입 중 — 전진 명령은 있으나 실제 이동 없음") : FString::Printf(TEXT("삽입 중 — 실제 전진 %.1f / 목표 %.1fcm"), Travel, AutoRequiredForwardTravelCm); if (Travel >= AutoRequiredForwardTravelCm) { AutoLiftStartCm = LiftCm; bool bBaselineSet = false; for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It) { AutoLiftPalletStartZ = (*It)->GetActorLocation().Z; bBaselineSet = true; break; } if (!bBaselineSet) for (TActorIterator<APallet> It(GetWorld()); It; ++It) { AutoLiftPalletStartZ = (*It)->GetActorLocation().Z; break; } SetAutoPickupState(EAutoPickupState::Lifting, TEXT("영상 거리+포크 끝 오프셋 기준 삽입 깊이 도달 — 물리 접촉 인양 시작")); } else if (Travel > AutoMaximumForwardTravelCm || AutoPickupStateElapsed > 12.f || bLastAutoDriveBlocked) CancelAutoPickup(TEXT("삽입이 막혔거나 거리/시간 한계 도달"), true); return; }
	if (AutoPickupState == EAutoPickupState::Lifting) { AutoLiftInput = AutoLiftSpeedScale; float Rise = 0.f; bLastPhysicalPalletContact = HasPhysicalPalletLift(Rise); LastPhysicalPalletRiseCm = Rise; AutoPickupStatus = bLastPhysicalPalletContact ? TEXT("인양 중 — 파레트 물리 접촉 확인") : TEXT("인양 중 — 파레트 지지 접촉 확인 대기"); if (bLastPhysicalPalletContact) { AutoLiftInput = 0.f; SetAutoPickupState(EAutoPickupState::Complete, FString::Printf(TEXT("파레트 물리 접촉 및 상승 %.1fcm 확인"), Rise)); } else if (LiftCm >= AutoLiftStartCm + AutoTestLiftCm || AutoPickupStateElapsed > 5.f) CancelAutoPickup(TEXT("포크는 상승했으나 파레트 상승을 확인하지 못함"), true); }
}

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
	const FVector StartLocation = GetActorLocation(); FHitResult DriveHit;
	FVector DriveDelta = GetActorForwardVector() * TargetForwardSpeedCmPerSec * Dt;
	float EarliestPartHit = 1.f;
	for (UBoxComponent* Part : {LeftSupportLegCollision.Get(), RightSupportLegCollision.Get()})
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
	}
	AddActorWorldOffset(DriveDelta, true, &DriveHit);
	if (DriveHit.bBlockingHit) RecordCollision(TEXT("body root sweep"), BodyCollision, DriveHit, DriveDelta, true);
	const FVector EndLocation = GetActorLocation();
	CurrentForwardSpeedCmPerSec = Dt > SMALL_NUMBER ? FVector::DotProduct((EndLocation - StartLocation) / Dt, GetActorForwardVector()) : 0.f;
	const bool bIsDriving = !FMath::IsNearlyZero(LastAppliedDriveInput); if (bIsDriving && !bWasDriving) LogInitialDriveOverlaps(); if (bIsDriving) EmitDriveDiagnostic(StartLocation, EndLocation, DriveHit); bWasDriving = bIsDriving;
	const float YawDelta = LastAppliedSteerInput * Motion.SteeringRateDegPerSec * Dt;
	LastAutoYawDeltaDeg = IsAutoPickupMotionActive() ? YawDelta : 0.f;
	if (IsAutoPickupMotionActive()) AutoApproachAccumulatedYawDeg += YawDelta;
	if (!WouldForkRotationHitFixedObstacle(YawDelta)) AddActorWorldRotation(FRotator(0.f, YawDelta, 0.f));
	// Fork axes are deterministic kinematic controls. Their prediction sweeps
	// use the real separated tine volumes against fixed geometry. The dynamic
	// pallet is intentionally excluded so physical fork contact can lift it.
	ApplyKinematicForkControls(Dt);
	DrawCollisionDebug();
	DrawPalletSensorDebug();
}
