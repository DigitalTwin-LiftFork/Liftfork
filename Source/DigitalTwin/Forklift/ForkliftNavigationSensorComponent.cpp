#include "Forklift/ForkliftNavigationSensorComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"

float UForkliftNavigationSensorComponent::GetGroundHeightInBody() const
{
	const UBoxComponent* Body = GetOwner() ? Cast<UBoxComponent>(GetOwner()->GetRootComponent()) : nullptr;
	return Body ? -Body->GetUnscaledBoxExtent().Z - 2.f : -55.f;
}

UForkliftNavigationSensorComponent::UForkliftNavigationSensorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UForkliftNavigationSensorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (FMath::IsNearlyEqual(ScanIntervalSeconds,.01f) || FMath::IsNearlyEqual(ScanIntervalSeconds,.10f)) ScanIntervalSeconds=.002f;
	// Migrate the previous 15 m instance setting for the expanded factory.
	if (FMath::IsNearlyEqual(MaximumRangeCm, 1500.f)) MaximumRangeCm = 3000.f;
	// Migrate serialized defaults from the old stationary sector scanner.
	if (FMath::IsNearlyEqual(ScanIntervalSeconds, .10f)) ScanIntervalSeconds = .01f;
	if (HorizontalSamples == 120) HorizontalSamples = 100;
	if (FMath::IsNearlyEqual(VerticalFieldOfViewDegrees, 50.f)) VerticalFieldOfViewDegrees = 120.f;
	if (VerticalChannels == 8) VerticalChannels = 9;
	if (FMath::IsNearlyEqual(VerticalCenterDegrees, 0.f) || FMath::IsNearlyEqual(VerticalCenterDegrees, -10.f) || FMath::IsNearlyEqual(VerticalCenterDegrees, -15.f)) VerticalCenterDegrees = -30.f;
	if (AActor* Owner = GetOwner()) PrimaryComponentTick.AddPrerequisite(Owner, Owner->PrimaryActorTick);
	PreviousWorldLocation = GetComponentLocation();
	PreviousWorldRotation = GetComponentQuat();
	PreviousImuTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	SweepSamples.Reserve(FMath::Max(1, HorizontalSamples) * (FMath::Max(1, VerticalChannels)+38));
}

void UForkliftNavigationSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateImu();
	if (!bScanEnabled)
	{
		ScanElapsedSeconds = 0.f;
		SweepSamples.Reset();
		bSweepReferenceInitialized = false;
	}
	else if (ScanIntervalSeconds >= .001f && HorizontalSamples > 0 && VerticalChannels > 0 && MaximumRangeCm > MinimumRangeCm)
	{
		ScanElapsedSeconds += DeltaTime;
		int32 ColumnsThisFrame = 0;
		while (ScanElapsedSeconds >= ScanIntervalSeconds && ColumnsThisFrame < 128)
		{
			ScanElapsedSeconds -= ScanIntervalSeconds;
			PerformScan();
			++ColumnsThisFrame;
		}
		// Drop excess backlog after a long frame instead of replaying stale rays.
		if (ColumnsThisFrame == 128) ScanElapsedSeconds = 0.f;
	}
	if (bDebugVisible) DrawLidarDebug();
}

void UForkliftNavigationSensorComponent::ToggleDebugVisible()
{
	bDebugVisible = !bDebugVisible;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_LIDAR] debugVisible=%d scanEnabled=%d spin=%.1f deg/s verticalFov=%.1f deg"),
		bDebugVisible, bScanEnabled, 360.f / FMath::Max(.001f, ScanIntervalSeconds * HorizontalSamples), VerticalFieldOfViewDegrees);
}

void UForkliftNavigationSensorComponent::PerformScan()
{
	UWorld* World = GetWorld();
	if (!World) return;
	const double Now = World->GetTimeSeconds();
	const FTransform SensorWorld = GetComponentTransform();
	if (!bSweepReferenceInitialized)
	{
		SweepStartTransform = SensorWorld;
		SweepSamples.Reset();
		ScannerYawDegrees = 0.f;
		bSweepReferenceInitialized = true;
	}

	LatestBeamSamples.Reset();
	LatestBeamWorldEnds.Reset();
	LatestBeamSamples.Reserve(VerticalChannels+38);
	LatestBeamWorldEnds.Reserve(VerticalChannels+38);
	LatestBeamWorldStart = SensorWorld.GetLocation();
	LatestBeamWorldForward = SensorWorld.TransformVectorNoScale(FRotator(VerticalCenterDegrees, ScannerYawDegrees, 0.f).Vector()).GetSafeNormal();

	const float VerticalStep = VerticalChannels > 1 ? VerticalFieldOfViewDegrees / (VerticalChannels - 1) : 0.f;
	const float VerticalStart = VerticalCenterDegrees - VerticalFieldOfViewDegrees * .5f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(R16HDLidar), false);
	Params.bTraceComplex = false;
	Params.AddIgnoredActor(GetOwner()); // Ignore only this forklift in the sensor query.

	// Same rotating head, with extra downward elevation channels. Fixed 15-degree
	// spacing at a 7 m mount left metre-wide gaps in floor observations.
	const float MountHeight = GetRelativeLocation().Z - GetGroundHeightInBody();
	const int32 GroundChannels = MountHeight > 250.f ? 38 : 0;
	for (int32 VerticalIndex = 0; VerticalIndex < VerticalChannels + GroundChannels; ++VerticalIndex)
	{
		const float PitchDegrees = VerticalIndex < VerticalChannels ?
			(VerticalChannels > 1 ? VerticalStart + VerticalIndex * VerticalStep : VerticalCenterDegrees) :
			-FMath::RadiansToDegrees(FMath::Atan2(MountHeight, 20.f + 40.f * (VerticalIndex - VerticalChannels)));
		const FVector LocalDirection = FRotator(PitchDegrees, ScannerYawDegrees, 0.f).Vector();
		const FVector WorldDirection = SensorWorld.TransformVectorNoScale(LocalDirection).GetSafeNormal();
		const FVector Start = SensorWorld.GetLocation() + WorldDirection * MinimumRangeCm;
		const FVector End = SensorWorld.GetLocation() + WorldDirection * MaximumRangeCm;

		FLidarRaySample Sample;
		Sample.LocalDirection = SweepStartTransform.InverseTransformVectorNoScale(WorldDirection).GetSafeNormal();
		FVector DebugEnd = End;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			Sample.DistanceCm = FVector::Distance(SensorWorld.GetLocation(), Hit.ImpactPoint);
			Sample.LocalPointCm = SweepStartTransform.InverseTransformPosition(Hit.ImpactPoint);
			Sample.LocalNormal = SweepStartTransform.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();
			DebugEnd = Hit.ImpactPoint;
			if (Hit.GetActor() == GetOwner())
			{
				Sample.ReturnKind = ELidarReturnKind::SelfOccluded;
				Sample.bValid = false;
			}
			else
			{
				Sample.ReturnKind = ELidarReturnKind::ExternalHit;
				Sample.bValid = true;
			}
		}
		else
		{
			Sample.ReturnKind = ELidarReturnKind::NoReturn;
		}
		SweepSamples.Add(Sample);
		LatestBeamSamples.Add(Sample);
		LatestBeamWorldEnds.Add(DebugEnd);
	}

	const float AzimuthStepDegrees = 360.f / FMath::Max(1, HorizontalSamples);
	ScannerYawDegrees += AzimuthStepDegrees;
	if (ScannerYawDegrees >= 360.f - KINDA_SMALL_NUMBER)
	{
		ScannerYawDegrees = 0.f;
		const FTransform EndSensorTransform = GetComponentTransform();
		for (FLidarRaySample& Sample : SweepSamples)
		{
			if (Sample.ReturnKind != ELidarReturnKind::NoReturn)
				Sample.LocalPointCm = EndSensorTransform.InverseTransformPosition(SweepStartTransform.TransformPosition(Sample.LocalPointCm));
			Sample.LocalDirection = EndSensorTransform.InverseTransformVectorNoScale(SweepStartTransform.TransformVectorNoScale(Sample.LocalDirection)).GetSafeNormal();
			if (!Sample.LocalNormal.IsNearlyZero()) Sample.LocalNormal = EndSensorTransform.InverseTransformVectorNoScale(SweepStartTransform.TransformVectorNoScale(Sample.LocalNormal)).GetSafeNormal();
		}
		LatestScan.TimestampSeconds = Now;
		LatestScan.SensorToBody = GetRelativeTransform();
		LatestScan.Samples = MoveTemp(SweepSamples);
		SweepSamples.Reserve(FMath::Max(1, HorizontalSamples) * (FMath::Max(1, VerticalChannels)+38));
		SweepStartTransform = EndSensorTransform;
	}
}

void UForkliftNavigationSensorComponent::UpdateImu()
{
	UWorld* World = GetWorld();
	if (!World) return;
	const double Now = World->GetTimeSeconds();
	const FVector CurrentLocation = GetComponentLocation();
	const FQuat CurrentRotation = GetComponentQuat();
	LatestImu.TimestampSeconds = Now;
	LatestImu.bIncludesGravity = bImuIncludesGravity;
	if (!bHasPreviousImuPose)
	{
		bHasPreviousImuPose = true;
		PreviousWorldLocation = CurrentLocation;
		PreviousWorldRotation = CurrentRotation;
		PreviousWorldVelocity = FVector::ZeroVector;
		PreviousImuTimeSeconds = Now;
		LatestImu = FForkliftImuSample();
		LatestImu.TimestampSeconds = Now;
		LatestImu.bIncludesGravity = bImuIncludesGravity;
		LatestImu.bInitialized = true;
		return;
	}
	const float Dt = static_cast<float>(Now - PreviousImuTimeSeconds);
	FQuat DeltaRotation = CurrentRotation * PreviousWorldRotation.Inverse();
	DeltaRotation.Normalize();
	float AngleRadians = 0.f;
	FVector Axis = FVector::UpVector;
	DeltaRotation.ToAxisAndAngle(Axis, AngleRadians);
	if (AngleRadians > PI) AngleRadians -= 2.f * PI;
	const bool bTeleported = FVector::Dist(CurrentLocation, PreviousWorldLocation) > TeleportResetDistanceCm || FMath::Abs(FMath::RadiansToDegrees(AngleRadians)) > TeleportResetRotationDegrees;
	if (Dt <= SMALL_NUMBER || bTeleported)
	{
		PreviousWorldVelocity = FVector::ZeroVector;
		LatestImu.LinearVelocityCmPerSec = FVector::ZeroVector;
		LatestImu.LinearAccelerationCmPerSecSq = FVector::ZeroVector;
		LatestImu.AngularVelocityRadPerSec = FVector::ZeroVector;
	}
	else
	{
		const FVector WorldVelocity = (CurrentLocation - PreviousWorldLocation) / Dt;
		FVector WorldAcceleration = (WorldVelocity - PreviousWorldVelocity) / Dt;
		if (bImuIncludesGravity) WorldAcceleration += FVector(0.f, 0.f, -980.f);
		LatestImu.LinearVelocityCmPerSec = CurrentRotation.UnrotateVector(WorldVelocity);
		LatestImu.LinearAccelerationCmPerSecSq = CurrentRotation.UnrotateVector(WorldAcceleration);
		LatestImu.AngularVelocityRadPerSec = CurrentRotation.UnrotateVector(Axis * (AngleRadians / Dt));
		PreviousWorldVelocity = WorldVelocity;
	}
	PreviousWorldLocation = CurrentLocation;
	PreviousWorldRotation = CurrentRotation;
	PreviousImuTimeSeconds = Now;
	LatestImu.bInitialized = true;
}

void UForkliftNavigationSensorComponent::DrawLidarDebug() const
{
	UWorld* World = GetWorld();
	if (!World) return;
	DrawDebugSphere(World, LatestBeamWorldStart, 8.f, 12, FColor::Cyan, false, 0.f, 0, 1.5f);
	DrawDebugDirectionalArrow(World, LatestBeamWorldStart, LatestBeamWorldStart + LatestBeamWorldForward * 70.f, 12.f, FColor::Cyan, false, 0.f, 0, 2.f);
	for (int32 Index = 0; Index < LatestBeamSamples.Num(); ++Index)
	{
		const FLidarRaySample& Sample = LatestBeamSamples[Index];
		const FVector End = LatestBeamWorldEnds.IsValidIndex(Index) ? LatestBeamWorldEnds[Index] : LatestBeamWorldStart;
		const FColor Colour = Sample.ReturnKind == ELidarReturnKind::ExternalHit ? FColor::Green : (Sample.ReturnKind == ELidarReturnKind::SelfOccluded ? FColor::Orange : FColor(80, 80, 80));
		DrawDebugLine(World, LatestBeamWorldStart, End, Colour, false, 0.f, 0, 1.f);
		if (Sample.ReturnKind != ELidarReturnKind::NoReturn) DrawDebugPoint(World, End, 5.f, Colour, false, 0.f);
	}
}
