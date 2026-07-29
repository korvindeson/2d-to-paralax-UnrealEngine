#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxPreviewActor.h"
#include "FaceParallaxComponent.h"
#include "FaceParallaxPreset.h"
#include "DepthDebugVisualizerComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/SavePackage.h"

bool UFaceParallaxEditorWidget::ValidatePreset() const
{
    if (!ActivePreset)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No ActivePreset assigned."));
        return false;
    }
    return true;
}

bool UFaceParallaxEditorWidget::ValidatePreviewActor() const
{
    if (!PreviewActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No PreviewActor assigned."));
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
    if (!ValidatePreviewActor()) return;
    PreviewActor->ShowTextures(bVisible);
}

void UFaceParallaxEditorWidget::ShowDepthMesh(bool bVisible)
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->ShowDepthMesh(bVisible);
}

void UFaceParallaxEditorWidget::ShowWireframe(bool bVisible)
{
    if (!ValidatePreviewActor()) return;
    PreviewActor->ShowWireframe(bVisible);
}

void UFaceParallaxEditorWidget::ColorByDepth(bool bEnabled)
{
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
