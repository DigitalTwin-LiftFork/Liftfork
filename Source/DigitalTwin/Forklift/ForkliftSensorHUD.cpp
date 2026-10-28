#include "Forklift/ForkliftSensorHUD.h"

#include "Forklift/AutomatedForklift.h"
#include "Forklift/ForkliftSlamComponent.h"
#include "Forklift/ForkliftNavigationSensorComponent.h"
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
		DrawText(Forklift->GetNavigationStatus(), FLinearColor::Yellow, 20.f, 40.f, nullptr, .9f);
	const bool bRunning = Forklift->GetAutoPickupState() == EAutoPickupState::Detecting || Forklift->GetAutoPickupState() == EAutoPickupState::Aligning || Forklift->GetAutoPickupState() == EAutoPickupState::Inserting || Forklift->GetAutoPickupState() == EAutoPickupState::Lifting;
	const float ButtonY = Canvas->SizeY - 72.f;
	const FBox2D NavButton=AAutomatedForklift::NavigationButtonRect(Canvas->SizeY);
	const bool NavActive=Forklift->IsNavigationActive();
	DrawRect(NavActive ? FLinearColor(.65f,.12f,.08f,.95f) : FLinearColor(.05f,.3f,.55f,.95f),NavButton.Min.X,NavButton.Min.Y,NavButton.GetSize().X,NavButton.GetSize().Y);
	DrawText(NavActive ? TEXT("주행 정지") : TEXT("랜덤 순찰 시작"), FLinearColor::White,NavButton.Min.X+16.f,NavButton.Min.Y+12.f,nullptr,1.f);
	DrawText(TEXT("지도 클릭: 지정 위치 주행 / 시작 버튼: 랜덤 순찰"),FLinearColor::White,24.f,NavButton.Min.Y-20.f,nullptr,.72f);
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
	const float PanelX = Canvas->SizeX - PanelWidth - 18.f, PanelY = FMath::Max(90.f, Canvas->SizeY - PanelHeight - 190.f);
	DrawRect(FLinearColor(.02f, .02f, .02f, .82f), PanelX, PanelY, PanelWidth, PanelHeight);
	DrawText(TEXT("자동 집기 상태"), FLinearColor(0.f, .85f, .85f, 1.f), PanelX + 10.f, PanelY + 7.f, nullptr, .85f);
	for (int32 Index = 0; Index < Diagnostics.Num(); ++Index) DrawText(Diagnostics[Index], FLinearColor::White, PanelX + 10.f, PanelY + 28.f + Index * LineHeight, nullptr, .68f);
		if (const UForkliftSlamComponent* Slam = Forklift->FindComponentByClass<UForkliftSlamComponent>(); Slam && Slam->bShowMap)
	{
		const float MX = 20.f, MY = 100.f, Size = FMath::Min(300.f, Canvas->SizeY * .40f);
		const float Scale = Size / 3000.f;
		const FVector2D Pose = Slam->GetPosition();
		DrawRect(FLinearColor(.025f, .035f, .05f, .94f), MX, MY, Size, Size);
		for (const auto& Cell : Slam->GetCells())
		{
			const FVector2D Delta = (FVector2D(Cell.Key.X + .5f, Cell.Key.Y + .5f) * UForkliftSlamComponent::CellCm - Pose) * Scale;
			const float PX = Size * .5f + Delta.Y, PY = Size * .5f - Delta.X;
			if (PX < 0.f || PY < 0.f || PX > Size - 3.f || PY > Size - 3.f) continue;
			const FLinearColor Color = Cell.Value >= 3 ? FLinearColor(.1f, .9f, .7f) :
				(Cell.Value < 0 ? FLinearColor(.22f, .27f, .32f) : FLinearColor(.12f, .15f, .19f));
			DrawRect(Color, MX + PX, MY + PY, FMath::Max(1.f, UForkliftSlamComponent::CellCm * Scale), FMath::Max(1.f, UForkliftSlamComponent::CellCm * Scale));
		}
				for (const FVector2D& Point : Forklift->GetNavigationRoute())
		{
			const FVector2D D = (Point - Pose) * Scale;
			const float X = Size*.5f+D.Y, Y = Size*.5f-D.X;
			if (X>=0 && Y>=0 && X<Size-3 && Y<Size-3)
				DrawRect(FLinearColor(.1f,.5f,1.f), MX+X, MY+Y, 3.f, 3.f);
		}
		if (Forklift->HasNavigationGoal())
		{
			const FVector2D D=(Forklift->GetNavigationGoal()-Pose)*Scale;
			const float X=Size*.5f+D.Y, Y=Size*.5f-D.X;
			if (X>=4 && Y>=4 && X<Size-4 && Y<Size-4)
				DrawRect(FLinearColor(1.f,.2f,.8f),MX+X-4,MY+Y-4,8.f,8.f);
		}
		const float CX = MX + Size * .5f, CY = MY + Size * .5f;
		DrawRect(FLinearColor::Yellow, CX - 3.f, CY - 3.f, 6.f, 6.f);
		DrawLine(CX, CY, CX + FMath::Sin(Slam->GetYaw()) * 18.f, CY - FMath::Cos(Slam->GetYaw()) * 18.f, FLinearColor::Yellow, 2.f);
		DrawText(TEXT("LiDAR / SLAM | 클릭해 탐색된 목표 선택"), FLinearColor::White, MX, MY - 20.f, nullptr, .8f);
		DrawText(Slam->GetStatus(), FLinearColor::Yellow, MX, MY + Size + 4.f, nullptr, .75f);
		DrawText(FString::Printf(TEXT("x %.2f m  y %.2f m | match %.0f%%"), Pose.X / 100., Pose.Y / 100., Slam->GetConfidence() * 100.f), FLinearColor::White, MX, MY + Size + 22.f, nullptr, .75f);
		DrawText(TEXT("M: map  B: reset  L: LiDAR rays"), FLinearColor::White, MX, MY + Size + 40.f, nullptr, .75f);
		DrawText(FString::Printf(TEXT("Scans: %d | Map updates: %d"), Slam->GetScanCount(), Slam->GetMapUpdateCount()), FLinearColor::White, MX, MY + Size + 58.f, nullptr, .75f);
		if (const auto* Sensor = Forklift->FindComponentByClass<UForkliftNavigationSensorComponent>())
		{
			// Sensor-local preview stays live even when map localization is uncertain.
			const float LX = MX + Size + 12.f, LY = MY, LS = FMath::Min(160.f, Size);
			DrawRect(FLinearColor(.02f, .035f, .05f, .95f), LX, LY, LS, LS);
			DrawText(TEXT("LIVE LiDAR (all elevations)"), FLinearColor::White, LX, LY - 18.f, nullptr, .65f);
			const float Range = FMath::Max(Sensor->MaximumRangeCm, 1.f);
			for (const auto& Ray : Sensor->GetLatestScan().Samples)
			{
				if (!Ray.bValid) continue;
				const float PX = LS * .5f + Ray.LocalPointCm.Y / Range * (LS * .48f);
				const float PY = LS * .5f - Ray.LocalPointCm.X / Range * (LS * .48f);
				DrawRect(FLinearColor::Green, LX + PX, LY + PY, 2.f, 2.f);
			}
			DrawRect(FLinearColor::Yellow, LX + LS * .5f - 2.f, LY + LS * .5f - 2.f, 4.f, 4.f);
		}
	}
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
