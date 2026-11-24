#pragma once

#include "TwinTypes.h"

/** 설비별 최신 값. 게임 스레드에서만 쓴다. */
class METATWIN_API FTwinRegistry
{
public:
	struct FRecord
	{
		FTwinSample Latest;
		double ReceivedAt = 0.0;
	};

	/** 새 프레임이면 반영하고 true. 중복·역순·이전 세션 프레임이면 무시하고 false. Now는 단조 시계(초). */
	bool Apply(const FTwinFrame& Frame, double Now);

	const FRecord* Find(FName MachineId) const { return Records.Find(MachineId); }
	void GetMachineIds(TArray<FName>& Out) const { Records.GetKeys(Out); }
	bool IsFresh(FName MachineId, double Now, double MaxAge) const;

	int64 AcceptedCount = 0;
	int64 RejectedCount = 0;
	/** 건너뛴 순번 수. 늦게 도착해 버린 프레임도 포함한다. */
	int64 GapCount = 0;

private:
	TMap<FName, FRecord> Records;
	FString Session;
	TArray<FString> RetiredSessions;
	int64 LastSeq = -1;
};
