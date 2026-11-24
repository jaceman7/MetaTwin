#pragma once

#include "CoreMinimal.h"
#include "TwinTypes.generated.h"

UENUM(BlueprintType)
enum class ETwinMachineState : uint8
{
	Unknown,
	Running,
	Warning,
	Fault,
	Maintenance
};

UENUM(BlueprintType)
enum class ETwinLinkState : uint8
{
	Disconnected,
	Connecting,
	Connected
};

USTRUCT(BlueprintType)
struct FTwinSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	FName MachineId;

	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	ETwinMachineState State = ETwinMachineState::Unknown;

	/** 섭씨 */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Temperature = 0.0;

	/** mm/s */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Vibration = 0.0;

	/** 개/분 */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Throughput = 0.0;

	/** 지금 낼 수 있는 처리량(개/분) */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Capacity = 0.0;

	/** 설정 속도 비율 */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Speed = 1.0;

	/** 냉각 효율 추정값. 1이 정상 */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Cooling = 1.0;

	/** 구동부 상태 추정값. 1이 정상 */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Wear = 1.0;

	/** 남은 정비 시간(초) */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Maint = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	bool bBypass = false;

	/** 출구 버퍼의 셀 수 */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	double Buffer = 0.0;

	/** 송신 측 시각(Unix ms). 신선도 판정에는 쓰지 않는다. */
	UPROPERTY(BlueprintReadOnly, Category = "Twin")
	int64 SourceTimeMs = 0;
};

/** 텔레메트리 메시지 하나. */
struct FTwinFrame
{
	FString Session;
	int64 Seq = 0;
	/** 라인 누적 생산량 */
	double Produced = 0.0;
	TArray<FTwinSample> Samples;
};
