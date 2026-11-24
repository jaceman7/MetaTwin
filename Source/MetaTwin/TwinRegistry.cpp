#include "TwinRegistry.h"

bool FTwinRegistry::Apply(const FTwinFrame& Frame, double Now)
{
	if (Frame.Session != Session)
	{
		if (RetiredSessions.Contains(Frame.Session))
		{
			++RejectedCount;
			return false;
		}
		if (!Session.IsEmpty())
		{
			RetiredSessions.Add(Session);
			if (RetiredSessions.Num() > 16)
			{
				RetiredSessions.RemoveAt(0);
			}
		}
		Session = Frame.Session;
		LastSeq = -1;
	}

	if (Frame.Seq <= LastSeq)
	{
		++RejectedCount;
		return false;
	}
	if (LastSeq >= 0)
	{
		GapCount += Frame.Seq - LastSeq - 1;
	}
	LastSeq = Frame.Seq;
	++AcceptedCount;

	for (const FTwinSample& Sample : Frame.Samples)
	{
		FRecord& Record = Records.FindOrAdd(Sample.MachineId);
		Record.Latest = Sample;
		Record.ReceivedAt = Now;
	}
	return true;
}

bool FTwinRegistry::IsFresh(FName MachineId, double Now, double MaxAge) const
{
	const FRecord* Record = Records.Find(MachineId);
	return Record && Now - Record->ReceivedAt <= MaxAge;
}
