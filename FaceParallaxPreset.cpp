#include "FaceParallaxPreset.h"

FFaceArtSlot UFaceParallaxPreset::GetSlot(EFaceAngleState State, FName LayerTag) const
{
    if (LayerTag.IsNone())
    {
        return FFaceArtSlot();
    }

    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GetSlot] State %d not found in preset %s"), (int32)State, *GetNameSafe(this));
        return FFaceArtSlot();
    }

    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GetSlot] Layer '%s' not found for state %d in preset %s"), *LayerTag.ToString(), (int32)State, *GetNameSafe(this));
        return FFaceArtSlot();
    }

    return *Slot;
}

void UFaceParallaxPreset::SetSlot(EFaceAngleState State, FName LayerTag, const FFaceArtSlot& Slot)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Added = StateSet.Layers.FindOrAdd(LayerTag);
    Added = Slot;
    Added.Textures.SyncSoftRefs();
}

FFaceTextureSet UFaceParallaxPreset::GetTexturesForSlot(EFaceAngleState State, FName LayerTag) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return FFaceTextureSet();

    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return FFaceTextureSet();

    return Slot->Textures;
}

void UFaceParallaxPreset::SetTexturesForSlot(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);

    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    Slot.Textures = Textures;
    Slot.Textures.CaptureSourceSize();
    Slot.Textures.SyncSoftRefs();

    if (bAutoFitOnAssign && Slot.CanonicalTransform.IsIdentity())
    {
        Slot.CanonicalTransform = ComputeAutoFitTransform(Slot.Textures);
    }
}

FFaceArtTransform UFaceParallaxPreset::GetEffectiveTransform(EFaceAngleState State, FName LayerTag) const
{
    FFaceArtSlot Slot = GetSlot(State, LayerTag);
    return Slot.GetEffectiveTransform(State);
}

void UFaceParallaxPreset::SetCanonicalTransform(EFaceAngleState State, FName LayerTag,
    const FFaceArtTransform& Transform)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    Slot.CanonicalTransform = Transform;
}

void UFaceParallaxPreset::SetViewOverride(EFaceAngleState State, FName LayerTag,
    EFaceAngleState OverrideView, const FFaceArtTransform& Override)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    Slot.SetOverride(OverrideView, Override);
}

bool UFaceParallaxPreset::HasViewOverride(EFaceAngleState State, FName LayerTag,
    EFaceAngleState OverrideView) const
{
    FFaceArtSlot Slot = GetSlot(State, LayerTag);
    return Slot.HasOverride(OverrideView);
}

void UFaceParallaxPreset::ClearViewOverride(EFaceAngleState State, FName LayerTag,
    EFaceAngleState OverrideView)
{
    FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return;

    FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return;

    Slot->ClearOverride(OverrideView);
}

void UFaceParallaxPreset::ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag)
{
    FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return;

    FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return;

    Slot->ClearAllOverrides();
}

void UFaceParallaxPreset::ClearAllOverrides()
{
    for (auto& StatePair : ViewAssignments)
    {
        for (auto& LayerPair : StatePair.Value.Layers)
        {
            LayerPair.Value.ClearAllOverrides();
        }
    }
}

FFaceArtTransform UFaceParallaxPreset::ComputeAutoFitTransform(const FFaceTextureSet& Textures) const
{
    FFaceArtTransform Result;
    if (!Textures.Albedo || CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f)
    {
        return Result;
    }

    int32 TexW = Textures.SourceTexWidth > 0 ? Textures.SourceTexWidth : Textures.Albedo->GetSizeX();
    int32 TexH = Textures.SourceTexHeight > 0 ? Textures.SourceTexHeight : Textures.Albedo->GetSizeY();

    if (TexW <= 0 || TexH <= 0)
    {
        return Result;
    }

    float FitScaleX = CanvasSize.X / (float)TexW;
    float FitScaleY = CanvasSize.Y / (float)TexH;
    float UniformScale = FMath::Min(FitScaleX, FitScaleY);

    Result.Scale = FVector2D(UniformScale, UniformScale);
    return Result;
}

void UFaceParallaxPreset::ApplyAutoFitToSlot(EFaceAngleState State, FName LayerTag)
{
    FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return;

    FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return;

    Slot->CanonicalTransform = ComputeAutoFitTransform(Slot->Textures);
}

void UFaceParallaxPreset::SyncCanonicalToAllViews(EFaceAngleState State, FName LayerTag)
{
    FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return;

    FFaceArtSlot* SourceSlot = StateSet->Layers.Find(LayerTag);
    if (!SourceSlot) return;

    FFaceArtTransform SourceCanonical = SourceSlot->CanonicalTransform;

    for (auto& OtherStatePair : ViewAssignments)
    {
        if (OtherStatePair.Key == State) continue;

        FFaceArtSlot* OtherSlot = OtherStatePair.Value.Layers.Find(LayerTag);
        if (!OtherSlot) continue;

        // Store the source transform as a view override on each target state.
        // This preserves the target state's canonical (which may have been
        // positioned differently for perspective) but applies the source's
        // adjustment as a relative delta.
        FFaceArtTransform TargetCanonical = OtherSlot->CanonicalTransform;
        FFaceArtTransform Override;
        Override.Position = SourceCanonical.Position - TargetCanonical.Position;
        Override.Scale.X = SourceCanonical.Scale.X / FMath::Max(TargetCanonical.Scale.X, KINDA_SMALL_NUMBER);
        Override.Scale.Y = SourceCanonical.Scale.Y / FMath::Max(TargetCanonical.Scale.Y, KINDA_SMALL_NUMBER);
        Override.Rotation = SourceCanonical.Rotation - TargetCanonical.Rotation;
        OtherSlot->SetOverride(OtherStatePair.Key, Override);
    }
}

void UFaceParallaxPreset::SyncTexturesToAllViews(EFaceAngleState State, FName LayerTag)
{
    if (LayerTag.IsNone()) return;

    const FFaceViewStateLayerSet* SourceSet = ViewAssignments.Find(State);
    if (!SourceSet)
    {
        UE_LOG(LogTemp, Warning, TEXT("SyncTexturesToAllViews: source state %d not found"), (int32)State);
        return;
    }
    const FFaceArtSlot* SourceSlot = SourceSet->Layers.Find(LayerTag);
    if (!SourceSlot)
    {
        UE_LOG(LogTemp, Warning, TEXT("SyncTexturesToAllViews: layer '%s' not found for state %d"), *LayerTag.ToString(), (int32)State);
        return;
    }

    FFaceTextureSet SourceTextures = SourceSlot->Textures;
    FFaceTextureSet SourceAltTextures = SourceSlot->AltTextures;

    for (auto& StatePair : ViewAssignments)
    {
        if (StatePair.Key == State) continue;

        FFaceArtSlot* Slot = StatePair.Value.Layers.Find(LayerTag);
        if (!Slot) continue;

        Slot->Textures = SourceTextures;
        Slot->Textures.CaptureSourceSize();
        Slot->Textures.SyncSoftRefs();
        Slot->AltTextures = SourceAltTextures;
        Slot->AltTextures.SyncSoftRefs();
    }

    MarkPackageDirty();
}

bool UFaceParallaxPreset::HasState(EFaceAngleState State) const
{
    return ViewAssignments.Contains(State);
}

bool UFaceParallaxPreset::HasSlot(EFaceAngleState State, FName LayerTag) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return false;
    return StateSet->Layers.Contains(LayerTag);
}

TArray<EFaceAngleState> UFaceParallaxPreset::GetAssignedStates() const
{
    TArray<EFaceAngleState> Result;
    ViewAssignments.GetKeys(Result);
    return Result;
}

int32 UFaceParallaxPreset::GetTotalAssignedSlots() const
{
    int32 Count = 0;
    for (const auto& StatePair : ViewAssignments)
    {
        Count += StatePair.Value.Layers.Num();
    }
    return Count;
}

void UFaceParallaxPreset::ClearState(EFaceAngleState State)
{
    ViewAssignments.Remove(State);
}

// ====================================================================
// PARAMETER BINDINGS
// ====================================================================

TArray<FFaceParamBinding> UFaceParallaxPreset::GetParamBindings(EFaceAngleState State, FName LayerTag) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return TArray<FFaceParamBinding>();

    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return TArray<FFaceParamBinding>();

    return Slot->ParamBindings;
}

void UFaceParallaxPreset::SetParamBindings(EFaceAngleState State, FName LayerTag, const TArray<FFaceParamBinding>& Bindings)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    Slot.ParamBindings = Bindings;
}

FFaceTextureSet UFaceParallaxPreset::GetAltTextures(EFaceAngleState State, FName LayerTag) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return FFaceTextureSet();

    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return FFaceTextureSet();

    return Slot->AltTextures;
}

void UFaceParallaxPreset::SetAltTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    Slot.AltTextures = Textures;
}

FFaceSwooshArt UFaceParallaxPreset::GetSwooshArt(EFaceAngleState State, FName LayerTag) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return FFaceSwooshArt();

    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return FFaceSwooshArt();

    const FFaceSwooshArt* Art = Slot->SwooshToState.Find(State);
    if (!Art) return FFaceSwooshArt();

    return *Art;
}

void UFaceParallaxPreset::SetSwooshArt(EFaceAngleState State, FName LayerTag, const FFaceSwooshArt& Art)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    Slot.SwooshToState.Add(State, Art);
}

bool UFaceParallaxPreset::HasSwooshArt(EFaceAngleState State, FName LayerTag) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return false;

    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return false;

    return Slot->SwooshToState.Contains(State);
}

void UFaceParallaxPreset::ClearSwooshArt(EFaceAngleState State, FName LayerTag)
{
    FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return;

    FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return;

    Slot->SwooshToState.Remove(State);
}

void UFaceParallaxPreset::ClearAll()
{
    ViewAssignments.Empty();
}

// --- POPULATION ---

void UFaceParallaxPreset::PopulateDefaultAssignments(const TArray<FString>& LayerNames)
{
    ViewAssignments.Empty();

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::BackLeft, EFaceAngleState::LeftProfile,
        EFaceAngleState::ThreeQuarterLeft, EFaceAngleState::Back,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    for (EFaceAngleState State : AllStates)
    {
        FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
        for (const FString& LayerName : LayerNames)
        {
            FName LayerTag = FName(*LayerName);
            FFaceArtSlot Slot;
            Slot.CanonicalTransform.Position = FVector2D(0.0f, 0.0f);
            Slot.CanonicalTransform.Scale = FVector2D(1.0f, 1.0f);
            Slot.CanonicalTransform.Rotation = 0.0f;
            Slot.Textures.SyncSoftRefs();
            StateSet.Layers.Add(LayerTag, Slot);
        }
    }

    MarkPackageDirty();
}

FFaceArtSlot& UFaceParallaxPreset::GetSlotMutable(EFaceAngleState State, FName LayerTag)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    return StateSet.Layers.FindOrAdd(LayerTag);
}

// --- NESTED ELEMENTS ---

int32 UFaceParallaxPreset::GetNestedElementCount(EFaceAngleState State, FName LayerTag) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return 0;
    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return 0;
    return Slot->NestedElements.Num();
}

FFaceNestedArt UFaceParallaxPreset::GetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return FFaceNestedArt();
    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return FFaceNestedArt();
    if (Index < 0 || Index >= Slot->NestedElements.Num()) return FFaceNestedArt();
    return Slot->NestedElements[Index];
}

void UFaceParallaxPreset::SetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceNestedArt& Element)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return;
    Slot.NestedElements[Index] = Element;
}

void UFaceParallaxPreset::AddNestedElement(EFaceAngleState State, FName LayerTag, const FFaceNestedArt& Element)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    Slot.NestedElements.Add(Element);
}

void UFaceParallaxPreset::RemoveNestedElement(EFaceAngleState State, FName LayerTag, int32 Index)
{
    FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return;
    FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return;
    if (Index < 0 || Index >= Slot->NestedElements.Num()) return;
    Slot->NestedElements.RemoveAt(Index);
}

void UFaceParallaxPreset::ClearNestedElements(EFaceAngleState State, FName LayerTag)
{
    FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return;
    FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return;
    Slot->NestedElements.Empty();
}

FFacePin3D UFaceParallaxPreset::GetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return FFacePin3D();
    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return FFacePin3D();
    if (Index < 0 || Index >= Slot->NestedElements.Num()) return FFacePin3D();
    return Slot->NestedElements[Index].Pin3D;
}

void UFaceParallaxPreset::SetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index, const FFacePin3D& Pin)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    FFaceArtSlot& Slot = StateSet.Layers.FindOrAdd(LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return;
    Slot.NestedElements[Index].Pin3D = Pin;
}

// ====================================================================
// BATCH OPERATIONS
// ====================================================================

void UFaceParallaxPreset::BatchSetTextures(EFaceAngleState State, FName LayerTag, const TArray<FFaceTextureSet>& Textures)
{
    if (Textures.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("BatchSetTextures: empty array for State=%d, Layer=%s"), (int32)State, *LayerTag.ToString());
        return;
    }
    FFaceArtSlot& Slot = GetSlotMutable(State, LayerTag);
    Slot.Textures = Textures[0];
    Slot.Textures.CaptureSourceSize();
    if (bAutoFitOnAssign && Slot.CanonicalTransform.IsIdentity())
    {
        Slot.CanonicalTransform = ComputeAutoFitTransform(Slot.Textures);
    }
}

void UFaceParallaxPreset::BatchSetTexturesAllLayers(EFaceAngleState State, const TMap<FName, FFaceTextureSet>& LayerTextures)
{
    for (const auto& Pair : LayerTextures)
    {
        FFaceArtSlot& Slot = GetSlotMutable(State, Pair.Key);
        Slot.Textures = Pair.Value;
        Slot.Textures.CaptureSourceSize();
        if (bAutoFitOnAssign && Slot.CanonicalTransform.IsIdentity())
        {
            Slot.CanonicalTransform = ComputeAutoFitTransform(Slot.Textures);
        }
    }
}

void UFaceParallaxPreset::SyncLayerNestedToAllViews(FName LayerTag, FName ElementName,
    const FFaceNestedArt& Element, bool bSyncPins)
{
    FFaceViewStateLayerSet* FrontSet = ViewAssignments.Find(EFaceAngleState::Front);
    if (!FrontSet || !FrontSet->Layers.Contains(LayerTag))
    {
        UE_LOG(LogTemp, Warning, TEXT("SyncLayerNestedToAllViews: Front state has no layer '%s'"), *LayerTag.ToString());
        return;
    }

    for (auto& StatePair : ViewAssignments)
    {
        if (StatePair.Key == EFaceAngleState::Front) continue;

        FFaceArtSlot* Slot = StatePair.Value.Layers.Find(LayerTag);
        if (!Slot) continue;

        int32 FoundIndex = -1;
        for (int32 i = 0; i < Slot->NestedElements.Num(); ++i)
        {
            if (Slot->NestedElements[i].ElementName == ElementName)
            {
                FoundIndex = i;
                break;
            }
        }

        FFaceNestedArt Copied = Element;
        // Phase 5: without pin sync, each view keeps its own pin (the source
        // element's art/jiggle/pivot still propagate); new elements get a
        // fresh unpinned pin so the source pin never leaks across views.
        if (!bSyncPins)
        {
            Copied.Pin3D = (FoundIndex >= 0)
                ? Slot->NestedElements[FoundIndex].Pin3D
                : FFacePin3D();
        }

        if (FoundIndex >= 0)
        {
            Slot->NestedElements[FoundIndex] = Copied;
        }
        else
        {
            Slot->NestedElements.Add(Copied);
        }
    }
}

void UFaceParallaxPreset::ClearAllTextures()
{
    for (auto& StatePair : ViewAssignments)
    {
        for (auto& LayerPair : StatePair.Value.Layers)
        {
            LayerPair.Value.Textures = FFaceTextureSet();
        }
    }
}

TArray<FName> UFaceParallaxPreset::GetAllLayerTags(EFaceAngleState State) const
{
    TArray<FName> Result;
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (StateSet)
    {
        StateSet->Layers.GetKeys(Result);
    }
    return Result;
}

int32 UFaceParallaxPreset::GetNumViewStates() const
{
    return ViewAssignments.Num();
}

void UFaceParallaxPreset::DuplicateState(EFaceAngleState SourceState, EFaceAngleState DestState)
{
    const FFaceViewStateLayerSet* SourceSet = ViewAssignments.Find(SourceState);
    if (!SourceSet)
    {
        UE_LOG(LogTemp, Warning, TEXT("DuplicateState: source state %d not found"), (int32)SourceState);
        return;
    }
    ViewAssignments.Add(DestState, *SourceSet);
}

void UFaceParallaxPreset::SetNestedAltTextures(EFaceAngleState State, FName LayerTag, int32 NestedIndex, const TArray<UTexture2D*>& AltTextures)
{
    FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return;
    FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return;
    if (NestedIndex < 0 || NestedIndex >= Slot->NestedElements.Num()) return;

    FFaceNestedArt& Nested = Slot->NestedElements[NestedIndex];
    if (AltTextures.Num() > 0) Nested.AltTextures.Albedo = AltTextures[0];
    if (AltTextures.Num() > 1) Nested.AltTextures.Normal = AltTextures[1];
    if (AltTextures.Num() > 2) Nested.AltTextures.Depth = AltTextures[2];
}
