#include "FaceParallaxEditorWidget.h"
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

namespace
{
    FString ChannelFromTextureName(const FString& Name)
    {
        const FString Lower = Name.ToLower();
        if (Lower.Contains(TEXT("_normal")) || Lower.Contains(TEXT("_norm")) || Lower.Contains(TEXT("_n"))
            || Lower.Contains(TEXT("_normalmap")))
        {
            return TEXT("Normal");
        }
        if (Lower.Contains(TEXT("_depth")) || Lower.Contains(TEXT("_d"))
            || Lower.Contains(TEXT("_height")) || Lower.Contains(TEXT("_displacement")))
        {
            return TEXT("Depth");
        }
        return TEXT("Albedo");
    }

    // Parses the view-state suffix from a file base name (channel suffix already removed).
    // Full names are matched before short codes so "_front" wins over "_f".
    // Returns the state index (0-9) or -1, and the matched suffix text.
    int32 MatchStateSuffix(const FString& BaseName, FString& OutSuffix)
    {
        const FString Lower = BaseName.ToLower();
        struct FStateSuffix { const TCHAR* Suffix; int32 State; };
        static const FStateSuffix Map[] = {
            {TEXT("_threequarterright"), 1}, {TEXT("_threequarterleft"), 7},
            {TEXT("_3quarterright"), 1},    {TEXT("_3quarterleft"), 7},
            {TEXT("_rightprofile"), 2},     {TEXT("_leftprofile"), 6},
            {TEXT("_backright"), 3},        {TEXT("_backleft"), 5},
            {TEXT("_front"), 0},            {TEXT("_back"), 4},
            {TEXT("_top"), 8},              {TEXT("_bottom"), 9},
            {TEXT("_3r"), 1},               {TEXT("_3l"), 7},
            {TEXT("_pr"), 2},               {TEXT("_pl"), 6},
            {TEXT("_br"), 3},               {TEXT("_bl"), 5},
            {TEXT("_f"), 0},                {TEXT("_b"), 4},
            {TEXT("_t"), 8},                {TEXT("_bot"), 9},
        };
        for (const FStateSuffix& M : Map)
        {
            if (Lower.EndsWith(M.Suffix))
            {
                OutSuffix = M.Suffix;
                return M.State;
            }
        }
        return -1;
    }

    // Removes the channel suffix matched by ChannelFromTextureName from a file base name.
    FString StripChannelSuffix(const FString& Name, const FString& Channel)
    {
        const FString Lower = Name.ToLower();
        if (Channel == TEXT("Normal"))
        {
            for (const TCHAR* S : {TEXT("_normalmap"), TEXT("_normal"), TEXT("_norm"), TEXT("_n")})
            {
                if (Lower.EndsWith(S))
                    return Name.Left(Name.Len() - FCString::Strlen(S));
            }
        }
        else if (Channel == TEXT("Depth"))
        {
            for (const TCHAR* S : {TEXT("_displacement"), TEXT("_depth"), TEXT("_height"), TEXT("_d")})
            {
                if (Lower.EndsWith(S))
                    return Name.Left(Name.Len() - FCString::Strlen(S));
            }
        }
        return Name;
    }

    // Phase D: 16-bin luminance histogram, bins normalized by the max count.
    // (mirror of UFaceParallaxEditorWidget::BuildLumaHistogram)
    void BuildLumaHistogram(const TArray<float>& Luma, int32 Grid, TArray<float>& OutBins)
    {
        OutBins.Reset();
        OutBins.SetNum(16);
        if (Luma.Num() != Grid * Grid) return;
        for (float V : Luma)
        {
            const int32 B = FMath::Clamp((int32)FMath::FloorToInt(FMath::Clamp(V, 0.0f, 1.0f) * 15.9999f), 0, 15);
            OutBins[B] += 1.0f;
        }
        float MaxB = 1.0f;
        for (float B : OutBins) MaxB = FMath::Max(MaxB, B);
        for (float& B : OutBins) B /= MaxB;
    }

    // Phase D: fraction of interior pixels whose Sobel edge magnitude exceeds the threshold.
    // (mirror of UFaceParallaxEditorWidget::EdgeDensity)
    float EdgeDensity(const TArray<float>& Luma, int32 Grid, float Threshold)
    {
        if (Luma.Num() != Grid * Grid || Grid < 3) return 0.0f;
        int32 Edges = 0;
        for (int32 Y = 1; Y < Grid - 1; ++Y)
        {
            for (int32 X = 1; X < Grid - 1; ++X)
            {
                const float TL = Luma[(Y - 1) * Grid + X - 1];
                const float TC = Luma[(Y - 1) * Grid + X];
                const float TR = Luma[(Y - 1) * Grid + X + 1];
                const float ML = Luma[Y * Grid + X - 1];
                const float MR = Luma[Y * Grid + X + 1];
                const float BL = Luma[(Y + 1) * Grid + X - 1];
                const float BC = Luma[(Y + 1) * Grid + X];
                const float BR = Luma[(Y + 1) * Grid + X + 1];
                const float Gx = (TR + 2.0f * MR + BR) - (TL + 2.0f * ML + BL);
                const float Gy = (BL + 2.0f * BC + BR) - (TL + 2.0f * TC + TR);
                const float Mag = FMath::Sqrt(Gx * Gx + Gy * Gy) / 4.0f;
                if (Mag > Threshold) ++Edges;
            }
        }
        const int32 Interior = (Grid - 2) * (Grid - 2);
        return Interior > 0 ? (float)Edges / (float)Interior : 0.0f;
    }

    struct FPresetTransactionScope
    {
        UFaceParallaxPreset* Preset;
        bool bActive = false;
        FPresetTransactionScope(UFaceParallaxPreset* InPreset, const FString& Desc)
            : Preset(InPreset)
        {
            if (GEditor && Preset)
            {
                bActive = true;
                GEditor->BeginTransaction(FText::FromString(Desc));
                Preset->Modify();
            }
        }
        ~FPresetTransactionScope()
        {
            if (bActive)
                GEditor->EndTransaction();
        }
    };
}

// ====================================================================
// SFaceLayerGizmo — canvas transform gizmo (Phase B)
// Drag body = move, bottom-right corner = scale, top handle = rotate.
// Writes through UFaceParallaxEditorWidget::SetGizmoTransform so
// canonical/override/link semantics stay in one place.
// ====================================================================

class UFaceParallaxEditorWidget::SFaceLayerGizmo : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceLayerGizmo) {}
    SLATE_END_ARGS()

    UFaceParallaxEditorWidget* Owner = nullptr;

    void SetPinMode(bool bInPinMode)
    {
        bPinMode = bInPinMode;
        PinDragMode = 0;
    }

    void Construct(const FArguments& InArgs) {}

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(450.0f, 450.0f);
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
        const FSlateRect&, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle&, bool) const override
    {
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        if (!Owner || !Brush) return LayerId;
        const FVector2D CanvasSize = AllottedGeometry.GetLocalSize();
        if (CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f) return LayerId;

        // Pin mode: draw the selected pinned element's handle (its projected
        // UV in the active view state) instead of the layer transform box.
        if (bPinMode)
        {
            const FVector2D PinUV = Owner->GetSelectedPinUV();
            if (PinUV.X >= 0.0f)
            {
                const FVector2D PinPx = UFaceParallaxEditorWidget::GizmoUVToPixels(PinUV, CanvasSize);
                FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
                    AllottedGeometry.ToPaintGeometry(FVector2D(14.0f, 14.0f),
                        FSlateLayoutTransform(PinPx - FVector2D(7.0f, 7.0f))),
                    Brush, ESlateDrawEffect::None, FLinearColor(1.0f, 0.9f, 0.4f, 0.95f));
                FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
                    AllottedGeometry.ToPaintGeometry(FVector2D(6.0f, 6.0f),
                        FSlateLayoutTransform(PinPx - FVector2D(3.0f, 3.0f))),
                    Brush, ESlateDrawEffect::None, FLinearColor(0.2f, 0.2f, 0.2f, 0.95f));
            }
            return LayerId + 3;
        }

        const FFaceArtTransform T = Owner->GetGizmoTransform();
        const FVector2D Center = CanvasSize * 0.5f
            + UFaceParallaxEditorWidget::GizmoUVToPixels(T.Position, CanvasSize);
        const FVector2D Half = UFaceParallaxEditorWidget::GizmoUVToPixels(T.Scale, CanvasSize) * 0.5f;
        const float Rad = FMath::DegreesToRadians(T.Rotation);

        auto RotV = [Rad](const FVector2D& V)
        {
            return FVector2D(V.X * FMath::Cos(Rad) - V.Y * FMath::Sin(Rad),
                             V.X * FMath::Sin(Rad) + V.Y * FMath::Cos(Rad));
        };

        const FVector2D Corners[4] = {
            Center + RotV(FVector2D(-Half.X, -Half.Y)),
            Center + RotV(FVector2D(Half.X, -Half.Y)),
            Center + RotV(FVector2D(Half.X, Half.Y)),
            Center + RotV(FVector2D(-Half.X, Half.Y)),
        };

        // Translucent quad fill
        FSlateDrawElement::MakeRotatedBox(OutDrawElements, LayerId,
            AllottedGeometry.ToPaintGeometry(FVector2D(Half.X * 2.0f, Half.Y * 2.0f),
                FSlateLayoutTransform(Center - Half)),
            Brush, ESlateDrawEffect::None, Rad, TOptional<FVector2D>(),
            FSlateDrawElement::ERotationSpace::RelativeToElement,
            FLinearColor(0.4f, 0.8f, 1.0f, 0.12f));

        // Edges
        for (int32 e = 0; e < 4; ++e)
        {
            const FVector2D A = Corners[e];
            const FVector2D B = Corners[(e + 1) % 4];
            const FVector2D Mid = (A + B) * 0.5f;
            const float Len = (B - A).Size();
            const float Ang = FMath::Atan2((B - A).Y, (B - A).X);
            if (Len < 1.0f) continue;
            FSlateDrawElement::MakeRotatedBox(OutDrawElements, LayerId + 1,
                AllottedGeometry.ToPaintGeometry(FVector2D(Len, 2.0f),
                    FSlateLayoutTransform(Mid - FVector2D(Len * 0.5f, 1.0f))),
                Brush, ESlateDrawEffect::None, Ang,
                TOptional<FVector2D>(FVector2D(Len * 0.5f, 1.0f)),
                FSlateDrawElement::ERotationSpace::RelativeToElement,
                FLinearColor(0.4f, 0.8f, 1.0f, 0.9f));
        }

        // Rotate handle (top)
        const FVector2D RotHandle = Center + RotV(FVector2D(0.0f, -Half.Y - 14.0f));
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
            AllottedGeometry.ToPaintGeometry(FVector2D(10.0f, 10.0f),
                FSlateLayoutTransform(RotHandle - FVector2D(5.0f, 5.0f))),
            Brush, ESlateDrawEffect::None, FLinearColor(1.0f, 0.7f, 0.3f, 0.95f));

        // Scale handle (bottom-right corner)
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
            AllottedGeometry.ToPaintGeometry(FVector2D(10.0f, 10.0f),
                FSlateLayoutTransform(Corners[2] - FVector2D(5.0f, 5.0f))),
            Brush, ESlateDrawEffect::None, FLinearColor(0.4f, 1.0f, 0.5f, 0.95f));

        return LayerId + 3;
    }

    virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override
    {
        if (!Owner || Ev.GetEffectingButton() != EKeys::LeftMouseButton)
            return FReply::Unhandled();
        const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
        const FVector2D CanvasSize = Geo.GetLocalSize();

        // Pin mode: drag moves the selected pinned element's 3D pin
        // (writes through SetGizmoPinUV -> SetNestedPinFromUV).
        if (bPinMode)
        {
            const FVector2D PinUV = Owner->GetSelectedPinUV();
            if (PinUV.X >= 0.0f
                && FVector2D::Distance(Local,
                    UFaceParallaxEditorWidget::GizmoUVToPixels(PinUV, CanvasSize)) < 12.0f)
            {
                PinDragMode = 1;
                return FReply::Handled().CaptureMouse(AsShared());
            }
            return FReply::Unhandled();
        }

        const FFaceArtTransform T = Owner->GetGizmoTransform();
        const FVector2D Center = CanvasSize * 0.5f
            + UFaceParallaxEditorWidget::GizmoUVToPixels(T.Position, CanvasSize);
        const FVector2D Half = UFaceParallaxEditorWidget::GizmoUVToPixels(T.Scale, CanvasSize) * 0.5f;
        const float Rad = FMath::DegreesToRadians(T.Rotation);
        auto RotV = [Rad](const FVector2D& V)
        {
            return FVector2D(V.X * FMath::Cos(Rad) - V.Y * FMath::Sin(Rad),
                             V.X * FMath::Sin(Rad) + V.Y * FMath::Cos(Rad));
        };
        const FVector2D RotHandle = Center + RotV(FVector2D(0.0f, -Half.Y - 14.0f));
        const FVector2D ScaleHandle = Center + RotV(FVector2D(Half.X, Half.Y));
        DragStartMouse = Local;
        DragStartTransform = T;
        DragCenterPx = Center;
        if (FVector2D::Distance(Local, RotHandle) < 12.0f)
            DragMode = 3;
        else if (FVector2D::Distance(Local, ScaleHandle) < 12.0f)
            DragMode = 2;
        else if (FMath::Abs(Local.X - Center.X) <= Half.X + 4.0f
            && FMath::Abs(Local.Y - Center.Y) <= Half.Y + 4.0f)
            DragMode = 1;
        else
            return FReply::Unhandled();
        return FReply::Handled().CaptureMouse(AsShared());
    }

    virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev) override
    {
        if (!Owner) return FReply::Unhandled();
        if (PinDragMode == 1)
        {
            const FVector2D CanvasSize = Geo.GetLocalSize();
            Owner->SetGizmoPinUV(UFaceParallaxEditorWidget::GizmoPixelsToUV(
                Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()), CanvasSize));
            return FReply::Handled();
        }
        if (DragMode == 0) return FReply::Unhandled();
        const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
        const FVector2D CanvasSize = Geo.GetLocalSize();
        FFaceArtTransform NewT = DragStartTransform;
        const FVector2D Delta = Local - DragStartMouse;
        switch (DragMode)
        {
        case 1:
            NewT.Position = DragStartTransform.Position
                + UFaceParallaxEditorWidget::GizmoPixelsToUV(Delta, CanvasSize);
            break;
        case 2:
            {
                const float StartDist = FVector2D::Distance(DragStartMouse, DragCenterPx);
                const float NewDist = FVector2D::Distance(Local, DragCenterPx);
                const float Fac = StartDist > 1.0f ? FMath::Max(0.05f, NewDist / StartDist) : 1.0f;
                NewT.Scale = DragStartTransform.Scale * Fac;
            }
            break;
        case 3:
            {
                const FVector2D StartDir = DragStartMouse - DragCenterPx;
                const FVector2D NewDir = Local - DragCenterPx;
                const float StartAng = FMath::Atan2(StartDir.Y, StartDir.X);
                const float NewAng = FMath::Atan2(NewDir.Y, NewDir.X);
                NewT.Rotation = DragStartTransform.Rotation
                    + FMath::RadiansToDegrees(NewAng - StartAng);
            }
            break;
        }
        Owner->SetGizmoTransform(NewT);
        return FReply::Handled();
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
    {
        if (DragMode == 0 && PinDragMode == 0) return FReply::Unhandled();
        DragMode = 0;
        PinDragMode = 0;
        return FReply::Handled().ReleaseMouseCapture();
    }

private:
    int32 DragMode = 0; // 0 none, 1 move, 2 scale, 3 rotate
    int32 PinDragMode = 0; // 0 none, 1 pin drag (bPinMode only)
    bool bPinMode = false; // true: gizmo edits the selected pinned element instead of the layer transform
    FVector2D DragStartMouse = FVector2D::ZeroVector;
    FFaceArtTransform DragStartTransform;
    FVector2D DragCenterPx = FVector2D::ZeroVector;
};

bool UFaceParallaxEditorWidget::ValidatePreset() const
{
    if (!ActivePreset)
    {
        if (!IsTemplate() && !HasAnyFlags(RF_Transient) && GEditor && !bSuppressValidation)
        {
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxEditorWidget] No ActivePreset assigned."));
        }
        return false;
    }
    return true;
}

bool UFaceParallaxEditorWidget::ValidatePreviewActor() const
{
    if (!PreviewActor.IsValid())
    {
        if (!IsTemplate() && !HasAnyFlags(RF_Transient) && GEditor && !bSuppressValidation)
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

void UFaceParallaxEditorWidget::SetPreviewActor(AFaceParallaxPreviewActor* NewPreviewActor)
{
    PreviewActor = NewPreviewActor;
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Reset Layer Transform"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Sync Layer to All Views"));
    if (!ValidatePreset()) return;
    ActivePreset->SyncCanonicalToAllViews(State, LayerTag);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncTexturesLayerToAllViews(EFaceAngleState State, FName LayerTag)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Sync Textures to All Views"));
    if (!ValidatePreset()) return;
    ActivePreset->SyncTexturesToAllViews(State, LayerTag);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::SyncAllLayersToAllViews()
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Sync All Layers to All Views"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Sync Layer to Selected Views"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set View Override"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear View Override"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearViewOverride(State, LayerTag, OverrideView);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllOverridesForSlot(EFaceAngleState State, FName LayerTag)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear All Overrides for Slot"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearAllOverridesForSlot(State, LayerTag);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllOverrides()
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear All Overrides"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Slot Textures"));
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
    SetStatus(FString::Printf(TEXT("Imported %d texture(s), assigned %d to %s:%s by channel suffix"),
        Imported.Num(), Assigned, *SelectedLayerName.ToString(),
        *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)ActiveViewState)),
        FLinearColor(0.3f, 1.0f, 0.3f));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Blink Frame Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear Blink Frames"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear Expression Textures"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.ExpressionTextures.Remove(Expression);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

void UFaceParallaxEditorWidget::SetExpressionTextures(EFaceAngleState State, FName LayerTag,
    EExpression Expression, const FFaceTextureSet& Textures)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Expression Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Named Expression Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear Named Expression Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Viseme Frame Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear Viseme Frames"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear All Visemes"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Named Viseme Frame Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear Named Viseme Frames"));
    if (!ValidatePreset()) return;
    FFaceArtSlot ArtSlot = ActivePreset->GetSlot(State, LayerTag);
    ArtSlot.NamedVisemeFrames.Remove(VisemeName);
    ActivePreset->SetSlot(State, LayerTag, ArtSlot);
}

void UFaceParallaxEditorWidget::ClearAllNamedVisemes(EFaceAngleState State, FName LayerTag)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear All Named Visemes"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Param Bindings"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Alt Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Swoosh Frame Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear Swoosh Frames"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Nested Element"));
    if (!ValidatePreset()) return;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Element);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::AddNestedElement(EFaceAngleState State, FName LayerTag, const FFaceNestedArt& Element)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Add Nested Element"));
    if (!ValidatePreset()) return;
    ActivePreset->AddNestedElement(State, LayerTag, Element);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::RemoveNestedElement(EFaceAngleState State, FName LayerTag, int32 Index)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Remove Nested Element"));
    if (!ValidatePreset()) return;
    ActivePreset->RemoveNestedElement(State, LayerTag, Index);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (Comp) Comp->ApplyCurrentStateTextures();
}

void UFaceParallaxEditorWidget::SetNestedTextures(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceTextureSet& Textures)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Nested Textures"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Nested Transform"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Nested Pivot"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Nested Jiggle Enabled"));
    if (!ValidatePreset()) return;
    FFaceNestedArt Elem = ActivePreset->GetNestedElement(State, LayerTag, Index);
    Elem.bJiggleEnabled = bEnabled;
    ActivePreset->SetNestedElement(State, LayerTag, Index, Elem);
}

void UFaceParallaxEditorWidget::SetNestedJiggleSettings(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceJiggleSettings& Settings)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Nested Jiggle Settings"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Nested Visibility"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Nested Idle Frames"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear Nested Idle Frames"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear State"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearState(State);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAll()
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear All"));
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Batch Set Textures"));
    if (!ValidatePreset()) return;
    ActivePreset->BatchSetTextures(State, LayerTag, Textures);

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ClearAllTextures()
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Clear All Textures"));
    if (!ValidatePreset()) return;
    ActivePreset->ClearAllTextures();

    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::DuplicateState(EFaceAngleState SourceState, EFaceAngleState DestState)
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Duplicate State"));
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
        .OnClicked_Lambda([T, Fn = MoveTemp(Fn)]()
        {
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxWidget] CLICK '%s'"), *T);
            Fn();
            return FReply::Handled();
        })
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
    UE_LOG(LogTemp, Log, TEXT("[FaceParallaxWidget] REBUILD DOCKED-TAB v3 (marker 0xV3)"));
    if (IsTemplate())
    {
        return SNew(SBox).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Face Parallax Editor")))];
    }

    UFaceParallaxComponent* Comp = GetParallaxComponent();

    // Only auto-discover a preview actor when none is selected — never override the user's choice.
    // (Handles BP re-instancing clearing the reference without clobbering deliberate selections.)
    if (!PreviewActor.IsValid())
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
    // Auto-assign ActivePreset from the selected preview actor's component
    if (PreviewActor.IsValid() && PreviewActor->FaceParallax && PreviewActor->FaceParallax->ActivePreset)
    {
        ActivePreset = PreviewActor->FaceParallax->ActivePreset;
    }
    // Fallback: use the default preset so the editor works even without an actor
    if (!ActivePreset)
    {
        UFaceParallaxPreset* DefaultPreset = LoadObject<UFaceParallaxPreset>(nullptr,
            TEXT("/Game/FaceParallax/Presets/DA_FaceParallax_Default.DA_FaceParallax_Default"));
        if (DefaultPreset)
        {
            ActivePreset = DefaultPreset;
        }
    }

    TArray<FName> LNames;
    if (Comp)
    {
        for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
        {
            FName Tag = Comp->GetLayerDefinition(i).LayerTag;
            if (Tag.IsValid()) LNames.Add(Tag);
        }
    }
    if (LNames.Num() == 0 && ActivePreset)
    {
        LNames = ActivePreset->GetAllLayerTags(ActiveViewState);
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

    auto MakeSectionBox = [this](const FString& Title, TSharedRef<SWidget> Content) -> TSharedRef<SWidget>
    {
        TSharedRef<SVerticalBox> Section = SNew(SVerticalBox)
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
        SectionSectionTitles.Add(Section, Title);
        return Section;
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
                OpenImportArtDialog();
            })];
        SearchBox = SNew(SSearchBox)
            .HintText(FText::FromString(TEXT("Search settings...")))
            .OnTextChanged_Lambda([this](const FText& T) { ApplySearchFilter(T.ToString()); });
        TB->AddSlot().Padding(FMargin(6,2)).AutoWidth()
            [SNew(SBox).WidthOverride(140)[SearchBox.ToSharedRef()]];
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
                UE_LOG(LogTemp, Log, TEXT("[FaceParallaxEditorWidget] %s"), *Msg.ToString());
                if (TextStatus.IsValid())
                    TextStatus->SetText(Msg);
            }, FLinearColor(0.7f,0.7f,0.7f))];
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Spawn Preview"), [this]()
            {
                UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
                if (!World) return;
                AFaceParallaxPreviewActor* NewActor = World->SpawnActor<AFaceParallaxPreviewActor>(
                    AFaceParallaxPreviewActor::StaticClass());
                if (NewActor)
                {
                    SetPreviewActor(NewActor);
                    if (TextStatus.IsValid())
                        TextStatus->SetText(FText::FromString(TEXT("Spawned new PreviewActor")));
                }
            }, FLinearColor(0.5f,1.0f,0.5f))];
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Find Preview"), [this]()
            {
                UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
                if (!World) return;
                int32 Found = 0;
                for (TActorIterator<AFaceParallaxPreviewActor> It(World); It; ++It)
                {
                    ++Found;
                    if (Found == 1)
                    {
                        SetPreviewActor(*It);
                    }
                }
                if (Found > 0)
                {
                    if (TextStatus.IsValid())
                        TextStatus->SetText(FText::FromString(FString::Printf(
                            TEXT("Found %d PreviewActor(s) in level; selected first"), Found)));
                }
                else if (TextStatus.IsValid())
                {
                    TextStatus->SetText(FText::FromString(TEXT("No PreviewActor found in level")));
                }
            }, FLinearColor(0.5f,0.8f,1.0f))];
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Spawn Quads"), [this]()
            {
                SpawnLayerQuadsOnPreview();
            }, FLinearColor(1.0f,0.8f,0.4f))];
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [SNew(SBox).WidthOverride(170)
                [SAssignNew(ActorSelector, SComboBox<TWeakObjectPtr<AFaceParallaxPreviewActor>>)
                    .OptionsSource(&ActorOptions)
                    .OnGenerateWidget_Lambda([](TWeakObjectPtr<AFaceParallaxPreviewActor> Item)
                    {
                        FString Label = Item.IsValid() ? Item->GetActorLabel() : TEXT("None");
                        return SNew(STextBlock).Text(FText::FromString(Label));
                    })
                    .OnSelectionChanged_Lambda([this](TWeakObjectPtr<AFaceParallaxPreviewActor> Item, ESelectInfo::Type)
                    {
                        if (Item.IsValid())
                        {
                            SetPreviewActor(Item.Get());
                            if (TextStatus.IsValid())
                                TextStatus->SetText(FText::FromString(
                                    FString::Printf(TEXT("Selected %s"), *Item->GetActorLabel())));
                        }
                    })
                    [SNew(STextBlock).Text_Lambda([this]()
                    {
                        FString Label = PreviewActor.IsValid() ? PreviewActor->GetActorLabel() : TEXT("None");
                        return FText::FromString(Label);
                    })]]];
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Clear Stale"), [this]()
            {
                ClearStaleTargets();
                RefreshUI();
            }, FLinearColor(1.0f,0.5f,0.5f))];
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
        ViewTabDots.Reset();
        for (auto& St : States)
        {
            EFaceAngleState S = St.S;
            bool IsActive = (S == ActiveViewState);
            TSharedRef<SImage> DotImg = SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush"));
            DotImg->SetColorAndOpacity(GetStateDotColor(S));
            ViewTabDots.Add(DotImg);
            TSharedRef<SBox> Dot = SNew(SBox).WidthOverride(8).HeightOverride(8)[DotImg];
            TSharedRef<SButton> TabBtn = SNew(SButton)
                .ButtonColorAndOpacity(IsActive ? AccentBlue() : FLinearColor(0.12f,0.12f,0.12f))
                .OnClicked_Lambda([this, S](){ SetActiveViewState(S); RefreshUI(); return FReply::Handled(); })
                .Content()
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0,0,3,0))[Dot]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(STextBlock)
                            .Text(FText::FromString(St.T))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", IsActive ? 11 : 9))
                            .ColorAndOpacity(IsActive ? FLinearColor(1,1,1) : St.C)]];
            // Context menu (v arrow): built eagerly — click lambdas capture only [this] and S (safe)
            FString StateName = StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)S);
            TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox)
                .IsEnabled_Lambda([this]() { return SelectedLayerName.IsValid(); });
            Menu->AddSlot().AutoHeight().Padding(FMargin(6,4,6,2))
                [MakeLbl(FString::Printf(TEXT("State: %s"), *StateName), 9, FLinearColor(0.9f,0.8f,0.5f))];
            Menu->AddSlot().AutoHeight().Padding(FMargin(2,1))
                [MakeBtn(TEXT("Sync layer to all views"), [this, S]()
                {
                    if (SelectedLayerName.IsValid()) SyncLayerToAllViews(ActiveViewState, SelectedLayerName);
                    RefreshUI();
                })];
            Menu->AddSlot().AutoHeight().Padding(FMargin(2,1))
                [MakeBtn(TEXT("Sync textures to all views"), [this, S]()
                {
                    if (SelectedLayerName.IsValid()) SyncTexturesLayerToAllViews(ActiveViewState, SelectedLayerName);
                    RefreshUI();
                })];
            Menu->AddSlot().AutoHeight().Padding(FMargin(2,1))
                [MakeBtn(TEXT("Clear overrides (this slot)"), [this, S]()
                {
                    if (SelectedLayerName.IsValid()) ClearAllOverridesForSlot(S, SelectedLayerName);
                    RefreshUI();
                }, FLinearColor(1.0f,0.6f,0.6f), FLinearColor(0.1f,0.1f,0.1f))];
            Menu->AddSlot().AutoHeight().Padding(FMargin(2,1))
                [MakeBtn(TEXT("Fill missing views from this state"), [this, S]()
                {
                    SetActiveViewState(S);
                    FillMissingViewsFromActiveSlot();
                })];
            Menu->AddSlot().AutoHeight().Padding(FMargin(6,6,6,2))
                [MakeLbl(TEXT("Duplicate from:"), 8, FLinearColor(0.6f,0.6f,0.6f))];
            TSharedRef<SScrollBox> DupScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
            for (int32 Di = 0; Di < 10; ++Di)
            {
                EFaceAngleState DS = (EFaceAngleState)Di;
                if (DS == S) continue;
                FString DSName = StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)DS);
                DupScroll->AddSlot().Padding(FMargin(2,1))
                    [MakeBtn(FString::Printf(TEXT("Copy %s -> %s"), *DSName, *StateName),
                        [this, DS, S]()
                        {
                            if (SelectedLayerName.IsValid()) DuplicateState(DS, S);
                            RefreshUI();
                        }, FLinearColor(0.6f,0.8f,1.0f), FLinearColor(0.08f,0.08f,0.08f))];
            }
            Menu->AddSlot().AutoHeight()
                [SNew(SBox).HeightOverride(140)[DupScroll]];
            TSharedRef<SMenuAnchor> AnchorRef = SNew(SMenuAnchor)
                .Placement(MenuPlacement_BelowAnchor)
                .OnGetMenuContent_Lambda([Menu]() { return Menu; });
            AnchorRef->SetContent(SNew(SButton)
                .ButtonColorAndOpacity(FLinearColor(0.08f,0.08f,0.08f))
                .OnClicked_Lambda([AnchorRef]()
                {
                    AnchorRef->SetIsOpen(true, true);
                    return FReply::Handled();
                })
                .Content()
                [SNew(STextBlock)
                    .Text(FText::FromString(TEXT("v")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f))]);
            StateBar->AddSlot().Padding(FMargin(1)).VAlign(VAlign_Fill).HAlign(HAlign_Fill)
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f)[TabBtn]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[AnchorRef]];
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
    // 3. MAIN AREA: RAIL | CANVAS | SLOT PROPS
    // ========================

    // --- Rail containers (created up-front so every section below can target them) ---
    RailContent.SetNum(5);
    for (int32 Ri = 0; Ri < 5; ++Ri)
        RailContent[Ri] = SNew(SVerticalBox);
    RailSwitcher = SNew(SWidgetSwitcher).WidgetIndex(ActiveRailIndex);
    for (int32 Ri = 0; Ri < 5; ++Ri)
        RailSwitcher->AddSlot()[RailContent[Ri].ToSharedRef()];
    SlotPropsBox = SNew(SVerticalBox);
    PropTabContent.Reset();
    PropTabContent.Add(SlotPropsBox);        // [0] right pane
    PropTabContent.Add(RailContent[0]);      // [1] Layers
    PropTabContent.Add(RailContent[1]);      // [2] Transform
    PropTabContent.Add(RailContent[2]);      // [3] Camera
    PropTabContent.Add(RailContent[3]);      // [4] Debug
    PropTabContent.Add(RailContent[4]);      // [5] Advanced

    // --- 3a. LAYERS PANEL (rail 0) ---
    TSharedRef<SVerticalBox> LayerPanel = RailContent[0].ToSharedRef();
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

    // --- 3b. CENTER CANVAS ---
    TSharedRef<SVerticalBox> CenterCol = SNew(SVerticalBox);
    {
        // Display mode row (0 Textured 1 Depth 2 Wireframe 3 Split)
        {
            TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
            struct { int32 M; const TCHAR* T; } Modes[] = {
                {0, TEXT("Textured")}, {1, TEXT("Depth")}, {2, TEXT("Wireframe")}, {3, TEXT("Split")},
            };
            for (auto& Mo : Modes)
            {
                const int32 M = Mo.M;
                ModeRow->AddSlot().Padding(FMargin(1)).AutoWidth()
                    [SNew(SButton)
                        .ButtonColorAndOpacity(DisplayMode == M ? AccentBlue() : FLinearColor(0.13f,0.13f,0.15f))
                        .OnClicked_Lambda([this, M](){ SetDisplayMode(M); return FReply::Handled(); })
                        .Content()
                        [SNew(STextBlock)
                            .Text(FText::FromString(Mo.T))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            .ColorAndOpacity(FLinearColor(0.85f,0.85f,0.85f))]];
            }
            ModeRow->AddSlot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)
                [MakeLbl(TEXT("Display"), 8, FLinearColor(0.6f,0.6f,0.6f))];
            ModeRow->AddSlot().FillWidth(1.0f);
            CenterCol->AddSlot().AutoHeight().Padding(FMargin(2,2,2,0))
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.1f,0.1f,0.1f))
                    .Padding(FMargin(2,1))
                    [ModeRow]];
        }

        // Preview canvas (overlays: outline, onion skin, edge detection, gizmo)
        PreviewImageWidget = SNew(SImage).Image(&PreviewBrush);
        OutlinePreviewImage = SNew(SImage)
            .Image(&OutlineDepthBrush)
            .Visibility_Lambda([this]()
            {
                return bOutlineOverlayVisible && OutlineDepthTexture ? EVisibility::Visible : EVisibility::Collapsed;
            });
        OnionSkinImage = SNew(SImage)
            .Image(FCoreStyle::Get().GetBrush("NoBorder"))
            .Visibility_Lambda([this]() { return bOnionSkin ? EVisibility::Visible : EVisibility::Collapsed; });
        EdgeOverlayImage = SNew(SImage)
            .Image(FCoreStyle::Get().GetBrush("NoBorder"))
            .Visibility_Lambda([this]()
            {
                return bEdgeOverlayVisible && EdgeOverlayTexture ? EVisibility::Visible : EVisibility::Collapsed;
            });
        GizmoWidget = SNew(SFaceLayerGizmo);
        GizmoWidget->Owner = this;
        GizmoLayer = SNew(SBox)
            .Visibility_Lambda([this]() { return SelectedLayerName.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
            [GizmoWidget.ToSharedRef()];
        PreviewHost = SNew(SBox).HeightOverride(450)
            [SNew(SOverlay)
                + SOverlay::Slot()[PreviewImageWidget.ToSharedRef()]
                + SOverlay::Slot()[OutlinePreviewImage.ToSharedRef()]
                + SOverlay::Slot()[OnionSkinImage.ToSharedRef()]
                + SOverlay::Slot()[EdgeOverlayImage.ToSharedRef()]
                + SOverlay::Slot()[GizmoLayer.ToSharedRef()]];
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(2))
            [PreviewHost.ToSharedRef()];

        // Layer label + view info
        TextLayerName = SNew(STextBlock)
            .Text(FText::FromString(SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : TEXT("(no layer)")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            .ColorAndOpacity(FLinearColor(0.9f,0.9f,0.9f));
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(4,2,4,0))
            [TextLayerName.ToSharedRef()];
    }

    // --- 3c. SLOT PROPERTIES (right pane) ---
    TSharedRef<SVerticalBox> PropPanel = SNew(SVerticalBox).Visibility(EVisibility::Visible);
    PropScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
    {
        // Header: layer + state
        PropPanel->AddSlot().AutoHeight().Padding(FMargin(4,4,4,2))
            [MakeLbl(TEXT("SELECTED SLOT"), 10, FLinearColor(0.9f,0.8f,0.5f))];

        // Texture slots with inline import status (thumb + filename + check/warning)
        {
            TSharedRef<SHorizontalBox> ThumbRow = SNew(SHorizontalBox);
            auto MakeThumbCol = [&](const FString& Label,
                TSharedPtr<SImage>& ThumbOut, TSharedPtr<STextBlock>& StatusOut) -> TSharedRef<SVerticalBox>
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
                StatusOut = MakeLbl(TEXT(""), 7, FLinearColor(0.6f,0.6f,0.6f));
                StatusOut->SetAutoWrapText(true);
                Col->AddSlot().AutoHeight().Padding(FMargin(2))
                    [SNew(SBox).WidthOverride(72)[StatusOut.ToSharedRef()]];
                return Col;
            };
            ThumbRow->AddSlot().Padding(FMargin(1))[MakeThumbCol(TEXT("Albedo"), ThumbAlbedo, TextSlotAlbedoStatus)];
            ThumbRow->AddSlot().Padding(FMargin(1))[MakeThumbCol(TEXT("Normal"), ThumbNormal, TextSlotNormalStatus)];
            ThumbRow->AddSlot().Padding(FMargin(1))[MakeThumbCol(TEXT("Depth"), ThumbDepth, TextSlotDepthStatus)];
            PropPanel->AddSlot().AutoHeight().Padding(FMargin(2))
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.06f,0.06f,0.06f))
                    .Padding(FMargin(2))
                    [ThumbRow]];
        }

        // Action buttons row (selected slot)
        {
            TSharedRef<SHorizontalBox> ActRow = SNew(SHorizontalBox);
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [MakeBtn(TEXT("Auto-Fit"), [this](){ if (SelectedLayerName.IsValid()) { ApplyAutoFit(ActiveViewState, SelectedLayerName); RefreshUI(); } })];
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [MakeBtn(TEXT("Reset"), [this](){ if (SelectedLayerName.IsValid()) { ResetLayerTransform(ActiveViewState, SelectedLayerName); RefreshUI(); } })];
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [MakeBtn(TEXT("Sync->All"), [this](){ SyncLayerToAllViews(ActiveViewState, SelectedLayerName); RefreshUI(); })];
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [MakeBtn(TEXT("Sync Tex->All"), [this]()
                {
                    if (!SelectedLayerName.IsValid()) return;
                    SyncTexturesLayerToAllViews(ActiveViewState, SelectedLayerName);
                    RefreshUI();
                })];
            ActRow->AddSlot().Padding(FMargin(2)).AutoWidth()
                [SNew(SCheckBox)
                    .IsChecked(bAutoFitOnAssign ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { bool b = (S == ECheckBoxState::Checked); bAutoFitOnAssign = b; SetAutoFitOnAssign(b); })
                    [MakeLbl(TEXT("AF"), 9, FLinearColor(0.5f,0.7f,0.5f))]];
            PropPanel->AddSlot().AutoHeight().Padding(FMargin(2))
                [ActRow];
        }

        // ============ RIGHT PANE: TRANSFORM / OVERRIDE / SYNC ============
        {
            TSharedRef<SVerticalBox> T0 = SlotPropsBox.ToSharedRef();

            // Transform section (canonical or view-override mode)
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
                AddNumRow(TEXT("Pos X"), EditPosX, [this](float V)
                {
                    if (!SelectedLayerName.IsValid() || !ActivePreset) return;
                    if (bViewOverrideMode)
                    {
                        FFaceArtTransform T = GetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState);
                        T.Position.X = V;
                        SetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState, T);
                    }
                    else
                    {
                        FFaceArtTransform T = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName).CanonicalTransform;
                        SetLayerPosition(ActiveViewState, SelectedLayerName, V, T.Position.Y);
                    }
                });
                AddNumRow(TEXT("Pos Y"), EditPosY, [this](float V)
                {
                    if (!SelectedLayerName.IsValid() || !ActivePreset) return;
                    if (bViewOverrideMode)
                    {
                        FFaceArtTransform T = GetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState);
                        T.Position.Y = V;
                        SetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState, T);
                    }
                    else
                    {
                        FFaceArtTransform T = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName).CanonicalTransform;
                        SetLayerPosition(ActiveViewState, SelectedLayerName, T.Position.X, V);
                    }
                });
                AddNumRow(TEXT("Scale X"), EditScaleX, [this](float V)
                {
                    if (!SelectedLayerName.IsValid() || !ActivePreset) return;
                    if (bViewOverrideMode)
                    {
                        FFaceArtTransform T = GetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState);
                        T.Scale.X = V;
                        SetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState, T);
                    }
                    else
                    {
                        FFaceArtTransform T = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName).CanonicalTransform;
                        SetLayerScale(ActiveViewState, SelectedLayerName, V, T.Scale.Y);
                    }
                });
                AddNumRow(TEXT("Scale Y"), EditScaleY, [this](float V)
                {
                    if (!SelectedLayerName.IsValid() || !ActivePreset) return;
                    if (bViewOverrideMode)
                    {
                        FFaceArtTransform T = GetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState);
                        T.Scale.Y = V;
                        SetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState, T);
                    }
                    else
                    {
                        FFaceArtTransform T = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName).CanonicalTransform;
                        SetLayerScale(ActiveViewState, SelectedLayerName, T.Scale.X, V);
                    }
                });
                AddNumRow(TEXT("Rot"), EditRot, [this](float V)
                {
                    if (!SelectedLayerName.IsValid()) return;
                    if (bViewOverrideMode)
                    {
                        FFaceArtTransform T = GetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState);
                        T.Rotation = V;
                        SetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState, T);
                    }
                    else
                    {
                        SetLayerRotation(ActiveViewState, SelectedLayerName, V);
                    }
                });
                T0->AddSlot()
                    [MakeSectionBox(TEXT("Transform"), XForm)];
            }

            // View override toggle + clear
            {
                TSharedRef<SVerticalBox> OvBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> OvRow = SNew(SHorizontalBox);
                TSharedRef<SCheckBox> OvCheck = SNew(SCheckBox)
                    .IsChecked(bViewOverrideMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { SetViewOverrideMode(S == ECheckBoxState::Checked); });
                CheckViewOverrideMode = OvCheck;
                OvRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[OvCheck];
                TSharedRef<STextBlock> OvLbl = MakeLbl(TEXT("Per-View Mode"), 9, FLinearColor(1.0f,0.8f,0.4f));
                OvLbl->SetToolTipText(FText::FromString(TEXT("When on, the transform fields edit the override for the current view "
                    "instead of the shared canonical transform. Overrides are per-rendered-view deltas applied on top of the canonical base.")));
                OvRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()[OvLbl];
                OvRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                OvRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeBtn(TEXT("Clear Overrides"), [this](){ ClearAllOverridesForSlot(ActiveViewState, SelectedLayerName); RefreshUI(); }, FLinearColor(1.0f,0.6f,0.6f))];
                OvBox->AddSlot().AutoHeight()[OvRow];
                T0->AddSlot()
                    [MakeSectionBox(TEXT("View Override"), OvBox)];
            }

            // Sync picker
            {
                TSharedRef<SVerticalBox> SyncBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> SyncRow = SNew(SHorizontalBox);
                SyncRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeLbl(TEXT("Sync layer to:"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                TSharedRef<SCheckBox> SyncTexCheck = SNew(SCheckBox)
                    .IsChecked(ECheckBoxState::Checked);
                SyncTexCheck->SetToolTipText(FText::FromString(TEXT("Also copy albedo/normal/depth textures to the destination views")));
                CheckSyncTextures = SyncTexCheck;
                SyncRow->AddSlot().Padding(FMargin(8,2)).AutoWidth()[SyncTexCheck];
                SyncRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Tex"), 8, FLinearColor(0.7f,0.7f,0.7f))];
                SyncRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                SyncRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeBtn(TEXT("Sync -> Selected"), [this]()
                    {
                        if (!SelectedLayerName.IsValid()) return;
                        TArray<EFaceAngleState> Dests;
                        for (int32 i = 0; i < SyncViewCheckBoxes.Num() && i < 10; ++i)
                        {
                            if (SyncViewCheckBoxes[i].IsValid() && SyncViewCheckBoxes[i]->IsChecked())
                                Dests.Add((EFaceAngleState)i);
                        }
                        const bool bTex = CheckSyncTextures.IsValid() && CheckSyncTextures->IsChecked();
                        SyncLayerToSelectedViews(ActiveViewState, SelectedLayerName, Dests, bTex);
                        RefreshUI();
                    }, FLinearColor(0.6f,0.8f,1.0f))];
                SyncRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeBtn(TEXT("Sync Both -> All"), [this]()
                    {
                        if (!SelectedLayerName.IsValid()) return;
                        SyncLayerToAllViews(ActiveViewState, SelectedLayerName);
                        SyncTexturesLayerToAllViews(ActiveViewState, SelectedLayerName);
                        RefreshUI();
                    })];
                SyncPickerRow = SyncRow;
                SyncBox->AddSlot().AutoHeight()[SyncRow];

                TSharedRef<SHorizontalBox> PickRow = SNew(SHorizontalBox);
                struct { EFaceAngleState S; const TCHAR* T; } PickStates[] = {
                    {EFaceAngleState::Front, TEXT("F")},
                    {EFaceAngleState::ThreeQuarterRight, TEXT("3R")},
                    {EFaceAngleState::RightProfile, TEXT("PR")},
                    {EFaceAngleState::BackRight, TEXT("BR")},
                    {EFaceAngleState::Back, TEXT("B")},
                    {EFaceAngleState::BackLeft, TEXT("BL")},
                    {EFaceAngleState::LeftProfile, TEXT("PL")},
                    {EFaceAngleState::ThreeQuarterLeft, TEXT("3L")},
                    {EFaceAngleState::Top, TEXT("TP")},
                    {EFaceAngleState::Bottom, TEXT("BT")},
                };
                SyncViewCheckBoxes.Reset();
                for (auto& PS : PickStates)
                {
                    TSharedPtr<SCheckBox> Chk = SNew(SCheckBox)
                        .IsChecked(ECheckBoxState::Unchecked);
                    SyncViewCheckBoxes.Add(Chk);
                    PickRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth()[Chk.ToSharedRef()]
                            + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(1,0,0,0))
                                [MakeLbl(PS.T, 8, FLinearColor(0.7f,0.7f,0.7f))]];
                }
                SyncBox->AddSlot().AutoHeight()[PickRow];
                T0->AddSlot()
                    [MakeSectionBox(TEXT("Sync to Views"), SyncBox)];
            }

            // Alignment (onion skin + link) — right pane
            {
                TSharedRef<SVerticalBox> AlignBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> OnRow = SNew(SHorizontalBox);
                TSharedRef<SCheckBox> OnionCheck = SNew(SCheckBox)
                    .IsChecked(bOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { ToggleOnionSkin(S == ECheckBoxState::Checked); RefreshUI(); });
                OnionCheck->SetToolTipText(FText::FromString(TEXT("Ghosts the adjacent view's albedo at low opacity for alignment")));
                OnRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[OnionCheck];
                OnRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeLbl(TEXT("Onion skin"), 9, FLinearColor(0.6f,0.9f,0.7f))];
                OnRow->AddSlot().Padding(FMargin(6,2)).FillWidth(1.0f).VAlign(VAlign_Center)
                    [SNew(SSlider).Value(OnionSkinOpacity)
                        .OnValueChanged_Lambda([this](float V){ SetOnionSkinOpacity(V); })];
                OnRow->AddSlot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)
                    [MakeLbl(TEXT("opacity"), 8, FLinearColor(0.6f,0.6f,0.6f))];
                AlignBox->AddSlot().AutoHeight()[OnRow];
                TSharedRef<SHorizontalBox> LinkRow = SNew(SHorizontalBox);
                TSharedRef<SCheckBox> LinkCheck = SNew(SCheckBox)
                    .IsChecked(bLinkAcrossViews ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { bLinkAcrossViews = (S == ECheckBoxState::Checked); });
                LinkCheck->SetToolTipText(FText::FromString(TEXT("Edits in this state are broadcast to all other states (Phase B)")));
                LinkRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[LinkCheck];
                LinkRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeLbl(TEXT("Link transform across views"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                AlignBox->AddSlot().AutoHeight()[LinkRow];
                T0->AddSlot()
                    [MakeSectionBox(TEXT("Alignment"), AlignBox)];
            }

            // Outline → depth
            {
                TSharedRef<SVerticalBox> OdBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> OdRow = SNew(SHorizontalBox);
                TSharedRef<SEditableTextBox> GridEdit = SNew(SEditableTextBox)
                    .Text(FText::FromString(TEXT("64")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
                GridEdit->SetToolTipText(FText::FromString(TEXT("Depth grid resolution (8-256). Higher = finer depth buffer, slower to compute.")));
                EditOutlineGridSize = GridEdit;
                TSharedRef<SBox> GridBox = SNew(SBox).WidthOverride(44)[GridEdit];
                auto ReadGrid = [this, GridEdit]() -> int32
                {
                    int32 G = FCString::Atoi(*GridEdit->GetText().ToString());
                    return FMath::Clamp(G, 8, 256);
                };
                OdRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeBtn(TEXT("Generate Depth from Outlines"), [this, ReadGrid](){ GenerateDepthFromOutlines(ReadGrid()); }, FLinearColor(0.5f,1.0f,0.7f))];
                OdRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()[GridBox];
                OdRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Detect Profile"), [this, ReadGrid](){ DetectFaceProfile(); GenerateDepthFromOutlines(ReadGrid()); RefreshUI(); })];
                OdRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                TSharedRef<SCheckBox> OdCheck = SNew(SCheckBox)
                    .IsChecked(bOutlineOverlayVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { SetOutlineOverlayVisible(S == ECheckBoxState::Checked); });
                CheckOutlineOverlay = OdCheck;
                OdRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()[OdCheck];
                OdRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Overlay"), 8, FLinearColor(0.7f,0.7f,0.7f))];
                OdBox->AddSlot().AutoHeight()[OdRow];
                TextOutlineStats = MakeLbl(TEXT("No depth buffer generated yet"), 8, FLinearColor(0.5f,0.5f,0.5f));
                OdBox->AddSlot().AutoHeight().Padding(FMargin(2,1))
                    [TextOutlineStats.ToSharedRef()];

                TSharedRef<SHorizontalBox> OvRows = SNew(SHorizontalBox);
                struct { EFaceAngleState S; const TCHAR* T; } OvPickStates[] = {
                    {EFaceAngleState::Front, TEXT("F")},
                    {EFaceAngleState::ThreeQuarterRight, TEXT("3R")},
                    {EFaceAngleState::RightProfile, TEXT("PR")},
                    {EFaceAngleState::BackRight, TEXT("BR")},
                    {EFaceAngleState::Back, TEXT("B")},
                    {EFaceAngleState::BackLeft, TEXT("BL")},
                    {EFaceAngleState::LeftProfile, TEXT("PL")},
                    {EFaceAngleState::ThreeQuarterLeft, TEXT("3L")},
                    {EFaceAngleState::Top, TEXT("TP")},
                    {EFaceAngleState::Bottom, TEXT("BT")},
                };
                UFaceParallaxComponent* OvComp = GetParallaxComponent();
                OutlineViewChecks.Reset();
                for (auto& PS : OvPickStates)
                {
                    const bool bOn = OvComp && OvComp->IsOutlineViewState(PS.S);
                    TSharedRef<SCheckBox> ChkRef = SNew(SCheckBox)
                        .IsChecked(bOn ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                        .OnCheckStateChanged(FOnCheckStateChanged::CreateLambda(
                            [this, S = PS.S](ECheckBoxState St)
                            {
                                if (UFaceParallaxComponent* C = GetParallaxComponent())
                                {
                                    C->SetOutlineViewEnabled(S, St == ECheckBoxState::Checked);
                                }
                            }));
                    ChkRef->SetToolTipText(FText::FromString(FString::Printf(TEXT("Include %s silhouettes in depth-map generation"),
                        *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)PS.S))));
                    TSharedPtr<SCheckBox> Chk = ChkRef;
                    OutlineViewChecks.Add(Chk);
                    OvRows->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth()[Chk.ToSharedRef()]
                            + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(1,0,0,0))
                                [MakeLbl(PS.T, 8, FLinearColor(0.7f,0.7f,0.7f))]];
                }
                OvRows->AddSlot().Padding(FMargin(6,2)).AutoWidth()
                    [MakeBtn(TEXT("All"), [this]()
                    {
                        for (int32 i = 0; i < 10; ++i)
                        {
                            if (UFaceParallaxComponent* C = GetParallaxComponent()) C->SetOutlineViewEnabled((EFaceAngleState)i, true);
                        }
                        RefreshOutlineViewChecks();
                    })];
                OvRows->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeBtn(TEXT("None"), [this]()
                    {
                        if (UFaceParallaxComponent* C = GetParallaxComponent()) C->ClearOutlineViewStates();
                        RefreshOutlineViewChecks();
                    })];
                OdBox->AddSlot().AutoHeight()[OvRows];
                RailContent[3]->AddSlot()
                    [MakeSectionBox(TEXT("Outline -> Depth"), OdBox)];
            }

            // Camera follow
            {
                TSharedRef<SVerticalBox> CfBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> CfRow = SNew(SHorizontalBox);
                TSharedRef<SCheckBox> CfCheck = SNew(SCheckBox)
                    .IsChecked(bCameraFollowsView ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { SetCameraFollowsView(S == ECheckBoxState::Checked); });
                CheckCameraFollow = CfCheck;
                CfRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[CfCheck];
                CfRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeLbl(TEXT("Camera follows view"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                CfRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                CfRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeBtn(TEXT("Snap Camera"), [this](){ SnapCameraToActiveView(); })];
                CfBox->AddSlot().AutoHeight()[CfRow];
                RailContent[2]->AddSlot()
                    [MakeSectionBox(TEXT("Camera Follow"), CfBox)];
            }

            // Quick actions (batch operations)
            {
                TSharedRef<SVerticalBox> QaBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> QaRow = SNew(SHorizontalBox);
                QaRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeBtn(TEXT("Auto-Fit All"), [this](){ ApplyAutoFitToAllSlots(); RefreshUI(); })];
                QaRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Sync All -> All"), [this](){ SyncAllLayersToAllViews(); RefreshUI(); })];
                QaRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Clear All Overrides"), [this](){ ClearAllOverrides(); RefreshUI(); }, FLinearColor(1.0f,0.6f,0.6f))];
                QaRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Duplicate Front -> This"), [this]()
                    {
                        if (ActiveViewState == EFaceAngleState::Front)
                        {
                            SetStatus(TEXT("Already on Front view"), FLinearColor::Yellow);
                            return;
                        }
                        DuplicateState(EFaceAngleState::Front, ActiveViewState);
                        RefreshUI();
                    })];
                QaRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Fill Missing Views"), [this]()
                    {
                        FillMissingViewsFromActiveSlot();
                    })];
                QaRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                QaBox->AddSlot().AutoHeight()[QaRow];
                RailContent[1]->AddSlot()
                    [MakeSectionBox(TEXT("Quick Actions"), QaBox)];
            }

            // Cross-view transform tools (copy-from + link)
            {
                TSharedRef<SVerticalBox> XvBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> XvRow = SNew(SHorizontalBox);
                CopyFromOptions.Reset();
                for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
                {
                    if (i == (int32)ActiveViewState) continue;
                    CopyFromOptions.Add(MakeShared<FString>(
                        StaticEnum<EFaceAngleState>()->GetNameStringByValue(i)));
                }
                if (CopyFromSelection.IsValid())
                {
                    bool bStillValid = false;
                    for (const TSharedPtr<FString>& Opt : CopyFromOptions)
                    {
                        if (Opt->Equals(*CopyFromSelection)) { bStillValid = true; break; }
                    }
                    if (!bStillValid) CopyFromSelection.Reset();
                }
                if (!CopyFromSelection.IsValid() && CopyFromOptions.Num() > 0)
                    CopyFromSelection = CopyFromOptions[0];
                TSharedRef<SComboBox<TSharedPtr<FString>>> CopyCombo =
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&CopyFromOptions)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                    {
                        return SNew(STextBlock)
                            .Text(FText::FromString(*Item))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
                    {
                        if (Item.IsValid()) CopyFromSelection = Item;
                    })
                    [SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return CopyFromSelection.IsValid()
                                ? FText::FromString(*CopyFromSelection)
                                : FText::FromString(TEXT("View..."));
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))];
                XvRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeLbl(TEXT("Copy from:"), 9, FLinearColor(0.6f,0.9f,0.7f))];
                XvRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [SNew(SBox).WidthOverride(90)[CopyCombo]];
                XvRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Copy -> Active"), [this]()
                    {
                        if (!CopyFromSelection.IsValid()) return;
                        for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
                        {
                            if (i == (int32)ActiveViewState) continue;
                            if (StaticEnum<EFaceAngleState>()->GetNameStringByValue(i).Equals(*CopyFromSelection))
                            {
                                CopyTransformFromView((EFaceAngleState)i, ActiveViewState);
                                RefreshUI();
                                return;
                            }
                        }
                    }, FLinearColor(0.6f,1.0f,0.6f))];
                XvRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                TSharedRef<SCheckBox> LinkCheck = SNew(SCheckBox)
                    .IsChecked(bLinkAcrossViews ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { bLinkAcrossViews = (S == ECheckBoxState::Checked); });
                LinkCheck->SetToolTipText(FText::FromString(TEXT("Edits in this state are broadcast to all other states")));
                XvRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()[LinkCheck];
                XvRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Link"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                XvBox->AddSlot().AutoHeight()[XvRow];
                RailContent[1]->AddSlot()
                    [MakeSectionBox(TEXT("Cross-View Transform"), XvBox)];
            }
        }

        // ============ DEBUG RAIL (import + config) ============
        {
            TSharedRef<SVerticalBox> T1 = SNew(SVerticalBox);

            TSharedRef<SVerticalBox> ImportBox = SNew(SVerticalBox);
            TSharedRef<SHorizontalBox> ImpRow = SNew(SHorizontalBox);
            ImpRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                [MakeBtn(TEXT("Import Art..."), [this](){ OpenImportArtDialog(); }, FLinearColor(0.6f,0.8f,1.0f))];
            ImpRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [MakeBtn(TEXT("Import & Assign"), [this]()
                {
                    TArray<FString> OutFiles;
                    IDesktopPlatform* Platform = FDesktopPlatformModule::Get();
                    if (!Platform || !SelectedLayerName.IsValid()) return;
                    FString DefaultPath = FEditorDirectories::Get().GetLastDirectory(ELastDirectory::UNR);
                    if (DefaultPath.IsEmpty()) DefaultPath = FPaths::ProjectContentDir();
                    uint32 FilterFlags = 0;
                    if (Platform->OpenFileDialog(nullptr, TEXT("Import Texture"),
                        DefaultPath, TEXT(""),
                        TEXT("Image files (*.png, *.jpg, *.tga)|*.png;*.jpg;*.tga|All files (*.*)|*.*"),
                        FilterFlags, OutFiles) && OutFiles.Num() > 0)
                    {
                        FEditorDirectories::Get().SetLastDirectory(ELastDirectory::UNR, FPaths::GetPath(OutFiles[0]));
                        TArray<UTexture2D*> Imported = ImportTexturesFromFiles(OutFiles);
                        int32 Assigned = 0;
                        for (UTexture2D* Tex : Imported)
                        {
                            const FString Channel = ChannelFromTextureName(Tex->GetName());
                            if (AssignTextureToSlot(Tex, ActiveViewState, SelectedLayerName, Channel))
                            {
                                ++Assigned;
                            }
                        }
                        SetStatus(FString::Printf(TEXT("Imported %d, assigned %d to %s:%s by channel suffix"),
                            Imported.Num(), Assigned, *SelectedLayerName.ToString(),
                            *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)ActiveViewState)),
                            FLinearColor(0.3f, 1.0f, 0.3f));
                    }
                }, FLinearColor(0.6f,1.0f,0.6f))];
            ImpRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [MakeBtn(TEXT("Assign CB Selection"), [this]()
                {
                    if (!SelectedLayerName.IsValid()) return;
                    UTexture2D* Tex = GetSelectedContentBrowserTexture();
                    if (Tex)
                    {
                        AssignTextureToSlot(Tex, ActiveViewState, SelectedLayerName,
                            ChannelFromTextureName(Tex->GetName()));
                    }
                    else
                    {
                        SetStatus(TEXT("No texture selected in Content Browser"), FLinearColor::Yellow);
                    }
                })];
            ImpRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
            ImportBox->AddSlot().AutoHeight()[ImpRow];
            T1->AddSlot()
                [MakeSectionBox(TEXT("Import"), ImportBox)];

            // Config checks
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

                T1->AddSlot()
                    [MakeSectionBox(TEXT("Config"), CfgBox.ToSharedRef())];
            }

            // Edge analysis (edge overlay + luminance histogram)
            {
                TSharedRef<SVerticalBox> EdBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> EdRow = SNew(SHorizontalBox);
                CheckEdgeOverlay = SNew(SCheckBox)
                    .IsChecked(bEdgeOverlayVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    {
                        bEdgeOverlayVisible = (S == ECheckBoxState::Checked);
                        if (bEdgeOverlayVisible) BuildEdgeOverlay();
                        RefreshUI();
                    });
                EdRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[CheckEdgeOverlay.ToSharedRef()];
                EdRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Edge Overlay"), 9)];
                CheckHistogram = SNew(SCheckBox)
                    .IsChecked(bHistogramVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    {
                        bHistogramVisible = (S == ECheckBoxState::Checked);
                        if (bHistogramVisible) BuildEdgeOverlay();
                        RefreshUI();
                    });
                EdRow->AddSlot().Padding(FMargin(8,2)).AutoWidth()[CheckHistogram.ToSharedRef()];
                EdRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Histogram"), 9)];
                EdRow->AddSlot().Padding(FMargin(6,2)).AutoWidth()
                    [MakeBtn(TEXT("Rebuild"), [this]()
                    {
                        BuildEdgeOverlay();
                        SetStatus(TEXT("Edge overlay + histogram rebuilt"), FLinearColor(0.6f,1.0f,0.6f));
                    })];
                EdRow->AddSlot().FillWidth(1.0f);
                EdBox->AddSlot().AutoHeight()[EdRow];
                HistogramBox = SNew(SVerticalBox);
                TextHistogramStats = SNew(STextBlock)
                    .Text(FText::FromString(TEXT("")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f))
                    .AutoWrapText(true);
                EdBox->AddSlot().AutoHeight().Padding(FMargin(2))
                    [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                        .BorderBackgroundColor(FLinearColor(0.1f,0.1f,0.1f))
                        .Padding(FMargin(4,2))
                        [SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                                [HistogramBox.ToSharedRef()]
                            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0,2,0,0))
                                [TextHistogramStats.ToSharedRef()]]];
                T1->AddSlot()
                    [MakeSectionBox(TEXT("Edge Analysis"), EdBox)];
            }

            // Depth debug visualizer knobs
            {
                TSharedRef<SVerticalBox> DdBox = SNew(SVerticalBox);
                auto AddDebugSlider = [&](const FString& Label, float Def, float Mn, float Mx,
                    TFunction<void(float)>&& Fn, TSharedPtr<SSlider>& SliderOut, TSharedPtr<STextBlock>& LabelOut)
                {
                    TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                    TSharedRef<STextBlock> ValLbl = MakeLbl(TEXT(""), 8, FLinearColor(0.7f,0.7f,0.7f));
                    TSharedRef<SSlider> Sl = SNew(SSlider).Value(FMath::Clamp((Def - Mn) / (Mx - Mn), 0.0f, 1.0f))
                        .OnValueChanged_Lambda([Fn = MoveTemp(Fn), Mn, Mx, ValLbl](float V)
                        {
                            const float Out = Mn + V * (Mx - Mn);
                            ValLbl->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), Out)));
                            Fn(Out);
                        });
                    SliderOut = Sl;
                    LabelOut = ValLbl;
                    R->AddSlot().Padding(FMargin(0,2)).AutoWidth()[MakeLbl(Label, 9)];
                    R->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)[Sl];
                    R->AddSlot().Padding(FMargin(4,2)).AutoWidth()[ValLbl];
                    DdBox->AddSlot().AutoHeight()[R];
                };
                auto RebuildFromActiveDepth = [this]()
                {
                    UDepthDebugVisualizerComponent* Vis =
                        ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
                    if (!Vis) return;
                    UTexture2D* DepthTex = nullptr;
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    if (Comp) DepthTex = Comp->GetCurrentDepthTexture();
                    if (!DepthTex && ValidatePreset() && SelectedLayerName.IsValid())
                        DepthTex = ActivePreset->GetTexturesForSlot(ActiveViewState, SelectedLayerName).Depth;
                    Vis->RebuildMeshFromDepthMap(DepthTex);
                };
                UDepthDebugVisualizerComponent* Vis0 =
                    ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
                AddDebugSlider(TEXT("Grid Res"), Vis0 ? (float)Vis0->GridResolution : 48.0f, 8.0f, 256.0f,
                    [this, RebuildFromActiveDepth](float V)
                    {
                        UDepthDebugVisualizerComponent* Vis =
                            ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
                        if (Vis) { Vis->GridResolution = FMath::Clamp((int32)V, 8, 256); RebuildFromActiveDepth(); }
                    }, SliderDebugGrid, TextDebugGrid);
                AddDebugSlider(TEXT("Mesh Size"), Vis0 ? Vis0->MeshSize : 30.0f, 5.0f, 100.0f,
                    [this, RebuildFromActiveDepth](float V)
                    {
                        UDepthDebugVisualizerComponent* Vis =
                            ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
                        if (Vis) { Vis->MeshSize = V; RebuildFromActiveDepth(); }
                    }, SliderDebugMeshSize, TextDebugMeshSize);
                AddDebugSlider(TEXT("Height Scale"), Vis0 ? Vis0->HeightScale : 10.0f, 0.5f, 30.0f,
                    [this, RebuildFromActiveDepth](float V)
                    {
                        UDepthDebugVisualizerComponent* Vis =
                            ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
                        if (Vis) { Vis->HeightScale = V; RebuildFromActiveDepth(); }
                    }, SliderDebugHeight, TextDebugHeight);
                AddDebugSlider(TEXT("Offset Z"), Vis0 ? Vis0->LocalOffset.Z : 25.0f, -50.0f, 100.0f,
                    [this, RebuildFromActiveDepth](float V)
                    {
                        UDepthDebugVisualizerComponent* Vis =
                            ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
                        if (Vis) { Vis->LocalOffset.Z = V; RebuildFromActiveDepth(); }
                    }, SliderDebugOffset, TextDebugOffset);

                auto AddColorEdit = [&](const FString& Label, FLinearColor Def,
                    TSharedPtr<SEditableTextBox>& EditOut, TFunction<void(FLinearColor)>&& Fn)
                {
                    TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                    TSharedRef<SEditableTextBox> Edit = SNew(SEditableTextBox)
                        .Text(FText::FromString(Def.ToFColor(true).ToHex()))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                        .OnTextCommitted_Lambda([Fn = MoveTemp(Fn)](const FText& T, ETextCommit::Type)
                        {
                            const FColor C = FColor::FromHex(T.ToString());
                            Fn(FLinearColor(C));
                        });
                    EditOut = Edit;
                    R->AddSlot().Padding(FMargin(0,2)).AutoWidth()[MakeLbl(Label, 9)];
                    R->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                        [SNew(SBox).WidthOverride(70)[Edit]];
                    R->AddSlot().FillWidth(1.0f);
                    DdBox->AddSlot().AutoHeight()[R];
                };
                AddColorEdit(TEXT("Low Color"), Vis0 ? Vis0->LowColor : FLinearColor(0,0,0.8f),
                    EditDebugLowColor, [this, RebuildFromActiveDepth](FLinearColor C)
                    {
                        UDepthDebugVisualizerComponent* Vis =
                            ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
                        if (Vis) { Vis->LowColor = C; RebuildFromActiveDepth(); }
                    });
                AddColorEdit(TEXT("High Color"), Vis0 ? Vis0->HighColor : FLinearColor(0.8f,0,0),
                    EditDebugHighColor, [this, RebuildFromActiveDepth](FLinearColor C)
                    {
                        UDepthDebugVisualizerComponent* Vis =
                            ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
                        if (Vis) { Vis->HighColor = C; RebuildFromActiveDepth(); }
                    });

                TSharedRef<SHorizontalBox> DdRow = SNew(SHorizontalBox);
                DdRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeBtn(TEXT("Rebuild Mesh"), [this, RebuildFromActiveDepth]()
                    {
                        RebuildFromActiveDepth();
                        SetStatus(TEXT("Depth debug mesh rebuilt"), FLinearColor(0.6f,1.0f,0.6f));
                    }, FLinearColor(0.6f,0.8f,1.0f))];
                DdRow->AddSlot().Padding(FMargin(6,2)).AutoWidth()
                    [MakeBtn(TEXT("Color by Depth"), [this]()
                    {
                        if (ValidatePreviewActor() && PreviewActor->DepthDebug)
                            ColorByDepth(true);
                    })];
                DdRow->AddSlot().FillWidth(1.0f);
                DdBox->AddSlot().AutoHeight()[DdRow];
                T1->AddSlot()
                    [MakeSectionBox(TEXT("Depth Debug"), DdBox)];
            }

            // Hull review (orbit 3D preview controls + 10 state thumbnails)
            {
                TSharedRef<SVerticalBox> HrBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> OrbitRow = SNew(SHorizontalBox);
                OrbitRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                        {
                            if (ValidatePreviewActor())
                                PreviewActor->SetAutoRotate(S == ECheckBoxState::Checked);
                        })];
                OrbitRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeLbl(TEXT("Orbit 3D"), 9, FLinearColor(1.0f,0.8f,0.4f))];
                OrbitRow->AddSlot().Padding(FMargin(2,2)).FillWidth(1.0f)
                    [SNew(SSlider).Value(30.0f / 360.0f)
                        .OnValueChanged_Lambda([this](float V){ SetAutoRotateSpeed(V * 359.0f + 1.0f); })];
                OrbitRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Spd"), 9)];
                OrbitRow->AddSlot().Padding(FMargin(6,2)).AutoWidth()
                    [MakeBtn(TEXT("Snap"), [this](){ SnapCameraToActiveView(); })];
                HrBox->AddSlot().AutoHeight()[OrbitRow];

                HullThumbBox = SNew(SVerticalBox);
                RefreshHullThumbnails();
                HrBox->AddSlot().AutoHeight().Padding(FMargin(0,4,0,0))
                    [HullThumbBox.ToSharedRef()];
                T1->AddSlot()
                    [MakeSectionBox(TEXT("Hull Review (click thumb = jump)"), HrBox)];
            }

            // Viseme frames grid (Phase E)
            {
                TSharedRef<SVerticalBox> VgBox = SNew(SVerticalBox);
                TSharedRef<SScrollBox> VgScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
                VisemeGridBox = SNew(SVerticalBox);
                VgScroll->AddSlot()[VisemeGridBox.ToSharedRef()];
                RebuildVisemeGrid();
                VgBox->AddSlot().AutoHeight()[VgScroll];
                T1->AddSlot()
                    [MakeSectionBox(TEXT("Viseme Frames (click filled cell = play)"), VgBox)];
            }

            // Problems panel (Phase F)
            {
                TSharedRef<SVerticalBox> PrBox = SNew(SVerticalBox);
                TSharedRef<SScrollBox> PrScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
                ProblemsPanelBox = SNew(SVerticalBox);
                PrScroll->AddSlot()[ProblemsPanelBox.ToSharedRef()];
                RebuildProblemsPanel();
                PrBox->AddSlot().AutoHeight()[PrScroll];
                T1->AddSlot()
                    [MakeSectionBox(TEXT("Problems (click row = jump)"), PrBox)];
            }

            TSharedRef<SScrollBox> DebugScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
            DebugScroll->AddSlot()[T1];
            RailContent[3]->AddSlot()[DebugScroll];
        }

        // ============ CAMERA RAIL ============
        {
            TSharedRef<SVerticalBox> T2 = RailContent[2].ToSharedRef();

            // Camera section
            {
                TSharedRef<SVerticalBox> Cam = SNew(SVerticalBox);
                auto AddCamSlider = [&](const FString& Label, float Def, float Mn, float Mx,
                    TFunction<void(float)>&& Fn, TSharedPtr<STextBlock>& LabelOut)
                {
                    TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                    TSharedRef<STextBlock> ValLbl = MakeLbl(TEXT(""), 8, FLinearColor(0.7f,0.7f,0.7f));
                    LabelOut = ValLbl;
                    R->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                        [MakeLbl(Label, 9)];
                    R->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)
                        [SNew(SSlider).Value((Def - Mn) / (Mx - Mn))
                            .OnValueChanged_Lambda([Fn = MoveTemp(Fn), Mn, Mx, ValLbl](float V)
                            {
                                const float Out = Mn + V * (Mx - Mn);
                                ValLbl->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), Out)));
                                Fn(Out);
                            })];
                    R->AddSlot().Padding(FMargin(4,2)).AutoWidth()[ValLbl];
                    Cam->AddSlot().AutoHeight()[R];
                };
                AddCamSlider(TEXT("Yaw"), 0, -180, 180, [this](float V){ SetOrbitYaw(V); }, TextCameraYaw);
                AddCamSlider(TEXT("Pitch"), -15, -89, 89, [this](float V){ SetOrbitPitch(V); }, TextCameraPitch);
                AddCamSlider(TEXT("Dist"), 180, 50, 500, [this](float V){ SetOrbitDistance(V); }, TextCameraDist);
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

                // Zone boundary multipliers
                TSharedRef<SHorizontalBox> ZoneRow = SNew(SHorizontalBox);
                ZoneRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeLbl(TEXT("Zones (F/3Q/P/B):"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                auto AddZoneEdit = [&](int32 Zi)
                {
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    float Def = Comp ? UFaceParallaxComponent::GetBoundaryOrDefault(Comp->ZoneBoundaryMultipliers, Zi) : 1.0f;
                    TSharedRef<SEditableTextBox> Edit = SNew(SEditableTextBox)
                        .Text(FText::FromString(FString::Printf(TEXT("%.0f"), Def)))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                        .OnTextCommitted_Lambda([this, Zi](const FText& T, ETextCommit::Type)
                        {
                            float V = FMath::Clamp(FCString::Atof(*T.ToString()), 0.5f, 20.0f);
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (Comp)
                            {
                                if (!Comp->ZoneBoundaryMultipliers.IsValidIndex(Zi))
                                {
                                    Comp->ZoneBoundaryMultipliers.SetNum(4);
                                    static const float DefaultZoneMults[4] = {1.0f, 3.0f, 5.0f, 7.0f};
                                    for (int32 i = 0; i < 4; ++i)
                                        if (Comp->ZoneBoundaryMultipliers[i] == 0.0f)
                                            Comp->ZoneBoundaryMultipliers[i] = DefaultZoneMults[i];
                                }
                                Comp->ZoneBoundaryMultipliers[Zi] = V;
                                RebuildZoneDiagram();
                            }
                        });
                    ZoneRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                        [SNew(SBox).WidthOverride(36)[Edit]];
                };
                AddZoneEdit(0); AddZoneEdit(1); AddZoneEdit(2); AddZoneEdit(3);
                Cam->AddSlot().AutoHeight()[ZoneRow];

                T2->AddSlot()
                    [MakeSectionBox(TEXT("Camera"), Cam)];
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
                T2->AddSlot()
                    [MakeSectionBox(TEXT("Blend Preview"), BlendBox)];
            }
        }

        // ============ ADVANCED RAIL ============
        {
            TSharedRef<SVerticalBox> T3 = RailContent[4].ToSharedRef();

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
            T3->AddSlot()
                [MakeSectionBox(TEXT("All Layers (current state)"), CrossLayerScroll.ToSharedRef())];

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
                T3->AddSlot()
                    [MakeSectionBox(TEXT("Param Reference"), RefBox)];
            }

            // Param bindings table (Phase E)
            {
                TSharedRef<SVerticalBox> PbBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> AddRow = SNew(SHorizontalBox);
                EditParamAddName = SNew(SEditableTextBox)
                    .Text(FText::FromString(TEXT("ParamName")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .HintText(FText::FromString(TEXT("Param name...")));
                AddRow->AddSlot().Padding(FMargin(0,2)).FillWidth(1.0f)
                    [SNew(SBox).WidthOverride(90)[EditParamAddName.ToSharedRef()]];
                AddRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Add"), [this]()
                    {
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp || !SelectedLayerName.IsValid() || !EditParamAddName.IsValid()) return;
                        TArray<FFaceParamBinding> All = GetParamBindings(ActiveViewState, SelectedLayerName);
                        FFaceParamBinding NewB;
                        NewB.ParamName = FName(*EditParamAddName->GetText().ToString());
                        if (NewB.ParamName.IsNone()) NewB.ParamName = FName(TEXT("Param"));
                        All.Add(NewB);
                        SetParamBindings(ActiveViewState, SelectedLayerName, All);
                        RefreshUI();
                    }, FLinearColor(0.6f,1.0f,0.6f), FLinearColor(0.1f,0.1f,0.1f))];
                PbBox->AddSlot().AutoHeight()[AddRow];
                ParamTableBox = SNew(SVerticalBox);
                RebuildParamTable();
                PbBox->AddSlot().AutoHeight().Padding(FMargin(0,2,0,0))
                    [ParamTableBox.ToSharedRef()];
                T3->AddSlot()
                    [MakeSectionBox(TEXT("Param Bindings (state + layer)"), PbBox)];
            }

            // Nested Art / Pin section
            {
                TSharedRef<SVerticalBox> Pin = SNew(SVerticalBox);

                // Element selector stepper
                {
                    TSharedRef<SHorizontalBox> StepRow = SNew(SHorizontalBox);
                    TextPinIndex = MakeLbl(TEXT("0/0"), 9, FLinearColor(0.8f,0.8f,0.8f));
                    StepRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                        [MakeBtn(TEXT("<"), [this]()
                        {
                            if (SelectedNestedElementIndex > 0) --SelectedNestedElementIndex;
                            RefreshUI();
                        })];
                    StepRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                        [TextPinIndex.ToSharedRef()];
                    StepRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                        [MakeBtn(TEXT(">"), [this]()
                        {
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (!Comp || !SelectedLayerName.IsValid()) return;
                            int32 N = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
                            if (SelectedNestedElementIndex + 1 < N) ++SelectedNestedElementIndex;
                            RefreshUI();
                        })];
                    Pin->AddSlot().AutoHeight()[StepRow];
                }

                // bPinned toggle (reads actual state via RefreshPinControls)
                {
                    TSharedRef<SHorizontalBox> PinnedRow = SNew(SHorizontalBox);
                    CheckPinPinned = SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                        {
                            FFaceNestedArt El;
                            int32 Count = 0;
                            if (!GetSelectedPinElement(El, Count)) return;
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (!Comp) return;
                            El.Pin3D.bPinned = (S == ECheckBoxState::Checked);
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                        });
                    PinnedRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                        [CheckPinPinned.ToSharedRef()];
                    PinnedRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                        [MakeLbl(TEXT("Pinned"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                    Pin->AddSlot().AutoHeight()[PinnedRow];
                }

                // Generic slider row: maps slider [0..1] to [Min..Max], shows readout
                auto AddPinSliderRow = [&](const FString& Label, float Min, float Max, float Def,
                    int Precision, TSharedPtr<SSlider>& SliderOut, TSharedPtr<STextBlock>& TextOut,
                    TFunction<void(float)>&& OnChange)
                {
                    TSharedRef<STextBlock> ValLbl = MakeLbl(TEXT(""), 8, FLinearColor(0.7f,0.7f,0.7f));
                    TextOut = ValLbl;
                    TSharedRef<SSlider> Sl = SNew(SSlider).Value(PinSliderNorm(Def, Min, Max))
                        .OnValueChanged_Lambda([Fn = MoveTemp(OnChange), Min, Max, Precision, ValLbl](float V)
                        {
                            const float Out = Min + V * (Max - Min);
                            ValLbl->SetText(FText::FromString(Precision == 1
                                ? FString::Printf(TEXT("%.1f"), Out)
                                : FString::Printf(TEXT("%.2f"), Out)));
                            Fn(Out);
                        });
                    SliderOut = Sl;
                    TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                    R->AddSlot().Padding(FMargin(0,2)).AutoWidth()[MakeLbl(Label, 9)];
                    R->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)[Sl];
                    R->AddSlot().Padding(FMargin(4,2)).AutoWidth()[ValLbl];
                    Pin->AddSlot().AutoHeight()[R];
                };

                FFaceNestedArt InitEl;
                int32 InitCount = 0;
                GetSelectedPinElement(InitEl, InitCount);

                AddPinSliderRow(TEXT("Pin X"), -2.0f, 2.0f, InitEl.Pin3D.Position3D.X, 2, SliderPinX, TextPinX,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.Position3D.X = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });
                AddPinSliderRow(TEXT("Pin Y"), -2.0f, 2.0f, InitEl.Pin3D.Position3D.Y, 2, SliderPinY, TextPinY,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.Position3D.Y = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });
                AddPinSliderRow(TEXT("Pin Z"), -2.0f, 2.0f, InitEl.Pin3D.Position3D.Z, 2, SliderPinZ, TextPinZ,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.Position3D.Z = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });

                // View-angle rotation controls
                {
                    TSharedRef<SHorizontalBox> RotRow = SNew(SHorizontalBox);
                    CheckPinRotEnabled = SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                        {
                            FFaceNestedArt El;
                            int32 Count = 0;
                            if (!GetSelectedPinElement(El, Count)) return;
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (!Comp) return;
                            El.Pin3D.bEnableViewAngleRotation = (S == ECheckBoxState::Checked);
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                        });
                    RotRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                        [CheckPinRotEnabled.ToSharedRef()];
                    RotRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                        [MakeLbl(TEXT("Rotate w/ view angle"), 9, FLinearColor(1.0f,0.8f,0.4f))];
                    Pin->AddSlot().AutoHeight()[RotRow];
                }
                AddPinSliderRow(TEXT("Min Rot"), -180.0f, 180.0f, InitEl.Pin3D.MinRotation, 1, SliderPinMinRot, TextPinMinRot,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.MinRotation = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });
                AddPinSliderRow(TEXT("Max Rot"), -180.0f, 180.0f, InitEl.Pin3D.MaxRotation, 1, SliderPinMaxRot, TextPinMaxRot,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.MaxRotation = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });
                AddPinSliderRow(TEXT("Sens"), -10.0f, 10.0f, InitEl.Pin3D.RotationSensitivity, 2, SliderPinRotSens, TextPinRotSens,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.RotationSensitivity = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });

                Pin->AddSlot().AutoHeight().Padding(FMargin(0,2))
                    [MakeBtn(TEXT("Detect Profile"), [this](){ DetectFaceProfile(); RefreshUI(); })];
                Pin->AddSlot().AutoHeight().Padding(FMargin(0,4,0,0))
                    [MakeLbl(TEXT("Nested Elements"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                NestedOutlinerBox = SNew(SVerticalBox);
                RebuildNestedOutliner();
                Pin->AddSlot().AutoHeight().Padding(FMargin(0,2,0,0))
                    [NestedOutlinerBox.ToSharedRef()];
                T3->AddSlot()
                    [MakeSectionBox(TEXT("Nested Art / Pins"), Pin)];

                RefreshPinControls();
            }
        }

        PropScroll->AddSlot()[SlotPropsBox.ToSharedRef()];
        PropPanel->AddSlot().FillHeight(1.0f)[PropScroll.ToSharedRef()];
        TextStatus = MakeLbl(TEXT("Ready"), 9, FLinearColor(0.5f,0.8f,0.5f));
        PropPanel->AddSlot().AutoHeight().Padding(FMargin(6,2))
            [TextStatus.ToSharedRef()];
    }

    // --- Assemble main row: rail icons | rail switcher | canvas | slot props ---
    {
        TSharedRef<SHorizontalBox> MainRow = SNew(SHorizontalBox);
        // Rail icon column
        TSharedRef<SVerticalBox> RailIcons = SNew(SVerticalBox);
        struct { int32 I; const TCHAR* T; FLinearColor C; } RailDefs[] = {
            {0, TEXT("L"), FLinearColor(0.6f,0.8f,1.0f)},        // Layers
            {1, TEXT("T"), FLinearColor(0.6f,1.0f,0.6f)},        // Transform
            {2, TEXT("C"), FLinearColor(1.0f,0.7f,0.5f)},        // Camera
            {3, TEXT("D"), FLinearColor(1.0f,0.6f,0.8f)},        // Debug
            {4, TEXT("A"), FLinearColor(0.8f,0.7f,1.0f)},        // Advanced
        };
        const TCHAR* RailTooltips[] = {
            TEXT("Layers: layer list, add/remove, status matrix"),
            TEXT("Transform: quick actions, copy-from, link, onion skin"),
            TEXT("Camera: orbit controls, zone boundaries, blend preview"),
            TEXT("Debug: import, config checks, outline->depth, visualizer"),
            TEXT("Advanced: cross-layer overlay, param reference, nested art"),
        };
        for (auto& Rd : RailDefs)
        {
            const int32 RI = Rd.I;
            TSharedRef<SButton> IconBtn = MakeBtn(Rd.T, [this, RI](){ SetActiveRailIndex(RI); },
                ActiveRailIndex == RI ? AccentBlue() : FLinearColor(0.12f,0.12f,0.14f),
                FLinearColor(0.08f,0.08f,0.08f));
            IconBtn->SetToolTipText(FText::FromString(RailTooltips[RI]));
            RailIcons->AddSlot().Padding(FMargin(2,3)).AutoHeight()
                [SNew(SBox).WidthOverride(30).HeightOverride(30)[IconBtn]];
        }
        RailIcons->AddSlot().FillHeight(1.0f);
        MainRow->AddSlot().AutoWidth().VAlign(VAlign_Fill)
            [SNew(SBox).WidthOverride(36)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.05f,0.05f,0.05f))
                    .Padding(FMargin(2,4,0,4))
                    [RailIcons]]];
        MainRow->AddSlot().AutoWidth().VAlign(VAlign_Fill)
            [SNew(SBox).WidthOverride(180)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.07f,0.07f,0.07f))
                    .Padding(FMargin(0))
                    [RailSwitcher.ToSharedRef()]]];
        MainRow->AddSlot().FillWidth(1.0f).VAlign(VAlign_Fill)
            [CenterCol];
        MainRow->AddSlot().AutoWidth().VAlign(VAlign_Fill)
            [SNew(SBox).WidthOverride(300)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.07f,0.07f,0.07f))
                    .Padding(FMargin(0))
                    [PropPanel]]];
        Root->AddSlot().AutoHeight()
            [SNew(SBox).HeightOverride(560)[MainRow]];
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

        // Status matrix detail (inside Layers rail panel)
        StatusMatrixGrid = SNew(SGridPanel);
        StatusMatrixScroll = SNew(SScrollBox).Orientation(Orient_Horizontal);
        StatusMatrixScroll->AddSlot() [StatusMatrixGrid.ToSharedRef()];
        RebuildStatusMatrix();
        RailContent[0]->AddSlot().AutoHeight().Padding(FMargin(2,1))
            [MakeSectionBox(TEXT("Status Detail"), StatusMatrixScroll.ToSharedRef())];

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
            [MakeBtn(TEXT("Snapshot"), [this](){ SnapshotPreset(); if (TextStatus.IsValid()) TextStatus->SetText(FText::FromString(TEXT("Snapshot saved."))); })];
        TSharedRef<SButton> RestoreBtn = MakeBtn(TEXT("Restore Snapshot"), [this](){ RestoreSnapshot(); RefreshUI(); }, FLinearColor(1.0f,0.7f,0.3f));
        RestoreBtn->SetToolTipText(FText::FromString(TEXT("Restores the preset to the last saved Snapshot (this is not a generic undo)")));
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()[RestoreBtn];
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Clear State"), [this]()
            {
                if (!bClearStateArmed)
                {
                    bClearStateArmed = true;
                    if (TextStatus.IsValid()) TextStatus->SetText(FText::FromString(TEXT("Clear State: click again to confirm")));
                    return;
                }
                bClearStateArmed = false;
                ClearState(ActiveViewState);
                RefreshUI();
            })];
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Clear All"), [this]()
            {
                if (!bClearAllArmed)
                {
                    bClearAllArmed = true;
                    if (TextStatus.IsValid()) TextStatus->SetText(FText::FromString(TEXT("Clear All: click again to confirm")));
                    return;
                }
                bClearAllArmed = false;
                ClearAll();
                RefreshUI();
            })];
        BotBar->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Log: ON"), [this]()
            {
                bShowDiagnosticLog = !bShowDiagnosticLog;
                if (TextStatus.IsValid())
                    TextStatus->SetText(FText::FromString(bShowDiagnosticLog ? TEXT("Diagnostic log visible") : TEXT("Diagnostic log hidden")));
            }, FLinearColor(0.6f,0.7f,0.8f))];
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

    // --- Diagnostic Log ---
    DiagnosticLog = SNew(SMultiLineEditableTextBox)
        .Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
        .IsReadOnly(true);
    DiagnosticLogBox = SNew(SBox)
        .HeightOverride(100)
        .Visibility_Lambda([this]()
        {
            return bShowDiagnosticLog ? EVisibility::Visible : EVisibility::Collapsed;
        });
    DiagnosticLogBox->SetContent(DiagnosticLog.ToSharedRef());
    Root->AddSlot().AutoHeight()[DiagnosticLogBox.ToSharedRef()];

    // Bind asset registry callback for auto-refresh
    if (!AssetModifiedHandle.IsValid())
    {
        AssetModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &UFaceParallaxEditorWidget::OnAssetModified);
    }

    // Initial diagnostics and UI population deferred until targets are set
    if (!bSuppressValidation)
    {
        RunDiagnostics();
        RefreshUI();
    }

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

// ====================================================================
// PHASE A: WORKSPACE RAIL
// ====================================================================

void UFaceParallaxEditorWidget::SetActiveRailIndex(int32 Index)
{
    ActiveRailIndex = FMath::Clamp(Index, 0, 4);
    RebuildWidget();
}

FLinearColor UFaceParallaxEditorWidget::GetStateDotColor(EFaceAngleState State) const
{
    if (!ActivePreset || !ValidatePreset())
        return FLinearColor(0.35f, 0.35f, 0.35f);
    TArray<FName> Tags;
    {
        UFaceParallaxComponent* Comp = GetParallaxComponent();
        if (Comp)
        {
            for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
                Tags.Add(Comp->GetLayerDefinition(i).LayerTag);
        }
    }
    if (Tags.Num() == 0)
        return FLinearColor(0.35f, 0.35f, 0.35f);
    bool bAnyOverride = false;
    for (const FName& Tag : Tags)
    {
        if (ActivePreset->HasViewOverride(State, Tag, State))
            bAnyOverride = true;
        const FFaceTextureSet Tex = ActivePreset->GetTexturesForSlot(State, Tag);
        if (!Tex.Albedo)
            return FLinearColor(1.0f, 0.75f, 0.2f); // amber: missing art
    }
    return bAnyOverride ? FLinearColor(1.0f, 0.5f, 0.2f)  // orange: has per-view overrides
                        : FLinearColor(0.3f, 0.9f, 0.3f); // green: complete
}

void UFaceParallaxEditorWidget::RefreshViewStripDots()
{
    for (int32 i = 0; i < ViewTabDots.Num() && i < 10; ++i)
    {
        if (ViewTabDots[i].IsValid())
            ViewTabDots[i]->SetColorAndOpacity(GetStateDotColor((EFaceAngleState)i));
    }
}

int32 UFaceParallaxEditorWidget::FillMissingViewsFromActiveSlot()
{
    if (!SelectedLayerName.IsValid() || !ActivePreset) return 0;
    const FFaceTextureSet Src = ActivePreset->GetTexturesForSlot(ActiveViewState, SelectedLayerName);
    if (!Src.Albedo)
    {
        SetStatus(TEXT("Active slot has no albedo to copy"), FLinearColor::Yellow);
        return 0;
    }
    int32 Filled = 0;
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Fill Missing Views"));
    for (int32 i = 0; i < 10; ++i)
    {
        const EFaceAngleState S = (EFaceAngleState)i;
        if (S == ActiveViewState) continue;
        const FFaceTextureSet Dst = ActivePreset->GetTexturesForSlot(S, SelectedLayerName);
        if (!Dst.Albedo)
        {
            ActivePreset->SetTexturesForSlot(S, SelectedLayerName, Src);
            ++Filled;
        }
    }
    SetStatus(FString::Printf(TEXT("Filled %d view(s) with %s:%s art"),
        Filled, *SelectedLayerName.ToString(),
        *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)ActiveViewState)),
        FLinearColor(0.3f, 1.0f, 0.3f));
    RefreshTextureThumbs();
    RefreshUI();
    return Filled;
}

void UFaceParallaxEditorWidget::RefreshSlotPropStatus()
{
    const FString ActiveStateName = StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)ActiveViewState);
    if (!SelectedLayerName.IsValid() || !ActivePreset)
    {
        if (TextSlotAlbedoStatus.IsValid()) TextSlotAlbedoStatus->SetText(FText::FromString(TEXT("")));
        if (TextSlotNormalStatus.IsValid()) TextSlotNormalStatus->SetText(FText::FromString(TEXT("")));
        if (TextSlotDepthStatus.IsValid()) TextSlotDepthStatus->SetText(FText::FromString(TEXT("")));
        return;
    }
    FFaceTextureSet Tex = ActivePreset->GetTexturesForSlot(ActiveViewState, SelectedLayerName);
    FLinearColor OkCol(0.3f, 0.9f, 0.3f), BadCol(1.0f, 0.6f, 0.4f);
    if (TextSlotAlbedoStatus.IsValid())
    {
        if (Tex.Albedo)
        {
            TextSlotAlbedoStatus->SetColorAndOpacity(OkCol);
            TextSlotAlbedoStatus->SetText(FText::FromString(Tex.Albedo->GetName()));
        }
        else
        {
            TextSlotAlbedoStatus->SetColorAndOpacity(BadCol);
            TextSlotAlbedoStatus->SetText(FText::FromString(TEXT("missing")));
        }
    }
    if (TextSlotNormalStatus.IsValid())
    {
        if (Tex.Normal)
        {
            TextSlotNormalStatus->SetColorAndOpacity(OkCol);
            TextSlotNormalStatus->SetText(FText::FromString(Tex.Normal->GetName()));
        }
        else
        {
            TextSlotNormalStatus->SetColorAndOpacity(BadCol);
            TextSlotNormalStatus->SetText(FText::FromString(TEXT("missing")));
        }
    }
    if (TextSlotDepthStatus.IsValid())
    {
        if (Tex.Depth)
        {
            TextSlotDepthStatus->SetColorAndOpacity(OkCol);
            TextSlotDepthStatus->SetText(FText::FromString(Tex.Depth->GetName()));
        }
        else
        {
            TextSlotDepthStatus->SetColorAndOpacity(BadCol);
            TextSlotDepthStatus->SetText(FText::FromString(TEXT("missing")));
        }
    }
}

// ====================================================================
// PHASE B: ALIGNMENT (onion skin / gizmo / link / copy-from)
// ====================================================================

EFaceAngleState UFaceParallaxEditorWidget::GetAdjacentState(EFaceAngleState S, int32 Offset)
{
    constexpr int32 N = (int32)EFaceAngleState::Bottom + 1;
    const int32 Idx = ((int32)S + Offset) % N;
    return (EFaceAngleState)(Idx < 0 ? Idx + N : Idx);
}

TArray<EFaceAngleState> UFaceParallaxEditorWidget::GetLinkTargets(EFaceAngleState Active)
{
    TArray<EFaceAngleState> Out;
    for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
    {
        if ((EFaceAngleState)i != Active)
            Out.Add((EFaceAngleState)i);
    }
    return Out;
}

FVector2D UFaceParallaxEditorWidget::GizmoUVToPixels(const FVector2D& UV, const FVector2D& CanvasSize)
{
    return FVector2D(UV.X * CanvasSize.X, UV.Y * CanvasSize.Y);
}

FVector2D UFaceParallaxEditorWidget::GizmoPixelsToUV(const FVector2D& Pixels, const FVector2D& CanvasSize)
{
    if (CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f)
        return FVector2D::ZeroVector;
    return FVector2D(Pixels.X / CanvasSize.X, Pixels.Y / CanvasSize.Y);
}

void UFaceParallaxEditorWidget::CopyTransformFromView(EFaceAngleState Src, EFaceAngleState Dst)
{
    if (!ValidatePreset() || !SelectedLayerName.IsValid() || Src == Dst) return;
    const FFaceArtTransform SrcT = ActivePreset->GetSlot(Src, SelectedLayerName).CanonicalTransform;
    ApplyCanonicalTransformWithLink(Dst, SelectedLayerName, SrcT);
}

void UFaceParallaxEditorWidget::ApplyCanonicalTransformWithLink(EFaceAngleState State, FName LayerTag,
    const FFaceArtTransform& T)
{
    if (!ValidatePreset()) return;
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Set Layer Transform"));
    if (bLinkAcrossViews)
    {
        for (EFaceAngleState S : GetLinkTargets(State))
            ActivePreset->SetCanonicalTransform(S, LayerTag, T);
    }
    ActivePreset->SetCanonicalTransform(State, LayerTag, T);
    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

FFaceArtTransform UFaceParallaxEditorWidget::GetGizmoTransform() const
{
    if (!ValidatePreset() || !SelectedLayerName.IsValid())
        return FFaceArtTransform();
    if (bViewOverrideMode)
        return GetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState);
    return ActivePreset->GetSlot(ActiveViewState, SelectedLayerName).CanonicalTransform;
}

void UFaceParallaxEditorWidget::SetGizmoTransform(const FFaceArtTransform& T)
{
    if (!ValidatePreset() || !SelectedLayerName.IsValid()) return;
    const TArray<EFaceAngleState> Targets = bLinkAcrossViews
        ? GetLinkTargets(ActiveViewState)
        : TArray<EFaceAngleState>{ActiveViewState};
    if (bViewOverrideMode)
    {
        for (EFaceAngleState S : Targets)
            SetViewOverride(S, SelectedLayerName, S, T);
    }
    else
    {
        for (EFaceAngleState S : Targets)
            ActivePreset->SetCanonicalTransform(S, SelectedLayerName, T);
    }
    if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
    {
        PreviewActor->FaceParallax->ApplyCurrentStateTextures();
    }
}

void UFaceParallaxEditorWidget::ToggleOnionSkin(bool bEnable)
{
    bOnionSkin = bEnable;
    RefreshOnionSkin();
}

void UFaceParallaxEditorWidget::SetOnionSkinOpacity(float Opacity)
{
    OnionSkinOpacity = FMath::Clamp(Opacity, 0.0f, 1.0f);
    RefreshOnionSkin();
}

void UFaceParallaxEditorWidget::RefreshOnionSkin()
{
    if (OnionSkinImage.IsValid())
    {
        OnionSkinImage->SetVisibility(bOnionSkin ? EVisibility::Visible : EVisibility::Collapsed);
        if (bOnionSkin && SelectedLayerName.IsValid() && ActivePreset)
        {
            // Ghost of the adjacent view state (previous in render order)
            const int32 Cur = (int32)ActiveViewState;
            const EFaceAngleState Adj = (EFaceAngleState)((Cur + 9) % 10);
            UTexture2D* AdjAlbedo = ActivePreset->GetTexturesForSlot(Adj, SelectedLayerName).Albedo;
            if (AdjAlbedo)
            {
                OnionSkinBrush.SetResourceObject(AdjAlbedo);
                OnionSkinImage->SetImage(&OnionSkinBrush);
                OnionSkinImage->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, OnionSkinOpacity));
            }
            else
            {
                OnionSkinImage->SetImage(FCoreStyle::Get().GetBrush("NoBorder"));
            }
        }
    }
}

// ====================================================================
// PHASE C: FOLDER IMPORT WIZARD
// ====================================================================

void UFaceParallaxEditorWidget::RefreshSyncDriftIndicator()
{
    if (!TextSyncDrift.IsValid()) return;
    if (!SelectedLayerName.IsValid() || !ActivePreset)
    {
        TextSyncDrift->SetText(FText::FromString(TEXT("")));
        return;
    }
    const FFaceArtTransform Active = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName).CanonicalTransform;
    int32 Drifted = 0;
    for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
    {
        if (i == (int32)ActiveViewState) continue;
        const FFaceArtTransform T = ActivePreset->GetSlot((EFaceAngleState)i, SelectedLayerName).CanonicalTransform;
        if (T.Position != Active.Position || T.Scale != Active.Scale || T.Rotation != Active.Rotation)
            ++Drifted;
    }
    if (Drifted > 0)
    {
        TextSyncDrift->SetColorAndOpacity(FLinearColor(1.0f, 0.7f, 0.3f));
        TextSyncDrift->SetText(FText::FromString(FString::Printf(TEXT("Drifted: %d/9"), Drifted)));
    }
    else
    {
        TextSyncDrift->SetColorAndOpacity(FLinearColor(0.3f, 0.9f, 0.3f));
        TextSyncDrift->SetText(FText::FromString(TEXT("Synced")));
    }
}

void UFaceParallaxEditorWidget::OpenImportFolderWizard()
{
    if (!GEditor) return;

    struct FWizardState
    {
        FString Folder;
        TArray<FString> Files;              // full paths (images found in folder)
        TArray<int32> PartOfFile;           // part index per file
        TArray<FString> Parts;              // unique part names
        int32 SelectedPart = -1;
    };
    struct FWizardCallbacks
    {
        std::function<void()> RebuildParts;
        std::function<void()> RebuildPreview;
    };
    TSharedPtr<FWizardState> W = MakeShared<FWizardState>();
    TSharedPtr<FWizardCallbacks> CB = MakeShared<FWizardCallbacks>();

    TSharedRef<SVerticalBox> RootV = SNew(SVerticalBox);

    // Title
    RootV->AddSlot().AutoHeight().Padding(FMargin(8,8,8,2))
        [SNew(STextBlock)
            .Text(FText::FromString(TEXT("Import Folder Wizard")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
            .ColorAndOpacity(FLinearColor(0.9f,0.9f,0.9f))];
    RootV->AddSlot().AutoHeight().Padding(FMargin(8,0,8,4))
        [SNew(STextBlock)
            .Text(FText::FromString(TEXT("Name textures {Part}_{View}_{Map}: e.g. Eyes_Front_Normal.png, Eyes_3R_Depth.png. ")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f))
            .AutoWrapText(true)];

    // Folder row
    TSharedRef<SEditableTextBox> FolderEdit = SNew(SEditableTextBox)
        .Text(FText::FromString(TEXT("")))
        .HintText(FText::FromString(TEXT("Folder with textures...")));
    TSharedRef<SHorizontalBox> FolderRow = SNew(SHorizontalBox);
    FolderRow->AddSlot().Padding(FMargin(8,4)).FillWidth(1.0f)[FolderEdit];
    FolderRow->AddSlot().Padding(FMargin(0,4,4,4)).AutoWidth()
        [SNew(SButton)
            .OnClicked_Lambda([FolderEdit]()
            {
                FString OutFolder;
                if (FDesktopPlatformModule::Get()
                    && FDesktopPlatformModule::Get()->OpenDirectoryDialog(nullptr,
                        TEXT("Pick texture folder"), FPaths::ProjectContentDir(), OutFolder))
                {
                    FolderEdit->SetText(FText::FromString(OutFolder));
                }
                return FReply::Handled();
            })
            .Content()
            [SNew(STextBlock)
                .Text(FText::FromString(TEXT("Browse...")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))]];
    RootV->AddSlot().AutoHeight()[FolderRow];

    // Scan row
    TSharedRef<STextBlock> WizardStatus = SNew(STextBlock)
        .Text(FText::FromString(TEXT("Pick a folder, then Scan.")))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
        .ColorAndOpacity(FLinearColor(0.7f,0.7f,0.7f));
    TSharedRef<SHorizontalBox> ScanRow = SNew(SHorizontalBox);
    ScanRow->AddSlot().Padding(FMargin(8,4)).FillWidth(1.0f).VAlign(VAlign_Center)[WizardStatus];
    ScanRow->AddSlot().Padding(FMargin(0,4,8,4)).AutoWidth()
        [SNew(SButton)
            .OnClicked_Lambda([W, CB, WizardStatus, FolderEdit]()
            {
                W->Folder = FolderEdit->GetText().ToString();
                W->Files.Reset(); W->PartOfFile.Reset(); W->Parts.Reset(); W->SelectedPart = -1;
                if (W->Folder.IsEmpty()) return FReply::Handled();
                TArray<FString> Found;
                for (const TCHAR* Ext : {TEXT("*.png"), TEXT("*.jpg"), TEXT("*.jpeg"), TEXT("*.tga")})
                    IFileManager::Get().FindFilesRecursive(Found, *W->Folder, Ext, true, false);
                for (const FString& F : Found)
                {
                    const FString Base = FPaths::GetBaseFilename(F);
                    const FString Channel = ChannelFromTextureName(Base);
                    const FString Base2 = StripChannelSuffix(Base, Channel);
                    FString Suffix;
                    const int32 State = MatchStateSuffix(Base2, Suffix);
                    if (State < 0) continue;
                    FString Part = Suffix.Len() > 0 ? Base2.Left(Base2.Len() - Suffix.Len()) : TEXT("(root)");
                    if (Part.IsEmpty()) Part = TEXT("(root)");
                    int32 PartIdx = W->Parts.IndexOfByKey(Part);
                    if (PartIdx == INDEX_NONE)
                    {
                        W->Parts.Add(Part);
                        PartIdx = W->Parts.Num() - 1;
                    }
                    W->Files.Add(F);
                    W->PartOfFile.Add(PartIdx);
                }
                WizardStatus->SetText(FText::FromString(FString::Printf(
                    TEXT("Scanned %s: %d parts, %d matching files. Click a part to preview."),
                    *W->Folder, W->Parts.Num(), W->Files.Num())));
                if (CB->RebuildParts) CB->RebuildParts();
                return FReply::Handled();
            })
            .Content()
            [SNew(STextBlock)
                .Text(FText::FromString(TEXT("Scan")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))]];
    RootV->AddSlot().AutoHeight()[ScanRow];

    // Parts row
    TSharedRef<SVerticalBox> PartsBox = SNew(SVerticalBox);
    PartsBox->AddSlot().AutoHeight().Padding(FMargin(8,4,8,0))
        [SNew(STextBlock)
            .Text(FText::FromString(TEXT("Parts:")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.9f))];
    TSharedRef<SHorizontalBox> PartsRow = SNew(SHorizontalBox);
    TSharedRef<SScrollBox> PartsScroll = SNew(SScrollBox).Orientation(Orient_Horizontal);
    PartsScroll->AddSlot()[PartsRow];
    PartsBox->AddSlot().AutoHeight().Padding(FMargin(8,2,8,0))
        [SNew(SBox).HeightOverride(34)[PartsScroll]];
    RootV->AddSlot().AutoHeight()[PartsBox];

    // Preview box
    TSharedRef<SVerticalBox> PreviewBox = SNew(SVerticalBox);
    PreviewBox->AddSlot().AutoHeight().Padding(FMargin(8,4,8,0))
        [SNew(STextBlock)
            .Text(FText::FromString(TEXT("Matches (per view):")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.9f))];
    TSharedRef<SVerticalBox> PreviewRows = SNew(SVerticalBox);
    TSharedRef<SScrollBox> PreviewScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
    PreviewScroll->AddSlot()[PreviewRows];
    PreviewBox->AddSlot().AutoHeight().Padding(FMargin(8,2,8,0))
        [SNew(SBox).HeightOverride(210)[PreviewScroll]];
    RootV->AddSlot().AutoHeight()[PreviewBox];

    CB->RebuildPreview = [W, PreviewRows]()
    {
        PreviewRows->ClearChildren();
        if (W->SelectedPart < 0)
        {
            PreviewRows->AddSlot().AutoHeight().Padding(FMargin(4))
                [SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Select a part to preview its view coverage.")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f))];
            return;
        }
        const FLinearColor FoundCol(0.3f, 0.9f, 0.3f), MissCol(0.2f, 0.2f, 0.2f);
        for (int32 S = 0; S <= (int32)EFaceAngleState::Bottom; ++S)
        {
            TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
            Row->AddSlot().Padding(FMargin(2,1)).AutoWidth().VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(110)
                    [SNew(STextBlock)
                        .Text(FText::FromString(StaticEnum<EFaceAngleState>()->GetNameStringByValue(S)))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                        .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.8f))]];
            const TCHAR* Channels[] = {TEXT("Albedo"), TEXT("Normal"), TEXT("Depth")};
            for (int32 C = 0; C < 3; ++C)
            {
                FString FoundFile;
                FString FoundExt;
                for (int32 i = 0; i < W->Files.Num(); ++i)
                {
                    if (W->PartOfFile[i] != W->SelectedPart) continue;
                    const FString Base = FPaths::GetBaseFilename(W->Files[i]);
                    const FString Ch = ChannelFromTextureName(Base);
                    if (Ch != Channels[C]) continue;
                    FString Suffix;
                    if (MatchStateSuffix(StripChannelSuffix(Base, Ch), Suffix) == S)
                    {
                        FoundFile = Base;
                        FoundExt = FPaths::GetExtension(W->Files[i]);
                        break;
                    }
                }
                Row->AddSlot().Padding(FMargin(2,1)).AutoWidth().VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(8).HeightOverride(8)
                        [SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
                            .ColorAndOpacity(FoundFile.IsEmpty() ? MissCol : FoundCol)]];
                Row->AddSlot().Padding(FMargin(2,1)).AutoWidth().VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(90)
                        [SNew(STextBlock)
                            .Text(FText::FromString(FoundFile.IsEmpty()
                                ? TEXT("\u2014")
                                : FString::Printf(TEXT("%s.%s"), *FoundFile, *FoundExt)))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                            .ColorAndOpacity(FoundFile.IsEmpty()
                                ? FLinearColor(0.4f,0.4f,0.4f)
                                : FLinearColor(0.8f,0.8f,0.8f))]];
            }
            PreviewRows->AddSlot().AutoHeight()[Row];
        }
    };

    CB->RebuildParts = [W, CB, PartsRow]()
    {
        PartsRow->ClearChildren();
        for (int32 P = 0; P < W->Parts.Num(); ++P)
        {
            const int32 PartIdx = P;
            const bool bSel = (P == W->SelectedPart);
            TSharedRef<SButton> Btn = SNew(SButton)
                .ButtonColorAndOpacity(bSel ? AccentBlue() : FLinearColor(0.12f,0.12f,0.14f))
                .OnClicked_Lambda([W, CB, PartIdx]()
                {
                    W->SelectedPart = PartIdx;
                    if (CB->RebuildParts) CB->RebuildParts();
                    if (CB->RebuildPreview) CB->RebuildPreview();
                    return FReply::Handled();
                })
                .Content()
                [SNew(STextBlock)
                    .Text(FText::FromString(W->Parts[P]))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FLinearColor(0.85f,0.85f,0.85f))];
            PartsRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()[Btn];
        }
    };

    // Bottom bar: hint + Apply + Close
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Import Folder Wizard")))
        .ClientSize(FVector2D(660, 480))
        .SupportsMaximize(false)
        .SupportsMinimize(false);
    TSharedRef<SHorizontalBox> BotRow = SNew(SHorizontalBox);
    BotRow->AddSlot().Padding(FMargin(8,4)).FillWidth(1.0f)
        [SNew(STextBlock)
            .Text(FText::FromString(TEXT("Applies the selected part's textures to the active layer.")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(FLinearColor(0.6f,0.6f,0.6f))];
    BotRow->AddSlot().Padding(FMargin(4,4)).AutoWidth()
        [SNew(SButton)
            .OnClicked_Lambda([W, Window, this]()
            {
                if (W->SelectedPart < 0 || !SelectedLayerName.IsValid() || !ActivePreset)
                {
                    SetStatus(TEXT("Wizard: select a part first (and a layer in the editor)"), FLinearColor::Yellow);
                    return FReply::Handled();
                }
                TArray<FString> SelFiles;
                for (int32 i = 0; i < W->Files.Num(); ++i)
                    if (W->PartOfFile[i] == W->SelectedPart) SelFiles.Add(W->Files[i]);
                TArray<UTexture2D*> Imported = ImportTexturesFromFiles(SelFiles);
                int32 Assigned = 0;
                for (int32 i = 0; i < Imported.Num() && i < SelFiles.Num(); ++i)
                {
                    if (!Imported[i]) continue;
                    const FString Base = FPaths::GetBaseFilename(SelFiles[i]);
                    const FString Channel = ChannelFromTextureName(Base);
                    const FString Base2 = StripChannelSuffix(Base, Channel);
                    FString Suffix;
                    const int32 State = MatchStateSuffix(Base2, Suffix);
                    if (State < 0) continue;
                    FFaceTextureSet Set = ActivePreset->GetTexturesForSlot((EFaceAngleState)State, SelectedLayerName);
                    if (Channel == TEXT("Normal")) Set.Normal = Imported[i];
                    else if (Channel == TEXT("Depth")) Set.Depth = Imported[i];
                    else Set.Albedo = Imported[i];
                    ActivePreset->SetTexturesForSlot((EFaceAngleState)State, SelectedLayerName, Set);
                    ++Assigned;
                }
                SetStatus(FString::Printf(TEXT("Wizard: imported %d, assigned %d for part '%s'"),
                    Imported.Num(), Assigned, *W->Parts[W->SelectedPart]),
                    FLinearColor(0.3f, 1.0f, 0.3f));
                RefreshTextureThumbs();
                RefreshUI();
                Window->RequestDestroyWindow();
                return FReply::Handled();
            })
            .Content()
            [SNew(STextBlock)
                .Text(FText::FromString(TEXT("Apply to Active Layer")))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))]];
    BotRow->AddSlot().Padding(FMargin(4,4,8,4)).AutoWidth()
        [SNew(SButton)
            .OnClicked_Lambda([Window]()
            {
                Window->RequestDestroyWindow();
                return FReply::Handled();
            })
            .Content()
            [SNew(STextBlock)
                .Text(FText::FromString(TEXT("Close")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))]];
    RootV->AddSlot().AutoHeight()[BotRow];

    Window->SetContent(RootV);
    FSlateApplication::Get().AddWindow(Window);
}

// Phase D: 16-bin luminance histogram, bins normalized by the max count.
// (mirrored by TestPhaseDMirrors in Tests/ParallaxMathTests.cpp)
void UFaceParallaxEditorWidget::BuildLumaHistogram(const TArray<float>& Luma, int32 Grid, TArray<float>& OutBins)
{
    OutBins.Reset();
    OutBins.SetNum(16);
    if (Luma.Num() != Grid * Grid) return;
    for (float V : Luma)
    {
        const int32 B = FMath::Clamp((int32)FMath::FloorToInt(FMath::Clamp(V, 0.0f, 1.0f) * 15.9999f), 0, 15);
        OutBins[B] += 1.0f;
    }
    float MaxB = 1.0f;
    for (float B : OutBins) MaxB = FMath::Max(MaxB, B);
    for (float& B : OutBins) B /= MaxB;
}

// Phase D: fraction of interior pixels whose Sobel edge magnitude exceeds the threshold.
// (mirrored by TestPhaseDMirrors in Tests/ParallaxMathTests.cpp)
float UFaceParallaxEditorWidget::EdgeDensity(const TArray<float>& Luma, int32 Grid, float Threshold)
{
    if (Luma.Num() != Grid * Grid || Grid < 3) return 0.0f;
    int32 Edges = 0;
    for (int32 Y = 1; Y < Grid - 1; ++Y)
    {
        for (int32 X = 1; X < Grid - 1; ++X)
        {
            const float TL = Luma[(Y - 1) * Grid + X - 1];
            const float TC = Luma[(Y - 1) * Grid + X];
            const float TR = Luma[(Y - 1) * Grid + X + 1];
            const float ML = Luma[Y * Grid + X - 1];
            const float MR = Luma[Y * Grid + X + 1];
            const float BL = Luma[(Y + 1) * Grid + X - 1];
            const float BC = Luma[(Y + 1) * Grid + X];
            const float BR = Luma[(Y + 1) * Grid + X + 1];
            const float Gx = (TR + 2.0f * MR + BR) - (TL + 2.0f * ML + BL);
            const float Gy = (BL + 2.0f * BC + BR) - (TL + 2.0f * TC + TR);
            const float Mag = FMath::Sqrt(Gx * Gx + Gy * Gy) / 4.0f;
            if (Mag > Threshold) ++Edges;
        }
    }
    const int32 Interior = (Grid - 2) * (Grid - 2);
    return Interior > 0 ? (float)Edges / (float)Interior : 0.0f;
}

// ====================================================================
// PHASE D: DISPLAY MODE + DEBUG SLIDERS
// ====================================================================

void UFaceParallaxEditorWidget::SetDisplayMode(int32 Mode)
{
    DisplayMode = FMath::Clamp(Mode, 0, 3);
    // 0 Textured: albedo quads only; 1 Depth: color-by-depth mesh; 2 Wireframe: mesh wireframe; 3 Split: both
    ShowTextures(DisplayMode == 0 || DisplayMode == 3);
    ShowDepthMesh(DisplayMode == 1 || DisplayMode == 3);
    ShowWireframe(DisplayMode == 2);
    RefreshUI();
}

void UFaceParallaxEditorWidget::RefreshDebugSliders()
{
    UDepthDebugVisualizerComponent* Vis =
        ValidatePreviewActor() ? PreviewActor->DepthDebug : nullptr;
    if (!Vis) return;
    auto ApplySlider = [](const TSharedPtr<SSlider>& S, float Min, float Max, float Val,
        const TSharedPtr<STextBlock>& Lbl)
    {
        if (S.IsValid())
            S->SetValue(FMath::Clamp((Val - Min) / (Max - Min), 0.0f, 1.0f));
        if (Lbl.IsValid())
            Lbl->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), Val)));
    };
    ApplySlider(SliderDebugGrid, 8.0f, 256.0f, (float)Vis->GridResolution, TextDebugGrid);
    ApplySlider(SliderDebugMeshSize, 5.0f, 100.0f, Vis->MeshSize, TextDebugMeshSize);
    ApplySlider(SliderDebugHeight, 0.5f, 30.0f, Vis->HeightScale, TextDebugHeight);
    ApplySlider(SliderDebugOffset, -50.0f, 100.0f, Vis->LocalOffset.Z, TextDebugOffset);
}

void UFaceParallaxEditorWidget::BuildEdgeOverlay()
{
    EdgeOverlayTexture = nullptr;
    if (TextHistogramStats.IsValid())
        TextHistogramStats->SetText(FText::FromString(TEXT("No overlay — select a layer with albedo")));
    if (!ValidatePreset() || !SelectedLayerName.IsValid()) return;

    UTexture2D* Tex = ActivePreset->GetTexturesForSlot(ActiveViewState, SelectedLayerName).Albedo;
    if (!Tex) return;

    const int32 SW = Tex->Source.GetSizeX();
    const int32 SH = Tex->Source.GetSizeY();
    if (SW == 0 || SH == 0) return;
    TArray64<uint8> Data;
    if (!Tex->Source.GetMipData(Data, 0)) return;
    const int32 BPP = Tex->Source.GetBytesPerPixel();
    if (BPP < 3) return;

    const int32 Grid = FMath::Clamp(EdgeGridSize, 16, 128);
    TArray<float> Luma;
    Luma.Reserve(Grid * Grid);
    for (int32 Gy = 0; Gy < Grid; ++Gy)
    {
        const int32 Sy = FMath::Clamp((int32)((float)Gy / (float)Grid * (float)SH), 0, SH - 1);
        for (int32 Gx = 0; Gx < Grid; ++Gx)
        {
            const int32 Sx = FMath::Clamp((int32)((float)Gx / (float)Grid * (float)SW), 0, SW - 1);
            const int32 O = (Sy * SW + Sx) * BPP;
            const float R = Data[O + 0] / 255.0f;
            const float G = Data[O + 1] / 255.0f;
            const float B = Data[O + 2] / 255.0f;
            Luma.Add(0.2126f * R + 0.7152f * G + 0.0722f * B);
        }
    }

    BuildLumaHistogram(Luma, Grid, HistogramBins);
    const float Density = EdgeDensity(Luma, Grid, EdgeThreshold);

    if (!EdgeOverlayTexture)
        EdgeOverlayTexture = UTexture2D::CreateTransient(Grid, Grid, PF_B8G8R8A8);
    if (!EdgeOverlayTexture) return;
    EdgeOverlayTexture->Source.Init(Grid, Grid, 1, 1, TSF_BGRA8);
    uint8* Px = (uint8*)EdgeOverlayTexture->Source.LockMip(0);
    if (Px)
    {
        for (int32 Y = 0; Y < Grid; ++Y)
        {
            for (int32 X = 0; X < Grid; ++X)
            {
                bool bEdge = false;
                if (X > 0 && X < Grid - 1 && Y > 0 && Y < Grid - 1)
                {
                    const float TL = Luma[(Y - 1) * Grid + X - 1];
                    const float TC = Luma[(Y - 1) * Grid + X];
                    const float TR = Luma[(Y - 1) * Grid + X + 1];
                    const float ML = Luma[Y * Grid + X - 1];
                    const float MR = Luma[Y * Grid + X + 1];
                    const float BL = Luma[(Y + 1) * Grid + X - 1];
                    const float BC = Luma[(Y + 1) * Grid + X];
                    const float BR = Luma[(Y + 1) * Grid + X + 1];
                    const float Gx = (TR + 2.0f * MR + BR) - (TL + 2.0f * ML + BL);
                    const float Gy = (BL + 2.0f * BC + BR) - (TL + 2.0f * TC + TR);
                    const float Mag = FMath::Sqrt(Gx * Gx + Gy * Gy) / 4.0f;
                    bEdge = Mag > EdgeThreshold;
                }
                const int32 I = (Y * Grid + X) * 4;
                Px[I + 0] = 0;
                Px[I + 1] = 255;
                Px[I + 2] = 0;
                Px[I + 3] = bEdge ? 255 : 0;
            }
        }
    }
    EdgeOverlayTexture->Source.UnlockMip(0);
    EdgeOverlayTexture->UpdateResource();

    EdgeOverlayBrush.SetResourceObject(EdgeOverlayTexture);
    EdgeOverlayBrush.ImageSize = FVector2D((float)Grid, (float)Grid);
    EdgeOverlayBrush.DrawAs = ESlateBrushDrawType::Image;

    if (TextHistogramStats.IsValid())
    {
        float Mean = 0.0f;
        for (float V : Luma) Mean += V;
        Mean = Luma.Num() > 0 ? Mean / (float)Luma.Num() : 0.0f;
        TextHistogramStats->SetText(FText::FromString(FString::Printf(
            TEXT("%s @ %dx%d — edge density %.1f%%, mean luma %.2f"),
            *Tex->GetName(), Grid, Grid, Density * 100.0f, Mean)));
    }
    RebuildHistogramBars();
}

void UFaceParallaxEditorWidget::RebuildHistogramBars()
{
    if (!HistogramBox.IsValid()) return;
    HistogramBox->ClearChildren();
    if (!bHistogramVisible || HistogramBins.Num() != 16) return;
    TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
    for (int32 i = 0; i < 16; ++i)
    {
        const float H = FMath::Clamp(HistogramBins[i], 0.0f, 1.0f);
        const float T = i / 15.0f;
        Row->AddSlot().Padding(FMargin(1)).AutoWidth()
            [SNew(SBox).WidthOverride(6).HeightOverride(FMath::Max(2.0f, H * 44.0f))
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(T, 0.2f, 1.0f - T, 1.0f))]];
    }
    HistogramBox->AddSlot().AutoHeight()
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[Row]
            + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpacer)]];
}

void UFaceParallaxEditorWidget::RefreshHullThumbnails()
{
    if (!HullThumbBox.IsValid()) return;
    HullThumbBox->ClearChildren();
    HullThumbBrushes.SetNum(10);
    TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
    for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
    {
        HullThumbBrushes[i] = FSlateBrush();
        if (ValidatePreset() && SelectedLayerName.IsValid())
        {
            UTexture2D* T = ActivePreset->GetTexturesForSlot((EFaceAngleState)i, SelectedLayerName).Albedo;
            if (T)
            {
                HullThumbBrushes[i].SetResourceObject(T);
                HullThumbBrushes[i].ImageSize = FVector2D(64.0f, 48.0f);
                HullThumbBrushes[i].DrawAs = ESlateBrushDrawType::Image;
            }
        }
        const EFaceAngleState St = (EFaceAngleState)i;
        const FString Name = StaticEnum<EFaceAngleState>()->GetNameStringByValue(i);
        TSharedRef<SButton> Btn = SNew(SButton)
            .ButtonColorAndOpacity(ActiveViewState == St ? AccentBlue() : FLinearColor(0.12f, 0.12f, 0.14f))
            .OnClicked_Lambda([this, St](){ SetActiveViewState(St); return FReply::Handled(); })
            .Content()
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [SNew(SBox).WidthOverride(64).HeightOverride(48)
                        [SNew(SImage).Image(&HullThumbBrushes[i])]]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [SNew(STextBlock)
                        .Text(FText::FromString(Name))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                        .ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))]];
        Grid->AddSlot(i % 5, i / 5).Padding(FMargin(2))[Btn];
    }
    HullThumbBox->AddSlot().AutoHeight()
        [SNew(SBox)[Grid]];
}

// ====================================================================
// PHASE E/F: TIMELINE + PROBLEMS
// ====================================================================

bool UFaceParallaxEditorWidget::GetSelectedPinElement(FFaceNestedArt& OutEl, int32& OutCount)
{
    OutCount = 0;
    if (!SelectedLayerName.IsValid()) return false;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return false;
    OutCount = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
    if (OutCount <= 0) return false;
    if (SelectedNestedElementIndex >= OutCount)
        SelectedNestedElementIndex = OutCount - 1;
    if (SelectedNestedElementIndex < 0)
        SelectedNestedElementIndex = 0;
    OutEl = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex);
    return true;
}

FVector2D UFaceParallaxEditorWidget::GetSelectedPinUV()
{
    FFaceNestedArt El;
    int32 Count = 0;
    if (!GetSelectedPinElement(El, Count) || !El.Pin3D.bPinned) return FVector2D(-1.0f, -1.0f);
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return FVector2D(-1.0f, -1.0f);
    return Comp->ProjectPinToUVForState(El.Pin3D.Position3D, ActiveViewState);
}

void UFaceParallaxEditorWidget::SetGizmoPinUV(const FVector2D& UV)
{
    FFaceNestedArt El;
    int32 Count = 0;
    if (!GetSelectedPinElement(El, Count) || !El.Pin3D.bPinned) return;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return;
    const FVector2D Clamped(FMath::Clamp(UV.X, 0.0f, 1.0f), FMath::Clamp(UV.Y, 0.0f, 1.0f));
    SetNestedPinFromUV(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex,
        ActiveViewState, Clamped);
    RefreshPinControls();
}

void UFaceParallaxEditorWidget::RefreshPinControls()
{
    FFaceNestedArt El;
    int32 Count = 0;
    const bool bHasElement = GetSelectedPinElement(El, Count);

    if (TextPinIndex.IsValid())
    {
        FString IdxStr = bHasElement
            ? FString::Printf(TEXT("%d/%d"), SelectedNestedElementIndex + 1, Count)
            : TEXT("0/0");
        TextPinIndex->SetText(FText::FromString(IdxStr));
    }
    if (CheckPinPinned.IsValid())
    {
        CheckPinPinned->SetIsChecked(bHasElement && El.Pin3D.bPinned ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    }
    if (CheckPinRotEnabled.IsValid())
    {
        CheckPinRotEnabled->SetIsChecked(bHasElement && El.Pin3D.bEnableViewAngleRotation ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    }

    auto SetSliderReadout = [](const TSharedPtr<SSlider>& Sl, const TSharedPtr<STextBlock>& Txt,
        float Value, float Min, float Max, const FString& Readout)
    {
        if (Sl.IsValid()) Sl->SetValue(PinSliderNorm(Value, Min, Max));
        if (Txt.IsValid()) Txt->SetText(FText::FromString(Readout));
    };
    auto SetCtrlEnabled = [](const TSharedPtr<SWidget>& W, bool Enabled)
    {
        if (W.IsValid()) W->SetEnabled(Enabled);
    };
    SetCtrlEnabled(CheckPinPinned, bHasElement);
    SetCtrlEnabled(CheckPinRotEnabled, bHasElement);
    SetCtrlEnabled(SliderPinX, bHasElement);
    SetCtrlEnabled(SliderPinY, bHasElement);
    SetCtrlEnabled(SliderPinZ, bHasElement);
    SetCtrlEnabled(SliderPinMinRot, bHasElement);
    SetCtrlEnabled(SliderPinMaxRot, bHasElement);
    SetCtrlEnabled(SliderPinRotSens, bHasElement);
    if (bHasElement)
    {
        SetSliderReadout(SliderPinX, TextPinX, El.Pin3D.Position3D.X, -2.0f, 2.0f,
            FString::Printf(TEXT("%.2f"), El.Pin3D.Position3D.X));
        SetSliderReadout(SliderPinY, TextPinY, El.Pin3D.Position3D.Y, -2.0f, 2.0f,
            FString::Printf(TEXT("%.2f"), El.Pin3D.Position3D.Y));
        SetSliderReadout(SliderPinZ, TextPinZ, El.Pin3D.Position3D.Z, -2.0f, 2.0f,
            FString::Printf(TEXT("%.2f"), El.Pin3D.Position3D.Z));
        SetSliderReadout(SliderPinMinRot, TextPinMinRot, El.Pin3D.MinRotation, -180.0f, 180.0f,
            FString::Printf(TEXT("%.1f"), El.Pin3D.MinRotation));
        SetSliderReadout(SliderPinMaxRot, TextPinMaxRot, El.Pin3D.MaxRotation, -180.0f, 180.0f,
            FString::Printf(TEXT("%.1f"), El.Pin3D.MaxRotation));
        SetSliderReadout(SliderPinRotSens, TextPinRotSens, El.Pin3D.RotationSensitivity, -10.0f, 10.0f,
            FString::Printf(TEXT("%.2f"), El.Pin3D.RotationSensitivity));
    }
    else
    {
        SetSliderReadout(SliderPinX, TextPinX, 0.0f, -2.0f, 2.0f, FString::Printf(TEXT("%.2f"), 0.0f));
        SetSliderReadout(SliderPinY, TextPinY, 0.0f, -2.0f, 2.0f, FString::Printf(TEXT("%.2f"), 0.0f));
        SetSliderReadout(SliderPinZ, TextPinZ, 0.0f, -2.0f, 2.0f, FString::Printf(TEXT("%.2f"), 0.0f));
        SetSliderReadout(SliderPinMinRot, TextPinMinRot, 0.0f, -180.0f, 180.0f, FString::Printf(TEXT("%.1f"), 0.0f));
        SetSliderReadout(SliderPinMaxRot, TextPinMaxRot, 0.0f, -180.0f, 180.0f, FString::Printf(TEXT("%.1f"), 0.0f));
        SetSliderReadout(SliderPinRotSens, TextPinRotSens, 1.0f, -10.0f, 10.0f, FString::Printf(TEXT("%.2f"), 1.0f));
    }
}

float UFaceParallaxEditorWidget::FrameFillRatio(const TArray<bool>& Occupied)
{
    if (Occupied.Num() == 0) return 0.0f;
    int32 Filled = 0;
    for (bool B : Occupied) if (B) ++Filled;
    return (float)Filled / (float)Occupied.Num();
}

float UFaceParallaxEditorWidget::PinSliderNorm(float Value, float Min, float Max)
{
    if (Max <= Min) return 0.0f;
    return FMath::Clamp((Value - Min) / (Max - Min), 0.0f, 1.0f);
}

int32 UFaceParallaxEditorWidget::ClampGridCols(int32 MaxFrames)
{
    return FMath::Clamp(MaxFrames, 1, 16);
}

void UFaceParallaxEditorWidget::AppendSortedUnique(TArray<FString>& Out, const FString& Line)
{
    for (int32 i = 0; i < Out.Num(); ++i)
    {
        if (Out[i] == Line) return;
        if (Out[i] > Line) { Out.Insert(Line, i); return; }
    }
    Out.Add(Line);
}

bool UFaceParallaxEditorWidget::VisemeFramesMismatch(int32 A, int32 B)
{
    return A > 0 && B > 0 && A != B;
}

void UFaceParallaxEditorWidget::RebuildVisemeGrid()
{
    if (!VisemeGridBox.IsValid()) return;
    VisemeGridBox->ClearChildren();
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !SelectedLayerName.IsValid())
    {
        VisemeGridBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("Select a layer to see viseme frames."), 8, FLinearColor(0.5f,0.5f,0.5f))];
        return;
    }
    static const EViseme AllVisemes[] = {
        EViseme::Uh, EViseme::Ah, EViseme::Ee, EViseme::D, EViseme::S, EViseme::F,
        EViseme::M, EViseme::L, EViseme::WOO, EViseme::Oh, EViseme::R
    };
    auto VisemeLabel = [](EViseme V) -> const TCHAR*
    {
        switch (V)
        {
            case EViseme::Uh:   return TEXT("Uh");
            case EViseme::Ah:   return TEXT("Ah");
            case EViseme::Ee:   return TEXT("Ee");
            case EViseme::D:    return TEXT("D");
            case EViseme::S:    return TEXT("S");
            case EViseme::F:    return TEXT("F");
            case EViseme::M:    return TEXT("M");
            case EViseme::L:    return TEXT("L");
            case EViseme::WOO:  return TEXT("WO");
            case EViseme::Oh:   return TEXT("Oh");
            case EViseme::R:    return TEXT("R");
        }
        return TEXT("?");
    };
    struct FVisemeRow
    {
        FString Label;
        TArray<bool> Occupied;
        EViseme Enum = EViseme::Uh;
        FName Named;
        bool bNamed = false;
    };
    TArray<FVisemeRow> Rows;
    int32 MaxFrames = 0;
    const EExpression Expr = EExpression::Neutral;
    for (EViseme V : AllVisemes)
    {
        const int32 Cnt = GetVisemeFrameCount(ActiveViewState, SelectedLayerName, Expr, V);
        FVisemeRow Row;
        Row.Enum = V;
        Row.Label = VisemeLabel(V);
        for (int32 f = 0; f < FMath::Min(Cnt, 16); ++f)
        {
            FFaceTextureSet Set = GetVisemeFrameTextures(ActiveViewState, SelectedLayerName, Expr, V, f);
            Row.Occupied.Add(Set.Albedo != nullptr);
        }
        MaxFrames = FMath::Max(MaxFrames, Cnt);
        Rows.Add(Row);
    }
    TArray<FName> Named = GetAssignedNamedVisemes(ActiveViewState, SelectedLayerName);
    for (FName N : Named)
    {
        const int32 Cnt = GetNamedVisemeFrameCount(ActiveViewState, SelectedLayerName, N);
        FVisemeRow Row;
        Row.bNamed = true;
        Row.Named = N;
        Row.Label = N.ToString();
        for (int32 f = 0; f < FMath::Min(Cnt, 16); ++f)
        {
            FFaceTextureSet Set = GetNamedVisemeFrameTextures(ActiveViewState, SelectedLayerName, N, f);
            Row.Occupied.Add(Set.Albedo != nullptr);
        }
        MaxFrames = FMath::Max(MaxFrames, Cnt);
        Rows.Add(Row);
    }
    if (MaxFrames <= 0)
    {
        VisemeGridBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No viseme frames assigned for this layer/state."), 8, FLinearColor(0.5f,0.5f,0.5f))];
        return;
    }
    const int32 Cols = ClampGridCols(MaxFrames);
    const bool bPlaying = Comp->IsVisemePlaying();
    const EViseme CurV = Comp->GetCurrentViseme();
    for (const FVisemeRow& Row : Rows)
    {
        if (Row.Occupied.Num() == 0) continue;
        TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
        R->AddSlot().Padding(FMargin(0,1)).AutoWidth()
            [SNew(SBox).WidthOverride(46)
                [SNew(STextBlock)
                    .Text(FText::FromString(Row.Label))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(!Row.bNamed && bPlaying && Row.Enum == CurV
                        ? FLinearColor(1.0f,0.8f,0.4f) : FLinearColor(0.8f,0.8f,0.8f))]];
        for (int32 c = 0; c < Cols; ++c)
        {
            const bool Filled = Row.Occupied.IsValidIndex(c) && Row.Occupied[c];
            TSharedRef<SBox> Cell = SNew(SBox).WidthOverride(18).HeightOverride(18)
                [SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(Filled ? FLinearColor(0.3f,0.6f,0.3f) : FLinearColor(0.06f,0.06f,0.06f))
                    .Padding(FMargin(0))];
            FVisemeRow RowCopy = Row;
            Cell->SetOnMouseButtonDown(FPointerEventHandler::CreateLambda(
                [this, Filled, RowCopy](const FGeometry&, const FPointerEvent&) -> FReply
                {
                    if (!Filled) return FReply::Handled();
                    if (RowCopy.bNamed) PlayVisemeByName(RowCopy.Named);
                    else PlayViseme(RowCopy.Enum);
                    RefreshUI();
                    return FReply::Handled();
                }));
            R->AddSlot().Padding(FMargin(1))[Cell];
        }
        R->AddSlot().Padding(FMargin(4,1)).AutoWidth()
            [MakeLbl(FString::Printf(TEXT("%.0f%%"), FrameFillRatio(Row.Occupied) * 100.0f),
                7, FLinearColor(0.5f,0.7f,0.5f))];
        VisemeGridBox->AddSlot().AutoHeight()[R];
    }
}

void UFaceParallaxEditorWidget::RebuildNestedOutliner()
{
    if (!NestedOutlinerBox.IsValid()) return;
    NestedOutlinerBox->ClearChildren();
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !SelectedLayerName.IsValid())
    {
        NestedOutlinerBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No layer selected."), 8, FLinearColor(0.5f,0.5f,0.5f))];
        return;
    }
    const int32 N = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
    if (N == 0)
    {
        NestedOutlinerBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No nested elements for this layer/state."), 8, FLinearColor(0.5f,0.5f,0.5f))];
        return;
    }
    for (int32 i = 0; i < N; ++i)
    {
        FFaceNestedArt El = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
        FString ElName = El.ElementName.IsValid()
            ? El.ElementName.ToString()
            : FString::Printf(TEXT("Element %d"), i);
        TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
        bool bVis = true;
        if (const bool* V = El.ViewVisibility.Find(ActiveViewState)) bVis = *V;
        R->AddSlot().Padding(FMargin(0,2)).AutoWidth()
            [SNew(SCheckBox).IsChecked(bVis ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this, i](ECheckBoxState S)
                {
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    if (!Comp || !SelectedLayerName.IsValid()) return;
                    if (Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName) <= i) return;
                    FFaceNestedArt E = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
                    E.ViewVisibility.FindOrAdd(ActiveViewState) = (S == ECheckBoxState::Checked);
                    Comp->SetNestedElement(ActiveViewState, SelectedLayerName, i, E);
                    RefreshUI();
                })];
        R->AddSlot().Padding(FMargin(4,2)).AutoWidth()
            [MakeBtn(ElName, [this, i]()
            {
                SelectedNestedElementIndex = i;
                RefreshUI();
            }, i == SelectedNestedElementIndex ? FLinearColor(1.0f,0.8f,0.4f) : FLinearColor(0.8f,0.8f,0.8f),
                FLinearColor(0.15f,0.15f,0.15f))];
        if (El.Pin3D.bPinned)
            R->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [MakeLbl(TEXT("[Pin]"), 8, FLinearColor(1.0f,0.8f,0.4f))];
        if (El.bJiggleEnabled)
            R->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [MakeLbl(TEXT("[Jiggle]"), 8, FLinearColor(0.6f,1.0f,0.6f))];
        R->AddSlot().FillWidth(1.0f);
        R->AddSlot().Padding(FMargin(4,2)).AutoWidth()
            [MakeBtn(TEXT("Del"), [this, i]()
            {
                RemoveNestedElement(ActiveViewState, SelectedLayerName, i);
                RefreshUI();
            }, FLinearColor(1.0f,0.5f,0.5f), FLinearColor(0.1f,0.1f,0.1f))];
        NestedOutlinerBox->AddSlot().AutoHeight()[R];
        for (int32 ci = 0; ci < El.Children.Num(); ++ci)
        {
            const FFaceNestedArt& Ch = El.Children[ci];
            TSharedRef<SHorizontalBox> Cr = SNew(SHorizontalBox);
            Cr->AddSlot().Padding(FMargin(14,1)).AutoWidth()
                [MakeLbl(TEXT("\u2514"), 7, FLinearColor(0.4f,0.4f,0.4f))];
            FString ChName = Ch.ElementName.IsValid()
                ? Ch.ElementName.ToString()
                : FString::Printf(TEXT("Child %d"), ci);
            Cr->AddSlot().Padding(FMargin(4,1)).AutoWidth()
                [MakeLbl(ChName, 7, FLinearColor(0.6f,0.6f,0.6f))];
            if (Ch.Pin3D.bPinned)
                Cr->AddSlot().Padding(FMargin(4,1)).AutoWidth()
                    [MakeLbl(TEXT("[Pin]"), 7, FLinearColor(1.0f,0.8f,0.4f))];
            NestedOutlinerBox->AddSlot().AutoHeight()[Cr];
        }
    }
}

void UFaceParallaxEditorWidget::RebuildParamTable()
{
    if (!ParamTableBox.IsValid()) return;
    ParamTableBox->ClearChildren();
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !SelectedLayerName.IsValid())
    {
        ParamTableBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No layer selected."), 8, FLinearColor(0.5f,0.5f,0.5f))];
        return;
    }
    auto TargetName = [](EFaceParamTarget T) -> const TCHAR*
    {
        switch (T)
        {
            case EFaceParamTarget::PositionX:    return TEXT("PosX");
            case EFaceParamTarget::PositionY:    return TEXT("PosY");
            case EFaceParamTarget::ScaleX:       return TEXT("SclX");
            case EFaceParamTarget::ScaleY:       return TEXT("SclY");
            case EFaceParamTarget::Rotation:     return TEXT("Rot");
            case EFaceParamTarget::TextureBlend: return TEXT("Blend");
        }
        return TEXT("?");
    };
    auto CycleTarget = [](EFaceParamTarget T) -> EFaceParamTarget
    {
        switch (T)
        {
            case EFaceParamTarget::PositionX:    return EFaceParamTarget::PositionY;
            case EFaceParamTarget::PositionY:    return EFaceParamTarget::ScaleX;
            case EFaceParamTarget::ScaleX:       return EFaceParamTarget::ScaleY;
            case EFaceParamTarget::ScaleY:       return EFaceParamTarget::Rotation;
            case EFaceParamTarget::Rotation:     return EFaceParamTarget::TextureBlend;
            case EFaceParamTarget::TextureBlend: return EFaceParamTarget::PositionX;
        }
        return EFaceParamTarget::PositionX;
    };
    TArray<FFaceParamBinding> Bindings = GetParamBindings(ActiveViewState, SelectedLayerName);
    if (Bindings.Num() == 0)
    {
        ParamTableBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No bindings. Use the box above to add one."), 8, FLinearColor(0.5f,0.5f,0.5f))];
        return;
    }
    for (int32 i = 0; i < Bindings.Num(); ++i)
    {
        FFaceParamBinding B = Bindings[i];
        TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
        R->AddSlot().Padding(FMargin(0,2)).AutoWidth()
            [SNew(SBox).WidthOverride(74)
                [SNew(SEditableTextBox)
                    .Text(FText::FromString(B.ParamName.ToString()))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .OnTextCommitted_Lambda([this, i](const FText& T, ETextCommit::Type)
                    {
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp || !SelectedLayerName.IsValid()) return;
                        TArray<FFaceParamBinding> All = GetParamBindings(ActiveViewState, SelectedLayerName);
                        if (!All.IsValidIndex(i)) return;
                        All[i].ParamName = FName(*T.ToString());
                        SetParamBindings(ActiveViewState, SelectedLayerName, All);
                        RefreshUI();
                    })]];
        R->AddSlot().Padding(FMargin(2,2)).AutoWidth()
            [MakeBtn(TargetName(B.Target), [this, i, CycleTarget]()
            {
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (!Comp || !SelectedLayerName.IsValid()) return;
                TArray<FFaceParamBinding> All = GetParamBindings(ActiveViewState, SelectedLayerName);
                if (!All.IsValidIndex(i)) return;
                All[i].Target = CycleTarget(All[i].Target);
                SetParamBindings(ActiveViewState, SelectedLayerName, All);
                RefreshUI();
            }, FLinearColor(0.6f,0.8f,1.0f))];
        R->AddSlot().Padding(FMargin(2,2)).AutoWidth()
            [SNew(SCheckBox).IsChecked(B.bInvert ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this, i](ECheckBoxState S)
                {
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    if (!Comp || !SelectedLayerName.IsValid()) return;
                    TArray<FFaceParamBinding> All = GetParamBindings(ActiveViewState, SelectedLayerName);
                    if (!All.IsValidIndex(i)) return;
                    All[i].bInvert = (S == ECheckBoxState::Checked);
                    SetParamBindings(ActiveViewState, SelectedLayerName, All);
                    RefreshUI();
                })
                [MakeLbl(TEXT("Inv"), 7, FLinearColor(0.7f,0.7f,0.7f))]];
        R->AddSlot().FillWidth(1.0f);
        R->AddSlot().Padding(FMargin(2,2)).AutoWidth()
            [MakeBtn(TEXT("X"), [this, i]()
            {
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (!Comp || !SelectedLayerName.IsValid()) return;
                TArray<FFaceParamBinding> All = GetParamBindings(ActiveViewState, SelectedLayerName);
                if (!All.IsValidIndex(i)) return;
                All.RemoveAt(i);
                SetParamBindings(ActiveViewState, SelectedLayerName, All);
                RefreshUI();
            }, FLinearColor(1.0f,0.5f,0.5f), FLinearColor(0.1f,0.1f,0.1f))];
        ParamTableBox->AddSlot().AutoHeight()[R];
    }
}

void UFaceParallaxEditorWidget::RebuildProblemsPanel()
{
    if (!ProblemsPanelBox.IsValid()) return;
    ProblemsPanelBox->ClearChildren();
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !Comp->ActivePreset)
    {
        ProblemsPanelBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No preset active."), 8, FLinearColor(1.0f,0.5f,0.5f))];
        return;
    }
    auto StateLabel = [](EFaceAngleState S) -> const TCHAR*
    {
        switch (S)
        {
            case EFaceAngleState::Front:            return TEXT("Front");
            case EFaceAngleState::ThreeQuarterRight:return TEXT("3/4R");
            case EFaceAngleState::RightProfile:     return TEXT("Right");
            case EFaceAngleState::BackRight:        return TEXT("BkR");
            case EFaceAngleState::Back:             return TEXT("Back");
            case EFaceAngleState::BackLeft:         return TEXT("BkL");
            case EFaceAngleState::LeftProfile:      return TEXT("Left");
            case EFaceAngleState::ThreeQuarterLeft: return TEXT("3/4L");
            case EFaceAngleState::Top:              return TEXT("Top");
            case EFaceAngleState::Bottom:           return TEXT("Bot");
        }
        return TEXT("?");
    };
    struct FProblem
    {
        EFaceAngleState State = EFaceAngleState::Front;
        bool bError = true;
        FString Text;
    };
    TArray<FProblem> Rows;
    TArray<FString> Seen;
    auto AddProblem = [&](EFaceAngleState S, bool bError, const FString& Text)
    {
        const int32 Before = Seen.Num();
        AppendSortedUnique(Seen, Text);
        if (Seen.Num() == Before) return;
        FProblem P;
        P.State = S;
        P.bError = bError;
        P.Text = Text;
        Rows.Add(P);
    };
    const int32 StateCount = 10;
    for (int32 s = 0; s < StateCount; ++s)
    {
        const EFaceAngleState St = (EFaceAngleState)s;
        for (FName Tag : LayerNames)
        {
            FFaceTextureSet Set = GetSlotTextures(St, Tag);
            if (!Set.Albedo)
                AddProblem(St, true, FString::Printf(TEXT("%s / %s: missing albedo"), StateLabel(St), *Tag.ToString()));
            if (!Set.Normal)
                AddProblem(St, true, FString::Printf(TEXT("%s / %s: missing normal"), StateLabel(St), *Tag.ToString()));
            if (!Set.Depth)
                AddProblem(St, true, FString::Printf(TEXT("%s / %s: missing depth"), StateLabel(St), *Tag.ToString()));
        }
        int32 BlinkFrames = -1;
        for (FName Tag : LayerNames)
        {
            const int32 Cnt = GetBlinkFrameCount(St, Tag);
            if (Cnt > 0)
            {
                if (BlinkFrames == -1) BlinkFrames = Cnt;
                else if (BlinkFrames != Cnt)
                    AddProblem(St, false, FString::Printf(TEXT("%s: blink frame count mismatch (%d vs %d)"),
                        StateLabel(St), BlinkFrames, Cnt));
            }
        }
        static const EViseme AllVisemes[] = {
            EViseme::Uh, EViseme::Ah, EViseme::Ee, EViseme::D, EViseme::S, EViseme::F,
            EViseme::M, EViseme::L, EViseme::WOO, EViseme::Oh, EViseme::R
        };
        for (EViseme V : AllVisemes)
        {
            int32 MinFrames = -1;
            int32 MaxFrames = -1;
            for (FName Tag : LayerNames)
            {
                const int32 Cnt = GetVisemeFrameCount(St, Tag, EExpression::Neutral, V);
                if (Cnt <= 0) continue;
                if (MinFrames == -1 || Cnt < MinFrames) MinFrames = Cnt;
                if (MaxFrames == -1 || Cnt > MaxFrames) MaxFrames = Cnt;
            }
            if (MinFrames > 0 && VisemeFramesMismatch(MinFrames, MaxFrames))
                AddProblem(St, false, FString::Printf(TEXT("%s: viseme %d frame count mismatch (%d vs %d)"),
                    StateLabel(St), (int32)V, MinFrames, MaxFrames));
        }
    }
    if (Rows.Num() == 0)
    {
        ProblemsPanelBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No problems found."), 8, FLinearColor(0.5f,1.0f,0.5f))];
        return;
    }
    int32 ErrorCount = 0;
    for (const FProblem& P : Rows) if (P.bError) ++ErrorCount;
    ProblemsPanelBox->AddSlot().AutoHeight()
        [MakeLbl(FString::Printf(TEXT("%d issues (%d errors, %d warnings)"), Rows.Num(), ErrorCount, Rows.Num() - ErrorCount),
            9, FLinearColor(0.9f,0.7f,0.3f))];
    const int32 MaxRows = 40;
    for (int32 i = 0; i < FMath::Min(Rows.Num(), MaxRows); ++i)
    {
        const FProblem& P = Rows[i];
        FString JumpLabel = FString::Printf(TEXT(">%s"), StateLabel(P.State));
        TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
        R->AddSlot().Padding(FMargin(0,1)).AutoWidth()
            [SNew(SBox).WidthOverride(10).HeightOverride(10)
                [SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(P.bError ? FLinearColor(1.0f,0.35f,0.35f) : FLinearColor(1.0f,0.85f,0.3f))
                    .Padding(FMargin(0))]];
        R->AddSlot().Padding(FMargin(4,1)).AutoWidth()
            [MakeLbl(JumpLabel, 8, FLinearColor(0.6f,0.7f,1.0f))];
        R->AddSlot().Padding(FMargin(4,1)).FillWidth(1.0f)
            [SNew(STextBlock)
                .Text(FText::FromString(P.Text))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(P.bError ? FLinearColor(1.0f,0.6f,0.6f) : FLinearColor(0.9f,0.8f,0.5f))
                .AutoWrapText(true)];
        const EFaceAngleState JumpState = P.State;
        R->SetOnMouseButtonDown(FPointerEventHandler::CreateLambda(
            [this, JumpState](const FGeometry&, const FPointerEvent&) -> FReply
            {
                ActiveViewState = JumpState;
                if (LayerNames.Num() > 0) SelectedLayerName = LayerNames[0];
                RefreshUI();
                return FReply::Handled();
            }));
        ProblemsPanelBox->AddSlot().AutoHeight()[R];
    }
    if (Rows.Num() > MaxRows)
    {
        ProblemsPanelBox->AddSlot().AutoHeight()
            [MakeLbl(FString::Printf(TEXT("... and %d more"), Rows.Num() - MaxRows),
                8, FLinearColor(0.5f,0.5f,0.5f))];
    }
}

void UFaceParallaxEditorWidget::RefreshUI()
{
    if (bIsRefreshing) return;
    bIsRefreshing = true;

    RefreshActorSelector();
    RefreshLayerList();
    RefreshTextureThumbs();
    RefreshViewStripDots();
    RefreshSlotPropStatus();
    RefreshOnionSkin();
    if (GizmoWidget.IsValid())
    {
        // Pin mode: when the selected nested element is pinned, the gizmo
        // edits the pin (drag handle at its projected UV) instead of the
        // layer transform.
        FFaceNestedArt PinEl;
        int32 PinCount = 0;
        const bool bPinMode = GetSelectedPinElement(PinEl, PinCount) && PinEl.Pin3D.bPinned;
        GizmoWidget->SetPinMode(bPinMode);
        GizmoWidget->Invalidate(EInvalidateWidgetReason::Paint);
    }
    RefreshTimeline();
    RefreshTransformSliders();
    RefreshConfigCheckboxes();
    RefreshDebugSliders();
    RefreshHullThumbnails();
    RefreshPinControls();
    if (bEdgeOverlayVisible) BuildEdgeOverlay();
    RebuildZoneDiagram();
    RebuildCrossLayerPanel();
    RebuildStatusMatrix();
    RebuildTagValidator();
    RebuildMaterialCrossRef();
    RebuildVisemeGrid();
    RebuildNestedOutliner();
    RebuildParamTable();
    RebuildProblemsPanel();
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
    // Update camera numeric readouts
    if (TextCameraYaw.IsValid())
    {
        TextCameraYaw->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), GetOrbitYaw())));
    }
    if (TextCameraPitch.IsValid())
    {
        TextCameraPitch->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), GetOrbitPitch())));
    }
    if (TextCameraDist.IsValid())
    {
        TextCameraDist->SetText(FText::FromString(FString::Printf(TEXT("%.1f"), GetOrbitDistance())));
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
    RunDiagnostics();

    bIsRefreshing = false;
}

void UFaceParallaxEditorWidget::RefreshActorSelector()
{
    ActorOptions.Reset();
    ActorOptions.Add(TWeakObjectPtr<AFaceParallaxPreviewActor>());

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World)
    {
        for (TActorIterator<AFaceParallaxPreviewActor> It(World); It; ++It)
        {
            if (*It)
            {
                ActorOptions.Add(*It);
            }
        }
    }

    if (ActorSelector.IsValid())
    {
        ActorSelector->RefreshOptions();
        ActorSelector->SetSelectedItem(PreviewActor);
    }
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

    if (!SelectedLayerName.IsValid() && LayerNames.Num() > 0)
        SelectedLayerName = LayerNames[0];

    UFaceParallaxComponent* Comp = GetParallaxComponent();
    for (const FName& Tag : LayerNames)
    {
        bool bSelected = (Tag == SelectedLayerName);
        FString TagStr = Tag.ToString();
        bool bLayerHidden = Comp && !Comp->GetLayerVisibility(Tag);

        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        // Eye toggle
        Row->AddSlot().AutoWidth().Padding(FMargin(2,0,0,0)).VAlign(VAlign_Center)
            [SNew(SCheckBox)
                .IsChecked(bLayerHidden ? ECheckBoxState::Unchecked : ECheckBoxState::Checked)
                .OnCheckStateChanged_Lambda([this, Tag, Comp](ECheckBoxState S)
                {
                    bool bNowHidden = (S == ECheckBoxState::Unchecked);
                    if (Comp) Comp->SetLayerVisibility(Tag, !bNowHidden);
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

    // Check if async loads are in-flight — show a visual cue
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    bool bLoading = false;
    if (Comp)
    {
        // We can't directly access ActiveTextureLoads from the widget (it's private).
        // Instead, check if textures that should be loaded aren't yet resolved.
        FFaceTextureSet Tex = GetSlotTextures(ActiveViewState, SelectedLayerName);
        if (SelectedLayerName.IsValid())
        {
            bool bHasAnySoftRef = false;
            bHasAnySoftRef |= !Tex.SoftAlbedo.IsNull() && Tex.Albedo == nullptr;
            bHasAnySoftRef |= !Tex.SoftNormal.IsNull() && Tex.Normal == nullptr;
            bHasAnySoftRef |= !Tex.SoftDepth.IsNull() && Tex.Depth == nullptr;
            bLoading = bHasAnySoftRef;
        }
    }

    auto ApplyTex = [bLoading](TSharedPtr<SImage>& Thumb, UTexture2D* Tex, FSlateBrush& Brush)
    {
        if (Tex)
        {
            Brush.SetResourceObject(Tex);
            Brush.ImageSize = FVector2D(72, 72);
            if (Thumb.IsValid()) { Thumb->SetImage(&Brush); Thumb->SetColorAndOpacity(FLinearColor(1,1,1)); }
        }
        else if (bLoading)
        {
            if (Thumb.IsValid()) Thumb->SetColorAndOpacity(FLinearColor(0.2f,0.3f,0.6f)); // blue-ish loading hint
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
                if (SelectedLayerName.IsValid())
                {
                    UTexture2D* Tex = GetSelectedContentBrowserTexture();
                    FFaceTextureSet Cur = GetBlinkFrameTextures(ActiveViewState, SelectedLayerName, Idx);
                    if (Tex)
                    {
                        FString TexName = Tex->GetName();
                        // Heuristic: assign to channel matching texture name suffix
                        if (TexName.Contains(TEXT("_N"), ESearchCase::IgnoreCase) || TexName.Contains(TEXT("Normal"), ESearchCase::IgnoreCase))
                            Cur.Normal = Tex;
                        else if (TexName.Contains(TEXT("_D"), ESearchCase::IgnoreCase) || TexName.Contains(TEXT("Depth"), ESearchCase::IgnoreCase))
                            Cur.Depth = Tex;
                        else
                            Cur.Albedo = Tex;
                        SetBlinkFrameTextures(ActiveViewState, SelectedLayerName, Idx, Cur);
                    }
                    RefreshUI();
                }
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
                FFaceTextureSet DefaultSet = GetSlotTextures(ActiveViewState, SelectedLayerName);
                SetBlinkFrameTextures(ActiveViewState, SelectedLayerName, Cnt, DefaultSet);
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
        // Iterate all expression types from the enum
        UEnum* ExprEnum = StaticEnum<EExpression>();
        int32 ExprCount = ExprEnum ? ExprEnum->NumEnums() - 1 : 0;
        for (int32 ExprIdx = 0; ExprIdx < ExprCount; ++ExprIdx)
        {
            EExpression Expr = (EExpression)ExprEnum->GetValueByIndex(ExprIdx);
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
    {
        TForm = bViewOverrideMode
            ? GetViewOverride(ActiveViewState, SelectedLayerName, ActiveViewState)
            : GetLayerCanonicalTransform(ActiveViewState, SelectedLayerName);
    }

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
            TEXT("State: %s | Layer: %s | Textures: %d%s"),
            *StateStr, *LayerStr, TexCount,
            bViewOverrideMode ? TEXT(" | VIEW OVERRIDE") : TEXT(""))));
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
        // Back view: the projection mirrors X (ViewX = -WX at yaw 180),
        // so authoring from a Back-view click must mirror back.
        if (FMath::Abs(YawDeg) > 135.0f)
            Pin.Position3D.X = -UVCenter.X * 2.0f;
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
// OUTLINE → DEPTH
// ====================================================================

bool UFaceParallaxEditorWidget::GenerateDepthFromOutlines(int32 GridSize)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp)
    {
        SetStatus(TEXT("Generate depth: no parallax component"), FLinearColor::Red);
        return false;
    }

    TArray<float> Depth;
    float CellSize = 0.0f;
    if (!Comp->GenerateDepthBufferFromOutlines(GridSize, Depth, CellSize))
    {
        SetStatus(TEXT("No outline art found — assign a Front view with an albedo first"),
            FLinearColor::Yellow);
        return false;
    }
    BuildOutlineDepthTexture(Depth, GridSize);
    if (OutlinePreviewImage.IsValid())
    {
        OutlinePreviewImage->SetImage(&OutlineDepthBrush);
    }

    // Bake result into every layer's depth channel
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Generate Depth From Outlines"));
    int32 LayersUpdated = 0;
    if (ValidatePreset())
    {
        for (const auto& StatePair : ActivePreset->ViewAssignments)
        {
            for (const auto& LayerPair : StatePair.Value.Layers)
            {
                FFaceTextureSet Textures = ActivePreset->GetTexturesForSlot(StatePair.Key, LayerPair.Key);
                Textures.Depth = OutlineDepthTexture;
                ActivePreset->SetTexturesForSlot(StatePair.Key, LayerPair.Key, Textures);
                ++LayersUpdated;
            }
        }
        if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
        {
            PreviewActor->FaceParallax->ApplyCurrentStateTextures();
        }
    }

    SetStatus(FString::Printf(TEXT("Depth map %dx%d generated from silhouette edges (%d layer(s) updated)"),
        GridSize, GridSize, LayersUpdated), FLinearColor(0.3f, 1.0f, 0.3f));
    RefreshTextureThumbs();
    RefreshUI();
    return true;
}

void UFaceParallaxEditorWidget::SetOutlineOverlayVisible(bool bVisible)
{
    bOutlineOverlayVisible = bVisible;
    if (CheckOutlineOverlay.IsValid())
    {
        CheckOutlineOverlay->SetIsChecked(bVisible);
    }
}

bool UFaceParallaxEditorWidget::GetOutlineOverlayVisible() const
{
    return bOutlineOverlayVisible;
}

void UFaceParallaxEditorWidget::RefreshOutlineViewChecks()
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    for (int32 i = 0; i < OutlineViewChecks.Num() && i < 10; ++i)
    {
        if (OutlineViewChecks[i].IsValid())
        {
            OutlineViewChecks[i]->SetIsChecked(Comp && Comp->IsOutlineViewState((EFaceAngleState)i));
        }
    }
}

void UFaceParallaxEditorWidget::BuildOutlineDepthTexture(const TArray<float>& Depth, int32 GridSize)
{
    if (Depth.Num() != GridSize * GridSize) return;

    if (!OutlineDepthTexture)
    {
        OutlineDepthTexture = UTexture2D::CreateTransient(GridSize, GridSize, PF_B8G8R8A8);
        if (!OutlineDepthTexture) return;
    }

    OutlineDepthTexture->Source.Init(GridSize, GridSize, 1, 1, TSF_BGRA8);
    uint8* Pixels = (uint8*)OutlineDepthTexture->Source.LockMip(0);
    if (Pixels)
    {
        for (int32 i = 0; i < GridSize * GridSize; ++i)
        {
            const float N = FMath::Clamp(Depth[i], 0.0f, 1.0f);
            const uint8 V = (uint8)FMath::RoundToInt(N * 255.0f);
            Pixels[i * 4 + 0] = V;
            Pixels[i * 4 + 1] = V;
            Pixels[i * 4 + 2] = V;
            Pixels[i * 4 + 3] = 255;
        }
    }
    OutlineDepthTexture->Source.UnlockMip(0);
    OutlineDepthTexture->UpdateResource();

    OutlineDepthBrush.SetResourceObject(OutlineDepthTexture);
    OutlineDepthBrush.ImageSize = FVector2D(128.0f, 128.0f);
    OutlineDepthBrush.DrawAs = ESlateBrushDrawType::Image;

    if (TextOutlineStats.IsValid())
    {
        int32 NonZero = 0;
        for (float V : Depth) if (V > 0.01f) ++NonZero;
        TextOutlineStats->SetText(FText::FromString(FString::Printf(
            TEXT("%dx%d depth buffer — silhouette edge visual hull (%d active cells)"),
            GridSize, GridSize, NonZero)));
    }
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
    StatusMatrixBrushes.Reset();

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
    const TCHAR* Abbrs[] = { TEXT("F"), TEXT("3R"), TEXT("PR"), TEXT("BR"), TEXT("B"),
        TEXT("BL"), TEXT("PL"), TEXT("3L"), TEXT("T"), TEXT("Bt") };

    const float CellW = 64.0f, CellH = 44.0f;
    const float PadX = 3.0f, PadY = 3.0f;
    const float MaxW = CellW - PadX * 2.0f, MaxH = CellH - PadY * 2.0f;

    auto MakeThumbCell = [&](EFaceAngleState State, FName Tag) -> TSharedRef<SWidget>
    {
        bool bAssigned = ActivePreset->HasSlot(State, Tag);
        UTexture2D* Tex = bAssigned ? ActivePreset->GetTexturesForSlot(State, Tag).Albedo : nullptr;
        FLinearColor BackColor = (State == ActiveViewState)
            ? FLinearColor(0.25f, 0.45f, 0.55f)
            : FLinearColor(0.1f, 0.1f, 0.12f);
        const TArray<EFaceAngleState> Overrides = bAssigned ? GetOverrideViewsForSlot(State, Tag) : TArray<EFaceAngleState>();
        const bool bHasOverrides = Overrides.Num() > 0;
        if (bHasOverrides)
        {
            BackColor = FLinearColor::LerpUsingHSV(BackColor, FLinearColor(0.45f, 0.38f, 0.12f), 0.55f);
        }

        float DW = MaxW, DH = MaxH;
        FString Info;
        if (Tex)
        {
            const int32 TW = Tex->GetSizeX();
            const int32 TH = Tex->GetSizeY();
            if (TW > 0 && TH > 0)
            {
                const float Aspect = (float)TW / (float)TH;
                if (Aspect >= 1.0f) { DW = MaxW; DH = MaxW / Aspect; }
                else { DH = MaxH; DW = MaxH * Aspect; }
            }
            Info = FString::Printf(TEXT("%s @ %s\n%d x %d px\nClick to jump%s"),
                *Tag.ToString(),
                *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)State), TW, TH,
                bHasOverrides ? TEXT("\nHas per-view overrides") : TEXT(""));
        }
        else
        {
            Info = FString::Printf(TEXT("%s @ %s — no textures%s"),
                *Tag.ToString(),
                *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)State),
                bHasOverrides ? TEXT("\nHas per-view overrides") : TEXT(""));
        }

        TSharedRef<SOverlay> Cell = SNew(SOverlay);
        if (Tex)
        {
            TSharedPtr<FSlateBrush> Brush = MakeShareable(new FSlateBrush());
            Brush->SetResourceObject(Tex);
            Brush->ImageSize = FVector2D(DW, DH);
            Brush->DrawAs = ESlateBrushDrawType::Image;
            StatusMatrixBrushes.Add(Brush);
            Cell->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(DW).HeightOverride(DH)[SNew(SImage).Image(Brush.Get())]];
        }
        else
        {
            Cell->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                [MakeLbl(TEXT("\u2014"), 8, FLinearColor(0.35f,0.35f,0.35f))];
        }

        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(BackColor)
            .Padding(FMargin(1))
            .Cursor(EMouseCursor::Hand)
            .ToolTipText(FText::FromString(Info))
            .OnMouseButtonDown_Lambda([this, State, Tag](const FGeometry&, const FPointerEvent&) -> FReply
            {
                SetActiveViewState(State);
                SetSelectedLayer(Tag.ToString());
                return FReply::Handled();
            })
            .Content()
            [SNew(SBox).WidthOverride(CellW).HeightOverride(CellH)[Cell]];
    };

    // Corner label
    StatusMatrixGrid->AddSlot(0, 0)
        [SNew(SBox).WidthOverride(68).HeightOverride(28)
            [SNew(STextBlock)
                .Text(FText::FromString(TEXT("STATE \\ LAYER")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                .ColorAndOpacity(FLinearColor(0.5f,0.5f,0.5f))]];

    // Header row — state labels
    for (int32 si = 0; si < States.Num(); ++si)
    {
        StatusMatrixGrid->AddSlot(si + 1, 0)
            [SNew(SBox).WidthOverride(CellW).HeightOverride(28)
                [SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(FLinearColor(0.12f,0.12f,0.12f))
                    .Padding(FMargin(1))
                    [SNew(STextBlock)
                        .Text(FText::FromString(Abbrs[si]))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                        .ColorAndOpacity(FLinearColor(0.7f,0.7f,0.7f))
                        .Justification(ETextJustify::Center)]]];
    }

    // Data rows — one per layer
    for (int32 li = 0; li < Tags.Num(); ++li)
    {
        FName Tag = Tags[li];
        StatusMatrixGrid->AddSlot(0, li + 1)
            [SNew(SBox).WidthOverride(68).HeightOverride(CellH)
                [SNew(STextBlock)
                    .Text(FText::FromString(Tag.ToString()))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                    .ColorAndOpacity(FLinearColor(0.7f,0.7f,0.7f))]];
        for (int32 si = 0; si < States.Num(); ++si)
        {
            StatusMatrixGrid->AddSlot(si + 1, li + 1)
                [MakeThumbCell(States[si], Tag)];
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

    // Build a set of layer names that match the filter, so sections
    // that contain them stay visible even if the section title doesn't match.
    TSet<FString> MatchingLayers;
    for (const FName& L : LayerNames)
    {
        FString LStr = L.ToString();
        if (LStr.Contains(Filter, ESearchCase::IgnoreCase))
            MatchingLayers.Add(LStr);
    }

    auto ApplyToChildren = [&](TSharedPtr<SVerticalBox> Container)
    {
        if (!Container.IsValid()) return;
        for (int32 Idx = 0; Idx < Container->GetChildren()->Num(); ++Idx)
        {
            TSharedRef<SWidget> Section = Container->GetChildren()->GetChildAt(Idx);
            bool bVisible = true;
            if (!Filter.IsEmpty())
            {
                FString SectionText = SectionSectionTitles.FindRef(Section);
                if (SectionText.IsEmpty())
                    bVisible = MatchingLayers.Num() > 0;
                else
                    bVisible = SectionText.Contains(Filter, ESearchCase::IgnoreCase) || MatchingLayers.Num() > 0;
            }
            Section->SetVisibility(bVisible ? EVisibility::Visible : EVisibility::Collapsed);
        }
    };

    for (const TSharedPtr<SVerticalBox>& TabContent : PropTabContent)
    {
        ApplyToChildren(TabContent);
    }
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Snapshot Preset"));
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
        // Copy NestedPin3D references
        if (SnapshotPresetBackup->HasState(S))
        {
            TArray<FName> STags = SnapshotPresetBackup->GetAllLayerTags(S);
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

void UFaceParallaxEditorWidget::BeginDestroy()
{
    if (AssetModifiedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectModified.Remove(AssetModifiedHandle);
        AssetModifiedHandle.Reset();
    }
    Super::BeginDestroy();
}

void UFaceParallaxEditorWidget::OnAssetModified(UObject* Object)
{
    if (ActivePreset && Object == ActivePreset)
        RefreshUI();
}

void UFaceParallaxEditorWidget::LogDiagnostic(const FString& Message)
{
    if (DiagnosticLog.IsValid())
    {
        FString Current = DiagnosticLog->GetText().ToString();
        FString Updated = Current.IsEmpty() ? Message : Current + TEXT("\n") + Message;
        DiagnosticLog->SetText(FText::FromString(Updated));
    }
}

void UFaceParallaxEditorWidget::RunDiagnostics()
{
    if (!DiagnosticLog.IsValid()) return;
    DiagnosticLog->SetText(FText::GetEmpty());

    if (!ActivePreset)
    {
        LogDiagnostic(TEXT("[ERROR] No ActivePreset assigned."));
        return;
    }

    LogDiagnostic(FString::Printf(TEXT("Preset: %s"), *ActivePreset->GetName()));
    LogDiagnostic(FString::Printf(TEXT("Canvas: %.0f x %.0f"), ActivePreset->CanvasSize.X, ActivePreset->CanvasSize.Y));

    int32 TotalSlots = ActivePreset->GetTotalAssignedSlots();
    LogDiagnostic(FString::Printf(TEXT("Total assigned slots: %d"), TotalSlots));

    TArray<EFaceAngleState> Missing = GetMissingStates();
    if (Missing.Num() > 0)
    {
        FString States;
        for (auto S : Missing)
            States += FString::Printf(TEXT("%d "), (int32)S);
        LogDiagnostic(FString::Printf(TEXT("[WARN] Missing states: %s"), *States));
    }

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::Back, EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };
    for (auto S : AllStates)
    {
        if (!ActivePreset->HasState(S)) continue;
        TArray<FName> MissingLayers = GetMissingLayers(S);
        if (MissingLayers.Num() > 0)
        {
            FString Layers;
            for (auto& L : MissingLayers)
                Layers += L.ToString() + TEXT(" ");
            LogDiagnostic(FString::Printf(TEXT("  State %d missing layers: %s"), (int32)S, *Layers));
        }
    }

    LogDiagnostic(TEXT("--- Diagnostics complete ---"));

    ValidateMaterialParameters();
}

void UFaceParallaxEditorWidget::ValidateMaterialParameters()
{
    if (!DiagnosticLog.IsValid() || !PreviewActor.IsValid() || !PreviewActor->PreviewMesh) return;

    UMaterialInterface* Mat = PreviewActor->PreviewMesh->GetMaterial(0);
    if (!Mat)
    {
        LogDiagnostic(TEXT("[WARN] Preview mesh has no material on slot 0."));
        return;
    }

    // Expected parameter names from UFaceParallaxComponent
    TArray<FName> Expected = {
        TEXT("StateBlendAlpha"), TEXT("ParallaxOffset"), TEXT("DepthIntensity"),
        TEXT("DebugDepth"), TEXT("IsTopDown"), TEXT("IsTopView"),
        TEXT("ArtPosition"), TEXT("ArtScale"), TEXT("ArtRotation"), TEXT("ArtPivot"),
        TEXT("NestedAnimFrame"), TEXT("AlbedoTexture"), TEXT("NormalTexture"),
        TEXT("DepthTexture"), TEXT("AlbedoTexturePrev"), TEXT("NormalTexturePrev"),
        TEXT("DepthTexturePrev"), TEXT("ExpressionBlendAlpha"),
        TEXT("ExpressionAlbedoPrev"), TEXT("ExpressionNormalPrev"),
        TEXT("ExpressionDepthPrev"), TEXT("ParamBlendAlpha"),
        TEXT("AltAlbedoTexture"), TEXT("AltNormalTexture"), TEXT("AltDepthTexture"),
        TEXT("SwooshLayerBlend"), TEXT("SwooshIntensity"), TEXT("SwooshAngle"),
        TEXT("SwooshSize"), TEXT("SwooshTexture")
    };

    TArray<FName> ExpectedTextureParams = {
        TEXT("AlbedoTexture"), TEXT("NormalTexture"), TEXT("DepthTexture"),
        TEXT("AlbedoTexturePrev"), TEXT("NormalTexturePrev"), TEXT("DepthTexturePrev"),
        TEXT("ExpressionAlbedoPrev"), TEXT("ExpressionNormalPrev"), TEXT("ExpressionDepthPrev"),
        TEXT("AltAlbedoTexture"), TEXT("AltNormalTexture"), TEXT("AltDepthTexture"),
        TEXT("SwooshTexture")
    };

    // Skip normal map validation for Unlit shading model
    UMaterial* BaseMat = Mat->GetMaterial();
    if (BaseMat && BaseMat->GetShadingModels().HasShadingModel(MSM_Unlit))
    {
        ExpectedTextureParams.Remove(TEXT("NormalTexture"));
        ExpectedTextureParams.Remove(TEXT("NormalTexturePrev"));
    }

    // Check scalar parameters
    int32 FoundScalar = 0, MissingScalar = 0;
    for (FName Param : Expected)
    {
        if (ExpectedTextureParams.Contains(Param)) continue; // skip texture-only params
        float Val;
        bool bExists = Mat->GetScalarParameterValue(Param, Val);
        if (bExists)
            FoundScalar++;
        else
            MissingScalar++;
    }

    // Check texture parameters
    int32 FoundTex = 0, MissingTex = 0;
    for (FName Param : ExpectedTextureParams)
    {
        UTexture* Val = nullptr;
        bool bExists = Mat->GetTextureParameterValue(Param, Val);
        if (bExists)
            FoundTex++;
        else
            MissingTex++;
    }

    if (MissingScalar > 0 || MissingTex > 0)
    {
        LogDiagnostic(FString::Printf(TEXT("[WARN] Material missing %d scalar + %d texture params (out of %d expected scalar, %d texture)"),
            MissingScalar, MissingTex, FoundScalar + MissingScalar, FoundTex + MissingTex));
        LogDiagnostic(TEXT("  Update your master material to expose missing parameters."));
    }
    else
    {
        LogDiagnostic(FString::Printf(TEXT("[OK] Material has all %d expected parameters (%d scalar, %d texture)."),
            FoundScalar + FoundTex, FoundScalar, FoundTex));
    }
}

#endif // WITH_EDITOR
