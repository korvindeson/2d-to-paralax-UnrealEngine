#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "FaceParallaxTypes.h"
#include "FaceParallaxPreset.h"
#include "FaceParallaxPreviewActor.h"
#include "FaceParallaxDataModel.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDataModelChanged);

UCLASS(BlueprintType)
class UFaceParallaxDataModel : public UObject
{
    GENERATED_BODY()

public:
    void InitializeDataModel();
    void ShutdownDataModel();

    // --- Active Preset ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|Preset")
    TObjectPtr<UFaceParallaxPreset> ActivePreset;

    // --- Preview actor ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|Preview")
    TWeakObjectPtr<AFaceParallaxPreviewActor> PreviewActor;

    // --- UI Selection State ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|Selection")
    EFaceAngleState SelectedState = EFaceAngleState::Front;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|Selection")
    FName SelectedLayerTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|Selection")
    int32 SelectedExpressionIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|Selection")
    int32 SelectedVisemeIndex = 0;

    // --- Preview view ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|View")
    float PreviewFOV = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|View")
    float OrbitYaw = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|View")
    float OrbitPitch = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|View")
    float OrbitDistance = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|View")
    float GuideHeight = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|View")
    bool bShowGrid = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|View")
    bool bShowWireframe = false;

    // --- Editor options ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|Options")
    bool bAutoFitOnAssign = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Data|Options")
    bool bUseAsyncTextureLoading = true;

    // --- Delegate ---
    UPROPERTY(BlueprintAssignable, Category = "Face Data|Events")
    FOnDataModelChanged OnDataModelChanged;

    // --- Preset Operations ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Preset")
    void SetActivePreset(UFaceParallaxPreset* NewPreset);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Preset")
    UFaceParallaxPreset* GetActivePreset() const { return ActivePreset; }

    UFUNCTION(BlueprintCallable, Category = "Face Data|Preset")
    bool HasValidPreset() const { return ActivePreset != nullptr; }

    // --- Texture Assignment ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Textures")
    void SetSlotTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Textures")
    FFaceTextureSet GetSlotTextures(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Textures")
    void ClearSlotTextures(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Textures")
    void SetAltTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Textures")
    FFaceTextureSet GetAltTextures(EFaceAngleState State, FName LayerTag) const;

    // --- Expression Textures ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    void SetExpressionTextures(EFaceAngleState State, FName LayerTag, EExpression Expression, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    FFaceTextureSet GetExpressionTextures(EFaceAngleState State, FName LayerTag, EExpression Expression) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    bool HasExpressionTextures(EFaceAngleState State, FName LayerTag, EExpression Expression) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    void ClearExpressionTextures(EFaceAngleState State, FName LayerTag, EExpression Expression);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    TArray<EExpression> GetAssignedExpressions(EFaceAngleState State, FName LayerTag) const;

    // --- Named Expression Textures ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    void SetNamedExpressionTextures(EFaceAngleState State, FName LayerTag, FName ExpressionName, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    FFaceTextureSet GetNamedExpressionTextures(EFaceAngleState State, FName LayerTag, FName ExpressionName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    bool HasNamedExpressionTextures(EFaceAngleState State, FName LayerTag, FName ExpressionName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    void ClearNamedExpressionTextures(EFaceAngleState State, FName LayerTag, FName ExpressionName);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Expression")
    TArray<FName> GetAssignedNamedExpressions(EFaceAngleState State, FName LayerTag) const;

    // --- Blink Frames ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Blink")
    int32 GetBlinkFrameCount(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Blink")
    void SetBlinkFrameTextures(EFaceAngleState State, FName LayerTag, int32 FrameIndex, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Blink")
    FFaceTextureSet GetBlinkFrameTextures(EFaceAngleState State, FName LayerTag, int32 FrameIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Blink")
    void AddBlinkFrame(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Blink")
    void ClearBlinkFrames(EFaceAngleState State, FName LayerTag);

    // --- Transform ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Transform")
    void SetLayerTransform(EFaceAngleState State, FName LayerTag, const FFaceArtTransform& Transform);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Transform")
    FFaceArtTransform GetLayerTransform(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Transform")
    void ResetLayerTransform(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Transform")
    void SyncLayerToAllViews(FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Transform")
    void SyncAllLayersToAllViews();

    // --- View Overrides ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Override")
    void SetViewOverride(EFaceAngleState State, FName LayerTag, const FFaceArtTransform& Override);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Override")
    bool HasViewOverride(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Override")
    FFaceArtTransform GetViewOverride(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Override")
    void ClearViewOverride(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Override")
    void ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Override")
    void ClearAllOverrides();

    // --- Slot Management ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    bool HasSlot(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    TArray<FName> GetLayerTagsForState(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    TArray<EFaceAngleState> GetAssignedStates() const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    TArray<EFaceAngleState> GetMissingStates() const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    TArray<FName> GetMissingLayers(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    int32 GetTotalAssignedSlots() const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    void ClearState(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    void ClearAll();

    // --- Batch Operations ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Batch")
    void BatchSetTextures(const TArray<EFaceAngleState>& States, FName LayerTag, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Batch")
    void ClearAllTextures();

    // --- Viseme Operations ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    int32 GetVisemeFrameCount(EFaceAngleState State, FName LayerTag, EExpression Expression, EViseme Viseme) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    void SetVisemeFrameTextures(EFaceAngleState State, FName LayerTag, EExpression Expression, EViseme Viseme, int32 FrameIndex, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    FFaceTextureSet GetVisemeFrameTextures(EFaceAngleState State, FName LayerTag, EExpression Expression, EViseme Viseme, int32 FrameIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    TArray<EViseme> GetAssignedVisemes(EFaceAngleState State, FName LayerTag, EExpression Expression) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    void ClearVisemeFrames(EFaceAngleState State, FName LayerTag, EExpression Expression, EViseme Viseme);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    void ClearAllVisemes(EFaceAngleState State, FName LayerTag, EExpression Expression);

    // --- Named Viseme Operations ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    int32 GetNamedVisemeFrameCount(EFaceAngleState State, FName LayerTag, FName VisemeName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    void SetNamedVisemeFrameTextures(EFaceAngleState State, FName LayerTag, FName VisemeName, int32 FrameIndex, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    FFaceTextureSet GetNamedVisemeFrameTextures(EFaceAngleState State, FName LayerTag, FName VisemeName, int32 FrameIndex) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    TArray<FName> GetAssignedNamedVisemes(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    void ClearNamedVisemeFrames(EFaceAngleState State, FName LayerTag, FName VisemeName);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Viseme")
    void ClearAllNamedVisemes(EFaceAngleState State, FName LayerTag);

    // --- Param Bindings ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Params")
    TArray<FFaceParamBinding> GetParamBindings(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Data|Params")
    void SetParamBindings(EFaceAngleState State, FName LayerTag, const TArray<FFaceParamBinding>& Bindings);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Params")
    void AddParamBinding(EFaceAngleState State, FName LayerTag, const FFaceParamBinding& Binding);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Params")
    void RemoveParamBinding(EFaceAngleState State, FName LayerTag, int32 Index);

    UFUNCTION(BlueprintCallable, Category = "Face Data|Params")
    void ClearParamBindings(EFaceAngleState State, FName LayerTag);

    // --- Duplicate State ---
    UFUNCTION(BlueprintCallable, Category = "Face Data|Slot")
    void DuplicateState(EFaceAngleState SourceState, EFaceAngleState TargetState);

    // --- Texture helpers (for editor UI) ---
    void EnqueueAsyncLoadForSlot(EFaceAngleState State, FName LayerTag);

private:
    void NotifyChanged();
    void ValidatePreset();
};
