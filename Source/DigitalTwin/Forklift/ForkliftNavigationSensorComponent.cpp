#include "Forklift/ForkliftNavigationSensorComponent.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UForkliftNavigationSensorComponent::UForkliftNavigationSensorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UForkliftNavigationSensorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner()) PrimaryComponentTick.AddPrerequisite(Owner, Owner->PrimaryActorTick);
	PreviousWorldLocation = GetComponentLocation();
	PreviousWorldRotation = GetComponentQuat();
	PreviousImuTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void UForkliftNavigationSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateImu();
	if (bScanEnabled)
	{
		ScanElapsedSeconds += DeltaTime;
		if (ScanElapsedSeconds >= ScanIntervalSeconds)
		{
			ScanElapsedSeconds = FMath::Fmod(ScanElapsedSeconds, ScanIntervalSeconds);
			PerformScan();
		}
	}
	if (bDebugVisible) DrawLidarDebug();
}

void UForkliftNavigationSensorComponent::ToggleDebugVisible()
{
	bDebugVisible = !bDebugVisible;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_LIDAR] debugVisible=%d scanEnabled=%d"), bDebugVisible, bScanEnabled);
}

void UForkliftNavigationSensorComponent::PerformScan()
{
	UWorld* World = GetWorld();
	if (!World) return;
	LatestScan.TimestampSeconds = World->GetTimeSeconds();
	LatestScan.SensorToBody = GetRelativeTransform();
	LatestScan.Samples.Reset();
	LatestScan.Samples.Reserve(HorizontalSamples * VerticalChannels);
	const FTransform SensorWorld = GetComponentTransform();
	const float HorizontalStep = HorizontalFieldOfViewDegrees >= 359.99f ? HorizontalFieldOfViewDegrees / HorizontalSamples : HorizontalFieldOfViewDegrees / FMath::Max(1, HorizontalSamples - 1);
	const float VerticalStep = VerticalChannels > 1 ? VerticalFieldOfViewDegrees / (VerticalChannels - 1) : 0.f;
	const float HorizontalStart = -HorizontalFieldOfViewDegrees * .5f;
	const float VerticalStart = VerticalCenterDegrees - VerticalFieldOfViewDegrees * .5f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(R16HDLidar), false);
	Params.bTraceComplex = false;
	for (int32 VerticalIndex = 0; VerticalIndex < VerticalChannels; ++VerticalIndex)
	{
		const float PitchRad = FMath::DegreesToRadians(VerticalStart + VerticalIndex * VerticalStep);
		for (int32 HorizontalIndex = 0; HorizontalIndex < HorizontalSamples; ++HorizontalIndex)
		{
			const float YawRad = FMath::DegreesToRadians(HorizontalStart + HorizontalIndex * HorizontalStep);
			FLidarRaySample& Sample = LatestScan.Samples.AddDefaulted_GetRef();
			Sample.LocalDirection = FRotator(FMath::RadiansToDegrees(PitchRad), FMath::RadiansToDegrees(YawRad), 0.f).Vector();
			const FVector Start = SensorWorld.GetLocation() + SensorWorld.TransformVectorNoScale(Sample.LocalDirection) * MinimumRangeCm;
			const FVector End = SensorWorld.GetLocation() + SensorWorld.TransformVectorNoScale(Sample.LocalDirection) * MaximumRangeCm;
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
			{
				Sample.DistanceCm = FVector::Distance(SensorWorld.GetLocation(), Hit.ImpactPoint);
				Sample.LocalPointCm = SensorWorld.InverseTransformPosition(Hit.ImpactPoint);
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
		}
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
	const FTransform SensorWorld = GetComponentTransform();
	DrawDebugSphere(World, SensorWorld.GetLocation(), 8.f, 12, FColor::Cyan, false, 0.f, 0, 1.5f);
	DrawDebugDirectionalArrow(World, SensorWorld.GetLocation(), SensorWorld.GetLocation() + SensorWorld.GetUnitAxis(EAxis::X) * 50.f, 10.f, FColor::Cyan, false, 0.f, 0, 1.5f);
	for (int32 Index = 0; Index < LatestScan.Samples.Num(); Index += FMath::Max(1, DebugRayStride))
	{
		const FLidarRaySample& Sample = LatestScan.Samples[Index];
		const FVector Start = SensorWorld.GetLocation();
		const FVector End = Sample.ReturnKind == ELidarReturnKind::NoReturn ? Start + SensorWorld.TransformVectorNoScale(Sample.LocalDirection) * MaximumRangeCm : SensorWorld.TransformPosition(Sample.LocalPointCm);
		const FColor Colour = Sample.ReturnKind == ELidarReturnKind::ExternalHit ? FColor::Green : (Sample.ReturnKind == ELidarReturnKind::SelfOccluded ? FColor::Orange : FColor(80, 80, 80));
		DrawDebugLine(World, Start, End, Colour, false, 0.f, 0, 0.75f);
		if (Sample.ReturnKind != ELidarReturnKind::NoReturn) DrawDebugPoint(World, End, 4.f, Colour, false, 0.f);
	}
}
