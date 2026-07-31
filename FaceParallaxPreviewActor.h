#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FaceParallaxPreviewActor.generated.h"

class USkeletalMeshComponent;
class USceneCaptureComponent2D;
class UFaceParallaxComponent;
class UDepthDebugVisualizerComponent;
class UTextureRenderTarget2D;

UCLASS(BlueprintType)
class AFaceParallaxPreviewActor : public AActor
{
    GENERATED_BODY()

public:
    AFaceParallaxPreviewActor();

    // --- COMPONENTS ---
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
    class USceneComponent* PreviewRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
    USkeletalMeshComponent* PreviewMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
    UFaceParallaxComponent* FaceParallax;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
    UDepthDebugVisualizerComponent* DepthDebug;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
    USceneCaptureComponent2D* SceneCapture;

    // --- SETUP METHODS ---
    UFUNCTION(BlueprintCallable, Category = "Preview")
    void AssignSkeletalMesh(USkeletalMesh* Mesh);

    UFUNCTION(BlueprintCallable, Category = "Preview")
    void SetRenderTarget(UTextureRenderTarget2D* RenderTarget);

    // --- CAMERA CONTROL ---
    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    void SetOrbitYaw(float Degrees);

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    void SetOrbitPitch(float Degrees);

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    void SetOrbitDistance(float Distance);

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    void SetPreviewFOV(float FOV);

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    float GetOrbitYaw() const { return OrbitYaw; }

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    float GetOrbitPitch() const { return OrbitPitch; }

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    float GetOrbitDistance() const { return OrbitDistance; }

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    float GetPreviewFOV() const { return PreviewFOV; }

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    void ResetCamera();

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    void SetAutoRotate(bool bEnabled) { bAutoRotate = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    bool GetAutoRotate() const { return bAutoRotate; }

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    void SetAutoRotateSpeed(float DegreesPerSec) { AutoRotateSpeed = FMath::Max(0.0f, DegreesPerSec); }

    UFUNCTION(BlueprintCallable, Category = "Preview|Camera")
    float GetAutoRotateSpeed() const { return AutoRotateSpeed; }

    // --- PRESET CONTROL ---
    UFUNCTION(BlueprintCallable, Category = "Preview|Preset")
    void ApplyPreset(class UFaceParallaxPreset* Preset);

    // --- PART TRANSFORM ACCESS ---
    UFUNCTION(BlueprintCallable, Category = "Preview|Parts")
    FFaceArtTransform GetEffectivePartTransform(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Preview|Parts")
    FVector2D GetPartSourceSize(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Preview|Parts")
    void RefreshPreview();

    // --- DEBUG TOGGLES ---
    UFUNCTION(BlueprintCallable, Category = "Preview|Debug")
    void ShowDepthMesh(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Preview|Debug")
    void ShowWireframe(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Preview|Debug")
    void ColorByDepth(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Preview|Debug")
    void ShowTextures(bool bVisible);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview|Camera")
    float AutoRotateSpeed = 30.0f;

private:
    float OrbitYaw = 0.0f;
    float OrbitPitch = -10.0f;
    float OrbitDistance = 220.0f;
    float PreviewFOV = 30.0f;
    bool bAutoRotate = false;

    void UpdateCaptureTransform();
    void MarkOrbitDirty() { bOrbitDirty = true; }

    bool bOrbitDirty = true;
    bool bCaptureDirty = true;
    bool bLastColorByDepth = false;
};
