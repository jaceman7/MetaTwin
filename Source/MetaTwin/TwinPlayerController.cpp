#include "TwinPlayerController.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TelemetrySubsystem.h"
#include "TimerManager.h"
#include "TwinHudWidget.h"
#include "TwinMachineActor.h"
#include "TwinShowcaseActor.h"
#include "TwinTourActor.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTwinTour, Log, All);

namespace
{
	const FVector OverviewLocation(2520, 0, 1180);
	const FRotator OverviewRotation(-27, 180, 0);
}

void ATwinPlayerController::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<ATwinMachineActor> It(GetWorld()); It; ++It)
	{
		Machines.Add(*It);
	}
	Machines.Sort([](const TWeakObjectPtr<ATwinMachineActor>& A, const TWeakObjectPtr<ATwinMachineActor>& B)
	{
		return A->LineOrder != B->LineOrder ? A->LineOrder < B->LineOrder : A->MachineId.LexicalLess(B->MachineId);
	});

	if (!IsLocalController())
	{
		return;
	}
	Hud = CreateWidget<UTwinHudWidget>(this, UTwinHudWidget::StaticClass());
	Hud->AddToViewport();
	SetViewMode(ETwinViewMode::Overview);
	if (APawn* MyPawn = GetPawn())
	{
		MyPawn->SetActorLocation(OverviewLocation);
		SetControlRotation(OverviewRotation);
		bFlying = false;
	}
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("TwinShowcase")))
	{
		GetWorld()->SpawnActor<ATwinShowcaseActor>();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("TwinTourM3")))
	{
		GetWorld()->SpawnActor<ATwinTourActor>();
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("TwinTour")))
	{
		GetWorldTimerManager().SetTimer(TourTimer, this, &ATwinPlayerController::RunTourStep, 6.f, true, 8.f);
	}
#endif
}

void ATwinPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ATwinPlayerController::OnClick);
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ATwinPlayerController::OnToggleView);
	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ATwinPlayerController::ToggleHeatmap);
#if !WITH_EDITOR
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ATwinPlayerController::OnQuit);
#endif
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ATwinPlayerController::OnToggleReplay);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ATwinPlayerController::FocusSelected);
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this, &ATwinPlayerController::SelectNext);
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this, &ATwinPlayerController::SelectPrevious);
}

void ATwinPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	APawn* MyPawn = GetPawn();
	if (bFlying && MyPawn)
	{
		const FVector Location = FMath::VInterpTo(MyPawn->GetActorLocation(), FlyLocation, DeltaTime, 3.5f);
		MyPawn->SetActorLocation(Location);
		SetControlRotation(FMath::RInterpTo(GetControlRotation(), FlyRotation, DeltaTime, 4.5f));
		bFlying = FVector::DistSquared(Location, FlyLocation) > 4.0 || !GetControlRotation().Equals(FlyRotation, 0.2f);
	}
	NextLabelUpdate -= DeltaTime;
	if (NextLabelUpdate <= 0.f)
	{
		NextLabelUpdate = 0.1f;
		UpdateLabels();
	}
	if (bMeasure)
	{
		FrameTimes.Add(FApp::GetDeltaTime());
	}
}

void ATwinPlayerController::UpdateLabels()
{
	// 선택한 설비, 가까운 설비 순으로 자리를 잡고 겹치는 라벨은 숨긴다.
	FVector Eye;
	FRotator Rotation;
	GetPlayerViewPoint(Eye, Rotation);
	TArray<ATwinMachineActor*> Sorted;
	for (const TWeakObjectPtr<ATwinMachineActor>& Machine : Machines)
	{
		if (Machine.IsValid()) { Sorted.Add(Machine.Get()); }
	}
	Sorted.Sort([&](const ATwinMachineActor& A, const ATwinMachineActor& B)
	{
		return A.IsSelected() != B.IsSelected() ? A.IsSelected() : FVector::DistSquared(A.GetLabelPoint(), Eye) < FVector::DistSquared(B.GetLabelPoint(), Eye);
	});

	const float Scale = FMath::Max(0.1f, UWidgetLayoutLibrary::GetViewportScale(this));
	TArray<FBox2D> Taken;
	for (ATwinMachineActor* Machine : Sorted)
	{
		FVector2D Screen;
		bool bVisible = false;
		if (ProjectWorldLocationToScreen(Machine->GetLabelPoint(), Screen))
		{
			const FVector2D Size = FVector2D(146, 48) * Scale;
			const FBox2D Box(Screen - FVector2D(Size.X * 0.5, Size.Y), Screen + FVector2D(Size.X * 0.5, 0));
			bVisible = !Taken.ContainsByPredicate([&](const FBox2D& Other) { return Other.Intersect(Box); });
			if (bVisible) { Taken.Add(Box); }
		}
		Machine->SetLabelVisible(bVisible);
	}
}

void ATwinPlayerController::SetPose(const FVector& Location, const FRotator& Rotation)
{
	bFlying = false;
	if (APawn* MyPawn = GetPawn())
	{
		MyPawn->SetActorLocation(Location);
	}
	SetControlRotation(Rotation);
}

void ATwinPlayerController::OnQuit()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void ATwinPlayerController::ReportFps(const TCHAR* View)
{
	bMeasure = false;
	FrameTimes.Sort();
	if (FrameTimes.Num() > 10)
	{
		double Sum = 0.0;
		for (const float T : FrameTimes) { Sum += T; }
		UE_LOG(LogTwinTour, Display, TEXT("%s fps avg=%.1f median=%.1f p99_ms=%.2f max_ms=%.1f frames=%d"), View, FrameTimes.Num() / Sum,
			1.f / FrameTimes[FrameTimes.Num() / 2], FrameTimes[FMath::FloorToInt32(FrameTimes.Num() * 0.99)] * 1000.f, FrameTimes.Last() * 1000.f, FrameTimes.Num());
	}
	FrameTimes.Reset();
}

void ATwinPlayerController::SelectMachine(ATwinMachineActor* Machine)
{
	if (Selected.Get() == Machine)
	{
		return;
	}
	if (ATwinMachineActor* Old = Selected.Get())
	{
		Old->SetSelected(false);
	}
	Selected = Machine;
	if (Machine)
	{
		Machine->SetSelected(true);
	}
}

ATwinMachineActor* ATwinPlayerController::SelectAtScreen(const FVector2D& ScreenPosition)
{
	FHitResult Hit;
	GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, false, Hit);
	ATwinMachineActor* Machine = Cast<ATwinMachineActor>(Hit.GetActor());
	SelectMachine(Machine);
	return Machine;
}

void ATwinPlayerController::OnClick()
{
	if (ViewMode == ETwinViewMode::Drone)
	{
		int32 Width = 0, Height = 0;
		GetViewportSize(Width, Height);
		SelectAtScreen(FVector2D(Width * 0.5, Height * 0.5));
		return;
	}
	float X = 0.f, Y = 0.f;
	if (GetMousePosition(X, Y))
	{
		SelectAtScreen(FVector2D(X, Y));
	}
}

void ATwinPlayerController::OnToggleView()
{
	SetViewMode(ViewMode == ETwinViewMode::Overview ? ETwinViewMode::Drone : ETwinViewMode::Overview);
}

void ATwinPlayerController::OnToggleReplay()
{
	UTelemetrySubsystem* Telemetry = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
	Telemetry->SetReplay(!Telemetry->IsReplay());
}

void ATwinPlayerController::SelectNext()
{
	StepSelection(1);
}

void ATwinPlayerController::SelectPrevious()
{
	StepSelection(-1);
}

void ATwinPlayerController::StepSelection(int32 Delta)
{
	if (Machines.IsEmpty())
	{
		return;
	}
	const int32 Current = Machines.IndexOfByPredicate([this](const TWeakObjectPtr<ATwinMachineActor>& M) { return M == Selected; });
	const int32 Num = Machines.Num();
	const int32 Next = Current == INDEX_NONE ? (Delta > 0 ? 0 : Num - 1) : (Current + Delta + Num) % Num;
	SelectMachine(Machines[Next].Get());
}

void ATwinPlayerController::SetViewMode(ETwinViewMode Mode)
{
	ViewMode = Mode;
	const bool bOverview = Mode == ETwinViewMode::Overview;
	bShowMouseCursor = bOverview;
	// 중첩 카운터이므로 먼저 초기화한다.
	ResetIgnoreLookInput();
	ResetIgnoreMoveInput();
	if (bOverview)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
		SetIgnoreLookInput(true);
		SetIgnoreMoveInput(true);
		FlyTo(OverviewLocation, OverviewRotation);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		bFlying = false;
	}
}

void ATwinPlayerController::ToggleHeatmap()
{
	bHeatmap = !bHeatmap;
	for (const TWeakObjectPtr<ATwinMachineActor>& Machine : Machines)
	{
		if (Machine.IsValid())
		{
			Machine->SetHeatmap(bHeatmap);
		}
	}
}

void ATwinPlayerController::FocusSelected()
{
	if (const ATwinMachineActor* Machine = Selected.Get())
	{
		const FVector Target = Machine->GetFocusPoint();
		const FVector Location = Target + FVector(820, 260, 240);
		FlyTo(Location, (Target + FVector(0, 0, 60) - Location).Rotation());
	}
}

void ATwinPlayerController::FlyTo(const FVector& Location, const FRotator& Rotation)
{
	FlyLocation = Location;
	FlyRotation = Rotation;
	bFlying = true;
}

void ATwinPlayerController::RunTourStep()
{
#if !UE_BUILD_SHIPPING
	const auto Shot = [](const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("M2") / Name, true, false);
	};
	const auto Check = [this](bool bOk, const FString& What)
	{
		TourFailures += bOk ? 0 : 1;
		UE_LOG(LogTwinTour, Display, TEXT("%s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};

	switch (TourStep++)
	{
	case 0:
	{
		Check(Machines.Num() == 8, FString::Printf(TEXT("machines=%d"), Machines.Num()));
		int32 Hits = 0;
		for (const TWeakObjectPtr<ATwinMachineActor>& Machine : Machines)
		{
			FVector2D Screen;
			if (ProjectWorldLocationToScreen(Machine->GetFocusPoint() + FVector(0, 0, 40), Screen) && SelectAtScreen(Screen) == Machine.Get() && Machine->IsSelected())
			{
				++Hits;
			}
			else
			{
				UE_LOG(LogTwinTour, Display, TEXT("not pickable: %s"), *Machine->MachineId.ToString());
			}
		}
		Check(Hits == Machines.Num(), FString::Printf(TEXT("pickable from overview %d/%d"), Hits, Machines.Num()));
		Check(SelectAtScreen(FVector2D(4, 4)) == nullptr && !GetSelected(), TEXT("empty click clears selection"));
		int32 Marked = 0;
		for (int32 i = 0; i < Machines.Num(); ++i)
		{
			SelectNext();
			Marked += GetSelected() == Machines[i].Get();
		}
		Check(Marked == Machines.Num(), TEXT("cycle follows line order"));
		SelectMachine(nullptr);
		int32 ColorMatches = 0;
		const UTelemetrySubsystem* Telemetry = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
		for (const TWeakObjectPtr<ATwinMachineActor>& Machine : Machines)
		{
			FTwinSample Sample;
			bool bFresh = false;
			Telemetry->GetLatest(Machine->MachineId, Sample, bFresh);
			ColorMatches += bFresh && Machine->GetBeaconColor().Equals(ATwinMachineActor::StatusColor(Sample.State, bFresh), 0.01f);
		}
		Check(ColorMatches >= Machines.Num() - 1, FString::Printf(TEXT("beacon color matches state %d/%d"), ColorMatches, Machines.Num()));
		bMeasure = true; // 화면 저장의 멈춤이 섞이지 않게 먼저 잰다.
		break;
	}
	case 1:
	{
		ReportFps(TEXT("overview"));
		Shot(TEXT("overview.png"));
		break;
	}
	case 2:
		SelectMachine(Machines[1].Get());
		FocusSelected();
		break;
	case 3:
		bMeasure = true;
		break;
	case 4:
		ReportFps(TEXT("detail"));
		Shot(TEXT("detail.png"));
		break;
	case 5:
		SelectMachine(Machines[6].Get());
		FocusSelected();
		break;
	case 6:
		Shot(TEXT("rack.png"));
		break;
	case 7:
		SetViewMode(ETwinViewMode::Overview);
		ToggleHeatmap();
		break;
	case 8:
		Shot(TEXT("heatmap.png"));
		break;
	case 9:
	{
		SetViewMode(ETwinViewMode::Drone);
		Check(!IsLookInputIgnored() && !IsMoveInputIgnored() && !bShowMouseCursor, TEXT("drone accepts input"));
		if (APawn* MyPawn = GetPawn())
		{
			MyPawn->SetActorLocation(FVector(0, -2100, 260));
			SetControlRotation(FRotator(0, 80, 0));
		}
		break;
	}
	case 10:
		bMeasure = true;
		break;
	case 11:
		ReportFps(TEXT("drone"));
		Shot(TEXT("drone.png"));
		break;
	case 12:
		SetViewMode(ETwinViewMode::Overview);
		Check(IsLookInputIgnored() && bShowMouseCursor, TEXT("overview restores cursor"));
		UE_LOG(LogTwinTour, Display, TEXT("TOUR DONE failures=%d"), TourFailures);
		break;
	default:
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
	}
#endif
}
