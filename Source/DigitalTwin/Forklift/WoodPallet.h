#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WoodPallet.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/** 100 x 110 x 13.5cm wood pallet for placement and physical fork tests. */
UCLASS()
class DIGITALTWIN_API AWoodPallet : public AActor
{
	GENERATED_BODY()
public:
	AWoodPallet();
	virtual void BeginPlay() override;
	void SetPlacementPreview(bool bPreview);
	void FinalizePlacement();
	bool IsPlacementPreview() const { return bPlacementPreview; }
	const TArray<TObjectPtr<UBoxComponent>>& GetCollisionShapes() const { return CollisionShapes; }
	void SetSelectedDebug(bool bSelected) { bSelectedForPlacement = bSelected; }
	bool IsSelectedDebug() const { return bSelectedForPlacement; }
	bool GetRecentForkContact(float MaxAge, UPrimitiveComponent*& OutOther) const;
	float GetBaseHeightCm() const { return 0.f; }
	// Model assumptions derived from the supplied outer dimensions, not a product drawing.
	static float LengthCm() { return 100.f; }
	static float WidthCm() { return 110.f; }
	static float HeightCm() { return 13.5f; }
	static float OpeningWidthCm() { return 36.f; }
	static float OpeningHeightCm() { return 10.f; }
	static float OpeningCenterSpacingCm() { return 60.f; }
private:
	UStaticMeshComponent* AddWoodBox(const TCHAR* Name, const FVector& Location, const FVector& FullSize);
	UBoxComponent* AddCollisionBox(const TCHAR* Name, const FVector& Location, const FVector& HalfExtent);
	UFUNCTION() void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> PhysicsRoot;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> VisualParts;
	UPROPERTY() TArray<TObjectPtr<UBoxComponent>> CollisionShapes;
	bool bPlacementPreview = false;
	bool bSelectedForPlacement = false;
	FHitResult LastHit;
	float LastHitTime = -BIG_NUMBER;
	TWeakObjectPtr<UPrimitiveComponent> LastHitOther;
};
