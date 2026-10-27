#include "Forklift/ForkliftPlayerController.h"
#include "Forklift/AutomatedForklift.h"
#include "Forklift/WoodPallet.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/EngineTypes.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"

AForkliftPlayerController::AForkliftPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = false;
}

void AForkliftPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AForkliftPlayerController::ClickAutoPickupButton);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AForkliftPlayerController::EndWoodPalletPlacement);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AForkliftPlayerController::CancelWoodPalletPlacement);
}

void AForkliftPlayerController::ClickAutoPickupButton()
{
	float MouseX = 0.f, MouseY = 0.f; int32 ViewX = 0, ViewY = 0;
	GetViewportSize(ViewX, ViewY);
	const bool bHasMouse = GetMousePosition(MouseX, MouseY);
		if (bHasMouse && AAutomatedForklift::NavigationButtonRect(ViewY).IsInside(FVector2D(MouseX,MouseY)))
	{
		if (AAutomatedForklift* Forklift=Cast<AAutomatedForklift>(GetPawn()))
		{
			if (ActiveWoodPallet) CancelWoodPalletPlacement();
			Forklift->ToggleNavigation();
		}
		return;
	}
	if (bHasMouse && AAutomatedForklift::SlamMapRect(ViewY).IsInside(FVector2D(MouseX,MouseY)))
	{
		if (AAutomatedForklift* Forklift=Cast<AAutomatedForklift>(GetPawn()))
		{
			if (ActiveWoodPallet) CancelWoodPalletPlacement();
			Forklift->SetNavigationGoalFromMapScreen(FVector2D(MouseX,MouseY),static_cast<float>(ViewY));
		}
		return;
	}
	const bool bInsideAutoButton = bHasMouse && MouseX >= 24.f && MouseX <= 264.f && MouseY >= ViewY - 72.f && MouseY <= ViewY - 24.f;
	const bool bInsideWoodButton = bHasMouse && MouseX >= 274.f && MouseX <= 514.f && MouseY >= ViewY - 72.f && MouseY <= ViewY - 24.f;
	UE_LOG(LogTemp, Warning, TEXT("[R16HD_UI] controller=%s viewport=%dx%d mouse=(%.0f,%.0f) valid=%d auto=%d wood=%d pawn=%s"), *GetName(), ViewX, ViewY, MouseX, MouseY, bHasMouse, bInsideAutoButton, bInsideWoodButton, *GetNameSafe(GetPawn()));
	if (bInsideAutoButton) { if (AAutomatedForklift* Forklift = Cast<AAutomatedForklift>(GetPawn())) Forklift->NotifyAutoPickupButtonClick(true, FVector2D(MouseX, MouseY)); return; }
	if (bInsideWoodButton) { if (AAutomatedForklift* Forklift=Cast<AAutomatedForklift>(GetPawn())) Forklift->StopNavigation(); BeginOrSelectWoodPallet(); return; }
	if (ActiveWoodPallet) return;
	if (AAutomatedForklift* Forklift = Cast<AAutomatedForklift>(GetPawn()))
	{
		if (Forklift->GetAutoPickupState() == EAutoPickupState::Detecting || Forklift->GetAutoPickupState() == EAutoPickupState::Aligning || Forklift->GetAutoPickupState() == EAutoPickupState::Inserting || Forklift->GetAutoPickupState() == EAutoPickupState::Lifting)
		{
			WoodPalletPlacementStatus = TEXT("자동 집기 실행 중에는 기존 목재 파레트를 이동할 수 없음");
			return;
		}
	}
	FHitResult Hit; if (GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), false, Hit)) if (AWoodPallet* Pallet = Cast<AWoodPallet>(Hit.GetActor())) { if (AAutomatedForklift* Forklift=Cast<AAutomatedForklift>(GetPawn())) Forklift->StopNavigation(); ActiveWoodPallet = Pallet; SavedWoodPalletTransform = Pallet->GetActorTransform(); bWoodPalletIsNew = false; Pallet->SetSelectedDebug(true); Pallet->SetPlacementPreview(true); WoodPalletPlacementStatus = TEXT("목재 파레트 드래그 중"); }
}

void AForkliftPlayerController::BeginOrSelectWoodPallet()
{
	if (ActiveWoodPallet) { WoodPalletPlacementStatus = TEXT("이미 배치 중 — 좌클릭 확정 또는 Esc 취소"); return; }
	AAutomatedForklift* Forklift = Cast<AAutomatedForklift>(GetPawn());
	if (Forklift && (Forklift->GetAutoPickupState() == EAutoPickupState::Detecting || Forklift->GetAutoPickupState() == EAutoPickupState::Aligning || Forklift->GetAutoPickupState() == EAutoPickupState::Inserting || Forklift->GetAutoPickupState() == EAutoPickupState::Lifting)) { WoodPalletPlacementStatus = TEXT("자동 집기 실행 중에는 배치 불가"); return; }
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ActiveWoodPallet = GetWorld()->SpawnActor<AWoodPallet>(AWoodPallet::StaticClass(), FVector(0.f, 0.f, -10000.f), FRotator::ZeroRotator, Params);
	if (!ActiveWoodPallet) { WoodPalletPlacementStatus = TEXT("목재 파레트 생성 실패"); return; }
	bWoodPalletIsNew = true; bIgnoreNextPlacementRelease = true; ActiveWoodPallet->SetPlacementPreview(true); WoodPalletPlacementStatus = TEXT("목재 파레트 미리보기 — 바닥을 가리킨 뒤 좌클릭");
}

void AForkliftPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime); UpdateWoodPalletPlacement();
}
void AForkliftPlayerController::UpdateWoodPalletPlacement()
{
	if (!ActiveWoodPallet) return;
	FHitResult Hit; if (!GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_Visibility), false, Hit)) { bWoodPalletPositionValid = false; WoodPalletPlacementStatus = TEXT("배치 중 — 바닥 표면을 찾지 못함"); return; }
	PlacementGroundComponent = Hit.GetComponent();
	FVector Location = Hit.ImpactPoint; Location.Z += ActiveWoodPallet->GetBaseHeightCm(); ActiveWoodPallet->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	bWoodPalletPositionValid = IsWoodPalletPositionValid(ActiveWoodPallet, Hit.GetComponent());
	WoodPalletPlacementStatus = bWoodPalletPositionValid ? TEXT("배치 중 — 배치 가능 (좌클릭 확정)") : TEXT("배치 중 — 장애물과 겹쳐 배치 불가");
	const FColor Color = bWoodPalletPositionValid ? FColor::Green : FColor::Red;
	DrawDebugBox(GetWorld(), ActiveWoodPallet->GetActorLocation() + FVector(0, 0, AWoodPallet::HeightCm() * .5f), FVector(AWoodPallet::LengthCm() * .5f, AWoodPallet::WidthCm() * .5f, AWoodPallet::HeightCm() * .5f), Color, false, 0.f, 0, 2.f);
}
bool AForkliftPlayerController::IsWoodPalletPositionValid(AWoodPallet* Pallet, UPrimitiveComponent* GroundComponent) const
{
	if (!Pallet) return false; FCollisionQueryParams Params(SCENE_QUERY_STAT(WoodPalletPlacement), false, Pallet); if (GroundComponent) Params.AddIgnoredComponent(GroundComponent);
	FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
	for (UBoxComponent* Shape : Pallet->GetCollisionShapes()) { TArray<FOverlapResult> Results; if (GetWorld()->OverlapMultiByObjectType(Results, Shape->GetComponentLocation(), Shape->GetComponentQuat(), Objects, FCollisionShape::MakeBox(Shape->GetScaledBoxExtent()), Params)) for (const FOverlapResult& Result : Results) if (Result.GetActor() != Pallet && Result.GetComponent() != GroundComponent) return false; }
	return true;
}
void AForkliftPlayerController::EndWoodPalletPlacement()
{
	if (!ActiveWoodPallet) return;
	if (bIgnoreNextPlacementRelease) { bIgnoreNextPlacementRelease = false; return; }
	if (bWoodPalletPositionValid) { ActiveWoodPallet->SetSelectedDebug(false); ActiveWoodPallet->FinalizePlacement(); WoodPalletPlacementStatus = TEXT("목재 파레트 배치 완료"); ActiveWoodPallet = nullptr; }
	else if (bWoodPalletIsNew) { ActiveWoodPallet->Destroy(); ActiveWoodPallet = nullptr; WoodPalletPlacementStatus = TEXT("잘못된 위치 — 새 파레트 생성 취소"); }
	else { ActiveWoodPallet->SetSelectedDebug(false); ActiveWoodPallet->SetActorTransform(SavedWoodPalletTransform, false, nullptr, ETeleportType::TeleportPhysics); ActiveWoodPallet->FinalizePlacement(); ActiveWoodPallet = nullptr; WoodPalletPlacementStatus = TEXT("잘못된 위치 — 기존 파레트 원위치 복귀"); }
}
void AForkliftPlayerController::CancelWoodPalletPlacement()
{
	if (!ActiveWoodPallet) return;
	if (bWoodPalletIsNew) { ActiveWoodPallet->Destroy(); WoodPalletPlacementStatus = TEXT("새 목재 파레트 배치 취소"); }
	else { ActiveWoodPallet->SetSelectedDebug(false); ActiveWoodPallet->SetActorTransform(SavedWoodPalletTransform, false, nullptr, ETeleportType::TeleportPhysics); ActiveWoodPallet->FinalizePlacement(); WoodPalletPlacementStatus = TEXT("기존 목재 파레트 배치 취소 — 원위치 복귀"); }
	ActiveWoodPallet = nullptr; bIgnoreNextPlacementRelease = false;
}

void AForkliftPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// The pawn's UCameraComponent becomes the active view target; no template camera can take control.
	SetViewTarget(InPawn);
}
