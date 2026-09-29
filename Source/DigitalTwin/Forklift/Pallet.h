#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pallet.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

/** Simple, collision-enabled wooden pallet built from reusable engine cubes. */
UCLASS()
class DIGITALTWIN_API APallet : public AActor
{
	GENERATED_BODY()

public:
	APallet();
	virtual void BeginPlay() override;
	/** Disable pallet collision while fork-carried; restore gravity and collision when set down. */
	void SetCarriedState(bool bIsCarried);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pallet") TObjectPtr<UBoxComponent> CollisionBody;
private:
	UStaticMeshComponent* AddBoard(const TCHAR* Name, const FVector& Location, const FVector& Scale);
	UBoxComponent* AddCollisionShape(const TCHAR* Name, const FVector& Location, const FVector& HalfExtent);
	UPROPERTY(Transient) TArray<TObjectPtr<UBoxComponent>> CollisionShapes;
};
