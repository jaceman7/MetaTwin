#pragma once

#include "TwinTypes.h"

enum class ETwinAction : uint8
{
	InjectCooling,
	InjectWear,
	SetSpeed,
	SetBypass,
	Maintain
};

struct FTwinCommand
{
	ETwinAction Action = ETwinAction::Maintain;
	FName MachineId;
	double Value = 0.0;
};

struct FTwinMachineConfig
{
	FName Id;
	double Base = 0.0;
	double Vibration = 0.0;
	double Rate = 0.0;
	double Heat = 0.0;
};

struct FTwinMachineModel
{
	FTwinMachineConfig Config;
	double Temp = 0.0;
	double Speed = 1.0;
	double Cooling = 1.0, CoolingTarget = 1.0;
	double Wear = 1.0, WearTarget = 1.0;
	double Maint = 0.0;
	bool bBypass = false;
	bool bTripped = false;
	double Throughput = 0.0;
	double Capacity = 0.0;
	int32 Trips = 0;

	ETwinMachineState GetState() const;
	double GetVibration() const;
};

/** 직렬 공정 모델. Tools/emulator/plant.py와 같은 수식이며 기준 데이터로 일치를 검사한다. */
class METATWIN_API FTwinPlant
{
public:
	static constexpr double Dt = 0.1;
	static constexpr double BufferCap = 12.0;

	void Init(const TArray<FTwinMachineConfig>& Configs);
	void Step();
	/** 실패하면 OutError에 이유를 담고 상태를 바꾸지 않는다. */
	bool Apply(const FTwinCommand& Command, FString& OutError);

	/** 수신한 값으로 상태를 맞춘다. 진행 중인 열화는 해당 고장의 최종값까지 나빠진다고 본다. */
	bool SyncFrom(const FTwinFrame& Frame);
	void MakeSamples(TArray<FTwinSample>& Out) const;

	const FTwinMachineModel* Find(FName Id) const;

	TArray<FTwinMachineModel> Machines;
	TArray<double> Buffers;
	double Produced = 0.0;
};

namespace TwinPlant
{
	/** Content/Data/plant_layout.json을 읽는다. */
	METATWIN_API bool LoadConfigs(TArray<FTwinMachineConfig>& Out);
	METATWIN_API const TCHAR* ActionName(ETwinAction Action);
	METATWIN_API bool ParseAction(const FString& Name, ETwinAction& Out);
}

struct FTwinWhatIfResult
{
	FText Name;
	/** 적용할 명령. 비어 있으면 현 상태 유지. */
	TArray<FTwinCommand> Commands;
	/** 예측 기간의 라인 생산량(개). */
	double Output = 0.0;
	/** 예측 기간의 전체 고장 정지 횟수. */
	int32 Trips = 0;
	/** 대상 설비의 최고 온도. */
	double MaxTemp = 0.0;
	bool bRecommended = false;
};

namespace TwinWhatIf
{
	/** 대상 설비에 조치안을 각각 적용해 Horizon초를 예측한다. */
	METATWIN_API TArray<FTwinWhatIfResult> Evaluate(const FTwinPlant& Snapshot, FName MachineId, double Horizon);
}

/** 최근 프레임 기록. 시각은 단조 증가한다. */
class METATWIN_API FTwinTimeline
{
public:
	static constexpr int32 Capacity = 6000; // 10Hz에서 10분

	struct FEntry
	{
		double Time = 0.0;
		FTwinFrame Frame;
	};

	void Add(const FTwinFrame& Frame, double Time);
	/** Time 이하에서 가장 최근 기록. */
	const FEntry* FindAt(double Time) const;
	/** (End-Seconds, End] 구간의 한 설비 값을 오래된 것부터 담는다. */
	void GetRange(FName MachineId, double End, double Seconds, TArray<FTwinSample>& Out) const;

	bool IsEmpty() const { return Entries.IsEmpty(); }
	double GetStart() const { return IsEmpty() ? 0.0 : At(0).Time; }
	double GetEnd() const { return IsEmpty() ? 0.0 : At(Entries.Num() - 1).Time; }
	int32 Num() const { return Entries.Num(); }

private:
	const FEntry& At(int32 Index) const { return Entries[(Head + Index) % Entries.Num()]; }
	int32 IndexAt(double Time) const;

	TArray<FEntry> Entries; // 원형 버퍼
	int32 Head = 0;
};
