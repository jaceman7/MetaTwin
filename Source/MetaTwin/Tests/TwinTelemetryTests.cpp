#include "Misc/AutomationTest.h"
#include "TwinRegistry.h"
#include "TwinTelemetry.h"

#if WITH_DEV_AUTOMATION_TESTS

#define TWIN_EXTRA ",\"capacity\":42,\"speed\":1,\"cooling\":0.5,\"wear\":1,\"maint\":0,\"buffer\":6,\"bypass\":true"

namespace
{
	constexpr EAutomationTestFlags TwinTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FTwinFrame MakeFrame(const TCHAR* Session, int64 Seq, double Temperature = 1.0)
	{
		FTwinFrame Frame;
		Frame.Session = Session;
		Frame.Seq = Seq;
		FTwinSample& Sample = Frame.Samples.AddDefaulted_GetRef();
		Sample.MachineId = TEXT("M1");
		Sample.Temperature = Temperature;
		return Frame;
	}

	FString MakeJson(const TCHAR* Machine, const TCHAR* Seq = TEXT("5"))
	{
		return FString::Printf(TEXT("{\"type\":\"telemetry\",\"v\":2,\"session\":\"s\",\"seq\":%s,\"ts\":1700000000000,\"produced\":12.5,\"machines\":[%s]}"), Seq, Machine);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinParseTest, "MetaTwin.Telemetry.Parse", TwinTestFlags)
bool FTwinParseTest::RunTest(const FString&)
{
	const TCHAR* Good = TEXT("{\"id\":\"M1\",\"state\":\"warning\",\"temperature\":81.5,\"vibration\":2.5,\"throughput\":40" TWIN_EXTRA "}");
	FTwinFrame Frame;
	FString Error;
	if (!TestTrue(TEXT("valid"), TwinTelemetry::ParseFrame(MakeJson(Good), Frame, Error)) || !TestEqual(TEXT("count"), Frame.Samples.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("seq"), Frame.Seq, (int64)5);
	TestEqual(TEXT("temp"), Frame.Samples[0].Temperature, 81.5);
	TestTrue(TEXT("state"), Frame.Samples[0].State == ETwinMachineState::Warning);
	TestEqual(TEXT("time"), Frame.Samples[0].SourceTimeMs, (int64)1700000000000);

	TestTrue(TEXT("extra"), Frame.Samples[0].bBypass && Frame.Samples[0].Cooling == 0.5 && Frame.Produced == 12.5);

	const FString Bad[] = {
		MakeJson(TEXT("{\"id\":\"M1\",\"state\":\"running\",\"temperature\":1,\"vibration\":1,\"throughput\":1}")),
		TEXT(""), TEXT("not json"), TEXT("[]"), TEXT("{}"),
		MakeJson(Good, TEXT("-1")), MakeJson(Good, TEXT("1.5")), MakeJson(Good, TEXT("\"5\"")),
		MakeJson(TEXT("null")),
		MakeJson(TEXT("{\"id\":\"M1\",\"state\":\"melting\",\"temperature\":1,\"vibration\":1,\"throughput\":1" TWIN_EXTRA "}")),
		MakeJson(TEXT("{\"id\":\"M1\",\"state\":\"running\",\"temperature\":\"hot\",\"vibration\":1,\"throughput\":1" TWIN_EXTRA "}")),
		MakeJson(TEXT("{\"id\":\"M1\",\"state\":\"running\",\"temperature\":1,\"vibration\":1" TWIN_EXTRA "}")),
		MakeJson(TEXT("{\"id\":\"\",\"state\":\"running\",\"temperature\":1,\"vibration\":1,\"throughput\":1" TWIN_EXTRA "}")),
	};
	for (const FString& Json : Bad)
	{
		TestFalse(FString::Printf(TEXT("rejects %s"), *Json), TwinTelemetry::ParseFrame(Json, Frame, Error));
	}
	TestEqual(TEXT("output untouched on failure"), Frame.Seq, (int64)5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinOrderTest, "MetaTwin.Registry.Ordering", TwinTestFlags)
bool FTwinOrderTest::RunTest(const FString&)
{
	FTwinRegistry Registry;
	TestTrue(TEXT("first"), Registry.Apply(MakeFrame(TEXT("A"), 10, 10), 1.0));
	TestFalse(TEXT("duplicate"), Registry.Apply(MakeFrame(TEXT("A"), 10, 99), 2.0));
	TestFalse(TEXT("late"), Registry.Apply(MakeFrame(TEXT("A"), 9, 99), 2.0));
	TestEqual(TEXT("value kept"), Registry.Find(TEXT("M1"))->Latest.Temperature, 10.0);
	TestEqual(TEXT("rejected frames do not refresh"), Registry.Find(TEXT("M1"))->ReceivedAt, 1.0);
	TestTrue(TEXT("gap accepted"), Registry.Apply(MakeFrame(TEXT("A"), 13), 3.0));
	TestEqual(TEXT("gaps"), Registry.GapCount, (int64)2);
	TestTrue(TEXT("restart with lower seq"), Registry.Apply(MakeFrame(TEXT("B"), 1, 20), 4.0));
	TestFalse(TEXT("old session"), Registry.Apply(MakeFrame(TEXT("A"), 100, 99), 5.0));
	TestEqual(TEXT("new session value"), Registry.Find(TEXT("M1"))->Latest.Temperature, 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinFreshTest, "MetaTwin.Registry.Freshness", TwinTestFlags)
bool FTwinFreshTest::RunTest(const FString&)
{
	FTwinRegistry Registry;
	TestFalse(TEXT("unknown machine"), Registry.IsFresh(TEXT("M1"), 0.0, 1.5));
	Registry.Apply(MakeFrame(TEXT("A"), 1), 100.0);
	TestTrue(TEXT("fresh"), Registry.IsFresh(TEXT("M1"), 101.5, 1.5));
	TestFalse(TEXT("stale"), Registry.IsFresh(TEXT("M1"), 101.6, 1.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinAckTest, "MetaTwin.Telemetry.Commands", TwinTestFlags)
bool FTwinAckTest::RunTest(const FString&)
{
	FString Id, Error;
	bool bOk = true;
	TestTrue(TEXT("ack"), TwinTelemetry::ParseAck(TEXT("{\"type\":\"ack\",\"v\":2,\"id\":\"x\",\"ok\":false,\"error\":\"no\"}"), Id, bOk, Error));
	TestTrue(TEXT("fields"), Id == TEXT("x") && !bOk && Error == TEXT("no"));
	TestFalse(TEXT("not ack"), TwinTelemetry::ParseAck(TEXT("{\"type\":\"telemetry\"}"), Id, bOk, Error));
	TestFalse(TEXT("garbage"), TwinTelemetry::ParseAck(TEXT("ack"), Id, bOk, Error));
	TestEqual(TEXT("command"), TwinTelemetry::MakeCommand(TEXT("x"), {ETwinAction::SetSpeed, TEXT("COATER-01"), 0.5}),
		FString(TEXT("{\"type\":\"command\",\"v\":2,\"id\":\"x\",\"action\":\"set_speed\",\"machine\":\"COATER-01\",\"value\":0.5000}")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTwinBackoffTest, "MetaTwin.Telemetry.Backoff", TwinTestFlags)
bool FTwinBackoffTest::RunTest(const FString&)
{
	TestEqual(TEXT("first"), TwinTelemetry::ReconnectDelay(0, 0.5), 0.5, 1e-9);
	TestEqual(TEXT("doubles"), TwinTelemetry::ReconnectDelay(2, 0.5), 2.0, 1e-9);
	TestEqual(TEXT("capped"), TwinTelemetry::ReconnectDelay(1000, 0.5), 8.0, 1e-9);
	TestEqual(TEXT("jitter low"), TwinTelemetry::ReconnectDelay(0, 0.0), 0.4, 1e-9);
	TestEqual(TEXT("jitter high"), TwinTelemetry::ReconnectDelay(0, 1.0), 0.6, 1e-9);
	return true;
}

#endif
