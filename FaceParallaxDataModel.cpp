#include "FaceParallaxDataModel.h"
#include "FaceParallaxComponent.h"

void UFaceParallaxDataModel::InitializeDataModel()
{
}

void UFaceParallaxDataModel::ShutdownDataModel()
{
    ActivePreset = nullptr;
    PreviewActor = nullptr;
}

void UFaceParallaxDataModel::SetActivePreset(UFaceParallaxPreset* NewPreset)
{
    ActivePreset = NewPreset;
    if (ActivePreset)
    {
        bAutoFitOnAssign = ActivePreset->bAutoFitOnAssign;
    }
    NotifyChanged();
}

// =============================================================
// TEXTURES
// =============================================================

void UFaceParallaxDataModel::SetSlotTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.Textures = Textures;
    Slot.Textures.CaptureSourceSize();
    Slot.Textures.SyncSoftRefs();
    if (bAutoFitOnAssign && Slot.CanonicalTransform.IsIdentity())
    {
        Slot.CanonicalTransform = ActivePreset->ComputeAutoFitTransform(Slot.Textures);
    }
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

FFaceTextureSet UFaceParallaxDataModel::GetSlotTextures(EFaceAngleState State, FName LayerTag) const
{
    if (!ActivePreset) return FFaceTextureSet();
    return ActivePreset->GetSlot(State, LayerTag).Textures;
}

void UFaceParallaxDataModel::ClearSlotTextures(EFaceAngleState State, FName LayerTag)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.Textures = FFaceTextureSet();
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::SetAltTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.AltTextures = Textures;
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

FFaceTextureSet UFaceParallaxDataModel::GetAltTextures(EFaceAngleState State, FName LayerTag) const
{
    if (!ActivePreset) return FFaceTextureSet();
    return ActivePreset->GetSlot(State, LayerTag).AltTextures;
}

// =============================================================
// EXPRESSION TEXTURES
// =============================================================

void UFaceParallaxDataModel::SetExpressionTextures(EFaceAngleState State, FName LayerTag,
    EExpression Expression, const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.ExpressionTextures.Add(Expression, Textures);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

FFaceTextureSet UFaceParallaxDataModel::GetExpressionTextures(EFaceAngleState State,
    FName LayerTag, EExpression Expression) const
{
    if (!ActivePreset) return FFaceTextureSet();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceTextureSet* Found = Slot.ExpressionTextures.Find(Expression);
    return Found ? *Found : FFaceTextureSet();
}

bool UFaceParallaxDataModel::HasExpressionTextures(EFaceAngleState State,
    FName LayerTag, EExpression Expression) const
{
    if (!ActivePreset) return false;
    return ActivePreset->GetSlot(State, LayerTag).ExpressionTextures.Contains(Expression);
}

void UFaceParallaxDataModel::ClearExpressionTextures(EFaceAngleState State,
    FName LayerTag, EExpression Expression)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.ExpressionTextures.Remove(Expression);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

TArray<EExpression> UFaceParallaxDataModel::GetAssignedExpressions(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ActivePreset) return TArray<EExpression>();
    TArray<EExpression> Result;
    ActivePreset->GetSlot(State, LayerTag).ExpressionTextures.GetKeys(Result);
    return Result;
}

// =============================================================
// NAMED EXPRESSION TEXTURES
// =============================================================

void UFaceParallaxDataModel::SetNamedExpressionTextures(EFaceAngleState State, FName LayerTag,
    FName ExpressionName, const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.NamedExpressionTextures.Add(ExpressionName, Textures);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

FFaceTextureSet UFaceParallaxDataModel::GetNamedExpressionTextures(EFaceAngleState State,
    FName LayerTag, FName ExpressionName) const
{
    if (!ActivePreset) return FFaceTextureSet();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceTextureSet* Found = Slot.NamedExpressionTextures.Find(ExpressionName);
    return Found ? *Found : FFaceTextureSet();
}

bool UFaceParallaxDataModel::HasNamedExpressionTextures(EFaceAngleState State,
    FName LayerTag, FName ExpressionName) const
{
    if (!ActivePreset) return false;
    return ActivePreset->GetSlot(State, LayerTag).NamedExpressionTextures.Contains(ExpressionName);
}

void UFaceParallaxDataModel::ClearNamedExpressionTextures(EFaceAngleState State,
    FName LayerTag, FName ExpressionName)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.NamedExpressionTextures.Remove(ExpressionName);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

TArray<FName> UFaceParallaxDataModel::GetAssignedNamedExpressions(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ActivePreset) return TArray<FName>();
    TArray<FName> Result;
    ActivePreset->GetSlot(State, LayerTag).NamedExpressionTextures.GetKeys(Result);
    return Result;
}

// =============================================================
// BLINK FRAMES
// =============================================================

int32 UFaceParallaxDataModel::GetBlinkFrameCount(EFaceAngleState State, FName LayerTag) const
{
    if (!ActivePreset) return 0;
    return ActivePreset->GetSlot(State, LayerTag).BlinkFrames.Num();
}

void UFaceParallaxDataModel::SetBlinkFrameTextures(EFaceAngleState State, FName LayerTag,
    int32 FrameIndex, const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    if (FrameIndex >= 0 && FrameIndex < Slot.BlinkFrames.Num())
    {
        Slot.BlinkFrames[FrameIndex] = Textures;
    }
    else if (FrameIndex >= 0 && FrameIndex == Slot.BlinkFrames.Num())
    {
        Slot.BlinkFrames.Add(Textures);
    }
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

FFaceTextureSet UFaceParallaxDataModel::GetBlinkFrameTextures(EFaceAngleState State,
    FName LayerTag, int32 FrameIndex) const
{
    if (!ActivePreset) return FFaceTextureSet();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    if (FrameIndex >= 0 && FrameIndex < Slot.BlinkFrames.Num())
    {
        return Slot.BlinkFrames[FrameIndex];
    }
    return FFaceTextureSet();
}

void UFaceParallaxDataModel::AddBlinkFrame(EFaceAngleState State, FName LayerTag,
    const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.BlinkFrames.Add(Textures);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::ClearBlinkFrames(EFaceAngleState State, FName LayerTag)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.BlinkFrames.Empty();
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

// =============================================================
// TRANSFORM
// =============================================================

void UFaceParallaxDataModel::SetLayerTransform(EFaceAngleState State, FName LayerTag,
    const FFaceArtTransform& Transform)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.CanonicalTransform = Transform;
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

FFaceArtTransform UFaceParallaxDataModel::GetLayerTransform(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ActivePreset) return FFaceArtTransform();
    return ActivePreset->GetSlot(State, LayerTag).CanonicalTransform;
}

void UFaceParallaxDataModel::ResetLayerTransform(EFaceAngleState State, FName LayerTag)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.CanonicalTransform = FFaceArtTransform();
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::SyncLayerToAllViews(FName LayerTag)
{
    ValidatePreset();
    if (!ActivePreset) return;

    FFaceArtTransform RefTransform = GetLayerTransform(EFaceAngleState::Front, LayerTag);

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::BackLeft, EFaceAngleState::LeftProfile,
        EFaceAngleState::ThreeQuarterLeft, EFaceAngleState::Back,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    for (EFaceAngleState State : AllStates)
    {
        if (ActivePreset->HasSlot(State, LayerTag))
        {
            SetLayerTransform(State, LayerTag, RefTransform);
        }
    }
}

void UFaceParallaxDataModel::SyncAllLayersToAllViews()
{
    ValidatePreset();
    if (!ActivePreset) return;
    if (!ActivePreset->HasState(EFaceAngleState::Front)) return;

    TArray<FName> LayerTags = GetLayerTagsForState(EFaceAngleState::Front);
    for (FName Tag : LayerTags)
    {
        SyncLayerToAllViews(Tag);
    }
}

// =============================================================
// VIEW OVERRIDES
// =============================================================

void UFaceParallaxDataModel::SetViewOverride(EFaceAngleState State, FName LayerTag,
    const FFaceArtTransform& Override)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.ViewOverrides.Add(State, Override);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

bool UFaceParallaxDataModel::HasViewOverride(EFaceAngleState State, FName LayerTag) const
{
    if (!ActivePreset) return false;
    return ActivePreset->GetSlot(State, LayerTag).HasOverride(State);
}

FFaceArtTransform UFaceParallaxDataModel::GetViewOverride(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ActivePreset) return FFaceArtTransform();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceArtTransform* Found = Slot.ViewOverrides.Find(State);
    return Found ? *Found : FFaceArtTransform();
}

void UFaceParallaxDataModel::ClearViewOverride(EFaceAngleState State, FName LayerTag)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.ViewOverrides.Remove(State);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.ViewOverrides.Empty();
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::ClearAllOverrides()
{
    ValidatePreset();
    if (!ActivePreset) return;

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::BackLeft, EFaceAngleState::LeftProfile,
        EFaceAngleState::ThreeQuarterLeft, EFaceAngleState::Back,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    for (EFaceAngleState State : AllStates)
    {
        if (!ActivePreset->HasState(State)) continue;
        TArray<FName> LayerTags = GetLayerTagsForState(State);
        for (FName Tag : LayerTags)
        {
            FFaceArtSlot Slot = ActivePreset->GetSlot(State, Tag);
            Slot.ViewOverrides.Empty();
            ActivePreset->SetSlot(State, Tag, Slot);
        }
    }
    NotifyChanged();
}

// =============================================================
// SLOT MANAGEMENT
// =============================================================

bool UFaceParallaxDataModel::HasSlot(EFaceAngleState State, FName LayerTag) const
{
    if (!ActivePreset) return false;
    return ActivePreset->HasSlot(State, LayerTag);
}

TArray<FName> UFaceParallaxDataModel::GetLayerTagsForState(EFaceAngleState State) const
{
    if (!ActivePreset) return TArray<FName>();
    return ActivePreset->GetAllLayerTags(State);
}

TArray<EFaceAngleState> UFaceParallaxDataModel::GetAssignedStates() const
{
    if (!ActivePreset) return TArray<EFaceAngleState>();
    return ActivePreset->GetAssignedStates();
}

TArray<EFaceAngleState> UFaceParallaxDataModel::GetMissingStates() const
{
    if (!ActivePreset) return TArray<EFaceAngleState>();

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::BackLeft, EFaceAngleState::LeftProfile,
        EFaceAngleState::ThreeQuarterLeft, EFaceAngleState::Back,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    TArray<EFaceAngleState> Missing;
    TArray<EFaceAngleState> Assigned = GetAssignedStates();
    for (EFaceAngleState S : AllStates)
    {
        if (!Assigned.Contains(S))
            Missing.Add(S);
    }
    return Missing;
}

TArray<FName> UFaceParallaxDataModel::GetMissingLayers(EFaceAngleState State) const
{
    if (!ActivePreset) return TArray<FName>();

    if (!ActivePreset->HasState(State))
    {
        TArray<FName> Empty;
        return Empty;
    }

    TArray<FName> FrontTags = ActivePreset->GetAllLayerTags(EFaceAngleState::Front);
    TArray<FName> StateTags = ActivePreset->GetAllLayerTags(State);

    TArray<FName> Missing;
    for (const FName& Tag : FrontTags)
    {
        if (!StateTags.Contains(Tag))
            Missing.Add(Tag);
    }
    return Missing;
}

int32 UFaceParallaxDataModel::GetTotalAssignedSlots() const
{
    if (!ActivePreset) return 0;
    return ActivePreset->GetTotalAssignedSlots();
}

void UFaceParallaxDataModel::ClearState(EFaceAngleState State)
{
    ValidatePreset();
    if (!ActivePreset) return;
    ActivePreset->ClearState(State);
    NotifyChanged();
}

void UFaceParallaxDataModel::ClearAll()
{
    ValidatePreset();
    if (!ActivePreset) return;
    ActivePreset->ClearAll();
    NotifyChanged();
}

// =============================================================
// BATCH OPERATIONS
// =============================================================

void UFaceParallaxDataModel::BatchSetTextures(const TArray<EFaceAngleState>& States,
    FName LayerTag, const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    for (EFaceAngleState State : States)
    {
        SetSlotTextures(State, LayerTag, Textures);
    }
    NotifyChanged();
}

void UFaceParallaxDataModel::ClearAllTextures()
{
    ValidatePreset();
    if (!ActivePreset) return;

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::BackLeft, EFaceAngleState::LeftProfile,
        EFaceAngleState::ThreeQuarterLeft, EFaceAngleState::Back,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    for (EFaceAngleState State : AllStates)
    {
        if (!ActivePreset->HasState(State)) continue;
        TArray<FName> LayerTags = GetLayerTagsForState(State);
        for (FName Tag : LayerTags)
        {
            ClearSlotTextures(State, Tag);
        }
    }
    NotifyChanged();
}

// =============================================================
// VISEME
// =============================================================

int32 UFaceParallaxDataModel::GetVisemeFrameCount(EFaceAngleState State, FName LayerTag,
    EExpression Expression, EViseme Viseme) const
{
    if (!ActivePreset) return 0;
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceExpressionVisemeMap* ExprVisemes = Slot.VisemeFrameSets.Find(Expression);
    if (ExprVisemes)
    {
        const FFaceVisemeFrameArray* Frames = ExprVisemes->Visemes.Find(Viseme);
        if (Frames) return Frames->Frames.Num();
    }
    return 0;
}

void UFaceParallaxDataModel::SetVisemeFrameTextures(EFaceAngleState State, FName LayerTag,
    EExpression Expression, EViseme Viseme, int32 FrameIndex, const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    FFaceExpressionVisemeMap& ExprVisemes = Slot.VisemeFrameSets.FindOrAdd(Expression);
    FFaceVisemeFrameArray& Frames = ExprVisemes.Visemes.FindOrAdd(Viseme);
    if (FrameIndex >= 0 && FrameIndex < Frames.Frames.Num())
    {
        Frames.Frames[FrameIndex] = Textures;
    }
    else if (FrameIndex >= 0 && FrameIndex == Frames.Frames.Num())
    {
        Frames.Frames.Add(Textures);
    }
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

FFaceTextureSet UFaceParallaxDataModel::GetVisemeFrameTextures(EFaceAngleState State,
    FName LayerTag, EExpression Expression, EViseme Viseme, int32 FrameIndex) const
{
    if (!ActivePreset) return FFaceTextureSet();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceExpressionVisemeMap* ExprVisemes = Slot.VisemeFrameSets.Find(Expression);
    if (ExprVisemes)
    {
        const FFaceVisemeFrameArray* Frames = ExprVisemes->Visemes.Find(Viseme);
        if (Frames && FrameIndex >= 0 && FrameIndex < Frames->Frames.Num())
        {
            return Frames->Frames[FrameIndex];
        }
    }
    return FFaceTextureSet();
}

TArray<EViseme> UFaceParallaxDataModel::GetAssignedVisemes(EFaceAngleState State,
    FName LayerTag, EExpression Expression) const
{
    if (!ActivePreset) return TArray<EViseme>();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceExpressionVisemeMap* ExprVisemes = Slot.VisemeFrameSets.Find(Expression);
    if (ExprVisemes)
    {
        TArray<EViseme> Result;
        ExprVisemes->Visemes.GetKeys(Result);
        return Result;
    }
    return TArray<EViseme>();
}

void UFaceParallaxDataModel::ClearVisemeFrames(EFaceAngleState State, FName LayerTag,
    EExpression Expression, EViseme Viseme)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    FFaceExpressionVisemeMap* ExprVisemes = Slot.VisemeFrameSets.Find(Expression);
    if (ExprVisemes)
    {
        ExprVisemes->Visemes.Remove(Viseme);
    }
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::ClearAllVisemes(EFaceAngleState State, FName LayerTag,
    EExpression Expression)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.VisemeFrameSets.Remove(Expression);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

// =============================================================
// NAMED VISEME
// =============================================================

int32 UFaceParallaxDataModel::GetNamedVisemeFrameCount(EFaceAngleState State,
    FName LayerTag, FName VisemeName) const
{
    if (!ActivePreset) return 0;
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceVisemeFrameArray* Found = Slot.NamedVisemeFrames.Find(VisemeName);
    return Found ? Found->Frames.Num() : 0;
}

void UFaceParallaxDataModel::SetNamedVisemeFrameTextures(EFaceAngleState State, FName LayerTag,
    FName VisemeName, int32 FrameIndex, const FFaceTextureSet& Textures)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    FFaceVisemeFrameArray& Frames = Slot.NamedVisemeFrames.FindOrAdd(VisemeName);
    if (FrameIndex >= 0 && FrameIndex < Frames.Frames.Num())
    {
        Frames.Frames[FrameIndex] = Textures;
    }
    else if (FrameIndex >= 0 && FrameIndex == Frames.Frames.Num())
    {
        Frames.Frames.Add(Textures);
    }
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

FFaceTextureSet UFaceParallaxDataModel::GetNamedVisemeFrameTextures(EFaceAngleState State,
    FName LayerTag, FName VisemeName, int32 FrameIndex) const
{
    if (!ActivePreset) return FFaceTextureSet();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceVisemeFrameArray* Found = Slot.NamedVisemeFrames.Find(VisemeName);
    if (Found && FrameIndex >= 0 && FrameIndex < Found->Frames.Num())
    {
        return Found->Frames[FrameIndex];
    }
    return FFaceTextureSet();
}

TArray<FName> UFaceParallaxDataModel::GetAssignedNamedVisemes(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ActivePreset) return TArray<FName>();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    TArray<FName> Result;
    Slot.NamedVisemeFrames.GetKeys(Result);
    return Result;
}

void UFaceParallaxDataModel::ClearNamedVisemeFrames(EFaceAngleState State, FName LayerTag,
    FName VisemeName)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.NamedVisemeFrames.Remove(VisemeName);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::ClearAllNamedVisemes(EFaceAngleState State, FName LayerTag)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.NamedVisemeFrames.Empty();
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

// =============================================================
// PARAM BINDINGS
// =============================================================

TArray<FFaceParamBinding> UFaceParallaxDataModel::GetParamBindings(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ActivePreset) return TArray<FFaceParamBinding>();
    return ActivePreset->GetSlot(State, LayerTag).ParamBindings;
}

void UFaceParallaxDataModel::SetParamBindings(EFaceAngleState State, FName LayerTag,
    const TArray<FFaceParamBinding>& Bindings)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.ParamBindings = Bindings;
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::AddParamBinding(EFaceAngleState State, FName LayerTag,
    const FFaceParamBinding& Binding)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.ParamBindings.Add(Binding);
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

void UFaceParallaxDataModel::RemoveParamBinding(EFaceAngleState State, FName LayerTag,
    int32 Index)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    if (Index >= 0 && Index < Slot.ParamBindings.Num())
    {
        Slot.ParamBindings.RemoveAt(Index);
        ActivePreset->SetSlot(State, LayerTag, Slot);
        NotifyChanged();
    }
}

void UFaceParallaxDataModel::ClearParamBindings(EFaceAngleState State, FName LayerTag)
{
    ValidatePreset();
    if (!ActivePreset) return;
    FFaceArtSlot Slot = ActivePreset->GetSlot(State, LayerTag);
    Slot.ParamBindings.Empty();
    ActivePreset->SetSlot(State, LayerTag, Slot);
    NotifyChanged();
}

// =============================================================
// DUPLICATE STATE
// =============================================================

void UFaceParallaxDataModel::DuplicateState(EFaceAngleState SourceState, EFaceAngleState TargetState)
{
    ValidatePreset();
    if (!ActivePreset) return;
    ActivePreset->DuplicateState(SourceState, TargetState);
    NotifyChanged();
}

// =============================================================
// TEXTURE LOADING
// =============================================================

void UFaceParallaxDataModel::EnqueueAsyncLoadForSlot(EFaceAngleState State, FName LayerTag)
{
    UFaceParallaxComponent* Comp = PreviewActor.IsValid() ? PreviewActor->FindComponentByClass<UFaceParallaxComponent>() : nullptr;
    if (Comp && ActivePreset)
    {
        Comp->AsyncLoadSlotTextures(State, LayerTag);
    }
}

// =============================================================
// INTERNAL
// =============================================================

void UFaceParallaxDataModel::NotifyChanged()
{
    OnDataModelChanged.Broadcast();
}

void UFaceParallaxDataModel::ValidatePreset()
{
    if (!ActivePreset)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxDataModel] No ActivePreset set."));
    }
}
