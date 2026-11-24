#pragma once

#include "TwinPlant.h"

namespace TwinTelemetry
{
	/** JSON을 검증해 프레임으로 바꾼다. 한 항목이라도 잘못되면 프레임 전체를 거부한다. */
	METATWIN_API bool ParseFrame(const FString& Json, FTwinFrame& OutFrame, FString& OutError);

	/** 명령 응답이면 true. */
	METATWIN_API bool ParseAck(const FString& Json, FString& OutId, bool& bOutOk, FString& OutError);
	METATWIN_API FString MakeCommand(const FString& Id, const FTwinCommand& Command);

	/** 재연결 대기 시간(초). Jitter01은 [0,1] 난수다. */
	METATWIN_API double ReconnectDelay(int32 Attempt, double Jitter01);
}
