#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
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
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void BeginDestroy() override;
    virtual void PostInitProperties() override;
    // ===== TARGETS =====
    // Transient: these are runtime bindings, never serialized into the widget
    // asset. Previously (non-transient) the asset CDO baked stale references
    // to level actors, producing "Illegal TEXT reference ... Import failed"
    // warnings and a PreviewActor that reset to None on reload.
    UPROPERTY(Transient, BlueprintReadWrite, Category = "Face Editor|Targets",
        meta = (DisplayName = "Preview Actor"))
    TWeakObjectPtr<AFaceParallaxPreviewActor> PreviewActor;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Targets")
    void SetPreviewActor(AFaceParallaxPreviewActor* NewPreviewActor);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Targets")
    AFaceParallaxPreviewActor* GetPreviewActor() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Targets")
    void SetStatus(const FString& Msg, const FLinearColor& Color);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Targets")
    void ClearStaleTargets();

    UPROPERTY(Transient, BlueprintReadWrite, Category = "Face Editor|Targets",
        meta = (DisplayName = "Active Preset"))
    TObjectPtr<UFaceParallaxPreset> ActivePreset;

    // ===== PRESET =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void ApplyPresetToPreview();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Targets",
        meta = (ToolTip="Spawn layer quads (plane meshes tagged with layer tags) on the preview actor's face parallax component."))
    int32 SpawnLayerQuadsOnPreview();

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
    void SyncTexturesLayerToAllViews(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SyncAllLayersToAllViews();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SyncLayerToSelectedViews(EFaceAngleState State, FName LayerTag,
        const TArray<EFaceAngleState>& DestViews, bool bIncludeTextures);

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

    UPROPERTY(Transient, BlueprintReadWrite, Category = "Face Editor|ViewOverride",
        meta = (DisplayName = "View Override Mode"))
    bool bViewOverrideMode = false;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    void SetViewOverrideMode(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|ViewOverride")
    bool GetViewOverrideMode() const;

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

    UPROPERTY(Transient, BlueprintReadWrite, Category = "Face Editor|Camera",
        meta = (DisplayName = "Camera Follows View"))
    bool bCameraFollowsView = true;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void SetCameraFollowsView(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    bool GetCameraFollowsView() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Camera")
    void SnapCameraToActiveView();

    // ===== IMPORT =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Import")
    TArray<class UTexture2D*> ImportTexturesFromFiles(const TArray<FString>& Files);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Import")
    bool AssignTextureToSlot(class UTexture2D* Tex, EFaceAngleState State, FName LayerTag,
        const FString& Channel);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Import")
    void OpenImportArtDialog();

    // ===== DEBUG OVERLAYS =====
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
    void SetExpressionByName(FName NewExpressionName);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FName GetExpressionByName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    bool IsNamedExpressionValid() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void SetNamedExpressionTextures(EFaceAngleState State, FName LayerTag, FName ExpressionName, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    FFaceTextureSet GetNamedExpressionTextures(EFaceAngleState State, FName LayerTag, FName ExpressionName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    bool HasNamedExpressionTextures(EFaceAngleState State, FName LayerTag, FName ExpressionName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    TArray<FName> GetAssignedNamedExpressions(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Expression")
    void ClearNamedExpressionTextures(EFaceAngleState State, FName LayerTag, FName ExpressionName);

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

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void PlayVisemeByName(FName NewVisemeName);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    FName GetVisemeByName() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    bool IsNamedVisemeValid() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    int32 GetNamedVisemeFrameCount(EFaceAngleState State, FName LayerTag, FName VisemeName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void SetNamedVisemeFrameTextures(EFaceAngleState State, FName LayerTag, FName VisemeName, int32 FrameIndex, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    FFaceTextureSet GetNamedVisemeFrameTextures(EFaceAngleState State, FName LayerTag, FName VisemeName, int32 FrameIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    TArray<FName> GetAssignedNamedVisemes(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void ClearNamedVisemeFrames(EFaceAngleState State, FName LayerTag, FName VisemeName);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Viseme")
    void ClearAllNamedVisemes(EFaceAngleState State, FName LayerTag);

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

    // ===== OUTLINE → DEPTH =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Outline")
    bool GenerateDepthFromOutlines(int32 GridSize);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Outline")
    void SetOutlineOverlayVisible(bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Outline")
    bool GetOutlineOverlayVisible() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    void SetRenderTarget(class UTextureRenderTarget2D* RT);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Editor|UI",
        meta = (DisplayName = "Preview Render Target"))
    TObjectPtr<class UTextureRenderTarget2D> RenderTargetTexture;

    // Exposed so BP can trigger UI refresh
    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    void RefreshUI();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    void SetSelectedLayer(const FString& LayerName);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    class UTexture2D* GetSelectedContentBrowserTexture();

    // ===== ZONE DIAGRAM =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    void SetBlendPreview(float Alpha);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    void ClearBlendPreview();

    // ===== STATUS MATRIX =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    TArray<EFaceAngleState> GetMissingStates() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    TArray<FName> GetMissingLayers(EFaceAngleState State) const;

    // ===== CROSS-LAYER OVERLAY =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    TArray<FString> GetAllLayerTransforms(EFaceAngleState State) const;

    // ===== PARAM REFERENCE =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Status")
    TArray<FString> FindParamUsages(FName ParamName) const;

    // ===== SNAPSHOT / UNDO =====
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void SnapshotPreset();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    void RestoreSnapshot();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Preset")
    bool HasSnapshot() const;

private:
    EFaceAngleState ActiveViewState = EFaceAngleState::Front;
    FName SelectedLayerName;
    bool bAutoFitOnAssign = true;
    TArray<FName> LayerNames;
    int32 GetLayerIndex(FName Tag) const;

    // Diagnostic overlay
    TSharedPtr<class SMultiLineEditableTextBox> DiagnosticLog;

    // Object modification callback for auto-refresh
    FDelegateHandle AssetModifiedHandle;
    void OnAssetModified(UObject* Object);

    void LogDiagnostic(const FString& Message);
    void RunDiagnostics();
    void ValidateMaterialParameters();

    bool ValidatePreset() const;
    bool ValidatePreviewActor() const;
    UFaceParallaxComponent* GetParallaxComponent() const;

    const FSlateBrush* GetPreviewBrush() const { return &PreviewBrush; }

    void RefreshLayerList();
    void RefreshTextureThumbs();
    void RefreshTimeline();
    void RefreshTransformSliders();
    void RefreshConfigCheckboxes();

    FSlateBrush PreviewBrush;
    FSlateBrush ThumbBrushA;
    FSlateBrush ThumbBrushN;
    FSlateBrush ThumbBrushD;

    TSharedPtr<SImage> PreviewImageWidget;
    TSharedPtr<SImage> ThumbAlbedo;
    TSharedPtr<SImage> ThumbNormal;
    TSharedPtr<SImage> ThumbDepth;
    TSharedPtr<STextBlock> TextStatus;
    TSharedPtr<STextBlock> TextStatusDetail;
    TSharedPtr<STextBlock> TextLayerName;
    TSharedPtr<SVerticalBox> LayerPanelBox;
    TSharedPtr<SVerticalBox> TimelineBox;
    TSharedPtr<SScrollBox> LayerScrollBox;
    TSharedPtr<SScrollBox> TimelineScrollBox;
    TSharedPtr<SComboBox<TWeakObjectPtr<AFaceParallaxPreviewActor>>> ActorSelector;
    TSharedPtr<STextBlock> TextFrameCounts;
    TSharedPtr<SEditableTextBox> EditPosX;
    TSharedPtr<SEditableTextBox> EditPosY;
    TSharedPtr<SEditableTextBox> EditScaleX;
    TSharedPtr<SEditableTextBox> EditScaleY;
    TSharedPtr<SEditableTextBox> EditRot;

    // Nested art pin sliders
    TSharedPtr<SSlider> SliderPinX;
    TSharedPtr<SSlider> SliderPinY;
    TSharedPtr<SSlider> SliderPinZ;

    // Config checkboxes
    TSharedPtr<SCheckBox> CheckBlinking;
    TSharedPtr<SCheckBox> CheckSwoosh;
    TSharedPtr<SCheckBox> CheckNestedArt;
    TSharedPtr<SCheckBox> CheckParams;
    TSharedPtr<SCheckBox> CheckShowTextures;
    TSharedPtr<SCheckBox> CheckDepthMesh;
    TSharedPtr<SCheckBox> CheckWireframe;
    TSharedPtr<SCheckBox> CheckColorByDepth;
    TSharedPtr<SVerticalBox> CfgBox;

    bool bLocalShowTextures = false;
    bool bLocalShowDepthMesh = false;
    bool bLocalShowWireframe = false;
    bool bLocalColorByDepth = false;

    // ===== ZONE DIAGRAM =====
    TSharedPtr<SWidget> ZoneDiagramWidget;
    TSharedPtr<STextBlock> ZoneYawLabel;
    TSharedPtr<STextBlock> ZonePitchLabel;

    // ===== BLEND PREVIEW =====
    TSharedPtr<SSlider> BlendPreviewSlider;
    TSharedPtr<STextBlock> BlendPreviewLabel;

    // ===== STATUS MATRIX =====
    TSharedPtr<SGridPanel> StatusMatrixGrid;
    TSharedPtr<SScrollBox> StatusMatrixScroll;
    TArray<TSharedPtr<FSlateBrush>> StatusMatrixBrushes;

    // ===== VIEW OVERRIDE / SYNC UI =====
    TSharedPtr<SCheckBox> CheckViewOverrideMode;
    TArray<TSharedPtr<SCheckBox>> SyncViewCheckBoxes;
    TSharedPtr<SHorizontalBox> SyncPickerRow;
    TSharedPtr<SCheckBox> CheckSyncTextures;
    bool bClearStateArmed = false;
    bool bClearAllArmed = false;

    // ===== CAMERA FOLLOW =====
    TSharedPtr<SCheckBox> CheckCameraFollow;
    TSharedPtr<STextBlock> TextCameraYaw;
    TSharedPtr<STextBlock> TextCameraPitch;
    TSharedPtr<STextBlock> TextCameraDist;

    // ===== PROPERTY TABS =====
    TSharedPtr<SWidgetSwitcher> PropSwitcher;
    TArray<TSharedPtr<SVerticalBox>> PropTabContent;

    // ===== OUTLINE → DEPTH =====
    bool bOutlineOverlayVisible = false;
    UPROPERTY(Transient)
    TObjectPtr<class UTexture2D> OutlineDepthTexture;
    FSlateBrush OutlineDepthBrush;
    TSharedPtr<SImage> OutlinePreviewImage;
    TSharedPtr<SCheckBox> CheckOutlineOverlay;
    TSharedPtr<STextBlock> TextOutlineStats;
    TSharedPtr<SEditableTextBox> EditOutlineGridSize;
    TArray<TSharedPtr<SCheckBox>> OutlineViewChecks;
    void BuildOutlineDepthTexture(const TArray<float>& Depth, int32 GridSize);
    void RefreshOutlineViewChecks();

    // ===== DIAGNOSTIC LOG =====
    bool bShowDiagnosticLog = true;
    TSharedPtr<SBox> DiagnosticLogBox;

    // ===== CROSS-LAYER OVERLAY =====
    TSharedPtr<SVerticalBox> CrossLayerBox;
    TSharedPtr<SScrollBox> CrossLayerScroll;
    TSharedPtr<STextBlock> TextCrossLayer;

    // ===== REENTRANCY GUARD =====
    bool bIsRefreshing = false;

public:
    // ===== SUPPRESS VALIDATION UNTIL TARGETS ARE SET =====
    bool bSuppressValidation = true;

private:
    // ===== SEARCH =====
    TSharedPtr<SSearchBox> SearchBox;
    FString SearchFilter;
    TMap<TWeakPtr<SWidget>, FString> SectionSectionTitles;

    // ===== PARAM REFERENCE =====
    TSharedPtr<STextBlock> TextParamRefResults;
    TSharedPtr<SEditableTextBox> EditParamRefName;

    // ===== SNAPSHOT / UNDO =====
    UPROPERTY(Transient)
    TObjectPtr<UFaceParallaxPreset> SnapshotPresetBackup;

    // ===== TAG VALIDATOR =====
    TSharedPtr<STextBlock> TextTagValidator;

    // ===== MATERIAL CROSS-REFERENCER =====
    TSharedPtr<STextBlock> TextMaterialCrossRef;

    // ===== INTERNAL HELPERS =====
    void RebuildZoneDiagram();
    void RebuildStatusMatrix();
    void RebuildCrossLayerPanel();
    void RebuildTagValidator();
    void RebuildMaterialCrossRef();
    void ApplySearchFilter(const FString& Filter);

    // ===== ACTOR SELECTION =====
    TArray<TWeakObjectPtr<AFaceParallaxPreviewActor>> ActorOptions;
    void RefreshActorSelector();

    TSharedPtr<SScrollBox> PropScroll;
};