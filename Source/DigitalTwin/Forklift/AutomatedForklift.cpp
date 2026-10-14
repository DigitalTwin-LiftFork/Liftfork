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
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "UObject/ConstructorHelpers.h"

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
	if (!PalletSensorCapture || !PalletSensorRenderTarget) return;
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
	if (PalletSensorRenderTarget->SizeX != PalletSensorWidthPx || PalletSensorRenderTarget->SizeY != PalletSensorHeightPx)
	{
		PalletSensorRenderTarget->ResizeTarget(PalletSensorWidthPx, PalletSensorHeightPx);
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
	VisionDebugViewMode = (VisionDebugViewMode + 1) % 2;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_VISION] O debug view=%d (0=source, 1=binary mask)"), VisionDebugViewMode);
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
	const float LiftRate = LiftInput >= 0.f ? Motion.UnloadedLiftSpeedCmPerSec : Motion.UnloadedLowerSpeedCmPerSec;
	float DesiredLift = FMath::Clamp(LiftCm + LiftInput * LiftRate * Dt, 0.f, RequiredLiftStrokeCm);
	float DesiredReach = FMath::Clamp(ReachCm + ReachInput * Motion.ReachSpeedCmPerSec * Dt, 0.f, Dimensions.ReachTravelCm);
	float DesiredSide = FMath::Clamp(SideShiftCm + SideShiftInput * Motion.SideShiftSpeedCmPerSec * Dt, -Dimensions.ForkSideShiftCm, Dimensions.ForkSideShiftCm);
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
	switch (State) { case EAutoPickupState::Idle: return TEXT("대기"); case EAutoPickupState::Detecting: return TEXT("인식 중"); case EAutoPickupState::Aligning: return TEXT("정렬 중"); case EAutoPickupState::Inserting: return TEXT("삽입 중"); case EAutoPickupState::Lifting: return TEXT("인양 중"); case EAutoPickupState::Complete: return TEXT("완료"); case EAutoPickupState::Stopped: return TEXT("중지"); default: return TEXT("실패"); }
}

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

void AAutomatedForklift::UpdateAutoPickup(float DeltaSeconds)
{
	if (AutoPickupState == EAutoPickupState::Idle || AutoPickupState == EAutoPickupState::Stopped || AutoPickupState == EAutoPickupState::Complete || AutoPickupState == EAutoPickupState::Failed) return;
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

void AAutomatedForklift::LogInitialDriveOverlaps()
{
	TArray<FOverlapResult> Overlaps; FCollisionQueryParams Params(SCENE_QUERY_STAT(ForkliftInitialDriveOverlap), false, this);
	GetWorld()->OverlapMultiByChannel(Overlaps, BodyCollision->GetComponentLocation(), BodyCollision->GetComponentQuat(), BodyCollision->GetCollisionObjectType(), BodyCollision->GetCollisionShape(), Params);
	if (bEnableDriveDebug) { for (const FOverlapResult& Overlap : Overlaps) { UE_LOG(LogTemp, Warning, TEXT("Forklift initial overlap: component=%s actor=%s"), *GetNameSafe(Overlap.GetComponent()), *GetNameSafe(Overlap.GetActor())); } UE_LOG(LogTemp, Log, TEXT("Forklift collision: root=%s bodyCenter=%s bodyExtent=%s groundOrigin=%s overlaps=%d"), *GetNameSafe(GetRootComponent()), *BodyCollision->GetComponentLocation().ToString(), *BodyCollision->GetScaledBoxExtent().ToString(), *GetActorLocation().ToString(), Overlaps.Num()); }
}
void AAutomatedForklift::EmitDriveDiagnostic(const FVector& StartLocation, const FVector& EndLocation, const FHitResult& Hit)
{
	if (!bEnableDriveDebug) return; DriveDebugElapsed += GetWorld()->GetDeltaSeconds(); if (DriveDebugElapsed < DriveDebugIntervalSeconds) return; DriveDebugElapsed = 0.f;
	UE_LOG(LogTemp, Log, TEXT("Forklift drive: input=%.2f target=%.1fcm/s current=%.1fcm/s start=%s end=%s delta=%.2f hit=%d startPenetrating=%d targetActor=%s targetComponent=%s"), DriveInput, TargetForwardSpeedCmPerSec, CurrentForwardSpeedCmPerSec, *StartLocation.ToString(), *EndLocation.ToString(), FVector::Dist(StartLocation, EndLocation), Hit.bBlockingHit, Hit.bStartPenetrating, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
	if (bDrawDriveDebug) { DrawDebugDirectionalArrow(GetWorld(), StartLocation, StartLocation + GetActorForwardVector() * TargetForwardSpeedCmPerSec * .25f, 20.f, Hit.bBlockingHit ? FColor::Red : FColor::Green, false, DriveDebugIntervalSeconds * 1.5f, 0, 2.f); if (Hit.bBlockingHit) DrawDebugSphere(GetWorld(), Hit.ImpactPoint, 18.f, 12, FColor::Red, false, DriveDebugIntervalSeconds * 1.5f); }
}
void AAutomatedForklift::Tick(float Dt)
{
	Super::Tick(Dt);
	const float Speed = DriveInput >= 0.f ? Motion.ForwardSpeedCmPerSec : Motion.ReverseSpeedCmPerSec;
	TargetForwardSpeedCmPerSec = DriveInput * Speed;
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
	if (!FMath::IsNearlyZero(DriveInput) && EarliestPartHit < 1.f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_COLLISION_FIX_V2] Drive sweep reduced: forklift=%s input=%.2f earliestTime=%.3f resultingDelta=%s"), *GetName(), DriveInput, EarliestPartHit, *DriveDelta.ToString());
	}
	AddActorWorldOffset(DriveDelta, true, &DriveHit);
	if (DriveHit.bBlockingHit) RecordCollision(TEXT("body root sweep"), BodyCollision, DriveHit, DriveDelta, true);
	const FVector EndLocation = GetActorLocation();
	CurrentForwardSpeedCmPerSec = Dt > SMALL_NUMBER ? FVector::DotProduct((EndLocation - StartLocation) / Dt, GetActorForwardVector()) : 0.f;
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
