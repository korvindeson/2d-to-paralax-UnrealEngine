#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxEditorWidgetShared.h"
#include "FaceParallaxPreviewActor.h"
#include "FaceParallaxComponent.h"
#include "FaceParallaxPreset.h"
#include "DepthDebugVisualizerComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Engine/EngineTypes.h"
#include "EngineUtils.h"
#include <functional>

#if WITH_EDITOR
#include "UObject/SavePackage.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SGridPanel.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "AssetToolsModule.h"
#include "Misc/FileHelper.h"
#include "Editor.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "UObject/ObjectSaveContext.h"
#include "Rendering/DrawElements.h"


bool UFaceParallaxEditorWidget::ValidatePreset() const
{
    if (!ActivePreset)
    {
        if (!IsTemplate() && !HasAnyFlags(RF_Transient) && GEditor && !bSuppressValidation && !bActivePresetWarned)
        {
            bActivePresetWarned = true;
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No ActivePreset assigned."));
        }
        return false;
    }
    bActivePresetWarned = false;
    return true;
}

bool UFaceParallaxEditorWidget::ValidatePreviewActor() const
{
    if (!PreviewActor.IsValid())
    {
        if (!IsTemplate() && !HasAnyFlags(RF_Transient) && GEditor && !bSuppressValidation && !bPreviewActorWarned)
        {
            bPreviewActorWarned = true;
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No PreviewActor assigned."));
        }
        return false;
    }
    bPreviewActorWarned = false;
    return true;
}

UFaceParallaxComponent* UFaceParallaxEditorWidget::GetParallaxComponent() const
{
    return ValidatePreviewActor() ? PreviewActor->FaceParallax : nullptr;
}

void UFaceParallaxEditorWidget::SetPreviewActor(AFaceParallaxPreviewActor* NewPreviewActor)
{
    PreviewActor = NewPreviewActor;
    if (PreviewActor.IsValid())
    {
        // Ensure the scene capture has a render target and the canvas shows it
        // (the render-target wiring was previously missing entirely, leaving
        // the layer screen white).
        if (UTextureRenderTarget2D* RT = PreviewActor->CreateRenderTarget(512, 512))
        {
            SetRenderTarget(RT);
        }
        PreviewActor->RequestCapture();
    }
    RefreshUI();
}

AFaceParallaxPreviewActor* UFaceParallaxEditorWidget::GetPreviewActor() const
{
    return PreviewActor.Get();
}

void UFaceParallaxEditorWidget::SetStatus(const FString& Msg, const FLinearColor& Color)
{
    if (TextStatus.IsValid())
    {
        TextStatus->SetText(FText::FromString(Msg));
        TextStatus->SetColorAndOpacity(FSlateColor(Color));
    }
}

void UFaceParallaxEditorWidget::PostInitProperties()
{
    Super::PostInitProperties();
    if (!PreviewActor.IsValid())
        PreviewActor = nullptr;
    if (ActivePreset && !IsValid(ActivePreset))
        ActivePreset = nullptr;
}

void UFaceParallaxEditorWidget::ClearStaleTargets()
{
    bool bWasStale = !PreviewActor.IsValid();
    PreviewActor = nullptr;
    // Only re-discover when the previous target was actually stale — never clobber a valid selection
    if (bWasStale)
    {
        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World)
        {
            for (TActorIterator<AFaceParallaxPreviewActor> It(World); It; ++It)
            {
                if (*It)
                {
                    SetPreviewActor(*It);
                    break;
                }
            }
        }
    }
    RefreshActorSelector();
    if (TextStatus.IsValid())
        TextStatus->SetText(FText::FromString(TEXT("Cleared stale targets")));
}

// ====================================================================
// PRESET MANAGEMENT
// ====================================================================

void UFaceParallaxEditorWidget::ApplyPresetToPreview()
{
    if (!ValidatePreviewActor() || !ValidatePreset()) return;
    PreviewActor->ApplyPreset(ActivePreset);
    ActiveViewState = EFaceAngleState::Front;
}

int32 UFaceParallaxEditorWidget::SpawnLayerQuadsOnPreview()
{
    if (!ValidatePreviewActor()) return 0;
    if (!PreviewActor->FaceParallax)
    {
        if (TextStatus.IsValid())
            TextStatus->SetText(FText::FromString(TEXT("Preview actor has no FaceParallax component")));
        return 0;
    }
    const int32 Count = PreviewActor->FaceParallax->SpawnLayerQuads();
    PreviewActor->RefreshPreview();
    if (TextStatus.IsValid())
    {
        TextStatus->SetText(FText::FromString(
            FString::Printf(TEXT("Spawned %d layer quads on preview"), Count)));
    }
    return Count;
}

UFaceParallaxPreset* UFaceParallaxEditorWidget::CreateNewPreset(const FString& AssetName,
    const FString& PackagePath)
{
    // Ensure a unique asset name — CreatePackage/NewObject on an existing
    // package would silently reuse the stale on-disk object.
    FString UniqueName = AssetName;
    int32 Suffix = 1;
    while (FindObject<UObject>(nullptr, *(PackagePath / UniqueName + TEXT(".") + UniqueName)))
    {
        UniqueName = FString::Printf(TEXT("%s_%d"), *AssetName, Suffix++);
    }

    UPackage* Package = CreatePackage(*(PackagePath / UniqueName));
    UFaceParallaxPreset* NewPreset = NewObject<UFaceParallaxPreset>(Package, FName(*UniqueName),
        RF_Public | RF_Standalone);
    if (NewPreset)
    {
        NewPreset->CanvasSize = FVector2D(512.0f, 512.0f);
        NewPreset->bAutoFitOnAssign = true;
        NewPreset->PopulateDefaultAssignments(TArray<FString>{ TEXT("Eyes"), TEXT("Brows"), TEXT("Mouth"), TEXT("Hair") });
        NewPreset->MarkPackageDirty();
        ActivePreset = NewPreset;
        SavePreset();   // persist to disk so the preset survives editor restarts
    }
    return NewPreset;
}

bool UFaceParallaxEditorWidget::SavePreset()
{
    if (!ValidatePreset()) return false;

    UPackage* Package = ActivePreset->GetPackage();
    if (!Package) return false;

    Package->MarkAsFullyLoaded();
    ActivePreset->MarkPackageDirty();

    FString PackageName = Package->GetName();
    FString PackageFilename = FPackageName::LongPackageNameToFilename(PackageName,
        FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Standalone;
    return UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs);
}

void UFaceParallaxEditorWidget::SetCanvasSize(float Width, float Height)
{
    if (!ValidatePreset()) return;
    ActivePreset->CanvasSize = FVector2D(Width, Height);
}

FVector2D UFaceParallaxEditorWidget::GetCanvasSize() const
{
    return ValidatePreset() ? ActivePreset->CanvasSize : FVector2D::ZeroVector;
}

void UFaceParallaxEditorWidget::SetAutoFitOnAssign(bool bEnabled)
{
    if (!ValidatePreset()) return;
    ActivePreset->bAutoFitOnAssign = bEnabled;
}

bool UFaceParallaxEditorWidget::GetAutoFitOnAssign() const
{
    return ValidatePreset() && ActivePreset->bAutoFitOnAssign;
}

// ====================================================================
// VIEW STATE
// ====================================================================

void UFaceParallaxEditorWidget::SetActiveViewState(EFaceAngleState State)
{
    ActiveViewState = State;
    if (bCameraFollowsView && PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->SetOrbitYaw(PreviewActor->FaceParallax->GetZoneCenterYaw(State));
        PreviewActor->SetOrbitPitch(PreviewActor->FaceParallax->GetZoneCenterPitch(State));
    }
    RefreshUI();
}

EFaceAngleState UFaceParallaxEditorWidget::GetActiveViewState() const
{
    return ActiveViewState;
}

TArray<EFaceAngleState> UFaceParallaxEditorWidget::GetAssignedStates() const
{
    return ValidatePreset() ? ActivePreset->GetAssignedStates() : TArray<EFaceAngleState>();
}

// ====================================================================
// UNDO STACK
// ====================================================================

void UFaceParallaxEditorWidget::PushUndoState(const FString& Desc)
{
    if (bIsRestoringUndo || IsTemplate() || !ActivePreset) return;

    UPackage* TempPkg = CreatePackage(TEXT("/Temp/FaceParallaxUndoStack"));
    TempPkg->SetFlags(RF_Transient);
    const FString BackupName = FString::Printf(TEXT("UndoBackup_%d"), UndoSerial++);
    UFaceParallaxPreset* Backup = DuplicateObject<UFaceParallaxPreset>(ActivePreset, TempPkg, FName(*BackupName));
    if (!Backup) return;

    UndoStack.Add(FFaceUndoEntry(Desc, Backup));
    if (UndoStack.Num() > MaxUndoEntries)
    {
        UndoStack.RemoveAt(0);
    }
    // A new mutation invalidates the redo branch.
    RedoStack.Empty();
}

bool UFaceParallaxEditorWidget::Undo()
{
    if (UndoStack.Num() == 0)
    {
        SetStatus(TEXT("Nothing to undo"), FLinearColor(0.8f, 0.8f, 0.8f));
        return false;
    }
    const FFaceUndoEntry Entry = UndoStack.Last();
    UndoStack.RemoveAt(UndoStack.Num() - 1);
    bIsRestoringUndo = true;
    RestoreFromBackup(Entry.Backup, Entry.Label);
    bIsRestoringUndo = false;
    RedoStack.Add(Entry);
    RefreshUI();
    if (TextStatus.IsValid())
        TextStatus->SetText(FText::FromString(FString::Printf(TEXT("Undid: %s"), *Entry.Label)));
    return true;
}

bool UFaceParallaxEditorWidget::Redo()
{
    if (RedoStack.Num() == 0)
    {
        SetStatus(TEXT("Nothing to redo"), FLinearColor(0.8f, 0.8f, 0.8f));
        return false;
    }
    const FFaceUndoEntry Entry = RedoStack.Last();
    RedoStack.RemoveAt(RedoStack.Num() - 1);
    bIsRestoringUndo = true;
    RestoreFromBackup(Entry.Backup, Entry.Label);
    bIsRestoringUndo = false;
    UndoStack.Add(Entry);
    if (UndoStack.Num() > MaxUndoEntries)
    {
        UndoStack.RemoveAt(0);
    }
    RefreshUI();
    if (TextStatus.IsValid())
        TextStatus->SetText(FText::FromString(FString::Printf(TEXT("Redid: %s"), *Entry.Label)));
    return true;
}

bool UFaceParallaxEditorWidget::CanUndo() const
{
    return UndoStack.Num() > 0;
}

bool UFaceParallaxEditorWidget::CanRedo() const
{
    return RedoStack.Num() > 0;
}

FString UFaceParallaxEditorWidget::GetUndoLabel() const
{
    return UndoStack.Num() > 0 ? UndoStack.Last().Label : FString();
}

FString UFaceParallaxEditorWidget::GetRedoLabel() const
{
    return RedoStack.Num() > 0 ? RedoStack.Last().Label : FString();
}

bool UFaceParallaxEditorWidget::RestoreFromBackup(UFaceParallaxPreset* Backup, const FString& Desc)
{
    if (!Backup || !ActivePreset) return false;

    // Copy all assignments from the backup back into the active preset.
    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::Back, EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };
    for (EFaceAngleState S : AllStates)
    {
        ActivePreset->ClearState(S);
        TArray<FName> Tags = Backup->GetAllLayerTags(S);
        for (FName Tag : Tags)
        {
            FFaceArtSlot ArtSlot = Backup->GetSlot(S, Tag);
            ActivePreset->SetSlot(S, Tag, ArtSlot);
        }
        if (Backup->HasState(S))
        {
            TArray<FName> STags = Backup->GetAllLayerTags(S);
            for (FName Tag : STags)
            {
                const int32 N = Backup->GetNestedElementCount(S, Tag);
                for (int32 i = 0; i < N; ++i)
                {
                    FFacePin3D Pin = Backup->GetNestedPin3D(S, Tag, i);
                    ActivePreset->SetNestedPin3D(S, Tag, i, Pin);
                }
            }
        }
    }
    ActivePreset->MarkPackageDirty();
    ApplyPresetToPreview();
    UE_LOG(LogTemp, Log, TEXT("[FaceParallaxWidget] RestoreFromBackup: %s"), *Desc);
    return true;
}

bool UFaceParallaxEditorWidget::HasState(EFaceAngleState State) const
{
    return ValidatePreset() && ActivePreset->HasState(State);
}

TArray<FName> UFaceParallaxEditorWidget::GetLayerTagsForState(EFaceAngleState State) const
{
    if (!ValidatePreset()) return TArray<FName>();

    const FFaceViewStateLayerSet* StateSet = ActivePreset->ViewAssignments.Find(State);
    if (!StateSet) return TArray<FName>();

    TArray<FName> Tags;
    StateSet->Layers.GetKeys(Tags);
    return Tags;
}

int32 UFaceParallaxEditorWidget::GetLayerCount() const
{
    return GetUILayerTags().Num();
}

TArray<FName> UFaceParallaxEditorWidget::GetUILayerTags() const
{
    TArray<FName> LNames;
    // The preset is the source of truth for which layers have art.
    if (ActivePreset)
    {
        for (int32 S = 0; S <= (int32)EFaceAngleState::Bottom; ++S)
        {
            for (const FName& Tag : ActivePreset->GetAllLayerTags((EFaceAngleState)S))
            {
                if (Tag.IsValid() && !LNames.Contains(Tag)) LNames.Add(Tag);
            }
        }
    }
    // Supplement with component layer definitions that are not yet in the
    // preset (e.g. just-added layers) — except the constructor-seeded
    // placeholder "FaceLayer", which has no slot in the preset.
    if (UFaceParallaxComponent* Comp = GetParallaxComponent())
    {
        for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
        {
            FFaceLayerDef Def = Comp->GetLayerDefinition(i);
            FName Tag = Def.LayerTag;
            if (!Tag.IsValid() || LNames.Contains(Tag)) continue;
            if (ActivePreset && IsSeedPlaceholderLayerDef(Def)) continue;
            LNames.Add(Tag);
        }
    }
    if (LNames.Num() == 0)
        LNames = { FName("Eyes"), FName("Brows"), FName("Mouth"), FName("Hair") };
    return LNames;
}

// ====================================================================
// TRANSFORM — PER-LAYER
// ====================================================================

FFaceArtTransform UFaceParallaxEditorWidget::GetLayerCanonicalTransform(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return FFaceArtTransform();
    return ActivePreset->GetSlot(State, LayerTag).CanonicalTransform;
}

void UFaceParallaxEditorWidget::SetLayerPosition(EFaceAngleState State, FName LayerTag,
    float X, float Y)
{
    if (!ValidatePreset()) return;
    FFaceArtTransform T = ActivePreset->GetSlot(State, LayerTag).CanonicalTransform;
    T.Position = FVector2D(X, Y);
    ApplyCanonicalTransformWithLink(State, LayerTag, T);
}

void UFaceParallaxEditorWidget::SetLayerScale(EFaceAngleState State, FName LayerTag,
    float X, float Y)
{
    if (!ValidatePreset()) return;
    FFaceArtTransform T = ActivePreset->GetSlot(State, LayerTag).CanonicalTransform;
    T.Scale = FVector2D(FMath::Max(0.01f, X), FMath::Max(0.01f, Y));
    ApplyCanonicalTransformWithLink(State, LayerTag, T);
}

void UFaceParallaxEditorWidget::SetLayerRotation(EFaceAngleState State, FName LayerTag,
    float Degrees)
{
    if (!ValidatePreset()) return;
    FFaceArtTransform T = ActivePreset->GetSlot(State, LayerTag).CanonicalTransform;
    T.Rotation = Degrees;
    ApplyCanonicalTransformWithLink(State, LayerTag, T);
}

void UFaceParallaxEditorWidget::SetLayerTransform(EFaceAngleState State, FName LayerTag,
    const FFaceArtTransform& Transform)
{
    ApplyCanonicalTransformWithLink(State, LayerTag, Transform);
}

void UFaceParallaxEditorWidget::ResetLayerTransform(EFaceAngleState State, FName LayerTag)
{
    FWidgetUndoScope UndoScope(this, TEXT("Reset Layer Transform"));
    if (!ValidatePreset()) return;
    ActivePreset->SetCanonicalTransform(State, LayerTag, FFaceArtTransform());

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

FFaceArtTransform UFaceParallaxEditorWidget::GetEffectiveLayerTransform(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return FFaceArtTransform();
    return ActivePreset->GetEffectiveTransform(State, LayerTag);
}

void UFaceParallaxEditorWidget::ApplyAutoFit(EFaceAngleState State, FName LayerTag)
{
    if (!ValidatePreset()) return;
    ActivePreset->ApplyAutoFitToSlot(State, LayerTag);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ApplyAutoFitToAllSlots()
{
    if (!ValidatePreset()) return;

    for (const auto& StatePair : ActivePreset->ViewAssignments)
    {
        for (const auto& LayerPair : StatePair.Value.Layers)
        {
            ActivePreset->ApplyAutoFitToSlot(StatePair.Key, LayerPair.Key);
        }
    }

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncLayerToAllViews(EFaceAngleState State, FName LayerTag)
{
    FWidgetUndoScope UndoScope(this, TEXT("Sync Layer to All Views"));
    if (!ValidatePreset()) return;
    ActivePreset->SyncCanonicalToAllViews(State, LayerTag);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncTexturesLayerToAllViews(EFaceAngleState State, FName LayerTag)
{
    FWidgetUndoScope UndoScope(this, TEXT("Sync Textures to All Views"));
    if (!ValidatePreset()) return;
    ActivePreset->SyncTexturesToAllViews(State, LayerTag);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncAllLayersToAllViews()
{
    FWidgetUndoScope UndoScope(this, TEXT("Sync All Layers to All Views"));
    if (!ValidatePreset()) return;

    for (const auto& StatePair : ActivePreset->ViewAssignments)
    {
        for (const auto& LayerPair : StatePair.Value.Layers)
        {
            ActivePreset->SyncCanonicalToAllViews(StatePair.Key, LayerPair.Key);
        }
    }

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncLayerToSelectedViews(EFaceAngleState State, FName LayerTag,
    const TArray<EFaceAngleState>& DestViews, bool bIncludeTextures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Sync Layer to Selected Views"));
    if (!ValidatePreset()) return;
    if (!ActivePreset->HasState(State)) return;

    const FFaceArtSlot& SourceSlot = ActivePreset->GetSlot(State, LayerTag);
    if (bIncludeTextures)
    {
        for (EFaceAngleState Dest : DestViews)
        {
            ActivePreset->SetTexturesForSlot(Dest, LayerTag, SourceSlot.Textures);
        }
    }
    for (EFaceAngleState Dest : DestViews)
    {
        if (Dest == State) continue;
        ActivePreset->SyncCanonicalToAllViews(State, LayerTag);
        FFaceArtSlot& DestSlot = ActivePreset->GetSlotMutable(Dest, LayerTag);
        DestSlot.CanonicalTransform = SourceSlot.CanonicalTransform;
    }
    if (bIncludeTextures)
    {
        ActivePreset->SetTexturesForSlot(State, LayerTag, SourceSlot.Textures);
    }

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncLayerAxisToAllViews(EFaceAngleState State, FName LayerTag, int32 Axis)
{
    FWidgetUndoScope UndoScope(this, TEXT("Sync Layer Axis to All Views"));
    if (!ValidatePreset()) return;
    ActivePreset->SyncCanonicalAxisToAllViews(State, LayerTag, Axis);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

// ====================================================================
// VIEW OVERRIDES
// ====================================================================

bool UFaceParallaxEditorWidget::HasViewOverride(EFaceAngleState State, FName LayerTag,
    EFaceAngleState OverrideView) const
{
    return ValidatePreset() && ActivePreset->HasViewOverride(State, LayerTag, OverrideView);
}

FFaceArtTransform UFaceParallaxEditorWidget::GetViewOverride(EFaceAngleState State, FName LayerTag,
    EFaceAngleState OverrideView) const
{
    if (!ValidatePreset()) return FFaceArtTransform();
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceArtTransform* Override = ArtSlot.ViewOverrides.Find(OverrideView);
    return Override ? *Override : FFaceArtTransform();
}

void UFaceParallaxEditorWidget::SetViewOverride(EFaceAngleState State, FName LayerTag,
    EFaceAngleState OverrideView, const FFaceArtTransform& Override)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set View Override"));
    if (!ValidatePreset()) return;
    ActivePreset->SetViewOverride(State, LayerTag, OverrideView, Override);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearViewOverride(EFaceAngleState State, FName LayerTag,
    EFaceAngleState OverrideView)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear View Override"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearViewOverride(State, LayerTag, OverrideView);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear All Overrides for Slot"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearAllOverridesForSlot(State, LayerTag);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllOverrides()
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear All Overrides"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearAllOverrides();

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

TArray<EFaceAngleState> UFaceParallaxEditorWidget::GetOverrideViewsForSlot(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return TArray<EFaceAngleState>();

    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    TArray<EFaceAngleState> Result;
    ArtSlot.ViewOverrides.GetKeys(Result);
    return Result;
}

void UFaceParallaxEditorWidget::SetViewOverrideMode(bool bEnabled)
{
    bViewOverrideMode = bEnabled;
    if (CheckViewOverrideMode.IsValid())
    {
        CheckViewOverrideMode->SetIsChecked(bEnabled);
    }
    RefreshUI();
}

bool UFaceParallaxEditorWidget::GetViewOverrideMode() const
{
    return bViewOverrideMode;
}

// ====================================================================
// TEXTURES
// ====================================================================

FFaceTextureSet UFaceParallaxEditorWidget::GetSlotTextures(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    return ActivePreset->GetTexturesForSlot(State, LayerTag);
}

void UFaceParallaxEditorWidget::SetSlotTextures(EFaceAngleState State, FName LayerTag,
    const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Slot Textures"));
    if (!ValidatePreset()) return;
    ActivePreset->SetTexturesForSlot(State, LayerTag, Textures);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

FVector2D UFaceParallaxEditorWidget::GetSlotSourceSize(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset() || !PreviewActor.IsValid()) return FVector2D::ZeroVector;
    return PreviewActor->GetPartSourceSize(State, LayerTag);
}

UTexture2D* UFaceParallaxEditorWidget::GetSlotAlbedo(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return nullptr;
    return ActivePreset->GetTexturesForSlot(State, LayerTag).Albedo;
}

UTexture2D* UFaceParallaxEditorWidget::GetSlotDepth(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return nullptr;
    return ActivePreset->GetTexturesForSlot(State, LayerTag).Depth;
}

UTexture2D* UFaceParallaxEditorWidget::GetSlotNormal(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return nullptr;
    return ActivePreset->GetTexturesForSlot(State, LayerTag).Normal;
}

// ====================================================================
// IMPORT
// ====================================================================

TArray<UTexture2D*> UFaceParallaxEditorWidget::ImportTexturesFromFiles(const TArray<FString>& Files)
{
    TArray<UTexture2D*> Result;
    if (Files.Num() == 0) return Result;

    const FString DestPath = TEXT("/Game/FaceParallax/Imported");
    UPackage* RootPkg = CreatePackage(*DestPath);
    RootPkg->SetFlags(RF_Public | RF_Standalone);
    RootPkg->FullyLoad();
    RootPkg->MarkPackageDirty();

    FEditorDirectories::Get().SetLastDirectory(ELastDirectory::UNR, FPaths::GetPath(Files[0]));

    FAssetToolsModule& AssetTools = FAssetToolsModule::GetModule();
    TArray<UObject*> ImportedObjs = AssetTools.Get().ImportAssets(Files, DestPath, nullptr);
    if (ImportedObjs.Num() == 0)
    {
        SetStatus(TEXT("Import failed: no assets were created"), FLinearColor::Red);
        return Result;
    }

    for (UObject* Obj : ImportedObjs)
    {
        if (UTexture2D* Tex = Cast<UTexture2D>(Obj))
        {
            Result.Add(Tex);
        }
    }

    return Result;
}

bool UFaceParallaxEditorWidget::AssignTextureToSlot(UTexture2D* Tex, EFaceAngleState State,
    FName LayerTag, const FString& Channel)
{
    if (!Tex || !ValidatePreset()) return false;

    FFaceTextureSet Textures = ActivePreset->GetTexturesForSlot(State, LayerTag);
    if (Channel == TEXT("Normal") || Channel == TEXT("normal") || Channel == TEXT("N"))
    {
        Textures.Normal = Tex;
    }
    else if (Channel == TEXT("Depth") || Channel == TEXT("depth") || Channel == TEXT("D"))
    {
        Textures.Depth = Tex;
    }
    else
    {
        Textures.Albedo = Tex;
    }
    ActivePreset->SetTexturesForSlot(State, LayerTag, Textures);
    if (ActivePreset->bAutoFitOnAssign)
    {
        ActivePreset->SetCanonicalTransform(State, LayerTag, FFaceArtTransform());
    }

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
    RefreshTextureThumbs();
    RefreshUI();
    SetStatus(FString::Printf(TEXT("Assigned %s -> %s:%s (%s)"),
        *FPaths::GetBaseFilename(Tex->GetName()),
        *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)State), *LayerTag.ToString(),
        *Channel), FLinearColor(0.3f, 1.0f, 0.3f));
    return true;
}

void UFaceParallaxEditorWidget::OpenImportArtDialog()
{
    TArray<FString> OutFiles;
    void* ParentWindow = nullptr;
    if (IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get())
    {
        const FString DefaultPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::UNR);
        const bool bOpened = DesktopPlatform->OpenFileDialog(
            ParentWindow, TEXT("Import face art"),
            DefaultPath, TEXT(""), TEXT("Image files|*.png;*.jpg;*.jpeg;*.tga;*.bmp|All files|*.*"),
            EFileDialogFlags::Multiple, OutFiles);
        if (!bOpened || OutFiles.Num() == 0)
        {
            return;
        }
    }
    else
    {
        return;
    }

    TArray<UTexture2D*> Imported = ImportTexturesFromFiles(OutFiles);
    if (Imported.Num() == 0)
    {
        SetStatus(TEXT("No textures imported"), FLinearColor::Yellow);
        return;
    }

    int32 Assigned = 0;
    if (SelectedLayerName != NAME_None)
    {
        for (UTexture2D* Tex : Imported)
        {
            const FString Channel = ChannelFromTextureName(Tex->GetName());
            if (AssignTextureToSlot(Tex, ActiveViewState, SelectedLayerName, Channel))
            {
                ++Assigned;
            }
        }
    }
    if (SelectedLayerName == NAME_None)
    {
        SetStatus(FString::Printf(TEXT("Imported %d texture(s) to /Game/FaceParallax/Imported — select a layer first, then re-run Import Art to assign them"),
            Imported.Num()), FLinearColor(0.8f, 0.8f, 0.3f));
    }
    else
    {
        SetStatus(FString::Printf(TEXT("Imported %d texture(s), assigned %d to %s:%s by channel suffix"),
            Imported.Num(), Assigned, *SelectedLayerName.ToString(),
            *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)ActiveViewState)),
            FLinearColor(0.3f, 1.0f, 0.3f));
    }
    RefreshTextureThumbs();
    RefreshUI();
}

// ====================================================================
// CAMERA
// ====================================================================

void UFaceParallaxEditorWidget::SetOrbitYaw(float Degrees)
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->SetOrbitYaw(Degrees);
}

float UFaceParallaxEditorWidget::GetOrbitYaw() const
{
    return ValidatePreviewActor() ? PreviewActor->GetOrbitYaw() : 0.0f;
}

void UFaceParallaxEditorWidget::SetOrbitPitch(float Degrees)
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->SetOrbitPitch(Degrees);
}

float UFaceParallaxEditorWidget::GetOrbitPitch() const
{
    return ValidatePreviewActor() ? PreviewActor->GetOrbitPitch() : 0.0f;
}

void UFaceParallaxEditorWidget::SetOrbitDistance(float Distance)
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->SetOrbitDistance(Distance);
}

float UFaceParallaxEditorWidget::GetOrbitDistance() const
{
    return ValidatePreviewActor() ? PreviewActor->GetOrbitDistance() : 0.0f;
}

void UFaceParallaxEditorWidget::SetPreviewFOV(float FOV)
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->SetPreviewFOV(FOV);
}

float UFaceParallaxEditorWidget::GetPreviewFOV() const
{
    return ValidatePreviewActor() ? PreviewActor->GetPreviewFOV() : 0.0f;
}

void UFaceParallaxEditorWidget::SetAutoRotate(bool bEnabled)
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->SetAutoRotate(bEnabled);
}

bool UFaceParallaxEditorWidget::GetAutoRotate() const
{
    return ValidatePreviewActor() && PreviewActor->GetAutoRotate();
}

void UFaceParallaxEditorWidget::SetAutoRotateSpeed(float DegreesPerSec)
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->SetAutoRotateSpeed(DegreesPerSec);
}

float UFaceParallaxEditorWidget::GetAutoRotateSpeed() const
{
    return ValidatePreviewActor() ? PreviewActor->GetAutoRotateSpeed() : 0.0f;
}

void UFaceParallaxEditorWidget::ResetCamera()
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->ResetCamera();
}

void UFaceParallaxEditorWidget::SetCameraFollowsView(bool bEnabled)
{
    bCameraFollowsView = bEnabled;
    if (CheckCameraFollow.IsValid())
    {
        CheckCameraFollow->SetIsChecked(bEnabled);
    }
}

bool UFaceParallaxEditorWidget::GetCameraFollowsView() const
{
    return bCameraFollowsView;
}

void UFaceParallaxEditorWidget::SnapCameraToActiveView()
{
    if (!ValidatePreviewActor()) return;
    if (!PreviewActor->FaceParallax) return;
    const float TargetYaw = PreviewActor->FaceParallax->GetZoneCenterYaw(ActiveViewState);
    const float TargetPitch = PreviewActor->FaceParallax->GetZoneCenterPitch(ActiveViewState);
    PreviewActor->SetOrbitYaw(TargetYaw);
    PreviewActor->SetOrbitPitch(TargetPitch);
}

// ====================================================================
// DEBUG OVERLAYS
// ====================================================================

void UFaceParallaxEditorWidget::ShowTextures(bool bVisible)
{
    bLocalShowTextures = bVisible;
    if (!ValidatePreviewActor()) return;
    PreviewActor->ShowTextures(bVisible);
}

void UFaceParallaxEditorWidget::ShowDepthMesh(bool bVisible)
{
    bLocalShowDepthMesh = bVisible;
    if (!ValidatePreviewActor()) return;
    PreviewActor->ShowDepthMesh(bVisible);
}

void UFaceParallaxEditorWidget::ShowWireframe(bool bVisible)
{
    bLocalShowWireframe = bVisible;
    if (!ValidatePreviewActor()) return;
    PreviewActor->ShowWireframe(bVisible);
}

void UFaceParallaxEditorWidget::ColorByDepth(bool bEnabled)
{
    bLocalColorByDepth = bEnabled;
    if (!ValidatePreviewActor()) return;
    PreviewActor->ColorByDepth(bEnabled);
}

// ====================================================================
// STATUS
// ====================================================================

int32 UFaceParallaxEditorWidget::GetAssignedStateCount() const
{
    if (!ValidatePreset()) return 0;
    return ActivePreset->GetAssignedStates().Num();
}

int32 UFaceParallaxEditorWidget::GetTotalAssignedSlots() const
{
    return ValidatePreset() ? ActivePreset->GetTotalAssignedSlots() : 0;
}

int32 UFaceParallaxEditorWidget::GetActiveLayerCount() const
{
    return GetLayerCount();
}

FString UFaceParallaxEditorWidget::GetStatusString() const
{
    if (!ValidatePreset()) return TEXT("No preset assigned");

    int32 AssignedStates = ActivePreset->GetAssignedStates().Num();
    int32 TotalSlots = ActivePreset->GetTotalAssignedSlots();
    int32 Layers = GetLayerCount();
    UFaceParallaxComponent* Comp = GetParallaxComponent();

    FString BlinkStr = (Comp && Comp->bBlinkingEnabled) ? TEXT("On") : TEXT("Off");

    FString ExprStr = TEXT("Neutral");
    if (Comp)
    {
        switch (Comp->CurrentExpression)
        {
            case EExpression::Neutral: ExprStr = TEXT("Neutral"); break;
            case EExpression::Smile:   ExprStr = TEXT("Smile");   break;
            case EExpression::Frown:   ExprStr = TEXT("Frown");   break;
            default:
                ExprStr = TEXT("Unknown");
                UE_LOG(LogTemp, Warning, TEXT("GetStatusString: Unknown expression %d"), (int32)Comp->CurrentExpression);
                break;
        }
    }

    FString VisemeStr = TEXT("--");
    if (Comp && Comp->IsVisemePlaying())
    {
        switch (Comp->GetCurrentViseme())
        {
            case EViseme::Uh:  VisemeStr = TEXT("Uh");  break;
            case EViseme::Ah:  VisemeStr = TEXT("Ah");  break;
            case EViseme::Ee:  VisemeStr = TEXT("Ee");  break;
            case EViseme::D:   VisemeStr = TEXT("D");   break;
            case EViseme::S:   VisemeStr = TEXT("S");   break;
            case EViseme::F:   VisemeStr = TEXT("F");   break;
            case EViseme::M:   VisemeStr = TEXT("M");   break;
            case EViseme::L:   VisemeStr = TEXT("L");   break;
            case EViseme::WOO: VisemeStr = TEXT("WOO"); break;
            case EViseme::Oh:  VisemeStr = TEXT("Oh");  break;
            case EViseme::R:   VisemeStr = TEXT("R");   break;
            default:
                VisemeStr = TEXT("Unknown");
                UE_LOG(LogTemp, Warning, TEXT("GetStatusString: Unknown viseme %d"), (int32)Comp->GetCurrentViseme());
                break;
        }
    }

    return FString::Printf(TEXT("%d/10 states | %d layers | %d slots | Blink:%s | Expr:%s | Viseme:%s"),
        AssignedStates, Layers, TotalSlots, *BlinkStr, *ExprStr, *VisemeStr);
}

// ====================================================================
// DYNAMIC ART (eye tracking)
// ====================================================================

void UFaceParallaxEditorWidget::SetDriveArtPositionFromYaw(bool bEnabled)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->bDriveArtPositionFromYaw = bEnabled;
}

bool UFaceParallaxEditorWidget::GetDriveArtPositionFromYaw() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->bDriveArtPositionFromYaw;
}

void UFaceParallaxEditorWidget::SetMaxYawArtOffset(float Offset)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->MaxYawArtOffset = FMath::Clamp(Offset, 0.0f, 1.0f);
}

float UFaceParallaxEditorWidget::GetMaxYawArtOffset() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->MaxYawArtOffset : 0.0f;
}

// ====================================================================
// MATERIAL PARAM NAMES
// ====================================================================

FName UFaceParallaxEditorWidget::GetAlbedoParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->AlbedoParamName : FName("AlbedoTexture");
}

FName UFaceParallaxEditorWidget::GetNormalParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->NormalParamName : FName("NormalTexture");
}

FName UFaceParallaxEditorWidget::GetDepthParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->DepthParamName : FName("DepthTexture");
}

FName UFaceParallaxEditorWidget::GetAlbedoPrevParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->AlbedoPrevParamName : FName("AlbedoTexturePrev");
}

FName UFaceParallaxEditorWidget::GetNormalPrevParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->NormalPrevParamName : FName("NormalTexturePrev");
}

FName UFaceParallaxEditorWidget::GetDepthPrevParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->DepthPrevParamName : FName("DepthTexturePrev");
}

FName UFaceParallaxEditorWidget::GetArtPositionParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ArtPositionParamName : FName("ArtPosition");
}

FName UFaceParallaxEditorWidget::GetArtScaleParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ArtScaleParamName : FName("ArtScale");
}

FName UFaceParallaxEditorWidget::GetArtRotationParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ArtRotationParamName : FName("ArtRotation");
}

// ====================================================================
// BLINK ANIMATION
// ====================================================================

void UFaceParallaxEditorWidget::SetBlinkingEnabled(bool bEnabled)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->bBlinkingEnabled = bEnabled;
}

bool UFaceParallaxEditorWidget::GetBlinkingEnabled() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->bBlinkingEnabled;
}

void UFaceParallaxEditorWidget::SetBlinkInterval(float Min, float Max)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetBlinkInterval(Min, Max);
}

float UFaceParallaxEditorWidget::GetBlinkIntervalMin() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->BlinkIntervalMin : 3.0f;
}

float UFaceParallaxEditorWidget::GetBlinkIntervalMax() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->BlinkIntervalMax : 7.0f;
}

void UFaceParallaxEditorWidget::ForceBlink()
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ForceBlink();
}

bool UFaceParallaxEditorWidget::IsBlinking() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->IsBlinking();
}

void UFaceParallaxEditorWidget::SetBlinkFrameDuration(float Duration)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->BlinkFrameDuration = FMath::Max(0.001f, Duration);
}

float UFaceParallaxEditorWidget::GetBlinkFrameDuration() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->BlinkFrameDuration : 0.03f;
}

int32 UFaceParallaxEditorWidget::GetBlinkFrameCount(EFaceAngleState State, FName LayerTag) const
{
    if (!ValidatePreset()) return 0;
    return ActivePreset->GetSlot(State, LayerTag).BlinkFrames.Num();
}

void UFaceParallaxEditorWidget::SetBlinkFrameTextures(EFaceAngleState State, FName LayerTag,
    int32 FrameIndex, const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Blink Frame Textures"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    if (FrameIndex >= 0 && FrameIndex <= ArtSlot.BlinkFrames.Num())
    {
        if (FrameIndex == ArtSlot.BlinkFrames.Num())
        {
            ArtSlot.BlinkFrames.Add(Textures);
        }
        else
        {
            ArtSlot.BlinkFrames[FrameIndex] = Textures;
        }
        ActivePreset->SetSlot(State, LayerTag, ArtSlot);
    }
}

FFaceTextureSet UFaceParallaxEditorWidget::GetBlinkFrameTextures(EFaceAngleState State,
    FName LayerTag, int32 FrameIndex) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    if (FrameIndex >= 0 && FrameIndex < ArtSlot.BlinkFrames.Num())
    {
        return ArtSlot.BlinkFrames[FrameIndex];
    }
    return FFaceTextureSet();
}

void UFaceParallaxEditorWidget::ClearBlinkFrames(EFaceAngleState State, FName LayerTag)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear Blink Frames"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.BlinkFrames.Empty();
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

// ====================================================================
// EXPRESSION SYSTEM
// ====================================================================

void UFaceParallaxEditorWidget::SetExpression(EExpression NewExpression)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetExpression(NewExpression);
}

EExpression UFaceParallaxEditorWidget::GetExpression() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->CurrentExpression : EExpression::Neutral;
}

void UFaceParallaxEditorWidget::SetExpressionCrossfadeDuration(float Duration)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetExpressionCrossfadeDuration(Duration);
}

float UFaceParallaxEditorWidget::GetExpressionCrossfadeDuration() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ExpressionCrossfadeDuration : 0.3f;
}

bool UFaceParallaxEditorWidget::IsExpressionTransitioning() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->IsExpressionTransitioning();
}

void UFaceParallaxEditorWidget::ClearExpressionTextures(EFaceAngleState State, FName LayerTag,
    EExpression Expression)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear Expression Textures"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.ExpressionTextures.Remove(Expression);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

void UFaceParallaxEditorWidget::SetExpressionTextures(EFaceAngleState State, FName LayerTag,
    EExpression Expression, const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Expression Textures"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.ExpressionTextures.Add(Expression, Textures);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

FFaceTextureSet UFaceParallaxEditorWidget::GetExpressionTextures(EFaceAngleState State,
    FName LayerTag, EExpression Expression) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceTextureSet* Found = ArtSlot.ExpressionTextures.Find(Expression);
    return Found ? *Found : FFaceTextureSet();
}

bool UFaceParallaxEditorWidget::HasExpressionTextures(EFaceAngleState State,
    FName LayerTag, EExpression Expression) const
{
    if (!ValidatePreset()) return false;
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    return ArtSlot.ExpressionTextures.Contains(Expression);
}

TArray<EExpression> UFaceParallaxEditorWidget::GetAssignedExpressions(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return TArray<EExpression>();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    TArray<EExpression> Result;
    ArtSlot.ExpressionTextures.GetKeys(Result);
    return Result;
}

void UFaceParallaxEditorWidget::SetExpressionByName(FName NewExpressionName)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetExpressionByName(NewExpressionName);
}

FName UFaceParallaxEditorWidget::GetExpressionByName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->CurrentNamedExpression : NAME_None;
}

bool UFaceParallaxEditorWidget::IsNamedExpressionValid() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->CurrentNamedExpression != NAME_None;
}

void UFaceParallaxEditorWidget::SetNamedExpressionTextures(EFaceAngleState State, FName LayerTag,
    FName ExpressionName, const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Named Expression Textures"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.NamedExpressionTextures.Add(ExpressionName, Textures);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

FFaceTextureSet UFaceParallaxEditorWidget::GetNamedExpressionTextures(EFaceAngleState State,
    FName LayerTag, FName ExpressionName) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceTextureSet* Found = ArtSlot.NamedExpressionTextures.Find(ExpressionName);
    return Found ? *Found : FFaceTextureSet();
}

bool UFaceParallaxEditorWidget::HasNamedExpressionTextures(EFaceAngleState State,
    FName LayerTag, FName ExpressionName) const
{
    if (!ValidatePreset()) return false;
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    return ArtSlot.NamedExpressionTextures.Contains(ExpressionName);
}

TArray<FName> UFaceParallaxEditorWidget::GetAssignedNamedExpressions(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return TArray<FName>();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    TArray<FName> Result;
    ArtSlot.NamedExpressionTextures.GetKeys(Result);
    return Result;
}

void UFaceParallaxEditorWidget::ClearNamedExpressionTextures(EFaceAngleState State, FName LayerTag,
    FName ExpressionName)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear Named Expression Textures"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.NamedExpressionTextures.Remove(ExpressionName);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

FName UFaceParallaxEditorWidget::GetExpressionBlendParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ExpressionBlendParamName : FName("ExpressionBlendAlpha");
}

FName UFaceParallaxEditorWidget::GetExpressionAlbedoPrevParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ExpressionAlbedoPrevParamName : FName("ExpressionAlbedoPrev");
}

FName UFaceParallaxEditorWidget::GetExpressionNormalPrevParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ExpressionNormalPrevParamName : FName("ExpressionNormalPrev");
}

FName UFaceParallaxEditorWidget::GetExpressionDepthPrevParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ExpressionDepthPrevParamName : FName("ExpressionDepthPrev");
}

// ====================================================================
// VISEME (speech mouth shapes)
// ====================================================================

void UFaceParallaxEditorWidget::SetVisemeEnabled(bool bEnabled)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->bVisemeEnabled = bEnabled;
}

bool UFaceParallaxEditorWidget::GetVisemeEnabled() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->bVisemeEnabled;
}

void UFaceParallaxEditorWidget::PlayViseme(EViseme NewViseme)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->PlayViseme(NewViseme);
}

void UFaceParallaxEditorWidget::StopViseme()
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->StopViseme();
}

bool UFaceParallaxEditorWidget::IsVisemePlaying() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->IsVisemePlaying();
}

EViseme UFaceParallaxEditorWidget::GetCurrentViseme() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetCurrentViseme() : EViseme::Ah;
}

void UFaceParallaxEditorWidget::SetVisemeFrameDuration(float Duration)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->VisemeFrameDuration = FMath::Max(0.001f, Duration);
}

float UFaceParallaxEditorWidget::GetVisemeFrameDuration() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->VisemeFrameDuration : 0.04f;
}

int32 UFaceParallaxEditorWidget::GetVisemeFrameCount(EFaceAngleState State, FName LayerTag,
    EExpression Expression, EViseme Viseme) const
{
    if (!ValidatePreset()) return 0;
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceExpressionVisemeMap* ExprVisemes =
        ArtSlot.VisemeFrameSets.Find(Expression);
    if (!ExprVisemes) return 0;
    const FFaceVisemeFrameArray* Frames = ExprVisemes->Visemes.Find(Viseme);
    return Frames ? Frames->Frames.Num() : 0;
}

void UFaceParallaxEditorWidget::SetVisemeFrameTextures(EFaceAngleState State, FName LayerTag,
    EExpression Expression, EViseme Viseme, int32 FrameIndex, const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Viseme Frame Textures"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    TArray<FFaceTextureSet>& Frames = ArtSlot.VisemeFrameSets.FindOrAdd(Expression).Visemes.FindOrAdd(Viseme).Frames;
    if (FrameIndex >= 0 && FrameIndex <= Frames.Num())
    {
        if (FrameIndex == Frames.Num())
        {
            Frames.Add(Textures);
        }
        else
        {
            Frames[FrameIndex] = Textures;
        }
        ActivePreset->SetSlot(State, LayerTag, ArtSlot);
    }
}

FFaceTextureSet UFaceParallaxEditorWidget::GetVisemeFrameTextures(EFaceAngleState State, FName LayerTag,
    EExpression Expression, EViseme Viseme, int32 FrameIndex) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceExpressionVisemeMap* ExprVisemes =
        ArtSlot.VisemeFrameSets.Find(Expression);
    if (!ExprVisemes) return FFaceTextureSet();
    const FFaceVisemeFrameArray* Frames = ExprVisemes->Visemes.Find(Viseme);
    if (!Frames || FrameIndex < 0 || FrameIndex >= Frames->Frames.Num()) return FFaceTextureSet();
    return Frames->Frames[FrameIndex];
}

TArray<EViseme> UFaceParallaxEditorWidget::GetAssignedVisemes(EFaceAngleState State, FName LayerTag,
    EExpression Expression) const
{
    if (!ValidatePreset()) return TArray<EViseme>();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceExpressionVisemeMap* ExprVisemes =
        ArtSlot.VisemeFrameSets.Find(Expression);
    if (!ExprVisemes) return TArray<EViseme>();
    TArray<EViseme> Result;
    ExprVisemes->Visemes.GetKeys(Result);
    return Result;
}

void UFaceParallaxEditorWidget::ClearVisemeFrames(EFaceAngleState State, FName LayerTag,
    EExpression Expression, EViseme Viseme)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear Viseme Frames"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    FFaceExpressionVisemeMap* ExprVisemes =
        ArtSlot.VisemeFrameSets.Find(Expression);
    if (ExprVisemes)
    {
        ExprVisemes->Visemes.Remove(Viseme);
        ActivePreset->SetSlot(State, LayerTag, ArtSlot);
    }
}

void UFaceParallaxEditorWidget::ClearAllVisemes(EFaceAngleState State, FName LayerTag,
    EExpression Expression)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear All Visemes"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.VisemeFrameSets.Remove(Expression);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

void UFaceParallaxEditorWidget::PlayVisemeByName(FName NewVisemeName)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->PlayVisemeByName(NewVisemeName);
}

FName UFaceParallaxEditorWidget::GetVisemeByName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->CurrentNamedViseme : NAME_None;
}

bool UFaceParallaxEditorWidget::IsNamedVisemeValid() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->CurrentNamedViseme != NAME_None;
}

int32 UFaceParallaxEditorWidget::GetNamedVisemeFrameCount(EFaceAngleState State, FName LayerTag,
    FName VisemeName) const
{
    if (!ValidatePreset()) return 0;
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceVisemeFrameArray* Found = ArtSlot.NamedVisemeFrames.Find(VisemeName);
    return Found ? Found->Frames.Num() : 0;
}

void UFaceParallaxEditorWidget::SetNamedVisemeFrameTextures(EFaceAngleState State, FName LayerTag,
    FName VisemeName, int32 FrameIndex, const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Named Viseme Frame Textures"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    FFaceVisemeFrameArray& Frames = ArtSlot.NamedVisemeFrames.FindOrAdd(VisemeName);
    if (FrameIndex >= 0 && FrameIndex < Frames.Frames.Num())
    {
        Frames.Frames[FrameIndex] = Textures;
    }
    else if (FrameIndex >= 0 && FrameIndex == Frames.Frames.Num())
    {
        Frames.Frames.Add(Textures);
    }
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

FFaceTextureSet UFaceParallaxEditorWidget::GetNamedVisemeFrameTextures(EFaceAngleState State,
    FName LayerTag, FName VisemeName, int32 FrameIndex) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    const FFaceVisemeFrameArray* Found = ArtSlot.NamedVisemeFrames.Find(VisemeName);
    if (Found && FrameIndex >= 0 && FrameIndex < Found->Frames.Num())
    {
        return Found->Frames[FrameIndex];
    }
    return FFaceTextureSet();
}

TArray<FName> UFaceParallaxEditorWidget::GetAssignedNamedVisemes(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset()) return TArray<FName>();
    const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    TArray<FName> Result;
    ArtSlot.NamedVisemeFrames.GetKeys(Result);
    return Result;
}

void UFaceParallaxEditorWidget::ClearNamedVisemeFrames(EFaceAngleState State, FName LayerTag,
    FName VisemeName)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear Named Viseme Frames"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.NamedVisemeFrames.Remove(VisemeName);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

void UFaceParallaxEditorWidget::ClearAllNamedVisemes(EFaceAngleState State, FName LayerTag)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear All Named Visemes"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.NamedVisemeFrames.Empty();
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

// ====================================================================
// PARAMETER SYSTEM
// ====================================================================

void UFaceParallaxEditorWidget::SetParamsEnabled(bool bEnabled)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetParamsEnabled(bEnabled);
}

bool UFaceParallaxEditorWidget::GetParamsEnabled() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->GetParamsEnabled();
}

void UFaceParallaxEditorWidget::DefineParameter(FName ParamName, float DefaultValue, float Min, float Max, float SmoothingSpeed)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->DefineParameter(ParamName, DefaultValue, Min, Max, SmoothingSpeed);
}

void UFaceParallaxEditorWidget::SetParameterValue(FName ParamName, float Value)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetParameterValue(ParamName, Value);
}

float UFaceParallaxEditorWidget::GetParameterValue(FName ParamName) const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetParameterValue(ParamName) : 0.0f;
}

TArray<FName> UFaceParallaxEditorWidget::GetParameterNames() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetParameterNames() : TArray<FName>();
}

void UFaceParallaxEditorWidget::ResetAllParameters()
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ResetAllParameters();
}

void UFaceParallaxEditorWidget::SetParamSmoothingSpeed(FName ParamName, float Speed)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetParamSmoothingSpeed(ParamName, Speed);
}

float UFaceParallaxEditorWidget::GetParamSmoothingSpeed(FName ParamName) const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetParamSmoothingSpeed(ParamName) : 8.0f;
}

// ====================================================================
// PARAM BINDINGS (per-slot)
// ====================================================================

TArray<FFaceParamBinding> UFaceParallaxEditorWidget::GetParamBindings(EFaceAngleState State, FName LayerTag) const
{
    if (!ValidatePreset()) return TArray<FFaceParamBinding>();
    return ActivePreset->GetParamBindings(State, LayerTag);
}

void UFaceParallaxEditorWidget::SetParamBindings(EFaceAngleState State, FName LayerTag, const TArray<FFaceParamBinding>& Bindings)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Param Bindings"));
    if (!ValidatePreset()) return;
    ActivePreset->SetParamBindings(State, LayerTag, Bindings);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

FFaceTextureSet UFaceParallaxEditorWidget::GetAltTextures(EFaceAngleState State, FName LayerTag) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    return ActivePreset->GetAltTextures(State, LayerTag);
}

void UFaceParallaxEditorWidget::SetAltTextures(EFaceAngleState State, FName LayerTag, const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Alt Textures"));
    if (!ValidatePreset()) return;
    ActivePreset->SetAltTextures(State, LayerTag, Textures);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

// ====================================================================
// SWOOSH TRANSITION
// ====================================================================

void UFaceParallaxEditorWidget::SetSwooshEnabled(bool bEnabled)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshEnabled(bEnabled);
}

bool UFaceParallaxEditorWidget::GetSwooshEnabled() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshEnabled() : false;
}

void UFaceParallaxEditorWidget::SetSwooshSpeedThreshold(float Threshold)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshSpeedThreshold(Threshold);
}

float UFaceParallaxEditorWidget::GetSwooshSpeedThreshold() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshSpeedThreshold() : 0.0f;
}

void UFaceParallaxEditorWidget::SetSwooshBusyness(float Busyness)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshBusyness(Busyness);
}

float UFaceParallaxEditorWidget::GetSwooshBusyness() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshBusyness() : 0.0f;
}

void UFaceParallaxEditorWidget::SetSwooshSize(float Size)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshSize(Size);
}

float UFaceParallaxEditorWidget::GetSwooshSize() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshSize() : 0.0f;
}

void UFaceParallaxEditorWidget::ForceSwoosh(EFaceAngleState TargetState)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ForceSwoosh(TargetState);
}

bool UFaceParallaxEditorWidget::IsSwooshActive() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->IsSwooshActive() : false;
}

int32 UFaceParallaxEditorWidget::GetSwooshFrameCount(EFaceAngleState State, FName LayerTag) const
{
    if (!ValidatePreset()) return 0;
    return ActivePreset->GetSwooshArt(State, LayerTag).Frames.Num();
}

void UFaceParallaxEditorWidget::SetSwooshFrameTextures(EFaceAngleState State, FName LayerTag,
    int32 FrameIndex, const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Swoosh Frame Textures"));
    if (!ValidatePreset()) return;
    FFaceSwooshArt Art = ActivePreset->GetSwooshArt(State, LayerTag);
    if (FrameIndex >= 0)
    {
        if (FrameIndex >= Art.Frames.Num())
        {
            Art.Frames.SetNum(FrameIndex + 1);
        }
        Art.Frames[FrameIndex] = Textures;
    }
    ActivePreset->SetSwooshArt(State, LayerTag, Art);
}

FFaceTextureSet UFaceParallaxEditorWidget::GetSwooshFrameTextures(EFaceAngleState State,
    FName LayerTag, int32 FrameIndex) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    FFaceSwooshArt Art = ActivePreset->GetSwooshArt(State, LayerTag);
    if (FrameIndex >= 0 && FrameIndex < Art.Frames.Num())
    {
        return Art.Frames[FrameIndex];
    }
    return FFaceTextureSet();
}

void UFaceParallaxEditorWidget::ClearSwooshFrames(EFaceAngleState State, FName LayerTag)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear Swoosh Frames"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearSwooshArt(State, LayerTag);
}

// ====================================================================
// NESTED ART
// ====================================================================

void UFaceParallaxEditorWidget::SetNestedArtEnabled(bool bEnabled)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetNestedArtEnabled(bEnabled);
}

bool UFaceParallaxEditorWidget::GetNestedArtEnabled() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetNestedArtEnabled() : false;
}

int32 UFaceParallaxEditorWidget::GetNestedElementCount(EFaceAngleState State, FName LayerTag) const
{
    if (!ValidatePreset()) return 0;
    return ActivePreset->GetNestedElementCount(State, LayerTag);
}

FFaceNestedArt UFaceParallaxEditorWidget::GetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ValidatePreset()) return FFaceNestedArt();
    return ActivePreset->GetNestedElement(State, LayerTag, Index);
}

void UFaceParallaxEditorWidget::SetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceNestedArt& Element)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Nested Element"));
    if (!ValidatePreset()) return;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Element);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::AddNestedElement(EFaceAngleState State, FName LayerTag, const FFaceNestedArt& Element)
{
    FWidgetUndoScope UndoScope(this, TEXT("Add Nested Element"));
    if (!ValidatePreset()) return;
    ActivePreset->AddNestedElement(State, LayerTag, Element);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::RemoveNestedElement(EFaceAngleState State, FName LayerTag, int32 Index)
{
    FWidgetUndoScope UndoScope(this, TEXT("Remove Nested Element"));
    if (!ValidatePreset()) return;
    ActivePreset->RemoveNestedElement(State, LayerTag, Index);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::SetNestedTextures(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceTextureSet& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Nested Textures"));
    if (!ValidatePreset()) return;
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    Elem.Textures = Textures;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Elem);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetNestedTextures(State, LayerTag, Index, Textures);
}

FFaceTextureSet UFaceParallaxEditorWidget::GetNestedTextures(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ValidatePreset()) return FFaceTextureSet();
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    return Elem.Textures;
}

void UFaceParallaxEditorWidget::SetNestedTransform(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceArtTransform& Transform)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Nested Transform"));
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetNestedTransform(State, LayerTag, Index, Transform);
}

FFaceArtTransform UFaceParallaxEditorWidget::GetNestedTransform(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ValidatePreset()) return FFaceArtTransform();
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    return Elem.RelativeTransform;
}

void UFaceParallaxEditorWidget::SetNestedPivot(EFaceAngleState State, FName LayerTag, int32 Index, FVector2D Pivot)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Nested Pivot"));
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetNestedPivot(State, LayerTag, Index, Pivot);
}

FVector2D UFaceParallaxEditorWidget::GetNestedPivot(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ValidatePreset()) return FVector2D(0.5f, 0.5f);
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    return Elem.PivotPoint;
}

void UFaceParallaxEditorWidget::SetNestedJiggleEnabled(EFaceAngleState State, FName LayerTag, int32 Index, bool bEnabled)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Nested Jiggle Enabled"));
    if (!ValidatePreset()) return;
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    Elem.bJiggleEnabled = bEnabled;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Elem);
}

void UFaceParallaxEditorWidget::SetNestedJiggleSettings(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceJiggleSettings& Settings)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Nested Jiggle Settings"));
    if (!ValidatePreset()) return;
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    Elem.JiggleSettings = Settings;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Elem);
}

FFaceJiggleSettings UFaceParallaxEditorWidget::GetNestedJiggleSettings(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ValidatePreset()) return FFaceJiggleSettings();
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    return Elem.JiggleSettings;
}

void UFaceParallaxEditorWidget::SetNestedVisibility(EFaceAngleState State, FName LayerTag, FName ElementName, EFaceAngleState ViewState, bool bVisible)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Nested Visibility"));
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetNestedVisibility(State, LayerTag, ElementName, ViewState, bVisible);
}

bool UFaceParallaxEditorWidget::GetNestedVisibility(EFaceAngleState State, FName LayerTag, FName ElementName, EFaceAngleState ViewState) const
{
    if (!ValidatePreset()) return true;
    int32 Count = ActivePreset->GetNestedElementCount(State, LayerTag);
    for (int32 i = 0; i < Count; ++i)
    {
        FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, i);
        if (Elem.ElementName == ElementName)
        {
            const bool* bVis = Elem.ViewVisibility.Find(ViewState);
            return bVis ? *bVis : true;
        }
    }
    return true;
}

void UFaceParallaxEditorWidget::SetNestedIdleFrames(EFaceAngleState State, FName LayerTag, int32 Index, const TArray<FFaceTextureSet>& Frames)
{
    FWidgetUndoScope UndoScope(this, TEXT("Set Nested Idle Frames"));
    if (!ValidatePreset()) return;
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    Elem.IdleFrames = Frames;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Elem);
}

TArray<FFaceTextureSet> UFaceParallaxEditorWidget::GetNestedIdleFrames(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ValidatePreset()) return TArray<FFaceTextureSet>();
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    return Elem.IdleFrames;
}

void UFaceParallaxEditorWidget::ClearNestedIdleFrames(EFaceAngleState State, FName LayerTag, int32 Index)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear Nested Idle Frames"));
    if (!ValidatePreset()) return;
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    Elem.IdleFrames.Empty();
    ActivePreset->SetNestedElement(State, LayerTag, Index, Elem);
}

// ====================================================================
// PRESET QUERIES
// ====================================================================

bool UFaceParallaxEditorWidget::HasSlot(EFaceAngleState State, FName LayerTag) const
{
    return ValidatePreset() && ActivePreset->HasSlot(State, LayerTag);
}

bool UFaceParallaxEditorWidget::IsSlotFullyAssigned(EFaceAngleState State, FName LayerTag) const
{
    if (!ValidatePreset()) return false;
    return ActivePreset->GetTexturesForSlot(State, LayerTag).IsFullyAssigned();
}

void UFaceParallaxEditorWidget::ClearState(EFaceAngleState State)
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear State"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearState(State);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAll()
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear All"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearAll();

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

// ====================================================================
// TEXTUREANDTRANSFORMPARAMS (Set functions)
// ====================================================================

void UFaceParallaxEditorWidget::SetAlbedoParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->AlbedoParamName = Name;
}

void UFaceParallaxEditorWidget::SetNormalParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->NormalParamName = Name;
}

void UFaceParallaxEditorWidget::SetDepthParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->DepthParamName = Name;
}

void UFaceParallaxEditorWidget::SetAlbedoPrevParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->AlbedoPrevParamName = Name;
}

void UFaceParallaxEditorWidget::SetNormalPrevParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->NormalPrevParamName = Name;
}

void UFaceParallaxEditorWidget::SetDepthPrevParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->DepthPrevParamName = Name;
}

void UFaceParallaxEditorWidget::SetArtPositionParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ArtPositionParamName = Name;
}

void UFaceParallaxEditorWidget::SetArtScaleParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ArtScaleParamName = Name;
}

void UFaceParallaxEditorWidget::SetArtRotationParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ArtRotationParamName = Name;
}

// ====================================================================
// TRANSFORM (read-back accessors)
// ====================================================================

FVector2D UFaceParallaxEditorWidget::GetLayerPosition(EFaceAngleState State, FName LayerTag) const
{
    return GetLayerCanonicalTransform(State, LayerTag).Position;
}

FVector2D UFaceParallaxEditorWidget::GetLayerScale(EFaceAngleState State, FName LayerTag) const
{
    return GetLayerCanonicalTransform(State, LayerTag).Scale;
}

float UFaceParallaxEditorWidget::GetLayerRotation(EFaceAngleState State, FName LayerTag) const
{
    return GetLayerCanonicalTransform(State, LayerTag).Rotation;
}

// ====================================================================
// PRESET (batch operations)
// ====================================================================

void UFaceParallaxEditorWidget::BatchSetTextures(EFaceAngleState State, FName LayerTag, const TArray<FFaceTextureSet>& Textures)
{
    FWidgetUndoScope UndoScope(this, TEXT("Batch Set Textures"));
    if (!ValidatePreset()) return;
    ActivePreset->BatchSetTextures(State, LayerTag, Textures);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllTextures()
{
    FWidgetUndoScope UndoScope(this, TEXT("Clear All Textures"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearAllTextures();

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::DuplicateState(EFaceAngleState SourceState, EFaceAngleState DestState)
{
    FWidgetUndoScope UndoScope(this, TEXT("Duplicate State"));
    if (!ValidatePreset()) return;
    ActivePreset->DuplicateState(SourceState, DestState);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

// ====================================================================
// EXPRESSION (Set functions)
// ====================================================================

void UFaceParallaxEditorWidget::SetExpressionBlendParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ExpressionBlendParamName = Name;
}

void UFaceParallaxEditorWidget::SetExpressionAlbedoPrevParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ExpressionAlbedoPrevParamName = Name;
}

void UFaceParallaxEditorWidget::SetExpressionNormalPrevParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ExpressionNormalPrevParamName = Name;
}

void UFaceParallaxEditorWidget::SetExpressionDepthPrevParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ExpressionDepthPrevParamName = Name;
}

// ====================================================================
// PARAMETER (extended param name accessors)
// ====================================================================

void UFaceParallaxEditorWidget::SetParamBlendParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ParamBlendParamName = Name;
}

FName UFaceParallaxEditorWidget::GetParamBlendParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ParamBlendParamName : FName("ParamBlendAlpha");
}

void UFaceParallaxEditorWidget::SetParamAltAlbedoParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ParamAltAlbedoParamName = Name;
}

FName UFaceParallaxEditorWidget::GetParamAltAlbedoParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ParamAltAlbedoParamName : FName("AltAlbedoTexture");
}

void UFaceParallaxEditorWidget::SetParamAltNormalParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ParamAltNormalParamName = Name;
}

FName UFaceParallaxEditorWidget::GetParamAltNormalParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ParamAltNormalParamName : FName("AltNormalTexture");
}

void UFaceParallaxEditorWidget::SetParamAltDepthParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ParamAltDepthParamName = Name;
}

FName UFaceParallaxEditorWidget::GetParamAltDepthParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->ParamAltDepthParamName : FName("AltDepthTexture");
}

// ====================================================================
// SWOOSH (extended accessors)
// ====================================================================

void UFaceParallaxEditorWidget::SetSwooshFrameDuration(float Duration)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshFrameDuration(Duration);
}

float UFaceParallaxEditorWidget::GetSwooshFrameDuration() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshFrameDuration() : 0.033f;
}

void UFaceParallaxEditorWidget::SetSwooshBlendOutDuration(float Duration)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshBlendOutDuration(Duration);
}

float UFaceParallaxEditorWidget::GetSwooshBlendOutDuration() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshBlendOutDuration() : 0.15f;
}

void UFaceParallaxEditorWidget::SetSwooshLayerBlendParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshLayerBlendParamName(Name);
}

FName UFaceParallaxEditorWidget::GetSwooshLayerBlendParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshLayerBlendParamName() : FName("SwooshLayerBlend");
}

void UFaceParallaxEditorWidget::SetSwooshIntensityParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshIntensityParamName(Name);
}

FName UFaceParallaxEditorWidget::GetSwooshIntensityParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshIntensityParamName() : FName("SwooshIntensity");
}

void UFaceParallaxEditorWidget::SetSwooshAngleParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshAngleParamName(Name);
}

FName UFaceParallaxEditorWidget::GetSwooshAngleParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshAngleParamName() : FName("SwooshAngle");
}

void UFaceParallaxEditorWidget::SetSwooshSizeParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshSizeParamName(Name);
}

FName UFaceParallaxEditorWidget::GetSwooshSizeParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshSizeParamName() : FName("SwooshSize");
}

void UFaceParallaxEditorWidget::SetSwooshTextureParamName(FName Name)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetSwooshTextureParamName(Name);
}

FName UFaceParallaxEditorWidget::GetSwooshTextureParamName() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->GetSwooshTextureParamName() : FName("SwooshTexture");
}

// ====================================================================
// DEBUG OVERLAYS (material debug mode)
// ====================================================================

void UFaceParallaxEditorWidget::SetEnableMaterialDebugMode(bool bEnabled)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetEnableMaterialDebugMode(bEnabled);
}

bool UFaceParallaxEditorWidget::GetEnableMaterialDebugMode() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp && Comp->GetEnableMaterialDebugMode();
}

// ====================================================================
// STATUS (extended)
// ====================================================================

int32 UFaceParallaxEditorWidget::GetStateTextureCount() const
{
    if (!ValidatePreset()) return 0;
    int32 Count = 0;
    for (const auto& StatePair : ActivePreset->ViewAssignments)
    {
        for (const auto& LayerPair : StatePair.Value.Layers)
        {
            if (LayerPair.Value.Textures.Albedo) ++Count;
            if (LayerPair.Value.Textures.Normal) ++Count;
            if (LayerPair.Value.Textures.Depth) ++Count;
        }
    }
    return Count;
}

FString UFaceParallaxEditorWidget::GetStatusDetails() const
{
    if (!ValidatePreset()) return TEXT("No preset assigned");

    int32 AssignedStates = ActivePreset->GetAssignedStates().Num();
    int32 TotalSlots = ActivePreset->GetTotalAssignedSlots();
    int32 TextureCount = 0;
    int32 FullyAssigned = 0;

    for (const auto& StatePair : ActivePreset->ViewAssignments)
    {
        for (const auto& LayerPair : StatePair.Value.Layers)
        {
            const FFaceTextureSet& Tex = LayerPair.Value.Textures;
            if (Tex.Albedo) ++TextureCount;
            if (Tex.Normal) ++TextureCount;
            if (Tex.Depth) ++TextureCount;
            if (Tex.IsFullyAssigned()) ++FullyAssigned;
        }
    }

    int32 Layers = GetLayerCount();
    UFaceParallaxComponent* Comp = GetParallaxComponent();

    FString BlinkStr = (Comp && Comp->bBlinkingEnabled) ? TEXT("On") : TEXT("Off");
    FString SwooshStr = (Comp && Comp->bSwooshEnabled) ? TEXT("On") : TEXT("Off");
    FString NestedStr = (Comp && Comp->bNestedArtEnabled) ? TEXT("On") : TEXT("Off");
    FString ParamsStr = (Comp && Comp->bParamsEnabled) ? TEXT("On") : TEXT("Off");

    return FString::Printf(TEXT("States: %d/10 | Layers: %d | Slots: %d | Textures: %d | Fully: %d | Blink: %s | Swoosh: %s | Nested: %s | Params: %s"),
        AssignedStates, Layers, TotalSlots, TextureCount, FullyAssigned,
        *BlinkStr, *SwooshStr, *NestedStr, *ParamsStr);
}

// ====================================================================
// NESTED ART (batch operations)
// ====================================================================

void UFaceParallaxEditorWidget::BatchSetNestedTexturesAllViews(FName LayerTag, FName ElementName, const FFaceTextureSet& Textures)
{
    if (!ValidatePreset()) return;

    const EFaceAngleState AllStates[] = {
        EFaceAngleState::Front,
        EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile,
        EFaceAngleState::BackRight,
        EFaceAngleState::Back,
        EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile,
        EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top,
        EFaceAngleState::Bottom
    };

    for (EFaceAngleState State : AllStates)
    {
        int32 Count = ActivePreset->GetNestedElementCount(State, LayerTag);
        for (int32 i = 0; i < Count; ++i)
        {
            FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, i);
            if (Elem.ElementName == ElementName)
            {
                Elem.Textures = Textures;
                ActivePreset->SetNestedElement(State, LayerTag, i, Elem);
                break;
            }
        }
    }

    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::DuplicateNestedElement(EFaceAngleState State, FName LayerTag, int32 SourceIndex, int32 DestIndex)
{
    if (!ValidatePreset()) return;
    FFaceNestedArt Source = ActivePreset->GetNestedElement(State, LayerTag, SourceIndex);
    ActivePreset->SetNestedElement(State, LayerTag, DestIndex, Source);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::SyncNestedToAllViews(FName LayerTag, FName ElementName)
{
    if (!ValidatePreset()) return;

    int32 Count = ActivePreset->GetNestedElementCount(EFaceAngleState::Front, LayerTag);
    for (int32 i = 0; i < Count; ++i)
    {
        FFaceNestedArt Elem = ActivePreset->GetNestedElement(EFaceAngleState::Front, LayerTag, i);
        if (Elem.ElementName == ElementName)
        {
            ActivePreset->SyncLayerNestedToAllViews(LayerTag, ElementName, Elem);
            break;
        }
    }

    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}
#endif
