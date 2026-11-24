#include "TelemetrySubsystem.h"

#include "IWebSocket.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Modules/ModuleManager.h"
#include "TwinTelemetry.h"
#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogTwinTelemetry, Log, All);

namespace
{
	constexpr double CommandTimeout = 3.0;
	constexpr int32 MaxCommands = 20;
	const FName LineAlarmId(TEXT("LINE"));
}

void UTelemetrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	if (!TwinPlant::LoadConfigs(Configs))
	{
		UE_LOG(LogTwinTelemetry, Error, TEXT("plant_layout.json could not be read"));
	}
	WallStart = FDateTime::Now();
	StartedAt = LastTickAt = UTelemetrySubsystem::Now();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTelemetrySubsystem::Tick), 0.1f);
}

void UTelemetrySubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);
	UE_LOG(LogTwinTelemetry, Log, TEXT("summary connects=%d accepted=%lld rejected=%lld gaps=%lld invalid=%lld local=%d"),
		ConnectCount, Registry.AcceptedCount, Registry.RejectedCount, Registry.GapCount, InvalidCount, bLocal);
	DropSocket();
	LinkState = ETwinLinkState::Disconnected;
	Super::Deinitialize();
}

// ---- 보기 ----

double UTelemetrySubsystem::Now()
{
	return FApp::GetCurrentTime();
}

FDateTime UTelemetrySubsystem::ToWallTime(double Time) const
{
	return WallStart + FTimespan::FromSeconds(Time - StartedAt);
}

double UTelemetrySubsystem::GetViewTime() const
{
	return bReplay ? PlayTime : UTelemetrySubsystem::Now();
}

const FTwinFrame* UTelemetrySubsystem::GetViewFrame(bool& bOutFresh) const
{
	const double Time = GetViewTime();
	const FTwinTimeline::FEntry* Entry = Timeline.FindAt(Time);
	bOutFresh = Entry && Time - Entry->Time <= StaleSeconds;
	return Entry ? &Entry->Frame : nullptr;
}

bool UTelemetrySubsystem::GetLatest(FName MachineId, FTwinSample& OutSample, bool& bOutFresh) const
{
	if (bReplay)
	{
		const FTwinFrame* Frame = GetViewFrame(bOutFresh);
		const FTwinSample* Sample = Frame ? Frame->Samples.FindByPredicate([&](const FTwinSample& S) { return S.MachineId == MachineId; }) : nullptr;
		bOutFresh = bOutFresh && Sample;
		if (Sample)
		{
			OutSample = *Sample;
		}
		return Sample != nullptr;
	}
	const FTwinRegistry::FRecord* Record = Registry.Find(MachineId);
	bOutFresh = Registry.IsFresh(MachineId, UTelemetrySubsystem::Now(), StaleSeconds);
	if (Record)
	{
		OutSample = Record->Latest;
	}
	return Record != nullptr;
}

void UTelemetrySubsystem::GetHistory(FName MachineId, TArray<FTwinSample>& Out) const
{
	Timeline.GetRange(MachineId, GetViewTime(), 60.0, Out);
}

FTwinLineSummary UTelemetrySubsystem::GetSummary(const TArray<FName>& MachineIds) const
{
	TArray<FTwinMachineView> Views;
	for (const FName Id : MachineIds)
	{
		FTwinMachineView& View = Views.AddDefaulted_GetRef();
		View.bHasData = GetLatest(Id, View.Sample, View.bFresh);
		View.Sample.MachineId = Id;
	}
	return TwinLine::Summarize(Views);
}

bool UTelemetrySubsystem::GetProduced(double& Out) const
{
	bool bFresh = false;
	const FTwinFrame* Frame = GetViewFrame(bFresh);
	if (Frame)
	{
		Out = Frame->Produced;
	}
	return Frame != nullptr;
}

void UTelemetrySubsystem::SetReplay(bool bInReplay)
{
	if (bInReplay == bReplay || (bInReplay && Timeline.IsEmpty()))
	{
		return;
	}
	bReplay = bInReplay;
	bPaused = false;
	if (bReplay)
	{
		SeekTo(Timeline.GetEnd() - 30.0);
	}
}

void UTelemetrySubsystem::SeekTo(double Time)
{
	PlayTime = FMath::Clamp(Time, Timeline.GetStart(), Timeline.GetEnd());
}

bool UTelemetrySubsystem::EvaluateWhatIf(FName MachineId, double Horizon, TArray<FTwinWhatIfResult>& Out) const
{
	Out.Reset();
	bool bFresh = false;
	const FTwinFrame* Frame = GetViewFrame(bFresh);
	if (!Frame || !bFresh)
	{
		return false;
	}
	FTwinPlant Plant;
	Plant.Init(Configs);
	if (!Plant.SyncFrom(*Frame))
	{
		return false;
	}
	Out = TwinWhatIf::Evaluate(Plant, MachineId, Horizon);
	return Out.Num() > 0;
}

// ---- 명령 ----

FTwinCommandEntry& UTelemetrySubsystem::LogCommand(const FTwinCommand& Command)
{
	if (Commands.Num() >= MaxCommands)
	{
		Commands.RemoveAt(0);
	}
	FTwinCommandEntry& Entry = Commands.AddDefaulted_GetRef();
	Entry.Id = FGuid::NewGuid().ToString();
	Entry.Command = Command;
	Entry.SentAt = UTelemetrySubsystem::Now();
	return Entry;
}

bool UTelemetrySubsystem::SendCommand(const FTwinCommand& Command, FString& OutError)
{
	if (bReplay)
	{
		OutError = TEXT("재생 중에는 조작할 수 없습니다");
		return false;
	}
	if (bLocal)
	{
		FTwinCommandEntry& Entry = LogCommand(Command);
		const bool bOk = LocalPlant.Apply(Command, OutError);
		Entry.State = bOk ? ETwinCommandState::Accepted : ETwinCommandState::Failed;
		Entry.Message = OutError;
		return bOk;
	}
	if (LinkState != ETwinLinkState::Connected || !Socket)
	{
		OutError = TEXT("설비에 연결되어 있지 않습니다");
		return false;
	}
	// 응답이 없어도 다시 보내지 않는다. 적용 여부를 모르는 명령을 중복 실행하지 않기 위해서다.
	const FTwinCommandEntry& Entry = LogCommand(Command);
	Socket->Send(TwinTelemetry::MakeCommand(Entry.Id, Command));
	UE_LOG(LogTwinTelemetry, Log, TEXT("command %s %s %.2f"), TwinPlant::ActionName(Command.Action), *Command.MachineId.ToString(), Command.Value);
	return true;
}

void UTelemetrySubsystem::TickCommands(double Now)
{
	for (FTwinCommandEntry& Entry : Commands)
	{
		if (Entry.State == ETwinCommandState::Pending && (Now - Entry.SentAt > CommandTimeout || LinkState != ETwinLinkState::Connected))
		{
			Entry.State = ETwinCommandState::Failed;
			Entry.Message = TEXT("응답 없음. 적용 여부를 확인하세요");
		}
	}
}

// ---- 갱신 ----

bool UTelemetrySubsystem::Tick(float DeltaTime)
{
	const double Now = UTelemetrySubsystem::Now();
	TickLink(Now);
	TickLocal(Now);
	TickCommands(Now);
	TickAlarms(Now);
	if (bReplay && !bPaused)
	{
		SeekTo(PlayTime + (Now - LastTickAt));
	}
	LastTickAt = Now;
	return true;
}

// 소켓의 생성과 파괴는 Tick에서만 한다. 소켓 콜백 안에서 자기 자신을 파괴하지 않기 위해서다.
void UTelemetrySubsystem::TickLink(double Now)
{
	if (LinkState == ETwinLinkState::Disconnected)
	{
		if (Now >= NextConnectAt)
		{
			DropSocket();
			OpenSocket();
		}
	}
	else if (Now - LastActivityAt > WatchdogSeconds)
	{
		ScheduleReconnect(LinkState == ETwinLinkState::Connecting ? TEXT("connect timeout") : TEXT("no data"));
	}
}

void UTelemetrySubsystem::TickLocal(double Now)
{
	if (!bLocal)
	{
		if (LocalFallbackSeconds <= 0.f || ConnectCount > 0 || Configs.IsEmpty() || Now - StartedAt < LocalFallbackSeconds)
		{
			return;
		}
		bLocal = true;
		LocalPlant.Init(Configs);
		LocalSession = TEXT("local-") + FGuid::NewGuid().ToString();
		LocalSeq = 0;
		LocalNextStep = Now;
		UE_LOG(LogTwinTelemetry, Log, TEXT("local simulation started"));
	}
	// 멈췄다 돌아와도 밀린 시간을 한꺼번에 계산하지 않는다.
	LocalNextStep = FMath::Max(LocalNextStep, Now - 1.0);
	for (; LocalNextStep <= Now; LocalNextStep += FTwinPlant::Dt)
	{
		LocalPlant.Step();
		FTwinFrame Frame;
		Frame.Session = LocalSession;
		Frame.Seq = ++LocalSeq;
		Frame.Produced = LocalPlant.Produced;
		LocalPlant.MakeSamples(Frame.Samples);
		AcceptFrame(Frame, LocalNextStep);
	}
}

void UTelemetrySubsystem::StopLocal()
{
	if (bLocal)
	{
		// 내장 시뮬레이션과 실제 데이터의 기록을 섞지 않는다.
		bLocal = false;
		SetReplay(false);
		Timeline = FTwinTimeline();
		Registry = FTwinRegistry();
		AlarmLog = FTwinAlarmLog();
		Commands.Reset();
		UE_LOG(LogTwinTelemetry, Log, TEXT("local simulation stopped"));
	}
}

void UTelemetrySubsystem::TickAlarms(double Now)
{
	TArray<FName> Ids;
	Registry.GetMachineIds(Ids);
	if (Ids.IsEmpty())
	{
		return;
	}
	const bool bAnyFresh = Ids.ContainsByPredicate([&](FName Id) { return Registry.IsFresh(Id, Now, StaleSeconds); });
	const auto Update = [&](FName Id, ETwinMachineState State, bool bFresh)
	{
		if (AlarmLog.Update(Id, State, bFresh, Now))
		{
			UE_LOG(LogTwinTelemetry, Log, TEXT("alarm %s level=%d"), *Id.ToString(), int32(AlarmLog.GetAlarms().Last().Level));
		}
	};
	// 전체가 끊기면 설비마다 알람을 내지 않고 하나로 묶는다.
	Update(LineAlarmId, ETwinMachineState::Running, bAnyFresh);
	if (bAnyFresh)
	{
		for (const FName Id : Ids)
		{
			Update(Id, Registry.Find(Id)->Latest.State, Registry.IsFresh(Id, Now, StaleSeconds));
		}
	}
}

bool UTelemetrySubsystem::AcceptFrame(const FTwinFrame& Frame, double Now)
{
	if (!Registry.Apply(Frame, Now))
	{
		return false;
	}
	Timeline.Add(Frame, Now);
	return true;
}

// ---- 연결 ----

void UTelemetrySubsystem::OpenSocket()
{
	LinkState = ETwinLinkState::Connecting;
	LastActivityAt = UTelemetrySubsystem::Now();
	Socket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl);
	Socket->SetTextMessageMemoryLimit(1024 * 1024);
	Socket->OnConnected().AddWeakLambda(this, [this]
	{
		if (LinkState != ETwinLinkState::Connecting)
		{
			return; // 시간 초과로 포기한 시도가 뒤늦게 성공한 경우
		}
		LinkState = ETwinLinkState::Connected;
		LastActivityAt = UTelemetrySubsystem::Now();
		++ConnectCount;
		StopLocal();
		UE_LOG(LogTwinTelemetry, Log, TEXT("connected (#%d) %s"), ConnectCount, *ServerUrl);
	});
	Socket->OnConnectionError().AddWeakLambda(this, [this](const FString& Error)
	{
		ScheduleReconnect(Error);
	});
	Socket->OnClosed().AddWeakLambda(this, [this](int32 Code, const FString& Reason, bool)
	{
		ScheduleReconnect(FString::Printf(TEXT("closed %d %s"), Code, *Reason));
	});
	Socket->OnMessage().AddUObject(this, &UTelemetrySubsystem::HandleMessage);
	Socket->Connect();
}

void UTelemetrySubsystem::DropSocket()
{
	if (Socket)
	{
		Socket->OnConnected().RemoveAll(this);
		Socket->OnConnectionError().RemoveAll(this);
		Socket->OnClosed().RemoveAll(this);
		Socket->OnMessage().RemoveAll(this);
		Socket->Close();
		Socket.Reset();
	}
}

void UTelemetrySubsystem::ScheduleReconnect(const FString& Reason)
{
	if (LinkState == ETwinLinkState::Disconnected)
	{
		return; // 오류와 종료 콜백이 함께 와도 한 번만 예약한다.
	}
	LinkState = ETwinLinkState::Disconnected;
	const double Delay = TwinTelemetry::ReconnectDelay(Attempt++, FMath::FRand());
	NextConnectAt = UTelemetrySubsystem::Now() + Delay;
	UE_LOG(LogTwinTelemetry, Warning, TEXT("link lost url=%s (%s), retry in %.2fs"), *ServerUrl, *Reason, Delay);
}

void UTelemetrySubsystem::HandleMessage(const FString& Message)
{
	if (LinkState != ETwinLinkState::Connected)
	{
		return;
	}
	FString Id, Error;
	bool bOk = false;
	if (TwinTelemetry::ParseAck(Message, Id, bOk, Error))
	{
		if (FTwinCommandEntry* Entry = Commands.FindByPredicate([&](const FTwinCommandEntry& E) { return E.Id == Id; }))
		{
			Entry->State = bOk ? ETwinCommandState::Accepted : ETwinCommandState::Failed;
			Entry->Message = Error;
		}
		return;
	}
	FTwinFrame Frame;
	if (!TwinTelemetry::ParseFrame(Message, Frame, Error))
	{
		if (InvalidCount++ % 100 == 0)
		{
			UE_LOG(LogTwinTelemetry, Warning, TEXT("frame rejected: %s (total %lld)"), *Error, InvalidCount);
		}
		return;
	}
	const double Now = UTelemetrySubsystem::Now();
	if (AcceptFrame(Frame, Now))
	{
		LastActivityAt = Now;
		Attempt = 0; // 유효한 데이터를 받은 뒤에만 대기 시간을 초기화한다.
	}
}
