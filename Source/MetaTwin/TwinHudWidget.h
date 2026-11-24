#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "TwinPlant.h"
#include "TwinHudWidget.generated.h"

class ATwinMachineActor;
class ATwinPlayerController;
class UBorder;
class USlider;
class UTextBlock;
class UCanvasPanel;
class UVerticalBox;

/** 값 이력을 꺾은선으로 그린다. 범위는 표시 중인 값에 맞춘다. */
UCLASS()
class METATWIN_API UTwinSparkline : public UUserWidget
{
	GENERATED_BODY()

public:
	/** InMinRange보다 작은 변화는 확대하지 않는다. */
	void SetValues(TArray<float> InValues, const FLinearColor& InColor, float InMinRange);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TArray<float> Values;
	FLinearColor LineColor = FLinearColor::White;
	float MinRange = 1.f;
};

/** 설비 위에 뜨는 라벨. */
UCLASS()
class METATWIN_API UTwinLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetContent(const FText& Name, const FText& State, const FLinearColor& Color);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StateText;
};

/** 설비 목록의 한 줄. 누르면 그 설비를 선택한다. */
UCLASS()
class METATWIN_API UTwinMachineButton : public UButton
{
	GENERATED_BODY()

public:
	void Setup(ATwinMachineActor* InMachine);

	TWeakObjectPtr<ATwinMachineActor> Machine;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StateText;

private:
	UFUNCTION()
	void HandleClicked();
};

/** 누르면 Action을 실행하는 글자 버튼. */
UCLASS()
class METATWIN_API UTwinActionButton : public UButton
{
	GENERATED_BODY()

public:
	void Setup(UWidgetTree* Tree, const FText& Text, TFunction<void()> InAction);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Text;

private:
	UFUNCTION()
	void HandleClicked();

	TFunction<void()> Action;
};

/** 관제 HUD: 상단 요약, 좌측 공정 목록, 우측 상세, 하단 알람. */
UCLASS()
class METATWIN_API UTwinHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 선택한 설비의 조치안을 계산해 표시한다. */
	void RunWhatIf();
	/** Index번 조치안의 명령을 보낸다. */
	void ApplyWhatIf(int32 Index);
	const TArray<FTwinWhatIfResult>& GetWhatIf() const { return WhatIf; }
	void Command(ETwinAction Action, double Value);
	/** 화면 중앙 아래 자막. 비우면 숨긴다. */
	void SetCaption(const FText& Text);

	/** 어두운 패널 위에서 읽을 수 있도록 밝힌 상태 색. */
	static FLinearColor TextStatusColor(ETwinMachineState State, bool bFresh);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	// 패널 위 클릭이 뒤의 설비 선택으로 넘어가지 않게 한다.
	virtual FReply NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }

private:
	void BuildTree();
	void BuildControls(UVerticalBox* Detail);
	void BuildReplayBar(UCanvasPanel* Canvas);
	void RefreshControls();
	void RefreshReplay();
	UTwinActionButton* MakeAction(const FText& Text, TFunction<void()> Action);

	UFUNCTION()
	void HandleSeek(float Value);

	void Refresh();
	void RefreshDetail(ATwinPlayerController& Controller);

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MachineList;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTwinMachineButton>> MachineButtons;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LinkText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SummaryText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CountText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HintText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> DetailPanel;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailTitle;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> DetailState;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> MetricTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTwinSparkline>> Sparklines;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> AlarmTexts;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Crosshair;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ConditionText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTwinActionButton>> ControlButtons;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> WhatIfBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WhatIfTitle;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> WhatIfTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTwinActionButton>> WhatIfButtons;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CommandText;

	UPROPERTY(Transient)
	TObjectPtr<UTwinActionButton> ReplayButton;

	UPROPERTY(Transient)
	TObjectPtr<UTwinActionButton> PauseButton;

	UPROPERTY(Transient)
	TObjectPtr<USlider> SeekSlider;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ReplayText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CaptionText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> CaptionPanel;

	TArray<FTwinWhatIfResult> WhatIf;
	FName WhatIfMachine;
	FString LocalError;
	double LocalErrorUntil = 0.0;
	float NextRefresh = 0.f;
};
