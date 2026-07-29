#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FaceParallaxTypes.h"
#include "FaceParallaxEditorWidget.generated.h"

class AFaceParallaxPreviewActor;
class UFaceParallaxPreset;
class UFaceParallaxComponent;

UCLASS(BlueprintType, Blueprintable)
class UFaceParallaxEditorWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // ===== TARGETS =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Editor|Targets",
        meta = (DisplayName = "Preview Actor"))
    TObjectPtr<AFaceParallaxPreviewActor> PreviewActor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Editor|Targets",
        meta = (DisplayName = "Active Preset"))
    TObjectPtr<UFaceParallaxPreset> ActivePreset;

    // ===== PRESET =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void ApplyPresetToPreview();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    UFaceParallaxPreset* CreateNewPreset(const FString& AssetName, const FString& PackagePath);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    bool SavePreset();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void SetCanvasSize(float Width, float Height);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    FVector2D GetCanvasSize() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void SetAutoFitOnAssign(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    bool GetAutoFitOnAssign() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void BatchSetTextures(EFaceAngleState State, FName LayerTag, const TArray<FFaceTextureSet>& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void ClearAllTextures();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void DuplicateState(EFaceAngleState SourceState, EFaceAngleState DestState);

    // ===== VIEWSTATE =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewState")
    void SetActiveViewState(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewState")
    EFaceAngleState GetActiveViewState() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewState")
    TArray<EFaceAngleState> GetAssignedStates() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewState")
    bool HasState(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewState")
    TArray<FName> GetLayerTagsForState(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewState")
    int32 GetLayerCount() const;

    // ===== TRANSFORM =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    FFaceArtTransform GetLayerCanonicalTransform(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SetLayerPosition(EFaceAngleState State, FName LayerTag, float X, float Y);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    FVector2D GetLayerPosition(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SetLayerScale(EFaceAngleState State, FName LayerTag, float X, float Y);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    FVector2D GetLayerScale(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SetLayerRotation(EFaceAngleState State, FName LayerTag, float Degrees);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    float GetLayerRotation(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SetLayerTransform(EFaceAngleState State, FName LayerTag, const FFaceArtTransform& Transform);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void ResetLayerTransform(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    FFaceArtTransform GetEffectiveLayerTransform(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void ApplyAutoFit(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void ApplyAutoFitToAllSlots();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SyncLayerToAllViews(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SyncAllLayersToAllViews();

    // ===== VIEWOVERRIDE =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    bool HasViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    FFaceArtTransform GetViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    void SetViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView,
        const FFaceArtTransform& Override);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    void ClearViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    void ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    void ClearAllOverrides();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    TArray<EFaceAngleState> GetOverrideViewsForSlot(EFaceAngleState State, FName LayerTag) const;

    // ===== TEXTURES =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Textures")
    FFaceTextureSet GetSlotTextures(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Textures")
    void SetSlotTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Textures")
    FVector2D GetSlotSourceSize(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Textures")
    class UTexture2D* GetSlotAlbedo(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Textures")
    class UTexture2D* GetSlotDepth(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Textures")
    class UTexture2D* GetSlotNormal(EFaceAngleState State, FName LayerTag) const;

    // ===== CAMERA =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void SetOrbitYaw(float Degrees);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    float GetOrbitYaw() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void SetOrbitPitch(float Degrees);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    float GetOrbitPitch() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void SetOrbitDistance(float Distance);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    float GetOrbitDistance() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void SetPreviewFOV(float FOV);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    float GetPreviewFOV() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void SetAutoRotate(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    bool GetAutoRotate() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void SetAutoRotateSpeed(float DegreesPerSec);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    float GetAutoRotateSpeed() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void ResetCamera();

    // ===== DEBUGOAVERLAYS =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|DebugOverlays")
    void ShowTextures(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|DebugOverlays")
    void ShowDepthMesh(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|DebugOverlays")
    void ShowWireframe(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|DebugOverlays")
    void ColorByDepth(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|DebugOverlays")
    void SetEnableMaterialDebugMode(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|DebugOverlays")
    bool GetEnableMaterialDebugMode() const;

    // ===== STATUS =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    int32 GetAssignedStateCount() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    int32 GetTotalAssignedSlots() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    int32 GetActiveLayerCount() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    FString GetStatusString() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    int32 GetStateTextureCount() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    FString GetStatusDetails() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    bool HasSlot(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    bool IsSlotFullyAssigned(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    void ClearState(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    void ClearAll();

    // ===== DYNAMICART =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|DynamicArt")
    void SetDriveArtPositionFromYaw(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|DynamicArt")
    bool GetDriveArtPositionFromYaw() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|DynamicArt")
    void SetMaxYawArtOffset(float Offset);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|DynamicArt")
    float GetMaxYawArtOffset() const;

    // ===== TEXTUREANDTRANSFORMPARAMS =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetAlbedoParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetAlbedoParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetNormalParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetNormalParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetDepthParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetDepthParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetAlbedoPrevParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetAlbedoPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetNormalPrevParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetNormalPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetDepthPrevParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetDepthPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetArtPositionParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetArtPositionParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetArtScaleParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetArtScaleParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    void SetArtRotationParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|TextureAndTransformParams")
    FName GetArtRotationParamName() const;

    // ===== BLINK =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    void SetBlinkingEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    bool GetBlinkingEnabled() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    void SetBlinkInterval(float Min, float Max);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    float GetBlinkIntervalMin() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    float GetBlinkIntervalMax() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    void ForceBlink();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    bool IsBlinking() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    void SetBlinkFrameDuration(float Duration);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    float GetBlinkFrameDuration() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    int32 GetBlinkFrameCount(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    void SetBlinkFrameTextures(EFaceAngleState State, FName LayerTag, int32 FrameIndex, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    FFaceTextureSet GetBlinkFrameTextures(EFaceAngleState State, FName LayerTag, int32 FrameIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Blink")
    void ClearBlinkFrames(EFaceAngleState State, FName LayerTag);

    // ===== EXPRESSION =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void SetExpression(EExpression NewExpression);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    EExpression GetExpression() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void SetExpressionCrossfadeDuration(float Duration);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    float GetExpressionCrossfadeDuration() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    bool IsExpressionTransitioning() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void ClearExpressionTextures(EFaceAngleState State, FName LayerTag, EExpression Expression);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void SetExpressionTextures(EFaceAngleState State, FName LayerTag, EExpression Expression, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FFaceTextureSet GetExpressionTextures(EFaceAngleState State, FName LayerTag, EExpression Expression) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    bool HasExpressionTextures(EFaceAngleState State, FName LayerTag, EExpression Expression) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    TArray<EExpression> GetAssignedExpressions(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void SetExpressionBlendParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FName GetExpressionBlendParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void SetExpressionAlbedoPrevParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FName GetExpressionAlbedoPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void SetExpressionNormalPrevParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FName GetExpressionNormalPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void SetExpressionDepthPrevParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FName GetExpressionDepthPrevParamName() const;

    // ===== VISEME =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void SetVisemeEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    bool GetVisemeEnabled() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void PlayViseme(EViseme NewViseme);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void StopViseme();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    bool IsVisemePlaying() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    EViseme GetCurrentViseme() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void SetVisemeFrameDuration(float Duration);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    float GetVisemeFrameDuration() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    int32 GetVisemeFrameCount(EFaceAngleState State, FName LayerTag, EExpression Expression, EViseme Viseme) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void SetVisemeFrameTextures(EFaceAngleState State, FName LayerTag, EExpression Expression,
        EViseme Viseme, int32 FrameIndex, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    FFaceTextureSet GetVisemeFrameTextures(EFaceAngleState State, FName LayerTag, EExpression Expression,
        EViseme Viseme, int32 FrameIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    TArray<EViseme> GetAssignedVisemes(EFaceAngleState State, FName LayerTag, EExpression Expression) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void ClearVisemeFrames(EFaceAngleState State, FName LayerTag, EExpression Expression, EViseme Viseme);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void ClearAllVisemes(EFaceAngleState State, FName LayerTag, EExpression Expression);

    // ===== PARAMETER =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void SetParamsEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    bool GetParamsEnabled() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void DefineParameter(FName ParamName, float DefaultValue, float Min, float Max, float SmoothingSpeed);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void SetParameterValue(FName ParamName, float Value);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    float GetParameterValue(FName ParamName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    TArray<FName> GetParameterNames() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void ResetAllParameters();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void SetParamSmoothingSpeed(FName ParamName, float Speed);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    float GetParamSmoothingSpeed(FName ParamName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void SetParamBlendParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    FName GetParamBlendParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void SetParamAltAlbedoParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    FName GetParamAltAlbedoParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void SetParamAltNormalParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    FName GetParamAltNormalParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    void SetParamAltDepthParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Parameter")
    FName GetParamAltDepthParamName() const;

    // ===== PARAMBINDING =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|ParamBinding")
    TArray<FFaceParamBinding> GetParamBindings(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ParamBinding")
    void SetParamBindings(EFaceAngleState State, FName LayerTag, const TArray<FFaceParamBinding>& Bindings);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ParamBinding")
    FFaceTextureSet GetAltTextures(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ParamBinding")
    void SetAltTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures);

    // ===== SWOOSH =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    bool GetSwooshEnabled() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshSpeedThreshold(float Threshold);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    float GetSwooshSpeedThreshold() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshBusyness(float Busyness);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    float GetSwooshBusyness() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshSize(float Size);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    float GetSwooshSize() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void ForceSwoosh(EFaceAngleState TargetState);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    bool IsSwooshActive() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    int32 GetSwooshFrameCount(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshFrameTextures(EFaceAngleState State, FName LayerTag, int32 FrameIndex, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    FFaceTextureSet GetSwooshFrameTextures(EFaceAngleState State, FName LayerTag, int32 FrameIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void ClearSwooshFrames(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshFrameDuration(float Duration);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    float GetSwooshFrameDuration() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshBlendOutDuration(float Duration);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    float GetSwooshBlendOutDuration() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshLayerBlendParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    FName GetSwooshLayerBlendParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshIntensityParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    FName GetSwooshIntensityParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshAngleParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    FName GetSwooshAngleParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshSizeParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    FName GetSwooshSizeParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    void SetSwooshTextureParamName(FName Name);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Swoosh")
    FName GetSwooshTextureParamName() const;

    // ===== NESTEDART =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedArtEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    bool GetNestedArtEnabled() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    int32 GetNestedElementCount(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FFaceNestedArt GetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceNestedArt& Element);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void AddNestedElement(EFaceAngleState State, FName LayerTag, const FFaceNestedArt& Element);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void RemoveNestedElement(EFaceAngleState State, FName LayerTag, int32 Index);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedTextures(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FFaceTextureSet GetNestedTextures(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedTransform(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceArtTransform& Transform);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FFaceArtTransform GetNestedTransform(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedPivot(EFaceAngleState State, FName LayerTag, int32 Index, FVector2D Pivot);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FVector2D GetNestedPivot(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedJiggleEnabled(EFaceAngleState State, FName LayerTag, int32 Index, bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedJiggleSettings(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceJiggleSettings& Settings);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FFaceJiggleSettings GetNestedJiggleSettings(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedVisibility(EFaceAngleState State, FName LayerTag, FName ElementName, EFaceAngleState ViewState, bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    bool GetNestedVisibility(EFaceAngleState State, FName LayerTag, FName ElementName, EFaceAngleState ViewState) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedIdleFrames(EFaceAngleState State, FName LayerTag, int32 Index, const TArray<FFaceTextureSet>& Frames);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    TArray<FFaceTextureSet> GetNestedIdleFrames(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void ClearNestedIdleFrames(EFaceAngleState State, FName LayerTag, int32 Index);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void BatchSetNestedTexturesAllViews(FName LayerTag, FName ElementName, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void DuplicateNestedElement(EFaceAngleState State, FName LayerTag, int32 SourceIndex, int32 DestIndex);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SyncNestedToAllViews(FName LayerTag, FName ElementName);

    // --- PIN FUNCTIONS ---

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FFacePin3D GetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index, const FFacePin3D& Pin);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FVector2D GetNestedPinUV(EFaceAngleState State, FName LayerTag, int32 Index, EFaceAngleState ViewState) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetNestedPinFromUV(EFaceAngleState State, FName LayerTag, int32 Index, EFaceAngleState FromViewState, FVector2D UV);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FVector2D GetNestedEffectivePivot(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    FFaceProfile3D GetFaceProfile() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void SetFaceProfile(const FFaceProfile3D& Profile);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|NestedArt")
    void DetectFaceProfile();

private:
    EFaceAngleState ActiveViewState = EFaceAngleState::Front;

    bool ValidatePreset() const;
    bool ValidatePreviewActor() const;
    UFaceParallaxComponent* GetParallaxComponent() const;
};