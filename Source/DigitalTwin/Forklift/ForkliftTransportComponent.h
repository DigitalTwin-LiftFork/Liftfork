#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ForkliftTransportComponent.generated.h"

class AAutomatedForklift;
class FJsonObject;
class UStaticMeshComponent;

/** Local dispatch client. Vehicle collision and fork interlocks remain local. */
UCLASS(ClassGroup=(DigitalTwin), meta=(BlueprintSpawnableComponent))
class DIGITALTWIN_API UForkliftTransportComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UForkliftTransportComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UPROPERTY(EditAnywhere, Category="Battery", meta=(ClampMin="0", ClampMax="100")) float InitialBatteryPercent = 80.f;
	UPROPERTY(EditAnywhere, Category="Battery", meta=(ClampMin="1")) float BatteryCapacityKWh = 12.f;
	UPROPERTY(EditAnywhere, Category="Battery", meta=(ClampMin="1")) float BatteryTimeScale = 1.f;
	UPROPERTY(EditAnywhere, Category="Battery", meta=(ClampMin="1")) float ChargePowerKW = 12.f;
	UPROPERTY(EditAnywhere, Category="Battery", meta=(ClampMin="5", ClampMax="40")) float LowBatteryPercent = 20.f;
	UPROPERTY(EditAnywhere, Category="Battery", meta=(ClampMin="50", ClampMax="100")) float ChargeTargetPercent = 90.f;
	UPROPERTY(EditAnywhere, Category="Charging station") bool bChargerPowered = true;
	UPROPERTY(EditAnywhere, Category="Charging station") bool bBmsChargeAllowed = true;
	UPROPERTY(EditAnywhere, Category="Charging station") float BatteryTemperatureC = 25.f;
	UPROPERTY(EditAnywhere, Category="Charging station", meta=(ClampMin=".5", ClampMax="10")) float ContactLateralToleranceCm = 2.f;
	UPROPERTY(EditAnywhere, Category="Charging station", meta=(ClampMin="1", ClampMax="20")) float ContactLongitudinalToleranceCm = 5.f;
	UPROPERTY(EditAnywhere, Category="Charging station", meta=(ClampMin=".5", ClampMax="10")) float ContactYawToleranceDeg = 3.f;
	bool IsChargingMotionLocked() const;
	void ApplyChargingInterlock();
	float GetChargePowerKW() const { return DeliveredChargePowerKW; }
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void FinishJob(bool bSuccess, const FString& Message);
	UPROPERTY(EditAnywhere, Category="Transport") bool bServerEnabled = true;
	UPROPERTY(EditAnywhere, Category="Transport") FString ServerUrl = TEXT("http://127.0.0.1:8765");
	const FString& GetConnectionStatus() const { return ConnectionStatus; }
    float GetBatteryPercent() const { return BatteryPercent; }
    const FString& GetChargingState() const { return ChargeState; }
    const FString& GetChargingMessage() const { return ChargeMessage; }
	FString GetBatteryDiagnostic() const { return FString::Printf(TEXT("배터리 %.1f%% | %s"), BatteryPercent, *ChargeMessage); }
private:
	void Sync();
	void UpdateBattery(float Dt);
	void RequestCharging(bool bCancel);
	float BatteryPercent = 80.f;
	FString ChargeState = TEXT("idle");
	FString ChargeMessage = TEXT("배터리 정상");
	FString LastChargeCommand;
	bool bChargeRequested = false, bAutoChargeSuppressed = false;
	float ChargeRetryAt = 0.f;
	FVector ChargingDock = FVector::ZeroVector;
	float ContactDwellSeconds = 0.f, DisconnectSeconds = 0.f;
	float DeliveredChargePowerKW = 0.f;
	bool bContactsEngaged = false, bChargeContactorClosed = false;
    bool bStationContactsAvailable = false;
    float ChargingFloorZ = 0.f;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> VehicleContactShoes;
	bool bDockChargeInhibited = false, bVehicleContactsCreated = false;
	void BeginDisconnect(const FString& Reason);
	void ApplyResponse(const TSharedPtr<FJsonObject>& Response);
	void BuildFactoryAreas();
	void SetPalletIds();
	struct FFactoryArea
	{
		FString Id, Label, Purpose;
		FBox Bounds;
		FColor Color;
	};
	TArray<FFactoryArea> FactoryAreas;
	FString FindFactoryArea(const FVector& Position) const;
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
