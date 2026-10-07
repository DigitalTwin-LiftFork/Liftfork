#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pallet.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UPrimitiveComponent;

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
	/** Validates that both complete tine cross-sections are inside the two specified fork openings. */
	bool CanAcceptForkTines(const UBoxComponent* LeftTine, const UBoxComponent* RightTine) const;
	/** Contact-based support test. Both fork tips must pass the configurable depth inside their own openings. */
	bool CanSupportFromForks(const UBoxComponent* LeftTine, const UBoxComponent* RightTine, float MinimumInsertionDepthCm, FString& OutReason) const;
	/** World Z of the underside which the fork top must reach before automatic support begins. */
	float GetForkSupportSurfaceWorldZ() const;
	/** Query-only components used by the forklift's carried-load obstacle sweeps. */
	const TArray<TObjectPtr<UBoxComponent>>& GetCollisionShapes() const { return CollisionShapes; }
	/** Chaos contact is used only for diagnostics when this uncarried pallet simulates. */
	bool GetRecentPhysicsContact(float MaxAgeSeconds, FHitResult& OutHit, UPrimitiveComponent*& OutOther) const;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pallet") TObjectPtr<UBoxComponent> CollisionBody;
private:
	// Verified pallet specification, expressed in Unreal centimetres.
	static constexpr float DepthCm = 110.f;
	static constexpr float WidthCm = 110.f;
	static constexpr float HeightCm = 15.f;
	static constexpr float DeckThicknessCm = 2.f;
	static constexpr float ForkOpeningHeightCm = 11.f;
	static constexpr float ForkOpeningWidthCm = 30.f;
	static constexpr float ForkOpeningCenterOffsetCm = 25.f;
	UStaticMeshComponent* AddBoard(const TCHAR* Name, const FVector& Location, const FVector& Scale);
	UBoxComponent* AddCollisionShape(const TCHAR* Name, const FVector& Location, const FVector& HalfExtent);
	UFUNCTION() void HandlePalletHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
	UPROPERTY(Transient) TArray<TObjectPtr<UBoxComponent>> CollisionShapes;
	FHitResult LastPhysicsHit;
	float LastPhysicsHitTime = -BIG_NUMBER;
	TWeakObjectPtr<UPrimitiveComponent> LastPhysicsOther;
};
