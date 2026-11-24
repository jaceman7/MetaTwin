#pragma once

#include "GameFramework/Actor.h"
#include "TwinScene.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPostProcessComponent;
class UStaticMeshComponent;

/** 두 설비를 잇는 컨베이어. 앞 설비의 처리량에 비례해 셀이 흐르고, 데이터가 끊기면 멈춘다. */
UCLASS()
class METATWIN_API ATwinConveyorActor : public AActor
{
	GENERATED_BODY()

public:
	ATwinConveyorActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Twin")
	FName SourceId;

	/** 로컬 +X 방향 길이(cm) */
	UPROPERTY(EditAnywhere, Category = "Twin", meta = (ClampMin = "100"))
	float Length = 500.f;

	UPROPERTY(EditAnywhere, Category = "Twin")
	TObjectPtr<UMaterialInterface> HullMaterial;

	UPROPERTY(EditAnywhere, Category = "Twin")
	TObjectPtr<UMaterialInterface> CellMaterial;

private:
	void LayoutCells();

	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TObjectPtr<UStaticMeshComponent> Belt;

	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TArray<TObjectPtr<UStaticMeshComponent>> Rails;

	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TObjectPtr<UInstancedStaticMeshComponent> Cells;

	float Offset = 0.f;
};

/** 레벨 전체의 노출·블룸 설정. */
UCLASS()
class METATWIN_API ATwinEnvironmentActor : public AActor
{
	GENERATED_BODY()

public:
	ATwinEnvironmentActor();

private:
	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TObjectPtr<UPostProcessComponent> PostProcess;
};
