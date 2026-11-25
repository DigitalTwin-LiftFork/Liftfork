#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ForkliftFleetManager.generated.h"

class AAutomatedForklift;

/** Small local fleet coordinator for the factory simulation. */
UCLASS()
class DIGITALTWIN_API AForkliftFleetManager : public AActor
{
	GENERATED_BODY()

public:
	AForkliftFleetManager();
	virtual void Tick(float DeltaSeconds) override;

	void RegisterForklift(AAutomatedForklift* Forklift);
	void ToggleFleetOperation(AAutomatedForklift* RequestingForklift);
	int32 GetFleetSize() const { return Forklifts.Num(); }

private:
	bool DeployFleet(AAutomatedForklift* LeadForklift);
	bool IsSpawnAreaClear(const FVector& Location, const AAutomatedForklift* IgnoreForklift) const;
	void UpdateDeadlockHandling(float DeltaSeconds);

	UPROPERTY() TArray<TObjectPtr<AAutomatedForklift>> Forklifts;
	TMap<uint64, float> BlockedPairSeconds;
	float DeadlockCheckElapsed = 0.f;
	bool bFleetMoving = false;
	int32 NextFleetId = 0;
	static constexpr int32 MaximumFleetSize = 3;
};
