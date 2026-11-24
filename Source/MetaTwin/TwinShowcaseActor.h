#pragma once

#include "GameFramework/Actor.h"
#include "TwinShowcaseActor.generated.h"

/**
 * -TwinShowcase 실행 인자로 켜는 소개 영상 연출. 내장 시뮬레이션을 쓴다.
 * -benchmark -fps=30과 함께 실행하면 프레임을 Saved/Showcase에 JPG로 저장한다.
 */
UCLASS(NotPlaceable)
class METATWIN_API ATwinShowcaseActor : public AActor
{
	GENERATED_BODY()

public:
	ATwinShowcaseActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void SaveFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels);

	FDelegateHandle CaptureHandle;
	double StartAt = -1.0;
	int32 Cue = 0;
	int32 Frame = 0;
	double FaultAt = -1.0;
};
