#include "Forklift/ForkliftTransportComponent.h"
#include "Forklift/AutomatedForklift.h"
#include "Forklift/Pallet.h"
#include "Forklift/WoodPallet.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IHttpRequest.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "TimerManager.h"

UForkliftTransportComponent::UForkliftTransportComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UForkliftTransportComponent::BeginPlay()
{
	Super::BeginPlay();
	Forklift = Cast<AAutomatedForklift>(GetOwner());
	Session = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	if (Forklift.IsValid())
		GetWorld()->GetTimerManager().SetTimer(PollTimer, this, &UForkliftTransportComponent::Sync, .5f, true, 1.f);
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
}

void UForkliftTransportComponent::FinishJob(bool bSuccess, const FString& Message)
{
	if (JobId.IsEmpty() || bJobTerminal) return;
	JobStatus = bSuccess ? TEXT("completed") : TEXT("failed");
	JobMessage = Message;
	bJobTerminal = true;
	UE_LOG(LogTemp, Log, TEXT("Transport job %s %s: %s"), *JobId, *JobStatus, *Message);
}

void UForkliftTransportComponent::BuildFactoryAreas()
{
	AAutomatedForklift* Truck = Forklift.Get();
	if (bAreasBuilt || !Truck || !Truck->RackRows[0].IsValid || !Truck->RackRows[5].IsValid) return;
	bAreasBuilt = true;
	FBox Factory = Truck->RackRows[0];
	Factory += Truck->RackRows[5];
	const double Y = Factory.Min.Y - 850.f;
	const double Floor = Truck->GetActorLocation().Z - Truck->BodyCollision->GetScaledBoxExtent().Z;
	static const TCHAR* Names[] = {TEXT("INBOUND"), TEXT("WAIT"), TEXT("OUTBOUND")};
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (int32 Area = 0; Area < 3; ++Area)
	{
		const double X = Factory.GetCenter().X + (Area - 1) * 1000.f;
		UTextRenderComponent* Sign = NewObject<UTextRenderComponent>(Truck);
		Truck->AddInstanceComponent(Sign);
		Sign->SetAbsolute(true, true, true);
		Sign->SetText(FText::FromString(Names[Area]));
		Sign->SetWorldSize(70.f);
		Sign->SetTextRenderColor(Area == 0 ? FColor::Cyan : Area == 1 ? FColor::Yellow : FColor::Green);
		Sign->SetHorizontalAlignment(EHTA_Center);
		Sign->SetWorldLocationAndRotation(FVector(X, Y, Floor + 120.f), FRotator(0, 90, 0));
		Sign->RegisterComponent();
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			UStaticMeshComponent* Line = NewObject<UStaticMeshComponent>(Truck);
			Truck->AddInstanceComponent(Line);
			Line->SetAbsolute(true, true, true);
			Line->SetStaticMesh(Cube);
			Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			const bool bAcross = Edge < 2;
			Line->SetWorldLocation(FVector(X + (bAcross ? 0 : (Edge == 2 ? -350 : 350)),
				Y + (bAcross ? (Edge == 0 ? -250 : 250) : 0), Floor + 1.f));
			Line->SetWorldScale3D(bAcross ? FVector(7.f, .05f, .02f) : FVector(.05f, 5.f, .02f));
			Line->RegisterComponent();
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Transport factory areas ready: inbound/wait/outbound"));
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
		Truck->LiftCm >= 15.f && Truck->LiftCm <= 60.f &&
		Truck->RackDropPhase == AAutomatedForklift::ERackDropPhase::Idle;
	Payload->SetBoolField(TEXT("ready"), bReady);

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
	TArray<TSharedPtr<FJsonValue>> Inventory;
	for (AActor* Pallet : Pallets)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("id"), PalletId(Pallet));
		Entry->SetStringField(TEXT("location"), Truck->IsSupportedPallet(Pallet) ? TEXT("지게차 적재") :
			Locations.Contains(Pallet) ? Locations[Pallet] : TEXT("바닥 / 입고·대기"));
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
		Truck->IsAutoPickupMotionActive() || Truck->LiftCm < 15.f || Truck->LiftCm > 60.f)
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

