// Dedicated controller for the C++ forklift simulation. No template click-to-move bindings.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ForkliftPlayerController.generated.h"

UCLASS()
class DIGITALTWIN_API AForkliftPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AForkliftPlayerController();
	virtual void OnPossess(APawn* InPawn) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	bool IsWoodPalletPlacementActive() const { return ActiveWoodPallet != nullptr; }
	FString GetWoodPalletPlacementStatus() const { return WoodPalletPlacementStatus; }
private:
	void ClickAutoPickupButton();
	void BeginOrSelectWoodPallet();
	void EndWoodPalletPlacement();
	void CancelWoodPalletPlacement();
	void UpdateWoodPalletPlacement();
	bool IsWoodPalletPositionValid(class AWoodPallet* Pallet, UPrimitiveComponent* GroundComponent) const;
	TObjectPtr<class AWoodPallet> ActiveWoodPallet;
	FTransform SavedWoodPalletTransform;
	TWeakObjectPtr<UPrimitiveComponent> PlacementGroundComponent;
	bool bWoodPalletIsNew = false, bWoodPalletPositionValid = false, bIgnoreNextPlacementRelease = false;
	FString WoodPalletPlacementStatus = TEXT("목재 파레트 배치: 대기");
};
