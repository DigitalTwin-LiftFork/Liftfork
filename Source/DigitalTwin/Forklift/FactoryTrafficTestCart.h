#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FactoryTrafficTestCart.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/** Slow crossing cart spawned with T to exercise forklift traffic interlocks without a pedestrian asset. */
UCLASS()
class DIGITALTWIN_API AFactoryTrafficTestCart : public AActor
{
	GENERATED_BODY()

public:
	AFactoryTrafficTestCart();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> CartCollision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> CartVisual;
	UPROPERTY(EditAnywhere, Category="Traffic test", meta=(ClampMin="50.0", ClampMax="500.0")) float TravelSpeedCmPerSec = 120.f;
	UPROPERTY(EditAnywhere, Category="Traffic test", meta=(ClampMin="200.0", ClampMax="2000.0")) float TravelDistanceCm = 800.f;
	FVector StartLocation = FVector::ZeroVector;
	FVector EndLocation = FVector::ZeroVector;
	bool bMovingToEnd = true;
};
