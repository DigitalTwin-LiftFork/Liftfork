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
		return S->GetPosition() - NavRotate(FVector2D(Offset.X,Offset.Y), S->GetYaw());
	}
}

void AAutomatedForklift::StopNavigation()
{
	if (bNavigationActive) DriveInput = SteerInput = 0.f;
	bNavigationActive = false;
	NavigationRoute.Reset();
	bNavigationGoalSet=false;
	NavigationStatus = TEXT("자동 탐색 중지");
}

void AAutomatedForklift::ToggleNavigation()
{
	if (bNavigationActive) { StopNavigation(); return; }
	CancelAutoPickup(TEXT("자동 탐색 선택"), false);
	ClearAutomaticCommands();
	const auto* S=FindComponentByClass<UForkliftSlamComponent>();
	if (S && NavigationMapGeneration!=S->GetMapGeneration()) ExplorationVisited.Reset();
	NavigationMapGeneration=S ? S->GetMapGeneration() : -1;
	bNavigationActive=true;
	bNavigationGoalSet=false;
	NavigationRoute.Reset();
	ExplorationBlockedGoals=0;
	NavigationTrackingWaitStart=-1.f;
	NavigationStatus=TEXT("자동 탐색: 센서와 지도 준비 중");
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
	if (Blocked.Contains(Begin) || Blocked.Contains(End) || !Known.Contains(End)) return false;
	TArray<FIntPoint> Open; Open.Add(Begin);
	TSet<FIntPoint> Closed;
	TMap<FIntPoint,float> Cost; Cost.Add(Begin,0.f);
	TMap<FIntPoint,FIntPoint> Parent;
	const FIntPoint Neighbours[] = {FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)};
	for (int32 Iter=0; Iter<10000 && !Open.IsEmpty(); ++Iter)
	{
		int32 Best=0; float BestF=BIG_NUMBER;
		for (int32 I=0; I<Open.Num(); ++I)
		{
			const FIntPoint C=Open[I];
			const float F=Cost[C]+FMath::Abs(C.X-End.X)+FMath::Abs(C.Y-End.Y);
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
			const float G=Cost[Current]+1.f;
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
	if (!S->IsTrackingFresh(Now) || Now-NavigationSensor->GetLatestScan().TimestampSeconds>.3 || Dt>.15f)
	{
		if (NavigationTrackingWaitStart<0.f) NavigationTrackingWaitStart=Now;
		NavigationStatus=FString::Printf(TEXT("출발 대기: %s (%.2fs/frame)"), *S->GetStatus(), Dt);
		if (Now-NavigationTrackingWaitStart>5.f)
		{
			StopNavigation(); NavigationStatus=FString::Printf(TEXT("출발 중지: %s (%.2fs/frame)"), *S->GetStatus(), Dt);
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
	if (bNavigationGoalSet && FVector2D::Distance(Pose,NavigationGoal)<70.f)
	{
		ExplorationVisited.Add(NavigationGoal);
		bNavigationGoalSet=false;
		ExplorationBlockedGoals=0;
	}
	if (FVector2D::Distance(Pose,NavigationProgressPosition)>30.f)
	{
		NavigationProgressPosition=Pose; NavigationProgressTime=Now;
	}
	if (bNavigationGoalSet && Now-NavigationProgressTime>10.f)
	{
		ExplorationVisited.Add(NavigationGoal);
		bNavigationGoalSet=false;
		if (++ExplorationBlockedGoals>=5)
		{
			StopNavigation(); NavigationStatus=TEXT("중지: 연속된 통로 막힘"); return;
		}
	}
	if (!bNavigationGoalSet)
	{
		if (!SelectExplorationGoal(Pose))
		{
			StopNavigation(); NavigationStatus=TEXT("탐색 종료: 도달 가능한 새 탐색 지점 없음"); return;
		}
	}
	else if (Now>=NavigationReplanTime)
	{
		NavigationReplanTime=Now+1.f;
		if (!PlanNavigationRoute(Pose))
		{
			ExplorationVisited.Add(NavigationGoal);
			bNavigationGoalSet=false;
			NavigationStatus=TEXT("경로 변경: 다른 탐색 지점 선택 중");
			return;
		}
	}	while (NavigationRoute.Num()>1 && FVector2D::Distance(Pose,NavigationRoute[0])<70.f) NavigationRoute.RemoveAt(0);
	if (NavigationRoute.IsEmpty()) return;
	const FVector2D Target=NavRotate(NavigationRoute[0]-Pose,-S->GetYaw());
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
		if (P.Z>Bounds.Min.Z+8.f && P.Z<Bounds.Max.Z+15.f)
			Obstacles.Add(FVector2D(P.X,P.Y));
	}
	float Best=-BIG_NUMBER, BestSpeed=0.f, BestRate=0.f;
	for (float Speed:{0.f,30.f,60.f})
		for (float Rate:{-30.f,-15.f,0.f,15.f,30.f})
		{
			if (Speed==0.f && Rate==0.f) continue;
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
			const float Score=(Target.Size()-Remaining.Size())-Heading*100.f-FMath::Abs(Rate)*.1f;
			if (Score>Best) { Best=Score; BestSpeed=Speed; BestRate=Rate; }
		}
	if (Best==-BIG_NUMBER) { NavigationStatus=TEXT("장애물 대기: 다른 경로 탐색 예정"); return; }
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
				NavigationStatus=TEXT("충돌 방지 정지: 다른 경로 탐색 예정"); return;
			}
		}
	}
	DriveInput=BestSpeed/FMath::Max(Motion.ForwardSpeedCmPerSec,1.f);
	SteerInput=BestRate/FMath::Max(Motion.SteeringRateDegPerSec,1.f);
	NavigationStatus=TEXT("자동 탐색 중: 지도 생성 및 장애물 회피");
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
	FIntPoint Best=Begin; float BestScore=-BIG_NUMBER;
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
		bool Visited=false;
		for (FVector2D Old:ExplorationVisited)
			if (FVector2D::Distance(Old,Point)<200.f) { Visited=true; break; }
		if (Visited) continue;
		int32 Unknown=0;
		for (int32 X=-2; X<=2; ++X) for (int32 Y=-2; Y<=2; ++Y)
			if (!Known.Contains(C+FIntPoint(X,Y)) && !Blocked.Contains(C+FIntPoint(X,Y))) ++Unknown;
		if (Unknown==0) continue;
		const float Score=Unknown*30.f+FMath::Min(Travel,600.f)*.3f-Distance[C]*5.f;
		if (Score>BestScore) { BestScore=Score; Best=C; }
	}
	if (Best==Begin) return false;
	NavigationGoal=NavCentre(Best);
	bNavigationGoalSet=true;
	NavigationRoute.Reset();
	for (FIntPoint C=Best; C!=Begin; C=Parent[C]) NavigationRoute.Insert(NavCentre(C),0);
	NavigationProgressPosition=Start;
	NavigationProgressTime=GetWorld()->GetTimeSeconds();
	NavigationReplanTime=NavigationProgressTime+1.f;
	return true;
}
