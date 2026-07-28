#include "FaceParallaxPreset.h"

FFaceArtSlot UFaceParallaxPreset::GetSlot(EFaceAngleState State, FName LayerTag) const
{
    const FFaceViewStateLayerSet* StateSet = ViewAssignments.Find(State);
    if (!StateSet) return FFaceArtSlot();

    const FFaceArtSlot* Slot = StateSet->Layers.Find(LayerTag);
    if (!Slot) return FFaceArtSlot();

    return *Slot;
}

void UFaceParallaxPreset::SetSlot(EFaceAngleState State, FName LayerTag, const FFaceArtSlot& Slot)
{
    FFaceViewStateLayerSet& StateSet = ViewAssignments.FindOrAdd(State);
    StateSet.Layers.Add(LayerTag, Slot);
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

    FFaceArtTransform Canonical = SourceSlot->CanonicalTransform;

    for (auto& OtherStatePair : ViewAssignments)
    {
        if (OtherStatePair.Key == State) continue;

        FFaceArtSlot* OtherSlot = OtherStatePair.Value.Layers.Find(LayerTag);
        if (!OtherSlot) continue;

        OtherSlot->CanonicalTransform = Canonical;
    }
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
