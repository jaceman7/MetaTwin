#pragma once

#include "GameFramework/Actor.h"
#include "TwinTourActor.generated.h"

class ATwinPlayerController;
class UTelemetrySubsystem;

/** -TwinTourM3 실행 인자로 켜는 플레이 흐름 자동 점검. */
UCLASS(NotPlaceable)
class METATWIN_API ATwinTourActor : public AActor
{
	GENERATED_BODY()

public:
	ATwinTourActor();
	virtual void Tick(float DeltaSeconds) override;

private:
	void Check(bool bOk, const FString& What);
	void Shot(const TCHAR* Name) const;
	void Next(double Delay = 0.0);

	int32 Step = 0;
	int32 Failures = 0;
	double StepAt = 0.0;
	double WaitUntil = 0.0;
	double FaultAt = 0.0;
	double ApplyAt = 0.0;
	double ApplyProduced = 0.0;
	double Predicted = 0.0;
};
