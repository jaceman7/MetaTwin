#include "TwinShowcaseActor.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "TelemetrySubsystem.h"
#include "TwinHudWidget.h"
#include "TwinMachineActor.h"
#include "TwinPlayerController.h"
#include "UnrealClient.h"

namespace
{
	constexpr double WarmUp = 12.0; // 내장 시뮬레이션 전환과 셰이더 준비
	constexpr double Duration = 100.0;

	struct FKey { double Time; FVector Location; FVector Target; };
	const FVector Home(2520, 0, 1180), HomeTarget(0, 0, 60);
	// 시각 순. 사이는 부드럽게 잇는다.
	const FKey Keys[] = {
		{0, FVector(2450, 1100, 900), FVector(0, 0, 100)},
		{7, Home, HomeTarget},
		{8, Home, HomeTarget},
		{12, FVector(1350, -2000, 420), FVector(600, -500, 150)},
		{17, FVector(0, 300, 330), FVector(0, 1500, 150)},
		{21, Home, HomeTarget},
		{27, Home, HomeTarget},
		{30, FVector(1520, -240, 420), FVector(700, -500, 220)},
		{49, FVector(1480, -700, 400), FVector(700, -500, 220)},
		{52, FVector(1750, -100, 800), FVector(600, 0, 150)},
		{58, FVector(1750, 300, 800), FVector(600, 200, 150)},
		{61, FVector(1520, -240, 420), FVector(700, -500, 220)},
		{68, FVector(1520, -240, 420), FVector(700, -500, 220)},
		{74, Home, HomeTarget},
		{97, Home, HomeTarget},
		{100, FVector(2560, 0, 1260), HomeTarget},
	};

	void CameraAt(double Time, FVector& Location, FRotator& Rotation)
	{
		int32 i = 0;
		while (i + 2 < UE_ARRAY_COUNT(Keys) && Time >= Keys[i + 1].Time) { ++i; }
		const double Alpha = FMath::Clamp((Time - Keys[i].Time) / (Keys[i + 1].Time - Keys[i].Time), 0.0, 1.0);
		const double Smooth = FMath::InterpEaseInOut(0.0, 1.0, Alpha, 2.0);
		Location = FMath::Lerp(Keys[i].Location, Keys[i + 1].Location, Smooth);
		Rotation = (FMath::Lerp(Keys[i].Target, Keys[i + 1].Target, Smooth) - Location).Rotation();
	}
}

ATwinShowcaseActor::ATwinShowcaseActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATwinShowcaseActor::BeginPlay()
{
	Super::BeginPlay();
	CaptureHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &ATwinShowcaseActor::SaveFrame);
}

void ATwinShowcaseActor::EndPlay(const EEndPlayReason::Type Reason)
{
	UGameViewportClient::OnScreenshotCaptured().Remove(CaptureHandle);
	Super::EndPlay(Reason);
}

void ATwinShowcaseActor::SaveFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	TArray<FColor> Opaque = Pixels;
	for (FColor& Color : Opaque) { Color.A = 255; }
	const FImageView Image(Opaque.GetData(), Width, Height);
	FImageUtils::SaveImageAutoFormat(*(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Showcase/%05d.jpg"), Frame++)), Image, 95);
}

void ATwinShowcaseActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ATwinPlayerController* PC = Cast<ATwinPlayerController>(GetWorld()->GetFirstPlayerController());
	UTelemetrySubsystem* T = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
	UTwinHudWidget* Hud = PC->GetHud();
	const double Now = UTelemetrySubsystem::Now();
	if (StartAt < 0.0) { StartAt = Now + WarmUp; }
	const double Time = Now - StartAt;

	FVector Location;
	FRotator Rotation;
	CameraAt(FMath::Max(0.0, Time), Location, Rotation);
	PC->SetPose(Location, Rotation);
	if (Time < 0.0)
	{
		return;
	}

	ATwinMachineActor* Coater = PC->GetMachines()[1].Get();
	FTwinSample Sample;
	bool bFresh = false;
	T->GetLatest(Coater->MachineId, Sample, bFresh);
	if (FaultAt < 0.0 && Sample.State == ETwinMachineState::Fault)
	{
		FaultAt = Now;
	}

	const auto At = [&](int32 Index, double CueTime) { if (Cue == Index && Time >= CueTime) { ++Cue; return true; } return false; };
	if (At(0, 0)) { Hud->SetCaption(INVTEXT("MetaTwin  |  배터리 셀 라인 디지털 트윈")); }
	if (At(1, 8)) { Hud->SetCaption(INVTEXT("설비 8대의 상태를 10Hz로 동기화")); }
	if (At(2, 21)) { PC->ToggleHeatmap(); Hud->SetCaption(INVTEXT("온도 히트맵")); }
	if (At(3, 27)) { PC->ToggleHeatmap(); PC->SelectMachine(Coater); Hud->SetCaption(INVTEXT("설비 상세와 최근 60초 추세")); }
	if (At(4, 32)) { Hud->Command(ETwinAction::InjectCooling, 0); Hud->SetCaption(INVTEXT("냉각 고장 주입: 온도가 오르기 시작")); }
	if (At(5, 51.5)) { Hud->SetCaption(INVTEXT("과열 정지. 다음 공정으로 셀이 끊긴다")); }
	if (At(6, 61)) { Hud->RunWhatIf(); Hud->SetCaption(INVTEXT("조치안 4가지를 180초 앞까지 예측")); }
	if (At(7, 67)) { Hud->ApplyWhatIf(3); Hud->SetCaption(INVTEXT("추천안 적용: 정비 20초")); }
	if (At(8, 89)) { Hud->SetCaption(INVTEXT("정비 완료, 라인 회복")); }
	if (At(9, 92)) { T->SetReplay(true); T->SeekTo(FaultAt - 2.0); Hud->SetCaption(INVTEXT("타임라인을 되감아 사고 재생")); }
	if (At(10, 98)) { T->SetReplay(false); PC->SelectMachine(nullptr); Hud->SetCaption(INVTEXT("MetaTwin  |  Unreal Engine 5.8, C++")); }

	if (Time >= Duration)
	{
		FPlatformMisc::RequestExit(false);
		return;
	}
	FScreenshotRequest::RequestScreenshot(true);
}
