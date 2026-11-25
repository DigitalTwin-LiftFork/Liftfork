#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ForkliftSafetyZone.generated.h"

class UBoxComponent;

UENUM(BlueprintType)
enum class EForkliftSafetyZoneType : uint8
{
	SpeedLimit UMETA(DisplayName="Speed limit"),
	NoEntry UMETA(DisplayName="No entry")
};

/** Editor-placeable simulation zone used by the forklift's conservative safety interlock. */
UCLASS(Blueprintable)
class DIGITALTWIN_API AForkliftSafetyZone : public AActor
{
	GENERATED_BODY()

public:
	AForkliftSafetyZone();
	virtual void Tick(float DeltaSeconds) override;

	bool ContainsPointExpanded(const FVector& WorldPoint, float ExpansionCm) const;
	EForkliftSafetyZoneType GetZoneType() const { return ZoneType; }
	float GetMaximumSpeedCmPerSec() const { return MaximumSpeedCmPerSec; }
	const FString& GetZoneLabel() const { return ZoneLabel; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(VisibleAnywhere, Category="Safety zone") TObjectPtr<UBoxComponent> ZoneVolume;
	UPROPERTY(EditAnywhere, Category="Safety zone") EForkliftSafetyZoneType ZoneType = EForkliftSafetyZoneType::SpeedLimit;
	UPROPERTY(EditAnywhere, Category="Safety zone", meta=(ClampMin="10.0", ClampMax="195.0", EditCondition="ZoneType == EForkliftSafetyZoneType::SpeedLimit")) float MaximumSpeedCmPerSec = 60.f;
	UPROPERTY(EditAnywhere, Category="Safety zone") FString ZoneLabel = TEXT("Factory safety zone");
	UPROPERTY(EditAnywhere, Category="Safety zone|Debug") bool bDrawDebugZone = true;
};
