#include "Forklift/AutomatedForklift.h"
#include "Components/TextRenderComponent.h"
#include "Forklift/ForkliftNavigationSensorComponent.h"
#include "Forklift/ForkliftSlamComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Forklift/Pallet.h"

namespace
{
	FVector2D NavRotate(FVector2D P, float A)
	{
		return FVector2D(FMath::Cos(A)*P.X-FMath::Sin(A)*P.Y, FMath::Sin(A)*P.X+FMath::Cos(A)*P.Y);
	}
	FIntPoint NavCell(FVector2D P) { return FIntPoint(FMath::FloorToInt(P.X/40.), FMath::FloorToInt(P.Y/40.)); }
	FVector2D NavCentre(FIntPoint C) { return FVector2D(C.X*40.+20., C.Y*40.+20.); }
	FVector2D BodyPose(const UForkliftSlamComponent* S, const UForkliftNavigationSensorComponent* L)
	{
		const FVector Offset = L->GetRelativeLocation();
		const double Now=L->GetWorld()->GetTimeSeconds();
		return S->GetPredictedPosition(Now) - NavRotate(FVector2D(Offset.X,Offset.Y), S->GetPredictedYaw());
	}
}

FVector2D AAutomatedForklift::GetNavigationMapPose() const
{
 const auto* S = FindComponentByClass<UForkliftSlamComponent>();
 if (!S || !NavigationSensor) return FVector2D::ZeroVector;
 if (bUsingFactoryOdometry && FactoryPoseGeneration == S->GetMapGeneration())
 {
  const FVector2D WorldPosition(GetActorLocation().X, GetActorLocation().Y);
  return NavRotate(WorldPosition - FactoryMapOriginInWorld, -FactoryMapToWorldYaw);
 }
 return BodyPose(S, NavigationSensor);
}

float AAutomatedForklift::GetNavigationMapHeading() const
{
 const auto* S = FindComponentByClass<UForkliftSlamComponent>();
 return S ? GetNavigationHeading(*S) : 0.f;
}

void AAutomatedForklift::BeginTransportUnload(int32 StationIndex)
{
 if (!bIsLoaded || SupportedLoadActors.IsEmpty()) return;
 TransportStation = StationIndex;
 TransportBackOutTargetCm = 200.f;
 bNavigationEscapeBackOut = false;
 TransportPhase = ETransportPhase::Lower;
 TransportPhaseElapsed = 0.f;
 DriveInput = SteerInput = ReachInput = SideShiftInput = 0.f;
 NavigationStatus = TEXT("도착: 팔레트를 바닥에 내려놓는 중");
 UE_LOG(LogTemp, Log, TEXT("Transport unload started at station %d, lift %.1f cm"), StationIndex, LiftCm);
}

void AAutomatedForklift::UpdateTransportAutomation(float Dt)
{
 const bool bSupported = bIsLoaded && !SupportedLoadActors.IsEmpty();
 if (!bSupported && TransportPhase == ETransportPhase::None) bTransportHeightAdjusted = false;
 if (TransportPhase == ETransportPhase::None && bSupported && !bTransportHeightAdjusted && !IsAutoPickupMotionActive() &&
     FMath::IsNearlyZero(LiftInput) && IsSupportedLoadFullyInserted())
 {
  TransportPhase = ETransportPhase::Raise;
  TransportPhaseElapsed = 0.f;
 }
 if (TransportPhase == ETransportPhase::None) return;
 TransportPhaseElapsed += Dt;
 DriveInput = SteerInput = LiftInput = ReachInput = SideShiftInput = 0.f;
 if (TransportPhase == ETransportPhase::Raise)
 {
  if (!bSupported) { TransportPhase = ETransportPhase::None; bTransportHeightAdjusted = true; NavigationStatus = TEXT("운반 높이 조절 중지: 적재가 감지되지 않음"); return; }
  const float Difference = 25.f - LiftCm;
  if (FMath::Abs(Difference) <= 1.f) { TransportPhase = ETransportPhase::None; bTransportHeightAdjusted = true; NavigationStatus = TEXT("팔레트 운반 높이 25cm 준비 완료"); return; }
  if (TransportPhaseElapsed > 8.f) { TransportPhase = ETransportPhase::None; bTransportHeightAdjusted = true; NavigationStatus = TEXT("운반 높이 조절 실패: 포크 이동이 막힘"); return; }
  LiftInput = FMath::Sign(Difference) * FMath::Min(1.f, FMath::Abs(Difference) / FMath::Max(1.f, Motion.LoadedLiftSpeedCmPerSec * Dt));
  NavigationStatus = FString::Printf(TEXT("적재 감지: 운반 높이 25cm 조절 중 (현재 %.1fcm)"), LiftCm);
  return;
 }
 if (TransportPhase == ETransportPhase::Lower)
 {
  if (LiftCm > 1.f)
  {
   if (TransportPhaseElapsed > 15.f) { TransportPhase = ETransportPhase::None; NavigationStatus = TEXT("하역 중지: 포크 하강이 막힘"); return; }
   LiftInput = -1.f;
   NavigationStatus = FString::Printf(TEXT("팔레트 하역: 포크 하강 %.1fcm"), LiftCm);
   return;
  }
  TransportPhase = ETransportPhase::Settle;
  TransportPhaseElapsed = 0.f;
 }
 if (TransportPhase == ETransportPhase::Settle)
 {
  if (TransportPhaseElapsed < .5f) { NavigationStatus = TEXT("팔레트 하역: 바닥 안착 확인 중"); return; }
  if (bSupported) { TransportPhase = ETransportPhase::None; NavigationStatus = TEXT("하역 중지: 팔레트가 아직 포크에 지지됨 — 수동 확인 필요"); return; }
  TransportPhase = ETransportPhase::Retract;
  TransportPhaseElapsed = 0.f;
 }
 if (TransportPhase == ETransportPhase::Retract)
 {
  if (ReachCm > 1.f)
  {
   if (TransportPhaseElapsed > 8.f) { TransportPhase = ETransportPhase::None; NavigationStatus = TEXT("하역 중지: 리치 복귀가 막힘"); return; }
   ReachInput = -1.f;
   NavigationStatus = TEXT("팔레트 하역: 리치 복귀 중");
   return;
  }
  TransportPhase = ETransportPhase::BackOut;
  TransportPhaseElapsed = 0.f;
  TransportBackOutStart = GetActorLocation();
 }
 if (TransportPhase == ETransportPhase::BackOut)
 {
  const float BackedCm = FVector::DotProduct(TransportBackOutStart - GetActorLocation(), GetActorForwardVector());
  if (BackedCm >= TransportBackOutTargetCm)
  {
   TransportPhase = ETransportPhase::None;
   if (bNavigationEscapeBackOut)
   {
    bNavigationEscapeBackOut = false;
    NavigationReplanTime = 0.f;
    NavigationProgressTime = GetWorld()->GetTimeSeconds();
    NavigationStatus = TEXT("충돌 회피 후진 완료: 경로 재계산 중");
   }
   else NavigationStatus = TEXT("팔레트 하역 완료: 포크 후퇴 및 2m 이탈");
   return;
  }
  if (TransportPhaseElapsed > 15.f || (TransportPhaseElapsed > 1.f && FMath::Abs(CurrentForwardSpeedCmPerSec) < 2.f))
  {
   TransportPhase = ETransportPhase::None;
   if (bNavigationEscapeBackOut)
   {
    bNavigationEscapeBackOut = false;
    BeginNavigationRecovery(TEXT("후진 통로도 막힘"));
   }
   else NavigationStatus = TEXT("하역 후진 중지: 후방 통로 충돌 확인 필요");
   return;
  }
  DriveInput = -20.f / FMath::Max(1.f, Motion.ReverseSpeedCmPerSec);
  NavigationStatus = FString::Printf(TEXT("%s: 저속 후진 %.0f/%.0fcm"), bNavigationEscapeBackOut ? TEXT("충돌 회피") : TEXT("팔레트 하역"), BackedCm, TransportBackOutTargetCm);
 }
}
void AAutomatedForklift::GetNavigationDiagnosticLines(TArray<FString>& OutLines) const
{
	const auto* S = FindComponentByClass<UForkliftSlamComponent>();
	const float Now = GetWorld()->GetTimeSeconds();
	OutLines.Add(FString::Printf(TEXT("주행 상태: %s"), bNavigationActive ? TEXT("자동 주행 활성") : TEXT("정지 / 수동")));
	OutLines.Add(FString::Printf(TEXT("실제 속도 %.2f m/s | 명령 %.2f m/s"), CurrentForwardSpeedCmPerSec / 100.f, TargetForwardSpeedCmPerSec / 100.f));
	OutLines.Add(bNavigationGoalSet && S && NavigationSensor ? FString::Printf(TEXT("목표까지 %.2f m | 경로점 %d"), FVector2D::Distance(GetNavigationMapPose(), NavigationGoal) / 100.f, NavigationRoute.Num()) : TEXT("목표: 미지정"));
	OutLines.Add(S ? FString::Printf(TEXT("위치 신뢰도 %.0f%% | 추적 %s"), S->GetConfidence() * 100.f, S->IsTrackingFresh(Now) ? TEXT("정상") : TEXT("대기")) : TEXT("SLAM 센서 없음"));
	const double Stamp = NavigationSensor ? NavigationSensor->GetLatestScan().TimestampSeconds : -1.;
	OutLines.Add(Stamp >= 0. ? FString::Printf(TEXT("최근 스캔 %.2f초 전 | 복구 시도 %d/3"), Now - Stamp, NavigationRecoveryAttempts) : TEXT("첫 스캔 대기"));
	OutLines.Add(FString::Printf(TEXT("적재 %s | 포크 높이 %.1fcm | 리치 %.1fcm"),
		(CarriedPallet || bIsLoaded) ? TEXT("감지") : TEXT("미감지"), LiftCm, ReachCm));
	OutLines.Add(FString::Printf(TEXT("상태 / 정지 이유: %s"), *NavigationStatus));
}

bool AAutomatedForklift::TryNavigationBackOut(const FString& Reason)
{
 if (TransportPhase != ETransportPhase::None || NavigationEscapeAttempts >= 3 || !GetWorld()) return false;
 const FVector ReverseDelta = -GetActorForwardVector() * 30.f;
 FHitResult Hit;
 if (SweepForkliftBody(ReverseDelta, TEXT("navigation reverse body clearance"), Hit)) return false;
 for (UBoxComponent* Part : {LeftSupportLegCollision.Get(), RightSupportLegCollision.Get(), LeftMastRailCollision.Get(), RightMastRailCollision.Get(), MastTopCrossbarCollision.Get()})
  if (SweepForkliftPart(Part, ReverseDelta, TEXT("navigation reverse part clearance"), Hit) ||
      SweepForkPartAgainstFixedObstacle(Part, ReverseDelta, TEXT("navigation reverse fixed clearance"), Hit)) return false;
 for (UBoxComponent* Tine : {LeftForkCollision.Get(), RightForkCollision.Get()})
  if (SweepForkPartAgainstFixedObstacle(Tine, ReverseDelta, TEXT("navigation reverse fork clearance"), Hit)) return false;
 if (SweepCarriedPallet(ReverseDelta, TEXT("navigation reverse load clearance"), Hit)) return false;
 ++NavigationEscapeAttempts;
 bNavigationEscapeBackOut = true;
 TransportBackOutTargetCm = 120.f;
 TransportBackOutStart = GetActorLocation();
 TransportPhaseElapsed = 0.f;
 TransportPhase = ETransportPhase::BackOut;
 DriveInput = SteerInput = 0.f;
 NavigationStatus = FString::Printf(TEXT("충돌 회피 %d/3: 1.2m 후진 후 경로 재계산 — %s"), NavigationEscapeAttempts, *Reason);
 UE_LOG(LogTemp, Warning, TEXT("Navigation back-out %d/3: %s"), NavigationEscapeAttempts, *Reason);
 return true;
}
void AAutomatedForklift::BeginNavigationRecovery(const FString& Reason)
{
	DriveInput = SteerInput = 0.f;
	if (NavigationRecoveryStart >= 0.f) return;
	if (NavigationRecoveryAttempts >= 3)
	{
		StopNavigation();
		NavigationStatus = FString::Printf(TEXT("복구 중지: %s — 주변 통로 확인 후 다시 시작"), *Reason);
		return;
	}
	++NavigationRecoveryAttempts;
	NavigationRecoveryStart = GetWorld()->GetTimeSeconds();
	const auto* S = FindComponentByClass<UForkliftSlamComponent>();
	NavigationRecoveryScan = S ? S->GetScanCount() : 0;
	NavigationRecoveryReason = Reason;
	// Replan from observed occupancy; a stalled turn is not obstacle evidence.
	NavigationRoute.Reset();
	NavigationStatus = FString::Printf(TEXT("복구 %d/3: 정지 후 주변 재스캔 — %s"), NavigationRecoveryAttempts, *Reason);
	UE_LOG(LogTemp, Log, TEXT("Navigation recovery %d: %s"), NavigationRecoveryAttempts, *Reason);
}

void AAutomatedForklift::StopNavigation()
{
	if (TransportPhase != ETransportPhase::None)
	{
		TransportPhase = ETransportPhase::None;
		DriveInput = SteerInput = LiftInput = ReachInput = SideShiftInput = 0.f;
	}
	if (bNavigationActive) DriveInput = SteerInput = 0.f;
	bNavigationActive = false;
	bUsingFactoryOdometry = false;
	PendingWorkStation = INDEX_NONE;
	NavigationRecoveryStart = -1.f;
	NavigationRoute.Reset();
	bNavigationGoalSet=false;
	bNavigationGoalIsManual=false;
	ActiveWorkStation = INDEX_NONE;
	NavigationStatus = TEXT("자동 탐색 중지");
}

bool AAutomatedForklift::SetNavigationGoalFromMapScreen(FVector2D ScreenPosition, float ViewportHeight)
{
	const FBox2D MapRect = SlamMapRect(ViewportHeight);
	if (!MapRect.IsInside(ScreenPosition)) return false;
	const auto* S = FindComponentByClass<UForkliftSlamComponent>();
	if (!S || !NavigationSensor || !S->bShowMap) return false;
	const float Size = MapRect.GetSize().X;
	const float Scale = Size / 3000.f;
	const FVector2D Centre = MapRect.GetCenter();
	const FVector2D MapPose = GetNavigationMapPose();
	const FVector2D Offset((Centre.Y-ScreenPosition.Y)/Scale,(ScreenPosition.X-Centre.X)/Scale);
	ActiveWorkStation = INDEX_NONE;
	return SetNavigationGoalInMap(MapPose + Offset);
}

bool AAutomatedForklift::IsWorkStationRegistered(int32 Index) const
{
	const auto* Slam = FindComponentByClass<UForkliftSlamComponent>();
	return Slam && WorkStations[Index].MapGeneration == Slam->GetMapGeneration();
}

void AAutomatedForklift::UpdateWorkStationSign(int32 Index, const FVector& Position, const FRotator& Heading)
{
	UTextRenderComponent* Sign = WorkStationSigns[Index].Get();
	if (!Sign)
	{
		Sign = NewObject<UTextRenderComponent>(this);
		AddInstanceComponent(Sign);
		Sign->SetAbsolute(true, true, true);
		Sign->RegisterComponent();
		Sign->SetHorizontalAlignment(EHTA_Center);
		Sign->SetWorldSize(44.f);
		Sign->SetTextRenderColor(Index == 0 ? FColor::Cyan : FColor::Yellow);
		WorkStationSigns[Index] = Sign;
	}
	Sign->SetWorldLocationAndRotation(Position + FRotationMatrix(Heading).GetUnitAxis(EAxis::Y) * (Dimensions.MaximumWidthCm * .5f + 65.f) + FVector(0.f, 0.f, 140.f), Heading + FRotator(0.f, 180.f, 0.f));
	Sign->SetText(FText::FromString(FString::Printf(TEXT("ZONE %c"), TCHAR('A' + Index))));
}

void AAutomatedForklift::GoToStation(int32 Index)
{
	const auto* Slam = FindComponentByClass<UForkliftSlamComponent>();
	if (Index < 0 || Index >= WorkStationCount) return;
	const FWorkStation& Station = WorkStations[Index];
	if (!Slam || Station.MapGeneration != Slam->GetMapGeneration())
	{
		StopNavigation();
		CancelAutoPickup(TEXT("구역 이동 선택"), false);
		ClearAutomaticCommands();
		PendingWorkStation = Index;
		NextWorkStationSearch = 0.f;
		NavigationStatus = FString::Printf(TEXT("%s 이동 대기: 선반 옆 경로 확인 중 · 정지 버튼으로 취소"), *WorkStationLabel(Index));
		UE_LOG(LogTemp, Log, TEXT("Work station %d requested while mapping"), Index);
		return;
	}
	SetNavigationGoalInMap(Station.Position, Index);
	UE_LOG(LogTemp, Log, TEXT("Work station %d: %s"), Index, *NavigationStatus);
}

bool AAutomatedForklift::SetNavigationGoalInMap(FVector2D Goal, int32 StationIndex)
{
	const auto* S = FindComponentByClass<UForkliftSlamComponent>();
	if (!S || !NavigationSensor || !NavigationSensor->bScanEnabled) { NavigationStatus = TEXT("목표 주행 불가: LiDAR 센서 꺼짐"); return false; }
	CancelAutoPickup(TEXT("목표 주행 선택"), false);
	ClearAutomaticCommands();
	DriveInput = SteerInput = 0.f;
	const FIntPoint GoalCell = NavCell(Goal);
	NavigationGoal = StationIndex == INDEX_NONE ? NavCentre(GoalCell) : Goal;
	ActiveWorkStation = StationIndex;
	PendingWorkStation = INDEX_NONE;
	bNavigationGoalIsManual = true;
	bNavigationGoalSet = true;
	bNavigationActive = true;
	NavigationRecoveryStart = -1.f;
	NavigationRecoveryAttempts = 0;
	NavigationEscapeAttempts = 0;
	NavigationAvoidUntil.Reset();
	NavigationRoute.Reset();
	NavigationMapGeneration = S->GetMapGeneration();
	NavigationTrackingWaitStart = -1.f;
	NavigationReplanTime = 0.f;
	NavigationProgressTime = GetWorld()->GetTimeSeconds();
	if (!S->IsTrackingFresh(GetWorld()->GetTimeSeconds()))
	{
		NavigationStatus = TEXT("목표 설정됨: SLAM 위치 추적 대기");
		return true;
	}
	const FVector2D Start = BodyPose(S,NavigationSensor);
	if (FVector2D::Distance(Start, NavigationGoal) >= 30.f && !PlanNavigationRoute(Start))
	{
		bNavigationActive = false;
	bUsingFactoryOdometry = false;
	PendingWorkStation = INDEX_NONE;
		bNavigationGoalSet = false;
		bNavigationGoalIsManual = false;
		NavigationStatus = StationIndex == INDEX_NONE ? TEXT("목표 불가: 지도에서 탐색된 빈 구역을 선택하세요") : TEXT("구역 이동 불가: 현재 적재 폭으로 통과할 경로가 없습니다");
		return true;
	}
	NavigationReplanTime = GetWorld()->GetTimeSeconds()+1.f;
	NavigationProgressPosition = Start;
	NavigationProgressTime = GetWorld()->GetTimeSeconds();
	NavigationStatus = TEXT("목표 경로 설정: LiDAR 지도 경로 주행");
	return true;
}

void AAutomatedForklift::ToggleNavigation()
{
	if (bNavigationActive || PendingWorkStation != INDEX_NONE) { StopNavigation(); return; }
	CancelAutoPickup(TEXT("자동 탐색 선택"), false);
	ClearAutomaticCommands();
	const auto* S=FindComponentByClass<UForkliftSlamComponent>();
	if (S && NavigationMapGeneration!=S->GetMapGeneration()) { ExplorationVisited.Reset(); ExplorationBlockedUntil.Reset(); }
	NavigationMapGeneration=S ? S->GetMapGeneration() : -1;
	bNavigationActive=true;
	NavigationRecoveryStart=-1.f;
	NavigationRecoveryAttempts=0;
	NavigationAvoidUntil.Reset();
	bNavigationGoalIsManual=false;
	bNavigationGoalSet=false;
	NavigationRoute.Reset();
	ExplorationBlockedGoals=0;
	NavigationTrackingWaitStart=-1.f;
	NavigationStatus=TEXT("랜덤 순찰: 센서와 지도 준비 중");
}
float AAutomatedForklift::GetNavigationHalfWidth() const
{
	double Width = Dimensions.MaximumWidthCm * .5f;
	TArray<UBoxComponent*> LoadShapes;
	GetSupportedPalletShapes(LoadShapes);
	for (const UBoxComponent* Shape : LoadShapes)
	{
		const FBox LoadBounds = Shape->CalcBounds(Shape->GetComponentTransform().GetRelativeTransform(GetActorTransform())).GetBox();
		Width = FMath::Max(Width, FMath::Max(FMath::Abs(LoadBounds.Min.Y), FMath::Abs(LoadBounds.Max.Y)));
	}
	return static_cast<float>(Width);
}

bool AAutomatedForklift::IsFactoryAisleClear(const FVector& Position, float Radius) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FactoryAisle), false, this);
	if (CarriedPallet) Params.AddIgnoredActor(CarriedPallet.Get());
	for (const auto& Pallet : SupportedLoadActors) if (Pallet.IsValid()) Params.AddIgnoredActor(Pallet.Get());
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FHitResult Floor;
	if (!GetWorld()->LineTraceSingleByObjectType(Floor, Position + FVector(0, 0, 20), Position - FVector(0, 0, 120), Objects, Params) ||
		Floor.ImpactNormal.Z < .9f || FMath::Abs(Floor.ImpactPoint.Z - (GetActorLocation().Z - BodyCollision->GetScaledBoxExtent().Z)) > 15.f)
		return false;

	return !GetWorld()->OverlapAnyTestByObjectType(Floor.ImpactPoint + FVector(0, 0, 135), FQuat::Identity,
		Objects, FCollisionShape::MakeBox(FVector(Radius, Radius, 125.f)), Params);
}

void AAutomatedForklift::ResolveWorkStations(const UForkliftSlamComponent& Slam, const FVector2D& Start)
{
	if (!bRackRowsScanned)
	{
		bRackRowsScanned = true;
		for (FBox& Row : RackRows) Row.Init();
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const FString Label = It->GetActorNameOrLabel();
			constexpr int32 PrefixLength = 13; // Factory_Rack_
			if (!Label.StartsWith(TEXT("Factory_Rack_")) || Label.Len() <= PrefixLength + 1 ||
				Label[PrefixLength + 1] != TCHAR('_')) continue;
			const int32 Row = Label[PrefixLength] - TCHAR('0');
			if (Row < 0 || Row >= WorkStationCount) continue;
			RackRows[Row] += It->GetComponentsBoundingBox();
		}
	}
	const float MapToWorldYaw = FMath::DegreesToRadians(GetActorRotation().Yaw) - Slam.GetPredictedYaw();
	const FVector WorldStart = GetActorLocation();
	for (int32 Index = 0; Index < WorkStationCount; ++Index)
	{
		const FBox& Row = RackRows[Index];
		if (IsWorkStationRegistered(Index) || !Row.IsValid) continue;
		const FVector Center = Row.GetCenter();
		const bool bAlongY = Row.GetSize().Y >= Row.GetSize().X;
		const FVector Along = bAlongY ? FVector::YAxisVector : FVector::XAxisVector;
		const FVector Across = bAlongY ? FVector::XAxisVector : FVector::YAxisVector;
		const float Edge = bAlongY ? Row.GetExtent().X : Row.GetExtent().Y;
		const float HalfLength = bAlongY ? Row.GetExtent().Y : Row.GetExtent().X;
		const float Radius = FMath::Max(GetNavigationHalfWidth() + 25.f, 100.f);
		double BestDistance = TNumericLimits<double>::Max();
		FVector BestPosition = FVector::ZeroVector, BestFacing = FVector::ZeroVector;
		for (const float Sign : {-1.f, 1.f})
		{
			for (const float Fraction : {0.f, -.25f, .25f, -.5f, .5f, -.75f, .75f})
			{
				FVector Candidate = Center + Across * Sign * (Edge + 260.f) + Along * (HalfLength * Fraction);
				Candidate.Z = WorldStart.Z;
				const double Distance = FVector::DistSquared2D(Candidate, WorldStart);
				if (Distance >= BestDistance || !IsFactoryAisleClear(Candidate, Radius)) continue;
				BestDistance = Distance;
				BestPosition = Candidate;
				BestFacing = -Across * Sign;
			}
		}
		if (BestFacing.IsNearlyZero()) continue;
		const FVector Offset = BestPosition - WorldStart;
		const FVector2D Goal = NavCentre(NavCell(Start + NavRotate(FVector2D(Offset.X, Offset.Y), -MapToWorldYaw)));
		const float Heading = FMath::DegreesToRadians(BestFacing.Rotation().Yaw) - MapToWorldYaw;
		WorkStations[Index] = {Goal, BestPosition, Heading, Slam.GetMapGeneration()};
		UpdateWorkStationSign(Index, BestPosition, BestFacing.Rotation());
		UE_LOG(LogTemp, Log, TEXT("Rack zone %c ready at %s"), TCHAR('A' + Index), *BestPosition.ToCompactString());
	}
}
bool AAutomatedForklift::HasFactoryRackRows() const
{
	if (!bRackRowsScanned) return false;
	for (const FBox& Row : RackRows)
		if (Row.IsValid) return true;
	return false;
}

float AAutomatedForklift::GetNavigationHeading(const UForkliftSlamComponent& Slam) const
{
	return bUsingFactoryOdometry
		? FMath::UnwindRadians(FMath::DegreesToRadians(GetActorRotation().Yaw) - FactoryMapToWorldYaw)
		: Slam.GetPredictedYaw();
}

bool AAutomatedForklift::PlanNavigationRoute(FVector2D Start)
{
	const auto* S = FindComponentByClass<UForkliftSlamComponent>();
	NavigationRoute.Reset();
	if (!S) return false;
	const bool bFactoryRoute = ActiveWorkStation != INDEX_NONE || (!bNavigationGoalIsManual && HasFactoryRackRows());
	const FVector WorldStart = GetActorLocation();
	const float MapToWorldYaw = FMath::DegreesToRadians(GetActorRotation().Yaw) - GetNavigationHeading(*S);
	const float HalfWidth = GetNavigationHalfWidth();
	const float Radius = FMath::Max(HalfWidth + 25.f, 100.f);
	if (ActiveWorkStation != INDEX_NONE)
	{
		const FVector WorldOffset = WorkStations[ActiveWorkStation].WorldPosition - WorldStart;
		NavigationGoal = Start + NavRotate(FVector2D(WorldOffset.X, WorldOffset.Y), -MapToWorldYaw);
	}

	TSet<FIntPoint> Blocked, Known;
	const int32 Padding = FMath::CeilToInt((HalfWidth + 25.f) / 40.f);
	if (!bFactoryRoute) for (const auto& Entry : S->GetCells())
	{
		const FIntPoint C = NavCell(FVector2D(Entry.Key.X+.5,Entry.Key.Y+.5)*S->CellCm);
		if (Entry.Value < 0) Known.Add(C);
		if (Entry.Value >= 3)
			for (int32 X=-Padding; X<=Padding; ++X)
				for (int32 Y=-Padding; Y<=Padding; ++Y)
					if (X*X+Y*Y<=Padding*Padding) Blocked.Add(C+FIntPoint(X,Y));
	}
	const FIntPoint Begin=NavCell(Start), End=NavCell(NavigationGoal);
	const double Now = GetWorld()->GetTimeSeconds();
	for (auto It = NavigationAvoidUntil.CreateIterator(); It; ++It)
	{
		if (It.Value() <= Now) It.RemoveCurrent();
		else if (It.Key() != Begin && It.Key() != End) Blocked.Add(It.Key());
	}
	TMap<FIntPoint, bool> CheckedAisles;
	const auto CanTraverse = [&](const FIntPoint& Cell)
	{
		if (Blocked.Contains(Cell)) return false;
		if (!bFactoryRoute) return Known.Contains(Cell);
		if (const bool* Cached = CheckedAisles.Find(Cell)) return *Cached;
		const FVector2D Offset = NavRotate(NavCentre(Cell) - Start, MapToWorldYaw);
		const bool bClear = IsFactoryAisleClear(WorldStart + FVector(Offset.X, Offset.Y, 0.f), Radius);
		CheckedAisles.Add(Cell, bClear);
		return bClear;
	};
	if (Blocked.Contains(Begin) || !CanTraverse(End)) return false;
	TArray<FIntPoint> Open; Open.Add(Begin);
	TSet<FIntPoint> Closed;
	TMap<FIntPoint,float> Cost; Cost.Add(Begin,0.f);
	TMap<FIntPoint,FIntPoint> Parent;
	const FIntPoint Neighbours[] = {FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1), FIntPoint(1,1), FIntPoint(1,-1), FIntPoint(-1,1), FIntPoint(-1,-1)};
	for (int32 Iter=0; Iter<10000 && !Open.IsEmpty(); ++Iter)
	{
		int32 Best=0; float BestF=BIG_NUMBER;
		for (int32 I=0; I<Open.Num(); ++I)
		{
			const FIntPoint C=Open[I];
			const int32 DX = FMath::Abs(C.X - End.X), DY = FMath::Abs(C.Y - End.Y);
			const float F = Cost[C] + FMath::Max(DX, DY) + .41421356f * FMath::Min(DX, DY);
			if (F<BestF) { BestF=F; Best=I; }
		}
		const FIntPoint Current=Open[Best]; Open.RemoveAtSwap(Best);
		if (Current==End)
		{
			FIntPoint C=End;
			while (C!=Begin) { NavigationRoute.Insert(NavCentre(C),0); C=Parent[C]; }
			NavigationRoute.Add(NavigationGoal);
			return true;
		}
		Closed.Add(Current);
		for (FIntPoint D:Neighbours)
		{
			const FIntPoint Next=Current+D;
			if (Closed.Contains(Next) || !CanTraverse(Next)) continue;
			const bool bDiagonal = D.X != 0 && D.Y != 0;
			if (bDiagonal)
			{
				const FIntPoint SideX = Current + FIntPoint(D.X, 0);
				const FIntPoint SideY = Current + FIntPoint(0, D.Y);
				if (!CanTraverse(SideX) || !CanTraverse(SideY)) continue;
			}
			const float G = Cost[Current] + (bDiagonal ? 1.41421356f : 1.f);
			if (!Cost.Contains(Next)) { Open.Add(Next); Cost.Add(Next,G); Parent.Add(Next,Current); }
			else if (G<Cost[Next]) { Cost[Next]=G; Parent[Next]=Current; }
		}
	}
	return false;
}

void AAutomatedForklift::UpdateNavigation(float Dt)
{
	const auto* TrackingSlam = FindComponentByClass<UForkliftSlamComponent>();
	const double FrameTime = GetWorld()->GetTimeSeconds();
	if (TrackingSlam && NavigationSensor && NavigationSensor->bScanEnabled && TrackingSlam->IsTrackingFresh(FrameTime))
	{
		FactoryPoseGeneration = TrackingSlam->GetMapGeneration();
		FactoryMapToWorldYaw = FMath::DegreesToRadians(GetActorRotation().Yaw) - TrackingSlam->GetPredictedYaw();
		const FVector2D Position(GetActorLocation().X, GetActorLocation().Y);
		FactoryMapOriginInWorld = Position - NavRotate(BodyPose(TrackingSlam, NavigationSensor), FactoryMapToWorldYaw);
	}
	if (!bNavigationActive)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (PendingWorkStation != INDEX_NONE)
		{
			if (!FMath::IsNearlyZero(DriveInput) || !FMath::IsNearlyZero(SteerInput) ||
				!FMath::IsNearlyZero(LiftInput) || !FMath::IsNearlyZero(ReachInput) || !FMath::IsNearlyZero(SideShiftInput))
			{
				PendingWorkStation = INDEX_NONE;
				NavigationStatus = TEXT("구역 이동 대기 취소: 수동 조작");
			}
		}
		if (Now >= NextWorkStationSearch)
		{
			NextWorkStationSearch = Now + 1.f;
			const auto* Slam = FindComponentByClass<UForkliftSlamComponent>();
			if (NavigationSensor && NavigationSensor->bScanEnabled && Slam && Slam->IsTrackingFresh(Now))
			{
				const FVector2D Start = BodyPose(Slam, NavigationSensor);
				ResolveWorkStations(*Slam, Start);
				if (PendingWorkStation != INDEX_NONE && IsWorkStationRegistered(PendingWorkStation))
					GoToStation(PendingWorkStation);
			}
		}
		return;
	}
	DriveInput=SteerInput=0.f;
	const auto* S=FindComponentByClass<UForkliftSlamComponent>();
	const float Now=GetWorld()->GetTimeSeconds();
	if (S && NavigationMapGeneration != S->GetMapGeneration() && ActiveWorkStation != INDEX_NONE)
	{
		const int32 ResumeStation = ActiveWorkStation;
		StopNavigation();
		PendingWorkStation = ResumeStation;
		NextWorkStationSearch = 0.f;
		NavigationStatus = FString::Printf(TEXT("%s 이동 대기: 지도 자동 복구 후 경로 재계산"), *WorkStationLabel(ResumeStation));
		return;
	}
	if (!S || !NavigationSensor || !NavigationSensor->bScanEnabled || NavigationMapGeneration!=S->GetMapGeneration())
	{
		StopNavigation(); NavigationStatus=TEXT("중지: 센서 꺼짐 또는 지도 초기화"); return;
	}
	const bool bTrackingFresh = S->IsTrackingFresh(Now);
	const bool bFactoryNavigation = HasFactoryRackRows() && (ActiveWorkStation != INDEX_NONE || !bNavigationGoalIsManual);
	const bool bUseFactoryOdometry = !bTrackingFresh && bFactoryNavigation && FactoryPoseGeneration == S->GetMapGeneration();
	if (bUsingFactoryOdometry != bUseFactoryOdometry)
	{
		bUsingFactoryOdometry = bUseFactoryOdometry;
		NavigationReplanTime = 0.f;
		NavigationProgressTime = Now;
	}
	const bool bBridgingScan = !bTrackingFresh && !bUsingFactoryOdometry && NavigationRecoveryStart < 0.f && bNavigationGoalSet &&
		!NavigationRoute.IsEmpty() && S->CanBridgeRejectedScan(Now);
	const double ScanAge = Now - NavigationSensor->GetLatestScan().TimestampSeconds;
	const double ScanTimeout = NavigationSensor->GetScanTimeout();
	if ((!bTrackingFresh && !bBridgingScan && !bUsingFactoryOdometry) || ScanAge > ScanTimeout || Dt > .15f)
	{
		if (Dt > .15f)
			NavigationStatus = FString::Printf(TEXT("정지: 프레임 지연 %.0fms (한도 150ms)"), Dt * 1000.f);
		else if (ScanAge > ScanTimeout)
			NavigationStatus = FString::Printf(TEXT("정지: 라이다 스캔 지연 %.2fs (한도 %.2fs)"), ScanAge, ScanTimeout);
		else
			NavigationStatus = FString::Printf(TEXT("정지: SLAM 정합 대기 — %s"), *S->GetStatus());
		if (NavigationTrackingWaitStart < 0.f)
		{
			NavigationTrackingWaitStart = Now;
			UE_LOG(LogTemp, Warning, TEXT("Navigation hold: %s | load=%d lift=%.1f reach=%.1f"),
				*NavigationStatus, int32(CarriedPallet || bIsLoaded), LiftCm, ReachCm);
		}
		if (NavigationRecoveryStart >= 0.f && Now - NavigationRecoveryStart > 8.f)
		{
			StopNavigation();
			NavigationStatus=TEXT("복구 중지: 센서 또는 위치 추적 회복 실패 — SLAM 상태 확인 필요");
		}
		return;
	}
	if (NavigationTrackingWaitStart>=0.f)
	{
		NavigationProgressTime=Now;
		NavigationTrackingWaitStart=-1.f;
	}
	const bool bCarryingLoad = CarriedPallet || bIsLoaded;
	if (bCarryingLoad && !SupportedLoadActors.IsEmpty() && !bTransportHeightAdjusted && !IsSupportedLoadFullyInserted())
	{
		const int32 WaitingStation = ActiveWorkStation;
		StopNavigation();
		if (WaitingStation != INDEX_NONE) PendingWorkStation = WaitingStation;
		NavigationStatus = TEXT("주행 대기: 포크를 팔레트 끝까지 넣은 뒤 운반 높이로 올리세요");
		return;
	}
	const float TransportHeightLimit = bCarryingLoad ? 60.f : 20.f;
	if (LiftCm > TransportHeightLimit || (!bCarryingLoad && ReachCm > 3.f) ||
		!FMath::IsNearlyZero(LiftInput) || !FMath::IsNearlyZero(ReachInput) || !FMath::IsNearlyZero(SideShiftInput))
	{
		StopNavigation();
		if (LiftCm > TransportHeightLimit)
			NavigationStatus = FString::Printf(TEXT("정지: 포크 %.1fcm → %.0fcm 이하로 낮추세요"), LiftCm, TransportHeightLimit);
		else if (!bCarryingLoad && ReachCm > 3.f)
			NavigationStatus = FString::Printf(TEXT("정지: 적재 미감지, 리치 %.1fcm → 3cm 이하로 접으세요"), ReachCm);
		else
			NavigationStatus = TEXT("정지: 포크 높이·리치·좌우 조작을 놓으세요");
		UE_LOG(LogTemp, Warning, TEXT("Navigation transport guard: %s"), *NavigationStatus);
		return;
	}
	const FVector2D WorldPosition(GetActorLocation().X, GetActorLocation().Y);
	const FVector2D Pose = bUsingFactoryOdometry
		? NavRotate(WorldPosition - FactoryMapOriginInWorld, -FactoryMapToWorldYaw)
		: BodyPose(S, NavigationSensor);
	if (NavigationRecoveryStart >= 0.f)
	{
		const int32 FreshScans = S->GetScanCount() - NavigationRecoveryScan;
		if (Now - NavigationRecoveryStart < 1.f || FreshScans < 2)
		{
			NavigationStatus=FString::Printf(TEXT("복구 %d/3: 재스캔 %d/2 — %s"), NavigationRecoveryAttempts, FMath::Min(FreshScans, 2), *NavigationRecoveryReason);
			if (Now - NavigationRecoveryStart > 8.f)
			{
				StopNavigation(); NavigationStatus=TEXT("복구 중지: 새로운 LiDAR 스캔 시간 초과");
			}
			return;
		}
		NavigationRecoveryStart=-1.f;
		if (!PlanNavigationRoute(Pose))
		{
			BeginNavigationRecovery(TEXT("목표로 연결되는 우회 경로 없음"));
			return;
		}
		NavigationReplanTime=Now+1.f;
		NavigationProgressPosition=Pose;
		NavigationProgressTime=Now;
		NavigationStatus=TEXT("재스캔 완료: 새 경로 주행 재개");
	}
	if (bNavigationGoalSet && FVector2D::Distance(Pose,NavigationGoal)<70.f)
	{
		if (bNavigationGoalIsManual)
		{
			const int32 ArrivedStation = ActiveWorkStation;
			if (ArrivedStation != INDEX_NONE)
			{
				const float HeadingError = FMath::UnwindRadians(WorkStations[ArrivedStation].Heading - GetNavigationHeading(*S));
				if (FMath::Abs(HeadingError) > FMath::DegreesToRadians(5.f))
				{
					if (!bBridgingScan)
					{
						const float MaxTurnRate = bCarryingLoad || bUsingFactoryOdometry ? 15.f : 20.f;
						const float TurnRate = FMath::Clamp(FMath::RadiansToDegrees(HeadingError) * 2.f, -MaxTurnRate, MaxTurnRate);
						if (WouldForkRotationHitFixedObstacle(TurnRate * Dt))
						{
							if (!TryNavigationBackOut(TEXT("구역 도착 방향 정렬 공간 부족")))
							{
								StopNavigation();
								NavigationStatus = TEXT("구역 도착 방향 정렬 중지: 전후 공간 부족");
							}
							return;
						}
						SteerInput = TurnRate / FMath::Max(1.f, Motion.SteeringRateDegPerSec);
						NavigationStatus = TEXT("구역 도착 방향 정렬 중");
					}
					return;
				}
			}
			StopNavigation();
			if (bCarryingLoad && !SupportedLoadActors.IsEmpty()) BeginTransportUnload(ArrivedStation);
			else NavigationStatus = ArrivedStation != INDEX_NONE ? FString::Printf(TEXT("%s 선반 앞 도착"), *WorkStationLabel(ArrivedStation)) : TEXT("지정한 목표 위치에 도착했습니다");
			return;
		}
		ExplorationVisited.Add(NavigationGoal);
		if (ExplorationVisited.Num()>6) ExplorationVisited.RemoveAt(0);
		bNavigationGoalSet=false;
		ExplorationBlockedGoals=0;
	}
	if (FVector2D::Distance(Pose,NavigationProgressPosition)>15.f)
	{
		NavigationProgressPosition=Pose; NavigationProgressTime=Now;
		NavigationRecoveryAttempts=0;
	}
	if (bNavigationGoalSet && Now-NavigationProgressTime>12.f)
	{
		if (!TryNavigationBackOut(TEXT("12초간 이동 진행 없음"))) BeginNavigationRecovery(TEXT("12초간 이동 진행 없음"));
		return;
	}
	if (!bNavigationGoalSet)
	{
		if (!SelectExplorationGoal(Pose))
		{
			StopNavigation(); NavigationStatus=TEXT("순찰 정지: 현재 폭으로 연결된 안전 통로를 찾지 못했습니다"); return;
		}
	}
	else if (!bBridgingScan && Now>=NavigationReplanTime)
	{
		NavigationReplanTime=Now+1.f;
		if (!PlanNavigationRoute(Pose))
		{
			if (bNavigationGoalIsManual)
			{
				BeginNavigationRecovery(TEXT("목표 통로가 막힘"));
				return;
			}
			ExplorationBlockedUntil.Add(NavCell(NavigationGoal),Now+30.0);
			bNavigationGoalSet=false;
			NavigationStatus=TEXT("경로 변경: 다른 탐색 지점 선택 중");
			return;
		}
	}	while (NavigationRoute.Num()>1 && FVector2D::Distance(Pose,NavigationRoute[0])<70.f) NavigationRoute.RemoveAt(0);
	if (NavigationRoute.IsEmpty()) return;
	// SLAM map yaw is relative to the scan/map origin. Actor yaw is an absolute
	// world angle and can have a different zero, so use the SLAM heading to
	// rotate a map-frame route point into the vehicle's local scan frame.
	FVector2D LookAhead=NavigationRoute[0];
    // Follow up to 150 cm along a straight segment; retain the corner waypoint.
    for (int32 I=1; I<NavigationRoute.Num(); ++I)
    {
        if (I>1 && FVector2D::DotProduct((NavigationRoute[I]-NavigationRoute[I-1]).GetSafeNormal(),
            (NavigationRoute[I-1]-NavigationRoute[I-2]).GetSafeNormal())<.95) break;
        LookAhead=NavigationRoute[I];
        if (FVector2D::Distance(Pose,LookAhead)>=150.f) break;
    }
    const float HeadingNow = GetNavigationHeading(*S);
    const FVector2D Target=NavRotate(LookAhead-Pose,-HeadingNow);
	TArray<UBoxComponent*> Parts = {BodyCollision, LeftSupportLegCollision, RightSupportLegCollision,
		LeftForkCollision, RightForkCollision, LidarMastOccluder};
	GetSupportedPalletShapes(Parts);

	struct FLocalPart
	{
		FTransform Frame;
		FVector Extent;
	};
	TArray<FLocalPart, TInlineAllocator<16>> LocalParts;
	FBox Bounds(ForceInit);
	for (const UBoxComponent* Part : Parts)
	{
		FTransform Frame = Part->GetComponentTransform().GetRelativeTransform(GetActorTransform());
		Frame.SetScale3D(FVector::OneVector);
		const FVector Extent = Part->GetScaledBoxExtent();
		LocalParts.Add({Frame, Extent});
		Bounds += FBox(-Extent, Extent).TransformBy(Frame);
	}

	struct FLocalObstacle
	{
		FVector Position;
		double RequiredClearance;
	};
	const auto ClearanceFromParts = [&LocalParts](const FVector& Point)
	{
		double Clearance = TNumericLimits<double>::Max();
		for (const FLocalPart& Part : LocalParts)
		{
			const FVector Local = Part.Frame.InverseTransformPositionNoScale(Point);
			const FVector Delta = Local.GetAbs() - Part.Extent;
			Clearance = FMath::Min(Clearance, Delta.GetMax());
		}
		return Clearance;
	};
	TArray<FLocalObstacle> Obstacles;
	const auto& Scan = NavigationSensor->GetLatestScan();
	const FBox Nearby = Bounds.ExpandBy(175.0);
	for (const auto& Ray : Scan.Samples)
	{
		if (!Ray.bValid || Ray.ReturnKind != ELidarReturnKind::ExternalHit) continue;
		const FVector BodyPoint = Scan.SensorToBody.TransformPosition(Ray.LocalPointCm);
		if (BodyPoint.Z <= NavigationSensor->GetGroundHeightInBody() + 6.f) continue;
		const FVector2D MapPoint = S->GetPosition() + NavRotate(FVector2D(Ray.LocalPointCm.X, Ray.LocalPointCm.Y), S->GetYaw());
		const FVector2D XY = bUsingFactoryOdometry ? FVector2D(BodyPoint.X, BodyPoint.Y) : NavRotate(MapPoint - Pose, -HeadingNow);
		const FVector Point(XY.X, XY.Y, BodyPoint.Z);
		if (!Nearby.IsInsideOrOn(Point)) continue;
		Obstacles.Add({Point, FMath::Clamp(ClearanceFromParts(Point), 0.0, 15.0)});
	}
	float Best=-BIG_NUMBER, BestSpeed=0.f, BestRate=0.f;
	for (const int32 HorizonSteps : {25, 8})
	{
		for (float Speed:{0.f, 8.f, (bBridgingScan || S->GetConfidence()<.45f) ? 15.f : 30.f, (bBridgingScan || S->GetConfidence()<.45f) ? 30.f : 60.f})
		for (float Rate:{-30.f,-15.f,0.f,15.f,30.f})
		{
			if (Speed==0.f && Rate==0.f) continue;
			if (HorizonSteps == 8 && Speed > 8.f) continue;

			if (bBridgingScan && (Speed > 15.f || FMath::Abs(Rate) > 15.f)) continue;
			FVector2D P=FVector2D::ZeroVector; float A=0.f; bool Clear=true;
			for (int32 Step=0; Step < HorizonSteps && Clear; ++Step)
			{
				P+=NavRotate(FVector2D(Speed*.1f,0),A);
				A+=FMath::DegreesToRadians(Rate)*.1f;
				for (const FLocalObstacle& Obstacle : Obstacles)
				{
					const FVector2D Q = NavRotate(FVector2D(Obstacle.Position.X, Obstacle.Position.Y) - P, -A);
					if (ClearanceFromParts(FVector(Q.X, Q.Y, Obstacle.Position.Z)) < Obstacle.RequiredClearance) { Clear = false; break; }
				}
			}
			if (!Clear) continue;
			const FVector2D Remaining=Target-P;
			const float Heading=FMath::Abs(FMath::UnwindRadians(FMath::Atan2(Remaining.Y,Remaining.X)-A));
			// Prefer a safe translating arc. A large heading weight made the
			// planner choose a stationary spin repeatedly, even when forward
			// motion was clear. Keep pivot turns for targets well off the nose.
			const float PivotPenalty=Speed==0.f ? 35.f : 0.f;
			const float Score=(Target.Size()-Remaining.Size())-Heading*30.f-FMath::Abs(Rate)*.1f-PivotPenalty;
			if (Score>Best) { Best=Score; BestSpeed=Speed; BestRate=Rate; }
		}
			if (Best != -BIG_NUMBER) break;
	}
	if (Best == -BIG_NUMBER)
	{
		const FString Reason = FString::Printf(TEXT("전진·회전 공간 부족 (%d개 근접 측정점, 적재 %s)"),
			Obstacles.Num(), bCarryingLoad ? TEXT("감지") : TEXT("미감지"));
		if (!TryNavigationBackOut(Reason)) BeginNavigationRecovery(Reason);
		return;
	}
	// Simulation collision guard is separate from sensor-based planning.
	// Sweep all physical parts at small increments, including yaw, to stop for
	// objects hidden by self-occlusion and low pallets missed between beams.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NavigationGuard),false,this);
	if (CarriedPallet) Params.AddIgnoredActor(CarriedPallet.Get());
	for (const auto& Pallet : SupportedLoadActors) if (Pallet.IsValid()) Params.AddIgnoredActor(Pallet.Get());
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic); Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	for (int32 I=1; I<=5; ++I)
	{
		const float T=Dt*I/5.f;
		const FQuat Rotation=FQuat(FVector::UpVector,FMath::DegreesToRadians(BestRate*T))*GetActorQuat();
		const FVector Translation=GetActorLocation()+GetActorForwardVector()*BestSpeed*T;
		const FTransform Proposed(Rotation,Translation,GetActorScale3D());
		for (const UBoxComponent* Part:Parts)
		{
			const FTransform Relative=Part->GetComponentTransform().GetRelativeTransform(GetActorTransform());
			const FTransform End=Relative*Proposed;
			FHitResult Hit;
			if (GetWorld()->SweepSingleByObjectType(Hit,Part->GetComponentLocation(),End.GetLocation(),End.GetRotation(),Objects,
				FCollisionShape::MakeBox(Part->GetScaledBoxExtent()),Params))
			{
				const FString Reason = FString::Printf(TEXT("충돌 방지: %s"), *GetNameSafe(Hit.GetActor()));
				if (!TryNavigationBackOut(Reason)) BeginNavigationRecovery(Reason);
				return;
			}
		}
	}
	DriveInput=BestSpeed/FMath::Max(Motion.ForwardSpeedCmPerSec,1.f);
	SteerInput=BestRate/FMath::Max(Motion.SteeringRateDegPerSec,1.f);
	NavigationStatus=FString::Printf(TEXT("%s: 전진 %.0fcm/s / 조향 %.0f°/s"),bUsingFactoryOdometry ? TEXT("Factory 위치로 주행") : (bNavigationGoalIsManual ? TEXT("목표 주행") : TEXT("랜덤 순찰")),BestSpeed,BestRate);
}

bool AAutomatedForklift::SelectExplorationGoal(FVector2D Start)
{
	const auto* S=FindComponentByClass<UForkliftSlamComponent>();
	if (!S) return false;
	if (!bRackRowsScanned) ResolveWorkStations(*S, Start);
	const bool bFactoryRoute = HasFactoryRackRows();
	const float HalfWidth = GetNavigationHalfWidth();
	const float Radius = FMath::Max(HalfWidth + 25.f, 100.f);
	const FVector WorldStart = GetActorLocation();
	const float MapToWorldYaw = FMath::DegreesToRadians(GetActorRotation().Yaw) - GetNavigationHeading(*S);
	TMap<FIntPoint, bool> CheckedAisles;

	TSet<FIntPoint> Known, Blocked;
	const int32 Padding=FMath::CeilToInt((HalfWidth + 25.f) / 40.f);
	if (!bFactoryRoute) for (const auto& E:S->GetCells())
	{
		const FIntPoint C=NavCell(FVector2D(E.Key.X+.5,E.Key.Y+.5)*S->CellCm);
		if (E.Value<0) Known.Add(C);
		if (E.Value>=3)
			for (int32 X=-Padding; X<=Padding; ++X)
				for (int32 Y=-Padding; Y<=Padding; ++Y)
					if (X*X+Y*Y<=Padding*Padding) Blocked.Add(C+FIntPoint(X,Y));
	}
	const auto CanTraverse = [&](const FIntPoint& Cell)
	{
		if (!bFactoryRoute) return Known.Contains(Cell) && !Blocked.Contains(Cell);
		if (const bool* Cached = CheckedAisles.Find(Cell)) return *Cached;
		const FVector2D Offset = NavRotate(NavCentre(Cell) - Start, MapToWorldYaw);
		const bool bClear = IsFactoryAisleClear(WorldStart + FVector(Offset.X, Offset.Y, 0.f), Radius);
		CheckedAisles.Add(Cell, bClear);
		return bClear;
	};
	const FIntPoint Begin=NavCell(Start);
	if (Blocked.Contains(Begin)) return false;
	TArray<FIntPoint> Queue; Queue.Add(Begin);
	TMap<FIntPoint,FIntPoint> Parent;
	TMap<FIntPoint,int32> Distance; Distance.Add(Begin,0);
	const FIntPoint Dirs[]={FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)};
	FIntPoint Best=Begin, Fallback=Begin;
	int32 CandidateCount=0, FallbackCount=0;
	const double Now=GetWorld()->GetTimeSeconds();
	for (auto It=ExplorationBlockedUntil.CreateIterator(); It; ++It)
		if (It.Value()<=Now) It.RemoveCurrent();
	for (int32 I=0; I<Queue.Num() && I<12000; ++I)
	{
		const FIntPoint C=Queue[I];
		for (FIntPoint D:Dirs)
		{
			const FIntPoint N=C+D;
			if (!Distance.Contains(N) && (!bFactoryRoute || Distance[C] < 24) && CanTraverse(N))
			{
				Distance.Add(N,Distance[C]+1); Parent.Add(N,C); Queue.Add(N);
			}
		}
		const FVector2D Point=NavCentre(C);
		const float Travel=FVector2D::Distance(Start,Point);
		if (Travel < (bFactoryRoute ? 120.f : 250.f) || Travel > 1200.f) continue;
		bool RecentlyBlocked=false;
		for (const auto& Entry:ExplorationBlockedUntil)
			if (FVector2D::Distance(NavCentre(Entry.Key),Point)<200.f) { RecentlyBlocked=true; break; }
		if (RecentlyBlocked) continue;
		// Reservoir sampling gives each reachable free cell equal probability.
		// Revisit old destinations when necessary so patrol can continue.
		if (FMath::RandHelper(++FallbackCount)==0) Fallback=C;
		bool Visited=false;
		for (FVector2D Old:ExplorationVisited)
			if (FVector2D::Distance(Old,Point)<200.f) { Visited=true; break; }
		if (!Visited && FMath::RandHelper(++CandidateCount)==0) Best=C;
	}
	if (Best==Begin) Best=Fallback;
	if (Best==Begin) return false;
	NavigationGoal=NavCentre(Best);
	bNavigationGoalSet=true;
	bNavigationGoalIsManual=false;
	NavigationRoute.Reset();
	for (FIntPoint C=Best; C!=Begin; C=Parent[C]) NavigationRoute.Insert(NavCentre(C),0);
	NavigationProgressPosition=Start;
	NavigationProgressTime=GetWorld()->GetTimeSeconds();
	NavigationReplanTime=NavigationProgressTime+1.f;
	return true;
}
