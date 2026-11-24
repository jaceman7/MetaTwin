#pragma once

#include "GameFramework/Actor.h"
#include "TwinTypes.h"
#include "TwinMachineActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UBoxComponent;
class UTwinLabelWidget;
class UWidgetComponent;

UENUM(BlueprintType)
enum class ETwinMachineKind : uint8
{
	Tank,
	Roller,
	Rack,
	Gantry
};

/** 설비 하나의 상태를 표시만 한다. 데이터는 MachineId로 찾는다. */
UCLASS()
class METATWIN_API ATwinMachineActor : public AActor
{
	GENERATED_BODY()

public:
	ATwinMachineActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void SetSelected(bool bInSelected);
	bool IsSelected() const { return bSelected; }
	/** 비콘에 실제로 적용된 색. */
	FLinearColor GetBeaconColor() const;
	void SetLabelVisible(bool bInVisible);
	FVector GetLabelPoint() const { return GetActorLocation() + FVector(0, 0, 420); }
	void SetHeatmap(bool bInHeatmap) { bHeatmap = bInHeatmap; }
	/** 화면에 보이는 설비 중심. */
	FVector GetFocusPoint() const { return GetActorLocation() + FVector(0, 0, 160); }

	/** 상태 문구와 색. HUD와 3D 표시가 같은 기준을 쓴다. */
	static FLinearColor StatusColor(ETwinMachineState State, bool bFresh);
	static FText StatusText(ETwinMachineState State, bool bHasData, bool bFresh);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Twin")
	FName MachineId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Twin")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Twin")
	ETwinMachineKind Kind = ETwinMachineKind::Tank;

	/** 공정 순서. 0부터. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Twin")
	int32 LineOrder = 0;

	/** BaseColor, Glow 벡터 파라미터가 있는 머티리얼. */
	UPROPERTY(EditAnywhere, Category = "Twin")
	TObjectPtr<UMaterialInterface> HullMaterial;

	UPROPERTY(EditAnywhere, Category = "Twin")
	TObjectPtr<UMaterialInterface> MetalMaterial;

	/** StatusColor 벡터 파라미터가 있는 머티리얼. */
	UPROPERTY(EditAnywhere, Category = "Twin")
	TObjectPtr<UMaterialInterface> StatusMaterial;

private:
	enum class EPart : uint8 { Hidden, Hull, Metal, Status };
	void SetPart(int32 Index, EPart Type, bool bCylinder, const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator);

	static constexpr int32 PartCount = 16;

	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TObjectPtr<UStaticMeshComponent> Beacon;

	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TObjectPtr<UStaticMeshComponent> SelectPad;

	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TObjectPtr<UWidgetComponent> Label;

	/** 설비 빈 공간을 눌러도 선택되게 하는 클릭 영역. */
	UPROPERTY(VisibleAnywhere, Category = "Twin")
	TObjectPtr<UBoxComponent> PickBox;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeaconMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HullInstance;

	// 저장된 레벨에서는 OnConstruction이 다시 실행되지 않으므로 직렬화한다.
	UPROPERTY()
	TArray<int32> HullParts;

	UPROPERTY()
	TArray<int32> StatusParts;

	bool bSelected = false;
	bool bHeatmap = false;
	float NextRefresh = 0.f;
	float AnimPhase = 0.f;
	float AnimSpeed = 0.f;
};
