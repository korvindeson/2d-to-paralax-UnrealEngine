#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FaceParallaxTypes.h"
#include "FaceParallaxEditorWidget.generated.h"

class AFaceParallaxPreviewActor;
class UFaceParallaxPreset;
class UFaceParallaxComponent;

UCLASS(BlueprintType, Blueprintable)
class FACEPARALLAX_API UFaceParallaxEditorWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // --- PREVIEW ACTOR ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Editor|Targets",
        meta = (DisplayName = "Preview Actor"))
    TObjectPtr<AFaceParallaxPreviewActor> PreviewActor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Editor|Targets",
        meta = (DisplayName = "Active Preset"))
    TObjectPtr<UFaceParallaxPreset> ActivePreset;

    // ====================================================================
    // PRESET MANAGEMENT
    // ====================================================================
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

    // ====================================================================
    // VIEW STATE
    // ====================================================================
    UFUNCTION(BlueprintCallable, Category = "Face Editor|View State")
    void SetActiveViewState(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View State")
    EFaceAngleState GetActiveViewState() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View State")
    TArray<EFaceAngleState> GetAssignedStates() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View State")
    bool HasState(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View State")
    TArray<FName> GetLayerTagsForState(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View State")
    int32 GetLayerCount() const;

    // ====================================================================
    // TRANSFORM — PER-LAYER
    // ====================================================================
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    FFaceArtTransform GetLayerCanonicalTransform(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SetLayerPosition(EFaceAngleState State, FName LayerTag, float X, float Y);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SetLayerScale(EFaceAngleState State, FName LayerTag, float X, float Y);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SetLayerRotation(EFaceAngleState State, FName LayerTag, float Degrees);

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

    // ====================================================================
    // VIEW OVERRIDES
    // ====================================================================
    UFUNCTION(BlueprintCallable, Category = "Face Editor|View Override")
    bool HasViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View Override")
    FFaceArtTransform GetViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View Override")
    void SetViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView,
        const FFaceArtTransform& Override);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View Override")
    void ClearViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View Override")
    void ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View Override")
    void ClearAllOverrides();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|View Override")
    TArray<EFaceAngleState> GetOverrideViewsForSlot(EFaceAngleState State, FName LayerTag) const;

    // ====================================================================
    // TEXTURES
    // ====================================================================
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

    // ====================================================================
    // CAMERA
    // ====================================================================
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

    // ====================================================================
    // DEBUG OVERLAYS
    // ====================================================================
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Debug Overlays")
    void ShowTextures(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Debug Overlays")
    void ShowDepthMesh(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Debug Overlays")
    void ShowWireframe(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Debug Overlays")
    void ColorByDepth(bool bEnabled);

    // ====================================================================
    // STATUS
    // ====================================================================
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    int32 GetAssignedStateCount() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    int32 GetTotalAssignedSlots() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    int32 GetActiveLayerCount() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    FString GetStatusString() const;

    // ====================================================================
    // DYNAMIC ART (eye tracking)
    // ====================================================================
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Dynamic Art")
    void SetDriveArtPositionFromYaw(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Dynamic Art")
    bool GetDriveArtPositionFromYaw() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Dynamic Art")
    void SetMaxYawArtOffset(float Offset);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Dynamic Art")
    float GetMaxYawArtOffset() const;

    // ====================================================================
    // MATERIAL PARAM NAMES
    // ====================================================================
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetAlbedoParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetNormalParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetDepthParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetAlbedoPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetNormalPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetDepthPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetArtPositionParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetArtScaleParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Material Params")
    FName GetArtRotationParamName() const;

    // ====================================================================
    // BLINK ANIMATION
    // ====================================================================
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

    // ====================================================================
    // EXPRESSION SYSTEM
    // ====================================================================
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
    FName GetExpressionBlendParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FName GetExpressionAlbedoPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FName GetExpressionNormalPrevParamName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FName GetExpressionDepthPrevParamName() const;

    // ====================================================================
    // VISEME (speech mouth shapes)
    // ====================================================================
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

    // ====================================================================
    // SWOOSH TRANSITION
    // ====================================================================
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

    // ====================================================================
    // PRESET QUERIES
    // ====================================================================
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Query")
    bool HasSlot(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Query")
    bool IsSlotFullyAssigned(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Query")
    void ClearState(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Query")
    void ClearAll();

private:
    EFaceAngleState ActiveViewState = EFaceAngleState::Front;

    bool ValidatePreset() const;
    bool ValidatePreviewActor() const;
    UFaceParallaxComponent* GetParallaxComponent() const;
};
