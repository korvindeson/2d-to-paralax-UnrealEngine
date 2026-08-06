#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FaceParallaxTypes.h"
#include "FaceParallaxVectorArt.h"
#include "FaceParallaxPreset.generated.h"

UCLASS(BlueprintType, AutoExpandCategories = ("View Assignments|Canvas"))
class FACEPARALLAX_API UFaceParallaxPreset : public UDataAsset
{
    GENERATED_BODY()

public:
    // --- TEXTURE ASSIGNMENTS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Assignments",
        meta = (DisplayName = "State → Layer → Art Slot"))
    TMap<EFaceAngleState, FFaceViewStateLayerSet> ViewAssignments;

    // --- CANVAS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Canvas",
        meta = (DisplayName = "Reference canvas size for auto-fit"))
    FVector2D CanvasSize = FVector2D(512.0f, 512.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Canvas")
    bool bAutoFitOnAssign = true;

    // --- HOTSPOT → LAYER MAPPING ---
    // Explicit overrides for spatial part pick routing: anatomical region
    // name (e.g. "Nose", "CheekL") -> primary layer tag. Empty entries fall
    // back to FPLayout::FPHotspotLayerMatch derivation (exact, plural,
    // L/R collapse, prefix). Set by the parts strip's right-click remap menu.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "View Assignments",
        meta = (DisplayName = "Hotspot Region → Layer Map"))
    TMap<FString, FName> HotspotLayerMap;

    // --- VECTOR ART ---
    // Deploy-managed per-feature vector library assets (17 features, cells
    // keyed by full guide token). Read-only at authoring time; user art lands
    // in VectorArtOverrides.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art",
        meta = (DisplayName = "Vector Art Library (feature → asset)"))
    TMap<FName, TSoftObjectPtr<UFaceVectorArt>> LibraryVectorArt;

    // Per-token override assets: cell key -> asset holding that token's art.
    // Resolution is override-wins-else-library. Empty the map entry to
    // restore the library default (the asset itself is never deleted).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art",
        meta = (DisplayName = "Per-Token Vector Art Overrides"))
    TMap<FString, TSoftObjectPtr<UFaceVectorArt>> VectorArtOverrides;

    // Master switch for the editor's vector-art viewer mode.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art")
    bool bUseVectorArtViewer = true;

    // Master switch for the RUNTIME albedo bakes: when on, the 3D preview
    // quads render the per-tag composite of the vector-art cells for the
    // current view state (baked at runtime through FPSvg::RasterizeDocument)
    // instead of the raster slot textures. The 2D canvas viewer stays
    // independent (bUseVectorArtViewer).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art")
    bool bUseVectorArtAlbedo = false;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    bool HasVectorArtForFeature(FName Feature) const;

    // Deploy-facing library wiring: set/replace one feature's library asset
    // (null removes the entry). Authoring-time override edits are untouched.
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    void SetLibraryVectorArt(FName FeatureToken, UFaceVectorArt* Asset);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    void ClearLibraryVectorArt();

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    bool HasVectorOverride(const FString& CellKey) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    void SetVectorOverride(const FString& CellKey, UFaceVectorArt* Asset);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    void ClearVectorOverride(const FString& CellKey);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    void ClearFeatureOverrides(FName Feature);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    void ClearAllVectorOverrides();

    // Resolve the art for a full cell key (override wins, else library).
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    FFaceVectorArtPaths ResolveVectorCell(const FString& CellKey) const;

    // Resolve by feature + state index + pitch band (P00/P45/Pn45).
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Vector Art")
    FFaceVectorArtPaths ResolveVectorArtForCell(FName Feature, int32 StateIndex, int32 PitchBand) const;

    // --- SLOT ACCESS ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    FFaceArtSlot GetSlot(EFaceAngleState State, FName LayerTag) const;

    FFaceArtSlot& GetSlotMutable(EFaceAngleState State, FName LayerTag);

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

    // Per-axis sync (P3): propagates ONE axis of the source canonical into each
    // other state's view override as a relative delta (Position diff, Scale
    // ratio, Rotation diff); existing override components on other axes are
    // preserved. Axis: 0 = Position X, 1 = Position Y, 2 = Scale X,
    // 3 = Scale Y, 4 = Rotation.
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void SyncCanonicalAxisToAllViews(EFaceAngleState State, FName LayerTag, int32 Axis);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Transform")
    void SyncTexturesToAllViews(EFaceAngleState State, FName LayerTag);

    // --- PARAMETER BINDINGS ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Parameter")
    TArray<FFaceParamBinding> GetParamBindings(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Parameter")
    void SetParamBindings(EFaceAngleState State, FName LayerTag, const TArray<FFaceParamBinding>& Bindings);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Parameter")
    FFaceTextureSet GetAltTextures(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Parameter")
    void SetAltTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures);

    // --- SWOOSH ART ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Swoosh")
    FFaceSwooshArt GetSwooshArt(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Swoosh")
    void SetSwooshArt(EFaceAngleState State, FName LayerTag, const FFaceSwooshArt& Art);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Swoosh")
    bool HasSwooshArt(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Swoosh")
    void ClearSwooshArt(EFaceAngleState State, FName LayerTag);

    // --- NESTED ELEMENTS ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Nested")
    int32 GetNestedElementCount(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Nested")
    FFaceNestedArt GetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Nested")
    void SetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceNestedArt& Element);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Nested")
    void AddNestedElement(EFaceAngleState State, FName LayerTag, const FFaceNestedArt& Element);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Nested")
    void RemoveNestedElement(EFaceAngleState State, FName LayerTag, int32 Index);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Nested")
    void ClearNestedElements(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Nested")
    FFacePin3D GetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Nested")
    void SetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index, const FFacePin3D& Pin);

    // --- POPULATION ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset")
    void PopulateDefaultAssignments(const TArray<FString>& LayerNames);

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

    // --- BATCH OPERATIONS ---
    UFUNCTION(BlueprintCallable, Category = "Face Preset|Batch")
    void BatchSetTextures(EFaceAngleState State, FName LayerTag, const TArray<FFaceTextureSet>& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Batch")
    void BatchSetTexturesAllLayers(EFaceAngleState State, const TMap<FName, FFaceTextureSet>& LayerTextures);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Batch")
    void SyncLayerNestedToAllViews(FName LayerTag, FName ElementName, const FFaceNestedArt& Element,
        bool bSyncPins = true);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Batch")
    void ClearAllTextures();

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Batch")
    TArray<FName> GetAllLayerTags(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Batch")
    int32 GetNumViewStates() const;

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Batch")
    void DuplicateState(EFaceAngleState SourceState, EFaceAngleState DestState);

    UFUNCTION(BlueprintCallable, Category = "Face Preset|Batch")
    void SetNestedAltTextures(EFaceAngleState State, FName LayerTag, int32 NestedIndex, const TArray<UTexture2D*>& AltTextures);
};
