#pragma once

#include "TwinRegistry.h"

struct FTwinLineSummary
{
	int32 Running = 0;
	int32 Warning = 0;
	int32 Fault = 0;
	/** 값이 없거나 오래된 설비 수. */
	int32 Unknown = 0;
	int32 Maintenance = 0;
	/** 마지막 설비의 실제 출력. bComplete일 때만 유효하다. */
	double Throughput = 0.0;
	FName Bottleneck;
	/** 모든 설비의 값이 신선한지. */
	bool bComplete = false;
};

struct FTwinMachineView
{
	FTwinSample Sample;
	bool bHasData = false;
	bool bFresh = false;
};

namespace TwinLine
{
	/** Views는 공정 순서다. */
	METATWIN_API FTwinLineSummary Summarize(const TArray<FTwinMachineView>& Views);

	/** 온도를 히트맵 색으로 바꾼다. 20도 이하 파랑, 110도 이상 빨강. */
	METATWIN_API FLinearColor HeatColor(double Temperature);
}

enum class ETwinAlarmLevel : uint8
{
	Recovered,
	Warning,
	Fault,
	DataLost,
	Maintenance
};

struct FTwinAlarm
{
	FName MachineId;
	ETwinAlarmLevel Level = ETwinAlarmLevel::Recovered;
	double Time = 0.0;
};

/** 설비 상태 변화를 알람으로 기록한다. 임계값 부근의 떨림은 HoldSeconds 동안 유지될 때만 인정한다. */
class METATWIN_API FTwinAlarmLog
{
public:
	static constexpr double HoldSeconds = 0.6;
	static constexpr int32 Capacity = 100;

	/** 새 알람이 기록되면 true. */
	bool Update(FName MachineId, ETwinMachineState State, bool bFresh, double Now);

	/** 오래된 것부터. */
	const TArray<FTwinAlarm>& GetAlarms() const { return Alarms; }

private:
	struct FTrack
	{
		ETwinAlarmLevel Confirmed = ETwinAlarmLevel::Recovered;
		ETwinAlarmLevel Candidate = ETwinAlarmLevel::Recovered;
		double CandidateSince = 0.0;
	};

	TMap<FName, FTrack> Tracks;
	TArray<FTwinAlarm> Alarms;
};
