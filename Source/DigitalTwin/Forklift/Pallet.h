#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pallet.generated.h"

class UStaticMeshComponent;

/** Simple, collision-enabled wooden pallet built from reusable engine cubes. */
UCLASS()
class DIGITALTWIN_API APallet : public AActor
{
	GENERATED_BODY()

public:
	APallet();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pallet") TObjectPtr<USceneComponent> SceneRoot;
private:
	UStaticMeshComponent* AddBoard(const TCHAR* Name, const FVector& Location, const FVector& Scale);
};
