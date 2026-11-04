#include "Forklift/AutomatedForklift.h"
#include "Forklift/ForkliftNavigationSensorComponent.h"
#include "Forklift/ForkliftSlamComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

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

void AAutomatedForklift::GetNavigationDiagnosticLines(TArray<FString>& OutLines) const
{
	const auto* S = FindComponentByClass<UForkliftSlamComponent>();
	const float Now = GetWorld()->GetTimeSeconds();
	OutLines.Add(FString::Printf(TEXT("주행 상태: %s"), bNavigationActive ? TEXT("자동 주행 활성") : TEXT("정지 / 수동")));
	OutLines.Add(FString::Printf(TEXT("실제 속도 %.2f m/s | 명령 %.2f m/s"), CurrentForwardSpeedCmPerSec / 100.f, TargetForwardSpeedCmPerSec / 100.f));
	OutLines.Add(bNavigationGoalSet && S && NavigationSensor ? FString::Printf(TEXT("목표까지 %.2f m | 경로점 %d"), FVector2D::Distance(BodyPose(S, NavigationSensor), NavigationGoal) / 100.f, NavigationRoute.Num()) : TEXT("목표: 미지정"));
	OutLines.Add(S ? FString::Printf(TEXT("위치 신뢰도 %.0f%% | 추적 %s"), S->GetConfidence() * 100.f, S->IsTrackingFresh(Now) ? TEXT("정상") : TEXT("대기")) : TEXT("SLAM 센서 없음"));
	const double Stamp = NavigationSensor ? NavigationSensor->GetLatestScan().TimestampSeconds : -1.;
	OutLines.Add(Stamp >= 0. ? FString::Printf(TEXT("최근 스캔 %.2f초 전 | 복구 시도 %d/3"), Now - Stamp, NavigationRecoveryAttempts) : TEXT("첫 스캔 대기"));
	OutLines.Add(FString::Printf(TEXT("상태 / 정지 이유: %s"), *NavigationStatus));
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
	if (bNavigationActive) DriveInput = SteerInput = 0.f;
	bNavigationActive = false;
	NavigationRecoveryStart = -1.f;
	NavigationRoute.Reset();
	bNavigationGoalSet=false;
	bNavigationGoalIsManual=false;
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
	const FVector2D MapPose = S->GetPosition();
	const FVector2D Offset((Centre.Y-ScreenPosition.Y)/Scale,(ScreenPosition.X-Centre.X)/Scale);
	const FIntPoint GoalCell = NavCell(MapPose+Offset);
	NavigationGoal = NavCentre(GoalCell);
	bNavigationGoalIsManual = true;
	bNavigationGoalSet = true;
	bNavigationActive = true;
	NavigationRecoveryStart = -1.f;
	NavigationRecoveryAttempts = 0;
	NavigationAvoidUntil.Reset();
	NavigationRoute.Reset();
	NavigationMapGeneration = S->GetMapGeneration();
	NavigationTrackingWaitStart = -1.f;
	if (!S->IsTrackingFresh(GetWorld()->GetTimeSeconds()))
	{
		NavigationStatus = TEXT("목표 설정됨: SLAM 위치 추적 대기");
		return true;
	}
	const FVector2D Start = BodyPose(S,NavigationSensor);
	if (!PlanNavigationRoute(Start))
	{
		bNavigationActive = false;
		bNavigationGoalSet = false;
		bNavigationGoalIsManual = false;
		NavigationStatus = TEXT("목표 불가: 지도에서 탐색된 빈 구역을 선택하세요");
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
	if (bNavigationActive) { StopNavigation(); return; }
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
bool AAutomatedForklift::PlanNavigationRoute(FVector2D Start)
{
	const auto* S = FindComponentByClass<UForkliftSlamComponent>();
	NavigationRoute.Reset();
	if (!S) return false;
	TSet<FIntPoint> Blocked, Known;
	const int32 Padding = FMath::CeilToInt((Dimensions.MaximumWidthCm*.5f+25.f)/40.f);
	for (const auto& Entry : S->GetCells())
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
	if (Blocked.Contains(Begin) || Blocked.Contains(End) || !Known.Contains(End)) return false;
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
			if (Closed.Contains(Next)||Blocked.Contains(Next)||!Known.Contains(Next)) continue;
			const bool bDiagonal = D.X != 0 && D.Y != 0;
			if (bDiagonal)
			{
				const FIntPoint SideX = Current + FIntPoint(D.X, 0);
				const FIntPoint SideY = Current + FIntPoint(0, D.Y);
				if (Blocked.Contains(SideX) || Blocked.Contains(SideY) ||
					!Known.Contains(SideX) || !Known.Contains(SideY)) continue;
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
	if (!bNavigationActive) return;
	DriveInput=SteerInput=0.f;
	const auto* S=FindComponentByClass<UForkliftSlamComponent>();
	const float Now=GetWorld()->GetTimeSeconds();
	if (!S || !NavigationSensor || !NavigationSensor->bScanEnabled || NavigationMapGeneration!=S->GetMapGeneration())
	{
		StopNavigation(); NavigationStatus=TEXT("중지: 센서 꺼짐 또는 지도 초기화"); return;
	}
	const bool bTrackingFresh = S->IsTrackingFresh(Now);
	const bool bBridgingScan = !bTrackingFresh && NavigationRecoveryStart < 0.f && bNavigationGoalSet &&
		!NavigationRoute.IsEmpty() && S->CanBridgeRejectedScan(Now);
	if ((!bTrackingFresh && !bBridgingScan) || Now-NavigationSensor->GetLatestScan().TimestampSeconds>NavigationSensor->GetScanTimeout() || Dt>.15f)
	{
		if (NavigationTrackingWaitStart<0.f) NavigationTrackingWaitStart=Now;
		NavigationStatus=FString::Printf(TEXT("안전 정지: 위치 추정 회복 대기 — %s (%.2fs/frame)"), *S->GetStatus(), Dt);
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
	}	if (CarriedPallet || bIsLoaded || LiftCm>20.f || ReachCm>3.f ||
		!FMath::IsNearlyZero(LiftInput) || !FMath::IsNearlyZero(ReachInput) || !FMath::IsNearlyZero(SideShiftInput))
	{
		StopNavigation(); NavigationStatus=TEXT("중지: 빈 포크를 내리고 리치를 접어주세요"); return;
	}
	const FVector2D Pose=BodyPose(S,NavigationSensor);
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
			StopNavigation();
			NavigationStatus=TEXT("지정한 목표 위치에 도착했습니다");
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
		BeginNavigationRecovery(TEXT("12초간 이동 진행 없음"));
		return;
	}
	if (!bNavigationGoalSet)
	{
		if (!SelectExplorationGoal(Pose))
		{
			StopNavigation(); NavigationStatus=TEXT("순찰 대기 종료: 현재 도달 가능한 랜덤 목표 없음"); return;
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
    const float HeadingNow=S->GetPredictedYaw();
    const FVector2D Target=NavRotate(LookAhead-Pose,-HeadingNow);
	// Full vehicle planar envelope measured from existing collision parts.
	FBox Bounds(ForceInit);
	const UBoxComponent* Parts[]={BodyCollision,LeftSupportLegCollision,RightSupportLegCollision,LeftForkCollision,RightForkCollision,LidarMastOccluder};
	for (const UBoxComponent* Part:Parts)
	{
		const FTransform Relative=Part->GetComponentTransform().GetRelativeTransform(GetActorTransform());
		const FVector E=Part->GetUnscaledBoxExtent();
		for (int32 X:{-1,1}) for (int32 Y:{-1,1}) for (int32 Z:{-1,1})
			Bounds+=Relative.TransformPosition(E*FVector(X,Y,Z));
	}
	TArray<FVector2D> Obstacles;
	const auto& Scan=NavigationSensor->GetLatestScan();
	for (const auto& Ray:Scan.Samples)
	{
		if (!Ray.bValid) continue;
		const FVector P=Scan.SensorToBody.TransformPosition(Ray.LocalPointCm);
		if (P.Z>NavigationSensor->GetGroundHeightInBody()+6.f && P.Z<Bounds.Max.Z+15.f)
			{
            const FVector2D MapPoint=S->GetPosition()+NavRotate(FVector2D(Ray.LocalPointCm.X,Ray.LocalPointCm.Y),S->GetYaw());
            Obstacles.Add(NavRotate(MapPoint-Pose,-HeadingNow));
        }
	}
	float Best=-BIG_NUMBER, BestSpeed=0.f, BestRate=0.f;
	for (float Speed:{0.f, (bBridgingScan || S->GetConfidence()<.45f) ? 15.f : 30.f, (bBridgingScan || S->GetConfidence()<.45f) ? 30.f : 60.f})
		for (float Rate:{-30.f,-15.f,0.f,15.f,30.f})
		{
			if (Speed==0.f && Rate==0.f) continue;
			if (bBridgingScan && (Speed > 15.f || FMath::Abs(Rate) > 15.f)) continue;
			FVector2D P=FVector2D::ZeroVector; float A=0.f; bool Clear=true;
			for (int32 Step=0; Step<25 && Clear; ++Step)
			{
				P+=NavRotate(FVector2D(Speed*.1f,0),A);
				A+=FMath::DegreesToRadians(Rate)*.1f;
				for (FVector2D Obstacle:Obstacles)
				{
					const FVector2D Q=NavRotate(Obstacle-P,-A);
					if (Q.X>Bounds.Min.X-25.f && Q.X<Bounds.Max.X+25.f &&
						Q.Y>Bounds.Min.Y-25.f && Q.Y<Bounds.Max.Y+25.f) { Clear=false; break; }
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
	if (Best==-BIG_NUMBER) { BeginNavigationRecovery(TEXT("안전한 이동 후보 없음")); return; }
	// Simulation collision guard is separate from sensor-based planning.
	// Sweep all physical parts at small increments, including yaw, to stop for
	// objects hidden by self-occlusion and low pallets missed between beams.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NavigationGuard),false,this);
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
				BeginNavigationRecovery(FString::Printf(TEXT("충돌 방지: %s"), *GetNameSafe(Hit.GetActor()))); return;
			}
		}
	}
	DriveInput=BestSpeed/FMath::Max(Motion.ForwardSpeedCmPerSec,1.f);
	SteerInput=BestRate/FMath::Max(Motion.SteeringRateDegPerSec,1.f);
	NavigationStatus=FString::Printf(TEXT("%s: 전진 %.0fcm/s / 조향 %.0f°/s"),bNavigationGoalIsManual ? TEXT("목표 주행") : TEXT("랜덤 순찰"),BestSpeed,BestRate);
}

bool AAutomatedForklift::SelectExplorationGoal(FVector2D Start)
{
	const auto* S=FindComponentByClass<UForkliftSlamComponent>();
	if (!S) return false;
	TSet<FIntPoint> Known, Blocked;
	const int32 Padding=FMath::CeilToInt((Dimensions.MaximumWidthCm*.5f+25.f)/40.f);
	for (const auto& E:S->GetCells())
	{
		const FIntPoint C=NavCell(FVector2D(E.Key.X+.5,E.Key.Y+.5)*S->CellCm);
		if (E.Value<0) Known.Add(C);
		if (E.Value>=3)
			for (int32 X=-Padding; X<=Padding; ++X)
				for (int32 Y=-Padding; Y<=Padding; ++Y)
					if (X*X+Y*Y<=Padding*Padding) Blocked.Add(C+FIntPoint(X,Y));
	}
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
			if (Known.Contains(N) && !Blocked.Contains(N) && !Distance.Contains(N))
			{
				Distance.Add(N,Distance[C]+1); Parent.Add(N,C); Queue.Add(N);
			}
		}
		const FVector2D Point=NavCentre(C);
		const float Travel=FVector2D::Distance(Start,Point);
		if (Travel<250.f || Travel>1200.f) continue;
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
