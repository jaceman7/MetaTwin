#include "TwinTelemetry.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ParseState(const FString& Text, ETwinMachineState& Out)
	{
		if (Text == TEXT("running")) { Out = ETwinMachineState::Running; return true; }
		if (Text == TEXT("warning")) { Out = ETwinMachineState::Warning; return true; }
		if (Text == TEXT("maintenance")) { Out = ETwinMachineState::Maintenance; return true; }
		if (Text == TEXT("fault")) { Out = ETwinMachineState::Fault; return true; }
		return false;
	}

	bool ReadFinite(const FJsonObject& Obj, const TCHAR* Key, double& Out)
	{
		const TSharedPtr<FJsonValue> Value = Obj.TryGetField(Key);
		return Value && Value->Type == EJson::Number && Value->TryGetNumber(Out) && FMath::IsFinite(Out);
	}

	bool ReadString(const FJsonObject& Obj, const TCHAR* Key, FString& Out)
	{
		const TSharedPtr<FJsonValue> Value = Obj.TryGetField(Key);
		return Value && Value->Type == EJson::String && Value->TryGetString(Out) && !Out.IsEmpty() && Out.Len() <= 64;
	}

	/** double로 정확히 표현되는 음이 아닌 정수만 허용한다. */
	bool ReadCounter(const FJsonObject& Obj, const TCHAR* Key, int64& Out)
	{
		double Number = 0.0;
		if (!ReadFinite(Obj, Key, Number) || Number < 0.0 || Number > 9007199254740991.0 || FMath::Frac(Number) != 0.0)
		{
			return false;
		}
		Out = static_cast<int64>(Number);
		return true;
	}
}

bool TwinTelemetry::ParseFrame(const FString& Json, FTwinFrame& OutFrame, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root)
	{
		OutError = TEXT("invalid json");
		return false;
	}

	FTwinFrame Frame;
	FString Type;
	int64 Version = 0;
	int64 TimeMs = 0;
	if (!ReadString(*Root, TEXT("type"), Type) || Type != TEXT("telemetry") || !ReadCounter(*Root, TEXT("v"), Version) || Version != 2)
	{
		OutError = TEXT("unsupported message");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Machines = nullptr;
	if (!ReadString(*Root, TEXT("session"), Frame.Session) || !ReadCounter(*Root, TEXT("seq"), Frame.Seq)
		|| !ReadCounter(*Root, TEXT("ts"), TimeMs) || !ReadFinite(*Root, TEXT("produced"), Frame.Produced) || !Root->TryGetArrayField(TEXT("machines"), Machines) || Machines->Num() > 1024)
	{
		OutError = TEXT("invalid header");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Machines)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		FTwinSample Sample;
		FString Id, State;
		if (!Value || !Value->TryGetObject(Obj) || !ReadString(**Obj, TEXT("id"), Id) || !ReadString(**Obj, TEXT("state"), State)
			|| !ParseState(State, Sample.State) || !ReadFinite(**Obj, TEXT("temperature"), Sample.Temperature)
			|| !ReadFinite(**Obj, TEXT("vibration"), Sample.Vibration) || !ReadFinite(**Obj, TEXT("throughput"), Sample.Throughput)
			|| !ReadFinite(**Obj, TEXT("capacity"), Sample.Capacity) || !ReadFinite(**Obj, TEXT("speed"), Sample.Speed)
			|| !ReadFinite(**Obj, TEXT("cooling"), Sample.Cooling) || !ReadFinite(**Obj, TEXT("wear"), Sample.Wear)
			|| !ReadFinite(**Obj, TEXT("maint"), Sample.Maint) || !ReadFinite(**Obj, TEXT("buffer"), Sample.Buffer)
			|| !(*Obj)->TryGetBoolField(TEXT("bypass"), Sample.bBypass))
		{
			OutError = TEXT("invalid machine entry");
			return false;
		}
		Sample.MachineId = FName(*Id);
		Sample.SourceTimeMs = TimeMs;
		Frame.Samples.Add(Sample);
	}

	OutFrame = MoveTemp(Frame);
	return true;
}

bool TwinTelemetry::ParseAck(const FString& Json, FString& OutId, bool& bOutOk, FString& OutError)
{
	if (Json.Len() > 1024)
	{
		return false; // 텔레메트리를 두 번 해석하지 않는다.
	}
	TSharedPtr<FJsonObject> Root;
	FString Type;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root && ReadString(*Root, TEXT("type"), Type) && Type == TEXT("ack")
		&& ReadString(*Root, TEXT("id"), OutId) && Root->TryGetBoolField(TEXT("ok"), bOutOk) && Root->TryGetStringField(TEXT("error"), OutError);
}

FString TwinTelemetry::MakeCommand(const FString& Id, const FTwinCommand& Command)
{
	// 식별자는 GUID와 레이아웃의 설비 ID뿐이라 이스케이프할 문자가 없다.
	return FString::Printf(TEXT("{\"type\":\"command\",\"v\":2,\"id\":\"%s\",\"action\":\"%s\",\"machine\":\"%s\",\"value\":%.4f}"),
		*Id, TwinPlant::ActionName(Command.Action), *Command.MachineId.ToString(), Command.Value);
}

double TwinTelemetry::ReconnectDelay(int32 Attempt, double Jitter01)
{
	const double Base = FMath::Min(8.0, 0.5 * FMath::Pow(2.0, static_cast<double>(FMath::Clamp(Attempt, 0, 10))));
	return Base * (0.8 + 0.4 * FMath::Clamp(Jitter01, 0.0, 1.0));
}
