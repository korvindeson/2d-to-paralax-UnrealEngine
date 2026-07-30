#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxPreviewActor.h"
#include "FaceParallaxComponent.h"
#include "FaceParallaxPreset.h"
#include "DepthDebugVisualizerComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"

#if WITH_EDITOR
#include "UObject/SavePackage.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Misc/FileHelper.h"
#include "Editor.h"

bool UFaceParallaxEditorWidget::ValidatePreset() const
{
    if (!ActivePreset)
    {
        if (!HasAnyFlags(RF_ClassDefaultObject))
        {
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No ActivePreset assigned."));
        }
        return false;
    }
    return true;
}

bool UFaceParallaxEditorWidget::ValidatePreviewActor() const
{
    if (!PreviewActor)
    {
        if (!HasAnyFlags(RF_ClassDefaultObject))
        {
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No PreviewActor assigned."));
        }
        return false;
    }
    return true;
}

UFaceParallaxComponent* UFaceParallaxEditorWidget::GetParallaxComponent() const
{
    return ValidatePreviewActor() ? PreviewActor->FaceParallax : nullptr;
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

UFaceParallaxPreset* UFaceParallaxEditorWidget::CreateNewPreset(const FString& AssetName,
    const FString& PackagePath)
{
    UPackage* Package = CreatePackage(*(PackagePath / AssetName));
    UFaceParallaxPreset* NewPreset = NewObject<UFaceParallaxPreset>(Package, FName(*AssetName),
        RF_Public | RF_Standalone);
    if (NewPreset)
    {
        NewPreset->CanvasSize = FVector2D(512.0f, 512.0f);
        NewPreset->bAutoFitOnAssign = true;
        NewPreset->MarkPackageDirty();
        ActivePreset = NewPreset;
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
}

EFaceAngleState UFaceParallaxEditorWidget::GetActiveViewState() const
{
    return ActiveViewState;
}

TArray<EFaceAngleState> UFaceParallaxEditorWidget::GetAssignedStates() const
{
    return ValidatePreset() ? ActivePreset->GetAssignedStates() : TArray<EFaceAngleState>();
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
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->LayerDefinitions.Num() : 0;
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
    ActivePreset->SetCanonicalTransform(State, LayerTag, T);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SetLayerScale(EFaceAngleState State, FName LayerTag,
    float X, float Y)
{
    if (!ValidatePreset()) return;
    FFaceArtTransform T = ActivePreset->GetSlot(State, LayerTag).CanonicalTransform;
    T.Scale = FVector2D(FMath::Max(0.01f, X), FMath::Max(0.01f, Y));
    ActivePreset->SetCanonicalTransform(State, LayerTag, T);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SetLayerRotation(EFaceAngleState State, FName LayerTag,
    float Degrees)
{
    if (!ValidatePreset()) return;
    FFaceArtTransform T = ActivePreset->GetSlot(State, LayerTag).CanonicalTransform;
    T.Rotation = Degrees;
    ActivePreset->SetCanonicalTransform(State, LayerTag, T);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SetLayerTransform(EFaceAngleState State, FName LayerTag,
    const FFaceArtTransform& Transform)
{
    if (!ValidatePreset()) return;
    ActivePreset->SetCanonicalTransform(State, LayerTag, Transform);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ResetLayerTransform(EFaceAngleState State, FName LayerTag)
{
    if (!ValidatePreset()) return;
    ActivePreset->SetCanonicalTransform(State, LayerTag, FFaceArtTransform());

    if (PreviewActor && PreviewActor->FaceParallax)
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

    if (PreviewActor && PreviewActor->FaceParallax)
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

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncLayerToAllViews(EFaceAngleState State, FName LayerTag)
{
    if (!ValidatePreset()) return;
    ActivePreset->SyncCanonicalToAllViews(State, LayerTag);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncAllLayersToAllViews()
{
    if (!ValidatePreset()) return;

    for (const auto& StatePair : ActivePreset->ViewAssignments)
    {
        for (const auto& LayerPair : StatePair.Value.Layers)
        {
            ActivePreset->SyncCanonicalToAllViews(StatePair.Key, LayerPair.Key);
        }
    }

    if (PreviewActor && PreviewActor->FaceParallax)
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
    if (!ValidatePreset()) return;
    ActivePreset->SetViewOverride(State, LayerTag, OverrideView, Override);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearViewOverride(EFaceAngleState State, FName LayerTag,
    EFaceAngleState OverrideView)
{
    if (!ValidatePreset()) return;
    ActivePreset->ClearViewOverride(State, LayerTag, OverrideView);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag)
{
    if (!ValidatePreset()) return;
    ActivePreset->ClearAllOverridesForSlot(State, LayerTag);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllOverrides()
{
    if (!ValidatePreset()) return;
    ActivePreset->ClearAllOverrides();

    if (PreviewActor && PreviewActor->FaceParallax)
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
    if (!ValidatePreset()) return;
    ActivePreset->SetTexturesForSlot(State, LayerTag, Textures);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

FVector2D UFaceParallaxEditorWidget::GetSlotSourceSize(EFaceAngleState State,
    FName LayerTag) const
{
    if (!ValidatePreset() || !PreviewActor) return FVector2D::ZeroVector;
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
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    return Comp ? Comp->LayerDefinitions.Num() : 0;
}

FString UFaceParallaxEditorWidget::GetStatusString() const
{
    if (!ValidatePreset()) return TEXT("No preset assigned");

    int32 AssignedStates = ActivePreset->GetAssignedStates().Num();
    int32 TotalSlots = ActivePreset->GetTotalAssignedSlots();
    int32 Layers = 0;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Layers = Comp->LayerDefinitions.Num();

    FString BlinkStr = (Comp && Comp->bBlinkingEnabled) ? TEXT("On") : TEXT("Off");

    FString ExprStr = TEXT("Neutral");
    if (Comp)
    {
        switch (Comp->CurrentExpression)
        {
            case EExpression::Neutral: ExprStr = TEXT("Neutral"); break;
            case EExpression::Smile:   ExprStr = TEXT("Smile");   break;
            case EExpression::Frown:   ExprStr = TEXT("Frown");   break;
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
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.ExpressionTextures.Remove(Expression);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

void UFaceParallaxEditorWidget::SetExpressionTextures(EFaceAngleState State, FName LayerTag,
    EExpression Expression, const FFaceTextureSet& Textures)
{
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
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.VisemeFrameSets.Remove(Expression);
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
    if (!ValidatePreset()) return;
    ActivePreset->SetParamBindings(State, LayerTag, Bindings);

    if (PreviewActor && PreviewActor->FaceParallax)
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
    if (!ValidatePreset()) return;
    ActivePreset->SetAltTextures(State, LayerTag, Textures);

    if (PreviewActor && PreviewActor->FaceParallax)
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
    if (!ValidatePreset()) return;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Element);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::AddNestedElement(EFaceAngleState State, FName LayerTag, const FFaceNestedArt& Element)
{
    if (!ValidatePreset()) return;
    ActivePreset->AddNestedElement(State, LayerTag, Element);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::RemoveNestedElement(EFaceAngleState State, FName LayerTag, int32 Index)
{
    if (!ValidatePreset()) return;
    ActivePreset->RemoveNestedElement(State, LayerTag, Index);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::SetNestedTextures(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceTextureSet& Textures)
{
    if (!ValidatePreset()) return;
    ActivePreset->SetNestedElement(State, LayerTag, Index, ActivePreset->GetNestedElement(State, LayerTag, Index));
    // Must use the component function to set specific sub-field
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
    if (!ValidatePreset()) return;
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    Elem.bJiggleEnabled = bEnabled;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Elem);
}

void UFaceParallaxEditorWidget::SetNestedJiggleSettings(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceJiggleSettings& Settings)
{
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
    if (!ValidatePreset()) return;
    ActivePreset->ClearState(State);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAll()
{
    if (!ValidatePreset()) return;
    ActivePreset->ClearAll();

    if (PreviewActor && PreviewActor->FaceParallax)
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
    if (!ValidatePreset()) return;
    ActivePreset->BatchSetTextures(State, LayerTag, Textures);

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllTextures()
{
    if (!ValidatePreset()) return;
    ActivePreset->ClearAllTextures();

    if (PreviewActor && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::DuplicateState(EFaceAngleState SourceState, EFaceAngleState DestState)
{
    if (!ValidatePreset()) return;
    ActivePreset->DuplicateState(SourceState, DestState);

    if (PreviewActor && PreviewActor->FaceParallax)
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

    int32 Layers = 0;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Layers = Comp->LayerDefinitions.Num();

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

// ====================================================================
// HELPERS
// ====================================================================

static FLinearColor EditorBg(float L) { return FLinearColor(L, L, L); }
static FLinearColor AccentBlue() { return FLinearColor(0.35f, 0.55f, 1.0f); }
static FLinearColor AccentGreen() { return FLinearColor(0.3f, 0.8f, 0.3f); }

// Static helper functions — accessible from both RebuildWidget and Refresh* methods
static TSharedRef<STextBlock> MakeLbl(const FString& T, int32 S, const FLinearColor& C = FLinearColor(0.8f,0.8f,0.8f))
{
    return SNew(STextBlock)
        .Text(FText::FromString(T))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", S))
        .ColorAndOpacity(C);
}

static TSharedRef<SButton> MakeBtn(const FString& T, TFunction<void()>&& Fn,
    const FLinearColor& FG = FLinearColor(0.85f,0.85f,0.85f),
    const FLinearColor& BG = FLinearColor(0.15f,0.15f,0.15f))
{
    return SNew(SButton)
        .OnClicked_Lambda([Fn = MoveTemp(Fn)](){ Fn(); return FReply::Handled(); })
        .ButtonColorAndOpacity(BG)
        .Content()
        [SNew(STextBlock)
            .Text(FText::FromString(T))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
            .ColorAndOpacity(FG)];
}

static TSharedRef<SCheckBox> MakeToggle(bool Def, TFunction<void(bool)>&& Fn)
{
    return SNew(SCheckBox)
        .IsChecked(Def ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
        .OnCheckStateChanged_Lambda([Fn = MoveTemp(Fn)](ECheckBoxState S)
        { Fn(S == ECheckBoxState::Checked); });
}

static auto MakeSectionLbl(const FString& T, int32 S) -> TSharedRef<STextBlock>
{
    return SNew(STextBlock)
        .Text(FText::FromString(T))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", S > 0 ? S : 10))
        .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f));
}

// ====================================================================
// REBUILDWIDGET — Full editor layout
// ====================================================================

TSharedRef<SWidget> UFaceParallaxEditorWidget::RebuildWidget()
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    TArray<FName> LNames;
    if (Comp)
    {
        for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
        {
            FName Tag = Comp->GetLayerDefinition(i).LayerTag;
            if (Tag.IsValid()) LNames.Add(Tag);
        }
    }
    if (LNames.Num() == 0)
        LNames = { FName("Eyes"), FName("Brows"), FName("Mouth"), FName("Hair") };
    if (!SelectedLayerName.IsValid() && LNames.Num() > 0) SelectedLayerName = LNames[0];
    LayerNames = LNames;

    // ========================
    // LAMBDAS (RebuildWidget-only helpers)
    // ========================

    auto MakeSlider = [](float Val, TFunction<void(float)>&& Fn) -> TSharedRef<SSlider>
    {
        return SNew(SSlider).Value(Val)
            .OnValueChanged_Lambda(MoveTemp(Fn));
    };

    auto MakeSectionBox = [](const FString& Title, TSharedRef<SWidget> Content) -> TSharedRef<SWidget>
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(4,6,4,2))
                [SNew(STextBlock)
                    .Text(FText::FromString(Title))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                    .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(4,0,4,4))
                [SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.08f,0.08f,0.08f))
                    .Padding(FMargin(6))
                    [Content]];
    };

    auto MakeVisibleBtn = [this](FName Tag) -> TSharedRef<SWidget>
    {
        return SNew(SCheckBox)
            .IsChecked(ECheckBoxState::Checked)
            .OnCheckStateChanged_Lambda([this, Tag](ECheckBoxState S)
            {
                int32 Idx = GetLayerIndex(Tag);
                if (Idx >= 0)
                {
                    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(ActiveViewState, Tag);
                    // Toggle visibility not directly on slot — use nested art flags or skip
                    RefreshUI();
                }
            });
    };

    // ========================
    // ROOT
    // ========================

    TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

    // ========================
    // 1. TOOLBAR
    // ========================

    {
        TSharedRef<SHorizontalBox> TB = SNew(SHorizontalBox);
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("New Preset"), [this]()
            {
                FString AssetName = TEXT("MyPreset");
                FString Path = TEXT("/Game/FaceParallax/Presets");
                CreateNewPreset(*AssetName, *Path);
                if (TextStatus.IsValid())
                    TextStatus->SetText(FText::FromString(FString::Printf(TEXT("Created '%s'"), *AssetName)));
                RefreshUI();
            }, FLinearColor(0.7f,0.9f,1.0f))];
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Save"), [this](){ SavePreset(); })];
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Import Art..."), [this]()
            {
                TArray<FString> OutFiles;
                IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
                if (Platform)
                {
                    FString DefaultPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::UNR);
                    if (DefaultPath.IsEmpty()) DefaultPath = FPaths::ProjectContentDir();
                    uint32 FilterFlags = 0;
                    if (Platform->OpenFileDialog(nullptr, TEXT("Import Texture"),
                        DefaultPath, TEXT(""),
                        TEXT("Image files (*.png, *.jpg, *.tga)|*.png;*.jpg;*.tga|All files (*.*)|*.*"),
                        FilterFlags, OutFiles))
                    {
                        if (OutFiles.Num() > 0)
                        {
                            FEditorDirectories::Get().SetLastDirectory(ELastDirectory::UNR, FPaths::GetPath(OutFiles[0]));
                            FString Msg = FString::Printf(TEXT("Selected: %s"), *OutFiles[0]);
                            UE_LOG(LogTemp, Log, TEXT("[FaceParallaxEditorWidget] %s"), *Msg);
                            if (TextStatus.IsValid())
                                TextStatus->SetText(FText::FromString(Msg));
                        }
                    }
                }
            })];
        TB->AddSlot().FillWidth(1.0f);
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("?"), [this]()
            {
                FText Msg = FText::FromString(TEXT(
                    "Face Parallax Editor\n\n"
                    "1. Select a layer in the Layers panel.\n"
                    "2. Assign textures via Pick Albedo/Normal/Depth buttons\n"
                    "   (select a texture in Content Browser first).\n"
                    "3. Adjust Position/Scale/Rotation in Properties.\n"
                    "4. Use Timeline to author Blink/Swoosh animation frames.\n"
                    "5. Save Preset when done.\n\n"
                    "Nested Art pins: add nested elements, enable Pinned,\n"
                    "set Pin X/Y/Z to define 3D attachment points."
                ));
                if (HelpWindow.IsValid())
                    HelpWindow->SetVisibility(EVisibility::Collapsed);
                else
                    UE_LOG(LogTemp, Log, TEXT("[FaceParallaxEditorWidget] %s"), *Msg.ToString());
                if (TextStatus.IsValid())
                    TextStatus->SetText(Msg);
            }, FLinearColor(0.7f,0.7f,0.7f))];
        Root->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.12f,0.12f,0.12f))
                .Padding(FMargin(4,3))
                [TB]];
    }

    // ========================
    // 2. VIEW STATE STRIP
    // ========================

    {
        TSharedRef<SHorizontalBox> StateBar = SNew(SHorizontalBox);
        struct { EFaceAngleState S; const TCHAR* T; FLinearColor C; } States[] = {
            {EFaceAngleState::Front, TEXT("Front"), FLinearColor(0.6f,0.8f,1.0f)},
            {EFaceAngleState::ThreeQuarterRight, TEXT("3/4R"), FLinearColor(0.5f,0.7f,1.0f)},
            {EFaceAngleState::RightProfile, TEXT("ProfR"), FLinearColor(0.4f,0.6f,1.0f)},
            {EFaceAngleState::BackRight, TEXT("BackR"), FLinearColor(0.5f,0.5f,0.7f)},
            {EFaceAngleState::Back, TEXT("Back"), FLinearColor(0.4f,0.4f,0.6f)},
            {EFaceAngleState::BackLeft, TEXT("BackL"), FLinearColor(0.5f,0.5f,0.7f)},
            {EFaceAngleState::LeftProfile, TEXT("ProfL"), FLinearColor(0.4f,0.6f,1.0f)},
            {EFaceAngleState::ThreeQuarterLeft, TEXT("3/4L"), FLinearColor(0.5f,0.7f,1.0f)},
            {EFaceAngleState::Top, TEXT("Top"), FLinearColor(0.6f,1.0f,0.6f)},
            {EFaceAngleState::Bottom, TEXT("Bot"), FLinearColor(1.0f,0.6f,0.6f)},
        };
        for (auto& St : States)
        {
            EFaceAngleState S = St.S;
            bool IsActive = (S == ActiveViewState);
            StateBar->AddSlot().Padding(FMargin(1)).VAlign(VAlign_Fill).HAlign(HAlign_Fill)
                [SNew(SButton)
                    .ButtonColorAndOpacity(IsActive ? AccentBlue() : FLinearColor(0.12f,0.12f,0.12f))
                    .OnClicked_Lambda([this, S](){ SetActiveViewState(S); RefreshUI(); return FReply::Handled(); })
                    .Content()
                    [SNew(STextBlock)
                        .Text(FText::FromString(St.T))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", IsActive ? 11 : 9))
                        .ColorAndOpacity(IsActive ? FLinearColor(1,1,1) : St.C)]];
        }
        Root->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.1f,0.1f,0.1f))
                .Padding(FMargin(2,3))
                [SNew(SBox).HeightOverride(26)[StateBar]]];
    }

    // ========================
    // 2b. ZONE DIAGRAM
    // ========================

    {
        TSharedRef<SVerticalBox> ZoneCol = SNew(SVerticalBox);
        ZoneYawLabel = SNew(STextBlock)
            .Text(FText::FromString(TEXT("Yaw: 0.0°")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f));
        ZoneCol->AddSlot().AutoHeight().Padding(FMargin(2,0))
            [ZoneYawLabel.ToSharedRef()];
        ZonePitchLabel = SNew(STextBlock)
            .Text(FText::FromString(TEXT("Pitch: 0.0°")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f));
        ZoneCol->AddSlot().AutoHeight().Padding(FMargin(2,0))
            [ZonePitchLabel.ToSharedRef()];

        // Build zone diagram as colored horizontal strips
        ZoneDiagramWidget = SNew(SBox).HeightOverride(20);
        RebuildZoneDiagram();
        ZoneCol->AddSlot().AutoHeight().Padding(FMargin(2,1))
            [ZoneDiagramWidget.ToSharedRef()];

        Root->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.08f,0.08f,0.08f))
                .Padding(FMargin(4,2))
                [ZoneCol]];
    }

    // ========================
    // 3. MAIN AREA: LAYERS | PREVIEW+TEXTURES | PROPERTIES
    // ========================

    // --- 3a. LAYER PANEL (left) ---
    TSharedRef<SVerticalBox> LayerPanel = SNew(SVerticalBox);
    {
        LayerPanel->AddSlot().AutoHeight().Padding(FMargin(4,4,4,2))
            [MakeLbl(TEXT("LAYERS"), 10, FLinearColor(0.7f,0.7f,0.9f))];
        LayerScrollBox = SNew(SScrollBox)
            .Orientation(Orient_Vertical);
        LayerPanelBox = SNew(SVerticalBox);
        LayerScrollBox->AddSlot() [LayerPanelBox.ToSharedRef()];
        RefreshLayerList();
        LayerPanel->AddSlot().FillHeight(1.0f)
            [LayerScrollBox.ToSharedRef()];
        LayerPanel->AddSlot().AutoHeight().Padding(FMargin(4,2))
            [MakeBtn(TEXT("+ Add Layer"), [this]()
            {
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (Comp)
                {
                    int32 N = Comp->GetNumLayerDefinitions();
                    FName NewTag(*FString::Printf(TEXT("Layer_%d"), N));
                    FFaceLayerDef NewDef;
                    NewDef.LayerTag = NewTag;
                    Comp->AddLayerDefinition(NewDef);
                    LayerNames.Add(NewTag);
                    SelectedLayerName = NewTag;
                }
                RefreshUI();
            }, FLinearColor(0.6f,0.8f,0.6f), FLinearColor(0.08f,0.08f,0.08f))];
    }

    // --- 3b. PREVIEW + TEXTURES (center) ---
    TSharedRef<SVerticalBox> CenterCol = SNew(SVerticalBox);
    {
        // Preview
        PreviewImageWidget = SNew(SImage).Image(&PreviewBrush);
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(2))
            [SNew(SBox).HeightOverride(340)[PreviewImageWidget.ToSharedRef()]];

        // Layer label
        TextLayerName = SNew(STextBlock)
            .Text(FText::FromString(SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : TEXT("(no layer)")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            .ColorAndOpacity(FLinearColor(0.9f,0.9f,0.9f));
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(4,2,4,0))
            [TextLayerName.ToSharedRef()];

        // Texture thumbs row
        {
            TSharedRef<SHorizontalBox> ThumbRow = SNew(SHorizontalBox);
            auto MakeThumbCol = [&](const FString& Label,
                TSharedPtr<SImage>& ThumbOut, bool bHasImage) -> TSharedRef<SVerticalBox>
            {
                TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
                TSharedRef<SImage> Thumb = SNew(SImage)
                    .Image(FCoreStyle::Get().GetBrush("NoBorder"));
                Thumb->SetColorAndOpacity(FLinearColor(0.08f,0.08f,0.08f));
                ThumbOut = Thumb;
                Col->AddSlot().AutoHeight().Padding(FMargin(2))
                    [SNew(SBox).WidthOverride(72).HeightOverride(72)[Thumb]];
                FString L = Label;
                Col->AddSlot().AutoHeight().Padding(FMargin(2))
                    [MakeBtn(FString::Printf(TEXT("Pick %s"), *Label),
                        [this, L]()
                        {
                            FString Ch = L;
                            // Try to assign a loaded texture from the content browser selection
                            UTexture2D* Tex = GetSelectedContentBrowserTexture();
                            if (Tex && SelectedLayerName.IsValid())
                            {
                                FFaceTextureSet Cur = GetSlotTextures(ActiveViewState, SelectedLayerName);
                                if (Ch == TEXT("Albedo")) Cur.Albedo = Tex;
                                else if (Ch == TEXT("Normal")) Cur.Normal = Tex;
                                else if (Ch == TEXT("Depth")) Cur.Depth = Tex;
                                SetSlotTextures(ActiveViewState, SelectedLayerName, Cur);
                                if (bAutoFitOnAssign) ApplyAutoFit(ActiveViewState, SelectedLayerName);
                                RefreshUI();
                            }
                            else
                            {
                                UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No texture selected in CB or no layer"));
                            }
                        },
                        FLinearColor(0.6f,0.8f,1.0f), FLinearColor(0.1f,0.1f,0.1f))];
                Col->AddSlot().AutoHeight().Padding(FMargin(2))
                    [MakeBtn(TEXT("Clear"),
                        [this, L]()
                        {
                            if (!SelectedLayerName.IsValid()) return;
                            FString Ch = L;
                            FFaceTextureSet Cur = GetSlotTextures(ActiveViewState, SelectedLayerName);
                            if (Ch == TEXT("Albedo")) Cur.Albedo = nullptr;
                            else if (Ch == TEXT("Normal")) Cur.Normal = nullptr;
                            else if (Ch == TEXT("Depth")) Cur.Depth = nullptr;
                            SetSlotTextures(ActiveViewState, SelectedLayerName, Cur);
                            RefreshUI();
                        },
                        FLinearColor(1.0f,0.6f,0.6f), FLinearColor(0.1f,0.1f,0.1f))];
                return Col;
            };
            ThumbRow->AddSlot().Padding(FMargin(1))[MakeThumbCol(TEXT("Albedo"), ThumbAlbedo, true)];
            ThumbRow->AddSlot().Padding(FMargin(1))[MakeThumbCol(TEXT("Normal"), ThumbNormal, true)];
            ThumbRow->AddSlot().Padding(FMargin(1))[MakeThumbCol(TEXT("Depth"), ThumbDepth, true)];
            CenterCol->AddSlot().AutoHeight().Padding(FMargin(2))
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.06f,0.06f,0.06f))
                    .Padding(FMargin(2))
                    [ThumbRow]];
        }

        // Action buttons row
        {
            TSharedRef<SHorizontalBox> ActRow = SNew(SHorizontalBox);
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [MakeBtn(TEXT("Auto-Fit"), [this](){ if (SelectedLayerName.IsValid()) { ApplyAutoFit(ActiveViewState, SelectedLayerName); RefreshUI(); } })];
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [MakeBtn(TEXT("Reset"), [this](){ if (SelectedLayerName.IsValid()) { ResetLayerTransform(ActiveViewState, SelectedLayerName); RefreshUI(); } })];
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [MakeBtn(TEXT("Sync->All"), [this](){ SyncLayerToAllViews(ActiveViewState, SelectedLayerName); RefreshUI(); })];
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [SNew(SCheckBox)
                    .IsChecked(bAutoFitOnAssign ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { bool b = (S == ECheckBoxState::Checked); bAutoFitOnAssign = b; SetAutoFitOnAssign(b); })
                    [MakeLbl(TEXT("AF"), 9, FLinearColor(0.5f,0.7f,0.5f))]];
            CenterCol->AddSlot().AutoHeight().Padding(FMargin(2))
                [ActRow];
        }
    }

    // --- 3c. PROPERTIES PANEL (right) ---
    TSharedRef<SVerticalBox> PropPanel = SNew(SVerticalBox).Visibility(EVisibility::Visible);
    PropScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
    {
        // Search bar
        SearchBox = SNew(SSearchBox)
            .HintText(FText::FromString(TEXT("Search settings...")))
            .OnTextChanged_Lambda([this](const FText& T) { ApplySearchFilter(T.ToString()); });
        PropScroll->AddSlot()
            [MakeSectionBox(TEXT("Search"), SNew(SBox).HeightOverride(22)[SearchBox.ToSharedRef()])];

        // Cross-layer overlay
        CrossLayerBox = SNew(SVerticalBox);
        CrossLayerScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
        CrossLayerScroll->AddSlot() [CrossLayerBox.ToSharedRef()];
        TextCrossLayer = SNew(STextBlock)
            .Text(FText::FromString(TEXT("Select a layer to show overlay")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(FLinearColor(0.5f,0.5f,0.5f));
        CrossLayerBox->AddSlot().AutoHeight().Padding(FMargin(2))
            [TextCrossLayer.ToSharedRef()];
        PropScroll->AddSlot()
            [MakeSectionBox(TEXT("All Layers (current state)"), CrossLayerScroll.ToSharedRef())];
        // Transform section
        {
            TSharedRef<SVerticalBox> XForm = SNew(SVerticalBox);
            auto AddNumRow = [&](const FString& Label,
                TSharedPtr<SEditableTextBox>& EditOut,
                TFunction<void(float)>&& OnCommit)
            {
                TSharedRef<SEditableTextBox> Edit = SNew(SEditableTextBox)
                    .Text(FText::FromString(TEXT("0.00")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .OnTextCommitted_Lambda([OnCommit = MoveTemp(OnCommit)]
                        (const FText& T, ETextCommit::Type)
                        { float V = FCString::Atof(*T.ToString()); OnCommit(V); });
                EditOut = Edit;
                TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                R->AddSlot().Padding(FMargin(0,2)).VAlign(VAlign_Center).AutoWidth()
                    [MakeLbl(Label, 9)];
                R->AddSlot().Padding(FMargin(4,2)).VAlign(VAlign_Center).FillWidth(1.0f)
                    [SNew(SBox).WidthOverride(70)[Edit]];
                XForm->AddSlot().AutoHeight()[R];
            };
            AddNumRow(TEXT("Pos X"), EditPosX, [this](float V){ if (SelectedLayerName.IsValid()) SetLayerPosition(ActiveViewState, SelectedLayerName, V, 0.0f); });
            AddNumRow(TEXT("Pos Y"), EditPosY, [this](float V){ if (SelectedLayerName.IsValid()) SetLayerPosition(ActiveViewState, SelectedLayerName, 0.0f, V); });
            AddNumRow(TEXT("Scale X"), EditScaleX, [this](float V){ if (SelectedLayerName.IsValid()) SetLayerScale(ActiveViewState, SelectedLayerName, V, 1.0f); });
            AddNumRow(TEXT("Scale Y"), EditScaleY, [this](float V){ if (SelectedLayerName.IsValid()) SetLayerScale(ActiveViewState, SelectedLayerName, 1.0f, V); });
            AddNumRow(TEXT("Rot"), EditRot, [this](float V){ if (SelectedLayerName.IsValid()) SetLayerRotation(ActiveViewState, SelectedLayerName, V); });
            PropScroll->AddSlot()
                [MakeSectionBox(TEXT("Transform"), XForm)];
        }

        // Camera section
        {
            TSharedRef<SVerticalBox> Cam = SNew(SVerticalBox);
            auto AddCamSlider = [&](const FString& Label, float Def, float Mn, float Mx,
                TFunction<void(float)>&& Fn)
            {
                TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                R->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeLbl(Label, 9)];
                R->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)
                    [SNew(SSlider).Value((Def - Mn) / (Mx - Mn))
                        .OnValueChanged_Lambda([Fn = MoveTemp(Fn), Mn, Mx](float V)
                        { Fn(Mn + V * (Mx - Mn)); })];
                Cam->AddSlot().AutoHeight()[R];
            };
            AddCamSlider(TEXT("Yaw"), 0, -180, 180, [this](float V){ SetOrbitYaw(V); });
            AddCamSlider(TEXT("Pitch"), -15, -89, 89, [this](float V){ SetOrbitPitch(V); });
            AddCamSlider(TEXT("Dist"), 180, 50, 500, [this](float V){ SetOrbitDistance(V); });
            TSharedRef<SHorizontalBox> ARow = SNew(SHorizontalBox);
            ARow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                [SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { if (ValidatePreviewActor()) PreviewActor->SetAutoRotate(S == ECheckBoxState::Checked); })];
            ARow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [MakeLbl(TEXT("Auto"), 9)];
            ARow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)
                [SNew(SSlider).Value(30.0f / 360.0f)
                    .OnValueChanged_Lambda([this](float V){ SetAutoRotateSpeed(V * 359.0f + 1.0f); })];
            ARow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                [MakeLbl(TEXT("Spd"), 9)];
            Cam->AddSlot().AutoHeight()[ARow];
            PropScroll->AddSlot()
                [MakeSectionBox(TEXT("Camera"), Cam)];
        }

        // Config section
        {
            CfgBox = SNew(SVerticalBox);

            auto AddConfigCheck = [&](const FString& Label, bool bDef,
                TSharedPtr<SCheckBox>& CheckOut,
                TFunction<void(bool)>&& Fn)
            {
                TSharedRef<SCheckBox> Ch = SNew(SCheckBox)
                    .IsChecked(bDef ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([Fn = MoveTemp(Fn)](ECheckBoxState S)
                    { Fn(S == ECheckBoxState::Checked); });
                CheckOut = Ch;
                TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                R->AddSlot().Padding(FMargin(0,2)).AutoWidth()[Ch];
                R->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeLbl(Label, 9)];
                CfgBox->AddSlot().AutoHeight()[R];
            };

            UFaceParallaxComponent* Comp2 = GetParallaxComponent();
            bool bBlink = Comp2 ? Comp2->GetBlinkingEnabled() : false;
            bool bSw = Comp2 ? Comp2->GetSwooshEnabled() : false;
            bool bNA = Comp2 ? Comp2->GetNestedArtEnabled() : false;
            bool bPar = Comp2 ? Comp2->GetParamsEnabled() : false;
            // Debug overlay defaults: not exposed as bool properties on PreviewActor,
            // tracked locally via the respective checkbox state.
            bool bShowTex = bLocalShowTextures, bDepthMesh = bLocalShowDepthMesh;
            bool bWire = bLocalShowWireframe, bColor = bLocalColorByDepth;

            AddConfigCheck(TEXT("Blinking"), bBlink, CheckBlinking,
                [this](bool b){ SetBlinkingEnabled(b); });
            AddConfigCheck(TEXT("Swoosh"), bSw, CheckSwoosh,
                [this](bool b){ SetSwooshEnabled(b); });
            AddConfigCheck(TEXT("Nested Art"), bNA, CheckNestedArt,
                [this](bool b){ SetNestedArtEnabled(b); });
            AddConfigCheck(TEXT("Params"), bPar, CheckParams,
                [this](bool b){ SetParamsEnabled(b); });
            AddConfigCheck(TEXT("Show Textures"), bShowTex, CheckShowTextures,
                [this](bool b){ ShowTextures(b); });
            AddConfigCheck(TEXT("Depth Mesh"), bDepthMesh, CheckDepthMesh,
                [this](bool b){ ShowDepthMesh(b); });
            AddConfigCheck(TEXT("Wireframe"), bWire, CheckWireframe,
                [this](bool b){ ShowWireframe(b); });
            AddConfigCheck(TEXT("Color by Depth"), bColor, CheckColorByDepth,
                [this](bool b){ ColorByDepth(b); });

            PropScroll->AddSlot()
                [MakeSectionBox(TEXT("Config"), CfgBox.ToSharedRef())];
        }

        // Blend preview section
        {
            TSharedRef<SVerticalBox> BlendBox = SNew(SVerticalBox);
            TSharedRef<SHorizontalBox> BlendRow = SNew(SHorizontalBox);
            BlendRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                [SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    {
                        if (S == ECheckBoxState::Checked)
                        {
                            float Val = BlendPreviewSlider.IsValid() ? BlendPreviewSlider->GetValue() : 0.5f;
                            SetBlendPreview(Val);
                        }
                        else
                        {
                            ClearBlendPreview();
                        }
                        RefreshUI();
                    })];
            BlendRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [MakeLbl(TEXT("Blend"), 9, FLinearColor(1.0f,0.8f,0.4f))];
            BlendPreviewLabel = MakeLbl(TEXT("0.50"), 9, FLinearColor(0.8f,0.8f,0.8f));
            BlendPreviewSlider = SNew(SSlider).Value(0.5f)
                .OnValueChanged_Lambda([this](float V)
                {
                    if (BlendPreviewLabel.IsValid())
                        BlendPreviewLabel->SetText(FText::FromString(FString::Printf(TEXT("%.2f"), V)));
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    if (Comp) Comp->SetBlendPreview(V);
                });
            BlendRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)
                [BlendPreviewSlider.ToSharedRef()];
            BlendRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                [BlendPreviewLabel.ToSharedRef()];
            BlendBox->AddSlot().AutoHeight()[BlendRow];
            PropScroll->AddSlot()
                [MakeSectionBox(TEXT("Blend Preview"), BlendBox)];
        }

        // Param Reference section
        {
            TSharedRef<SVerticalBox> RefBox = SNew(SVerticalBox);
            TSharedRef<SHorizontalBox> RefRow = SNew(SHorizontalBox);
            EditParamRefName = SNew(SEditableTextBox)
                .Text(FText::FromString(TEXT("AlbedoTexture")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                .HintText(FText::FromString(TEXT("Param name to search...")));
            RefRow->AddSlot().Padding(FMargin(0,2)).FillWidth(1.0f)
                [SNew(SBox).WidthOverride(100)[EditParamRefName.ToSharedRef()]];
            RefRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [MakeBtn(TEXT("Find"), [this]()
                {
                    if (!EditParamRefName.IsValid()) return;
                    FName PName(*EditParamRefName->GetText().ToString());
                    TArray<FString> Results = FindParamUsages(PName);
                    if (TextParamRefResults.IsValid())
                    {
                        if (Results.Num() == 0)
                            TextParamRefResults->SetText(FText::FromString(TEXT("No references found.")));
                        else
                        {
                            FString Joined;
                            for (int32 i = 0; i < Results.Num(); ++i)
                            {
                                Joined += Results[i];
                                if (i < Results.Num() - 1) Joined += TEXT("\n");
                            }
                            TextParamRefResults->SetText(FText::FromString(Joined));
                        }
                    }
                }, FLinearColor(0.6f,0.8f,1.0f))];
            RefBox->AddSlot().AutoHeight()[RefRow];
            TextParamRefResults = SNew(STextBlock)
                .Text(FText::FromString(TEXT("Enter a param name and click Find.")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FLinearColor(0.5f,0.8f,0.5f))
                .AutoWrapText(true);
            RefBox->AddSlot().AutoHeight().Padding(FMargin(2,2))
                [TextParamRefResults.ToSharedRef()];
            PropScroll->AddSlot()
                [MakeSectionBox(TEXT("Param Reference"), RefBox)];
        }

        // Nested Art / Pin section
        {
            TSharedRef<SVerticalBox> Pin = SNew(SVerticalBox);
            // bPinned toggle
            TSharedRef<SHorizontalBox> PinnedRow = SNew(SHorizontalBox);
            PinnedRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                [SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    {
                        if (!SelectedLayerName.IsValid()) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        bool bPinned = (S == ECheckBoxState::Checked);
                        int32 N = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
                        if (N > 0)
                        {
                            FFaceNestedArt El = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, 0);
                            El.Pin3D.bPinned = bPinned;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, 0, El);
                        }
                    })];
            PinnedRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [MakeLbl(TEXT("Pinned"), 9, FLinearColor(0.6f,0.8f,1.0f))];
            Pin->AddSlot().AutoHeight()[PinnedRow];

            auto AddPinRow = [&](const FString& Label, TSharedPtr<SSlider>& SliderOut, TFunction<void(float)>&& OnChange)
            {
                TSharedRef<SSlider> Sl = SNew(SSlider).Value(0.5f)
                    .OnValueChanged_Lambda([Fn = MoveTemp(OnChange)](float V){ Fn(V * 4.0f - 2.0f); });
                SliderOut = Sl;
                TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                R->AddSlot().Padding(FMargin(0,2)).AutoWidth()[MakeLbl(Label, 9)];
                R->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)[Sl];
                Pin->AddSlot().AutoHeight()[R];
            };
            AddPinRow(TEXT("Pin X"), SliderPinX, [this](float V)
            {
                if (!SelectedLayerName.IsValid()) return;
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (!Comp) return;
                int32 N = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
                if (N > 0)
                {
                    FFaceNestedArt El = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, 0);
                    El.Pin3D.Position3D.X = V;
                    Comp->SetNestedElement(ActiveViewState, SelectedLayerName, 0, El);
                }
            });
            AddPinRow(TEXT("Pin Y"), SliderPinY, [this](float V)
            {
                if (!SelectedLayerName.IsValid()) return;
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (!Comp) return;
                int32 N = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
                if (N > 0)
                {
                    FFaceNestedArt El = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, 0);
                    El.Pin3D.Position3D.Y = V;
                    Comp->SetNestedElement(ActiveViewState, SelectedLayerName, 0, El);
                }
            });
            AddPinRow(TEXT("Pin Z"), SliderPinZ, [this](float V)
            {
                if (!SelectedLayerName.IsValid()) return;
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (!Comp) return;
                int32 N = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
                if (N > 0)
                {
                    FFaceNestedArt El = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, 0);
                    El.Pin3D.Position3D.Z = V;
                    Comp->SetNestedElement(ActiveViewState, SelectedLayerName, 0, El);
                }
            });
            Pin->AddSlot().AutoHeight().Padding(FMargin(0,2))
                [MakeBtn(TEXT("Detect Profile"), [this](){ DetectFaceProfile(); RefreshUI(); })];
            PropScroll->AddSlot()
                [MakeSectionBox(TEXT("Nested Art / Pins"), Pin)];
        }

        PropPanel->AddSlot().FillHeight(1.0f)[PropScroll.ToSharedRef()];
        TextStatus = MakeLbl(TEXT("Ready"), 9, FLinearColor(0.5f,0.8f,0.5f));
        PropPanel->AddSlot().AutoHeight().Padding(FMargin(6,2))
            [TextStatus.ToSharedRef()];
    }

    // --- Assemble main row ---
    {
        TSharedRef<SHorizontalBox> MainRow = SNew(SHorizontalBox);
        MainRow->AddSlot().AutoWidth().VAlign(VAlign_Fill)
            [SNew(SBox).WidthOverride(140)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.07f,0.07f,0.07f))
                    .Padding(FMargin(0))
                    [LayerPanel]]];
        MainRow->AddSlot().FillWidth(1.0f).VAlign(VAlign_Fill)
            [CenterCol];
        MainRow->AddSlot().AutoWidth().VAlign(VAlign_Fill)
            [SNew(SBox).WidthOverride(200)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.07f,0.07f,0.07f))
                    .Padding(FMargin(0))
                    [PropPanel]]];
        Root->AddSlot().AutoHeight()
            [SNew(SBox).HeightOverride(510)[MainRow]];
    }

    // ========================
    // 4. TIMELINE
    // ========================

    {
        TimelineScrollBox = SNew(SScrollBox).Orientation(Orient_Horizontal);
        TimelineBox = SNew(SVerticalBox);
        TimelineScrollBox->AddSlot() [TimelineBox.ToSharedRef()];
        RefreshTimeline();
        Root->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.06f,0.06f,0.06f))
                .Padding(FMargin(4,4))
                [SNew(SBox).HeightOverride(90)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                            [MakeLbl(TEXT("TIMELINE / ART FRAMES"), 10, FLinearColor(0.7f,0.7f,0.9f))]
                        + SVerticalBox::Slot().FillHeight(1.0f)
                            [TimelineScrollBox.ToSharedRef()]]]];
        TextFrameCounts = MakeLbl(TEXT(""), 9, FLinearColor(0.6f,0.6f,0.6f));
        Root->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.05f,0.05f,0.05f))
                .Padding(FMargin(6,2))
                [TextFrameCounts.ToSharedRef()]];
    }

    // ========================
    // 5. STATUS + BOTTOM BAR
    // ========================

    {
        TSharedRef<SVerticalBox> BotArea = SNew(SVerticalBox);

        // Status matrix grid
        StatusMatrixGrid = SNew(SGridPanel);
        StatusMatrixScroll = SNew(SScrollBox).Orientation(Orient_Horizontal);
        StatusMatrixScroll->AddSlot() [StatusMatrixGrid.ToSharedRef()];
        RebuildStatusMatrix();
        BotArea->AddSlot().AutoHeight().Padding(FMargin(2,1))
            [SNew(SBox).HeightOverride(28)[StatusMatrixScroll.ToSharedRef()]];

        // Tag validator
        TextTagValidator = SNew(STextBlock)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.8f));
        BotArea->AddSlot().AutoHeight().Padding(FMargin(2,1))
            [SNew(SBox).HeightOverride(20)[TextTagValidator.ToSharedRef()]];
        RebuildTagValidator();

        // Material cross-referencer
        TextMaterialCrossRef = SNew(STextBlock)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.8f));
        BotArea->AddSlot().AutoHeight().Padding(FMargin(2,1))
            [SNew(SBox).HeightOverride(20)[TextMaterialCrossRef.ToSharedRef()]];
        RebuildMaterialCrossRef();

        TSharedRef<SHorizontalBox> BotBar = SNew(SHorizontalBox);
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Save Preset"), [this](){ SavePreset(); })];
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("New Preset"), [this]()
            {
                FString AssetName = TEXT("MyPreset");
                FString Path = TEXT("/Game/FaceParallax/Presets");
                CreateNewPreset(*AssetName, *Path);
                FString Msg = FString::Printf(TEXT("Created preset '%s' at %s"), *AssetName, *Path);
                if (TextStatus.IsValid()) TextStatus->SetText(FText::FromString(Msg));
                RefreshUI();
            })];
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Snapshot"), [this](){ SnapshotPreset(); if (TextStatus.IsValid()) TextStatus->SetText(FText::FromString(TEXT("Snapshot saved."))); })];
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Undo"), [this](){ RestoreSnapshot(); RefreshUI(); }, FLinearColor(1.0f,0.7f,0.3f))];
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Clear State"), [this](){ ClearState(ActiveViewState); RefreshUI(); })];
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Clear All"), [this](){ ClearAll(); RefreshUI(); })];
        BotBar->AddSlot().FillWidth(1.0f);
        TextStatusDetail = SNew(STextBlock)
            .Text(FText::FromString(TEXT("State: Front | Layer: (none) | Textures: 0")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(FLinearColor(0.55f,0.55f,0.55f));
        BotBar->AddSlot().Padding(FMargin(4,2)).VAlign(VAlign_Center).AutoWidth()
            [TextStatusDetail.ToSharedRef()];
        BotArea->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.1f,0.1f,0.1f))
                .Padding(FMargin(4,3))
                [SNew(SBox).HeightOverride(24)[BotBar]]];
        Root->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.08f,0.08f,0.08f))
                .Padding(FMargin(2,2))
                [BotArea]];
    }

    // Initial UI population
    RefreshUI();

    return Root;
}

// ====================================================================
// REFRESH METHODS
// ====================================================================

void UFaceParallaxEditorWidget::SetSelectedLayer(const FString& LayerName)
{
    FName NewName(LayerName);
    if (NewName != SelectedLayerName)
    {
        SelectedLayerName = NewName;
        RefreshUI();
    }
}

void UFaceParallaxEditorWidget::RefreshUI()
{
    RefreshLayerList();
    RefreshTextureThumbs();
    RefreshTimeline();
    RefreshTransformSliders();
    RefreshConfigCheckboxes();
    RebuildZoneDiagram();
    RebuildCrossLayerPanel();
    RebuildStatusMatrix();
    RebuildTagValidator();
    RebuildMaterialCrossRef();
    // Update zone labels
    if (ZoneYawLabel.IsValid())
    {
        UFaceParallaxComponent* Comp = GetParallaxComponent();
        if (Comp)
        {
            ZoneYawLabel->SetText(FText::FromString(FString::Printf(TEXT("Yaw: %.1f\u00b0  Pitch: %.1f\u00b0"),
                Comp->CurrentYaw, Comp->CurrentPitch)));
        }
    }
    // Update status detail
    if (TextStatusDetail.IsValid())
    {
        FString S = GetStatusString();
        TextStatusDetail->SetText(FText::FromString(S));
    }
    // Clear transient feedback after refresh
    if (TextStatus.IsValid() && TextStatus->GetText().ToString().Len() > 60)
        TextStatus->SetText(FText::FromString(TEXT("Ready")));
}

void UFaceParallaxEditorWidget::RefreshConfigCheckboxes()
{
    if (!CfgBox.IsValid()) return;
    UFaceParallaxComponent* CompCfg = GetParallaxComponent();
    if (!CompCfg) return;

    auto ApplyCheck = [](TSharedPtr<SCheckBox>& Ch, bool bVal)
    {
        if (Ch.IsValid()) Ch->SetIsChecked(bVal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    };

    ApplyCheck(CheckBlinking, CompCfg->GetBlinkingEnabled());
    ApplyCheck(CheckSwoosh, CompCfg->GetSwooshEnabled());
    ApplyCheck(CheckNestedArt, CompCfg->GetNestedArtEnabled());
    ApplyCheck(CheckParams, CompCfg->GetParamsEnabled());
    ApplyCheck(CheckShowTextures, bLocalShowTextures);
    ApplyCheck(CheckDepthMesh, bLocalShowDepthMesh);
    ApplyCheck(CheckWireframe, bLocalShowWireframe);
    ApplyCheck(CheckColorByDepth, bLocalColorByDepth);
}

void UFaceParallaxEditorWidget::RefreshLayerList()
{
    if (!LayerPanelBox.IsValid()) return;
    LayerPanelBox->ClearChildren();

    if (SelectedLayerName.IsValid() && LayerNames.Num() == 0)
    {
        for (auto& N : LayerNames)
        {
            if (N == SelectedLayerName) { SelectedLayerName = N; break; }
        }
    }
    if (!SelectedLayerName.IsValid() && LayerNames.Num() > 0)
        SelectedLayerName = LayerNames[0];

    for (const FName& Tag : LayerNames)
    {
        bool bSelected = (Tag == SelectedLayerName);
        FString TagStr = Tag.ToString();

        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        bool bLayerHidden = HiddenLayers.Contains(Tag);
        // Eye toggle
        Row->AddSlot().AutoWidth().Padding(FMargin(2,0,0,0)).VAlign(VAlign_Center)
            [SNew(SCheckBox)
                .IsChecked(bLayerHidden ? ECheckBoxState::Unchecked : ECheckBoxState::Checked)
                .OnCheckStateChanged_Lambda([this, Tag](ECheckBoxState S)
                {
                    bool bNowHidden = (S == ECheckBoxState::Unchecked);
                    if (bNowHidden) HiddenLayers.Add(Tag);
                    else HiddenLayers.Remove(Tag);
                    RefreshUI();
                })];
        // Layer name button
        Row->AddSlot().FillWidth(1.0f).Padding(FMargin(4,0)).VAlign(VAlign_Center)
            [SNew(SButton)
                .ButtonColorAndOpacity(bSelected ? AccentBlue() : FLinearColor(0.08f,0.08f,0.08f))
                .OnClicked_Lambda([this, Tag]()
                {
                    SelectedLayerName = Tag;
                    RefreshUI();
                    return FReply::Handled();
                })
                .Content()
                [SNew(STextBlock)
                    .Text(FText::FromString(bLayerHidden ? (TagStr + TEXT(" [hidden]")) : TagStr))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", bSelected ? 10 : 9))
                    .ColorAndOpacity(bLayerHidden ? FLinearColor(0.35f,0.35f,0.35f) :
                        (bSelected ? FLinearColor(1,1,1) : FLinearColor(0.7f,0.7f,0.7f)))]];
        LayerPanelBox->AddSlot().AutoHeight().Padding(FMargin(0,1))
            [SNew(SBox).HeightOverride(22)[Row]];
    }
}

void UFaceParallaxEditorWidget::RefreshTextureThumbs()
{
    if (!ThumbAlbedo.IsValid() || !ThumbNormal.IsValid() || !ThumbDepth.IsValid()) return;

    UTexture2D* A = nullptr, * N = nullptr, * D = nullptr;
    if (SelectedLayerName.IsValid())
    {
        FFaceTextureSet Tex = GetSlotTextures(ActiveViewState, SelectedLayerName);
        A = Tex.Albedo;
        N = Tex.Normal;
        D = Tex.Depth;
    }

    auto ApplyTex = [](TSharedPtr<SImage>& Thumb, UTexture2D* Tex, FSlateBrush& Brush)
    {
        if (Tex)
        {
            Brush.SetResourceObject(Tex);
            Brush.ImageSize = FVector2D(72, 72);
            if (Thumb.IsValid()) { Thumb->SetImage(&Brush); Thumb->SetColorAndOpacity(FLinearColor(1,1,1)); }
        }
        else
        {
            if (Thumb.IsValid()) Thumb->SetColorAndOpacity(FLinearColor(0.12f,0.12f,0.12f));
        }
    };

    ApplyTex(ThumbAlbedo, A, ThumbBrushA);
    ApplyTex(ThumbNormal, N, ThumbBrushN);
    ApplyTex(ThumbDepth, D, ThumbBrushD);

    if (TextLayerName.IsValid())
    {
        FString LName = SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : TEXT("(none)");
        TextLayerName->SetText(FText::FromString(FString::Printf(TEXT("Layer: %s"), *LName)));
    }
}

void UFaceParallaxEditorWidget::RefreshTimeline()
{
    if (!TimelineBox.IsValid()) return;
    TimelineBox->ClearChildren();

    if (!SelectedLayerName.IsValid()) return;

    // Show blink frames, expression, viseme, swoosh info
    int32 BlinkFrames = GetBlinkFrameCount(ActiveViewState, SelectedLayerName);
    int32 SwooshFrames = GetSwooshFrameCount(ActiveViewState, SelectedLayerName);
    TArray<EExpression> Expressions = {};
    TArray<EViseme> Visemes = {};

    TSharedRef<SHorizontalBox> TimelineRow = SNew(SHorizontalBox);

    // Blink frames section
    {
        TSharedRef<SVerticalBox> BlkCol = SNew(SVerticalBox);
        BlkCol->AddSlot().AutoHeight()
            [MakeLbl(TEXT("Blink"), 9, FLinearColor(0.8f,0.8f,0.6f))];
        TSharedRef<SHorizontalBox> FrameRow = SNew(SHorizontalBox);
        for (int32 i = 0; i < FMath::Max(1, BlinkFrames); ++i)
        {
            FString IdxStr = FString::FromInt(i);
            TSharedRef<SBox> FrameBox = SNew(SBox).WidthOverride(28).HeightOverride(28)
                [SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(i < BlinkFrames ? FLinearColor(0.3f,0.6f,0.3f) : FLinearColor(0.12f,0.12f,0.12f))
                    .Padding(FMargin(0))
                    [SNew(STextBlock)
                        .Text(FText::FromString(IdxStr))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                        .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.8f))
                        .Justification(ETextJustify::Center)]];
            int32 Idx = i;
            FrameBox->SetOnMouseButtonDown(FPointerEventHandler::CreateLambda([this, Idx](const FGeometry&, const FPointerEvent&) -> FReply
            {
                UE_LOG(LogTemp, Log, TEXT("[FaceParallaxEditorWidget] Click blink frame %d — wire to SetBlinkFrameTextures"), Idx);
                return FReply::Handled();
            }));
            FrameRow->AddSlot().Padding(FMargin(1))[FrameBox];
        }
        // Add button
        FrameRow->AddSlot().Padding(FMargin(2)).VAlign(VAlign_Center)
            [MakeBtn(TEXT("+"), [this]()
            {
                if (!SelectedLayerName.IsValid()) return;
                int32 Cnt = GetBlinkFrameCount(ActiveViewState, SelectedLayerName);
                SetBlinkFrameTextures(ActiveViewState, SelectedLayerName, Cnt, FFaceTextureSet());
                RefreshUI();
            }, FLinearColor(0.6f,1.0f,0.6f), FLinearColor(0.1f,0.1f,0.1f))];
        BlkCol->AddSlot().AutoHeight().Padding(FMargin(0,2))[FrameRow];
        TimelineRow->AddSlot().Padding(FMargin(4,0))[BlkCol];
    }

    // Expression section
    {
        TSharedRef<SVerticalBox> ExpCol = SNew(SVerticalBox);
        ExpCol->AddSlot().AutoHeight()
            [MakeLbl(TEXT("Expression"), 9, FLinearColor(0.6f,0.8f,1.0f))];
        TSharedRef<SHorizontalBox> ExpRow = SNew(SHorizontalBox);
        // Show a few expression slots
        EExpression KnownExprs[] = {
            EExpression::Neutral, EExpression::Smile, EExpression::Frown,
        };
        for (auto Expr : KnownExprs)
        {
            FString ExprStr = StaticEnum<EExpression>()->GetNameStringByValue((int64)Expr);
            bool bHas = false;
            if (SelectedLayerName.IsValid())
                bHas = HasExpressionTextures(ActiveViewState, SelectedLayerName, Expr);
            EExpression E = Expr;
            ExpRow->AddSlot().Padding(FMargin(1))
                [SNew(SButton)
                    .ButtonColorAndOpacity(bHas ? FLinearColor(0.3f,0.5f,0.8f) : FLinearColor(0.08f,0.08f,0.08f))
                    .OnClicked_Lambda([this, E]()
                    {
                        SetExpression(E);
                        RefreshUI();
                        return FReply::Handled();
                    })
                    .Content()
                    [SNew(STextBlock)
                        .Text(FText::FromString(ExprStr.Left(4)))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                        .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.8f))]];
        }
        ExpCol->AddSlot().AutoHeight()[ExpRow];
        TimelineRow->AddSlot().Padding(FMargin(4,0))[ExpCol];
    }

    // Swoosh section
    {
        TSharedRef<SVerticalBox> SwCol = SNew(SVerticalBox);
        SwCol->AddSlot().AutoHeight()
            [MakeLbl(TEXT("Swoosh"), 9, FLinearColor(1.0f,0.7f,0.5f))];
        TSharedRef<SHorizontalBox> SwRow = SNew(SHorizontalBox);
        SwRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
            [SNew(SCheckBox)
                .IsChecked(GetSwooshEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                { SetSwooshEnabled(S == ECheckBoxState::Checked); RefreshUI(); })
                [MakeLbl(TEXT("On"), 8, FLinearColor(0.5f,1.0f,0.5f))]];
        if (SwooshFrames > 0)
        {
            for (int32 i = 0; i < FMath::Min(3, SwooshFrames); ++i)
            {
                SwRow->AddSlot().Padding(FMargin(1))
                    [SNew(SBox).WidthOverride(22).HeightOverride(22)
                        [SNew(SBorder)
                            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                            .BorderBackgroundColor(FLinearColor(0.5f,0.3f,0.1f))
                            [SNew(STextBlock)
                                .Text(FText::FromString(FString::FromInt(i)))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                                .Justification(ETextJustify::Center)]]];
            }
        }
        SwCol->AddSlot().AutoHeight()[SwRow];
        TimelineRow->AddSlot().Padding(FMargin(4,0))[SwCol];
    }

    // Frame counts line
    TimelineBox->AddSlot().AutoHeight().Padding(FMargin(2,0))[TimelineRow];

    if (TextFrameCounts.IsValid())
    {
        FString CntStr = FString::Printf(TEXT("Blink:%d  Swoosh:%d"),
            BlinkFrames, SwooshFrames);
        TextFrameCounts->SetText(FText::FromString(CntStr));
    }
}

void UFaceParallaxEditorWidget::RefreshTransformSliders()
{
    FFaceArtTransform TForm;
    if (SelectedLayerName.IsValid())
        TForm = GetLayerCanonicalTransform(ActiveViewState, SelectedLayerName);
    else
        TForm = FFaceArtTransform();

    auto SetEditText = [](TSharedPtr<SEditableTextBox>& Edit, float Val)
    {
        if (Edit.IsValid())
            Edit->SetText(FText::FromString(FString::Printf(TEXT("%.3f"), Val)));
    };
    SetEditText(EditPosX, TForm.Position.X);
    SetEditText(EditPosY, TForm.Position.Y);
    SetEditText(EditScaleX, TForm.Scale.X);
    SetEditText(EditScaleY, TForm.Scale.Y);
    SetEditText(EditRot, TForm.Rotation);

    // Status
    if (TextStatusDetail.IsValid())
    {
        int32 TexCount = 0;
        if (SelectedLayerName.IsValid())
        {
            FFaceTextureSet Tex = GetSlotTextures(ActiveViewState, SelectedLayerName);
            if (Tex.Albedo) ++TexCount;
            if (Tex.Normal) ++TexCount;
            if (Tex.Depth) ++TexCount;
        }
        FString StateStr = StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)ActiveViewState);
        FString LayerStr = SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : TEXT("(none)");
        TextStatusDetail->SetText(FText::FromString(FString::Printf(
            TEXT("State: %s | Layer: %s | Textures: %d"),
            *StateStr, *LayerStr, TexCount)));
    }
}

int32 UFaceParallaxEditorWidget::GetLayerIndex(FName Tag) const
{
    return LayerNames.IndexOfByKey(Tag);
}

UTexture2D* UFaceParallaxEditorWidget::GetSelectedContentBrowserTexture()
{
    if (!GEditor) return nullptr;
    FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    TArray<FAssetData> SelectedAssets;
    CBModule.Get().GetSelectedAssets(SelectedAssets);
    for (const FAssetData& Asset : SelectedAssets)
    {
        if (Asset.GetClass()->IsChildOf(UTexture2D::StaticClass()))
        {
            UTexture2D* Tex = Cast<UTexture2D>(Asset.GetAsset());
            if (Tex) return Tex;
        }
    }
    UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No texture selected in Content Browser."));
    return nullptr;
}

void UFaceParallaxEditorWidget::SetRenderTarget(UTextureRenderTarget2D* RT)
{
    RenderTargetTexture = RT;
    if (RT)
    {
        PreviewBrush.SetResourceObject(RT);
        PreviewBrush.ImageSize = FVector2D((float)RT->SizeX, (float)RT->SizeY);
        if (PreviewImageWidget.IsValid())
            PreviewImageWidget->SetImage(&PreviewBrush);
    }
}

// ====================================================================
// NESTED ART (3D Pin System)
// ====================================================================

FFacePin3D UFaceParallaxEditorWidget::GetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return FFacePin3D();
    return Comp->GetNestedPin3D(State, LayerTag, Index);
}

void UFaceParallaxEditorWidget::SetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index, const FFacePin3D& Pin)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetNestedPin3D(State, LayerTag, Index, Pin);
}

FVector2D UFaceParallaxEditorWidget::GetNestedPinUV(EFaceAngleState State, FName LayerTag, int32 Index, EFaceAngleState ViewState) const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return FVector2D(0.5f, 0.5f);
    FFacePin3D Pin = Comp->GetNestedPin3D(State, LayerTag, Index);
    if (!Pin.bPinned) return FVector2D(0.5f, 0.5f);
    return Comp->ProjectPinToUV(Pin.Position3D, ViewState);
}

void UFaceParallaxEditorWidget::SetNestedPinFromUV(EFaceAngleState State, FName LayerTag, int32 Index, EFaceAngleState FromViewState, FVector2D UV)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return;

    FFacePin3D Pin = Comp->GetNestedPin3D(State, LayerTag, Index);
    Pin.bPinned = true;

    float YawDeg = Comp->GetZoneCenterYaw(FromViewState);
    FVector2D UVCenter(UV.X - 0.5f, UV.Y - 0.5f);

    if (FMath::Abs(YawDeg) < 45.0f)
    {
        Pin.Position3D.X = UVCenter.X * 2.0f;
        Pin.Position3D.Y = UVCenter.Y * 2.0f;
    }
    else if (FMath::Abs(YawDeg - 90.0f) < 45.0f || FMath::Abs(YawDeg + 90.0f) < 45.0f)
    {
        Pin.Position3D.Z = UVCenter.X * 2.0f;
        Pin.Position3D.Y = UVCenter.Y * 2.0f;
    }
    else
    {
        Pin.Position3D.X = UVCenter.X * 2.0f;
        Pin.Position3D.Y = UVCenter.Y * 2.0f;
        Pin.Position3D.Z = 0.0f;
    }

    Comp->SetNestedPin3D(State, LayerTag, Index, Pin);
}

FVector2D UFaceParallaxEditorWidget::GetNestedEffectivePivot(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return FVector2D(0.5f, 0.5f);
    return Comp->GetNestedEffectivePivot(State, LayerTag, Index);
}

FFaceProfile3D UFaceParallaxEditorWidget::GetFaceProfile() const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return FFaceProfile3D();
    return Comp->FaceProfile;
}

void UFaceParallaxEditorWidget::SetFaceProfile(const FFaceProfile3D& Profile)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->FaceProfile = Profile;
}

void UFaceParallaxEditorWidget::DetectFaceProfile()
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->DetectFaceProfileFromPreset();
}

// ====================================================================
// ZONE DIAGRAM
// ====================================================================

void UFaceParallaxEditorWidget::RebuildZoneDiagram()
{
    if (!ZoneDiagramWidget.IsValid()) return;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    float HZW = Comp ? Comp->HalfZoneWidth : 22.5f;
    float BlendW = Comp ? Comp->BlendWindowWidth : 5.0f;
    float Yaw = Comp ? Comp->CurrentYaw : 0.0f;

    // Build horizontal zones: 8 zones across 180 degrees
    struct FZoneSeg { FString Label; float Start; float End; FLinearColor Color; };
    TArray<FZoneSeg> Segs;
    float HalfTotal = 180.0f;
    float Z2 = HZW * 2.0f, Z3 = HZW * 3.0f, Z4 = HZW * 4.0f, Z5 = HZW * 5.0f, Z6 = HZW * 6.0f, Z7 = HZW * 7.0f;

    auto AddSeg = [&](float S, float E, const FString& L, FLinearColor C)
    {
        S = FMath::Clamp(S, -HalfTotal, HalfTotal);
        E = FMath::Clamp(E, -HalfTotal, HalfTotal);
        if (S >= E) return;
        Segs.Add({L, S, E, C});
    };

    AddSeg(-Z7, -Z5, TEXT("BkL"), FLinearColor(0.5f,0.5f,0.7f));
    AddSeg(-Z5, -Z3, TEXT("ProfL"), FLinearColor(0.4f,0.6f,1.0f));
    AddSeg(-Z3, -HZW, TEXT("3/4L"), FLinearColor(0.5f,0.7f,1.0f));
    AddSeg(-HZW, HZW, TEXT("Front"), FLinearColor(0.6f,0.8f,1.0f));
    AddSeg(HZW, Z3, TEXT("3/4R"), FLinearColor(0.5f,0.7f,1.0f));
    AddSeg(Z3, Z5, TEXT("ProfR"), FLinearColor(0.4f,0.6f,1.0f));
    AddSeg(Z5, Z7, TEXT("BkR"), FLinearColor(0.5f,0.5f,0.7f));
    AddSeg(Z7, HalfTotal, TEXT("Back"), FLinearColor(0.4f,0.4f,0.6f));

    TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
    float TotalAngle = HalfTotal * 2.0f;
    int32 SegIdx = 0, YawPosPx = -1;
    for (const FZoneSeg& Seg : Segs)
    {
        float Frac = (Seg.End - Seg.Start) / TotalAngle;
        int32 Px = FMath::Max(1, (int32)(Frac * 400.0f));
        FLinearColor C = Seg.Color;

        // Blend window tint
        if (BlendW > 0.0f)
        {
            float SegLen = Seg.End - Seg.Start;
            float BlendFrac = BlendW / SegLen;
            if (BlendFrac > 0.1f)
                C = C * 0.7f + FLinearColor(0.5f, 0.5f, 0.3f) * 0.3f;
        }

        // Yaw cursor marker
        int32 ThisStartPx = SegIdx > 0 ? 0 : 0;
        if (Yaw >= Seg.Start && Yaw <= Seg.End && SegIdx > 0)
        {
            float YawFrac = (Yaw - Seg.Start) / (Seg.End - Seg.Start);
            YawPosPx = (int32)(YawFrac * Px);
        }

        Bar->AddSlot().Padding(FMargin(0)).FillWidth(Frac)
            [SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(C)
                .Padding(FMargin(2,0))
                [SNew(STextBlock)
                    .Text(FText::FromString(Seg.Label))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                    .ColorAndOpacity(FLinearColor(0.1f,0.1f,0.1f))
                    .Justification(ETextJustify::Center)]];
        ++SegIdx;
    }

    // Overlay yaw cursor if within range
    if (YawPosPx >= 0)
        ZoneDiagramWidget = SNew(SOverlay)
            + SOverlay::Slot()[Bar]
            + SOverlay::Slot().HAlign(HAlign_Left).Padding(FMargin(YawPosPx, 0, 0, 0))
                [SNew(SBox).WidthOverride(4)
                    [SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(FLinearColor(1.0f, 0.2f, 0.2f))]];
    else
        ZoneDiagramWidget = Bar;
}

// ====================================================================
// BLEND PREVIEW
// ====================================================================

void UFaceParallaxEditorWidget::SetBlendPreview(float Alpha)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->SetBlendPreview(Alpha);
}

void UFaceParallaxEditorWidget::ClearBlendPreview()
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ClearBlendPreview();
}

// ====================================================================
// STATUS MATRIX
// ====================================================================

void UFaceParallaxEditorWidget::RebuildStatusMatrix()
{
    if (!StatusMatrixGrid.IsValid()) return;
    StatusMatrixGrid->ClearChildren();

    if (!ValidatePreset()) return;
    TArray<FName> Tags;
    {
        UFaceParallaxComponent* Comp = GetParallaxComponent();
        if (Comp)
        {
            for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
                Tags.Add(Comp->GetLayerDefinition(i).LayerTag);
        }
        if (Tags.Num() == 0) Tags = { FName("Eyes"), FName("Brows"), FName("Mouth"), FName("Hair") };
    }

    TArray<EFaceAngleState> States = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::Back, EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    // Header row — state labels
    StatusMatrixGrid->AddSlot(0, 0)
        [SNew(SBox).WidthOverride(44).HeightOverride(20)
            [SNew(STextBlock)
                .Text(FText::FromString(TEXT("")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                .ColorAndOpacity(FLinearColor(0.5f,0.5f,0.5f))]];
    for (int32 si = 0; si < States.Num(); ++si)
    {
        FString Abbr;
        switch (States[si])
        {
            case EFaceAngleState::Front: Abbr = TEXT("F"); break;
            case EFaceAngleState::ThreeQuarterRight: Abbr = TEXT("3R"); break;
            case EFaceAngleState::RightProfile: Abbr = TEXT("PR"); break;
            case EFaceAngleState::BackRight: Abbr = TEXT("BR"); break;
            case EFaceAngleState::Back: Abbr = TEXT("B"); break;
            case EFaceAngleState::BackLeft: Abbr = TEXT("BL"); break;
            case EFaceAngleState::LeftProfile: Abbr = TEXT("PL"); break;
            case EFaceAngleState::ThreeQuarterLeft: Abbr = TEXT("3L"); break;
            case EFaceAngleState::Top: Abbr = TEXT("T"); break;
            case EFaceAngleState::Bottom: Abbr = TEXT("Bt"); break;
        }
        StatusMatrixGrid->AddSlot(si + 1, 0)
            [SNew(SBox).WidthOverride(22).HeightOverride(20)
                [SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.12f,0.12f,0.12f))
                    .Padding(FMargin(1))
                    [SNew(STextBlock)
                        .Text(FText::FromString(Abbr))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 6))
                        .ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f))
                        .Justification(ETextJustify::Center)]]];
    }

    // Data rows — one per layer
    for (int32 li = 0; li < Tags.Num(); ++li)
    {
        FName Tag = Tags[li];
        StatusMatrixGrid->AddSlot(0, li + 1)
            [SNew(SBox).WidthOverride(44).HeightOverride(18)
                [SNew(STextBlock)
                    .Text(FText::FromString(Tag.ToString()))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                    .ColorAndOpacity(FLinearColor(0.7f,0.7f,0.7f))]];
        for (int32 si = 0; si < States.Num(); ++si)
        {
            bool bAssigned = ActivePreset->HasSlot(States[si], Tag);
            bool bSelected = (States[si] == ActiveViewState);
            FLinearColor CellColor = bAssigned
                ? (bSelected ? FLinearColor(0.3f, 0.7f, 0.3f) : FLinearColor(0.2f, 0.5f, 0.2f))
                : (bSelected ? FLinearColor(0.7f, 0.3f, 0.3f) : FLinearColor(0.15f,0.15f,0.15f));
            StatusMatrixGrid->AddSlot(si + 1, li + 1)
                [SNew(SBox).WidthOverride(22).HeightOverride(18)
                    [SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(CellColor)
                        .Padding(FMargin(1))
                        [SNew(STextBlock)
                            .Text(FText::FromString(bAssigned ? TEXT("\u2713") : TEXT("\u00D7")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                            .ColorAndOpacity(bAssigned ? FLinearColor(0.8f,1.0f,0.8f) : FLinearColor(0.5f,0.5f,0.5f))
                            .Justification(ETextJustify::Center)]]];
        }
    }
}

// ====================================================================
// CROSS-LAYER OVERLAY
// ====================================================================

void UFaceParallaxEditorWidget::RebuildCrossLayerPanel()
{
    if (!CrossLayerBox.IsValid()) return;
    CrossLayerBox->ClearChildren();

    if (!ValidatePreset())
    {
        TextCrossLayer->SetText(FText::FromString(TEXT("No preset assigned.")));
        CrossLayerBox->AddSlot().AutoHeight()[TextCrossLayer.ToSharedRef()];
        return;
    }

    TArray<FName> AllTags;
    {
        UFaceParallaxComponent* Comp = GetParallaxComponent();
        if (Comp)
        {
            for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
                AllTags.Add(Comp->GetLayerDefinition(i).LayerTag);
        }
        if (AllTags.Num() == 0) AllTags = LayerNames;
    }

    FString Output;
    for (const FName& Tag : AllTags)
    {
        if (!ActivePreset->HasSlot(ActiveViewState, Tag)) continue;
        FFaceArtTransform T = GetEffectiveLayerTransform(ActiveViewState, Tag);
        Output += FString::Printf(TEXT("%s: P(%.2f,%.2f) S(%.2f,%.2f) R(%.1f)"),
            *Tag.ToString(),
            T.Position.X, T.Position.Y,
            T.Scale.X, T.Scale.Y,
            T.Rotation);
        // Check for nested elements
        int32 NE = ActivePreset->GetNestedElementCount(ActiveViewState, Tag);
        if (NE > 0)
        {
            Output += TEXT(" [");
            for (int32 ni = 0; ni < NE; ++ni)
            {
                FFaceNestedArt N = ActivePreset->GetNestedElement(ActiveViewState, Tag, ni);
                if (ni > 0) Output += TEXT(", ");
                Output += FString::Printf(TEXT("%s P(%.2f,%.2f)"),
                    *N.ElementName.ToString(),
                    N.RelativeTransform.Position.X, N.RelativeTransform.Position.Y);
            }
            Output += TEXT("]");
        }
        Output += TEXT("\n");
    }

    if (Output.IsEmpty())
        Output = TEXT("No layers assigned for this state.");

    TextCrossLayer->SetText(FText::FromString(Output));
    CrossLayerBox->AddSlot().AutoHeight()[TextCrossLayer.ToSharedRef()];
}

// ====================================================================
// TAG VALIDATOR
// ====================================================================

void UFaceParallaxEditorWidget::RebuildTagValidator()
{
    if (!TextTagValidator.IsValid()) return;

    if (!ActivePreset)
    {
        TextTagValidator->SetText(FText::FromString(TEXT("Tag Validator: no preset")));
        return;
    }

    TArray<EFaceAngleState> States = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::Back, EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    TSet<FName> KnownTags;
    TMap<FName, int32> TagCount;
    for (EFaceAngleState S : States)
    {
        if (!ActivePreset->HasState(S)) continue;
        TArray<FName> Tags = ActivePreset->GetAllLayerTags(S);
        for (FName Tag : Tags)
        {
            KnownTags.Add(Tag);
            TagCount.FindOrAdd(Tag)++;
        }
    }

    UFaceParallaxComponent* Comp = GetParallaxComponent();
    TSet<FName> DefTags;
    if (Comp)
    {
        for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
            DefTags.Add(Comp->GetLayerDefinition(i).LayerTag);
    }

    FString Report;
    for (FName Tag : KnownTags)
    {
        int32 C = TagCount.FindRef(Tag);
        bool bInDefs = DefTags.Contains(Tag);
        if (!bInDefs)
        {
            Report += FString::Printf(TEXT("\u26A0 %s(no def) "), *Tag.ToString());
        }
        else if (C < 10)
        {
            Report += FString::Printf(TEXT("\u26A1 %s(%d/10) "), *Tag.ToString(), C);
        }
    }

    if (Report.IsEmpty())
    {
        TextTagValidator->SetColorAndOpacity(FLinearColor(0.4f,0.8f,0.4f));
        TextTagValidator->SetText(FText::FromString(TEXT("Tags: all 10 states complete")));
    }
    else
    {
        TextTagValidator->SetColorAndOpacity(Report.Contains(TEXT("\u26A0")) ? FLinearColor(1.0f,0.6f,0.2f) : FLinearColor(0.8f,0.8f,0.4f));
        TextTagValidator->SetText(FText::FromString(FString::Printf(TEXT("Tags: %s"), *Report.Left(200))));
    }
}

// ====================================================================
// MATERIAL CROSS-REFERENCER
// ====================================================================

void UFaceParallaxEditorWidget::RebuildMaterialCrossRef()
{
    if (!TextMaterialCrossRef.IsValid()) return;

    if (!ActivePreset)
    {
        TextMaterialCrossRef->SetText(FText::FromString(TEXT("MatCrossRef: no preset")));
        return;
    }

    TArray<EFaceAngleState> States = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::Back, EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    TSet<FName> AllParams;
    TMap<FName, int32> ParamCount;
    for (EFaceAngleState S : States)
    {
        if (!ActivePreset->HasState(S)) continue;
        TArray<FName> Tags = ActivePreset->GetAllLayerTags(S);
        for (FName Tag : Tags)
        {
            const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(S, Tag);
            for (const FFaceParamBinding& B : ArtSlot.ParamBindings)
            {
                AllParams.Add(B.ParamName);
                ParamCount.FindOrAdd(B.ParamName)++;
            }
            for (const FFaceNestedArt& N : ArtSlot.NestedElements)
            {
                for (const FFaceParamBinding& B : N.ParamBindings)
                {
                    AllParams.Add(B.ParamName);
                    ParamCount.FindOrAdd(B.ParamName)++;
                }
            }
        }
    }

    FString Report;
    for (FName P : AllParams)
    {
        Report += FString::Printf(TEXT("%s(%d) "), *P.ToString(), ParamCount.FindRef(P));
    }

    if (Report.IsEmpty())
    {
        TextMaterialCrossRef->SetText(FText::FromString(TEXT("MatCrossRef: no param bindings")));
    }
    else
    {
        TextMaterialCrossRef->SetText(FText::FromString(FString::Printf(TEXT("Params: %s"), *Report.Left(200))));
    }
}

// ====================================================================
// SEARCH FILTER
// ====================================================================

void UFaceParallaxEditorWidget::ApplySearchFilter(const FString& Filter)
{
    SearchFilter = Filter;
    // Apply visibility on the main prop scroll sections
    if (!PropScroll.IsValid()) return;
    for (int32 Idx = 0; Idx < PropScroll->GetChildren()->Num(); ++Idx)
    {
        TSharedRef<SWidget> Child = PropScroll->GetChildren()->GetChildAt(Idx);
        // Show all when filter is empty; otherwise, a more
        // sophisticated per-section filter could be applied here.
        Child->SetVisibility(Filter.IsEmpty() ? EVisibility::Visible : EVisibility::Visible);
    }
    UE_LOG(LogTemp, Verbose, TEXT("[FaceParallaxEditorWidget] Search filter: %s"), *Filter);
}

// ====================================================================
// STATUS QUERIES
// ====================================================================

TArray<EFaceAngleState> UFaceParallaxEditorWidget::GetMissingStates() const
{
    TArray<EFaceAngleState> Missing;
    if (!ValidatePreset()) return Missing;
    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::Back, EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };
    for (EFaceAngleState S : AllStates)
    {
        if (!ActivePreset->HasState(S))
            Missing.Add(S);
    }
    return Missing;
}

TArray<FName> UFaceParallaxEditorWidget::GetMissingLayers(EFaceAngleState State) const
{
    TArray<FName> Missing;
    if (!ValidatePreset()) return Missing;
    TArray<FName> AllTags;
    {
        UFaceParallaxComponent* Comp = GetParallaxComponent();
        if (Comp)
        {
            for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
                AllTags.Add(Comp->GetLayerDefinition(i).LayerTag);
        }
    }
    for (FName Tag : AllTags)
    {
        if (!ActivePreset->HasSlot(State, Tag))
            Missing.Add(Tag);
    }
    return Missing;
}

TArray<FString> UFaceParallaxEditorWidget::GetAllLayerTransforms(EFaceAngleState State) const
{
    TArray<FString> Result;
    if (!ValidatePreset()) return Result;
    TArray<FName> AllTags;
    {
        UFaceParallaxComponent* Comp = GetParallaxComponent();
        if (Comp)
        {
            for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
                AllTags.Add(Comp->GetLayerDefinition(i).LayerTag);
        }
    }
    for (FName Tag : AllTags)
    {
        if (!ActivePreset->HasSlot(State, Tag)) continue;
        FFaceArtTransform T = ActivePreset->GetEffectiveTransform(State, Tag);
        Result.Add(FString::Printf(TEXT("%s: Pos(%.3f,%.3f) Scale(%.3f,%.3f) Rot(%.1f)"),
            *Tag.ToString(), T.Position.X, T.Position.Y, T.Scale.X, T.Scale.Y, T.Rotation));
    }
    return Result;
}

// ====================================================================
// PARAM REFERENCE
// ====================================================================

TArray<FString> UFaceParallaxEditorWidget::FindParamUsages(FName ParamName) const
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) return Comp->FindParamNameReferences(ParamName);
    return TArray<FString>();
}

// ====================================================================
// SNAPSHOT / UNDO
// ====================================================================

void UFaceParallaxEditorWidget::SnapshotPreset()
{
    if (!ActivePreset) return;
    // Duplicate the preset into a temporary package
    UPackage* TempPkg = CreatePackage(TEXT("/Temp/FaceParallaxSnapshot"));
    TempPkg->SetFlags(RF_Transient);
    SnapshotPresetBackup = DuplicateObject<UFaceParallaxPreset>(ActivePreset, TempPkg, TEXT("SnapshotBackup"));
    if (TextStatus.IsValid())
        TextStatus->SetText(FText::FromString(TEXT("Snapshot saved.")));
}

void UFaceParallaxEditorWidget::RestoreSnapshot()
{
    if (!SnapshotPresetBackup || !ActivePreset) return;
    // Copy all assignments from snapshot back into active preset
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
        TArray<FName> Tags = SnapshotPresetBackup->GetAllLayerTags(S);
        for (FName Tag : Tags)
        {
            FFaceArtSlot ArtSlot = SnapshotPresetBackup->GetSlot(S, Tag);
            ActivePreset->SetSlot(S, Tag, ArtSlot);
        }
        // Copy NestedPin3D and FaceProfile references
        if (SnapshotPresetBackup->HasState(S))
        {
            TArray<FName> STags = SnapshotPresetBackup->GetAllLayerTags(S);
            TArray<FName> ATags = ActivePreset->GetAllLayerTags(S);
            for (FName Tag : STags)
            {
                int32 N = SnapshotPresetBackup->GetNestedElementCount(S, Tag);
                for (int32 i = 0; i < N; ++i)
                {
                    FFacePin3D Pin = SnapshotPresetBackup->GetNestedPin3D(S, Tag, i);
                    ActivePreset->SetNestedPin3D(S, Tag, i, Pin);
                }
            }
        }
    }
    // Refresh preview
    ApplyPresetToPreview();
    if (TextStatus.IsValid())
        TextStatus->SetText(FText::FromString(TEXT("Snapshot restored.")));
}

bool UFaceParallaxEditorWidget::HasSnapshot() const
{
    return SnapshotPresetBackup != nullptr;
}

#endif // WITH_EDITOR
