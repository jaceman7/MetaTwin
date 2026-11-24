#pragma once

#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TwinLine.h"
#include "TwinPlant.h"
#include "TelemetrySubsystem.generated.h"

class IWebSocket;

enum class ETwinCommandState : uint8
{
	Pending,
	Accepted,
	Failed
};

struct FTwinCommandEntry
{
	FString Id;
	FTwinCommand Command;
	ETwinCommandState State = ETwinCommandState::Pending;
	FString Message;
	double SentAt = 0.0;
};

/**
 * 텔레메트리 연결, 명령 송신, 재생 시점을 관리한다.
 * 한 번도 서버에 연결하지 못하면 내장 시뮬레이션으로 전환한다. 연결됐다가 끊긴 경우에는 전환하지 않는다.
 */
UCLASS(Config = Game)
class METATWIN_API UTelemetrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Twin")
	ETwinLinkState GetLinkState() const { return LinkState; }

	UFUNCTION(BlueprintPure, Category = "Twin")
	bool IsLocalSimulation() const { return bLocal; }

	/** 보고 있는 시점(실시간 또는 재생)의 값. 값이 있으면 true. bOutFresh는 StaleSeconds 안의 값인지다. */
	UFUNCTION(BlueprintCallable, Category = "Twin")
	bool GetLatest(FName MachineId, FTwinSample& OutSample, bool& bOutFresh) const;

	/** 보고 있는 시점까지 최근 60초. */
	void GetHistory(FName MachineId, TArray<FTwinSample>& Out) const;
	FTwinLineSummary GetSummary(const TArray<FName>& MachineIds) const;
	/** 보고 있는 시점의 누적 생산량. 값이 없으면 false. */
	bool GetProduced(double& Out) const;

	/** 보내거나 적용했으면 true. 서버의 처리 결과는 GetCommands에서 확인한다. */
	bool SendCommand(const FTwinCommand& Command, FString& OutError);
	const TArray<FTwinCommandEntry>& GetCommands() const { return Commands; }
	/** 보고 있는 시점의 상태에서 조치안을 예측한다. 값이 오래됐거나 빠졌으면 false. */
	bool EvaluateWhatIf(FName MachineId, double Horizon, TArray<FTwinWhatIfResult>& Out) const;

	void SetReplay(bool bInReplay);
	bool IsReplay() const { return bReplay; }
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	bool IsPaused() const { return bPaused; }
	void SeekTo(double Time);
	double GetViewTime() const;
	/** 앱 시계(초). 평소에는 실제 시간과 같고 고정 프레임 캡처에서는 게임 시간을 따른다. */
	static double Now();
	FDateTime ToWallTime(double Time) const;
	const FTwinTimeline& GetTimeline() const { return Timeline; }

	const FTwinRegistry& GetRegistry() const { return Registry; }
	const FTwinAlarmLog& GetAlarmLog() const { return AlarmLog; }
	int64 GetInvalidCount() const { return InvalidCount; }
	int32 GetConnectCount() const { return ConnectCount; }

	UPROPERTY(Config)
	FString ServerUrl = TEXT("ws://127.0.0.1:8765/");

	/** 이 시간 동안 새 값이 없으면 오래된 데이터로 본다. */
	UPROPERTY(Config)
	float StaleSeconds = 1.5f;

	/** 연결 중이지만 이 시간 동안 새 프레임이 없으면 다시 연결한다. */
	UPROPERTY(Config)
	float WatchdogSeconds = 4.0f;

	/** 시작 후 이 시간 안에 연결하지 못하면 내장 시뮬레이션을 쓴다. 0 이하이면 쓰지 않는다. */
	UPROPERTY(Config)
	float LocalFallbackSeconds = 6.0f;

private:
	bool Tick(float DeltaTime);
	void TickLink(double Now);
	void TickLocal(double Now);
	void TickCommands(double Now);
	void TickAlarms(double Now);
	void OpenSocket();
	void DropSocket();
	void ScheduleReconnect(const FString& Reason);
	void HandleMessage(const FString& Message);
	bool AcceptFrame(const FTwinFrame& Frame, double Now);
	void StopLocal();
	const FTwinFrame* GetViewFrame(bool& bOutFresh) const;
	FTwinCommandEntry& LogCommand(const FTwinCommand& Command);

	TSharedPtr<IWebSocket> Socket;
	FTSTicker::FDelegateHandle TickHandle;
	FTwinRegistry Registry;
	FTwinTimeline Timeline;
	FTwinAlarmLog AlarmLog;
	TArray<FTwinMachineConfig> Configs;
	TArray<FTwinCommandEntry> Commands;

	ETwinLinkState LinkState = ETwinLinkState::Disconnected;
	double StartedAt = 0.0;
	FDateTime WallStart;
	double NextConnectAt = 0.0;
	double LastActivityAt = 0.0;
	int32 Attempt = 0;
	int32 ConnectCount = 0;
	int64 InvalidCount = 0;

	bool bLocal = false;
	FTwinPlant LocalPlant;
	FString LocalSession;
	int64 LocalSeq = 0;
	double LocalNextStep = 0.0;

	bool bReplay = false;
	bool bPaused = false;
	double PlayTime = 0.0;
	double LastTickAt = 0.0;
};
