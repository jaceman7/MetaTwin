#include "TwinHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TelemetrySubsystem.h"
#include "TwinMachineActor.h"
#include "TwinPlayerController.h"

namespace
{
	const FLinearColor PanelColor(0.004f, 0.010f, 0.018f, 0.86f);
	const FLinearColor Cyan(0.05f, 0.75f, 1.f);
	const FLinearColor Dim(0.45f, 0.58f, 0.66f);
	const FLinearColor Bright(0.88f, 0.95f, 1.f);
	const FLinearColor Orange(1.f, 0.52f, 0.08f);
	const FLinearColor Red(1.f, 0.18f, 0.18f);
	constexpr int32 AlarmRows = 5;
	constexpr int32 WhatIfRows = 4;
	constexpr double WhatIfHorizon = 180.0;

	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold = false)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		Block->SetText(Text);
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Block;
	}

	FButtonStyle MakeButtonStyle()
	{
		FButtonStyle Style;
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings = FSlateBrushOutlineSettings(4.f);
		Brush.TintColor = FSlateColor(FLinearColor(0.02f, 0.06f, 0.09f, 0.9f));
		Style.SetNormal(Brush);
		Brush.TintColor = FSlateColor(FLinearColor(0.02f, 0.16f, 0.24f, 0.95f));
		Style.SetHovered(Brush);
		Brush.TintColor = FSlateColor(FLinearColor(0.02f, 0.28f, 0.42f, 1.f));
		Style.SetPressed(Brush);
		return Style;
	}

	UBorder* MakePanel(UWidgetTree* Tree, UWidget* Content, const FMargin& Padding = FMargin(18, 14))
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>();
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(PanelColor);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(6.f, FSlateColor(FLinearColor(0.02f, 0.35f, 0.5f, 0.6f)), 1.f);
		Border->SetBrush(Brush);
		Border->SetPadding(Padding);
		Border->SetContent(Content);
		return Border;
	}

	UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* Widget, const FAnchors& Anchors, const FVector2D& Alignment, const FMargin& Offsets, bool bAutoSize)
	{
		UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetAnchors(Anchors);
		Slot->SetAlignment(Alignment);
		Slot->SetOffsets(Offsets);
		Slot->SetAutoSize(bAutoSize);
		return Slot;
	}
}

FLinearColor UTwinHudWidget::TextStatusColor(ETwinMachineState State, bool bFresh)
{
	if (!bFresh) { return Dim; }
	switch (State)
	{
	case ETwinMachineState::Running: return Cyan;
	case ETwinMachineState::Warning: return FLinearColor(1.f, 0.52f, 0.08f);
	case ETwinMachineState::Fault: return FLinearColor(1.f, 0.18f, 0.18f);
	case ETwinMachineState::Maintenance: return FLinearColor(0.62f, 0.42f, 1.f);
	default: return Dim;
	}
}

TSharedRef<SWidget> UTwinLabelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		NameText = MakeText(WidgetTree, FText::GetEmpty(), 12, Bright, true);
		StateText = MakeText(WidgetTree, FText::GetEmpty(), 11, Dim);
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		Box->AddChildToVerticalBox(NameText)->SetHorizontalAlignment(HAlign_Center);
		Box->AddChildToVerticalBox(StateText)->SetHorizontalAlignment(HAlign_Center);
		WidgetTree->RootWidget = MakePanel(WidgetTree, Box, FMargin(10, 4));
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	return Super::RebuildWidget();
}

void UTwinLabelWidget::SetContent(const FText& Name, const FText& State, const FLinearColor& Color)
{
	if (NameText && StateText)
	{
		NameText->SetText(Name);
		StateText->SetText(State);
		StateText->SetColorAndOpacity(FSlateColor(Color));
	}
}

void UTwinSparkline::SetValues(TArray<float> InValues, const FLinearColor& InColor, float InMinRange)
{
	MinRange = InMinRange;
	Values = MoveTemp(InValues);
	LineColor = InColor;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 UTwinSparkline::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FPaintGeometry Geometry = AllottedGeometry.ToPaintGeometry();
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry, TArray<FVector2f>{FVector2f(0, Size.Y), FVector2f(Size.X, Size.Y)},
		ESlateDrawEffect::None, FLinearColor(0.1f, 0.3f, 0.4f, 0.6f), true, 1.f);
	if (Values.Num() < 2)
	{
		return Layer;
	}

	const float Min = FMath::Min(Values), Max = FMath::Max(Values);
	const float Range = FMath::Max(Max - Min, MinRange);
	const float Mid = (Min + Max) * 0.5f;
	TArray<FVector2f> Points;
	Points.Reserve(Values.Num());
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		const float Alpha = (Values[i] - Mid) / Range + 0.5f;
		Points.Add(FVector2f(Size.X * i / (Values.Num() - 1), 3.f + (Size.Y - 6.f) * (1.f - Alpha)));
	}
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Geometry, Points, ESlateDrawEffect::None, LineColor, true, 2.f);
	return Layer + 1;
}

void UTwinMachineButton::Setup(ATwinMachineActor* InMachine)
{
	Machine = InMachine;
	InitIsFocusable(false); // 슬레이트 위젯이 만들어지기 전에만 정할 수 있다.
	SetStyle(MakeButtonStyle());
	OnClicked.AddDynamic(this, &UTwinMachineButton::HandleClicked);
}

void UTwinMachineButton::HandleClicked()
{
	if (ATwinPlayerController* Controller = Cast<ATwinPlayerController>(GetOwningPlayer()))
	{
		Controller->SelectMachine(Machine.Get());
	}
}

void UTwinActionButton::Setup(UWidgetTree* Tree, const FText& InText, TFunction<void()> InAction)
{
	Action = MoveTemp(InAction);
	InitIsFocusable(false);
	FButtonStyle Style = MakeButtonStyle();
	FSlateBrush Disabled = Style.Normal;
	Disabled.TintColor = FSlateColor(FLinearColor(0.02f, 0.03f, 0.04f, 0.6f));
	Style.SetDisabled(Disabled);
	SetStyle(Style);
	Text = MakeText(Tree, InText, 12, Bright);
	if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(AddChild(Text)))
	{
		ButtonSlot->SetPadding(FMargin(10, 5));
	}
	OnClicked.AddDynamic(this, &UTwinActionButton::HandleClicked);
}

void UTwinActionButton::HandleClicked()
{
	if (Action)
	{
		Action();
	}
}

TSharedRef<SWidget> UTwinHudWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UTwinHudWidget::BuildTree()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Canvas;
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// 상단 요약
	UHorizontalBox* Top = WidgetTree->ConstructWidget<UHorizontalBox>();
	const auto AddTop = [&](UWidget* Widget, bool bFill, EHorizontalAlignment Align)
	{
		UHorizontalBoxSlot* Slot = Top->AddChildToHorizontalBox(Widget);
		Slot->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
		Slot->SetHorizontalAlignment(Align);
		Slot->SetVerticalAlignment(VAlign_Center);
	};
	LinkText = MakeText(WidgetTree, FText::GetEmpty(), 14, Dim, true);
	SummaryText = MakeText(WidgetTree, FText::GetEmpty(), 18, Bright, true);
	CountText = MakeText(WidgetTree, FText::GetEmpty(), 14, Dim);
	AddTop(MakeText(WidgetTree, INVTEXT("METATWIN  |  배터리 셀 라인"), 18, Cyan, true), true, HAlign_Left);
	AddTop(SummaryText, true, HAlign_Center);
	UVerticalBox* TopRight = WidgetTree->ConstructWidget<UVerticalBox>();
	TopRight->AddChildToVerticalBox(LinkText)->SetHorizontalAlignment(HAlign_Right);
	TopRight->AddChildToVerticalBox(CountText)->SetHorizontalAlignment(HAlign_Right);
	AddTop(TopRight, true, HAlign_Right);
	Place(Canvas, MakePanel(WidgetTree, Top), FAnchors(0, 0, 1, 0), FVector2D(0, 0), FMargin(24, 20, 24, 72), false);

	// 좌측 공정 목록
	MachineList = WidgetTree->ConstructWidget<UVerticalBox>();
	MachineList->AddChildToVerticalBox(MakeText(WidgetTree, INVTEXT("공정 순서"), 13, Dim, true))->SetPadding(FMargin(0, 0, 0, 8));
	Place(Canvas, MakePanel(WidgetTree, MachineList), FAnchors(0, 0), FVector2D(0, 0), FMargin(24, 112, 300, 0), true);

	// 우측 상세
	UVerticalBox* Detail = WidgetTree->ConstructWidget<UVerticalBox>();
	DetailTitle = MakeText(WidgetTree, FText::GetEmpty(), 20, Bright, true);
	DetailState = MakeText(WidgetTree, FText::GetEmpty(), 14, Cyan, true);
	Detail->AddChildToVerticalBox(DetailTitle);
	Detail->AddChildToVerticalBox(DetailState)->SetPadding(FMargin(0, 2, 0, 10));
	for (int32 i = 0; i < 3; ++i)
	{
		UTextBlock* Metric = MakeText(WidgetTree, FText::GetEmpty(), 15, Bright);
		MetricTexts.Add(Metric);
		Detail->AddChildToVerticalBox(Metric)->SetPadding(FMargin(0, 8, 0, 4));
		UTwinSparkline* Sparkline = CreateWidget<UTwinSparkline>(this, UTwinSparkline::StaticClass());
		Sparkline->SetVisibility(ESlateVisibility::HitTestInvisible);
		Sparklines.Add(Sparkline);
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
		Box->SetWidthOverride(360.f);
		Box->SetHeightOverride(46.f);
		Box->AddChild(Sparkline);
		Detail->AddChildToVerticalBox(Box);
	}
	Detail->AddChildToVerticalBox(MakeText(WidgetTree, INVTEXT("그래프: 최근 최대 60초"), 11, Dim))->SetPadding(FMargin(0, 10, 0, 0));
	BuildControls(Detail);
	DetailPanel = MakePanel(WidgetTree, Detail);
	DetailPanel->SetVisibility(ESlateVisibility::Collapsed);
	Place(Canvas, DetailPanel, FAnchors(1, 0), FVector2D(1, 0), FMargin(-24, 112, 0, 0), true);

	BuildReplayBar(Canvas);

	CaptionText = MakeText(WidgetTree, FText::GetEmpty(), 22, Bright, true);
	CaptionText->SetJustification(ETextJustify::Center);
	CaptionPanel = MakePanel(WidgetTree, CaptionText, FMargin(24, 10));
	CaptionPanel->SetVisibility(ESlateVisibility::Collapsed);
	Place(Canvas, CaptionPanel, FAnchors(0.5f, 1), FVector2D(0.5f, 1), FMargin(0, -150, 0, 0), true);

	// 하단 알람
	UVerticalBox* Alarms = WidgetTree->ConstructWidget<UVerticalBox>();
	Alarms->AddChildToVerticalBox(MakeText(WidgetTree, INVTEXT("알람"), 13, Dim, true))->SetPadding(FMargin(0, 0, 0, 4));
	for (int32 i = 0; i < AlarmRows; ++i)
	{
		UTextBlock* Row = MakeText(WidgetTree, FText::GetEmpty(), 13, Dim);
		AlarmTexts.Add(Row);
		Alarms->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 2));
	}
	USizeBox* AlarmBox = WidgetTree->ConstructWidget<USizeBox>();
	AlarmBox->SetWidthOverride(440.f);
	AlarmBox->AddChild(Alarms);
	Place(Canvas, MakePanel(WidgetTree, AlarmBox), FAnchors(0, 1), FVector2D(0, 1), FMargin(24, -24, 0, 0), true);

	HintText = MakeText(WidgetTree, FText::GetEmpty(), 13, Bright);
	Place(Canvas, MakePanel(WidgetTree, HintText, FMargin(14, 8)), FAnchors(1, 1), FVector2D(1, 1), FMargin(-24, -24, 0, 0), true);

	Crosshair = WidgetTree->ConstructWidget<UBorder>();
	Crosshair->SetBrushColor(Cyan);
	Crosshair->SetVisibility(ESlateVisibility::Collapsed);
	Place(Canvas, Crosshair, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FMargin(0, 0, 6, 6), false);
}

void UTwinHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	NextRefresh -= InDeltaTime;
	if (NextRefresh <= 0.f)
	{
		NextRefresh = 0.1f;
		Refresh();
	}
}

void UTwinHudWidget::Refresh()
{
	ATwinPlayerController* Controller = Cast<ATwinPlayerController>(GetOwningPlayer());
	const UTelemetrySubsystem* Telemetry = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTelemetrySubsystem>() : nullptr;
	if (!Controller || !Telemetry)
	{
		return;
	}

	if (MachineButtons.IsEmpty())
	{
		for (const TWeakObjectPtr<ATwinMachineActor>& Machine : Controller->GetMachines())
		{
			UTwinMachineButton* Button = WidgetTree->ConstructWidget<UTwinMachineButton>();
			Button->Setup(Machine.Get());
			Button->NameText = MakeText(WidgetTree, FText::GetEmpty(), 14, Bright);
			Button->StateText = MakeText(WidgetTree, FText::GetEmpty(), 12, Dim, true);
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(Button->NameText);
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			NameSlot->SetVerticalAlignment(VAlign_Center);
			Row->AddChildToHorizontalBox(Button->StateText)->SetVerticalAlignment(VAlign_Center);
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
			Box->SetWidthOverride(260.f);
			Box->SetHeightOverride(30.f);
			Box->AddChild(Row);
			UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->AddChild(Box));
			ButtonSlot->SetPadding(FMargin(10, 3));
			MachineList->AddChildToVerticalBox(Button)->SetPadding(FMargin(0, 2));
			MachineButtons.Add(Button);
		}
	}

	TArray<FName> Ids;
	for (UTwinMachineButton* Button : MachineButtons)
	{
		const ATwinMachineActor* Machine = Button->Machine.Get();
		if (!Machine)
		{
			continue;
		}
		Ids.Add(Machine->MachineId);
		FTwinSample Sample;
		bool bFresh = false;
		const bool bHasData = Telemetry->GetLatest(Machine->MachineId, Sample, bFresh);
		Button->NameText->SetText(FText::FromString(FString::Printf(TEXT("%d  %s"), Machine->LineOrder + 1, *Machine->DisplayName)));
		Button->NameText->SetColorAndOpacity(FSlateColor(Machine->IsSelected() ? Cyan : Bright));
		Button->StateText->SetText(ATwinMachineActor::StatusText(Sample.State, bHasData, bFresh));
		Button->StateText->SetColorAndOpacity(FSlateColor(TextStatusColor(Sample.State, bFresh)));
	}

	const FTwinLineSummary Summary = Telemetry->GetSummary(Ids);
	if (Summary.bComplete)
	{
		double Produced = 0.0;
		Telemetry->GetProduced(Produced);
		SummaryText->SetText(FText::FromString(FString::Printf(TEXT("라인 처리량 %.1f 개/분   병목 %s   누적 %.0f개"), Summary.Throughput, *Summary.Bottleneck.ToString(), Produced)));
	}
	else
	{
		SummaryText->SetText(FText::FromString(FString::Printf(TEXT("라인 처리량 --   데이터 없는 설비 %d대"), Summary.Unknown)));
	}
	CountText->SetText(FText::FromString(FString::Printf(TEXT("정상 %d   경고 %d   고장 %d   정비 %d   데이터 없음 %d"), Summary.Running, Summary.Warning, Summary.Fault, Summary.Maintenance, Summary.Unknown)));

	if (Telemetry->IsLocalSimulation())
	{
		LinkText->SetText(INVTEXT("내장 시뮬레이션 (서버 없음)"));
		LinkText->SetColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.42f, 1.f)));
	}
	else
	{
		switch (Telemetry->GetLinkState())
		{
		case ETwinLinkState::Connected:
			LinkText->SetText(INVTEXT("연결됨"));
			LinkText->SetColorAndOpacity(FSlateColor(Cyan));
			break;
		case ETwinLinkState::Connecting:
			LinkText->SetText(INVTEXT("연결 중"));
			LinkText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.52f, 0.08f)));
			break;
		default:
			LinkText->SetText(INVTEXT("연결 끊김"));
			LinkText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.18f, 0.18f)));
		}
	}

	// 재생 중에는 그 시점까지의 알람만 보여준다.
	const TArray<FTwinAlarm> Alarms = Telemetry->GetAlarmLog().GetAlarms().FilterByPredicate(
		[Time = Telemetry->GetViewTime()](const FTwinAlarm& Alarm) { return Alarm.Time <= Time; });
	for (int32 i = 0; i < AlarmRows; ++i)
	{
		const int32 Index = Alarms.Num() - 1 - i;
		if (Index < 0)
		{
			AlarmTexts[i]->SetText(i == 0 ? INVTEXT("기록된 알람이 없습니다") : FText::GetEmpty());
			AlarmTexts[i]->SetColorAndOpacity(FSlateColor(Dim));
			continue;
		}
		const FTwinAlarm& Alarm = Alarms[Index];
		const TCHAR* What = TEXT("정상 복귀");
		FLinearColor Color = Cyan;
		switch (Alarm.Level)
		{
		case ETwinAlarmLevel::Warning: What = TEXT("경고 발생"); Color = FLinearColor(1.f, 0.52f, 0.08f); break;
		case ETwinAlarmLevel::Fault: What = TEXT("고장 발생"); Color = FLinearColor(1.f, 0.18f, 0.18f); break;
		case ETwinAlarmLevel::Maintenance: What = TEXT("정비 시작"); Color = FLinearColor(0.62f, 0.42f, 1.f); break;
		case ETwinAlarmLevel::DataLost: What = TEXT("데이터 수신 중단"); Color = Dim; break;
		default: break;
		}
		AlarmTexts[i]->SetText(FText::FromString(FString::Printf(TEXT("%s   %s   %s"), *Telemetry->ToWallTime(Alarm.Time).ToString(TEXT("%H:%M:%S")), *Alarm.MachineId.ToString(), What)));
		AlarmTexts[i]->SetColorAndOpacity(FSlateColor(Color));
	}

	const bool bDrone = Controller->GetViewMode() == ETwinViewMode::Drone;
	Crosshair->SetVisibility(bDrone ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	HintText->SetText(FText::FromString(FString::Printf(TEXT("%s   V 시점 전환   H 히트맵 %s   F 설비로 이동   [ ] 설비 순환   R 재생"),
		bDrone ? TEXT("WASD/QE 이동   마우스 시선   클릭 선택") : TEXT("클릭 선택"), Controller->IsHeatmap() ? TEXT("끄기") : TEXT("켜기"))));

	RefreshDetail(*Controller);
	RefreshReplay();
}

void UTwinHudWidget::RefreshDetail(ATwinPlayerController& Controller)
{
	const ATwinMachineActor* Machine = Controller.GetSelected();
	if (!Machine)
	{
		DetailPanel->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	DetailPanel->SetVisibility(ESlateVisibility::Visible);

	const UTelemetrySubsystem* Telemetry = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
	FTwinSample Sample;
	bool bFresh = false;
	const bool bHasData = Telemetry->GetLatest(Machine->MachineId, Sample, bFresh);
	DetailTitle->SetText(FText::FromString(Machine->DisplayName));
	DetailState->SetText(FText::FromString(FString::Printf(TEXT("%s   %s"), *Machine->MachineId.ToString(), *ATwinMachineActor::StatusText(Sample.State, bHasData, bFresh).ToString())));
	DetailState->SetColorAndOpacity(FSlateColor(TextStatusColor(Sample.State, bFresh)));

	ConditionText->SetText(FText::FromString(bHasData ? FString::Printf(TEXT("속도 %.0f%%   냉각 %.0f%%   구동부 %.0f%%   출구 버퍼 %.0f/%.0f%s"), Sample.Speed * 100,
		Sample.Cooling * 100, Sample.Wear * 100, Sample.Buffer, FTwinPlant::BufferCap, Sample.bBypass ? TEXT("   우회 중") : TEXT("")) : FString(TEXT("--"))));
	ConditionText->SetColorAndOpacity(FSlateColor(bFresh && (Sample.Cooling < 0.99 || Sample.Wear < 0.99) ? Orange : Dim));
	RefreshControls();

	TArray<FTwinSample> History;
	Telemetry->GetHistory(Machine->MachineId, History);
	const TCHAR* Names[] = {TEXT("온도"), TEXT("진동"), TEXT("처리량")};
	const TCHAR* Units[] = {TEXT("°C"), TEXT("mm/s"), TEXT("개/분")};
	const float MinRanges[] = {10.f, 1.f, 10.f};
	const double Values[] = {Sample.Temperature, Sample.Vibration, Sample.Throughput};
	for (int32 i = 0; i < 3; ++i)
	{
		MetricTexts[i]->SetText(FText::FromString(bHasData ? FString::Printf(TEXT("%s   %.1f %s"), Names[i], Values[i], Units[i]) : FString::Printf(TEXT("%s   --"), Names[i])));
		MetricTexts[i]->SetColorAndOpacity(FSlateColor(bFresh ? Bright : Dim));
		TArray<float> Series;
		Series.Reserve(History.Num());
		for (const FTwinSample& S : History)
		{
			Series.Add(static_cast<float>(i == 0 ? S.Temperature : i == 1 ? S.Vibration : S.Throughput));
		}
		Sparklines[i]->SetValues(MoveTemp(Series), bFresh ? Cyan : Dim, MinRanges[i]);
	}
}

UTwinActionButton* UTwinHudWidget::MakeAction(const FText& Text, TFunction<void()> Action)
{
	UTwinActionButton* Button = WidgetTree->ConstructWidget<UTwinActionButton>();
	Button->Setup(WidgetTree, Text, MoveTemp(Action));
	return Button;
}

void UTwinHudWidget::BuildControls(UVerticalBox* Detail)
{
	ConditionText = MakeText(WidgetTree, FText::GetEmpty(), 12, Dim);
	Detail->AddChildToVerticalBox(ConditionText)->SetPadding(FMargin(0, 8, 0, 8));

	const auto AddRow = [&](const TArray<UTwinActionButton*>& Buttons)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (UTwinActionButton* Button : Buttons)
		{
			Row->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0, 0, 6, 0));
			ControlButtons.Add(Button);
		}
		Detail->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 3));
	};
	AddRow({MakeAction(INVTEXT("냉각 고장 주입"), [this] { Command(ETwinAction::InjectCooling, 0); }),
		MakeAction(INVTEXT("마모 고장 주입"), [this] { Command(ETwinAction::InjectWear, 0); })});
	AddRow({MakeAction(INVTEXT("속도 100%"), [this] { Command(ETwinAction::SetSpeed, 1.0); }),
		MakeAction(INVTEXT("우회 해제"), [this] { Command(ETwinAction::SetBypass, 0.0); }),
		MakeAction(INVTEXT("조치안 비교"), [this] { RunWhatIf(); })});

	WhatIfBox = WidgetTree->ConstructWidget<UVerticalBox>();
	WhatIfTitle = MakeText(WidgetTree, FText::GetEmpty(), 12, Dim, true);
	WhatIfBox->AddChildToVerticalBox(WhatIfTitle)->SetPadding(FMargin(0, 8, 0, 4));
	for (int32 i = 0; i < WhatIfRows; ++i)
	{
		UTextBlock* Text = MakeText(WidgetTree, FText::GetEmpty(), 12, Bright);
		UTwinActionButton* Button = MakeAction(INVTEXT("적용"), [this, i] { ApplyWhatIf(i); });
		WhatIfTexts.Add(Text);
		WhatIfButtons.Add(Button);
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		Row->AddChildToHorizontalBox(Button);
		WhatIfBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 2));
	}
	WhatIfBox->SetVisibility(ESlateVisibility::Collapsed);
	Detail->AddChildToVerticalBox(WhatIfBox);

	CommandText = MakeText(WidgetTree, FText::GetEmpty(), 12, Dim);
	CommandText->SetAutoWrapText(true);
	Detail->AddChildToVerticalBox(CommandText)->SetPadding(FMargin(0, 8, 0, 0));
}

void UTwinHudWidget::BuildReplayBar(UCanvasPanel* Canvas)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	const auto Add = [&](UWidget* Widget, bool bFill = false)
	{
		UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Widget);
		Slot->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
		Slot->SetVerticalAlignment(VAlign_Center);
		Slot->SetPadding(FMargin(4, 0));
	};
	const auto Telemetry = [this] { return GetGameInstance()->GetSubsystem<UTelemetrySubsystem>(); };
	ReplayButton = MakeAction(INVTEXT("재생"), [Telemetry] { Telemetry()->SetReplay(!Telemetry()->IsReplay()); });
	PauseButton = MakeAction(INVTEXT("일시정지"), [Telemetry] { Telemetry()->SetPaused(!Telemetry()->IsPaused()); });
	SeekSlider = WidgetTree->ConstructWidget<USlider>();
	SeekSlider->IsFocusable = false;
	SeekSlider->OnValueChanged.AddDynamic(this, &UTwinHudWidget::HandleSeek);
	ReplayText = MakeText(WidgetTree, FText::GetEmpty(), 12, Dim);
	Add(ReplayButton);
	Add(MakeAction(INVTEXT("-10초"), [Telemetry] { if (Telemetry()->IsReplay()) { Telemetry()->SeekTo(Telemetry()->GetViewTime() - 10); } }));
	Add(PauseButton);
	Add(MakeAction(INVTEXT("+10초"), [Telemetry] { if (Telemetry()->IsReplay()) { Telemetry()->SeekTo(Telemetry()->GetViewTime() + 10); } }));
	Add(SeekSlider, true);
	Add(ReplayText);
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>();
	Box->SetWidthOverride(640.f);
	Box->AddChild(Row);
	Place(Canvas, MakePanel(WidgetTree, Box, FMargin(10, 8)), FAnchors(0.5f, 1), FVector2D(0.5f, 1), FMargin(0, -76, 0, 0), true);
}

void UTwinHudWidget::HandleSeek(float Value)
{
	UTelemetrySubsystem* Telemetry = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
	if (Telemetry->IsReplay())
	{
		const FTwinTimeline& Timeline = Telemetry->GetTimeline();
		Telemetry->SeekTo(FMath::Lerp(Timeline.GetStart(), Timeline.GetEnd(), double(Value)));
	}
}

void UTwinHudWidget::RefreshReplay()
{
	const UTelemetrySubsystem* Telemetry = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
	const FTwinTimeline& Timeline = Telemetry->GetTimeline();
	const bool bReplay = Telemetry->IsReplay();
	ReplayButton->Text->SetText(bReplay ? INVTEXT("실시간으로") : INVTEXT("재생"));
	ReplayButton->SetIsEnabled(!Timeline.IsEmpty());
	PauseButton->Text->SetText(Telemetry->IsPaused() ? INVTEXT("계속") : INVTEXT("일시정지"));
	PauseButton->SetIsEnabled(bReplay);
	SeekSlider->SetIsEnabled(bReplay);
	const double Range = Timeline.GetEnd() - Timeline.GetStart();
	// SetValue는 OnValueChanged를 부르지 않으므로 되감기와 충돌하지 않는다.
	SeekSlider->SetValue(bReplay && Range > 0.0 ? float((Telemetry->GetViewTime() - Timeline.GetStart()) / Range) : 1.f);
	ReplayText->SetText(bReplay ? FText::FromString(FString::Printf(TEXT("재생 중  -%.0f초"), Timeline.GetEnd() - Telemetry->GetViewTime())) : INVTEXT("실시간"));
	ReplayText->SetColorAndOpacity(FSlateColor(bReplay ? Orange : Cyan));
}

void UTwinHudWidget::Command(ETwinAction Action, double Value)
{
	const ATwinPlayerController* Controller = Cast<ATwinPlayerController>(GetOwningPlayer());
	const ATwinMachineActor* Machine = Controller ? Controller->GetSelected() : nullptr;
	FString Error;
	if (Machine && !GetGameInstance()->GetSubsystem<UTelemetrySubsystem>()->SendCommand({Action, Machine->MachineId, Value}, Error))
	{
		LocalError = Error;
		LocalErrorUntil = UTelemetrySubsystem::Now() + 4.0;
	}
}

void UTwinHudWidget::RunWhatIf()
{
	const ATwinPlayerController* Controller = Cast<ATwinPlayerController>(GetOwningPlayer());
	const ATwinMachineActor* Machine = Controller ? Controller->GetSelected() : nullptr;
	WhatIf.Reset();
	WhatIfMachine = Machine ? Machine->MachineId : NAME_None;
	if (!Machine)
	{
		return;
	}
	if (!GetGameInstance()->GetSubsystem<UTelemetrySubsystem>()->EvaluateWhatIf(Machine->MachineId, WhatIfHorizon, WhatIf))
	{
		LocalError = TEXT("현재 데이터로는 예측할 수 없습니다");
		LocalErrorUntil = UTelemetrySubsystem::Now() + 4.0;
	}
	WhatIfTitle->SetText(FText::FromString(FString::Printf(TEXT("앞으로 %.0f초 예측 (%s 기준)"), WhatIfHorizon,
		*GetGameInstance()->GetSubsystem<UTelemetrySubsystem>()->ToWallTime(GetGameInstance()->GetSubsystem<UTelemetrySubsystem>()->GetViewTime()).ToString(TEXT("%H:%M:%S")))));
}

void UTwinHudWidget::ApplyWhatIf(int32 Index)
{
	if (!WhatIf.IsValidIndex(Index))
	{
		return;
	}
	for (const FTwinCommand& Item : WhatIf[Index].Commands)
	{
		Command(Item.Action, Item.Value);
	}
}

void UTwinHudWidget::RefreshControls()
{
	const ATwinPlayerController* Controller = Cast<ATwinPlayerController>(GetOwningPlayer());
	const UTelemetrySubsystem* Telemetry = GetGameInstance()->GetSubsystem<UTelemetrySubsystem>();
	if (!Controller || !Controller->GetSelected() || Controller->GetSelected()->MachineId != WhatIfMachine)
	{
		WhatIf.Reset(); // 다른 설비의 예측을 남겨두지 않는다.
		WhatIfMachine = NAME_None;
	}
	const bool bCanCommand = !Telemetry->IsReplay() && (Telemetry->IsLocalSimulation() || Telemetry->GetLinkState() == ETwinLinkState::Connected);
	for (int32 i = 0; i < ControlButtons.Num() - 1; ++i) // 마지막은 조치안 비교라 재생 중에도 쓴다.
	{
		ControlButtons[i]->SetIsEnabled(bCanCommand);
	}

	WhatIfBox->SetVisibility(WhatIf.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	for (int32 i = 0; i < WhatIfRows; ++i)
	{
		const bool bValid = WhatIf.IsValidIndex(i);
		WhatIfTexts[i]->SetVisibility(bValid ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		WhatIfButtons[i]->SetVisibility(bValid ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bValid)
		{
			const FTwinWhatIfResult& R = WhatIf[i];
			WhatIfTexts[i]->SetText(FText::FromString(FString::Printf(TEXT("%s%s  %.0f개  정지 %d회  최고 %.0f°C"), R.bRecommended ? TEXT("추천  ") : TEXT(""),
				*R.Name.ToString(), R.Output, R.Trips, R.MaxTemp)));
			WhatIfTexts[i]->SetColorAndOpacity(FSlateColor(R.bRecommended ? Cyan : Bright));
			WhatIfButtons[i]->SetIsEnabled(bCanCommand && R.Commands.Num() > 0);
		}
	}

	if (UTelemetrySubsystem::Now() < LocalErrorUntil)
	{
		CommandText->SetText(FText::FromString(LocalError));
		CommandText->SetColorAndOpacity(FSlateColor(Red));
	}
	else if (Telemetry->GetCommands().IsEmpty())
	{
		CommandText->SetText(FText::GetEmpty());
	}
	else
	{
		const FTwinCommandEntry& Last = Telemetry->GetCommands().Last();
		const TCHAR* Names[] = {TEXT("냉각 고장 주입"), TEXT("마모 고장 주입"), TEXT("속도 변경"), TEXT("우회 설정"), TEXT("정비")};
		const TCHAR* State = Last.State == ETwinCommandState::Pending ? TEXT("응답 대기") : Last.State == ETwinCommandState::Accepted ? TEXT("적용됨") : TEXT("실패");
		CommandText->SetText(FText::FromString(FString::Printf(TEXT("%s %s: %s %s"), *Last.Command.MachineId.ToString(), Names[int32(Last.Command.Action)], State, *Last.Message)));
		CommandText->SetColorAndOpacity(FSlateColor(Last.State == ETwinCommandState::Failed ? Red : Dim));
	}
}

void UTwinHudWidget::SetCaption(const FText& Text)
{
	CaptionText->SetText(Text);
	CaptionPanel->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
