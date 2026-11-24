#include "Misc/AutomationTest.h"
#include "TwinLine.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TwinLineFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FTwinMachineView MakeView(const TCHAR* Id, ETwinMachineState State, double Capacity, double Throughput)
	{
		FTwinMachineView View;
		View.Sample.MachineId = Id;
		View.Sample.State = State;
		View.Sample.Capacity = Capacity;
		View.Sample.Throughput = Throughput;
		View.bHasData = View.bFresh = true;
		return View;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinSummaryTest, "MetaTwin.Line.Summary", TwinLineFlags)
bool FTwinSummaryTest::RunTest(const FString&)
{
	TestFalse(TEXT("no machines"), TwinLine::Summarize({}).bComplete);
	TArray<FTwinMachineView> Views = {MakeView(TEXT("A"), ETwinMachineState::Running, 40, 30), MakeView(TEXT("B"), ETwinMachineState::Warning, 28, 30),
		MakeView(TEXT("C"), ETwinMachineState::Maintenance, 35, 29), MakeView(TEXT("D"), ETwinMachineState::Fault, 45, 27)};
	const FTwinLineSummary Full = TwinLine::Summarize(Views);
	TestTrue(TEXT("complete"), Full.bComplete);
	TestEqual(TEXT("output of last machine"), Full.Throughput, 27.0);
	TestEqual(TEXT("bottleneck by capacity"), Full.Bottleneck, FName(TEXT("B")));
	TestTrue(TEXT("counts"), Full.Running == 1 && Full.Warning == 1 && Full.Maintenance == 1 && Full.Fault == 1 && Full.Unknown == 0);

	Views[1].bFresh = false;
	const FTwinLineSummary Partial = TwinLine::Summarize(Views);
	TestFalse(TEXT("stale machine makes summary incomplete"), Partial.bComplete);
	TestEqual(TEXT("one unknown"), Partial.Unknown, 1);
	TestTrue(TEXT("no bottleneck claim"), Partial.Bottleneck.IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinAlarmTest, "MetaTwin.Line.Alarms", TwinLineFlags)
bool FTwinAlarmTest::RunTest(const FString&)
{
	FTwinAlarmLog Log;
	const FName Id(TEXT("A"));
	TestFalse(TEXT("never seen"), Log.Update(Id, ETwinMachineState::Unknown, false, 0.0));
	TestFalse(TEXT("never seen stays quiet"), Log.Update(Id, ETwinMachineState::Unknown, false, 5.0));
	TestFalse(TEXT("running"), Log.Update(Id, ETwinMachineState::Running, true, 6.0));

	for (int32 i = 0; i < 20; ++i) // 0.1초마다 떨리는 값
	{
		TestFalse(TEXT("chatter"), Log.Update(Id, i % 2 ? ETwinMachineState::Running : ETwinMachineState::Warning, true, 7.0 + i * 0.1));
	}
	TestFalse(TEXT("candidate"), Log.Update(Id, ETwinMachineState::Fault, true, 10.0));
	TestTrue(TEXT("confirmed"), Log.Update(Id, ETwinMachineState::Fault, true, 10.7));
	TestFalse(TEXT("once only"), Log.Update(Id, ETwinMachineState::Fault, true, 12.0));
	Log.Update(Id, ETwinMachineState::Fault, false, 13.0);
	TestTrue(TEXT("data lost"), Log.Update(Id, ETwinMachineState::Fault, false, 14.0));
	Log.Update(Id, ETwinMachineState::Running, true, 15.0);
	TestTrue(TEXT("recovered"), Log.Update(Id, ETwinMachineState::Running, true, 16.0));
	if (TestEqual(TEXT("count"), Log.GetAlarms().Num(), 3))
	{
		TestTrue(TEXT("order"), Log.GetAlarms()[0].Level == ETwinAlarmLevel::Fault && Log.GetAlarms()[1].Level == ETwinAlarmLevel::DataLost
			&& Log.GetAlarms()[2].Level == ETwinAlarmLevel::Recovered);
	}

	double Now = 20.0;
	for (int32 i = 0; i < FTwinAlarmLog::Capacity * 2 + 5; ++i, Now += 1.0)
	{
		const ETwinMachineState State = i % 2 ? ETwinMachineState::Running : ETwinMachineState::Fault;
		Log.Update(Id, State, true, Now);
		Log.Update(Id, State, true, Now + 0.7);
	}
	TestEqual(TEXT("bounded"), Log.GetAlarms().Num(), int32(FTwinAlarmLog::Capacity));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinHeatTest, "MetaTwin.Line.HeatColor", TwinLineFlags)
bool FTwinHeatTest::RunTest(const FString&)
{
	const FLinearColor Cold = TwinLine::HeatColor(-50.0), Mid = TwinLine::HeatColor(65.0), Hot = TwinLine::HeatColor(500.0);
	TestTrue(TEXT("cold is blue"), Cold.B > Cold.R && Cold.B > Cold.G);
	TestTrue(TEXT("mid is green"), Mid.G > Mid.R && Mid.G > Mid.B);
	TestTrue(TEXT("hot is red"), Hot.R > Hot.G && Hot.R > Hot.B);
	return true;
}

#endif
