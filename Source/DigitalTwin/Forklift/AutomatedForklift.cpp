#include "Forklift/AutomatedForklift.h"
#include "Forklift/Pallet.h"
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
	ConfigureChaosPhysics();
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
	// The original lowest fork datum is MastAssembly, not the body origin or
	// a physics-body centre. Preserve it exactly for LiftCm=0.
	LiftPhysicsBody->SetRelativeLocation(FVector(Dimensions.OverallLengthCm * .5f - 10.f, 0.f, -Dimensions.BodyHeightCm * .5f)); LiftPhysicsBody->SetBoxExtent(FVector(35.f, Dimensions.MastWidthCm * .5f, 80.f));
	ReachPhysicsBody->SetRelativeLocation(FVector::ZeroVector); ReachPhysicsBody->SetBoxExtent(FVector(35.f, Dimensions.MastWidthCm * .5f, 35.f));
	ForkPhysicsBody->SetRelativeLocation(FVector::ZeroVector); ForkPhysicsBody->SetBoxExtent(FVector(75.f, 45.f, 12.f));
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

void AAutomatedForklift::ConfigureChaosPhysics()
{
	if (bChaosConfigured) return;
	// Hybrid mode: the chassis remains the proven kinematic Sweep driver.
	// Constraints are anchored to it, while only the lift/reach/fork chain is dynamic.
	BodyCollision->SetCollisionProfileName(TEXT("Pawn"));
	BodyCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	BodyCollision->SetSimulatePhysics(false);
	// A simulated child must not remain parented to another simulated body. In
	// that case Chaos may weld it, leaving a valid-looking Constraint whose
	// child cannot translate (the previous reach symptom). Keep every initial
	// world pose, then let only the explicit constraints define the chain.
	for (UBoxComponent* Body : {LiftPhysicsBody.Get(), ReachPhysicsBody.Get(), ForkPhysicsBody.Get()})
	{
		Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}
	for (UBoxComponent* Body : {LiftPhysicsBody.Get(), ReachPhysicsBody.Get(), ForkPhysicsBody.Get()})
	{
		// These are drive masses/constraint endpoints, not the forklift's
		// external collision geometry. Their former large boxes touched the floor
		// and pulled the entire chain down before an input was applied.
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Body->SetUseCCD(true); Body->SetLinearDamping(2.f); Body->SetAngularDamping(5.f); Body->SetSimulatePhysics(true);
	}
	// Unlike the three invisible drive masses above, these are the actual fork
	// contact bodies.  They must simulate and block WorldStatic/PhysicsBody so
	// the floor and a physical pallet can push back on the constraint chain.
	for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get()})
	{
		Tine->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		Tine->SetCollisionProfileName(TEXT("BlockAll"));
		Tine->SetCollisionObjectType(ECC_PhysicsBody);
		Tine->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Tine->SetUseCCD(true);
		Tine->SetLinearDamping(1.5f);
		Tine->SetAngularDamping(4.f);
		Tine->SetMassOverrideInKg(NAME_None, 18.f, true);
		Tine->SetSimulatePhysics(true);
	}
	// Simulating a primitive detaches it from its former parent. Reattach the
	// visual/collision descendants after simulation is enabled so the actual
	// forks follow ForkPhysicsBody rather than their stale scene transform.
	LiftCarriage->AttachToComponent(LiftPhysicsBody, FAttachmentTransformRules::KeepRelativeTransform);
	ReachCarriage->AttachToComponent(ReachPhysicsBody, FAttachmentTransformRules::KeepRelativeTransform);
	ForkCarriage->AttachToComponent(ForkPhysicsBody, FAttachmentTransformRules::KeepRelativeTransform);
	auto Configure = [this](UPhysicsConstraintComponent* C, UPrimitiveComponent* A, UPrimitiveComponent* B, ELinearConstraintMotion Axis, float Limit)
	{
		C->SetConstrainedComponents(A, NAME_None, B, NAME_None);
		C->SetDisableCollision(true);
		C->SetLinearXLimit(Axis == ELinearConstraintMotion::LCM_Limited ? ELinearConstraintMotion::LCM_Limited : ELinearConstraintMotion::LCM_Locked, Limit);
		C->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f); C->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		C->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.f); C->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.f); C->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.f);
		C->SetLinearPositionDrive(true, true, true); C->SetLinearDriveParams(ConstraintStiffness, ConstraintDamping, ConstraintForceLimit);
	};
	// Chaos linear limits are symmetric. Centre every one-sided user stroke in
	// that interval: lift [0,500] becomes native [-250,+250] and reach
	// [0,39.3] becomes native [-19.65,+19.65].  This gives the lower stop a
	// genuine physical limit instead of relying on a clamped target alone.
	ConfigureLiftHeightReference();
	LiftConstraintCentreCm = RequiredLiftStrokeCm * .5f;
	ReachConstraintCentreCm = Dimensions.ReachTravelCm * .5f;
	Configure(LiftConstraint, BodyCollision, LiftPhysicsBody, ELinearConstraintMotion::LCM_Limited, LiftConstraintCentreCm);
	LiftConstraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.f); LiftConstraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f); LiftConstraint->SetLinearZLimit(ELinearConstraintMotion::LCM_Limited, LiftConstraintCentreCm);
	Configure(ReachConstraint, LiftPhysicsBody, ReachPhysicsBody, ELinearConstraintMotion::LCM_Limited, ReachConstraintCentreCm);
	Configure(SideShiftConstraint, ReachPhysicsBody, ForkPhysicsBody, ELinearConstraintMotion::LCM_Limited, Dimensions.ForkSideShiftCm);
	SideShiftConstraint->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.f); SideShiftConstraint->SetLinearYLimit(ELinearConstraintMotion::LCM_Limited, Dimensions.ForkSideShiftCm);
	// Drive only the permitted translational axis. Driving locked axes as well
	// was harmless for lift but let the reach solver fight its own locked
	// degrees of freedom after its bodies were made independent.
	LiftConstraint->SetLinearPositionDrive(false, false, true);
	LiftConstraint->SetLinearVelocityDrive(false, false, false);
	ReachConstraint->SetLinearPositionDrive(true, false, false);
	ReachConstraint->SetLinearVelocityDrive(true, false, false);
	SideShiftConstraint->SetLinearPositionDrive(false, true, false);
	SideShiftConstraint->SetLinearVelocityDrive(false, false, false);
	// Reference frames are deliberately set after SetConstrainedComponents.
	// The old code let SetWorldLocation create implicit frames and assumed their
	// signs.  Frame2 offsets centre the allowed physical stroke as described
	// above, while both frames retain the truck's local orientation.
	SetConstraintFramesAtPose(LiftConstraint, BodyCollision, LiftPhysicsBody, LiftPhysicsBody->GetComponentTransform(), FVector(0.f, 0.f, -LiftConstraintCentreCm));
	SetConstraintFramesAtPose(ReachConstraint, LiftPhysicsBody, ReachPhysicsBody, ReachPhysicsBody->GetComponentTransform(), FVector(-ReachConstraintCentreCm, 0.f, 0.f));
	SetConstraintFramesAtPose(SideShiftConstraint, ReachPhysicsBody, ForkPhysicsBody, ForkPhysicsBody->GetComponentTransform(), FVector::ZeroVector);
	// Both tines are real dynamic contact bodies, rigidly coupled to the fork
	// carrier.  Visual meshes remain on the existing carriage hierarchy.
	auto ConfigureFixedTine = [this](UPhysicsConstraintComponent* C, UBoxComponent* Tine)
	{
		C->SetConstrainedComponents(ForkPhysicsBody, NAME_None, Tine, NAME_None);
		C->SetDisableCollision(true);
		C->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		C->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		C->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
		C->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.f);
		C->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.f);
		C->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.f);
		SetConstraintFramesAtPose(C, ForkPhysicsBody, Tine, Tine->GetComponentTransform(), FVector::ZeroVector);
	};
	ConfigureFixedTine(LeftForkFixedConstraint, LeftForkCollision);
	ConfigureFixedTine(RightForkFixedConstraint, RightForkCollision);
	LiftNativeDriveTargetCm = LiftConstraintCentreCm;
	ReachNativeDriveTargetCm = ReachConstraintCentreCm;
	LiftConstraint->SetLinearPositionTarget(FVector(0.f, 0.f, LiftNativeDriveTargetCm));
	ReachConstraint->SetLinearPositionTarget(FVector(ReachNativeDriveTargetCm, 0.f, 0.f));
	SideShiftConstraint->SetLinearPositionTarget(FVector::ZeroVector);
	ReachRestRelativeXcm = LiftPhysicsBody->GetComponentTransform().InverseTransformPosition(ReachPhysicsBody->GetComponentLocation()).X;
	const FTransform ReachFrame1 = ReachConstraint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1);
	const FTransform ReachFrame2 = ReachConstraint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2);
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_CHAOS_FRAME_V4] lift user=[0,%.1f] nativeTarget=[%.1f,%.1f] reach user=[0,%.1f] nativeTarget=[%.2f,%.2f] reachBodies=(%s,%s) reachLocalFrames=(F1=%s F2=%s) reachLimits=(X=%d Y=%d Z=%d) drive=%d"),
		RequiredLiftStrokeCm, LiftConstraintCentreCm, -LiftConstraintCentreCm, Dimensions.ReachTravelCm, ReachConstraintCentreCm, -ReachConstraintCentreCm,
		*GetNameSafe(LiftPhysicsBody), *GetNameSafe(ReachPhysicsBody), *ReachFrame1.ToString(), *ReachFrame2.ToString(),
		static_cast<int32>(ReachConstraint->ConstraintInstance.GetLinearXMotion()), static_cast<int32>(ReachConstraint->ConstraintInstance.GetLinearYMotion()), static_cast<int32>(ReachConstraint->ConstraintInstance.GetLinearZMotion()), ReachConstraint->ConstraintInstance.IsLinearPositionDriveXEnabled());
	bChaosConfigured = true;
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
		BackrestPlaneLocalXcm + PalletSensorForwardOfBackrestCm,
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
void AAutomatedForklift::Drive(float V) { DriveInput = V; LogInputInvocation(TEXT("ForkliftMoveForward(W/S)"), V); } void AAutomatedForklift::Steer(float V) { SteerInput = V; }
void AAutomatedForklift::Lift(float V) { LiftInput = V; if (!FMath::IsNearlyZero(V) && LiftPhysicsBody) LiftPhysicsBody->WakeAllRigidBodies(); LogInputInvocation(TEXT("ForkliftLift(E/Q)"), V); }
void AAutomatedForklift::Reach(float V)
{
	ReachInput = V;
	if (!FMath::IsNearlyZero(V))
	{
		// A constrained chain can remain asleep even when only its middle body
		// receives a new target. Wake the complete reach load path; this does
		// not alter transforms or collision responses.
		for (UBoxComponent* Body : {LiftPhysicsBody.Get(), ReachPhysicsBody.Get(), ForkPhysicsBody.Get(), LeftForkCollision.Get(), RightForkCollision.Get()})
		{
			if (Body) Body->WakeAllRigidBodies();
		}
	}
	LogInputInvocation(TEXT("ForkliftReach(R/F)"), V);
}
void AAutomatedForklift::SideShift(float V) { SideShiftInput = V; if (!FMath::IsNearlyZero(V) && ForkPhysicsBody) ForkPhysicsBody->WakeAllRigidBodies(); LogInputInvocation(TEXT("ForkliftSideShift(Z/C)"), V); }
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

APallet* AAutomatedForklift::FindPalletForAutomaticSupport(float LiftDeltaCm, FString& OutReason) const
{
	OutReason = TEXT("no pallet has both forks sufficiently inserted");
	if (LiftDeltaCm <= KINDA_SMALL_NUMBER || !GetWorld()) return nullptr;
	for (TActorIterator<APallet> It(GetWorld()); It; ++It)
	{
		APallet* Pallet = *It;
		if (!Pallet || Pallet == CarriedPallet) continue;
		FString Reason;
		if (!Pallet->CanSupportFromForks(LeftForkCollision, RightForkCollision, MinimumPalletSupportInsertionCm, Reason))
		{
			OutReason = Reason;
			continue;
		}
		const float ForkTopZ = FMath::Min(LeftForkCollision->GetComponentLocation().Z + LeftForkCollision->GetScaledBoxExtent().Z,
			RightForkCollision->GetComponentLocation().Z + RightForkCollision->GetScaledBoxExtent().Z);
		const float SupportSurfaceZ = Pallet->GetForkSupportSurfaceWorldZ();
		if (ForkTopZ <= SupportSurfaceZ + PalletSupportContactToleranceCm && ForkTopZ + LiftDeltaCm >= SupportSurfaceZ - PalletSupportContactToleranceCm)
		{
			OutReason = TEXT("both forks inserted and upper-deck underside reached");
			return Pallet;
		}
		OutReason = FString::Printf(TEXT("fork top Z=%.1f has not reached support underside Z=%.1f"), ForkTopZ, SupportSurfaceZ);
	}
	return nullptr;
}

void AAutomatedForklift::BeginAutomaticPalletSupport(APallet* Pallet)
{
	if (!Pallet || CarriedPallet) return;
	CarriedPallet = Pallet;
	bPalletSupportedAutomatically = true;
	bCarriedPalletResting = false;
	// KeepWorld prevents a support transition from moving the pallet.  The
	// upcoming lift transform moves the newly attached pallet by the same delta.
	CarriedPallet->SetCarriedState(true);
	CarriedPallet->AttachToComponent(ForkCarriage, FAttachmentTransformRules::KeepWorldTransform);
	bIsLoaded = true;
	PalletSupportDebugStatus = TEXT("AUTO SUPPORT: both forks support the upper deck");
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_SUPPORT] Begin: forklift=%s pallet=%s insertionMin=%.1fcm"), *GetName(), *GetNameSafe(CarriedPallet), MinimumPalletSupportInsertionCm);
}

void AAutomatedForklift::ReleaseAutomaticPalletSupport(const TCHAR* Reason)
{
	if (!CarriedPallet || !bPalletSupportedAutomatically) return;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_AUTO_SUPPORT] Release: forklift=%s pallet=%s reason=%s"), *GetName(), *GetNameSafe(CarriedPallet), Reason);
	CarriedPallet->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	CarriedPallet->SetCarriedState(false);
	CarriedPallet = nullptr;
	bPalletSupportedAutomatically = false;
	bCarriedPalletResting = false;
	bIsLoaded = false;
	PalletSupportDebugStatus = FString::Printf(TEXT("AUTO SUPPORT RELEASED: %s"), Reason);
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
	if (bChaosConfigured)
	{
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_CHAOS] G attachment is disabled in Chaos transport mode; lift by physical fork contact."));
		return;
	}
	if (CarriedPallet)
	{
		if (LiftCm > 12.f) return; // Lower the forks before setting the load down.
		CarriedPallet->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		CarriedPallet->SetCarriedState(false);
		CarriedPallet = nullptr;
		bPalletSupportedAutomatically = false;
		bCarriedPalletResting = false;
		bIsLoaded = false;
		PalletSupportDebugStatus = TEXT("G debug attachment released");
		return;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ForkliftPalletPickup), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, ForkCollision->GetComponentLocation(), ForkCollision->GetComponentQuat(),
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects), FCollisionShape::MakeBox(FVector(Dimensions.ForkLengthCm * .5f + 55.f, 45.f, 30.f)), QueryParams);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		APallet* Pallet = Cast<APallet>(Overlap.GetActor());
		if (!Pallet) continue;
		const FVector LocalPallet = GetActorTransform().InverseTransformPosition(Pallet->GetActorLocation());
		const float PalletYawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Pallet->GetActorRotation().Yaw));
		if (LocalPallet.X < 55.f || PalletYawDelta > 5.f || LiftCm > 2.f || !Pallet->CanAcceptForkTines(LeftForkCollision, RightForkCollision)) continue;
		CarriedPallet = Pallet;
		bPalletSupportedAutomatically = false; // G remains an explicit debug attachment.
		CarriedPallet->SetCarriedState(true);
		CarriedPallet->AttachToComponent(ForkCarriage, FAttachmentTransformRules::KeepWorldTransform);
		bIsLoaded = true;
		PalletSupportDebugStatus = TEXT("G debug attachment active");
		return;
	}
}

void AAutomatedForklift::ApplyChaosControls(float Dt)
{
	// Chassis is intentionally kinematic in hybrid mode; only constraint drives
	// actuate dynamic fork bodies. No body transform is overwritten here.
	LiftCm = FMath::Clamp(LiftCm + LiftInput * (LiftInput >= 0.f ? Motion.UnloadedLiftSpeedCmPerSec : Motion.UnloadedLowerSpeedCmPerSec) * Dt, 0.f, RequiredLiftStrokeCm);
	ReachCm = FMath::Clamp(ReachCm + ReachInput * Motion.ReachSpeedCmPerSec * Dt, 0.f, Dimensions.ReachTravelCm);
	SideShiftCm = FMath::Clamp(SideShiftCm + SideShiftInput * Motion.SideShiftSpeedCmPerSec * Dt, -Dimensions.ForkSideShiftCm, Dimensions.ForkSideShiftCm);
	// The native Chaos drive acts in the opposite direction to the Frame1 ->
	// Frame2 diagnostic vector.  The centred reference frames make user zero
	// the lower/retracted hard stop, rather than world Z/X zero.
	LiftNativeDriveTargetCm = LiftConstraintCentreCm - LiftCm;
	ReachNativeDriveTargetCm = ReachConstraintCentreCm - ReachCm;
	LiftConstraint->SetLinearPositionTarget(FVector(0.f, 0.f, LiftNativeDriveTargetCm));
	ReachConstraint->SetLinearPositionTarget(FVector(ReachNativeDriveTargetCm, 0.f, 0.f));
	// Position target owns the final 0..39.3cm pose; velocity drive supplies
	// the commanded prismatic motion while the target is changing. Both act
	// through the same X-axis constraint and never overwrite a body transform.
	ReachConstraint->SetLinearVelocityTarget(FVector(-ReachInput * Motion.ReachSpeedCmPerSec, 0.f, 0.f));
	SideShiftConstraint->SetLinearPositionTarget(FVector(0.f, SideShiftCm, 0.f));
	const FVector LiftNativeActual = GetConstraintFrameDelta(LiftConstraint, BodyCollision, LiftPhysicsBody);
	const FVector ReachNativeActual = GetConstraintFrameDelta(ReachConstraint, LiftPhysicsBody, ReachPhysicsBody);
	const FVector SideActual = GetConstraintFrameDelta(SideShiftConstraint, ReachPhysicsBody, ForkPhysicsBody);
	const float LiftActualCm = LiftNativeActual.Z + LiftConstraintCentreCm;
	const FVector ReachBodyInLift = LiftPhysicsBody->GetComponentTransform().InverseTransformPosition(ReachPhysicsBody->GetComponentLocation());
	const FVector ForkBodyInLift = LiftPhysicsBody->GetComponentTransform().InverseTransformPosition(ForkPhysicsBody->GetComponentLocation());
	const FVector LeftTineInLift = LiftPhysicsBody->GetComponentTransform().InverseTransformPosition(LeftForkCollision->GetComponentLocation());
	const FVector RightTineInLift = LiftPhysicsBody->GetComponentTransform().InverseTransformPosition(RightForkCollision->GetComponentLocation());
	// This is deliberately not reconstructed from the Constraint target or
	// centred native coordinate: it is the measured X displacement of the
	// reach rigid body in the lift body's current local frame.
	const float ReachActualCm = ReachBodyInLift.X - ReachRestRelativeXcm;
	UpdateMastVisuals(LiftActualCm);
	const float ForkTopHeightCm = GetForkTopHeightAboveGroundCm();
	ActuatorDebugStatus = FString::Printf(TEXT("ForkTop %.1f / %.1fcm (stroke %.1f) | E/Q user %.1f actual %.1f | R/F user %.1f actual %.1f / %.1f | Z/C %.1f Y=%.1f"),
		ForkTopHeightCm, MaximumForkTopHeightAboveGroundCm, RequiredLiftStrokeCm, LiftCm, LiftActualCm, ReachCm, ReachActualCm, Dimensions.ReachTravelCm, SideShiftCm, SideActual.Y);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if ((!FMath::IsNearlyZero(LiftInput) || !FMath::IsNearlyZero(ReachInput) || !FMath::IsNearlyZero(SideShiftInput)) && Now - LastActuatorLogTime >= .5f)
	{
		LastActuatorLogTime = Now;
		UE_LOG(LogTemp, Warning, TEXT("[R16HD_CHAOS_ACTUATOR_V5] inputs=(%.2f,%.2f,%.2f) user=(lift %.2f reach %.2f side %.2f) nativeTargets=(lift %.2f reach %.2f side %.2f) nativeActual=(lift %.2f reach %.2f side %.2f) userActual=(lift %.2f reach %.2f) forkTop=%.2f/%.2f reachInLift=(Body%s Fork%s LT%s RT%s) sim=(%d,%d,%d) tineSim=(%d,%d) constraints=(%d,%d,%d,%d,%d) world=(L%s R%s F%s LT%s RT%s) velocity=(L%s R%s F%s LT%s RT%s) parents=(Lift=%s Reach=%s Fork=%s LeftTine=%s RightTine=%s)"),
			LiftInput, ReachInput, SideShiftInput, LiftCm, ReachCm, SideShiftCm, LiftNativeDriveTargetCm, ReachNativeDriveTargetCm, SideShiftCm, LiftNativeActual.Z, ReachNativeActual.X, SideActual.Y, LiftActualCm, ReachActualCm, ForkTopHeightCm, MaximumForkTopHeightAboveGroundCm, *ReachBodyInLift.ToString(), *ForkBodyInLift.ToString(), *LeftTineInLift.ToString(), *RightTineInLift.ToString(),
			LiftPhysicsBody->IsSimulatingPhysics(), ReachPhysicsBody->IsSimulatingPhysics(), ForkPhysicsBody->IsSimulatingPhysics(),
			LeftForkCollision->IsSimulatingPhysics(), RightForkCollision->IsSimulatingPhysics(),
			LiftConstraint->ConstraintInstance.IsValidConstraintInstance(), ReachConstraint->ConstraintInstance.IsValidConstraintInstance(), SideShiftConstraint->ConstraintInstance.IsValidConstraintInstance(), LeftForkFixedConstraint->ConstraintInstance.IsValidConstraintInstance(), RightForkFixedConstraint->ConstraintInstance.IsValidConstraintInstance(),
			*LiftPhysicsBody->GetComponentLocation().ToString(), *ReachPhysicsBody->GetComponentLocation().ToString(), *ForkPhysicsBody->GetComponentLocation().ToString(),
			*LeftForkCollision->GetComponentLocation().ToString(), *RightForkCollision->GetComponentLocation().ToString(),
			*LiftPhysicsBody->GetPhysicsLinearVelocity().ToString(), *ReachPhysicsBody->GetPhysicsLinearVelocity().ToString(), *ForkPhysicsBody->GetPhysicsLinearVelocity().ToString(), *LeftForkCollision->GetPhysicsLinearVelocity().ToString(), *RightForkCollision->GetPhysicsLinearVelocity().ToString(),
			*GetNameSafe(LiftPhysicsBody->GetAttachParent()), *GetNameSafe(ReachPhysicsBody->GetAttachParent()), *GetNameSafe(ForkPhysicsBody->GetAttachParent()), *GetNameSafe(LeftForkCollision->GetAttachParent()), *GetNameSafe(RightForkCollision->GetAttachParent()));
	}
	if (bPalletSensorCaptureActive && PalletSensorCapture)
	{
		PalletSensorCaptureElapsed += Dt;
		if (PalletSensorCaptureElapsed >= PalletSensorCaptureIntervalSeconds) { PalletSensorCaptureElapsed = 0.f; PalletSensorCapture->CaptureScene(); }
	}
	DrawCollisionDebug(); DrawPalletSensorDebug();
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
	AddActorWorldRotation(FRotator(0.f, SteerInput * Motion.SteeringRateDegPerSec * Dt, 0.f));
	if (bChaosConfigured) { ApplyChaosControls(Dt); return; }
	const float LiftRate = LiftInput >= 0.f ? (bIsLoaded ? Motion.LoadedLiftSpeedCmPerSec : Motion.UnloadedLiftSpeedCmPerSec) : (bIsLoaded ? Motion.LoadedLowerSpeedCmPerSec : Motion.UnloadedLowerSpeedCmPerSec);
	const float DesiredLiftCm = FMath::Clamp(LiftCm + LiftInput * LiftRate * Dt, 0.f, Dimensions.MaximumLiftHeightCm);
	const float DesiredReachCm = FMath::Clamp(ReachCm + ReachInput * Motion.ReachSpeedCmPerSec * Dt, 0.f, Dimensions.ReachTravelCm);
	const float DesiredSideShiftCm = FMath::Clamp(SideShiftCm + SideShiftInput * Motion.SideShiftSpeedCmPerSec * Dt, -Dimensions.ForkSideShiftCm, Dimensions.ForkSideShiftCm);
	const auto IsForkMotionBlocked = [this](const FVector& WorldDelta, const TCHAR* Operation, const AActor* ExtraIgnoredActor = nullptr)
	{
		for (UBoxComponent* Part : {LeftForkCollision.Get(), RightForkCollision.Get()})
		{
			FHitResult PartHit;
			if (SweepForkliftPart(Part, WorldDelta, Operation, PartHit, ExtraIgnoredActor)) return true;
		}
		FHitResult LoadHit;
		return SweepCarriedPallet(WorldDelta, Operation, LoadHit);
	};
	const FVector WorldLiftDelta(0.f, 0.f, DesiredLiftCm - LiftCm);
	if (!CarriedPallet && LiftInput > KINDA_SMALL_NUMBER)
	{
		FString SupportReason;
		APallet* SupportCandidate = FindPalletForAutomaticSupport(WorldLiftDelta.Z, SupportReason);
		PalletSupportDebugStatus = SupportCandidate ? FString::Printf(TEXT("AUTO SUPPORT READY: %s"), *SupportReason) : FString::Printf(TEXT("NOT SUPPORTED: %s"), *SupportReason);
		FHitResult ExternalLoadHit;
		const bool bExternalLoadBlocked = SupportCandidate && SweepPalletAgainstExternal(SupportCandidate, WorldLiftDelta, TEXT("automatic support external lift sweep"), ExternalLoadHit);
		if (SupportCandidate && !bExternalLoadBlocked && !IsForkMotionBlocked(WorldLiftDelta, TEXT("automatic support fork lift sweep"), SupportCandidate))
		{
			BeginAutomaticPalletSupport(SupportCandidate);
			LiftCm = DesiredLiftCm;
		}
		else if (!SupportCandidate && !IsForkMotionBlocked(WorldLiftDelta, TEXT("lift sweep")))
		{
			LiftCm = DesiredLiftCm;
		}
	}
	else if (CarriedPallet && bPalletSupportedAutomatically && LiftInput < -KINDA_SMALL_NUMBER && bCarriedPalletResting)
	{
		// A second lowering input after the load has seated removes support; the
		// released pallet re-enables Chaos gravity instead of remaining suspended.
		ReleaseAutomaticPalletSupport(TEXT("operator lowered forks after pallet seated"));
	}
	else
	{
		FHitResult LoadLiftHit;
		const bool bLoadBlocked = SweepCarriedPallet(WorldLiftDelta, TEXT("carried pallet lift sweep"), LoadLiftHit);
		if (CarriedPallet && bPalletSupportedAutomatically && LiftInput < -KINDA_SMALL_NUMBER && bLoadBlocked)
		{
			bCarriedPalletResting = true;
			PalletSupportDebugStatus = FString::Printf(TEXT("AUTO SUPPORT RESTING: %s / %s"), *GetNameSafe(LoadLiftHit.GetActor()), *GetNameSafe(LoadLiftHit.GetComponent()));
		}
		else if (!bLoadBlocked && !IsForkMotionBlocked(WorldLiftDelta, TEXT("lift sweep")))
		{
			LiftCm = DesiredLiftCm;
			if (CarriedPallet) bCarriedPalletResting = false;
		}
	}
	const FVector WorldReachDelta = GetActorTransform().TransformVectorNoScale(FVector(DesiredReachCm - ReachCm, 0.f, 0.f));
	if (!IsForkMotionBlocked(WorldReachDelta, TEXT("reach sweep"))) ReachCm = DesiredReachCm;
	const FVector WorldSideShiftDelta = GetActorTransform().TransformVectorNoScale(FVector(0.f, DesiredSideShiftCm - SideShiftCm, 0.f));
	if (!IsForkMotionBlocked(WorldSideShiftDelta, TEXT("side-shift sweep"))) SideShiftCm = DesiredSideShiftCm;
	LiftCarriage->SetRelativeLocation(FVector(0, 0, LiftCm)); ReachCarriage->SetRelativeLocation(FVector(ReachCm, 0, 0)); ForkCarriage->SetRelativeLocation(FVector(0, SideShiftCm, 0));
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
	DrawCollisionDebug();
	DrawPalletSensorDebug();
}
