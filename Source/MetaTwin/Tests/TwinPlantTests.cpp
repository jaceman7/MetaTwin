#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TwinPlant.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TwinPlantFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FString StateName(ETwinMachineState State)
	{
		switch (State)
		{
		case ETwinMachineState::Running: return TEXT("running");
		case ETwinMachineState::Warning: return TEXT("warning");
		case ETwinMachineState::Fault: return TEXT("fault");
		case ETwinMachineState::Maintenance: return TEXT("maintenance");
		default: return TEXT("unknown");
		}
	}

	void Run(FTwinPlant& Plant, double Seconds)
	{
		for (int32 i = 0, N = FMath::RoundToInt32(Seconds / FTwinPlant::Dt); i < N; ++i) { Plant.Step(); }
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinGoldenTest, "MetaTwin.Plant.MatchesEmulator", TwinPlantFlags)
bool FTwinGoldenTest::RunTest(const FString&)
{
	TArray<FTwinMachineConfig> Configs;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!TestTrue(TEXT("layout"), TwinPlant::LoadConfigs(Configs))
		|| !TestTrue(TEXT("golden file"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / TEXT("Source/MetaTwin/Tests/plant_golden.json"))))
		|| !TestTrue(TEXT("golden json"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()))
	{
		return false;
	}
	FTwinPlant Plant;
	Plant.Init(Configs);

	const TArray<TSharedPtr<FJsonValue>>& Script = Root->GetArrayField(TEXT("script"));
	const TArray<TSharedPtr<FJsonValue>>& Rows = Root->GetArrayField(TEXT("rows"));
	TestTrue(TEXT("rows"), Rows.Num() > 40);
	int32 Step = 0;
	double MaxError = 0.0;
	for (const TSharedPtr<FJsonValue>& RowValue : Rows)
	{
		const TSharedPtr<FJsonObject> Row = RowValue->AsObject();
		for (const int32 End = Row->GetIntegerField(TEXT("step")); Step < End; ++Step)
		{
			for (const TSharedPtr<FJsonValue>& Item : Script)
			{
				const TSharedPtr<FJsonObject> Obj = Item->AsObject();
				if (Obj->GetIntegerField(TEXT("step")) == Step)
				{
					FTwinCommand Command;
					FString Error;
					TestTrue(TEXT("action"), TwinPlant::ParseAction(Obj->GetStringField(TEXT("action")), Command.Action));
					Command.MachineId = FName(*Obj->GetStringField(TEXT("machine")));
					Command.Value = Obj->GetNumberField(TEXT("value"));
					TestTrue(TEXT("apply"), Plant.Apply(Command, Error));
				}
			}
			Plant.Step();
		}
		MaxError = FMath::Max(MaxError, FMath::Abs(Plant.Produced - Row->GetNumberField(TEXT("produced"))));
		const TArray<TSharedPtr<FJsonValue>>& Buffers = Row->GetArrayField(TEXT("buffers"));
		for (int32 i = 0; i < Plant.Buffers.Num(); ++i)
		{
			MaxError = FMath::Max(MaxError, FMath::Abs(Plant.Buffers[i] - Buffers[i]->AsNumber()));
		}
		const TArray<TSharedPtr<FJsonValue>>& Machines = Row->GetArrayField(TEXT("machines"));
		const TArray<TSharedPtr<FJsonValue>>& States = Row->GetArrayField(TEXT("states"));
		for (int32 i = 0; i < Plant.Machines.Num(); ++i)
		{
			const FTwinMachineModel& M = Plant.Machines[i];
			const TArray<TSharedPtr<FJsonValue>>& V = Machines[i]->AsArray();
			const double Mine[] = {M.Temp, M.Throughput, M.Cooling, M.Wear, double(M.Trips)};
			for (int32 k = 0; k < 5; ++k) { MaxError = FMath::Max(MaxError, FMath::Abs(Mine[k] - V[k]->AsNumber())); }
			TestEqual(FString::Printf(TEXT("state %s @%d"), *M.Config.Id.ToString(), Step), StateName(M.GetState()), States[i]->AsString());
		}
	}
	TestTrue(FString::Printf(TEXT("max error %g"), MaxError), MaxError < 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinPlantRulesTest, "MetaTwin.Plant.Rules", TwinPlantFlags)
bool FTwinPlantRulesTest::RunTest(const FString&)
{
	TArray<FTwinMachineConfig> Configs;
	TwinPlant::LoadConfigs(Configs);
	FTwinPlant Plant;
	Plant.Init(Configs);
	FString Error;
	TestFalse(TEXT("unknown machine"), Plant.Apply({ETwinAction::Maintain, TEXT("NOPE"), 0}, Error));
	TestFalse(TEXT("speed range"), Plant.Apply({ETwinAction::SetSpeed, TEXT("COATER-01"), 1.5}, Error));
	TestFalse(TEXT("speed nan"), Plant.Apply({ETwinAction::SetSpeed, TEXT("COATER-01"), FMath::Sqrt(-1.0)}, Error));
	TestFalse(TEXT("bypass value"), Plant.Apply({ETwinAction::SetBypass, TEXT("COATER-01"), 0.5}, Error));
	TestTrue(TEXT("maintain"), Plant.Apply({ETwinAction::Maintain, TEXT("COATER-01"), 0}, Error));
	TestFalse(TEXT("maintain twice"), Plant.Apply({ETwinAction::Maintain, TEXT("COATER-01"), 0}, Error));

	Plant.Init(Configs);
	const double Before = [&] { double S = Plant.Produced; for (double B : Plant.Buffers) { S += B; } return S; }();
	double Input = 0.0;
	for (int32 i = 0; i < 600; ++i)
	{
		Plant.Step();
		Input += Plant.Machines[0].Throughput * FTwinPlant::Dt / 60.0;
	}
	double After = Plant.Produced;
	for (const double B : Plant.Buffers)
	{
		After += B;
		TestTrue(TEXT("buffer bounds"), B >= -1e-9 && B <= FTwinPlant::BufferCap + 1e-9);
	}
	TestEqual(TEXT("cells are conserved"), After, Before + Input, 1e-6);

	Plant.Apply({ETwinAction::InjectCooling, TEXT("PRESS-01"), 0}, Error);
	Run(Plant, 60);
	TestTrue(TEXT("cooling fault trips"), Plant.Find(TEXT("PRESS-01"))->Trips > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinWhatIfTest, "MetaTwin.WhatIf.Evaluate", TwinPlantFlags)
bool FTwinWhatIfTest::RunTest(const FString&)
{
	TArray<FTwinMachineConfig> Configs;
	TwinPlant::LoadConfigs(Configs);
	FTwinPlant Plant;
	Plant.Init(Configs);
	FString Error;
	const FName Id(TEXT("COATER-01"));
	Plant.Apply({ETwinAction::InjectCooling, Id, 0}, Error);
	Run(Plant, 40);

	FTwinFrame Frame;
	Plant.MakeSamples(Frame.Samples);
	FTwinPlant Synced;
	Synced.Init(Configs);
	TestTrue(TEXT("sync"), Synced.SyncFrom(Frame));
	const double BeforeTemp = Synced.Find(Id)->Temp;
	const TArray<FTwinWhatIfResult> Results = TwinWhatIf::Evaluate(Synced, Id, 180);
	if (!TestEqual(TEXT("options"), Results.Num(), 4)) { return false; }
	TestEqual(TEXT("input untouched"), Synced.Find(Id)->Temp, BeforeTemp);
	for (const FTwinWhatIfResult& R : Results)
	{
		AddInfo(FString::Printf(TEXT("%s output=%.1f trips=%d max=%.1f%s"), *R.Name.ToString(), R.Output, R.Trips, R.MaxTemp, R.bRecommended ? TEXT(" *") : TEXT("")));
	}
	TestTrue(TEXT("doing nothing trips again"), Results[0].Trips > 0);
	TestTrue(TEXT("maintenance recommended"), Results[3].bRecommended && Results[3].Trips == 0);
	TestEqual(TEXT("one recommendation"), Results.FilterByPredicate([](const FTwinWhatIfResult& R) { return R.bRecommended; }).Num(), 1);

	// 동기화한 모델의 예측은 원본을 실제로 진행한 결과와 같아야 한다.
	Plant.Apply({ETwinAction::Maintain, Id, 0}, Error);
	const double Start = Plant.Produced;
	Run(Plant, 180);
	TestEqual(TEXT("prediction matches actual"), Results[3].Output, Plant.Produced - Start, 1e-3);

	TestEqual(TEXT("unknown machine"), TwinWhatIf::Evaluate(Synced, TEXT("NOPE"), 10).Num(), 0);
	Frame.Samples.RemoveAt(0);
	TestFalse(TEXT("incomplete frame"), Synced.SyncFrom(Frame));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinTimelineTest, "MetaTwin.Timeline", TwinPlantFlags)
bool FTwinTimelineTest::RunTest(const FString&)
{
	FTwinTimeline Timeline;
	TestNull(TEXT("empty"), Timeline.FindAt(1.0));
	const int32 Total = FTwinTimeline::Capacity + 50;
	for (int32 i = 0; i < Total; ++i)
	{
		FTwinFrame Frame;
		FTwinSample& S = Frame.Samples.AddDefaulted_GetRef();
		S.MachineId = TEXT("M");
		S.Temperature = i;
		Timeline.Add(Frame, i * 0.1);
	}
	TestEqual(TEXT("bounded"), Timeline.Num(), int32(FTwinTimeline::Capacity));
	TestEqual(TEXT("start"), Timeline.GetStart(), 5.0, 1e-9);
	TestNull(TEXT("before start"), Timeline.FindAt(4.0));
	const FTwinTimeline::FEntry* Entry = Timeline.FindAt(100.05);
	if (TestNotNull(TEXT("between"), Entry)) { TestEqual(TEXT("latest not after"), Entry->Frame.Samples[0].Temperature, 1000.0); }
	TestEqual(TEXT("past end"), Timeline.FindAt(1e9)->Frame.Samples[0].Temperature, double(Total - 1));

	TArray<FTwinSample> Range;
	Timeline.GetRange(TEXT("M"), 100.05, 1.0, Range);
	if (TestEqual(TEXT("range"), Range.Num(), 10)) { TestEqual(TEXT("ordered"), Range.Last().Temperature, 1000.0); }
	Timeline.Add(FTwinFrame(), 1.0);
	TestEqual(TEXT("time never goes back"), Timeline.GetEnd(), (Total - 1) * 0.1, 1e-9);
	return true;
}

#endif
