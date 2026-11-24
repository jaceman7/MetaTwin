#include "TwinPlant.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// 아래 상수와 수식은 Tools/emulator/plant.py와 같아야 한다.
namespace
{
	constexpr double BufferInit = 6.0;
	constexpr double Tau = 12.0;
	constexpr double WarnRise = 15.0;
	constexpr double TripRise = 22.0;
	constexpr double ResetRise = 4.0;
	constexpr double Ramp = 0.05;
	constexpr double CoolingFault = 0.25;
	constexpr double WearFault = 0.5;
	constexpr double BypassRate = 0.7;
	constexpr double MaintSeconds = 20.0;
	constexpr double SpeedMin = 0.3;
	constexpr double SpeedMax = 1.0;

	double Approach(double Value, double Target, double Step)
	{
		return Value < Target ? FMath::Min(Target, Value + Step) : FMath::Max(Target, Value - Step);
	}

	struct FActionName { ETwinAction Action; const TCHAR* Name; };
	const FActionName ActionNames[] = {
		{ETwinAction::InjectCooling, TEXT("inject_cooling")},
		{ETwinAction::InjectWear, TEXT("inject_wear")},
		{ETwinAction::SetSpeed, TEXT("set_speed")},
		{ETwinAction::SetBypass, TEXT("set_bypass")},
		{ETwinAction::Maintain, TEXT("maintain")},
	};
}

ETwinMachineState FTwinMachineModel::GetState() const
{
	if (Maint > 0.0) { return ETwinMachineState::Maintenance; }
	if (bTripped) { return ETwinMachineState::Fault; }
	return Temp > Config.Base + WarnRise || Cooling < 0.8 || Wear < 0.8 ? ETwinMachineState::Warning : ETwinMachineState::Running;
}

double FTwinMachineModel::GetVibration() const
{
	const double Load = bBypass ? 0.0 : Throughput / Config.Rate;
	return Config.Vibration * (1.0 + 0.6 * Load + 1.5 * (1.0 - Wear));
}

void FTwinPlant::Init(const TArray<FTwinMachineConfig>& Configs)
{
	Machines.Reset();
	for (const FTwinMachineConfig& Config : Configs)
	{
		FTwinMachineModel& M = Machines.AddDefaulted_GetRef();
		M.Config = Config;
		M.Temp = Config.Base + Config.Heat;
	}
	Buffers.Init(BufferInit, FMath::Max(0, Machines.Num() - 1));
	Produced = 0.0;
}

void FTwinPlant::Step()
{
	const int32 Last = Machines.Num() - 1;
	for (int32 i = Last; i >= 0; --i) // 뒤 공정부터 당겨 간다.
	{
		FTwinMachineModel& M = Machines[i];
		const bool bMaintaining = M.Maint > 0.0;
		if (bMaintaining)
		{
			M.Maint = FMath::Max(0.0, M.Maint - Dt);
			if (M.Maint < 1e-9)
			{
				M.Maint = 0.0;
				M.Cooling = M.CoolingTarget = M.Wear = M.WearTarget = 1.0;
			}
		}
		M.Cooling = Approach(M.Cooling, M.CoolingTarget, Ramp * Dt);
		M.Wear = Approach(M.Wear, M.WearTarget, Ramp * Dt);
		if (M.bTripped && M.Temp < M.Config.Base + ResetRise)
		{
			M.bTripped = false;
		}

		M.Capacity = M.bBypass ? M.Config.Rate * BypassRate : bMaintaining || M.bTripped ? 0.0 : M.Config.Rate * M.Speed * M.Wear;
		double Amount = M.Capacity * Dt / 60.0;
		if (i > 0) { Amount = FMath::Min(Amount, Buffers[i - 1]); }
		if (i < Last) { Amount = FMath::Min(Amount, BufferCap - Buffers[i]); }
		if (i > 0) { Buffers[i - 1] -= Amount; }
		if (i < Last) { Buffers[i] += Amount; } else { Produced += Amount; }
		M.Throughput = Amount * 60.0 / Dt;

		const double Load = M.bBypass ? 0.0 : M.Throughput / M.Config.Rate;
		M.Temp += (M.Config.Base + M.Config.Heat * Load / M.Cooling - M.Temp) * Dt / Tau;
		if (!M.bBypass && !bMaintaining && !M.bTripped && M.Temp > M.Config.Base + TripRise)
		{
			M.bTripped = true;
			++M.Trips;
		}
	}
}

bool FTwinPlant::Apply(const FTwinCommand& Command, FString& OutError)
{
	FTwinMachineModel* M = Machines.FindByPredicate([&](const FTwinMachineModel& X) { return X.Config.Id == Command.MachineId; });
	if (!M)
	{
		OutError = TEXT("unknown machine");
		return false;
	}
	switch (Command.Action)
	{
	case ETwinAction::InjectCooling:
		M->CoolingTarget = CoolingFault;
		return true;
	case ETwinAction::InjectWear:
		M->WearTarget = WearFault;
		return true;
	case ETwinAction::SetSpeed:
		if (!(Command.Value >= SpeedMin && Command.Value <= SpeedMax))
		{
			OutError = TEXT("speed out of range");
			return false;
		}
		M->Speed = Command.Value;
		return true;
	case ETwinAction::SetBypass:
		if (Command.Value != 0.0 && Command.Value != 1.0)
		{
			OutError = TEXT("bypass must be 0 or 1");
			return false;
		}
		M->bBypass = Command.Value == 1.0;
		return true;
	case ETwinAction::Maintain:
		if (M->Maint > 0.0)
		{
			OutError = TEXT("already in maintenance");
			return false;
		}
		M->Maint = MaintSeconds;
		M->bTripped = false;
		return true;
	}
	OutError = TEXT("unknown action");
	return false;
}

bool FTwinPlant::SyncFrom(const FTwinFrame& Frame)
{
	for (int32 i = 0; i < Machines.Num(); ++i)
	{
		FTwinMachineModel& M = Machines[i];
		const FTwinSample* S = Frame.Samples.FindByPredicate([&](const FTwinSample& X) { return X.MachineId == M.Config.Id; });
		if (!S || S->Cooling <= 0.0 || S->Wear <= 0.0)
		{
			return false;
		}
		M.Temp = S->Temperature;
		M.Speed = S->Speed;
		M.Cooling = S->Cooling;
		M.CoolingTarget = S->Cooling < 1.0 ? CoolingFault : 1.0;
		M.Wear = S->Wear;
		M.WearTarget = S->Wear < 1.0 ? WearFault : 1.0;
		M.Maint = S->Maint;
		M.bBypass = S->bBypass;
		M.bTripped = S->State == ETwinMachineState::Fault;
		M.Throughput = S->Throughput;
		M.Capacity = S->Capacity;
		M.Trips = 0;
		if (Buffers.IsValidIndex(i))
		{
			Buffers[i] = FMath::Clamp(S->Buffer, 0.0, BufferCap);
		}
	}
	Produced = 0.0;
	return true;
}

void FTwinPlant::MakeSamples(TArray<FTwinSample>& Out) const
{
	Out.Reset();
	for (int32 i = 0; i < Machines.Num(); ++i)
	{
		const FTwinMachineModel& M = Machines[i];
		FTwinSample& S = Out.AddDefaulted_GetRef();
		S.MachineId = M.Config.Id;
		S.State = M.GetState();
		S.Temperature = M.Temp;
		S.Vibration = M.GetVibration();
		S.Throughput = M.Throughput;
		S.Capacity = M.Capacity;
		S.Speed = M.Speed;
		S.Cooling = M.Cooling;
		S.Wear = M.Wear;
		S.Maint = M.Maint;
		S.bBypass = M.bBypass;
		S.Buffer = Buffers.IsValidIndex(i) ? Buffers[i] : 0.0;
	}
}

const FTwinMachineModel* FTwinPlant::Find(FName Id) const
{
	return Machines.FindByPredicate([&](const FTwinMachineModel& X) { return X.Config.Id == Id; });
}

bool TwinPlant::LoadConfigs(TArray<FTwinMachineConfig>& Out)
{
	Out.Reset();
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/plant_layout.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root || !Root->TryGetArrayField(TEXT("machines"), List))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		FString Id;
		FTwinMachineConfig C;
		if (!Value->TryGetObject(Obj) || !(*Obj)->TryGetStringField(TEXT("id"), Id) || !(*Obj)->TryGetNumberField(TEXT("temp"), C.Base)
			|| !(*Obj)->TryGetNumberField(TEXT("vib"), C.Vibration) || !(*Obj)->TryGetNumberField(TEXT("rate"), C.Rate)
			|| !(*Obj)->TryGetNumberField(TEXT("heat"), C.Heat) || C.Rate <= 0.0)
		{
			Out.Reset();
			return false;
		}
		C.Id = FName(*Id);
		Out.Add(C);
	}
	return Out.Num() > 0;
}

const TCHAR* TwinPlant::ActionName(ETwinAction Action)
{
	for (const FActionName& Entry : ActionNames)
	{
		if (Entry.Action == Action) { return Entry.Name; }
	}
	return TEXT("");
}

bool TwinPlant::ParseAction(const FString& Name, ETwinAction& Out)
{
	for (const FActionName& Entry : ActionNames)
	{
		if (Name == Entry.Name) { Out = Entry.Action; return true; }
	}
	return false;
}

TArray<FTwinWhatIfResult> TwinWhatIf::Evaluate(const FTwinPlant& Snapshot, FName MachineId, double Horizon)
{
	TArray<FTwinWhatIfResult> Results;
	const auto Add = [&](const FText& Name, TArray<FTwinCommand> Commands)
	{
		FTwinWhatIfResult& R = Results.AddDefaulted_GetRef();
		R.Name = Name;
		R.Commands = MoveTemp(Commands);
	};
	Add(INVTEXT("현 상태 유지"), {});
	Add(INVTEXT("50% 감속"), {{ETwinAction::SetSpeed, MachineId, 0.5}});
	Add(INVTEXT("우회 (70%)"), {{ETwinAction::SetBypass, MachineId, 1.0}});
	Add(INVTEXT("정비 (20초)"), {{ETwinAction::Maintain, MachineId, 0.0}});

	const int32 Steps = FMath::Max(1, FMath::RoundToInt32(Horizon / FTwinPlant::Dt));
	for (int32 r = Results.Num() - 1; r >= 0; --r)
	{
		FTwinWhatIfResult& R = Results[r];
		FTwinPlant Plant = Snapshot;
		Plant.Produced = 0.0;
		for (FTwinMachineModel& M : Plant.Machines) { M.Trips = 0; }
		bool bOk = Plant.Find(MachineId) != nullptr;
		FString Error;
		for (const FTwinCommand& Command : R.Commands)
		{
			bOk = bOk && Plant.Apply(Command, Error);
		}
		if (!bOk)
		{
			Results.RemoveAt(r); // 지금 적용할 수 없는 조치
			continue;
		}
		R.MaxTemp = Plant.Find(MachineId)->Temp;
		for (int32 i = 0; i < Steps; ++i)
		{
			Plant.Step();
			R.MaxTemp = FMath::Max(R.MaxTemp, Plant.Find(MachineId)->Temp);
		}
		R.Output = Plant.Produced;
		for (const FTwinMachineModel& M : Plant.Machines) { R.Trips += M.Trips; }
	}

	// 고장 정지가 가장 적은 조치 중 생산량이 가장 많은 것을 권한다.
	FTwinWhatIfResult* Best = nullptr;
	for (FTwinWhatIfResult& R : Results)
	{
		if (!Best || R.Trips < Best->Trips || (R.Trips == Best->Trips && R.Output > Best->Output + 1e-6))
		{
			Best = &R;
		}
	}
	if (Best) { Best->bRecommended = true; }
	return Results;
}

void FTwinTimeline::Add(const FTwinFrame& Frame, double Time)
{
	if (!IsEmpty() && Time < GetEnd())
	{
		return;
	}
	if (Entries.Num() < Capacity)
	{
		Entries.Add({Time, Frame});
	}
	else
	{
		Entries[Head] = {Time, Frame};
		Head = (Head + 1) % Capacity;
	}
}

int32 FTwinTimeline::IndexAt(double Time) const
{
	int32 Low = 0, High = Entries.Num();
	while (Low < High)
	{
		const int32 Mid = (Low + High) / 2;
		if (At(Mid).Time <= Time) { Low = Mid + 1; } else { High = Mid; }
	}
	return Low - 1;
}

const FTwinTimeline::FEntry* FTwinTimeline::FindAt(double Time) const
{
	const int32 Index = IndexAt(Time);
	return Index >= 0 ? &At(Index) : nullptr;
}

void FTwinTimeline::GetRange(FName MachineId, double End, double Seconds, TArray<FTwinSample>& Out) const
{
	Out.Reset();
	for (int32 i = IndexAt(End - Seconds) + 1, Last = IndexAt(End); i <= Last; ++i)
	{
		if (const FTwinSample* S = At(i).Frame.Samples.FindByPredicate([&](const FTwinSample& X) { return X.MachineId == MachineId; }))
		{
			Out.Add(*S);
		}
	}
}
