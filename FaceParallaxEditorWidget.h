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
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "FaceParallaxTypes.h"
#include "FaceParallaxEditorWidget.generated.h"

class AFaceParallaxPreviewActor;
class UFaceParallaxPreset;
class UFaceParallaxComponent;

// Pin marker painted by the canvas gizmo (Phase 3): one per nested element
// of the selected layer, plus the slot's whole-layer pin (P3, bLayerPin).
// Color coding: pinned+rotation = cyan, pinned only = amber, unpinned+jiggle
// = purple ring, unpinned plain = red ring, layer pin = white.
struct FFacePinMarker
{
    FVector2D UV = FVector2D::ZeroVector;
    bool bPinned = false;
    bool bRotation = false;
    bool bJiggle = false;
    bool bLayerPin = false;
};

// One undo-stack entry: a label plus a duplicated preset snapshot taken
// BEFORE a mutation ran (mirror of the backup-point snapshot machinery).
// Entries are transient and never serialized into the widget asset.
USTRUCT()
struct FFaceUndoEntry
{
    GENERATED_BODY()

    FFaceUndoEntry() {}
    FFaceUndoEntry(const FString& InLabel, UFaceParallaxPreset* InBackup)
        : Label(InLabel), Backup(InBackup) {}

    UPROPERTY()
    FString Label;

    UPROPERTY()
    TObjectPtr<UFaceParallaxPreset> Backup;
};

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

    // Per-axis sync (P3): propagates ONE axis of the active slot's canonical
    // into every other state's view override as a relative delta, preserving
    // the other axes' existing overrides. Axis: 0 = Position X, 1 = Position Y,
    // 2 = Scale X, 3 = Scale Y, 4 = Rotation.
    UFUNCTION(BlueprintCallable, Category = "Face Editor|Transform")
    void SyncLayerAxisToAllViews(EFaceAngleState State, FName LayerTag, int32 Axis);

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

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Outline")
    void SetOutlineDepthScope(int32 Scope);

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Outline")
    int32 GetOutlineDepthScope() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|Outline")
    bool GetOutlineDepthArmed() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    void SetRenderTarget(class UTextureRenderTarget2D* RT);

    void RefreshCanvasPreview();
    void RefreshHotspotRegions();      // per-view region outlines (Phase 2): layer transforms applied

    // ===== CYCLE PREVIEW (Phase 2) =====
    void StartCyclePreview();          // blink 2s -> expression 2s -> viseme 2s -> orbit sweep 2s
    void StopCyclePreview();

    // ===== LIVE PREVIEW (Phase 4b) =====
    // Combined preview: blink + expression + viseme + orbit run TOGETHER so
    // the assembled result can be checked in one go (cycle mode runs the same
    // four systems sequentially, one at a time).
    void StartLivePreview();
    void StopLivePreview();

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

    // ===== UNDO STACK =====
    // Every user mutation wraps its preset edits in FWidgetUndoScope
    // (FaceParallaxEditorWidgetShared.h), which pushes a pre-mutation
    // duplicate onto UndoStack. Undo/Redo restore those duplicates and
    // refresh the whole editor state.
    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    bool Undo();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    bool Redo();

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    bool CanUndo() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    bool CanRedo() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    FString GetUndoLabel() const;

    UFUNCTION(BlueprintCallable, Category = "Face Editor|UI")
    FString GetRedoLabel() const;

    // Pushes a pre-mutation preset duplicate. Called by FWidgetUndoScope.
    void PushUndoState(const FString& Desc);

    // Copy-back loop shared by RestoreSnapshot and Undo/Redo.
    bool RestoreFromBackup(UFaceParallaxPreset* Backup, const FString& Desc);

    // ===== WORKSPACE RAIL (Phase A) =====
    void SetActiveRailIndex(int32 Index);
    FLinearColor GetStateDotColor(EFaceAngleState State) const;
    void RefreshViewStripDots();
    int32 FillMissingViewsFromActiveSlot();
    void RefreshSlotPropStatus();

    // ===== PHASE B: ALIGNMENT =====
    void ToggleOnionSkin(bool bEnable);
    void SetOnionSkinOpacity(float Opacity);
    void RefreshOnionSkin();
    static EFaceAngleState GetAdjacentState(EFaceAngleState S, int32 Offset);
    static TArray<EFaceAngleState> GetLinkTargets(EFaceAngleState Active);
    static FVector2D GizmoUVToPixels(const FVector2D& UV, const FVector2D& CanvasSize);
    static FVector2D GizmoPixelsToUV(const FVector2D& Pixels, const FVector2D& CanvasSize);
    void CopyTransformFromView(EFaceAngleState Src, EFaceAngleState Dst);
    void ApplyCanonicalTransformWithLink(EFaceAngleState State, FName LayerTag, const FFaceArtTransform& T);
    FFaceArtTransform GetGizmoTransform() const;
    void SetGizmoTransform(const FFaceArtTransform& T);

    // ===== PHASE C: IMPORT =====
    void OpenImportFolderWizard(const FString& PreselectPart = FString());
    void HandleHotspotClick(const FString& RegionName);   // canvas/parts-strip pick: select mapped layer + open import wizard
    void ImportHotspotRegion(const FString& RegionName);  // Alt+click path: open the import wizard preselected on that part
    void RebuildPartsStrip();                             // 13 anatomical chips under the canvas
    void OpenHotspotRemapMenu(const FString& RegionName, const FPointerEvent& Ev); // right-click: map region -> layer
    void RemapHotspotLayer(const FString& RegionName, FName LayerTag);             // writes preset HotspotLayerMap
    FName ResolveHotspotLayer(const FString& RegionName) const; // explicit map first, then derived match
    void RefreshSyncDriftIndicator();
    double LastSyncTimestamp = 0.0;
    int32 LastSyncedViewCount = 0;

    // ===== PHASE D: DISPLAY + DEBUG =====
    void SetDisplayMode(int32 Mode);
    void RefreshDebugSliders();
    void BuildEdgeOverlay();
    void RebuildHistogramBars();
    void RefreshHullThumbnails();
    void RefreshPinControls();
    void GetLayerPinMarkers(TArray<FFacePinMarker>& Out);   // Phase 3: all nested-element markers for the gizmo
    static void BuildLumaHistogram(const TArray<float>& Luma, int32 Grid, TArray<float>& OutBins);
    static float EdgeDensity(const TArray<float>& Luma, int32 Grid, float Threshold);

    // ===== PHASE E/F: TIMELINE + PROBLEMS =====
    void RebuildVisemeGrid();
    void RebuildNestedOutliner();
    void RebuildParamTable();
    void RebuildProblemsPanel();
    static float FrameFillRatio(const TArray<bool>& Occupied);
    static int32 ClampGridCols(int32 MaxFrames);
    static void AppendSortedUnique(TArray<FString>& Out, const FString& Line);
    static bool VisemeFramesMismatch(int32 A, int32 B);
    static float PinSliderNorm(float Value, float Min, float Max);

protected:
    // Cycle Preview state machine: drives blink/expression/viseme/orbit
    // phases from widget tick while the tool tab is visible.
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    bool bCyclePreviewActive = false;
    int32 CyclePhase = -1;
    float CyclePhaseTime = 0.0f;
    // Live Preview state: all four systems concurrent (Phase 4b).
    bool bLivePreviewActive = false;
    float LivePreviewTime = 0.0f;
    float CaptureCooldown = 0.0f;      // throttle for periodic scene-capture refresh (NativeTick)

private:
    EFaceAngleState ActiveViewState = EFaceAngleState::Front;
    FName SelectedLayerName;
    bool bAutoFitOnAssign = true;
    TArray<FName> LayerNames;
    int32 GetLayerIndex(FName Tag) const;
    TArray<FName> GetUILayerTags() const;
    int32 SelectedNestedElementIndex = 0;
    bool GetSelectedPinElement(FFaceNestedArt& OutEl, int32& OutCount);
    FVector2D GetSelectedPinUV();
    void SetGizmoPinUV(const FVector2D& UV);

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
    // Assign rail (rail 5): bulk-assign grid cell dots + coverage label.
    TArray<TSharedPtr<SImage>> AssignCells;
    TSharedPtr<STextBlock> TextAssignCoverage;
    int32 PerformanceTier = 1;
    // Perf tier + camera source combos (P3): owned members so the combos'
    // OptionsSource pointers stay valid for the widget's lifetime.
    TArray<TSharedPtr<FString>> PerfTierOptions;
    TSharedPtr<FString> PerfTierSelection;
    TArray<TSharedPtr<FString>> CameraSourceOptions;
    TSharedPtr<FString> CameraSourceSelection;
    TSharedPtr<STextBlock> TextLayerName;
    TSharedPtr<SVerticalBox> LayerPanelBox;
    // Carousel state (P17/P18): dynamic row lists flip through fixed-height
    // pages; the page index is clamped by each refresh on the real page count.
    int32 LayerPageIndex = 0;
    int32 PropsPageIndex = 0;
    int32 IssuesPageIndex = 0;
    int32 CrossLayerPageIndex = 0;
    TSharedPtr<STextBlock> LayerPageLabel;
    TSharedPtr<STextBlock> PropsPageLabel;
    TSharedPtr<STextBlock> IssuesPageLabel;
    TSharedPtr<STextBlock> CrossLayerPageLabel;
    TSharedPtr<SVerticalBox> PropsPageBox;
    TArray<TSharedRef<SWidget>> PropsPages;         // right-pane carousel pages (P18)
    void ShowPropsPage(int32 Page);                 // P18: flips the right-pane page
    TSharedPtr<SVerticalBox> TimelineBox;
    TSharedPtr<SVerticalBox> VisemeGridBox;
    TSharedPtr<SVerticalBox> NestedOutlinerBox;
    TSharedPtr<SVerticalBox> ParamTableBox;
    TSharedPtr<SVerticalBox> ProblemsPanelBox;
    TSharedPtr<SSearchBox> ProblemsSearchBox;       // Phase 4: filter issues by text
    TSharedPtr<STextBlock> TextProblemsSummary;     // Phase 4: issues summary line
    FString ProblemsFilter;                         // Phase 4: active search filter
    FString ProblemsSummaryText;                    // Phase 4: accordion header summary
    FLinearColor ProblemsSummaryColor = FLinearColor(0.6f, 0.6f, 0.6f);
    void RefreshProblemsSummary();                  // Phase 4: sets Problems section header text
    float RailWidthPx = 180.0f;                     // Phase 4: rail width slider (180-360)
    TSharedPtr<SSpinBox<float>> RailWidthSpin;      // Phase 4: rail width control
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
    TSharedPtr<STextBlock> TextPinX;
    TSharedPtr<STextBlock> TextPinY;
    TSharedPtr<STextBlock> TextPinZ;
    TSharedPtr<SCheckBox> CheckPinPinned;
    TSharedPtr<SCheckBox> CheckPinRotEnabled;
    TSharedPtr<SSlider> SliderPinMinRot;
    TSharedPtr<SSlider> SliderPinMaxRot;
    TSharedPtr<SSlider> SliderPinRotSens;
    TSharedPtr<STextBlock> TextPinMinRot;
    TSharedPtr<STextBlock> TextPinMaxRot;
    TSharedPtr<STextBlock> TextPinRotSens;
    TSharedPtr<STextBlock> TextPinIndex;

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
    // Stable 20px SBox slotted into the zone panel; RebuildZoneDiagram updates
    // its content in place (SetContent) so rebuilds are never orphaned.
    TSharedPtr<SBox> ZoneDiagramWidget;
    TSharedPtr<STextBlock> ZoneYawLabel;
    TSharedPtr<STextBlock> ZonePitchLabel;
    // Camera rail's 4 zone-boundary editors (F/3Q/P/B), kept in sync by
    // ApplyZoneBoundaryDrag so diagram drags and text edits agree.
    TArray<TSharedPtr<SEditableTextBox>> ZoneEditBoxes;

    // ===== BLEND PREVIEW =====
    TSharedPtr<SSlider> BlendPreviewSlider;
    TSharedPtr<STextBlock> BlendPreviewLabel;

    // ===== STATUS MATRIX =====
    TSharedPtr<SGridPanel> StatusMatrixGrid;
    TSharedPtr<SScrollBox> StatusMatrixScroll;
    TArray<TSharedPtr<FSlateBrush>> StatusMatrixBrushes;

    // ===== VIEW OVERRIDE / SYNC UI =====
    TSharedPtr<SCheckBox> CheckViewOverrideMode;
    TArray<TSharedPtr<SCheckBox>> SyncViewCheckBoxes;   // shared strip checkboxes (pick-mode toggles, one per state)
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

    // ===== WORKSPACE RAIL (Phase A) =====
    int32 ActiveRailIndex = 0;
    TSharedPtr<SWidgetSwitcher> RailSwitcher;
    TArray<TSharedPtr<SVerticalBox>> RailContent;   // 0 Layers 1 Transform 2 Camera 3 Debug 4 Advanced 5 Assign
    TSharedPtr<SVerticalBox> SlotPropsBox;          // right pane: selected slot properties
    TSharedPtr<SBox> PreviewHost;                   // center canvas container
    TSharedPtr<SImage> OnionSkinImage;              // onion-skin ghost (Phase B)
    TSharedPtr<SCheckBox> OnionCheckBox;            // onion-skin toggle on the canvas mode row
    TSharedPtr<SBox> GizmoLayer;                    // gizmo overlay (Phase B)
    TSharedPtr<SImage> EdgeOverlayImage;            // edge-detection overlay (Phase D)
    class SFaceLayerGizmo;
    TSharedPtr<SFaceLayerGizmo> GizmoWidget;        // canvas transform gizmo (Phase B)
    class SFaceAccordion;
    TSharedPtr<SFaceAccordion> DebugAccordion;      // Debug rail: 8 collapsible sections (P16)
    TSharedPtr<SFaceAccordion> AdvancedAccordion;   // Advanced rail: 4 collapsible sections (P16)
    class SFaceDisclosure;
    TSharedPtr<SFaceDisclosure> ConfigDisclosure;   // Config checks progressive disclosure (Phase 4b)
    TSharedPtr<SFaceDisclosure> VisemeDisclosure;   // Viseme grid progressive disclosure (Phase 4b)
    class SFaceRailResizer;
    TSharedPtr<SFaceRailResizer> RailResizer;       // drag-resize handle rail/canvas (Phase 4b)
    class SFaceHotspotLayer;
    TSharedPtr<SFaceHotspotLayer> HotspotLayer;     // canvas overlay: spatial part pick (Phase 4)
    class SZoneBoundaryOverlay;
    TSharedPtr<SZoneBoundaryOverlay> ZoneDragOverlay; // zone diagram drag layer (Phase P3)
    class SFaceCarouselNav;                         // P18: prev/page/next strip under a carousel viewport
    TSharedPtr<SWrapBox> PartsStrip;                // canvas strip: 13 anatomical part chips (Phase 1)
    bool bStatePickMode = false;                    // state-strip pick mode: tab clicks toggle sync destinations
    int32 DisplayMode = 0;                          // 0 Textured 1 Depth 2 Wireframe 3 Split
    TArray<TSharedPtr<SImage>> ViewTabDots;         // per-state status dots in view strip
    TSharedPtr<STextBlock> TextSlotAlbedoStatus;    // inline import status (filename/check/warning)
    TSharedPtr<STextBlock> TextSlotNormalStatus;
    TSharedPtr<STextBlock> TextSlotDepthStatus;
    bool bLinkAcrossViews = false;                  // Phase B: edit one state, broadcast to all
    bool bOnionSkin = false;                        // Phase B
    float OnionSkinOpacity = 0.35f;                 // Phase B
    TArray<TSharedPtr<FString>> CopyFromOptions;    // Phase B: copy-from dropdown options
    TSharedPtr<FString> CopyFromSelection;          // Phase B: selected source view
    TSharedPtr<STextBlock> TextSyncDrift;           // Phase C: last-synced indicator
    bool bShowPins = false;                         // Phase 3: paint all nested-element pin markers on the gizmo
    TSharedPtr<SCheckBox> CheckShowPins;            // Phase 3: Show Pins toggle on the canvas mode row

    // ===== PHASE D: DEBUG VIEW =====
    TSharedPtr<SVerticalBox> HistogramBox;          // 16 luminance bars
    TArray<float> HistogramBins;                    // 16 normalized bins
    TSharedPtr<STextBlock> TextHistogramStats;
    TSharedPtr<SCheckBox> CheckEdgeOverlay;
    TSharedPtr<SCheckBox> CheckHistogram;
    TSharedPtr<SVerticalBox> HullThumbBox;          // 10 state thumbnails
    TArray<FSlateBrush> HullThumbBrushes;           // per-state albedo brushes
    TSharedPtr<SSlider> SliderDebugGrid;
    TSharedPtr<SSlider> SliderDebugMeshSize;
    TSharedPtr<SSlider> SliderDebugHeight;
    TSharedPtr<SSlider> SliderDebugOffset;
    TSharedPtr<STextBlock> TextDebugGrid;
    TSharedPtr<STextBlock> TextDebugMeshSize;
    TSharedPtr<STextBlock> TextDebugHeight;
    TSharedPtr<STextBlock> TextDebugOffset;
    TSharedPtr<SEditableTextBox> EditDebugLowColor;
    TSharedPtr<SEditableTextBox> EditDebugHighColor;
    bool bEdgeOverlayVisible = false;
    bool bHistogramVisible = false;
    int32 EdgeGridSize = 64;
    float EdgeThreshold = 0.18f;

    // ===== OUTLINE → DEPTH =====
    bool bOutlineOverlayVisible = false;
    // 0 = front view only, 1 = 8 horizontal states, 2 = all 10 states.
    int32 OutlineDepthScope = 0;
    // Arm/confirm guard: the destructive depth bake needs a second click.
    bool bOutlineDepthArmed = false;
    UPROPERTY(Transient)
    TObjectPtr<class UTexture2D> OutlineDepthTexture;
    UPROPERTY(Transient)
    TObjectPtr<class UTexture2D> EdgeOverlayTexture;
    // Per-state depth textures baked by the last GenerateDepthFromOutlines run
    // (kept referenced until the next run; each target view gets its own map).
    UPROPERTY(Transient)
    TArray<TObjectPtr<class UTexture2D>> OutlineDepthTextures;
    FSlateBrush OutlineDepthBrush;
    FSlateBrush OnionSkinBrush;
    FSlateBrush EdgeOverlayBrush;
    TSharedPtr<SImage> OutlinePreviewImage;
    TSharedPtr<SCheckBox> CheckOutlineOverlay;
    TSharedPtr<STextBlock> TextOutlineStats;
    TSharedPtr<SEditableTextBox> EditOutlineGridSize;
    TArray<TSharedPtr<SCheckBox>> OutlineViewChecks;
    bool GenerateDepthFromOutlinesImpl(int32 GridSize);
    class UTexture2D* BuildOutlineDepthTexture(const TArray<float>& Depth, int32 GridSize, bool bUpdatePreview);
    void RefreshOutlineViewChecks();

    // ===== DIAGNOSTIC LOG =====
    bool bShowDiagnosticLog = true;
    TSharedPtr<SBox> DiagnosticLogBox;

    // ===== CROSS-LAYER OVERLAY =====
    TSharedPtr<SVerticalBox> CrossLayerBox;
    TSharedPtr<STextBlock> TextCrossLayer;

    // ===== REENTRANCY GUARD =====
    bool bIsRefreshing = false;

public:
    // ===== SUPPRESS VALIDATION UNTIL TARGETS ARE SET =====
    bool bSuppressValidation = true;

private:
    // One-shot warning latches: validate methods log only on the first miss
    // per state change instead of spamming on every RefreshUI.
    mutable bool bActivePresetWarned = false;
    mutable bool bPreviewActorWarned = false;
    // ===== SEARCH =====
    TSharedPtr<SSearchBox> SearchBox;
    FString SearchFilter;
    TMap<TWeakPtr<SWidget>, FString> SectionSectionTitles;

    // ===== PHASE 4b: RAIL ACCESSIBILITY (chips / jump / search / resizer) =====
    struct FFaceRailSection
    {
        FString Title;
        TSharedRef<SWidget> Target;                 // header widget to scroll into view
        TSharedPtr<SFaceAccordion> Accordion;       // set for accordion sections (expand first)
        int32 AccordionIdx = -1;
        FFaceRailSection(const FString& InTitle, TSharedRef<SWidget> InTarget)
            : Title(InTitle), Target(InTarget) {}
    };
    TArray<TArray<FFaceRailSection>> RailSections;  // per-rail registry (6 rails)
    TArray<TSharedPtr<SHorizontalBox>> RailChipsRows; // per-rail chip rows (jump chips)
    int32 ActiveChipRail = -1;                      // last jumped chip highlight
    int32 ActiveChipIdx = -1;
    int32 PendingJumpRail = -1;                     // jump queued across a rail switch
    FString PendingJumpTitle;
    TSharedPtr<SBox> RailWidthBox;                  // rail SBox (live resize target)

    void RegisterRailSection(int32 RailIdx, const FString& Title, TSharedRef<SWidget> Target,
        const TSharedPtr<SFaceAccordion>& Accordion = TSharedPtr<SFaceAccordion>(), int32 AccordionIdx = -1);
    void RegisterAccordionSections(int32 RailIdx, const TSharedPtr<SFaceAccordion>& Accordion);
    void BuildRailSectionChips();
    void JumpToRailSection(int32 RailIdx, int32 SectionIdx);
    void ConsumePendingJump();
    void OnRailSearchCommitted(const FString& Query);
    void UpdateDisclosureSummaries();
    float GetRailWidthPx() const;
    void SetRailWidthLive(float W);
    void ApplyRailWidthDelta(float DeltaPx);

    // ===== PARAM REFERENCE =====
    TSharedPtr<STextBlock> TextParamRefResults;
    TSharedPtr<SEditableTextBox> EditParamRefName;

    // ===== PARAM BINDINGS TABLE =====
    TSharedPtr<SEditableTextBox> EditParamAddName;

    // ===== SNAPSHOT / UNDO =====
    // Backup point: a single manual preset duplicate ("Backup Point" button).
    UPROPERTY(Transient)
    TObjectPtr<UFaceParallaxPreset> SnapshotPresetBackup;

    // Real multi-step undo: every user mutation (FWidgetUndoScope) pushes a
    // pre-mutation preset duplicate; Undo pops and restores, Redo re-applies.
    UPROPERTY(Transient)
    TArray<FFaceUndoEntry> UndoStack;

    UPROPERTY(Transient)
    TArray<FFaceUndoEntry> RedoStack;

    // The restore path must not push undo entries of its own.
    bool bIsRestoringUndo = false;

    static constexpr int32 MaxUndoEntries = 32;
    int32 UndoSerial = 0;

    // ===== TAG VALIDATOR =====
    TSharedPtr<STextBlock> TextTagValidator;

    // ===== MATERIAL CROSS-REFERENCER =====
    TSharedPtr<STextBlock> TextMaterialCrossRef;

    // ===== INTERNAL HELPERS =====
    void RebuildZoneDiagram();
    void ApplyZoneBoundaryDrag(int32 Idx, float Multiplier);
    void CommitZoneBoundaryDrag();
    void RebuildStatusMatrix();
    void RebuildCrossLayerPanel();
    void RebuildTagValidator();
    void RebuildMaterialCrossRef();
    void ApplySearchFilter(const FString& Filter);

    // ===== ACTOR SELECTION =====
    TArray<TWeakObjectPtr<AFaceParallaxPreviewActor>> ActorOptions;
    void RefreshActorSelector();

    // ===== PANEL BUILDERS (FaceParallaxEditorWidgetPanels.cpp) =====
    // RebuildWidget's per-panel construction blocks. Each builder
    // reproduces exactly one block of the former monolithic RebuildWidget
    // so the widget tree (Phase H design contract) is unchanged.
    TSharedRef<SWidget> MakeSectionBox(const FString& Title, TSharedRef<SWidget> Content);
    void BuildPanelToolbar(const TSharedRef<SVerticalBox>& Root);
    void BuildPanelStateStrip(const TSharedRef<SVerticalBox>& Root);
    void BuildPanelZoneDiagram(const TSharedRef<SVerticalBox>& Root);
    void BuildPanelRailContainers(const TSharedRef<SVerticalBox>& Root);
    void BuildPanelLayers(const TSharedRef<SVerticalBox>& Root);
    TSharedRef<SVerticalBox> BuildPanelCanvas(const TSharedRef<SVerticalBox>& Root);
    void BuildPanelSlotProps(const TSharedRef<SVerticalBox>& Root, TSharedRef<SVerticalBox>& PropPanelOut);
    void BuildPanelTransformRail();
    void BuildPanelDebugRail();
    void BuildPanelCameraRail();
    void BuildPanelAdvancedRail();
    void BuildPanelAssignRail();
    void RefreshAssignGrid();
    void BuildPanelTimeline(const TSharedRef<SVerticalBox>& Root);
    void BuildPanelBottomBar(const TSharedRef<SVerticalBox>& Root);
};