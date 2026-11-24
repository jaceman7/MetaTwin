#include "TwinLine.h"

FTwinLineSummary TwinLine::Summarize(const TArray<FTwinMachineView>& Views)
{
	FTwinLineSummary Out;
	double Lowest = TNumericLimits<double>::Max();
	for (const FTwinMachineView& View : Views)
	{
		if (!View.bHasData || !View.bFresh || View.Sample.State == ETwinMachineState::Unknown)
		{
			++Out.Unknown;
			continue;
		}
		switch (View.Sample.State)
		{
		case ETwinMachineState::Running: ++Out.Running; break;
		case ETwinMachineState::Warning: ++Out.Warning; break;
		case ETwinMachineState::Maintenance: ++Out.Maintenance; break;
		default: ++Out.Fault; break;
		}
		if (View.Sample.Capacity < Lowest)
		{
			Lowest = View.Sample.Capacity;
			Out.Bottleneck = View.Sample.MachineId;
		}
	}
	Out.bComplete = Views.Num() > 0 && Out.Unknown == 0;
	if (Out.bComplete)
	{
		Out.Throughput = Views.Last().Sample.Throughput;
	}
	else
	{
		Out.Bottleneck = NAME_None; // 일부 값만으로 병목을 단정하지 않는다.
	}
	return Out;
}

FLinearColor TwinLine::HeatColor(double Temperature)
{
	const float T = static_cast<float>(FMath::Clamp((Temperature - 20.0) / 90.0, 0.0, 1.0));
	const FLinearColor Cold(0.02f, 0.1f, 1.f), Mid(0.02f, 0.9f, 0.3f), Hot(1.f, 0.03f, 0.02f);
	return T < 0.5f ? FLinearColor::LerpUsingHSV(Cold, Mid, T * 2.f) : FLinearColor::LerpUsingHSV(Mid, Hot, T * 2.f - 1.f);
}

bool FTwinAlarmLog::Update(FName MachineId, ETwinMachineState State, bool bFresh, double Now)
{
	FTrack* Track = Tracks.Find(MachineId);
	if (!Track)
	{
		if (!bFresh)
		{
			return false; // 한 번도 받지 못한 설비는 유실로 보지 않는다.
		}
		Track = &Tracks.Add(MachineId);
	}

	ETwinAlarmLevel Level = ETwinAlarmLevel::Recovered;
	if (!bFresh)
	{
		Level = ETwinAlarmLevel::DataLost;
	}
	else if (State == ETwinMachineState::Fault)
	{
		Level = ETwinAlarmLevel::Fault;
	}
	else if (State == ETwinMachineState::Maintenance)
	{
		Level = ETwinAlarmLevel::Maintenance;
	}
	else if (State == ETwinMachineState::Warning)
	{
		Level = ETwinAlarmLevel::Warning;
	}

	if (Level != Track->Candidate)
	{
		Track->Candidate = Level;
		Track->CandidateSince = Now;
	}
	if (Track->Candidate == Track->Confirmed || Now - Track->CandidateSince < HoldSeconds)
	{
		return false;
	}

	Track->Confirmed = Track->Candidate;
	Alarms.Add({MachineId, Track->Confirmed, Now});
	if (Alarms.Num() > Capacity)
	{
		Alarms.RemoveAt(0);
	}
	return true;
}
