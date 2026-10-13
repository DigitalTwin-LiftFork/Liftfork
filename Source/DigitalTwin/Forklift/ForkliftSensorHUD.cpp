#include "Forklift/ForkliftSensorHUD.h"

#include "Forklift/AutomatedForklift.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"

void AForkliftSensorHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !PlayerOwner) return;
	AAutomatedForklift* Forklift = Cast<AAutomatedForklift>(PlayerOwner->GetPawn());
	if (!Forklift) return;
	UTextureRenderTarget2D* SensorImage = Forklift->GetPalletSensorRenderTarget();
	const int32 Mode = Forklift->GetPalletSensorDisplayMode();
	if (!SensorImage || Mode == 0) return;

	const float Aspect = static_cast<float>(SensorImage->SizeX) / FMath::Max(1, SensorImage->SizeY);
	const float Width = Mode == 1 ? Canvas->SizeX * .30f : Canvas->SizeX * .88f;
	const float Height = Width / Aspect;
	const float X = Mode == 1 ? Canvas->SizeX - Width - 24.f : (Canvas->SizeX - Width) * .5f;
	const float Y = Mode == 1 ? 24.f : (Canvas->SizeY - Height) * .5f;
	DrawTexture(SensorImage, X, Y, Width, Height, 0.f, 0.f, 1.f, 1.f, FLinearColor::White, BLEND_Opaque);
	DrawRect(FLinearColor::White, X, Y, Width, 1.f);
	DrawRect(FLinearColor::White, X, Y + Height - 1.f, Width, 1.f);
	DrawRect(FLinearColor::White, X, Y, 1.f, Height);
	DrawRect(FLinearColor::White, X + Width - 1.f, Y, 1.f, Height);
	// HUD-only alignment reference: it is not written to the sensor render target.
	const float CX = X + Width * .5f, CY = Y + Height * .5f;
	DrawLine(CX - 12.f, CY, CX + 12.f, CY, FLinearColor::Green, 1.5f);
	DrawLine(CX, CY - 12.f, CX, CY + 12.f, FLinearColor::Green, 1.5f);
	DrawText(TEXT("Pallet sensor preview - reference crosshair only"), FLinearColor::White, X + 8.f, Y + 8.f, nullptr, .8f);
}
