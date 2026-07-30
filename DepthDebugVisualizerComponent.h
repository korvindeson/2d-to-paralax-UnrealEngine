#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DepthDebugVisualizerComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UDepthDebugVisualizerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDepthDebugVisualizerComponent();

    UFUNCTION(BlueprintCallable, Category = "Depth Debug Visualizer")
    void ToggleVisualizer();

    UFUNCTION(BlueprintCallable, Category = "Depth Debug Visualizer")
    void SetVisualizerEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Depth Debug Visualizer")
    void SetWireframeEnabled(bool bEnabled) { bShowWireframe = bEnabled; UpdateWireframeMode(); }

    UFUNCTION(BlueprintCallable, Category = "Depth Debug Visualizer")
    bool GetWireframeEnabled() const { return bShowWireframe; }

    UFUNCTION(BlueprintCallable, Category = "Depth Debug Visualizer")
    void RebuildMeshFromDepthMap(UTexture2D* DepthMapTexture);

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings")
    bool bStartEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings",
        meta = (ClampMin = "8", ClampMax = "256", UIMin = "8", UIMax = "256"))
    int32 GridResolution = 48;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings",
        meta = (DisplayName = "Fallback Mesh Size (used when no profile set)"))
    float MeshSize = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings",
        meta = (DisplayName = "Fallback Height Scale (used when no profile set)"))
    float HeightScale = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings",
        meta = (DisplayName = "Profile Half Width (0=use Fallback Mesh)"))
    float ProfileHalfWidth = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings",
        meta = (DisplayName = "Profile Half Depth (0=use Fallback Height)"))
    float ProfileHalfDepth = 0.0f;

    UFUNCTION(BlueprintCallable, Category = "Depth Debug Visualizer|Settings")
    void SetProfileDimensions(float HalfWidth, float HalfHeight, float HalfDepth) { ProfileHalfWidth = HalfWidth; ProfileHalfDepth = HalfDepth; }

    UFUNCTION(BlueprintCallable, Category = "Depth Debug Visualizer|Settings")
    void ClearProfileDimensions() { ProfileHalfWidth = 0.0f; ProfileHalfDepth = 0.0f; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings")
    FVector LocalOffset = FVector(0.0f, 0.0f, 25.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings")
    bool bShowWireframe = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings",
        meta = (EditCondition = "bShowWireframe"))
    UMaterialInterface* WireframeMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings")
    bool bUseVertexColors = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings")
    FLinearColor LowColor = FLinearColor(0.0f, 0.0f, 0.8f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings")
    FLinearColor HighColor = FLinearColor(0.8f, 0.0f, 0.0f, 1.0f);

    UPROPERTY(BlueprintReadOnly, Category = "Depth Debug Visualizer|Outputs")
    bool bIsEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Depth Debug Visualizer|Settings")
    UMaterialInterface* DepthDebugMaterial = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Depth Debug Visualizer|Outputs")
    int32 CurrentVertexCount = 0;

private:
    UPROPERTY()
    UPrimitiveComponent* DebugMesh;

    UPROPERTY()
    UMaterialInstanceDynamic* DebugMaterialInstance;

    UPROPERTY()
    UTexture2D* LastDepthTexture;

    bool bNeedsRebuild = false;
    TArray<float> CachedDepthSamples;
    uint8 OriginalAddressX;
    uint8 OriginalAddressY;
    bool bAddressModeSaved = false;

    void CreateProceduralMeshComponent();
    void BuildDebugMesh();
    void SampleDepthMap(UTexture2D* Texture, TArray<float>& OutSamples, int32& OutSampleWidth, int32& OutSampleHeight);
    FLinearColor DepthToColor(float Depth) const;
    void UpdateWireframeMode();
    void OnStateChanged();
};
