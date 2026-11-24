#pragma once

#include "GameFramework/PlayerController.h"
#include "TwinPlayerController.generated.h"

class ATwinMachineActor;
class UTwinHudWidget;

UENUM()
enum class ETwinViewMode : uint8
{
	Overview,
	Drone
};

/** 관제 시점과 드론 시점, 설비 선택을 다룬다. */
UCLASS()
class METATWIN_API ATwinPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	void SelectMachine(ATwinMachineActor* Machine);
	/** 화면 좌표의 설비를 선택한다. 설비가 없으면 선택을 해제한다. */
	ATwinMachineActor* SelectAtScreen(const FVector2D& ScreenPosition);
	void SetViewMode(ETwinViewMode Mode);
	void ToggleHeatmap();
	void FocusSelected();
	/** 진행 중인 카메라 이동을 멈추고 시점을 정한다. */
	void SetPose(const FVector& Location, const FRotator& Rotation);

	ATwinMachineActor* GetSelected() const { return Selected.Get(); }
	ETwinViewMode GetViewMode() const { return ViewMode; }
	UTwinHudWidget* GetHud() const { return Hud; }
	bool IsHeatmap() const { return bHeatmap; }
	/** 공정 순서로 정렬된 설비. */
	const TArray<TWeakObjectPtr<ATwinMachineActor>>& GetMachines() const { return Machines; }

private:
	void OnToggleView();
	void OnClick();
	void OnToggleReplay();
	void OnQuit();
	void UpdateLabels();
	void SelectNext();
	void SelectPrevious();
	void StepSelection(int32 Delta);
	void FlyTo(const FVector& Location, const FRotator& Rotation);

	/** -TwinTour 실행 인자로 켜는 자동 점검. 화면 저장과 선택·fps 검사 후 종료한다. */
	void RunTourStep();

	UPROPERTY(Transient)
	TObjectPtr<UTwinHudWidget> Hud;

	TArray<TWeakObjectPtr<ATwinMachineActor>> Machines;
	TWeakObjectPtr<ATwinMachineActor> Selected;
	ETwinViewMode ViewMode = ETwinViewMode::Overview;
	bool bHeatmap = false;

	bool bFlying = false;
	FVector FlyLocation = FVector::ZeroVector;
	FRotator FlyRotation = FRotator::ZeroRotator;

	float NextLabelUpdate = 0.f;
	FTimerHandle TourTimer;
	int32 TourStep = 0;
	int32 TourFailures = 0;
	bool bMeasure = false;
	TArray<float> FrameTimes;
	void ReportFps(const TCHAR* View);
};
