#include "TwinTourActor.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Paths.h"
#include "TelemetrySubsystem.h"
#include "TwinHudWidget.h"
#include "TwinMachineActor.h"
#include "TwinPlayerController.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogTwinTour, Log, All);

namespace
{
	constexpr double VerifySeconds = 60.0;
}

ATwinTourActor::ATwinTourActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ATwinTourActor::Check(bool bOk, const FString& What)
{
	Failures += bOk ? 0 : 1;
	UE_LOG(LogTwinTour, Display, TEXT("%s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
}

void ATwinTourActor::Shot(const TCHAR* Name) const
{
	const UTelemetrySubsystem* Telemetry = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
	FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("M3") / (Telemetry->IsLocalSimulation() ? TEXT("local_") : TEXT("server_")) + Name, true, false);
}

void ATwinTourActor::Next(double Delay)
{
	++Step;
	StepAt = UTelemetrySubsystem::Now();
	WaitUntil = StepAt + Delay;
}

void ATwinTourActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = UTelemetrySubsystem::Now();
	if (StepAt == 0.0) { StepAt = Now; WaitUntil = Now + 10.0; }
	if (Now < WaitUntil) { return; }

	ATwinPlayerController* PC = Cast<ATwinPlayerController>(GetWorld()->GetFirstPlayerController());
	UTelemetrySubsystem* T = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
	ATwinMachineActor* Coater = PC->GetMachines()[1].Get();
	const FName Id = Coater->MachineId;
	FTwinSample S;
	bool bFresh = false;
	T->GetLatest(Id, S, bFresh);
	FString Error;

	switch (Step)
	{
	case 0:
		Check(bFresh && S.State == ETwinMachineState::Running, FString::Printf(TEXT("running, local=%d"), T->IsLocalSimulation()));
		PC->SelectMachine(Coater);
		PC->FocusSelected();
		T->SendCommand({ETwinAction::SetSpeed, Id, 9.0}, Error);
		Next(1.5);
		break;
	case 1:
		Check(T->GetCommands().Num() == 1 && T->GetCommands().Last().State == ETwinCommandState::Failed, TEXT("bad command rejected by plant"));
		PC->GetHud()->Command(ETwinAction::InjectCooling, 0);
		Next(1.5);
		break;
	case 2:
		Check(T->GetCommands().Last().State == ETwinCommandState::Accepted, TEXT("inject acknowledged"));
		Next();
		break;
	case 3:
		if (S.State == ETwinMachineState::Fault || Now - StepAt > 90.0)
		{
			Check(S.State == ETwinMachineState::Fault, FString::Printf(TEXT("fault after %.0fs, %.1f C"), Now - StepAt, S.Temperature));
			FaultAt = Now;
			Next(12.0); // 병목이 다음 공정으로 번질 때까지
		}
		break;
	case 4:
	{
		FTwinSample Downstream;
		bool bDownFresh = false;
		T->GetLatest(PC->GetMachines()[2]->MachineId, Downstream, bDownFresh);
		Check(Downstream.Throughput < 1.0, FString::Printf(TEXT("next machine starved, throughput=%.1f"), Downstream.Throughput));
		TArray<FTwinWhatIfResult> Short;
		Check(T->EvaluateWhatIf(Id, VerifySeconds, Short) && Short.Num() == 4, TEXT("what-if available"));
		PC->GetHud()->RunWhatIf();
		for (const FTwinWhatIfResult& R : PC->GetHud()->GetWhatIf())
		{
			UE_LOG(LogTwinTour, Display, TEXT("what-if %s output=%.1f trips=%d%s"), *R.Name.ToString(), R.Output, R.Trips, R.bRecommended ? TEXT(" *") : TEXT(""));
		}
		const TArray<FTwinWhatIfResult>& Full = PC->GetHud()->GetWhatIf();
		Check(Full.Num() == 4 && Full[3].bRecommended, TEXT("maintenance recommended"));
		Predicted = Short.Num() == 4 ? Short[3].Output : 0.0;
		Next(0.5);
		break;
	}
	case 5:
		Shot(TEXT("whatif.png"));
		T->GetProduced(ApplyProduced);
		ApplyAt = Now;
		PC->GetHud()->ApplyWhatIf(3);
		Next(3.0);
		break;
	case 6:
		Check(S.State == ETwinMachineState::Maintenance, TEXT("maintenance started"));
		Next(VerifySeconds - 3.0);
		break;
	case 7:
	{
		double Produced = 0.0;
		T->GetProduced(Produced);
		const double Actual = (Produced - ApplyProduced) * VerifySeconds / (Now - ApplyAt);
		Check(FMath::Abs(Actual - Predicted) <= FMath::Max(1.0, Predicted * 0.1), FString::Printf(TEXT("prediction %.1f vs actual %.1f in %.0fs"), Predicted, Actual, VerifySeconds));
		Check(S.State == ETwinMachineState::Running, TEXT("recovered"));
		T->SetReplay(true);
		T->SetPaused(true);
		T->SeekTo(FaultAt + 0.5);
		Next(0.5);
		break;
	}
	case 8:
	{
		Check(T->IsReplay() && S.State == ETwinMachineState::Fault, TEXT("replay shows past fault"));
		const bool bSent = T->SendCommand({ETwinAction::Maintain, Id, 0}, Error);
		Check(!bSent, TEXT("commands blocked in replay"));
		Check(Coater->GetBeaconColor().Equals(ATwinMachineActor::StatusColor(ETwinMachineState::Fault, true), 0.01f), TEXT("3D follows replay"));
		PC->SetViewMode(ETwinViewMode::Overview);
		Next(3.0);
		break;
	}
	case 9:
		Shot(TEXT("replay.png"));
		Next(1.0);
		break;
	case 10:
		T->SetPaused(false);
		Next(3.0);
		break;
	case 11:
		Check(FMath::Abs(T->GetViewTime() - (FaultAt + 0.5) - (Now - WaitUntil + 3.0)) < 0.5, TEXT("replay advances in real time"));
		T->SetReplay(false);
		Next(0.5);
		break;
	case 12:
		Check(!T->IsReplay() && bFresh && S.State == ETwinMachineState::Running, TEXT("back to live"));
		UE_LOG(LogTwinTour, Display, TEXT("TOUR DONE failures=%d"), Failures);
		Next(1.0);
		break;
	default:
		UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
	}
}
