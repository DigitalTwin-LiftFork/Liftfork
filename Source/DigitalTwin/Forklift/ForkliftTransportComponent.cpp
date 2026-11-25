#include "Forklift/ForkliftTransportComponent.h"
#include "Forklift/AutomatedForklift.h"
#include "Forklift/Pallet.h"
#include "Forklift/WoodPallet.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IHttpRequest.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "TimerManager.h"
#include "Forklift/ForkliftSlamComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UForkliftTransportComponent::UForkliftTransportComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = .1f;
}

void UForkliftTransportComponent::BeginPlay()
{
	Super::BeginPlay();
	Forklift = Cast<AAutomatedForklift>(GetOwner());
	BatteryPercent = FMath::Clamp(InitialBatteryPercent, 0.f, 100.f);
	FParse::Value(FCommandLine::Get(), TEXT("BatteryInitialPercent="), BatteryPercent);
	BatteryPercent = FMath::Clamp(BatteryPercent, 0.f, 100.f);
	Session = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (Forklift.IsValid())
		GetWorld()->GetTimerManager().SetTimer(PollTimer, this, &UForkliftTransportComponent::Sync, .5f, true, 1.f);
}

void UForkliftTransportComponent::TickComponent(float Dt, ELevelTick Type, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, Type, Function);
	if (Forklift.IsValid() && !bAreasBuilt) { Forklift->ScanRackRows(); BuildFactoryAreas(); }
	UpdateBattery(Dt);
}

bool UForkliftTransportComponent::IsChargingMotionLocked() const
{
	return bContactsEngaged || ChargeState == TEXT("connecting") || ChargeState == TEXT("disconnecting");
}

void UForkliftTransportComponent::BeginDisconnect(const FString& Reason)
{
	// Open the electrical contactor first; only then release the mechanical shoes.
	DeliveredChargePowerKW = 0.f;
	bChargeContactorClosed = false;
	bChargeRequested = false;
	bDockChargeInhibited = true;
	DisconnectSeconds = 0.f;
	ChargeState = TEXT("disconnecting");
	ChargeMessage = Reason + TEXT(" · 전류 차단 후 접점 해제 중");
}

void UForkliftTransportComponent::ApplyChargingInterlock()
{
	AAutomatedForklift* Truck = Forklift.Get();
	if (!Truck || !IsChargingMotionLocked()) return;
	if (ChargeState != TEXT("disconnecting") &&
		(FMath::Abs(Truck->DriveInput) > .01f || FMath::Abs(Truck->SteerInput) > .01f ||
		FMath::Abs(Truck->LiftInput) > .01f || FMath::Abs(Truck->ReachInput) > .01f))
		BeginDisconnect(TEXT("수동 조작 요청"));
	Truck->DriveInput = Truck->SteerInput = Truck->LiftInput = Truck->ReachInput = Truck->SideShiftInput = 0.f;
	Truck->AutoDriveInput = Truck->AutoSteerInput = Truck->AutoLiftInput = Truck->AutoReachInput = Truck->AutoSideShiftInput = 0.f;
}

void UForkliftTransportComponent::RequestCharging(bool bCancel)
{
	AAutomatedForklift* Truck = Forklift.Get();
	if (!Truck) return;
	if (bCancel)
	{
		if (ChargeState == TEXT("returning")) Truck->StopNavigation();
		bAutoChargeSuppressed = bDockChargeInhibited = true;
		if (IsChargingMotionLocked()) { BeginDisconnect(TEXT("충전 취소")); return; }
		bChargeRequested = false; ChargeState = TEXT("idle");
		ChargeMessage = TEXT("충전 요청 취소: 수동 주행 가능"); return;
	}
	if (ChargeState == TEXT("disconnecting")) return;
	bChargeRequested = true;
	bAutoChargeSuppressed = bDockChargeInhibited = false;
	ChargeRetryAt = 0.f;
	if (ChargeState != TEXT("returning") && ChargeState != TEXT("charging") && ChargeState != TEXT("connecting")) ChargeState = TEXT("waiting");
	ChargeMessage = TEXT("접점 충전 요청: 도킹 및 승인 조건 확인 중");
}

void UForkliftTransportComponent::UpdateBattery(float Dt)
{
	AAutomatedForklift* Truck = Forklift.Get();
	if (!Truck || !bAreasBuilt) return;
	for (const FFactoryArea& Area : FactoryAreas)
		if (Area.Id == TEXT("CHARGING")) ChargingDock = FVector(Area.Bounds.GetCenter().X, Area.Bounds.GetCenter().Y + 100.f, Truck->GetActorLocation().Z);
	const FVector Offset = Truck->GetActorLocation() - ChargingDock;
	const float Distance = FVector::Dist2D(ChargingDock, Truck->GetActorLocation());
	const float HeadingError = FMath::Abs(FMath::FindDeltaAngleDegrees(Truck->GetActorRotation().Yaw, -90.f));
	const float RequiredContactExtensionCm = Truck->GetActorLocation().Z - Truck->BodyCollision->GetScaledBoxExtent().Z + 5.f - ChargingFloorZ - 6.f;
	// Spring-loaded shoe travel is bounded; it cannot bridge an arbitrary height.
	const bool bHeightAligned = RequiredContactExtensionCm >= -2.f && RequiredContactExtensionCm <= 12.f;
	for (const auto& WeakShoe : VehicleContactShoes)
		if (UStaticMeshComponent* Shoe = WeakShoe.Get())
		{
			FVector Local = Shoe->GetRelativeLocation();
			Local.Z = -Truck->BodyCollision->GetScaledBoxExtent().Z + 5.f - (bContactsEngaged ? FMath::Clamp(RequiredContactExtensionCm, 0.f, 12.f) : 0.f);
			Shoe->SetRelativeLocation(Local);
		}
	const bool bAligned = bStationContactsAvailable && bHeightAligned && FMath::Abs(Offset.X) <= ContactLateralToleranceCm && FMath::Abs(Offset.Y) <= ContactLongitudinalToleranceCm && HeadingError <= ContactYawToleranceDeg;
	const bool bStopped = FMath::Abs(Truck->CurrentForwardSpeedCmPerSec) < .5f && FMath::Abs(Truck->DriveInput) < .01f && FMath::Abs(Truck->AutoDriveInput) < .01f;
	const bool bSafe = Truck->SupportedLoadActors.IsEmpty() && Truck->LiftCm <= 30.f && Truck->ReachCm <= 5.f &&
		!Truck->IsNavigationActive() && !Truck->IsAutoPickupMotionActive() && Truck->PendingWorkStation == INDEX_NONE &&
		Truck->TransportPhase == AAutomatedForklift::ETransportPhase::None && Truck->RackDropPhase == AAutomatedForklift::ERackDropPhase::Idle;
	const bool bApproved = bStationContactsAvailable && bChargerPowered && bBmsChargeAllowed && BatteryTemperatureC >= 0.f && BatteryTemperatureC <= 45.f;
	const float SimSeconds = FMath::Max(0.f, Dt) * FMath::Max(1.f, BatteryTimeScale);
	DeliveredChargePowerKW = 0.f;
	if (Distance > 250.f) bDockChargeInhibited = false;
	if (ChargeState == TEXT("disconnecting"))
	{
		DisconnectSeconds += Dt;
		if (DisconnectSeconds >= 1.f)
		{
			bContactsEngaged = false; ChargeState = TEXT("idle");
			ChargeMessage = TEXT("접점 해제 완료: 주행 가능");
		}
		return;
	}
	if (ChargeState == TEXT("connecting") || ChargeState == TEXT("charging"))
	{
		if (!bAligned || !bStopped || !bSafe || !bApproved)
		{ BeginDisconnect(!bApproved ? TEXT("충전기/BMS 승인 해제 또는 온도 범위 이탈") : TEXT("도킹 접점 또는 정지 조건 해제")); return; }
		if (ChargeState == TEXT("connecting"))
		{
			ContactDwellSeconds += Dt;
			ChargeMessage = TEXT("접점 감지: 정지 유지 및 충전기/BMS 승인 확인 중");
			if (ContactDwellSeconds >= 2.f) { bChargeContactorClosed = true; ChargeState = TEXT("charging"); }
			return;
		}
		// Approximate Li-ion CC/CV behaviour: taper above 80% SOC. Not a hardware BMS.
		const float Taper = BatteryPercent <= 80.f ? 1.f : FMath::Clamp((100.f - BatteryPercent) / 20.f, .05f, 1.f);
		DeliveredChargePowerKW = bChargeContactorClosed ? ChargePowerKW * Taper : 0.f;
		BatteryPercent = FMath::Min(ChargeTargetPercent, BatteryPercent + DeliveredChargePowerKW * SimSeconds / (3600.f * FMath::Max(1.f, BatteryCapacityKWh)) * 100.f);
		ChargeMessage = FString::Printf(TEXT("접점 충전 %s %.1fkW / %.0fA · %.1f%%"), BatteryPercent <= 80.f ? TEXT("CC") : TEXT("CV 근사"), DeliveredChargePowerKW, DeliveredChargePowerKW * 1000.f / 48.f, BatteryPercent);
		if (BatteryPercent >= ChargeTargetPercent) BeginDisconnect(TEXT("목표 잔량 충전 완료"));
		return;
	}
	const float PowerKW = .25f + FMath::Abs(Truck->CurrentForwardSpeedCmPerSec) / 195.f * 3.f + FMath::Abs(Truck->LiftInput) * 5.f + FMath::Abs(Truck->ReachInput);
	BatteryPercent = FMath::Max(0.f, BatteryPercent - PowerKW * SimSeconds / (3600.f * FMath::Max(1.f, BatteryCapacityKWh)) * 100.f);
	if (!bChargeRequested && !bDockChargeInhibited && bStopped && bSafe && BatteryPercent < ChargeTargetPercent - 2.f && Distance < 200.f)
		RequestCharging(false);
	if (!bChargeRequested && BatteryPercent <= LowBatteryPercent && !bAutoChargeSuppressed) RequestCharging(false);
	if (!bChargeRequested) return;
	if (ChargeState == TEXT("returning"))
	{
		if (Truck->IsNavigationActive())
		{
			// Local contact feedback completes docking even if SLAM is stale or
			// the navigation controller is waiting on a tighter pose threshold.
			if (bAligned && bStopped && Truck->SupportedLoadActors.IsEmpty() &&
				Truck->LiftCm <= 30.f && Truck->ReachCm <= 5.f && !Truck->IsAutoPickupMotionActive() &&
				Truck->TransportPhase == AAutomatedForklift::ETransportPhase::None &&
				Truck->RackDropPhase == AAutomatedForklift::ERackDropPhase::Idle)
			{
				Truck->StopNavigation();
				ChargeMessage = TEXT("접점 정렬 확인: 주행 종료 후 충전 승인 확인");
				return;
			}
			ChargeMessage = FString::Printf(TEXT("접점 도킹: 오차 좌우 %.1fcm / 전후 %.1fcm / 방향 %.1f도"), FMath::Abs(Offset.X), FMath::Abs(Offset.Y), HeadingError);
			return;
		}
		if (!bAligned) { bChargeRequested = false; bAutoChargeSuppressed = bDockChargeInhibited = true; ChargeState = TEXT("failed"); ChargeMessage = TEXT("도킹 정렬 미완료: ") + Truck->NavigationStatus; return; }
	}
	ChargeState = TEXT("waiting");
	if (!JobId.IsEmpty() && !bJobTerminal) { ChargeMessage = TEXT("충전 대기: 현재 운반 작업 완료 후 복귀"); return; }
	if (!Truck->SupportedLoadActors.IsEmpty()) { ChargeMessage = TEXT("충전 대기: 팔레트를 내려놓으세요"); return; }
	if (!bSafe) { ChargeMessage = TEXT("충전 대기: 자동 동작 종료 / 포크 30cm 이하 / 리치 복귀 필요"); return; }
	if (!bStopped) { ChargeMessage = TEXT("충전 대기: 주행을 멈추세요"); return; }
	if (!bApproved) { ChargeMessage = TEXT("충전 대기: 충전기 전원·BMS 승인·배터리 온도(0~45C) 확인 필요"); return; }
	if (BatteryPercent >= ChargeTargetPercent) { bChargeRequested = false; ChargeState = TEXT("idle"); ChargeMessage = TEXT("배터리 충분: 충전 불필요"); return; }
	if (bAligned)
	{
		bContactsEngaged = true; bChargeContactorClosed = false; ContactDwellSeconds = 0.f;
		ChargeState = TEXT("connecting"); ChargeMessage = TEXT("접점 연결: 충전기/BMS 승인 확인 중"); return;
	}
	const auto* Slam = Truck->FindComponentByClass<UForkliftSlamComponent>();
	if (!Slam || !Slam->IsTrackingFresh(GetWorld()->GetTimeSeconds())) { ChargeMessage = TEXT("충전 대기: LiDAR 위치 추적 준비 중"); return; }
	if (GetWorld()->GetTimeSeconds() < ChargeRetryAt) return;
	ChargeRetryAt = GetWorld()->GetTimeSeconds() + 5.f;
	Truck->GoToServicePoint(ChargingDock);
	if (Truck->IsNavigationActive()) { ChargeState = TEXT("returning"); ChargeMessage = TEXT("접점 충전소 정밀 도킹 경로 설정됨"); }
	else { bChargeRequested = false; bAutoChargeSuppressed = bDockChargeInhibited = true; ChargeState = TEXT("failed"); ChargeMessage = TEXT("충전 경로 없음: ") + Truck->NavigationStatus; }
}

void UForkliftTransportComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(PollTimer);
	Super::EndPlay(Reason);
}

FString UForkliftTransportComponent::PalletId(const AActor* Actor) const
{
	if (const APallet* Pallet = Cast<APallet>(Actor)) return Pallet->PalletId.ToString();
	if (const AWoodPallet* Pallet = Cast<AWoodPallet>(Actor)) return Pallet->PalletId.ToString();
	return FString();
}

void UForkliftTransportComponent::SetPalletIds()
{
	TArray<AActor*> Actors;
	TSet<FName> Used;
	for (TActorIterator<APallet> It(GetWorld()); It; ++It) Actors.Add(*It);
	for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It)
		if (!It->IsPlacementPreview()) Actors.Add(*It);
	Actors.Sort([](const AActor& A, const AActor& B) { return A.GetName() < B.GetName(); });
	for (AActor* Actor : Actors)
	{
		const FName Id(*PalletId(Actor));
		if (!Id.IsNone()) Used.Add(Id);
	}
	TSet<FName> Claimed;
	for (AActor* Actor : Actors)
	{
		FName& Id = Cast<APallet>(Actor) ? Cast<APallet>(Actor)->PalletId : Cast<AWoodPallet>(Actor)->PalletId;
		if (!Id.IsNone() && !Claimed.Contains(Id)) { Claimed.Add(Id); continue; }
		int32 Number = 1;
		FName Candidate;
		do { Candidate = FName(*FString::Printf(TEXT("P-%03d"), Number++)); } while (Used.Contains(Candidate));
		Id = Candidate;
		Used.Add(Id);
		Claimed.Add(Id);
	}
	for (AActor* Actor : Actors)
	{
		TArray<UTextRenderComponent*> Texts;
		Actor->GetComponents(Texts);
		UTextRenderComponent* Label = nullptr;
		for (UTextRenderComponent* Text : Texts)
			if (Text->ComponentHasTag(TEXT("TransportPalletId"))) { Label = Text; break; }
		if (!Label)
		{
			Label = NewObject<UTextRenderComponent>(Actor);
			Actor->AddInstanceComponent(Label);
			Label->ComponentTags.Add(TEXT("TransportPalletId"));
			Label->SetupAttachment(Actor->GetRootComponent());
			Label->SetRelativeLocation(FVector(0, -65, 130));
			Label->SetRelativeRotation(FRotator(0, -90, 0));
			Label->SetWorldSize(20.f);
			Label->SetHorizontalAlignment(EHTA_Center);
			Label->SetTextRenderColor(FColor::Yellow);
			Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Label->RegisterComponent();
		}
		Label->SetText(FText::FromString(PalletId(Actor)));
	}
}

void UForkliftTransportComponent::FinishJob(bool bSuccess, const FString& Message)
{
	if (JobId.IsEmpty() || bJobTerminal) return;
	JobStatus = bSuccess ? TEXT("completed") : TEXT("failed");
	JobMessage = Message;
	bJobTerminal = true;
	UE_LOG(LogTemp, Log, TEXT("Transport job %s %s: %s"), *JobId, *JobStatus, *Message);
}

FString UForkliftTransportComponent::FindFactoryArea(const FVector& Position) const
{
	for (const FFactoryArea& Area : FactoryAreas)
		if (Position.X >= Area.Bounds.Min.X && Position.X <= Area.Bounds.Max.X &&
			Position.Y >= Area.Bounds.Min.Y && Position.Y <= Area.Bounds.Max.Y) return Area.Label;
	return TEXT("통로 / 구역 밖");
}

void UForkliftTransportComponent::BuildFactoryAreas()
{
	AAutomatedForklift* Truck = Forklift.Get();
	if (bAreasBuilt || !Truck || !Truck->RackRows[0].IsValid || !Truck->RackRows[5].IsValid) return;
	FBox Building(ForceInit);
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		if (It->GetActorNameOrLabel() == TEXT("Factory_Floor")) Building = It->GetComponentsBoundingBox();
	if (!Building.IsValid) return;
	const double Floor = Building.Max.Z;
	ChargingFloorZ = Floor;
	int32 StationRails = 0;
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		if (It->GetActorNameOrLabel().StartsWith(TEXT("FactoryZone_CHARGING_ContactRail"))) ++StationRails;
	bStationContactsAvailable = StationRails == 2;
	if (!bVehicleContactsCreated)
	{
		for (float Side : {-1.f, 1.f})
		{
			UStaticMeshComponent* Shoe = NewObject<UStaticMeshComponent>(Truck);
			Truck->AddInstanceComponent(Shoe); Shoe->SetupAttachment(Truck->GetRootComponent());
			Shoe->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Shoe->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DigitalTwin/FactoryAssets/M_SafetyOrange.M_SafetyOrange")));
			Shoe->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Shoe->SetRelativeLocation(FVector(0, Side * 30.f, -Truck->BodyCollision->GetScaledBoxExtent().Z + 5.f));
			Shoe->SetRelativeScale3D(FVector(.3f, .08f, .04f)); Shoe->RegisterComponent();
            VehicleContactShoes.Add(Shoe);
		}
		bVehicleContactsCreated = true;
	}
	const FVector Origin = Building.GetCenter();
	const double FrontY = Building.Min.Y + 1300.f;
	// Keep the central +/-6 m vehicle aisle and the east pedestrian walkway clear.
	const double XOffsets[] = {-5500, -3400, -1300, 2500, 5100};
	const TCHAR* Ids[] = {TEXT("INBOUND"), TEXT("INSPECTION"), TEXT("PRODUCTION"), TEXT("OUTBOUND"), TEXT("CHARGING")};
	const TCHAR* Labels[] = {TEXT("입고장"), TEXT("검사·보류장"), TEXT("생산 투입장"), TEXT("출고장"), TEXT("충전·주차장")};
	const TCHAR* Purposes[] = {TEXT("입고 팔레트 접수 및 집기 대기"), TEXT("검사 대기 및 보류품 분리"), TEXT("생산라인 자재 인계 및 작업 대기"), TEXT("완제품 출하 순서별 대기"), TEXT("작업 종료 후 주차 및 충전 대기")};
	const FColor Colors[] = {FColor::Cyan, FColor::Orange, FColor::Green, FColor::Yellow, FColor(100, 160, 255)};
	for (int32 I = 0; I < 5; ++I)
	{
		const FVector C(Origin.X + XOffsets[I], FrontY, Floor);
		FactoryAreas.Add({Ids[I], Labels[I], Purposes[I], FBox(C - FVector(700, 700, 5), C + FVector(700, 700, 350)), Colors[I]});
	}
	FBox Raw = Truck->RackRows[0]; Raw += Truck->RackRows[1]; Raw += Truck->RackRows[2];
	FBox Finished = Truck->RackRows[3]; Finished += Truck->RackRows[4]; Finished += Truck->RackRows[5];
	FactoryAreas.Add({TEXT("RAW"), TEXT("원자재 보관 A~C"), TEXT("원자재 팔레트 선반 보관 / 생산 공급"), Raw, FColor::Cyan});
	FactoryAreas.Add({TEXT("FINISHED"), TEXT("완제품 보관 D~F"), TEXT("완제품 팔레트 선반 보관 / 출고 공급"), Finished, FColor::Yellow});
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (It->ActorHasTag(TEXT("FactoryZoneLayout")))
		{
			bAreasBuilt = true;
			UE_LOG(LogTemp, Log, TEXT("Factory zones loaded from saved map: 7 zones"));
			return;
		}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Yellow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DigitalTwin/FactoryAssets/M_Yellow.M_Yellow"));
	UMaterialInterface* Green = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DigitalTwin/FactoryAssets/M_Walkway.M_Walkway"));
	UMaterialInterface* Steel = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/DigitalTwin/FactoryAssets/M_Steel.M_Steel"));
	const auto Box = [&](const FVector& Position, const FVector& Size, UMaterialInterface* Material, bool bCollision = false)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Truck);
		Truck->AddInstanceComponent(Mesh);
		Mesh->SetAbsolute(true, true, true);
		Mesh->SetStaticMesh(Cube);
		if (Material) Mesh->SetMaterial(0, Material);
		Mesh->SetCollisionProfileName(bCollision ? TEXT("BlockAll") : TEXT("NoCollision"));
		Mesh->SetWorldLocation(Position);
		Mesh->SetWorldScale3D(Size / 100.f);
		Mesh->RegisterComponent();
	};
	const auto Sign = [&](const FString& Text, const FVector& Position, const FColor& Color, float Size)
	{
		for (float Yaw : {90.f})
		{
			UTextRenderComponent* TextComponent = NewObject<UTextRenderComponent>(Truck);
			Truck->AddInstanceComponent(TextComponent);
			TextComponent->SetAbsolute(true, true, true);
			TextComponent->SetText(FText::FromString(Text));
			TextComponent->SetWorldSize(Size);
			TextComponent->SetTextRenderColor(Color);
			TextComponent->SetHorizontalAlignment(EHTA_Center);
			TextComponent->SetWorldLocationAndRotation(Position, FRotator(0, Yaw, 0));
			TextComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			TextComponent->RegisterComponent();
		}
	};
	for (int32 I = 0; I < FactoryAreas.Num(); ++I)
	{
		const FFactoryArea& Area = FactoryAreas[I];
		const FVector C = Area.Bounds.GetCenter();
		Sign(I < 5 ? Area.Id : I == 5 ? TEXT("RAW MATERIALS / A-C") : TEXT("FINISHED GOODS / D-F"),
			FVector(C.X, I < 5 ? C.Y - 550 : Area.Bounds.Min.Y - 100, Floor + 200), Area.Color, 48.f);
		if (I >= 5) continue;
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			const bool bAcross = Edge < 2;
			Box(FVector(C.X + (bAcross ? 0 : Edge == 2 ? -700 : 700), C.Y + (bAcross ? Edge == 0 ? -700 : 700 : 0), Floor + 1),
				bAcross ? FVector(1400, 8, 2) : FVector(8, 1400, 2), I == 4 ? Green : Yellow);
		}
		// Two numbered pallet bays; the parking zone uses one wider vehicle bay.
		const int32 BayCount = I == 4 ? 1 : 2;
		for (int32 Bay = 0; Bay < BayCount; ++Bay)
		{
			const double X = C.X + (BayCount == 1 ? 0 : Bay == 0 ? -300 : 300);
			const float HalfWidth = BayCount == 1 ? 180.f : 90.f;
			for (float Side : {-1.f, 1.f})
			{
				Box(FVector(X + Side * HalfWidth, C.Y + 100, Floor + 2), FVector(5, 400, 2), Yellow);
				Box(FVector(X, C.Y + 100 + Side * 200, Floor + 2), FVector(HalfWidth * 2, 5, 2), Yellow);
			}
			Sign(FString::Printf(TEXT("%s-%02d"), *Area.Id, Bay + 1), FVector(X, C.Y + 320, Floor + 70), Area.Color, 24.f);
		}
		// Place equipment along the rear edge, away from the pallet approach.
		if (I == 1 || I == 2) Box(FVector(C.X, C.Y - 500, Floor + 55), FVector(350, 100, 110), Steel, true);
		if (I == 4) Box(FVector(C.X, C.Y - 500, Floor + 65), FVector(100, 60, 130), Steel, true);
	}
	bAreasBuilt = true;
	UE_LOG(LogTemp, Log, TEXT("Single-floor factory ready: 7 zones, 216 rack slots, central aisle preserved"));
}

void UForkliftTransportComponent::Sync()
{
	AAutomatedForklift* Truck = Forklift.Get();
	if (!bServerEnabled || !Truck || bRequestPending) return;
	SetPalletIds();
	Truck->ScanRackRows();
	BuildFactoryAreas();
	if (!JobId.IsEmpty() && !bJobTerminal && bDispatched)
	{
		if (Truck->RackDropPhase != AAutomatedForklift::ERackDropPhase::Idle)
		{
			bSawDrop = true;
			JobStatus = Truck->RackDropPhase == AAutomatedForklift::ERackDropPhase::Withdraw ||
				Truck->RackDropPhase == AAutomatedForklift::ERackDropPhase::Stow ? TEXT("withdrawing") : TEXT("unloading");
		}
		else if (!Truck->bNavigationActive && Truck->PendingWorkStation == INDEX_NONE)
			FinishJob(false, Truck->NavigationStatus);
		else if (Truck->bNavigationActive) JobStatus = TEXT("moving");
	}
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("session"), Session);
	Payload->SetStringField(TEXT("message"), Truck->NavigationStatus);
	FString Loaded;
	if (Truck->SupportedLoadActors.Num() == 1) Loaded = PalletId(Truck->SupportedLoadActors[0].Get());
	Payload->SetStringField(TEXT("loaded"), Loaded);
	const bool bReady = !Loaded.IsEmpty() && !Truck->IsNavigationActive() && Truck->PendingWorkStation == INDEX_NONE &&
		Truck->TransportPhase == AAutomatedForklift::ETransportPhase::None && !Truck->IsAutoPickupMotionActive() &&
		!bChargeRequested && !IsChargingMotionLocked() && BatteryPercent > LowBatteryPercent && Truck->LiftCm >= 15.f && Truck->LiftCm <= 60.f &&
		Truck->RackDropPhase == AAutomatedForklift::ERackDropPhase::Idle;
	Payload->SetBoolField(TEXT("ready"), bReady);
	Payload->SetStringField(TEXT("charge_ack"), LastChargeCommand);
	TSharedRef<FJsonObject> Battery = MakeShared<FJsonObject>();
	Battery->SetNumberField(TEXT("percent"), BatteryPercent);
	Battery->SetNumberField(TEXT("distance_to_dock_cm"), FVector::Dist2D(ChargingDock, Truck->GetActorLocation()));
	Battery->SetNumberField(TEXT("low_threshold"), LowBatteryPercent);
	Battery->SetNumberField(TEXT("target"), ChargeTargetPercent);
	Battery->SetNumberField(TEXT("time_scale"), BatteryTimeScale);
	Battery->SetNumberField(TEXT("power_kw"), DeliveredChargePowerKW);
	Battery->SetNumberField(TEXT("current_a"), DeliveredChargePowerKW * 1000.f / 48.f);
	Battery->SetBoolField(TEXT("contacts_connected"), bContactsEngaged);
	Battery->SetBoolField(TEXT("contactor_closed"), bChargeContactorClosed);
	Battery->SetBoolField(TEXT("charger_powered"), bChargerPowered);
    Battery->SetBoolField(TEXT("station_contacts_available"), bStationContactsAvailable);
	Battery->SetBoolField(TEXT("bms_allowed"), bBmsChargeAllowed);
	Battery->SetNumberField(TEXT("temperature_c"), BatteryTemperatureC);
	Battery->SetStringField(TEXT("state"), ChargeState);
	Battery->SetStringField(TEXT("message"), ChargeMessage);
	Payload->SetObjectField(TEXT("battery"), Battery);

	TArray<AActor*> Pallets;
	for (TActorIterator<APallet> It(GetWorld()); It; ++It) Pallets.Add(*It);
	for (TActorIterator<AWoodPallet> It(GetWorld()); It; ++It)
		if (!It->IsPlacementPreview()) Pallets.Add(*It);
	TMap<AActor*, FString> Locations;
	TArray<TSharedPtr<FJsonValue>> Slots;
	for (int32 Row = 0; Row < AAutomatedForklift::WorkStationCount; ++Row)
		for (int32 Slot = 0; Slot < AAutomatedForklift::RackSlotCount; ++Slot)
		{
			const int32 Tier = Slot / AAutomatedForklift::RackBayCount;
			const int32 Bay = Slot % AAutomatedForklift::RackBayCount;
			const TOptional<FBox>& Deck = Tier == 0 ? Truck->RackBayDecks[Row][Bay] : Truck->RackUpperDecks[Row][Bay][Tier - 1];
			if (!Deck.IsSet()) continue;
			const FString SlotId = FString::Printf(TEXT("%c-%02d"), TCHAR('A' + Row), Slot + 1);
			FString Occupant;
			for (AActor* Pallet : Pallets)
			{
				if (Truck->IsSupportedPallet(Pallet)) continue;
				const FVector P = Pallet->GetActorLocation();
				if (P.X >= Deck->Min.X && P.X <= Deck->Max.X && P.Y >= Deck->Min.Y && P.Y <= Deck->Max.Y &&
					P.Z >= Deck->Max.Z - 5.f && P.Z <= Deck->Max.Z + 20.f)
				{
					Occupant = PalletId(Pallet);
					Locations.Add(Pallet, SlotId);
				}
			}
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("id"), SlotId);
			Entry->SetBoolField(TEXT("occupied"), !Occupant.IsEmpty());
			Entry->SetStringField(TEXT("pallet"), Occupant);
			Slots.Add(MakeShared<FJsonValueObject>(Entry));
		}
	Payload->SetArrayField(TEXT("slots"), Slots);
	TArray<TSharedPtr<FJsonValue>> Areas;
	for (const FFactoryArea& Area : FactoryAreas)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), Area.Id);
		Entry->SetStringField(TEXT("name"), Area.Label);
		Entry->SetStringField(TEXT("purpose"), Area.Purpose);
		Entry->SetStringField(TEXT("color"), TEXT("#") + Area.Color.ToHex().Left(6));
		Entry->SetNumberField(TEXT("minX"), Area.Bounds.Min.X);
		Entry->SetNumberField(TEXT("minY"), Area.Bounds.Min.Y);
		Entry->SetNumberField(TEXT("maxX"), Area.Bounds.Max.X);
		Entry->SetNumberField(TEXT("maxY"), Area.Bounds.Max.Y);
		Areas.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Payload->SetArrayField(TEXT("areas"), Areas);
	TArray<TSharedPtr<FJsonValue>> Inventory;
	for (AActor* Pallet : Pallets)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), PalletId(Pallet));
		Entry->SetStringField(TEXT("location"), Truck->IsSupportedPallet(Pallet) ? TEXT("지게차 적재") :
			Locations.Contains(Pallet) ? Locations[Pallet] + (Locations[Pallet][0] <= TCHAR('C') ? TEXT(" / 원자재 보관") : TEXT(" / 완제품 보관")) : FindFactoryArea(Pallet->GetActorLocation()));
		Inventory.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Payload->SetArrayField(TEXT("pallets"), Inventory);
	if (!JobId.IsEmpty())
	{
		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("id"), JobId);
		Report->SetStringField(TEXT("status"), JobStatus);
		Report->SetStringField(TEXT("message"), bJobTerminal ? JobMessage : Truck->NavigationStatus);
		Payload->SetObjectField(TEXT("report"), Report);
	}
	FString Body;
	FJsonSerializer::Serialize(Payload, TJsonWriterFactory<>::Create(&Body));
	auto Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ServerUrl + TEXT("/api/sync"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);
	Request->SetTimeout(2.f);
	bRequestPending = true;
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
		{
			bRequestPending = false;
			if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() != 200)
			{
				ConnectionStatus = TEXT("운반 서버 연결 대기 / 끊김");
				if (Forklift.IsValid() && !JobId.IsEmpty() && !bJobTerminal)
				{
					Forklift->StopNavigation();
					Forklift->DriveInput = Forklift->SteerInput = Forklift->LiftInput = Forklift->ReachInput = 0.f;
					Forklift->TargetForwardSpeedCmPerSec = Forklift->CurrentForwardSpeedCmPerSec = Forklift->CurrentYawRateDegPerSec = 0.f;
					FinishJob(false, TEXT("운반 서버 연결 끊김: 작업 중단"));
				}
				return;
			}
			TSharedPtr<FJsonObject> Result;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Result))
				ApplyResponse(Result);
		});
	if (!Request->ProcessRequest())
	{
		bRequestPending = false;
		if (!JobId.IsEmpty() && !bJobTerminal)
		{
			Truck->StopNavigation();
			Truck->DriveInput = Truck->SteerInput = Truck->LiftInput = Truck->ReachInput = 0.f;
			FinishJob(false, TEXT("운반 서버 요청 시작 실패"));
		}
	}
}

void UForkliftTransportComponent::ApplyResponse(const TSharedPtr<FJsonObject>& Response)
{
	AAutomatedForklift* Truck = Forklift.Get();
	if (!Truck || !Response.IsValid()) return;
	ConnectionStatus = TEXT("운반 서버 연결됨 · ") + ServerUrl;
	const TSharedPtr<FJsonObject>* ChargingCommand = nullptr;
	if (Response->TryGetObjectField(TEXT("charging_command"), ChargingCommand) && ChargingCommand && ChargingCommand->IsValid())
	{
		const FString CommandId = (*ChargingCommand)->GetStringField(TEXT("id"));
		if (CommandId != LastChargeCommand)
		{
			LastChargeCommand = CommandId;
			RequestCharging((*ChargingCommand)->GetStringField(TEXT("action")) == TEXT("cancel"));
		}
	}
	FString Ack;
	if (bJobTerminal && Response->TryGetStringField(TEXT("ack"), Ack) && Ack == JobId)
	{
		JobId.Reset();
		bJobTerminal = bDispatched = bSawDrop = false;
	}
	const TSharedPtr<FJsonObject>* Job = nullptr;
	if (!Response->TryGetObjectField(TEXT("job"), Job) || !Job || !Job->IsValid())
	{
		if (!JobId.IsEmpty() && !bJobTerminal)
		{
			Truck->StopNavigation();
			Truck->DriveInput = Truck->SteerInput = Truck->LiftInput = Truck->ReachInput = 0.f;
			FinishJob(false, TEXT("서버 작업 배정 해제: 현장 확인 필요"));
		}
		return;
	}
	const FString Id = (*Job)->GetStringField(TEXT("id"));
	if (!JobId.IsEmpty() && Id != JobId) return;
	if ((*Job)->GetIntegerField(TEXT("cancel_requested")) != 0)
	{
		JobId = Id;
		Truck->StopNavigation();
		Truck->DriveInput = Truck->SteerInput = Truck->LiftInput = Truck->ReachInput = 0.f;
		FinishJob(false, TEXT("작업 화면에서 취소 요청"));
		JobStatus = TEXT("cancelled");
		return;
	}
	if (bDispatched) return;
	const FString Pallet = (*Job)->GetStringField(TEXT("pallet"));
	const FString Destination = (*Job)->GetStringField(TEXT("destination"));
	JobId = Id;
	JobPallet = Pallet;
	JobStatus = TEXT("assigned");
	if (Truck->SupportedLoadActors.Num() != 1 || PalletId(Truck->SupportedLoadActors[0].Get()) != Pallet ||
		Destination.Len() != 4 || Destination[1] != TCHAR('-') ||
		Truck->IsNavigationActive() || Truck->PendingWorkStation != INDEX_NONE ||
		Truck->RackDropPhase != AAutomatedForklift::ERackDropPhase::Idle ||
		Truck->TransportPhase != AAutomatedForklift::ETransportPhase::None ||
		bChargeRequested || IsChargingMotionLocked() || BatteryPercent <= LowBatteryPercent || Truck->IsAutoPickupMotionActive() || Truck->LiftCm < 15.f || Truck->LiftCm > 60.f)
	{
		FinishJob(false, TEXT("배정 팔레트 또는 목적지 확인 실패"));
		return;
	}
	const int32 Row = Destination[0] - TCHAR('A');
	const int32 Slot = FCString::Atoi(*Destination.Mid(2)) - 1;
	if (Row < 0 || Row >= 6 || Slot < 0 || Slot >= 36)
	{
		FinishJob(false, TEXT("목적지 번호 범위 오류"));
		return;
	}
	bDispatched = true;
	bSawDrop = false;
	Truck->GoToStation(Row, Slot);
	JobStatus = TEXT("moving");
	UE_LOG(LogTemp, Log, TEXT("Transport job assigned: %s pallet=%s destination=%s"), *Id, *Pallet, *Destination);
}
