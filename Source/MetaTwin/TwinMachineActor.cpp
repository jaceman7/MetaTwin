#include "TwinMachineActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TelemetrySubsystem.h"
#include "TwinHudWidget.h"
#include "TwinLine.h"
#include "UObject/ConstructorHelpers.h"

ATwinMachineActor::ATwinMachineActor()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	for (int32 i = 0; i < PartCount; ++i)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Part%d"), i));
		Part->SetupAttachment(RootComponent);
		Parts.Add(Part);
	}

	Beacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(RootComponent);
	Beacon->SetStaticMesh(CylinderMesh);
	Beacon->SetRelativeLocation(FVector(0, 0, 345));
	Beacon->SetRelativeScale3D(FVector(0.35, 0.35, 0.3));
	Beacon->SetCastShadow(false);

	SelectPad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectPad"));
	SelectPad->SetupAttachment(RootComponent);
	SelectPad->SetStaticMesh(CubeMesh);
	SelectPad->SetRelativeLocation(FVector(0, 0, 3));
	SelectPad->SetRelativeScale3D(FVector(3.6, 4.6, 0.05));
	SelectPad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SelectPad->SetCastShadow(false);
	SelectPad->SetVisibility(false);

	Label = CreateDefaultSubobject<UWidgetComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(0, 0, 420));
	Label->SetWidgetSpace(EWidgetSpace::Screen);
	Label->SetDrawAtDesiredSize(true);
	Label->SetPivot(FVector2D(0.5, 1.0));
	Label->SetWidgetClass(UTwinLabelWidget::StaticClass());
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PickBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PickBox"));
	PickBox->SetupAttachment(RootComponent);
	PickBox->SetRelativeLocation(FVector(0, 0, 175));
	PickBox->SetBoxExtent(FVector(150, 200, 175));
	PickBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

void ATwinMachineActor::SetPart(int32 Index, EPart Type, bool bCylinder, const FVector& Center, const FVector& Size, const FRotator& Rotation)
{
	UStaticMeshComponent* Part = Parts[Index];
	Part->SetStaticMesh(bCylinder ? CylinderMesh : CubeMesh);
	Part->SetRelativeLocationAndRotation(Center, Rotation);
	Part->SetRelativeScale3D(Size / 100.0);
	Part->SetVisibility(true);
	Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Part->SetMaterial(0, Type == EPart::Hull ? HullMaterial : Type == EPart::Metal ? MetalMaterial : StatusMaterial);
	if (Type == EPart::Hull) { HullParts.Add(Index); }
	if (Type == EPart::Status) { StatusParts.Add(Index); Part->SetCastShadow(false); }
}

void ATwinMachineActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	HullParts.Reset();
	StatusParts.Reset();
	for (UStaticMeshComponent* Part : Parts)
	{
		Part->SetVisibility(false);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Beacon->SetMaterial(0, StatusMaterial);
	SelectPad->SetMaterial(0, StatusMaterial);

	// 공통: 받침대와 둘레 상태등. 움직이는 부품은 항상 2번이다.
	SetPart(0, EPart::Hull, false, FVector(0, 0, 20), FVector(300, 400, 40));
	SetPart(1, EPart::Status, false, FVector(0, 0, 44), FVector(304, 404, 6));
	SetPart(14, EPart::Hull, false, FVector(0, 0, 50), FVector(296, 396, 10));
	const FRotator AlongY(0, 0, 90); // 원기둥 축을 Y로 눕힌다.

	switch (Kind)
	{
	case ETwinMachineKind::Tank:
		SetPart(2, EPart::Metal, false, FVector(0, -90, 305), FVector(130, 16, 12));
		SetPart(3, EPart::Hull, true, FVector(0, -90, 170), FVector(170, 170, 250));
		SetPart(4, EPart::Hull, true, FVector(0, 100, 150), FVector(140, 140, 210));
		SetPart(5, EPart::Metal, true, FVector(0, -90, 298), FVector(60, 60, 20));
		SetPart(6, EPart::Metal, true, FVector(0, 10, 200), FVector(24, 24, 120), AlongY);
		SetPart(7, EPart::Status, true, FVector(0, -90, 230), FVector(174, 174, 8));
		SetPart(8, EPart::Status, true, FVector(0, 100, 200), FVector(144, 144, 8));
		SetPart(9, EPart::Metal, false, FVector(110, 100, 100), FVector(50, 70, 110));
		SetPart(10, EPart::Metal, true, FVector(0, -90, 335), FVector(22, 22, 60));
		break;
	case ETwinMachineKind::Roller:
		SetPart(2, EPart::Metal, true, FVector(0, 0, 265), FVector(64, 64, 250), AlongY);
		SetPart(3, EPart::Hull, false, FVector(0, -160, 170), FVector(230, 50, 250));
		SetPart(4, EPart::Hull, false, FVector(0, 160, 170), FVector(230, 50, 250));
		SetPart(5, EPart::Metal, true, FVector(-80, 0, 180), FVector(54, 54, 280), AlongY);
		SetPart(6, EPart::Metal, true, FVector(80, 0, 180), FVector(54, 54, 280), AlongY);
		SetPart(7, EPart::Hull, false, FVector(0, 0, 80), FVector(210, 280, 70));
		SetPart(8, EPart::Status, false, FVector(117, -160, 200), FVector(4, 30, 120));
		SetPart(9, EPart::Status, false, FVector(117, 160, 200), FVector(4, 30, 120));
		SetPart(10, EPart::Metal, false, FVector(0, 0, 310), FVector(120, 330, 20));
		break;
	case ETwinMachineKind::Rack:
		SetPart(2, EPart::Status, false, FVector(142, 0, 190), FVector(6, 330, 6));
		for (int32 i = 0; i < 5; ++i)
		{
			SetPart(3 + i, EPart::Hull, false, FVector(0, 0, 80 + i * 55.0), FVector(260, 350, 30));
		}
		for (int32 i = 0; i < 4; ++i)
		{
			SetPart(8 + i, EPart::Metal, false, FVector(i & 1 ? 130 : -130, i & 2 ? 180 : -180, 185), FVector(16, 16, 290));
		}
		SetPart(12, EPart::Status, false, FVector(131, 0, 107), FVector(3, 300, 4));
		SetPart(13, EPart::Status, false, FVector(131, 0, 217), FVector(3, 300, 4));
		break;
	case ETwinMachineKind::Gantry:
		SetPart(2, EPart::Metal, false, FVector(0, 0, 245), FVector(70, 60, 110));
		SetPart(3, EPart::Hull, false, FVector(0, -170, 180), FVector(90, 40, 280));
		SetPart(4, EPart::Hull, false, FVector(0, 170, 180), FVector(90, 40, 280));
		SetPart(5, EPart::Hull, false, FVector(0, 0, 310), FVector(90, 380, 40));
		SetPart(6, EPart::Metal, false, FVector(0, 0, 90), FVector(200, 280, 50));
		SetPart(7, EPart::Status, false, FVector(46, 0, 310), FVector(4, 340, 8));
		SetPart(8, EPart::Metal, false, FVector(-90, 120, 120), FVector(50, 70, 150));
		break;
	}
}

void ATwinMachineActor::BeginPlay()
{
	Super::BeginPlay();
	if (StatusMaterial)
	{
		BeaconMaterial = UMaterialInstanceDynamic::Create(StatusMaterial, this);
		Beacon->SetMaterial(0, BeaconMaterial);
		SelectPad->SetMaterial(0, BeaconMaterial);
		for (const int32 Index : StatusParts)
		{
			Parts[Index]->SetMaterial(0, BeaconMaterial);
		}
	}
	if (HullMaterial)
	{
		HullInstance = UMaterialInstanceDynamic::Create(HullMaterial, this);
		for (const int32 Index : HullParts)
		{
			Parts[Index]->SetMaterial(0, HullInstance);
		}
	}
}

void ATwinMachineActor::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
	SelectPad->SetVisibility(bSelected);
}

void ATwinMachineActor::SetLabelVisible(bool bInVisible)
{
	if (Label->IsVisible() != bInVisible)
	{
		Label->SetVisibility(bInVisible);
	}
}

FLinearColor ATwinMachineActor::StatusColor(ETwinMachineState State, bool bFresh)
{
	if (bFresh)
	{
		switch (State)
		{
		case ETwinMachineState::Running: return FLinearColor(0.f, 0.8f, 1.f);
		case ETwinMachineState::Warning: return FLinearColor(1.f, 0.45f, 0.f);
		case ETwinMachineState::Fault: return FLinearColor(1.f, 0.02f, 0.05f);
		case ETwinMachineState::Maintenance: return FLinearColor(0.45f, 0.15f, 1.f);
		default: break;
		}
	}
	return FLinearColor(0.12f, 0.12f, 0.15f);
}

FText ATwinMachineActor::StatusText(ETwinMachineState State, bool bHasData, bool bFresh)
{
	if (!bHasData) { return INVTEXT("NO DATA"); }
	if (!bFresh) { return INVTEXT("STALE"); }
	switch (State)
	{
	case ETwinMachineState::Running: return INVTEXT("RUNNING");
	case ETwinMachineState::Warning: return INVTEXT("WARNING");
	case ETwinMachineState::Fault: return INVTEXT("FAULT");
	case ETwinMachineState::Maintenance: return INVTEXT("MAINT");
	default: return INVTEXT("UNKNOWN");
	}
}

void ATwinMachineActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AnimPhase += AnimSpeed * DeltaSeconds;
	switch (Kind)
	{
	case ETwinMachineKind::Tank: Parts[2]->SetRelativeRotation(FRotator(0, FMath::Fmod(AnimPhase * 140.f, 360.f), 0)); break;
	case ETwinMachineKind::Roller: Parts[2]->SetRelativeLocation(FVector(0, 0, 265 + 8 * FMath::Sin(AnimPhase * 3.f))); break;
	case ETwinMachineKind::Rack: Parts[2]->SetRelativeLocation(FVector(142, 0, 190 + 120 * FMath::Sin(AnimPhase))); break;
	case ETwinMachineKind::Gantry: Parts[2]->SetRelativeLocation(FVector(0, 110 * FMath::Sin(AnimPhase * 1.4f), 245)); break;
	}

	NextRefresh -= DeltaSeconds;
	if (NextRefresh > 0.f)
	{
		return;
	}
	NextRefresh = 0.1f;

	const UGameInstance* GameInstance = GetGameInstance();
	const UTelemetrySubsystem* Telemetry = GameInstance ? GameInstance->GetSubsystem<UTelemetrySubsystem>() : nullptr;
	if (!Telemetry)
	{
		return;
	}

	FTwinSample Sample;
	bool bFresh = false;
	const bool bHasData = Telemetry->GetLatest(MachineId, Sample, bFresh);
	AnimSpeed = bFresh ? static_cast<float>(Sample.Throughput) / 40.f : 0.f;

	if (BeaconMaterial)
	{
		BeaconMaterial->SetVectorParameterValue(TEXT("StatusColor"), StatusColor(Sample.State, bFresh));
	}
	if (HullInstance)
	{
		// 오래된 온도는 히트맵에 칠하지 않는다.
		const FLinearColor Glow = bHeatmap && bFresh ? TwinLine::HeatColor(Sample.Temperature) * 0.6f : FLinearColor::Black;
		HullInstance->SetVectorParameterValue(TEXT("Glow"), Glow);
	}
	if (UTwinLabelWidget* Widget = Cast<UTwinLabelWidget>(Label->GetUserWidgetObject()))
	{
		const FString Temperature = bHasData ? FString::Printf(TEXT("%.1f °C"), Sample.Temperature) : FString(TEXT("--"));
		Widget->SetContent(FText::FromName(MachineId), FText::FromString(FString::Printf(TEXT("%s   %s"),
			*StatusText(Sample.State, bHasData, bFresh).ToString(), *Temperature)), UTwinHudWidget::TextStatusColor(Sample.State, bFresh));
	}
}

FLinearColor ATwinMachineActor::GetBeaconColor() const
{
	return BeaconMaterial ? BeaconMaterial->K2_GetVectorParameterValue(TEXT("StatusColor")) : FLinearColor::Transparent;
}
