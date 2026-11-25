#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ForkliftTransportComponent.generated.h"

class AAutomatedForklift;
class FJsonObject;

/** Local dispatch client. Vehicle collision and fork interlocks remain local. */
UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent))
class DIGITALTWIN_API UForkliftTransportComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UForkliftTransportComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void FinishJob(bool bSuccess, const FString& Message);
	UPROPERTY(EditAnywhere, Category="Transport") bool bServerEnabled = true;
	UPROPERTY(EditAnywhere, Category="Transport") FString ServerUrl = TEXT("http://127.0.0.1:8765");
	const FString& GetConnectionStatus() const { return ConnectionStatus; }
private:
	void Sync();
	void ApplyResponse(const TSharedPtr<FJsonObject>& Response);
	void BuildFactoryAreas();
	void SetPalletIds();
	FString PalletId(const AActor* Actor) const;
	FTimerHandle PollTimer;
	FString Session;
	FString JobId;
	FString JobPallet;
	FString JobStatus;
	FString JobMessage;
	FString ConnectionStatus = TEXT("운반 서버 연결 대기");
	bool bRequestPending = false;
	bool bJobTerminal = false;
	bool bDispatched = false;
	bool bSawDrop = false;
	bool bAreasBuilt = false;
	TWeakObjectPtr<AAutomatedForklift> Forklift;
};

