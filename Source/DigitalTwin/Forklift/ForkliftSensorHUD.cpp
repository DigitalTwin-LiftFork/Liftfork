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
	const bool bRunning = Forklift->GetAutoPickupState() == EAutoPickupState::Detecting || Forklift->GetAutoPickupState() == EAutoPickupState::Approaching || Forklift->GetAutoPickupState() == EAutoPickupState::Aligning || Forklift->GetAutoPickupState() == EAutoPickupState::Inserting || Forklift->GetAutoPickupState() == EAutoPickupState::Lifting;
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
	const int32 DebugView = Forklift->GetVisionDebugViewMode();
	const int32 DepthView = Forklift->GetDepthDebugViewMode();
	const bool bDepthView = DepthView != 0 && Forklift->IsDepthFrameValid();
	const bool bMaskView = DebugView != 0 && Forklift->GetVisionMaskWidth() > 0;
	if (!bMaskView || bDepthView) DrawTexture(SensorImage, X, Y, Width, Height, 0.f, 0.f, 1.f, 1.f, FLinearColor::White, BLEND_Opaque);
	else
	{
		DrawRect(FLinearColor::Black, X, Y, Width, Height);
		const TArray<uint8>& Mask = DebugView == 1 ? Forklift->GetVisionRegionMask() : (DebugView == 2 ? Forklift->GetVisionBodyMask() : Forklift->GetVisionMask()); const int32 MW = Forklift->GetVisionMaskWidth(), MH = Forklift->GetVisionMaskHeight();
		// A 2x decimated canvas rendering keeps this diagnostic view cheap while
		// still revealing whether forks/shadows merge the opening regions.
		for (int32 MY = 0; MY < MH; MY += 2) for (int32 MX = 0; MX < MW; MX += 2) if (Mask.IsValidIndex(MY * MW + MX) && Mask[MY * MW + MX]) DrawRect(FLinearColor::White, X + Width * MX / MW, Y + Height * MY / MH, Width * 2.f / MW + 1.f, Height * 2.f / MH + 1.f);
	}
	if (bDepthView)
	{
		const TArray<float>& Depth = Forklift->GetLatestDepthCm(); const int32 DW=SensorImage->SizeX,DH=SensorImage->SizeY; const float MinD=Forklift->GetDepthMinimumCm(),MaxD=Forklift->GetDepthMaximumCm();
		for (int32 PY=0;PY<DH;PY+=4) for (int32 PX=0;PX<DW;PX+=4) if (Depth.IsValidIndex(PY*DW+PX))
		{
			const float D=Depth[PY*DW+PX]; const float N=FMath::Clamp((D-MinD)/FMath::Max(1.f,MaxD-MinD),0.f,1.f);
			if (DepthView==1) DrawRect(FLinearColor(N,N,N,.92f),X+Width*PX/DW,Y+Height*PY/DH,Width*4.f/DW+1.f,Height*4.f/DH+1.f);
			else if (PX+4<DW && FMath::Abs(D-Depth[PY*DW+PX+4])>18.f) DrawRect(FLinearColor(0.f,1.f,1.f,.9f),X+Width*PX/DW,Y+Height*PY/DH,Width*4.f/DW+1.f,Height*4.f/DH+1.f);
		}
	}
	DrawRect(FLinearColor::White, X, Y, Width, 1.f);
	DrawRect(FLinearColor::White, X, Y + Height - 1.f, Width, 1.f);
	DrawRect(FLinearColor::White, X, Y, 1.f, Height);
	DrawRect(FLinearColor::White, X + Width - 1.f, Y, 1.f, Height);
	// HUD-only alignment reference: it is not written to the sensor render target.
	const float CX = X + Width * .5f, CY = Y + Height * .5f;
	DrawLine(CX - 12.f, CY, CX + 12.f, CY, FLinearColor::Green, 1.5f);
	DrawLine(CX, CY - 12.f, CX, CY + 12.f, FLinearColor::Green, 1.5f);
	DrawText(bDepthView ? (DepthView==1 ? TEXT("RGB-D normalized SceneDepth (U: RGB/depth/edges)") : TEXT("RGB with SceneDepth edges (U: RGB/depth/edges)")) : (bMaskView ? (DebugView == 1 ? TEXT("Colour-region mask (O: source/region/body/opening)") : (DebugView == 2 ? TEXT("Object body mask (O: source/region/body/opening)") : TEXT("Opening mask (O: source/region/body/opening)"))) : TEXT("Pallet sensor preview (O: source/region/body/opening)")), FLinearColor::White, X + 8.f, Y + 8.f, nullptr, .8f);
	// Coarse grid and body region are player-HUD diagnostics only.
	const int32 GridX = Forklift->GetVisionCoarseGridColumns(), GridY = Forklift->GetVisionCoarseGridRows();
	const TArray<int32>& Labels = Forklift->GetVisionCoarseLabels();
	for (int32 GY = 0; GY < GridY; ++GY) for (int32 GX = 0; GX < GridX; ++GX)
	{
		const int32 Label = Labels.IsValidIndex(GY * GridX + GX) ? Labels[GY * GridX + GX] : -1;
		if (Label < 0) continue;
		const FLinearColor RegionColor = FLinearColor::MakeFromHSV8(static_cast<uint8>((Label * 67) % 255), 190, 255).CopyWithNewOpacity(.12f);
		DrawRect(RegionColor, X + Width * GX / GridX, Y + Height * GY / GridY, Width / GridX, Height / GridY);
		DrawText(FString::FromInt(Label), RegionColor.CopyWithNewOpacity(.9f), X + Width * GX / GridX + 2.f, Y + Height * GY / GridY + 2.f, nullptr, .45f);
	}
	for (int32 GX = 1; GX < GridX; ++GX) DrawLine(X + Width * GX / GridX, Y, X + Width * GX / GridX, Y + Height, FLinearColor(.2f, .5f, 1.f, .45f), .5f);
	for (int32 GY = 1; GY < GridY; ++GY) DrawLine(X, Y + Height * GY / GridY, X + Width, Y + Height * GY / GridY, FLinearColor(.2f, .5f, 1.f, .45f), .5f);
	const FIntRect& Body = Forklift->GetVisionBodyBounds();
	if (Body.Max.X > Body.Min.X && Forklift->GetVisionMaskWidth() > 0)
	{
		const float ScaleX = Width / Forklift->GetVisionMaskWidth(), ScaleY = Height / Forklift->GetVisionMaskHeight();
		const float BX = X + Body.Min.X * ScaleX, BY = Y + Body.Min.Y * ScaleY, BW = (Body.Max.X - Body.Min.X) * ScaleX, BH = (Body.Max.Y - Body.Min.Y) * ScaleY;
		DrawRect(FLinearColor(.1f, .55f, 1.f, 1.f), BX, BY, BW, 1.5f); DrawRect(FLinearColor(.1f, .55f, 1.f, 1.f), BX, BY + BH, BW, 1.5f); DrawRect(FLinearColor(.1f, .55f, 1.f, 1.f), BX, BY, 1.5f, BH); DrawRect(FLinearColor(.1f, .55f, 1.f, 1.f), BX + BW, BY, 1.5f, BH);
	}
	const TArray<FIntRect>& FineCells = Forklift->GetVisionSubdivisionCells(); const TArray<int32>& FineDepths = Forklift->GetVisionSubdivisionDepths();
	for (int32 Index = 0; Index < FineCells.Num(); ++Index)
	{
		const FIntRect& Cell = FineCells[Index]; const float SX = Width / FMath::Max(1, SensorImage->SizeX), SY = Height / FMath::Max(1, SensorImage->SizeY);
		DrawRect(FLinearColor(.75f, .2f, 1.f, .35f), X + Cell.Min.X * SX, Y + Cell.Min.Y * SY, (Cell.Max.X - Cell.Min.X) * SX, 1.f);
		if (FineDepths.IsValidIndex(Index) && FineDepths[Index] > 0) DrawText(FString::FromInt(FineDepths[Index]), FLinearColor(.75f,.2f,1.f,.65f), X + Cell.Min.X * SX + 1.f, Y + Cell.Min.Y * SY + 1.f, nullptr, .35f);
	}
	const TArray<FVisionCandidateDebug>& Candidates = Forklift->GetVisionCandidates();
	if (!Forklift->GetVisionFailureReason().IsEmpty()) DrawText(FString::Printf(TEXT("Vision: %s"), *Forklift->GetVisionFailureReason()), FLinearColor::Red, X + 8.f, Y + 28.f, nullptr, .70f);
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
