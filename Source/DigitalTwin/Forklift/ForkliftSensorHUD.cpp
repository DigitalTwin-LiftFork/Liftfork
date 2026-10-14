#include "Forklift/ForkliftSensorHUD.h"

#include "Forklift/AutomatedForklift.h"
#include "Forklift/ForkliftPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"

void AForkliftSensorHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !PlayerOwner) return;
	AAutomatedForklift* Forklift = Cast<AAutomatedForklift>(PlayerOwner->GetPawn());
	if (!Forklift) return;
	const bool bRunning = Forklift->GetAutoPickupState() == EAutoPickupState::Detecting || Forklift->GetAutoPickupState() == EAutoPickupState::Aligning || Forklift->GetAutoPickupState() == EAutoPickupState::Inserting || Forklift->GetAutoPickupState() == EAutoPickupState::Lifting;
	const float ButtonY = Canvas->SizeY - 72.f;
	DrawRect(bRunning ? FLinearColor(.65f, .18f, .12f, .92f) : FLinearColor(.08f, .32f, .12f, .92f), 24.f, ButtonY, 240.f, 48.f);
	DrawText(bRunning ? TEXT("작업 중지") : TEXT("파레트 자동 집기"), FLinearColor::White, 40.f, ButtonY + 13.f, nullptr, 1.f);
	DrawRect(FLinearColor(.35f, .22f, .08f, .92f), 274.f, ButtonY, 240.f, 48.f);
	DrawText(TEXT("목재 파레트 생성"), FLinearColor::White, 290.f, ButtonY + 13.f, nullptr, 1.f);
	DrawText(FString::Printf(TEXT("Auto: %s - %s"), *AAutomatedForklift::AutoPickupStateToString(Forklift->GetAutoPickupState()), *Forklift->GetAutoPickupStatus()), FLinearColor::White, 24.f, ButtonY - 24.f, nullptr, .78f);
	if (const AForkliftPlayerController* Controller = Cast<AForkliftPlayerController>(PlayerOwner)) DrawText(Controller->GetWoodPalletPlacementStatus(), FLinearColor::Yellow, 274.f, ButtonY - 24.f, nullptr, .75f);
	// This panel is independent of V preview and K collision debug. It is drawn
	// only to the player HUD, never into the sensor render target.
	TArray<FString> Diagnostics; Forklift->GetAutoPickupDiagnosticLines(Diagnostics);
	const float PanelWidth = 430.f, LineHeight = 20.f, PanelHeight = 30.f + Diagnostics.Num() * LineHeight;
	const float PanelX = Canvas->SizeX - PanelWidth - 18.f, PanelY = Canvas->SizeY - PanelHeight - 18.f;
	DrawRect(FLinearColor(.02f, .02f, .02f, .82f), PanelX, PanelY, PanelWidth, PanelHeight);
	DrawText(TEXT("자동 집기 상태"), FLinearColor(0.f, .85f, .85f, 1.f), PanelX + 10.f, PanelY + 7.f, nullptr, .85f);
	for (int32 Index = 0; Index < Diagnostics.Num(); ++Index) DrawText(Diagnostics[Index], FLinearColor::White, PanelX + 10.f, PanelY + 28.f + Index * LineHeight, nullptr, .68f);
	UTextureRenderTarget2D* SensorImage = Forklift->GetPalletSensorRenderTarget();
	const int32 Mode = Forklift->GetPalletSensorDisplayMode();
	if (!SensorImage || Mode == 0) return;

	const float Aspect = static_cast<float>(SensorImage->SizeX) / FMath::Max(1, SensorImage->SizeY);
	const float Width = Mode == 1 ? Canvas->SizeX * .30f : Canvas->SizeX * .88f;
	const float Height = Width / Aspect;
	const float X = Mode == 1 ? Canvas->SizeX - Width - 24.f : (Canvas->SizeX - Width) * .5f;
	const float Y = Mode == 1 ? 24.f : (Canvas->SizeY - Height) * .5f;
	const bool bMaskView = Forklift->GetVisionDebugViewMode() == 1 && Forklift->GetVisionMaskWidth() > 0;
	if (!bMaskView) DrawTexture(SensorImage, X, Y, Width, Height, 0.f, 0.f, 1.f, 1.f, FLinearColor::White, BLEND_Opaque);
	else
	{
		DrawRect(FLinearColor::Black, X, Y, Width, Height);
		const TArray<uint8>& Mask = Forklift->GetVisionMask(); const int32 MW = Forklift->GetVisionMaskWidth(), MH = Forklift->GetVisionMaskHeight();
		// A 2x decimated canvas rendering keeps this diagnostic view cheap while
		// still revealing whether forks/shadows merge the opening regions.
		for (int32 MY = 0; MY < MH; MY += 2) for (int32 MX = 0; MX < MW; MX += 2) if (Mask.IsValidIndex(MY * MW + MX) && Mask[MY * MW + MX]) DrawRect(FLinearColor::White, X + Width * MX / MW, Y + Height * MY / MH, Width * 2.f / MW + 1.f, Height * 2.f / MH + 1.f);
	}
	DrawRect(FLinearColor::White, X, Y, Width, 1.f);
	DrawRect(FLinearColor::White, X, Y + Height - 1.f, Width, 1.f);
	DrawRect(FLinearColor::White, X, Y, 1.f, Height);
	DrawRect(FLinearColor::White, X + Width - 1.f, Y, 1.f, Height);
	// HUD-only alignment reference: it is not written to the sensor render target.
	const float CX = X + Width * .5f, CY = Y + Height * .5f;
	DrawLine(CX - 12.f, CY, CX + 12.f, CY, FLinearColor::Green, 1.5f);
	DrawLine(CX, CY - 12.f, CX, CY + 12.f, FLinearColor::Green, 1.5f);
	DrawText(bMaskView ? TEXT("Binary mask (O: source/mask)") : TEXT("Pallet sensor preview (O: source/mask)"), FLinearColor::White, X + 8.f, Y + 8.f, nullptr, .8f);
	const TArray<FVisionCandidateDebug>& Candidates = Forklift->GetVisionCandidates();
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FVisionCandidateDebug& Candidate = Candidates[Index];
		const FLinearColor Color = Candidate.bSelected ? FLinearColor(.1f, 1.f, .15f, 1.f) : FLinearColor(1.f, .15f, .15f, 1.f);
		const float BX = X + (Candidate.Center.X - Candidate.Extent.X) * (Width / SensorImage->SizeX), BY = Y + (Candidate.Center.Y - Candidate.Extent.Y) * (Height / SensorImage->SizeY);
		const float BW = Candidate.Extent.X * 2.f * (Width / SensorImage->SizeX), BH = Candidate.Extent.Y * 2.f * (Height / SensorImage->SizeY);
		DrawRect(Color, BX, BY, BW, 1.5f); DrawRect(Color, BX, BY + BH, BW, 1.5f); DrawRect(Color, BX, BY, 1.5f, BH); DrawRect(Color, BX + BW, BY, 1.5f, BH);
		DrawText(FString::Printf(TEXT("%d"), Index + 1), Color, BX + 2.f, BY + 2.f, nullptr, .8f);
	}
	DrawText(TEXT("녹색: 선택된 쌍 / 빨강: 후보 또는 탈락"), FLinearColor::White, X + 8.f, Y + Height - 20.f, nullptr, .65f);
	const FOpeningDetection& Detection = Forklift->GetOpeningDetection();
	if (Detection.bValid)
	{
		const float SX = Width / FMath::Max(1, SensorImage->SizeX), SY = Height / FMath::Max(1, SensorImage->SizeY);
		const auto DrawOpening = [this, X, Y, SX, SY](const FVector2D& Center, const FVector2D& Extent)
		{
			const float BX = X + (Center.X - Extent.X) * SX, BY = Y + (Center.Y - Extent.Y) * SY, BW = Extent.X * 2.f * SX, BH = Extent.Y * 2.f * SY;
			DrawRect(FLinearColor::Yellow, BX, BY, BW, 1.5f); DrawRect(FLinearColor::Yellow, BX, BY + BH, BW, 1.5f); DrawRect(FLinearColor::Yellow, BX, BY, 1.5f, BH); DrawRect(FLinearColor::Yellow, BX + BW, BY, 1.5f, BH);
		};
		DrawOpening(Detection.LeftCenter, Detection.LeftExtent); DrawOpening(Detection.RightCenter, Detection.RightExtent);
		DrawText(FString::Printf(TEXT("OPENINGS: %.2f  dist %.0fcm lateral %.1fcm"), Detection.Confidence, Detection.EstimatedDistanceCm, Detection.EstimatedLateralCm), FLinearColor::Yellow, X + 8.f, Y + 28.f, nullptr, .75f);
	}
}
