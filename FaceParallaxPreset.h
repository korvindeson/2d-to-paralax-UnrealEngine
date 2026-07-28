#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FaceParallaxTypes.h"
#include "FaceParallaxPreset.generated.h"

UCLASS(BlueprintType, AutoExpandCategories = ("View Assignments|Canvas"))
class FACEPARALLAX_API UFaceParallaxPreset : public UDataAsset
{
    GENERATED_BODY()

public:
    // --- TEXTURE ASSIGNMENTS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Assignments",
        meta = (DisplayName = "State → Layer → Art Slot"))
    TMap<TEnumAsByte<EFaceAngleState>, FFaceViewStateLayerSet> ViewAssignments;

    // --- CANVAS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Canvas",
        meta = (DisplayName = "Reference canvas size for auto-fit"))
    FVector2D CanvasSize = FVector2D(512.0f, 512.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Canvas")
    bool bAutoFitOnAssign = true;

    // --- SLOT ACCESS ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    FFaceArtSlot GetSlot(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    void SetSlot(EFaceAngleState State, FName LayerTag, const FFaceArtSlot& Slot);

    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    FFaceTextureSet GetTexturesForSlot(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    void SetTexturesForSlot(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures);

    // --- TRANSFORM METHODS ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    FFaceArtTransform GetEffectiveTransform(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void SetCanonicalTransform(EFaceAngleState State, FName LayerTag, const FFaceArtTransform& Transform);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void SetViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView, const FFaceArtTransform& Override);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    bool HasViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void ClearViewOverride(EFaceAngleState State, FName LayerTag, EFaceAngleState OverrideView);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void ClearAllOverrides();

    // --- AUTO-FIT ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    FFaceArtTransform ComputeAutoFitTransform(const FFaceTextureSet& Textures) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void ApplyAutoFitToSlot(EFaceAngleState State, FName LayerTag);

    // --- SYNC ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void SyncCanonicalToAllViews(EFaceAngleState State, FName LayerTag);

    // --- SWOOSH ART ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Swoosh")
    FFaceSwooshArt GetSwooshArt(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Swoosh")
    void SetSwooshArt(EFaceAngleState State, FName LayerTag, const FFaceSwooshArt& Art);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Swoosh")
    bool HasSwooshArt(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Swoosh")
    void ClearSwooshArt(EFaceAngleState State, FName LayerTag);

    // --- QUERIES ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    bool HasState(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    bool HasSlot(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    TArray<EFaceAngleState> GetAssignedStates() const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    int32 GetTotalAssignedSlots() const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    void ClearState(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    void ClearAll();
};
