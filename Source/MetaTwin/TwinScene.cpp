#include "TwinScene.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "TelemetrySubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float CellSpacing = 110.f;
	constexpr float BeltHeight = 90.f;
	/** 처리량 1개/분당 이동 속도(cm/s). 시각 효과용 배율이다. */
	constexpr float SpeedPerThroughput = 2.2f;
}

ATwinConveyorActor::ATwinConveyorActor()
{
	PrimaryActorTick.bCanEverTick = true;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Belt = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Belt"));
	Belt->SetupAttachment(RootComponent);
	Belt->SetStaticMesh(Cube.Object);
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* Rail = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Rail%d"), i));
		Rail->SetupAttachment(RootComponent);
		Rail->SetStaticMesh(Cube.Object);
		Rails.Add(Rail);
	}
	Cells = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Cells"));
	Cells->SetupAttachment(RootComponent);
	Cells->SetStaticMesh(Cube.Object);
	Cells->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cells->SetCastShadow(false);
}

void ATwinConveyorActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Belt->SetRelativeLocation(FVector(Length * 0.5f, 0, BeltHeight * 0.5f));
	Belt->SetRelativeScale3D(FVector(Length / 100.f, 0.9f, BeltHeight / 100.f));
	Belt->SetMaterial(0, HullMaterial);
	for (int32 i = 0; i < Rails.Num(); ++i)
	{
		Rails[i]->SetRelativeLocation(FVector(Length * 0.5f, i ? 46.f : -46.f, BeltHeight + 4.f));
		Rails[i]->SetRelativeScale3D(FVector(Length / 100.f, 0.06f, 0.14f));
		Rails[i]->SetMaterial(0, HullMaterial);
	}
	Cells->SetMaterial(0, CellMaterial);
}

void ATwinConveyorActor::BeginPlay()
{
	Super::BeginPlay();
	Cells->ClearInstances();
	const int32 Count = FMath::Max(1, FMath::FloorToInt32(Length / CellSpacing));
	for (int32 i = 0; i < Count; ++i)
	{
		Cells->AddInstance(FTransform::Identity);
	}
	LayoutCells();
}

void ATwinConveyorActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UGameInstance* GameInstance = GetGameInstance();
	const UTelemetrySubsystem* Telemetry = GameInstance ? GameInstance->GetSubsystem<UTelemetrySubsystem>() : nullptr;
	FTwinSample Sample;
	bool bFresh = false;
	if (Telemetry && Telemetry->GetLatest(SourceId, Sample, bFresh) && bFresh && Sample.Throughput > 0.0)
	{
		Offset += static_cast<float>(Sample.Throughput) * SpeedPerThroughput * DeltaSeconds;
		LayoutCells();
	}
}

void ATwinConveyorActor::LayoutCells()
{
	const int32 Count = Cells->GetInstanceCount();
	const float Span = Count * CellSpacing;
	Offset = FMath::Fmod(Offset, Span);
	for (int32 i = 0; i < Count; ++i)
	{
		const float X = FMath::Fmod(i * CellSpacing + Offset, Span) + (Length - Span) * 0.5f;
		Cells->UpdateInstanceTransform(i, FTransform(FRotator::ZeroRotator, FVector(X, 0, BeltHeight + 16.f), FVector(0.5f, 0.36f, 0.26f)), false, i == Count - 1, true);
	}
}

ATwinEnvironmentActor::ATwinEnvironmentActor()
{
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	SetRootComponent(PostProcess);
	PostProcess->bUnbound = true;

	FPostProcessSettings& S = PostProcess->Settings;
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Manual;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.f;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.6f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.5f;
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.f;
}
