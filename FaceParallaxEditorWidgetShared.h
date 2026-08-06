#pragma once

#include "CoreMinimal.h"
#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxPreset.h"
#include "FaceParallaxComponent.h"
#include "FaceParallaxLayoutSpec.h"
#include "FaceParallaxSchematic.h"
#include "FaceParallaxVectorArt.h"
#include <functional>

#if WITH_EDITOR
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Rendering/SlateLayoutTransform.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "Widgets/SCompoundWidget.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ContentBrowserDataDragDropOp.h"
#include "Engine/Texture2D.h"
#include "Misc/Paths.h"

// Internal helpers shared by the FaceParallaxEditorWidget translation units:
// import channel/view-state parsing, preset transaction scope, Slate label
// factories, and the canvas transform gizmo. Not part of the public API;
// see FaceParallaxEditorWidget.h for the Blueprint-facing surface.

namespace
{
    // ChannelFromTextureName/StripChannelSuffix strip a trailing view-state
    // suffix before matching channel markers, so markers only count as the
    // FINAL token. Both naming conventions work: {Part}_{View}_{Map}
    // ("Eyes_Front_Normal") and {Part}_{Map}_{View} ("Eyes_N_Front").
    // Substring matching would misclassify layer names that contain "n"/"d"
    // tokens ("Eyes_Nose_Front" -> Normal, "Eyes_Dyed_Front" -> Depth, a bare
    // "Depth" layer -> Depth) and pollute wizard part names ("Eyes_N").
    int32 MatchStateSuffix(const FString& BaseName, FString& OutSuffix);

    FString ChannelFromTextureName(const FString& Name)
    {
        const FString Lower = Name.ToLower();
        FString Remainder = Lower;
        FString StateSuffix;
        if (MatchStateSuffix(Lower, StateSuffix) >= 0)
        {
            Remainder = Lower.Left(Lower.Len() - StateSuffix.Len());
        }
        if (Remainder.EndsWith(TEXT("_normalmap")) || Remainder.EndsWith(TEXT("_normal"))
            || Remainder.EndsWith(TEXT("_norm")) || Remainder.EndsWith(TEXT("_n")))
        {
            return TEXT("Normal");
        }
        if (Remainder.EndsWith(TEXT("_displacement")) || Remainder.EndsWith(TEXT("_depth"))
            || Remainder.EndsWith(TEXT("_height")) || Remainder.EndsWith(TEXT("_d")))
        {
            return TEXT("Depth");
        }
        return TEXT("Albedo");
    }

    // Parses the view-state suffix from a file base name (channel suffix already removed).
    // Full names are matched before short codes so "_front" wins over "_f".
    // Returns the state index (0-13) or -1, and the matched suffix text.
    int32 MatchStateSuffix(const FString& BaseName, FString& OutSuffix)
    {
        const FString Lower = BaseName.ToLower();
        struct FStateSuffix { const TCHAR* Suffix; int32 State; };
        static const FStateSuffix Map[] = {
            {TEXT("_narrowright"), 1},      {TEXT("_narrowleft"), 11},
            {TEXT("_threequarterright"), 2},{TEXT("_threequarterleft"), 10},
            {TEXT("_3quarterright"), 2},    {TEXT("_3quarterleft"), 10},
            {TEXT("_sliverright"), 3},      {TEXT("_sliverleft"), 9},
            {TEXT("_rightprofile"), 4},     {TEXT("_leftprofile"), 8},
            {TEXT("_backright"), 5},        {TEXT("_backleft"), 7},
            {TEXT("_front"), 0},            {TEXT("_back"), 6},
            {TEXT("_top"), 12},             {TEXT("_bottom"), 13},
            {TEXT("_nr"), 1},               {TEXT("_nl"), 11},
            {TEXT("_3r"), 2},               {TEXT("_3l"), 10},
            {TEXT("_sr"), 3},               {TEXT("_sl"), 9},
            {TEXT("_pr"), 4},               {TEXT("_pl"), 8},
            {TEXT("_br"), 5},               {TEXT("_bl"), 7},
            {TEXT("_f"), 0},                {TEXT("_b"), 6},
            {TEXT("_t"), 12},               {TEXT("_bot"), 13},
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
    // If the channel token precedes a trailing view-state suffix (e.g. "Eyes_N_Front"),
    // the state suffix is stripped first and re-attached, so the caller's
    // MatchStateSuffix/part split still resolves ({Part}_{View}_{Map} input).
    FString StripChannelSuffix(const FString& Name, const FString& Channel)
    {
        if (Channel != TEXT("Normal") && Channel != TEXT("Depth"))
        {
            return Name;
        }
        const FString Lower = Name.ToLower();
        FString StateSuffix;
        const int32 StateIdx = MatchStateSuffix(Lower, StateSuffix);
        FString Candidate = (StateIdx >= 0)
            ? Lower.Left(Lower.Len() - StateSuffix.Len())
            : Lower;
        const TCHAR* Suffixes[4] = {};
        if (Channel == TEXT("Normal"))
        {
            Suffixes[0] = TEXT("_normalmap"); Suffixes[1] = TEXT("_normal");
            Suffixes[2] = TEXT("_norm");       Suffixes[3] = TEXT("_n");
        }
        else
        {
            Suffixes[0] = TEXT("_displacement"); Suffixes[1] = TEXT("_depth");
            Suffixes[2] = TEXT("_height");       Suffixes[3] = TEXT("_d");
        }
        for (const TCHAR* S : Suffixes)
        {
            if (Candidate.EndsWith(S))
            {
                FString Result = Name.Left(Candidate.Len() - FCString::Strlen(S));
                if (StateIdx >= 0)
                {
                    Result += Name.Right(StateSuffix.Len());
                }
                return Result;
            }
        }
        return Name;
    }

    // Phase D: 16-bin luminance histogram, bins normalized by the max count.
    // (mirror of UFaceParallaxEditorWidget::BuildLumaHistogram)

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

    // User-mutation scope: opens a UE editor transaction (so Ctrl+Z works on
    // the preset asset) AND pushes a pre-mutation preset duplicate onto the
    // widget's own multi-step undo stack (Undo/Redo buttons). Every mutating
    // widget UFUNCTION must wrap its edits in one of these.
    struct FWidgetUndoScope
    {
        bool bActive = false;
        FWidgetUndoScope(UFaceParallaxEditorWidget* Owner, const FString& Desc)
        {
            if (Owner)
            {
                Owner->PushUndoState(Desc);
            }
            if (GEditor && Owner && Owner->ActivePreset)
            {
                bActive = true;
                GEditor->BeginTransaction(FText::FromString(Desc));
                Owner->ActivePreset->Modify();
            }
        }
        ~FWidgetUndoScope()
        {
            if (bActive)
                GEditor->EndTransaction();
        }
    };

static FLinearColor AccentBlue() { return FLinearColor(0.35f, 0.55f, 1.0f); }

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

    // Drag-drop wrapper used by every texture-slot display and the import
    // wizard drop zone: accepts Content Browser asset drags (legacy
    // FAssetDragDropOp and FContentBrowserDataDragDropOp) and OS file drags
    // (FExternalDragOperation). The lambdas attached here decide validity and
    // perform the channel assignment.
    DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnFaceDragOver, const FGeometry&, const FDragDropEvent&);
    DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnFaceDrop, const FGeometry&, const FDragDropEvent&);

    class SFaceDropTarget : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SFaceDropTarget) {}
            SLATE_DEFAULT_SLOT(FArguments, Content)
            SLATE_EVENT(FOnFaceDragOver, OnFaceDragOver)
            SLATE_EVENT(FOnFaceDrop, OnFaceDrop)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            DragOver = InArgs._OnFaceDragOver;
            Drop = InArgs._OnFaceDrop;
            ChildSlot[InArgs._Content.Widget];
        }

        virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
        {
            return DragOver.IsBound() ? DragOver.Execute(MyGeometry, DragDropEvent) : FReply::Unhandled();
        }

        virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override
        {
            return Drop.IsBound() ? Drop.Execute(MyGeometry, DragDropEvent) : FReply::Unhandled();
        }

    private:
        FOnFaceDragOver DragOver;
        FOnFaceDrop Drop;
    };

    static bool IsDroppableImageFile(const FString& Path)
    {
        const FString Ext = FPaths::GetExtension(Path).ToLower();
        return Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg") ||
            Ext == TEXT("tga") || Ext == TEXT("bmp");
    }

}

// ====================================================================
// P6: action-point confirmation button — flashes green with a "\u2713"
// suffix for ~0.7s after a click, confirming the action landed right at the
// clicked control (undo pushes, adds, applies). Uses the same CLICK log as
// MakeBtn so the probe audit trail stays uniform. Rebuilt widgets lose the
// flash naturally (state is per-instance, never a member). Defined at
// namespace scope like every SFace* widget in this header (the anonymous
// namespace above closes with the MakeBtn helpers).
// ====================================================================
class UFaceParallaxEditorWidget::SFaceFlashButton : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceFlashButton) {}
        SLATE_ARGUMENT(FString, Text)
        SLATE_EVENT(FOnClicked, OnClicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Label = InArgs._Text;
        Clicked = InArgs._OnClicked;
        Flash = false;
        FlashUntil = 0.0;
        SetCanTick(true);
        ChildSlot
            [ SNew(SButton)
                .OnClicked_Lambda([this]()
                {
                    UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxWidget] CLICK '%s'"), *Label);
                    Flash = true;
                    FlashUntil = FSlateApplication::Get().GetCurrentTime() + 0.7;
                    return Clicked.IsBound() ? Clicked.Execute() : FReply::Handled();
                })
                .ButtonColorAndOpacity_Lambda([this]()
                {
                    return Flash ? FLinearColor(0.14f, 0.5f, 0.2f) : FLinearColor(0.15f, 0.15f, 0.15f);
                })
                .Content()
                [ SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return FText::FromString(Flash ? Label + TEXT(" \u2713") : Label);
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    .ColorAndOpacity_Lambda([this]()
                    {
                        return Flash ? FLinearColor(0.9f, 1.0f, 0.9f) : FLinearColor(0.85f, 0.85f, 0.85f);
                    }) ] ];
    }

    virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override
    {
        if (Flash && InCurrentTime > FlashUntil)
        {
            Flash = false;
            SetCanTick(false);
        }
    }

private:
    FString Label;
    FOnClicked Clicked;
    bool Flash = false;
    double FlashUntil = 0.0;
};

// ====================================================================
// SFaceLayerGizmo — canvas transform gizmo (Phase B)
// Drag body = move, bottom-right corner = scale, top handle = rotate.
// Writes through UFaceParallaxEditorWidget::SetGizmoTransform so
// canonical/override/link semantics stay in one place.
// INTERACTIVITY (Phase 1): the gizmo stays a PAINT-ONLY leaf. Its visibility
// is EVisibility::SelfHitTestInvisible so it can never intercept a click;
// every canvas click is routed by SFaceHotspotLayer (the topmost interactive
// overlay), which now ALSO resolves the gizmo handles via the pure static
// contract UFaceParallaxEditorWidget::GizmoHitTest/GizmoApplyDrag — move
// (box edge ring), rotate (top handle), scale (bottom-right corner) — so the
// transform can be dragged directly on canvas while the box interior stays a
// part-click miss (P1 one-map) and pin-drag keeps priority in pin mode.
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
            if (Owner->bShowPins)
                PaintPinMarkers(AllottedGeometry, OutDrawElements, LayerId, CanvasSize);
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

        // Phase 3: all nested-element pin markers (Show Pins toggle).
        if (Owner->bShowPins)
            PaintPinMarkers(AllottedGeometry, OutDrawElements, LayerId, CanvasSize);

        // W6: canvas transform readout — a compact "P(x, y) S(x%, y%) R(deg)"
        // label pinned to the top-left corner of the canvas so it is always
        // visible regardless of where the gizmo box sits. Drawn through the
        // pure FPTransformReadout contract so the math tests pin the string.
        // Only shown while a layer is selected (a default/empty transform
        // would print a misleading all-zero readout).
        if (Owner->GetSelectedLayerName().IsValid())
        {
            const std::string Readout = FPLayout::FPTransformReadout(
                T.Position.X, T.Position.Y, T.Scale.X, T.Scale.Y, T.Rotation);
            const FVector2D RLblSize(((float)Readout.size() * 6.5f + 14.0f), 18.0f);
            const FVector2D RLblPos(10.0f, 10.0f);
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4,
                AllottedGeometry.ToPaintGeometry(RLblSize, FSlateLayoutTransform(RLblPos)),
                Brush, ESlateDrawEffect::None, FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));
            FSlateDrawElement::MakeText(OutDrawElements, LayerId + 5,
                AllottedGeometry.ToPaintGeometry(
                    FSlateLayoutTransform(RLblPos + FVector2D(7.0f, 3.0f))),
                FText::FromString(FString(UTF8_TO_TCHAR(Readout.c_str()))),
                FCoreStyle::GetDefaultFontStyle("Regular", 9),
                ESlateDrawEffect::None, FLinearColor(0.8f, 0.95f, 1.0f, 1.0f));
        }

        return LayerId + 3;
    }

    void PaintPinMarkers(const FGeometry& G, FSlateWindowElementList& L, int32 Id,
        const FVector2D& CanvasSize) const
    {
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        if (!Owner || !Brush) return;
        TArray<FFacePinMarker> Markers;
        Owner->GetLayerPinMarkers(Markers);
        for (const FFacePinMarker& M : Markers)
        {
            const FVector2D Px = UFaceParallaxEditorWidget::GizmoUVToPixels(M.UV, CanvasSize);
            const float S = M.bPinned ? 13.0f : 9.0f;
            // Pinned: filled. Unpinned: ring (dark inner box hollows it out).
            const FLinearColor Col = M.bLayerPin
                ? FLinearColor(0.95f, 0.95f, 1.0f, 0.95f)            // white: whole-layer pin (P3)
                : (M.bPinned
                    ? (M.bRotation ? FLinearColor(0.3f, 0.85f, 1.0f, 0.95f)   // cyan: rotation pin
                                   : FLinearColor(1.0f, 0.85f, 0.3f, 0.95f))  // amber: static pin
                    : (M.bJiggle ? FLinearColor(0.75f, 0.4f, 1.0f, 0.9f)      // purple: jiggle element
                                 : FLinearColor(1.0f, 0.3f, 0.3f, 0.9f)));    // red: plain pivot anchor
            FSlateDrawElement::MakeBox(L, Id,
                G.ToPaintGeometry(FVector2D(S, S),
                    FSlateLayoutTransform(Px - FVector2D(S * 0.5f, S * 0.5f))),
                Brush, ESlateDrawEffect::None, Col);
            if (!M.bPinned)
            {
                const float Inner = S - 4.0f;
                FSlateDrawElement::MakeBox(L, Id,
                    G.ToPaintGeometry(FVector2D(Inner, Inner),
                        FSlateLayoutTransform(Px - FVector2D(Inner * 0.5f, Inner * 0.5f))),
                    Brush, ESlateDrawEffect::None, FLinearColor(0.08f, 0.08f, 0.09f, 0.95f));
            }
        }
    }

    // Public so the widget can re-invalidate the pin overlay after toggles.
    void InvalidatePaint() { Invalidate(EInvalidateWidgetReason::Paint); }

private:
    bool bPinMode = false; // true: draw the selected pinned element's handle instead of the layer transform box
};

// SFaceAccordion - one-open-per-group collapsible section stack (P16).
// Sections are AutoHeight slots (the Python validator's section contract);
// collapsed bodies are Collapsed (zero height) instead of being removed.
// The first section starts open; clicking a header swaps to that section
// (or collapses everything when the open section is clicked again).
class UFaceParallaxEditorWidget::SFaceAccordion : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceAccordion) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&)
    {
        ChildSlot
            [ SAssignNew(Box, SVerticalBox) ];
    }

    void AddSection(const FString& Title, TSharedRef<SWidget> Body)
    {
        const int32 Idx = Titles.Num();
        Titles.Add(Title);
        Open.Add(Idx == 0);

        TSharedRef<SImage> Arrow = SNew(SImage)
            .Image(FCoreStyle::Get().GetBrush(TEXT("TreeArrow_Collapsed")));
        Arrows.Add(Arrow);

        TSharedRef<STextBlock> TitleText = SNew(STextBlock)
            .Text(FText::FromString(Title))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
            .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f));
        TSharedRef<STextBlock> SummaryText = SNew(STextBlock)
            .Text(FText::FromString(TEXT("")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f));
        Summaries.Add(SummaryText);
        TSharedRef<SButton> Header = SNew(SButton)
            .ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
            .ContentPadding(FMargin(4, 6, 4, 2))
            .OnClicked_Lambda([this, Idx]()
            {
                const bool bIsOpen = Open[Idx];
                for (int32 i = 0; i < Open.Num(); ++i) Open[i] = (bIsOpen ? false : (i == Idx));
                Refresh();
                return FReply::Handled();
            })
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    .Padding(FMargin(0, 0, 6, 0))[Arrow]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [TitleText]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    .Padding(FMargin(8, 0, 0, 0))
                    [SummaryText]
                + SHorizontalBox::Slot().FillWidth(1.0f) ];
        Headers.Add(Header);

        TSharedRef<SVerticalBox> BodyWrap = SNew(SVerticalBox);
        BodyWrap->AddSlot().AutoHeight().Padding(FMargin(4, 0, 4, 4))
            [ SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f))
                .Padding(FMargin(6))
                [Body] ];
        Bodies.Add(BodyWrap);
        Box->AddSlot().AutoHeight()[Header];
        Box->AddSlot().AutoHeight()[BodyWrap];
        Refresh();
    }

    // One-open-per-group expand; -1 collapses every section.
    void SetExpanded(int32 Idx, bool bExpand)
    {
        if (!bExpand)
        {
            if (Idx >= 0 && Idx < Open.Num()) Open[Idx] = false;
            Refresh();
            return;
        }
        for (int32 i = 0; i < Open.Num(); ++i) Open[i] = (i == Idx);
        Refresh();
    }

    void ExpandAll()
    {
        for (int32 i = 0; i < Open.Num(); ++i) Open[i] = true;
        Refresh();
    }

    int32 NumSections() const { return Titles.Num(); }
    FString SectionTitle(int32 Idx) const
    {
        return Titles.IsValidIndex(Idx) ? Titles[Idx] : FString();
    }

    // Phase 4b: expose the clickable header widget so section-jump chips and
    // cross-rail search can scroll the header into view (jump target).
    TSharedRef<SWidget> GetSectionHeader(int32 Idx) const
    {
        return Headers.IsValidIndex(Idx) ? Headers[Idx] : ChildSlot.GetWidget();
    }

    // Phase 4: show a short summary (e.g. "3 issues") in a section header.
    void SetSectionSummary(int32 Idx, const FString& Text, const FLinearColor& Color)
    {
        if (Summaries.IsValidIndex(Idx))
        {
            Summaries[Idx]->SetText(FText::FromString(Text));
            Summaries[Idx]->SetColorAndOpacity(Color);
        }
    }

private:
    void Refresh()
    {
        for (int32 i = 0; i < Bodies.Num(); ++i)
        {
            Bodies[i]->SetVisibility(Open[i] ? EVisibility::Visible : EVisibility::Collapsed);
            Arrows[i]->SetImage(FCoreStyle::Get().GetBrush(
                Open[i] ? TEXT("TreeArrow_Expanded") : TEXT("TreeArrow_Collapsed")));
        }
    }

    TSharedPtr<SVerticalBox> Box;
    TArray<TSharedRef<SImage>> Arrows;
    TArray<TSharedRef<SButton>> Headers;
    TArray<TSharedPtr<STextBlock>> Summaries;
    TArray<TSharedRef<SVerticalBox>> Bodies;
    TArray<FString> Titles;
    TArray<bool> Open;
};

// ====================================================================
// SFaceDisclosure — below-section progressive disclosure (Phase 4b).
// A collapsible sub-section: clickable header row (arrow + title +
// summary, e.g. "3 of 8 on") with the body hidden until expanded.
// Used inside accordion sections (Config checks, Viseme grid) so a rail
// doesn't bury information behind two long open stacks.
// ====================================================================

class UFaceParallaxEditorWidget::SFaceDisclosure : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceDisclosure) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        bOpen = false;
        Box = SNew(SVerticalBox);
        Arrow = SNew(SImage)
            .Image(FCoreStyle::Get().GetBrush(TEXT("TreeArrow_Collapsed")));
        TitleText = SNew(STextBlock)
            .Text(FText::FromString(TEXT("")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
            .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f));
        SummaryText = SNew(STextBlock)
            .Text(FText::FromString(TEXT("")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f));
        Header = SNew(SButton)
            .ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
            .ContentPadding(FMargin(2, 4, 2, 2))
            .OnClicked_Lambda([this]() { Toggle(); return FReply::Handled(); })
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    .Padding(FMargin(0, 0, 4, 0))[Arrow.ToSharedRef()]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [TitleText.ToSharedRef()]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    .Padding(FMargin(8, 0, 0, 0))
                    [SummaryText.ToSharedRef()]
                + SHorizontalBox::Slot().FillWidth(1.0f) ];
        BodyWrap = SNew(SVerticalBox);
        BodyWrap->SetVisibility(EVisibility::Collapsed);
        Box->AddSlot().AutoHeight()[Header.ToSharedRef()];
        Box->AddSlot().AutoHeight().Padding(FMargin(6, 0, 0, 2))
            [BodyWrap.ToSharedRef()];
        ChildSlot[Box.ToSharedRef()];
    }

    void SetTitle(const FString& T) { TitleText->SetText(FText::FromString(T)); }
    void SetSummary(const FString& T, const FLinearColor& C = FLinearColor(0.6f, 0.6f, 0.6f))
    {
        SummaryText->SetText(FText::FromString(T));
        SummaryText->SetColorAndOpacity(C);
    }
    void SetBody(TSharedRef<SWidget> Body)
    {
        BodyWrap->ClearChildren();
        BodyWrap->AddSlot().AutoHeight()[Body];
    }
    void SetOpen(bool b)
    {
        bOpen = b;
        Refresh();
    }
    void Toggle()
    {
        bOpen = !bOpen;
        Refresh();
    }
    bool IsOpen() const { return bOpen; }

private:
    void Refresh()
    {
        BodyWrap->SetVisibility(bOpen ? EVisibility::Visible : EVisibility::Collapsed);
        Arrow->SetImage(FCoreStyle::Get().GetBrush(
            bOpen ? TEXT("TreeArrow_Expanded") : TEXT("TreeArrow_Collapsed")));
    }

    bool bOpen = false;
    TSharedPtr<SVerticalBox> Box;
    TSharedPtr<SVerticalBox> BodyWrap;
    TSharedPtr<SButton> Header;
    TSharedPtr<SImage> Arrow;
    TSharedPtr<STextBlock> TitleText;
    TSharedPtr<STextBlock> SummaryText;
};

// ====================================================================
// SFaceCarouselNav — page-flip strip for carousel viewports (P18).
// "prev | Page i/n | next" buttons; the page label is updated through
// SetPageText (or by grabbing the exposed Label member). The strip sits
// BELOW the page viewport; the viewport's own bottom padding keeps the
// 8px reserve (ScrollReserveBottom, P19) so the last row never blocks
// the nav buttons.
// ====================================================================

class UFaceParallaxEditorWidget::SFaceCarouselNav : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceCarouselNav) {}
    SLATE_EVENT(FOnClicked, OnPrev);
    SLATE_EVENT(FOnClicked, OnNext);
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Label = SNew(STextBlock)
            .Text(FText::FromString(TEXT("Page 1/1")))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f));
        ChildSlot
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(SButton)
                        .OnClicked(InArgs._OnPrev)
                        .ContentPadding(FMargin(8, 1))
                        [ SNew(STextBlock)
                            .Text(FText::FromString(TEXT("<")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                            .ColorAndOpacity(FLinearColor(0.8f, 0.85f, 1.0f)) ] ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [ SNew(SBox).HAlign(HAlign_Center)[Label.ToSharedRef()] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(SButton)
                        .OnClicked(InArgs._OnNext)
                        .ContentPadding(FMargin(8, 1))
                        [ SNew(STextBlock)
                            .Text(FText::FromString(TEXT(">")))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                            .ColorAndOpacity(FLinearColor(0.8f, 0.85f, 1.0f)) ] ] ];
    }

    void SetPageText(const FString& T)
    {
        if (Label.IsValid()) Label->SetText(FText::FromString(T));
    }

    TSharedPtr<STextBlock> Label;
};

// ====================================================================
// SFaceRailResizer — drag handle between the rail column and the canvas
// (Phase 4b upgrade). Drag left/right to resize the rail; width is
// clamped to [RailWidthMin, RailWidthMax] by FPLayout::ClampRailWidth and
// applied live via the rail SBox's WidthOverride (no tree rebuild).
// ====================================================================

class UFaceParallaxEditorWidget::SFaceRailResizer : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceRailResizer) {}
    SLATE_END_ARGS()

    UFaceParallaxEditorWidget* Owner = nullptr;

    void Construct(const FArguments& InArgs) {}

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(6.0f, 560.0f);
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
        const FSlateRect&, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle&, bool) const override
    {
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        if (!Brush) return LayerId;
        const FVector2D Sz = AllottedGeometry.GetLocalSize();
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
            AllottedGeometry.ToPaintGeometry(Sz, FSlateLayoutTransform(FVector2D(0, 0))),
            Brush, ESlateDrawEffect::None,
            bDragging ? FLinearColor(0.4f, 0.6f, 1.0f, 0.9f) : FLinearColor(0.12f, 0.12f, 0.14f));
        if (Sz.X > 2.0f)
        {
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
                AllottedGeometry.ToPaintGeometry(FVector2D(1.0f, Sz.Y), FSlateLayoutTransform(FVector2D((Sz.X - 1.0f) * 0.5f, 0))),
                Brush, ESlateDrawEffect::None, FLinearColor(0.3f, 0.3f, 0.32f));
        }
        return LayerId + 2;
    }

    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
    {
        bDragging = true;
        DragStartX = E.GetScreenSpacePosition().X;
        DragStartWidth = Owner ? Owner->GetRailWidthPx() : 180.0f;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& E) override
    {
        if (bDragging && Owner)
            Owner->ApplyRailWidthDelta(E.GetScreenSpacePosition().X - DragStartX);
        bDragging = false;
        return FReply::Handled().ReleaseMouseCapture();
    }

    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent& E) override
    {
        if (bDragging && Owner)
        {
            Owner->SetRailWidthLive(DragStartWidth + (E.GetScreenSpacePosition().X - DragStartX));
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }

    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override
    {
        bDragging = false;
    }

    virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
    {
        return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
    }

private:
    bool bDragging = false;
    float DragStartX = 0.0f;
    float DragStartWidth = 180.0f;
};

// SFaceHotspotLayer - transparent canvas overlay (Phase 4): maps clicks to UV
// space and hit-tests FPLayout named polygon buckets (holes/concave supported).
// PHASE 0: this layer is THE canvas click router. It is the topmost
// interactive widget in the preview SOverlay (the gizmo above it is
// SelfHitTestInvisible / paint-only), so every canvas click lands here and is
// resolved in one order: pin-drag (pin mode) -> schematic glyph -> miss
// (swallowed — nothing below may receive clicks). P1 one-map: the schematic
// glyph is the SINGLE map — left-click selects/imports, right-click opens the
// remap menu; the old hotspot-region and layer-art-quad click layers were
// deleted (the region outline paint is gone too — the schematic is the only
// map). Hover is forwarded to SFaceSchematicLayer (lens- and filter-aware).
// The layer still paints the persistent selection outline and the post-assign
// flash ring (overlays on the art quad, not a second map).
class UFaceParallaxEditorWidget::SFaceHotspotLayer : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceHotspotLayer) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) {}

    UFaceParallaxEditorWidget* Owner = nullptr;

    void SetRegions(const std::vector<FPLayout::FPHotspotRegion>& InRegions)
    {
        Regions = InRegions;
    }

    // The gizmo yields to part clicks using the same live regions (the
    // canvas layer hit-tests against these, so selection always matches).
    const std::vector<FPLayout::FPHotspotRegion>& GetRegions() const { return Regions; }

    // Phase C: per-layer art quads (draw order = layer list order, last on
    // top) + their tags, and the currently selected layer for the persistent
    // selection outline. Re-fed on every RefreshUI, so the outline and the
    // hit-test hug the art in the ACTIVE VIEW STATE's stored transforms (the
    // cross-view outline constraint).
    void SetLayerQuads(const std::vector<FPLayout::FPLayerQuad>& InQuads,
        const TArray<FString>& InTags)
    {
        LayerQuads = InQuads;
        QuadLayerTags = InTags;
    }

    void SetSelectedLayerTag(const FString& InTag) { SelectedLayerTag = InTag; }

    // Phase C: the widget's hit-test/cycle logic reads the same quads the
    // overlay paints (single source inside the layer).
    const std::vector<FPLayout::FPLayerQuad>& GetLayerQuads() const { return LayerQuads; }
    const TArray<FString>& GetQuadLayerTags() const { return QuadLayerTags; }

    // Phase 0: the schematic glyph layer to hit-test / forward hover to.
    void SetSchematicLayer(TSharedPtr<SFaceSchematicLayer> InSchematic) { Schematic = InSchematic; }

    // Phase 0: pin mode moved here from the gizmo (paint-only). When set,
    // clicks near the selected pinned element's projected handle drag it.
    void SetCanvasPinMode(bool bInPinMode)
    {
        bCanvasPinMode = bInPinMode;
        PinDragMode = 0;
    }

    // Phase 1: interactive transform gizmo. The gizmo leaf stays paint-only
    // (SelfHitTestInvisible); the hotspot owns ALL canvas input and routes
    // its three handles — move (box edge ring), rotate (top handle), scale
    // (bottom-right corner) — through the canonical transform path.
    void SetGizmoDragMode(int32 Mode) { GizmoDragMode = Mode; }
    int32 GetGizmoDragMode() const { return GizmoDragMode; }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D::ZeroVector;
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
        const FSlateRect&, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle&, bool) const override
    {
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        if (Size.X <= 0.0f || Size.Y <= 0.0f || LayerQuads.size() != (size_t)QuadLayerTags.Num()) return LayerId;
        // P1 one-map: the schematic glyph is the single map — no region
        // outline paint here (the SFaceSchematicLayer below draws it).
        // Persistent selection outline — the selected layer's quad,
        // always visible (not hover-only), in AccentBlue at 2px so the
        // current selection is confirmable at a glance.
        if (!SelectedLayerTag.IsEmpty() && LayerQuads.size() == (size_t)QuadLayerTags.Num())
        {
            for (int32 i = 0; i < QuadLayerTags.Num(); ++i)
            {
                if (QuadLayerTags[i] != SelectedLayerTag) continue;
                const FPLayout::FPLayerQuad& QL = LayerQuads[(size_t)i];
                const std::vector<FPLayout::FPHotspotPoint> Loop = { QL.C[0], QL.C[1], QL.C[2], QL.C[3] };
                DrawLoop(AllottedGeometry, OutDrawElements, LayerId, Loop, Size,
                    FLinearColor(0.4f, 0.7f, 1.0f, 0.95f), 2.0f);
                break;
            }
        }
        // Redesign: post-assign flash — a fading pulse ring on the layer that
        // just received art (1.5s), confirming the assignment right where it
        // landed on the canvas. NativeTick invalidates while the flash is live.
        if (Owner && !Owner->GetAssignFlashLayer().IsEmpty())
        {
            const double FlashAge = FSlateApplication::Get().GetCurrentTime() - Owner->GetAssignFlashTimestamp();
            if (FlashAge >= 0.0 && FlashAge < 1.5 && LayerQuads.size() == (size_t)QuadLayerTags.Num())
            {
                for (int32 i = 0; i < QuadLayerTags.Num(); ++i)
                {
                    if (QuadLayerTags[i] != Owner->GetAssignFlashLayer()) continue;
                    const FPLayout::FPLayerQuad& QL = LayerQuads[(size_t)i];
                    const std::vector<FPLayout::FPHotspotPoint> Loop = { QL.C[0], QL.C[1], QL.C[2], QL.C[3] };
                    const float Alpha = 1.0f - (float)(FlashAge / 1.5);
                    DrawLoop(AllottedGeometry, OutDrawElements, LayerId, Loop, Size,
                        FLinearColor(0.4f, 1.0f, 0.5f, Alpha), 3.0f);
                    break;
                }
            }
        }
        // Phase 2: live drop ring — the whole canvas border glows green while a
        // valid image drag hovers, so the drop target is self-evident.
        if (bDragActive)
        {
            const std::vector<FPLayout::FPHotspotPoint> Border = {
                { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 }
            };
            DrawLoop(AllottedGeometry, OutDrawElements, LayerId, Border, Size,
                FLinearColor(0.3f, 1.0f, 0.5f, 0.9f), 3.0f);
        }
        return LayerId + 1;
    }

    // Phase 0 click router: pin -> region -> glyph -> quad -> miss. Bodies of
    // the schematic-touching handlers live after SFaceSchematicLayer below
    // (the schematic must be complete before its methods are called).
    virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override;
    virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Ev) override;
    virtual void OnMouseLeave(const FPointerEvent& Ev) override;
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& Ev) override;
    virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Ev) const override;

    // Phase 2: canvas drag-drop of face art. Accepts OS image files
    // (FExternalDragOperation) and Content Browser texture assets; on drop the
    // part glyph under the cursor is resolved (lens-aware, like clicks) and the
    // payload routes through Owner->HandleCanvasDrop. A live drop ring paints
    // while the drag hovers so the canvas is visibly a drop target.
    virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
    virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
    virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
    virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;

private:
    bool NearPin(const FVector2D& Local, const FVector2D& CanvasSize) const
    {
        if (!Owner || !bCanvasPinMode) return false;
        const FVector2D PinUV = Owner->GetSelectedPinUV();
        if (PinUV.X < 0.0f) return false;
        return FVector2D::Distance(Local,
            UFaceParallaxEditorWidget::GizmoUVToPixels(PinUV, CanvasSize)) < 12.0f;
    }

    // Phase 2: does the drag payload carry assignable art (image files or
    // texture assets)?
    bool DragHasImagePayload(const FDragDropEvent& Ev);

    void DrawLoop(const FGeometry& G, FSlateWindowElementList& L, int32 Id,
        const std::vector<FPLayout::FPHotspotPoint>& Loop,
        const FVector2D& Size, const FLinearColor& Tint, float Thickness = 1.0f) const
    {
        if (Loop.size() < 2) return;
        TArray<FVector2D> Pts;
        Pts.Reserve((int32)Loop.size() + 1);
        for (const FPLayout::FPHotspotPoint& P : Loop)
            Pts.Add(FVector2D((float)(P.X * Size.X), (float)(P.Y * Size.Y)));
        const FVector2D First = Pts[0];
        Pts.Add(First);
        FSlateDrawElement::MakeLines(L, Id, G.ToPaintGeometry(), Pts,
            ESlateDrawEffect::None, Tint, true, Thickness);
    }

    std::vector<FPLayout::FPHotspotRegion> Regions;
    std::vector<FPLayout::FPLayerQuad> LayerQuads;   // Phase C: draw-order art quads
    TArray<FString> QuadLayerTags;                   // Phase C: tags parallel to LayerQuads
    FString SelectedLayerTag;                        // Phase C: selection outline source
    TSharedPtr<SFaceSchematicLayer> Schematic;       // Phase 0: glyph hit-test/hover target
    bool bCanvasPinMode = false;                     // Phase 0: pin-drag routing (from the gizmo)
    int32 PinDragMode = 0;                           // 0 none, 1 pin drag (bCanvasPinMode only)
    int32 GizmoDragMode = 0;                         // Phase 1: 0 none, 1 move, 2 rotate, 3 scale
    FFaceArtTransform GizmoDragStart;                // Phase 1: transform snapshotted at mouse-down
    FVector2D GizmoDragStartPx = FVector2D::ZeroVector; // Phase 1: grab point (canvas-local px)
    bool bDragActive = false;                        // Phase 2: valid image drag hovering the canvas
    int32 SchematicCycleIndex = 0;                   // W2: current cycle index for the stacked glyphs under the cursor
    FVector2D SchematicLastClickUV = FVector2D(-100.0f, -100.0f); // W2: last glyph-click UV (repeat detection)
};

// SFaceSchematicLayer - the central-canvas DEFAULT VIEW (redesign): paints the
// part schematic glyphs (FaceParallaxSchematic.h) for every part — P1 one-map:
// the glyph layer is the SINGLE map (Review Req 2: every glyph paints per-edge
// — exposed edges solid, edges hidden behind a front layer dashed — so the
// map reads the same occlusion as the live composition; no artless "dashed
// part" state, the placeholder glyphs ARE the character). Glyph color encodes
// the Phase I EDGE MAP by default:
// every part edge paints in its FPEdgeGroup color (eyes green, mouth red,
// hair violet, surface grey-blue) scaled by depth-class luminance (front
// lighter than back) so the group structure reads at a glance; the Canvas
// Options "Edge map" checkbox reverts to the legacy depth-class tint (front
// = amber, base = grey, back = cyan) and "Hair edges" hides the hair system's
// detailed edge levels wholesale. The canvas filter row drops filtered parts
// (FPSchematicFilterAllows mirror); the SELECTED layer's glyphs render thick
// with a soft fill while the rest dim to 25% alpha; the Focus lens zooms the
// selected layer's glyphs to fit; a click pulse ring flashes the part that
// was just picked (P1 inline feedback at the point of action). PHASE 0: the
// layer is SelfHitTestInvisible — SFaceHotspotLayer routes clicks and
// forwards hover here (lens- and filter-aware, so what you see is exactly
// what you can click). Lives between the edge overlay and the hotspot layer
// in the preview SOverlay.
class UFaceParallaxEditorWidget::SFaceSchematicLayer : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceSchematicLayer) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) {}

    UFaceParallaxEditorWidget* Owner = nullptr;

    void SetParts(const std::vector<FPSchematic::FPSchematicPart>& InParts)
    {
        Parts = InParts;
    }

    const std::vector<FPSchematic::FPSchematicPart>& GetParts() const { return Parts; }

    // Phase 0/3: resolved layer tag per part (parallel to Parts; empty =
    // unmapped). Drives the filter drop AND the selection emphasis.
    void SetPartLayerTags(const std::vector<std::string>& InTags)
    {
        PartLayerTags = InTags;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    // P1 one-map: parallel per-part art flags (1 = the mapped layer carries
    // Front albedo). Feeds the Req 2 per-edge occlusion pass (and the P2
    // status chips); there is no "artless stays dashed" glyph state — the
    // placeholder glyphs ARE the character, so every part reads per-edge.
    void SetPartStatus(const std::vector<char>& InStatus)
    {
        PartStatus = InStatus;
    }

    // Phase 7: per-part art-availability alpha (parallel to Parts; 1 = the
    // runtime renders the layer in the resolved view, 0 = the runtime hides
    // it — walk-behind rule or the view's slot has no art). Glyphs multiply
    // their outline alpha by this so the preview shows the same swap/back-half
    // logic as the component, while staying fully clickable (the P1 map is
    // never removed).
    void SetPartAlpha(const std::vector<float>& InAlpha)
    {
        PartAlpha = InAlpha;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    // Vector-art viewer: per-part resolved SVG cell pair (Cur at BlendAlpha,
    // Prev at 1 - BlendAlpha) pushed by RefreshSchematic from the pure
    // FPSvg::ResolveViewCellPair contract (Phase 2 parity: the pair mirrors
    // the runtime's at-rest committed state, the dominant card is the albedo
    // the runtime bakes, and the two-card fade lives in the runtime's own
    // parameter-space window). Empty paths = no vector art for
    // the part's resolved cells (or the viewer is off) — the paint falls
    // back to the ring glyph. PartAlpha still dims the pair (the runtime's
    // art-availability read applies to both cells).
    void SetVectorCells(const std::vector<FFaceVectorArtPaths>& InCur,
        const std::vector<FFaceVectorArtPaths>& InPrev,
        const std::vector<float>& InBlend)
    {
        VectorCur = InCur;
        VectorPrev = InPrev;
        VectorBlend = InBlend;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    // Review Req 2 (occluded-edge dashes): the resolved view state the
    // runtime picked for the current angles (FPSchematicStateAtAngles mirror,
    // set by RefreshSchematic). The paint uses it with
    // FPSchematicLayerOrderInState to decide which parts render in front of
    // each other, so every glyph's edges hidden behind a front layer draw
    // dashed — per-edge occlusion, not a per-part solid/dashed switch.
    void SetCurrentState(int32 InState)
    {
        CurrentState = InState;
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    int32 GetCurrentState() const { return CurrentState; }

    // Phase 3: the canvas filter row mirror (layer chips + depth radio).
    void SetFilters(const std::vector<std::string>& InLayerFilter, int32 InDepthFilter)
    {
        LayerFilter = InLayerFilter;
        DepthFilter = InDepthFilter;
        if (HoveredIndex >= 0)
        {
            HoveredIndex = -1;
        }
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    // Phase I: group-colored edge map. When enabled, glyphs paint with
    // FPEdgeGroup colors (eyes/mouth/hair/surface) scaled by depth-class
    // luminance (front lighter than back); hair edges can be toggled off
    // wholesale while every other group stays.
    void SetEdgeMap(bool bInEdgeMap, bool bInHairEdges)
    {
        bEdgeMap = bInEdgeMap;
        bEdgeMapHairEdges = bInHairEdges;
        if (HoveredIndex >= 0)
        {
            HoveredIndex = -1;
        }
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    bool GetEdgeMap() const { return bEdgeMap; }
    bool GetEdgeMapHairEdges() const { return bEdgeMapHairEdges; }

    // Phase 3: zoom-to-fit lens. Min/Max are the selected layer's glyph
    // bounds in part-UV space (already view-transformed); the lens maps
    // part UV -> canvas UV as (UV - Center) * Scale + 0.5 with a uniform
    // scale clamped to [1, 8] so it never zooms OUT beyond the full canvas.
    void SetFocus(bool bInFocus, const FPSchematic::FPSchematicPoint& Min,
        const FPSchematic::FPSchematicPoint& Max)
    {
        bFocus = bInFocus;
        FocusMin = Min;
        FocusMax = Max;
        const float W = FMath::Max(0.05f, (float)(Max.X - Min.X));
        const float H = FMath::Max(0.05f, (float)(Max.Y - Min.Y));
        FocusScale = bFocus ? FMath::Clamp(FMath::Min(1.0f / W, 1.0f / H) * 0.9f, 1.0f, 8.0f) : 1.0f;
        FocusCenter = FPSchematic::FPSchematicPoint{ (Min.X + Max.X) * 0.5, (Min.Y + Max.Y) * 0.5 };
        Invalidate(EInvalidateWidgetReason::Paint);
    }

    // Phase 0: hover forwarding from the hotspot layer (lens- and filter-
    // aware so the highlight always matches what is clickable).
    void SetHoveredAt(const FVector2D& CanvasUV)
    {
        int32 NewHover = -1;
        if (CanvasUV.X >= 0.0f && CanvasUV.X <= 1.0f
            && CanvasUV.Y >= 0.0f && CanvasUV.Y <= 1.0f)
        {
            const FVector2D UV = InverseFocusUV(CanvasUV);
            const FPSchematic::FPSchematicPart* Hit = FPSchematic::FPSchematicPartAt(Parts, UV.X, UV.Y);
            if (Hit && Hit->Name && Hit->Name[0])
            {
                for (size_t i = 0; i < Parts.size(); ++i)
                {
                    if (!Parts[i].Name || std::string(Parts[i].Name) != std::string(Hit->Name)) continue;
                    if (FilterAllows((int32)i))
                    {
                        NewHover = (int32)i;
                    }
                    break;
                }
            }
        }
        HoverUV = CanvasUV;
        if (NewHover != HoveredIndex)
        {
            HoveredIndex = NewHover;
            Invalidate(EInvalidateWidgetReason::Paint);
        }
    }

    void ClearHover()
    {
        if (HoveredIndex >= 0)
        {
            HoveredIndex = -1;
            Invalidate(EInvalidateWidgetReason::Paint);
        }
        HoverUV = FVector2D(-1.0f, -1.0f);
    }

    bool HasHover() const { return HoveredIndex >= 0; }

    // Phase 0: the hotspot layer's glyph step — lens-inverse the canvas UV,
    // hit-test the schematic, and reject filtered-out parts.
    const FPSchematic::FPSchematicPart* HitTest(const FVector2D& CanvasUV) const
    {
        if (Parts.empty()) return nullptr;
        const FVector2D UV = InverseFocusUV(CanvasUV);
        const FPSchematic::FPSchematicPart* Hit = FPSchematic::FPSchematicPartAt(Parts, UV.X, UV.Y);
        if (!Hit || !Hit->Name || !Hit->Name[0]) return nullptr;
        for (size_t i = 0; i < Parts.size(); ++i)
        {
            if (Parts[i].Name && std::string(Parts[i].Name) == std::string(Hit->Name))
            {
                return FilterAllows((int32)i) ? &Parts[i] : nullptr;
            }
        }
        return nullptr;
    }

    // W2 cycle-through-stack: resolve the stack index under a canvas UV to a
    // part, honoring the filter (filtered-out stack members are skipped, so
    // what is clickable is what cycles). Uses the pure schematic contracts
    // FPSchematicPartStackCount/FPSchematicPartCycleAt + FPLayout::
    // FPSchematicCycleIndex. The hotspot layer holds the cycle counter and
    // decides repeat-click; this layer only maps (UV, CycleIndex) -> part.
    const FPSchematic::FPSchematicPart* HitTestCycle(const FVector2D& CanvasUV,
        int CycleIndex) const
    {
        const int Allowed = AllowedStackDepth(CanvasUV);
        if (Allowed <= 0) return nullptr;
        int Wrapped = CycleIndex % Allowed;
        if (Wrapped < 0) Wrapped += Allowed;
        int Seen = 0;
        for (const FPSchematic::FPSchematicPart& P : Parts)
        {
            if (!P.Name || P.Outline.empty()) continue;
            if (!FPSchematic::FPPartInOutline(InverseFocusUV(CanvasUV).X,
                    InverseFocusUV(CanvasUV).Y, P.Outline)) continue;
            bool bAllowed = false;
            for (size_t i = 0; i < Parts.size(); ++i)
            {
                if (Parts[i].Name && std::string(Parts[i].Name) == std::string(P.Name))
                {
                    bAllowed = FilterAllows((int32)i);
                    break;
                }
            }
            if (!bAllowed) continue;
            if (Seen == Wrapped) return &P;
            ++Seen;
        }
        return nullptr;
    }

    // W2: filter-aware count of parts stacked under a canvas UV. This is the
    // stack depth the hotspot layer feeds to FPLayout::FPSchematicCycleIndex —
    // filtered-out members are excluded so the cycle index stays consistent
    // with what HitTestCycle resolves.
    int AllowedStackDepth(const FVector2D& CanvasUV) const
    {
        const FVector2D UV = InverseFocusUV(CanvasUV);
        int Allowed = 0;
        for (const FPSchematic::FPSchematicPart& P : Parts)
        {
            if (!P.Name || P.Outline.empty()) continue;
            if (!FPSchematic::FPPartInOutline(UV.X, UV.Y, P.Outline)) continue;
            for (size_t i = 0; i < Parts.size(); ++i)
            {
                if (Parts[i].Name && std::string(Parts[i].Name) == std::string(P.Name))
                {
                    if (FilterAllows((int32)i)) { ++Allowed; break; }
                }
            }
        }
        return Allowed;
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D::ZeroVector;
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
        const FSlateRect&, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle&, bool) const override
    {
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        if (Size.X <= 0.0f || Size.Y <= 0.0f || Parts.empty()) return LayerId;
        const FString SelTag = (Owner && Owner->GetSelectedLayerName().IsValid())
            ? Owner->GetSelectedLayerName().ToString() : FString();
        const double Now = FSlateApplication::Get().GetCurrentTime();
        const double FlashAge = (Owner && !Owner->GetSchematicFlashPart().IsEmpty())
            ? Now - Owner->GetSchematicFlashTimestamp() : -1.0;
        for (size_t i = 0; i < Parts.size(); ++i)
        {
            const FPSchematic::FPSchematicPart& P = Parts[i];
            if (!P.Name || P.Outline.size() < 2) continue;
            if (!FilterAllows((int32)i)) continue;
            // Phase I edge map: hair edges toggle off wholesale while every
            // other group stays; group colors replace the depth-class tint.
            if (bEdgeMap && !FPSchematic::FPEdgeMapShows(
                    FPSchematic::FPEdgeGroupForPartName(P.Name), bEdgeMapHairEdges))
                continue;
            // Vector-art viewer: when the part's resolved SVG cell pair is
            // present, paint the imported art INSTEAD of the ring glyph —
            // Prev at (1 - Blend), Cur at Blend, exactly the pure
            // FPSvg::ResolveViewCellPair committed-state alpha (the runtime's
            // Schmitt-window two-card crossfade; at a static pose inside a
            // state the dominant card shows at full, inside a swap window
            // the pair crossfades over the same 1.5-deg sweep as the runtime).
            // Selection/hover emphasis thickens the art's strokes; art
            // availability dims it like the glyph path. Empty cells fall
            // through to the ring glyph below (no art loaded / viewer off).
            if ((size_t)i < VectorCur.size() && (size_t)i < VectorPrev.size()
                && (VectorCur[i].IsValid() || VectorPrev[i].IsValid()))
            {
                const bool bVecSel = !SelTag.IsEmpty()
                    && (size_t)i < PartLayerTags.size()
                    && PartLayerTags[i] == TCHAR_TO_UTF8(*SelTag);
                const float Blend = (size_t)i < VectorBlend.size() ? VectorBlend[i] : 0.0f;
                const float AvailV = (size_t)i < PartAlpha.size() ? PartAlpha[i] : 1.0f;
                // Art availability: the runtime hides the quad ENTIRELY (the
                // master material is opaque — alpha 0 renders hidden). Vector
                // mode mirrors that: a card the runtime hides (walk-behind
                // features, the profile merge dropping Nose/Mouth/Teeth) is
                // NOT drawn — the FaceBase profile cell carries the merged
                // read. The glyph map stays clickable via the hotspot layer.
                if (AvailV <= 0.0f) continue;
                const float Dim = AvailV < 1.0f ? AvailV : 1.0f;
                const float ThickMul = bVecSel ? 1.6f
                    : ((int32)i == HoveredIndex ? 1.25f : 1.0f);
                if (VectorPrev[i].IsValid())
                {
                    DrawVectorCell(AllottedGeometry, OutDrawElements, LayerId,
                        VectorPrev[i], Size, (1.0f - Blend) * Dim, ThickMul);
                }
                if (VectorCur[i].IsValid())
                {
                    DrawVectorCell(AllottedGeometry, OutDrawElements, LayerId,
                        VectorCur[i], Size, Blend * Dim, ThickMul);
                }
                continue;
            }
            const bool bHovered = (int32)i == HoveredIndex;
            const bool bSelected = !SelTag.IsEmpty()
                && (size_t)i < PartLayerTags.size() && PartLayerTags[i] == TCHAR_TO_UTF8(*SelTag);
            FLinearColor Color = bEdgeMap
                ? EdgeMapColor(P) : DepthClassColor(P.DepthClass);
            // Review Req 2: per-edge occlusion for EVERY part (the placeholder
            // glyphs ARE the character — they always exist, so an artless
            // part's exposed edges draw solid too). Edges hidden behind a part
            // rendered IN FRONT of it (per-state Z-order — smaller
            // FPSchematicLayerOrderInState = closer to the camera) draw
            // dashed, so the glyph communicates the same occlusion the live
            // composition shows; exposed edges stay solid. Computed once and
            // shared by the halo fill below so every stroke of a part agrees.
            std::vector<uint8> EdgeSolid;
            const bool bPerEdge = ComputeOccludedEdges((int32)i, EdgeSolid);
            // SVG-style art face for the resolved ring: the same smooth-curve
            // chains the Art/<Part>/*.svg library was generated from (fills
            // behind strokes), always derived from P.Outline so interactions
            // keep hit-testing / filtering / focusing the ring.
            const FPSchematic::FPSchematicArtFace Face =
                FPSchematic::FPSchematicArtFaceForRing(P.Name, P.Outline);
            const bool bArt = !Face.Chains.empty();
            const FPSchematic::FPSchematicArtChain* Contour =
                bArt ? &Face.Chains[0] : nullptr;
            if (bSelected)
            {
                Color.A = 1.0f;
                // Soft fill pass: a thick low-alpha halo behind the crisp
                // outline makes the selected layer read as "filled" (the
                // occluded edges of the halo stay dashed, matching the
                // outline's occlusion read).
                if (Contour)
                {
                    DrawSmoothChain(AllottedGeometry, OutDrawElements, LayerId,
                        *Contour, Size, FLinearColor(Color.R, Color.G, Color.B, 0.08f),
                        14.0f, bPerEdge ? &EdgeSolid : nullptr);
                }
                else
                {
                    DrawDashedLoop(AllottedGeometry, OutDrawElements, LayerId,
                        P.Outline, Size, FLinearColor(Color.R, Color.G, Color.B, 0.08f), 14.0f,
                        true, bPerEdge ? &EdgeSolid : nullptr);
                }
            }
            else
            {
                // Edge-map mode paints at full alpha so the group coloring
                // actually reads; the legacy depth-class tint keeps its
                // subdued look.
                Color.A = bHovered ? 1.0f : (bEdgeMap ? 0.9f : 0.25f);
            }
            // Phase 7 art-availability: glyphs the runtime HIDES in the
            // resolved view (walk-behind rule, or the view's slot has no art)
            // render dimmed — never removed — so the preview mirrors the
            // component's swap/back-half logic while the P1 map stays intact.
            const float Avail = (size_t)i < PartAlpha.size() ? PartAlpha[i] : 1.0f;
            if (Avail < 1.0f)
                Color.A *= (Avail <= 0.0f) ? 0.15f : Avail;
            const float Thickness = bSelected ? 3.0f
                : (bHovered ? 2.5f : (bEdgeMap ? 2.0f : 1.5f));
            // Review Req 2: per-edge occlusion. Every part's edges hidden
            // behind a layer rendered IN FRONT of it (per-state Z-order —
            // smaller FPSchematicLayerOrderInState = closer to the camera)
            // draw dashed, so the glyph communicates the same occlusion the
            // live composition shows (hidden edges read as "behind"), while
            // exposed edges draw solid. There is no separate "artless stays
            // dashed" state: the placeholder glyph IS the character, so the
            // per-edge read applies to every part regardless of art.
            if (bArt)
            {
                // Fills paint behind the strokes: iris uses the stroke-colored
                // dark #16181d, highlight/gloss patches the canonical art light
                // grey #d0d4da, both dimmed with the part (selection, hover,
                // edge map and art-availability all feed Color.A).
                const float FillA = bEdgeMap ? 1.0f : FMath::Max(Color.A, 0.5f);
                for (size_t ci = 0; ci < Face.Chains.size(); ++ci)
                {
                    const FPSchematic::FPSchematicArtChain& C = Face.Chains[ci];
                    if (C.Order != 0 || !C.bFill) continue;
                    const FLinearColor FCol = (C.Tint == 2)
                        ? FLinearColor(0.086f, 0.094f, 0.114f, C.Opacity * FillA) // iris #16181d
                        : FLinearColor(0.816f, 0.831f, 0.855f, C.Opacity * FillA); // #d0d4da
                    DrawFillChain(AllottedGeometry, OutDrawElements, LayerId, C, Size, FCol);
                }
                for (size_t ci = 0; ci < Face.Chains.size(); ++ci)
                {
                    const FPSchematic::FPSchematicArtChain& C = Face.Chains[ci];
                    if (C.Order != 1) continue;
                    DrawSmoothChain(AllottedGeometry, OutDrawElements, LayerId, C, Size,
                        Color, Thickness, bPerEdge ? &EdgeSolid : nullptr);
                }
            }
            else
            {
                DrawDashedLoop(AllottedGeometry, OutDrawElements, LayerId,
                    P.Outline, Size, Color, Thickness, true,
                    bPerEdge ? &EdgeSolid : nullptr);
            }
            // P1 click pulse: bright fading ring on the part just picked
            // (0.5s) — inline feedback at the point of action.
            if (FlashAge >= 0.0 && FlashAge < 0.5
                && Owner && Owner->GetSchematicFlashPart() == UTF8_TO_TCHAR(P.Name))
            {
                const float Alpha = 1.0f - (float)(FlashAge / 0.5);
                if (Contour)
                {
                    DrawSmoothChain(AllottedGeometry, OutDrawElements, LayerId + 1,
                        *Contour, Size, FLinearColor(1.0f, 1.0f, 0.7f, Alpha), 4.0f);
                }
                else
                {
                    DrawDashedLoop(AllottedGeometry, OutDrawElements, LayerId + 1,
                        P.Outline, Size, FLinearColor(1.0f, 1.0f, 0.7f, Alpha), 4.0f, true);
                }
            }
        }
        // P2 per-part status chip: a small dot at each glyph's centroid
        // encodes slot completeness in the ACTIVE view (red = no art,
        // amber = partial A/N/D, green = full). Painted after the loops so
        // dots always sit on top of outlines and halos.
        const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
        for (size_t i = 0; i < Parts.size(); ++i)
        {
            const FPSchematic::FPSchematicPart& P = Parts[i];
            if (!P.Name || P.Outline.size() < 3) continue;
            if (!FilterAllows((int32)i)) continue;
            if (bEdgeMap && !FPSchematic::FPEdgeMapShows(
                    FPSchematic::FPEdgeGroupForPartName(P.Name), bEdgeMapHairEdges))
                continue;
            const int32 Status = (size_t)i < PartStatus.size() ? (int32)PartStatus[i] : 0;
            const FLinearColor DotColor = Status >= 2
                ? FLinearColor(0.35f, 0.85f, 0.45f)                       // green: full A/N/D
                : (Status == 1 ? FLinearColor(1.0f, 0.75f, 0.25f)         // amber: partial
                               : FLinearColor(0.8f, 0.35f, 0.35f));       // red: missing
            double CX = 0.0, CY = 0.0;
            for (const FPSchematic::FPSchematicPoint& Pt : P.Outline)
            {
                const FPSchematic::FPSchematicPoint F = FocusPoint(Pt);
                CX += F.X;
                CY += F.Y;
            }
            CX /= (double)P.Outline.size();
            CY /= (double)P.Outline.size();
            const FVector2D C((float)(CX * Size.X), (float)(CY * Size.Y));
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
                AllottedGeometry.ToPaintGeometry(FVector2D(5.0f, 5.0f),
                    FSlateLayoutTransform(C - FVector2D(2.5f, 2.5f))),
                WhiteBrush, ESlateDrawEffect::None, DotColor);
        }
        // W2 hover label: paint the region label at the cursor while a part
        // is hovered — "<Part>" or "<Part> -> <Layer>" when the resolution
        // differs. Matches the pure FPLayout::FPHoverPartLabel contract.
        if (HoveredIndex >= 0 && (size_t)HoveredIndex < Parts.size()
            && Parts[(size_t)HoveredIndex].Name && HoveredIndex < (int32)PartLayerTags.size())
        {
            const FPSchematic::FPSchematicPart& HP = Parts[(size_t)HoveredIndex];
            const char* Resolved = (!PartLayerTags[(size_t)HoveredIndex].empty())
                ? PartLayerTags[(size_t)HoveredIndex].c_str() : nullptr;
            const std::string Label = FPLayout::FPHoverPartLabel(HP.Name, Resolved);
            if (!Label.empty() && HoverUV.X >= 0.0f && HoverUV.Y >= 0.0f)
            {
                const FVector2D Anchor((float)(HoverUV.X * Size.X), (float)(HoverUV.Y * Size.Y));
                const FVector2D LblSize(Label.size() * 7.0f + 12.0f, 16.0f);
                const FVector2D LblPos(Anchor.X + 8.0f, Anchor.Y + 6.0f);
                FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
                    AllottedGeometry.ToPaintGeometry(LblSize, FSlateLayoutTransform(LblPos)),
                    WhiteBrush, ESlateDrawEffect::None,
                    FLinearColor(0.05f, 0.05f, 0.08f, 0.85f));
                FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3,
                    AllottedGeometry.ToPaintGeometry(
                        FSlateLayoutTransform(LblPos + FVector2D(6.0f, 3.0f))),
                    FText::FromString(FString(UTF8_TO_TCHAR(Label.c_str()))),
                    FCoreStyle::GetDefaultFontStyle("Regular", 8),
                    ESlateDrawEffect::None, FLinearColor(0.95f, 0.95f, 0.9f, 1.0f));
            }
        }
        // W5 pin-placement marker: when Add Pin has armed a one-shot
        // placement, draw a crosshair + "Place pin on <Layer>" label at the
        // hover position so the user sees where the pin will land before the
        // click. Only drawn while hovering the canvas (HoverUV valid).
        if (Owner && Owner->IsPendingPinPlacement()
            && HoverUV.X >= 0.0f && HoverUV.Y >= 0.0f)
        {
            const FVector2D Anchor((float)(HoverUV.X * Size.X), (float)(HoverUV.Y * Size.Y));
            const FLinearColor PinCol(0.35f, 0.9f, 1.0f, 1.0f);   // cyan, distinct from part/edge tints
            // Crosshair: two lines through the anchor.
            const float Arm = 7.0f;
            TArray<FVector2D> HLine;
            HLine.Reserve(2);
            HLine.Add(FVector2D(Anchor.X - Arm, Anchor.Y));
            HLine.Add(FVector2D(Anchor.X + Arm, Anchor.Y));
            TArray<FVector2D> VLine;
            VLine.Reserve(2);
            VLine.Add(FVector2D(Anchor.X, Anchor.Y - Arm));
            VLine.Add(FVector2D(Anchor.X, Anchor.Y + Arm));
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 4,
                AllottedGeometry.ToPaintGeometry(), HLine, ESlateDrawEffect::None, PinCol, true, 1.5f);
            FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 4,
                AllottedGeometry.ToPaintGeometry(), VLine, ESlateDrawEffect::None, PinCol, true, 1.5f);
            // Label: "<Layer> pin" on a dark chip to the right of the anchor.
            const FString PinLabel = Owner->GetSelectedLayerName().IsValid()
                ? FString::Printf(TEXT("%s pin"), *Owner->GetSelectedLayerName().ToString())
                : TEXT("Place pin");
            const FVector2D PLblSize((PinLabel.Len() * 6.5f + 12.0f), 16.0f);
            const FVector2D PLblPos(Anchor.X + 10.0f, Anchor.Y - 8.0f);
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4,
                AllottedGeometry.ToPaintGeometry(PLblSize, FSlateLayoutTransform(PLblPos)),
                WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.02f, 0.05f, 0.09f, 0.9f));
            FSlateDrawElement::MakeText(OutDrawElements, LayerId + 5,
                AllottedGeometry.ToPaintGeometry(
                    FSlateLayoutTransform(PLblPos + FVector2D(6.0f, 3.0f))),
                FText::FromString(PinLabel),
                FCoreStyle::GetDefaultFontStyle("Regular", 8),
                ESlateDrawEffect::None, FLinearColor(0.75f, 0.95f, 1.0f, 1.0f));
        }
        return LayerId + 1;
    }

    virtual void OnMouseLeave(const FPointerEvent&) override
    {
        ClearHover();
    }

private:
    // Phase 3: does the i-th part pass the filter row (layer chips + radio)?
    // The "all layers" state is the empty filter (every mapped part shows);
    // unmapped parts show only while the layer filter is empty.
    bool FilterAllows(int32 Index) const
    {
        if (Index < 0 || (size_t)Index >= Parts.size()) return false;
        const FPSchematic::FPSchematicPart& P = Parts[Index];
        const char* LayerTagName = ((size_t)Index < PartLayerTags.size() && !PartLayerTags[Index].empty())
            ? PartLayerTags[Index].c_str() : nullptr;
        return FPSchematic::FPSchematicFilterAllows(P.DepthClass, LayerTagName, LayerFilter, DepthFilter);
    }

    // Review Req 2: per-edge occlusion flags for ANY part (the placeholder
    // glyphs ARE the character, so artless parts get the same read). OutSolid[e]
    // = 1 when the part's edge e is EXPOSED (nothing rendered in front of it
    // covers the edge midpoint), 0 when a part in FRONT of it per the
    // per-state Z-order (FPSchematicLayerOrderInState: smaller = closer)
    // contains the midpoint — those edges draw dashed. Returns false (no
    // occluders at all) when every edge stays exposed (so callers keep the
    // flat solid read). Occluders must be painted themselves: filter-allowed,
    // edge-map-shown, rendered by the runtime (PartAlpha >= 0.5 — a
    // walk-behind-dimmed glyph never occludes), and visible in the state.
    bool ComputeOccludedEdges(int32 Index, std::vector<uint8>& OutSolid) const
    {
        OutSolid.clear();
        if (Index < 0 || (size_t)Index >= Parts.size()) return false;
        const FPSchematic::FPSchematicPart& P = Parts[(size_t)Index];
        if (!P.Name || P.Outline.size() < 2) return false;
        const int PO = FPSchematic::FPSchematicLayerOrderInState(CurrentState, P.Name);
        OutSolid.assign(P.Outline.size(), 1);
        bool bAnyOccluder = false;
        for (size_t k = 0; k < Parts.size(); ++k)
        {
            if (k == (size_t)Index) continue;
            const FPSchematic::FPSchematicPart& Q = Parts[k];
            if (!Q.Name || Q.Outline.size() < 3) continue;
            if (!FilterAllows((int32)k)) continue;
            if (bEdgeMap && !FPSchematic::FPEdgeMapShows(
                    FPSchematic::FPEdgeGroupForPartName(Q.Name), bEdgeMapHairEdges))
                continue;
            if ((size_t)k >= PartAlpha.size() || PartAlpha[k] < 0.5f) continue;
            const int QO = FPSchematic::FPSchematicLayerOrderInState(CurrentState, Q.Name);
            if (QO < 0) continue;                       // hidden in this state
            if (PO >= 0 && QO >= PO) continue;          // Q not strictly in front
            bAnyOccluder = true;
            for (size_t e = 0; e < P.Outline.size(); ++e)
            {
                if (OutSolid[e] == 0) continue;
                const FPSchematic::FPSchematicPoint& A = P.Outline[e];
                const FPSchematic::FPSchematicPoint& B = P.Outline[(e + 1) % P.Outline.size()];
                if (FPSchematic::FPPartInOutline((A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5, Q.Outline))
                    OutSolid[e] = 0;
            }
        }
        return bAnyOccluder;
    }

    static FLinearColor DepthClassColor(FPSchematic::FPDepthClass C)
    {
        switch (C)
        {
        case FPSchematic::FPDepthClass::Front: return FLinearColor(1.0f, 0.72f, 0.25f, 0.9f);   // amber
        case FPSchematic::FPDepthClass::Back:  return FLinearColor(0.35f, 0.85f, 1.0f, 0.9f);   // cyan
        default:                               return FLinearColor(0.72f, 0.72f, 0.78f, 0.85f); // grey
        }
    }

    // Phase I edge map: group color scaled by depth-class luminance
    // (front lighter than back). Hair parts carry their own distinct
    // color at full luminance — the detailed levels stay recognizable.
    static FLinearColor EdgeMapColor(const FPSchematic::FPSchematicPart& P)
    {
        const FPSchematic::FPEdgeColor C = FPSchematic::FPEdgeColorForPart(P.Name, P.DepthClass);
        return FLinearColor((float)C.R, (float)C.G, (float)C.B, 0.9f);
    }

    // Phase 3 focus lens: part UV -> canvas UV (identity when off).
    FPSchematic::FPSchematicPoint FocusPoint(const FPSchematic::FPSchematicPoint& P) const
    {
        if (!bFocus) return P;
        return FPSchematic::FPSchematicPoint{
            (P.X - FocusCenter.X) * FocusScale + 0.5,
            (P.Y - FocusCenter.Y) * FocusScale + 0.5 };
    }

    // Phase 3 focus lens inverse: canvas UV -> part UV (identity when off).
    FVector2D InverseFocusUV(const FVector2D& CanvasUV) const
    {
        if (!bFocus) return CanvasUV;
        return FVector2D(
            (float)((CanvasUV.X - 0.5) / FocusScale + FocusCenter.X),
            (float)((CanvasUV.Y - 0.5) / FocusScale + FocusCenter.Y));
    }

    // Dashed outline: each edge is split into ~8px dashes, every other one
    // drawn (closed loop — first point repeated at the end). Callers pass
    // bSolid=true for the no-flags fallback (fully solid outline).
    // Review Req 2: EdgeSolid (optional, parallel to the loop edges) is
    // AUTHORITATIVE when provided — an edge draws solid when its flag is
    // nonzero, dashed when zero (bSolid only applies as the no-flags
    // fallback), so an occluded edge (flag 0) stays dashed while exposed
    // edges render solid. The focus lens is applied to every point here so
    // hover/hit and paint stay in sync.
    void DrawDashedLoop(const FGeometry& G, FSlateWindowElementList& L, int32 Id,
        const std::vector<FPSchematic::FPSchematicPoint>& Loop,
        const FVector2D& Size, const FLinearColor& Color, float Thickness,
        bool bSolid = false, const std::vector<uint8>* EdgeSolid = nullptr) const
    {
        constexpr float DashLen = 8.0f;
        TArray<FVector2D> Segs;
        auto PushSegment = [&Segs](const FVector2D& A, const FVector2D& B)
        {
            Segs.Add(A);
            Segs.Add(B);
        };
        bool bDrawNext = true;
        for (size_t i = 0; i < Loop.size(); ++i)
        {
            const FPSchematic::FPSchematicPoint& P0 = FocusPoint(Loop[i]);
            const FPSchematic::FPSchematicPoint& P1 = FocusPoint(Loop[(i + 1) % Loop.size()]);
            const FVector2D A((float)(P0.X * Size.X), (float)(P0.Y * Size.Y));
            const FVector2D B((float)(P1.X * Size.X), (float)(P1.Y * Size.Y));
            const float Len = (B - A).Size();
            if (Len <= 0.0f) continue;
            if (EdgeSolid && i < EdgeSolid->size() && (*EdgeSolid)[i] != 0)
            {
                PushSegment(A, B);
                continue;
            }
            if (bSolid)
            {
                PushSegment(A, B);
                continue;
            }
            const int32 NumDashes = FMath::Max(1, FMath::CeilToInt(Len / DashLen));
            for (int32 D = 0; D < NumDashes; ++D)
            {
                const FVector2D S = A + (B - A) * ((float)D / (float)NumDashes);
                const FVector2D E = A + (B - A) * ((float)(D + 1) / (float)NumDashes);
                if (bDrawNext) PushSegment(S, E);
                bDrawNext = !bDrawNext;
            }
        }
        if (Segs.Num() > 0)
        {
            FSlateDrawElement::MakeLines(L, Id, G.ToPaintGeometry(), Segs,
                ESlateDrawEffect::None, Color, true, Thickness);
        }
    }

    // SVG-style smooth stroke: tessellates an FPSchematicArtChain's curve
    // commands (sharp L chained, cubics sampled at fixed steps — the same
    // Catmull-Rom drivers as the Art/*.svg library) and paints them as one
    // MakeLines batch. Occlusion reuses the per-edge read at COMMAND
    // granularity: a command draws solid when it covers no ring edges
    // (decorative accent) or every covered edge is exposed; dashed when any
    // covered (CovEdgeA/B, WrapCov for the Z close) edge is occluded. The
    // dash toggle flows through the whole chain exactly like DrawDashedLoop
    // so solid runs do not disturb the pattern. The focus lens + Size scale
    // are applied per vertex so paint and hit stay in sync.
    void DrawSmoothChain(const FGeometry& G, FSlateWindowElementList& L, int32 Id,
        const FPSchematic::FPSchematicArtChain& Ch, const FVector2D& Size,
        const FLinearColor& Color, float Thickness,
        const std::vector<uint8>* EdgeSolid = nullptr) const
    {
        constexpr float DashLen = 8.0f;
        TArray<FVector2D> Segs;
        auto PushSegment = [&Segs](const FVector2D& A, const FVector2D& B)
        {
            Segs.Add(A);
            Segs.Add(B);
        };
        auto ToPix = [&](const FPSchematic::FPSchematicPoint& P) -> FVector2D
        {
            const FPSchematic::FPSchematicPoint F = FocusPoint(P);
            return FVector2D((float)(F.X * Size.X), (float)(F.Y * Size.Y));
        };
        auto CoveredSolid = [&](int32 e) -> bool
        {
            if (!EdgeSolid) return true;
            if (e < 0 || (size_t)e >= EdgeSolid->size()) return true;
            return (*EdgeSolid)[e] != 0;
        };
        bool bDrawNext = true;
        FVector2D CurrPix = ToPix(Ch.Start);
        auto DrawSpan = [&](const FVector2D& A, const FVector2D& B, bool bSolid)
        {
            const float Len = (B - A).Size();
            if (Len <= 0.0f) return;
            if (bSolid)
            {
                PushSegment(A, B);
                return;
            }
            const int32 NumDashes = FMath::Max(1, FMath::CeilToInt(Len / DashLen));
            for (int32 D = 0; D < NumDashes; ++D)
            {
                const FVector2D S = A + (B - A) * ((float)D / (float)NumDashes);
                const FVector2D E = A + (B - A) * ((float)(D + 1) / (float)NumDashes);
                if (bDrawNext) PushSegment(S, E);
                bDrawNext = !bDrawNext;
            }
        };
        for (const FPSchematic::FPSchematicCurveCmd& C : Ch.Cmds)
        {
            // A command is solid when it has no covered edges (decorative)
            // or every covered edge stays exposed.
            const bool bSolid = (C.CovEdgeA < 0 && C.CovEdgeB < 0)
                || (CoveredSolid(C.CovEdgeA) && CoveredSolid(C.CovEdgeB));
            if (C.Type == 0)
            {
                const FVector2D End = ToPix(C.End);
                DrawSpan(CurrPix, End, bSolid);
                CurrPix = End;
            }
            else
            {
                const FVector2D P0 = CurrPix;
                const FVector2D P1 = ToPix(C.C1);
                const FVector2D P2 = ToPix(C.C2);
                const FVector2D P3 = ToPix(C.End);
                const int32 Steps = 16;
                for (int32 S = 1; S <= Steps; ++S)
                {
                    const float T = (float)S / (float)Steps;
                    const float U = 1.0f - T;
                    const FVector2D P = P0 * (U * U * U)
                        + P1 * (3.0f * U * U * T)
                        + P2 * (3.0f * U * T * T)
                        + P3 * (T * T * T);
                    DrawSpan(CurrPix, P, bSolid);
                    CurrPix = P;
                }
            }
        }
        if (Ch.bClosed && Ch.WrapCov < 0)
        {
            DrawSpan(CurrPix, ToPix(Ch.Start), true);
        }
        else if (Ch.bClosed)
        {
            DrawSpan(CurrPix, ToPix(Ch.Start), CoveredSolid(Ch.WrapCov));
        }
        if (Segs.Num() > 0)
        {
            FSlateDrawElement::MakeLines(L, Id, G.ToPaintGeometry(), Segs,
                ESlateDrawEffect::None, Color, true, Thickness);
        }
    }

    // Closed flat fill: the chain's boundary (closed ellipse outlines for the
    // iris / highlights / gloss patches) is tessellated and painted as a
    // triangle fan via a custom-verts batch, so the patch reads as solid art
    // rather than an outline. Fans only apply to bClosed fill chains; open
    // chain bodies never reach this call.
    void DrawFillChain(const FGeometry& G, FSlateWindowElementList& L, int32 Id,
        const FPSchematic::FPSchematicArtChain& Ch, const FVector2D& Size,
        const FLinearColor& Color) const
    {
        TArray<FVector2D> Bnd;
        auto ToPix = [&](const FPSchematic::FPSchematicPoint& P) -> FVector2D
        {
            const FPSchematic::FPSchematicPoint F = FocusPoint(P);
            return FVector2D((float)(F.X * Size.X), (float)(F.Y * Size.Y));
        };
        FVector2D Cur = ToPix(Ch.Start);
        Bnd.Add(Cur);
        for (const FPSchematic::FPSchematicCurveCmd& C : Ch.Cmds)
        {
            if (C.Type == 0)
            {
                Cur = ToPix(C.End);
                Bnd.Add(Cur);
            }
            else
            {
                const FVector2D P0 = Cur;
                const FVector2D P1 = ToPix(C.C1);
                const FVector2D P2 = ToPix(C.C2);
                const FVector2D P3 = ToPix(C.End);
                const int32 Steps = 12;
                for (int32 S = 1; S <= Steps; ++S)
                {
                    const float T = (float)S / (float)Steps;
                    const float U = 1.0f - T;
                    Cur = P0 * (U * U * U)
                        + P1 * (3.0f * U * U * T)
                        + P2 * (3.0f * U * T * T)
                        + P3 * (T * T * T);
                    Bnd.Add(Cur);
                }
            }
        }
        if (!Ch.bClosed || Bnd.Num() < 3) return;
        // Re-seal the loop so the fan's last triangle wraps back to Start.
        const FVector2D FirstPt = Bnd[0];
        Bnd.Add(FirstPt);
        double SX = 0.0, SY = 0.0;
        for (const FVector2D& P : Bnd) { SX += P.X; SY += P.Y; }
        const FVector2D Cent((float)(SX / (double)Bnd.Num()),
            (float)(SY / (double)Bnd.Num()));
        const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
        const FSlateResourceHandle& Handle =
            FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
        const FSlateRenderTransform& RT = G.GetAccumulatedRenderTransform();
        TArray<FSlateVertex> Verts;
        TArray<SlateIndex> Indices;
        const FColor Col = Color.ToFColor(true);
        Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT,
            FVector2f(Cent.X, Cent.Y), FVector2f(0.0f, 0.0f), Col));
        for (const FVector2D& P : Bnd)
        {
            Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT,
                FVector2f(P.X, P.Y), FVector2f(0.0f, 0.0f), Col));
        }
        Indices.Reserve((Bnd.Num() - 1) * 3);
        for (int32 K = 1; K < Bnd.Num() - 1; ++K)
        {
            Indices.Add(0);
            Indices.Add((SlateIndex)K);
            Indices.Add((SlateIndex)(K + 1));
        }
        FSlateDrawElement::MakeCustomVerts(L, Id, Handle, Verts, Indices,
            nullptr, 0, 0, ESlateDrawEffect::None);
    }

    // Vector-art viewer cell paint: the imported SVG art (FFaceVectorArtPaths)
    // for one resolved cell — fills first (closed fill paths as triangle
    // fans, like DrawFillChain), then strokes (lines/quads/cubics tessellated
    // into MakeLines, like DrawSmoothChain). Paths live in the same 0..1
    // viewBox space as the rings, so the focus lens + Size scale apply per
    // vertex; the parsed per-path fill/stroke colors carry the art's own
    // alpha, multiplied by AlphaMul (bracket crossfade + art availability).
    // The SVG stroke widths are 1000-viewBox user units, scaled to pixels
    // via Size.X / 1000, then ThickMul (selection/hover emphasis).
    void DrawVectorCell(const FGeometry& G, FSlateWindowElementList& L, int32 Id,
        const FFaceVectorArtPaths& Cell, const FVector2D& Size,
        float AlphaMul, float ThickMul) const
    {
        if (AlphaMul <= 0.0f || !Cell.IsValid()) return;
        auto ToPix = [&](const FVector2D& P) -> FVector2D
        {
            const FPSchematic::FPSchematicPoint F = FocusPoint(
                FPSchematic::FPSchematicPoint{ P.X, P.Y });
            return FVector2D((float)(F.X * Size.X), (float)(F.Y * Size.Y));
        };
        const float ThickScale = Size.X / 1000.0f;
        for (const FFaceVectorPath& Path : Cell.Paths)
        {
            if (Path.Cmds.Num() < 1) continue;
            if (Path.bHasFill && Path.bClosed)
            {
                TArray<FVector2D> Bnd;
                FVector2D Cur = ToPix(Path.Cmds[0].P);
                Bnd.Add(Cur);
                for (int32 c = 0; c < Path.Cmds.Num(); ++c)
                {
                    const FFaceVectorCmdPt& C = Path.Cmds[c];
                    if (C.Cmd == EFaceVectorCmd::LineTo
                        || C.Cmd == EFaceVectorCmd::Close
                        || C.Cmd == EFaceVectorCmd::MoveTo)
                    {
                        Cur = ToPix(C.P);
                        Bnd.Add(Cur);
                    }
                    else if (C.Cmd == EFaceVectorCmd::QuadTo)
                    {
                        // Pts[i] = control, Pts[i+1] = target (pure-parser layout).
                        if (c + 1 >= Path.Cmds.Num()) break;
                        const FVector2D P0 = Cur;
                        const FVector2D P1 = ToPix(C.P);
                        const FVector2D P2 = ToPix(Path.Cmds[c + 1].P);
                        const int32 Steps = 12;
                        for (int32 S = 1; S <= Steps; ++S)
                        {
                            const float T = (float)S / (float)Steps;
                            const float U = 1.0f - T;
                            Cur = P0 * (U * U) + P1 * (2.0f * U * T) + P2 * (T * T);
                            Bnd.Add(Cur);
                        }
                        ++c;
                    }
                    else if (C.Cmd == EFaceVectorCmd::CubicTo)
                    {
                        // Pts[i] = control1, Pts[i+1] = control2, Pts[i+2] = target.
                        if (c + 2 >= Path.Cmds.Num()) break;
                        const FVector2D P0 = Cur;
                        const FVector2D P1 = ToPix(C.P);
                        const FVector2D P2 = ToPix(Path.Cmds[c + 1].P);
                        const FVector2D P3 = ToPix(Path.Cmds[c + 2].P);
                        const int32 Steps = 12;
                        for (int32 S = 1; S <= Steps; ++S)
                        {
                            const float T = (float)S / (float)Steps;
                            const float U = 1.0f - T;
                            Cur = P0 * (U * U * U)
                                + P1 * (3.0f * U * U * T)
                                + P2 * (3.0f * U * T * T)
                                + P3 * (T * T * T);
                            Bnd.Add(Cur);
                        }
                        c += 2;
                    }
                }
                if (Bnd.Num() >= 3)
                {
                    const FVector2D FirstPt = Bnd[0];
                    Bnd.Add(FirstPt);
                    double SX = 0.0, SY = 0.0;
                    for (const FVector2D& P : Bnd) { SX += P.X; SY += P.Y; }
                    const FVector2D Cent((float)(SX / (double)Bnd.Num()),
                        (float)(SY / (double)Bnd.Num()));
                    const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
                    const FSlateResourceHandle& Handle =
                        FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
                    const FSlateRenderTransform& RT = G.GetAccumulatedRenderTransform();
                    TArray<FSlateVertex> Verts;
                    TArray<SlateIndex> Indices;
                    FLinearColor FCol = Path.Fill;
                    FCol.A *= AlphaMul;
                    const FColor Col = FCol.ToFColor(true);
                    Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT,
                        FVector2f(Cent.X, Cent.Y), FVector2f(0.0f, 0.0f), Col));
                    for (const FVector2D& P : Bnd)
                    {
                        Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT,
                            FVector2f(P.X, P.Y), FVector2f(0.0f, 0.0f), Col));
                    }
                    Indices.Reserve((Bnd.Num() - 1) * 3);
                    for (int32 K = 1; K < Bnd.Num() - 1; ++K)
                    {
                        Indices.Add(0);
                        Indices.Add((SlateIndex)K);
                        Indices.Add((SlateIndex)(K + 1));
                    }
                    FSlateDrawElement::MakeCustomVerts(L, Id, Handle, Verts, Indices,
                        nullptr, 0, 0, ESlateDrawEffect::None);
                }
            }
            if (Path.bHasStroke)
            {
                TArray<FVector2D> Segs;
                FVector2D Cur = ToPix(Path.Cmds[0].P);
                auto PushLine = [&Segs](const FVector2D& A, const FVector2D& B)
                {
                    Segs.Add(A);
                    Segs.Add(B);
                };
                for (int32 c = 0; c < Path.Cmds.Num(); ++c)
                {
                    const FFaceVectorCmdPt& C = Path.Cmds[c];
                    if (C.Cmd == EFaceVectorCmd::LineTo
                        || C.Cmd == EFaceVectorCmd::Close
                        || C.Cmd == EFaceVectorCmd::MoveTo)
                    {
                        const FVector2D End = ToPix(C.P);
                        PushLine(Cur, End);
                        Cur = End;
                    }
                    else if (C.Cmd == EFaceVectorCmd::QuadTo)
                    {
                        if (c + 1 >= Path.Cmds.Num()) break;
                        const FVector2D P0 = Cur;
                        const FVector2D P1 = ToPix(C.P);
                        const FVector2D P2 = ToPix(Path.Cmds[c + 1].P);
                        const int32 Steps = 12;
                        for (int32 S = 1; S <= Steps; ++S)
                        {
                            const float T = (float)S / (float)Steps;
                            const float U = 1.0f - T;
                            const FVector2D P = P0 * (U * U)
                                + P1 * (2.0f * U * T) + P2 * (T * T);
                            PushLine(Cur, P);
                            Cur = P;
                        }
                        ++c;
                    }
                    else if (C.Cmd == EFaceVectorCmd::CubicTo)
                    {
                        if (c + 2 >= Path.Cmds.Num()) break;
                        const FVector2D P0 = Cur;
                        const FVector2D P1 = ToPix(C.P);
                        const FVector2D P2 = ToPix(Path.Cmds[c + 1].P);
                        const FVector2D P3 = ToPix(Path.Cmds[c + 2].P);
                        const int32 Steps = 16;
                        for (int32 S = 1; S <= Steps; ++S)
                        {
                            const float T = (float)S / (float)Steps;
                            const float U = 1.0f - T;
                            const FVector2D P = P0 * (U * U * U)
                                + P1 * (3.0f * U * U * T)
                                + P2 * (3.0f * U * T * T)
                                + P3 * (T * T * T);
                            PushLine(Cur, P);
                            Cur = P;
                        }
                        c += 2;
                    }
                }
                FLinearColor SCol = Path.Stroke;
                SCol.A *= AlphaMul;
                const float Thickness = FMath::Max(0.5f,
                    Path.StrokeWidth * ThickScale * ThickMul);
                if (Segs.Num() > 0)
                {
                    FSlateDrawElement::MakeLines(L, Id, G.ToPaintGeometry(), Segs,
                        ESlateDrawEffect::None, SCol, true, Thickness);
                }
            }
        }
    }

    std::vector<FPSchematic::FPSchematicPart> Parts;
    std::vector<std::string> PartLayerTags;      // Phase 0/3: resolved tag per part (parallel)
    std::vector<char> PartStatus;                // P2: per-part slot completeness in the ACTIVE view (0 none, 1 partial, 2 full)
    std::vector<float> PartAlpha;                // Phase 7: per-part art-availability alpha (1 = runtime shows, 0 = runtime hides)
    std::vector<FFaceVectorArtPaths> VectorCur;  // vector viewer: resolved Cur cells (parallel; empty = ring fallback)
    std::vector<FFaceVectorArtPaths> VectorPrev; // vector viewer: resolved Prev cells
    std::vector<float> VectorBlend;              // vector viewer: bracket blend (Cur alpha)
    int32 CurrentState = 0;                      // Review Req 2: resolved view state for per-edge occlusion
    std::vector<std::string> LayerFilter;        // Phase 3: selected layer chips (empty = all)
    int32 DepthFilter = 0;                       // Phase 3: 0 all, 1 Front, 2 Base, 3 Back
    bool bFocus = false;                         // Phase 3: zoom-to-fit lens
    bool bEdgeMap = false;                       // Phase I: group-colored edge map
    bool bEdgeMapHairEdges = true;               // Phase I: hair edges visible (edge map only)
    FPSchematic::FPSchematicPoint FocusMin;      // selected layer's glyph bounds (part UV)
    FPSchematic::FPSchematicPoint FocusMax;
    FPSchematic::FPSchematicPoint FocusCenter{ 0.5, 0.5 };
    float FocusScale = 1.0f;
    int32 HoveredIndex = -1;
    FVector2D HoverUV = FVector2D(-1.0f, -1.0f);   // W2: hover label anchor (canvas UV)
};

// ====================================================================
// SFaceHotspotLayer router bodies (Phase 0). Defined after
// SFaceSchematicLayer so the glyph step and hover forwarding can call its
// lens- and filter-aware API. Click order (P1 one-map): pin-drag -> glyph
// (left select/import, right remap) -> miss (swallowed so no image below
// ever receives a canvas click).
// ====================================================================
inline FReply UFaceParallaxEditorWidget::SFaceHotspotLayer::OnMouseButtonDown(
    const FGeometry& Geo, const FPointerEvent& Ev)
{
    if (!Owner) return FReply::Unhandled();
    const FVector2D CanvasSize = Geo.GetLocalSize();
    if (CanvasSize.X < 1.0f || CanvasSize.Y < 1.0f) return FReply::Unhandled();
    const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
    const FVector2D UV = UFaceParallaxEditorWidget::GizmoPixelsToUV(Local, CanvasSize);
    const bool bLeft = Ev.GetEffectingButton() == EKeys::LeftMouseButton;
    const bool bRight = Ev.GetEffectingButton() == EKeys::RightMouseButton;
    if (!bLeft && !bRight) return FReply::Handled();   // other buttons: keep the canvas inert

    // (0) Add Pin one-shot placement: a left-click right after "Add Pin"
    // places the newly-added pin at the cursor instead of routing to pin-drag /
    // glyph select. The arm is consumed by exactly one click (ConsumePendingPinPlacement
    // clears it), then the normal one-map order resumes.
    if (bLeft && Owner && Owner->ConsumePendingPinPlacement())
    {
        Owner->PlacePinAtUV(UV);
        return FReply::Handled();
    }

    // (0) P7-C: pins are always live — dragging an existing pin handle (the
    // selected element's pin, or the layer pin when no element is selected)
    // moves the pin. A click NOT on a handle falls through to one-map part
    // selection below (the old toggle's unconditional place-on-click is gone:
    // pins are placed via the Pins list / element rows, not canvas clicks).
    if (bLeft && bCanvasPinMode && NearPin(Local, CanvasSize))
    {
        PinDragMode = 1;
        return FReply::Handled().CaptureMouse(AsShared());
    }

    // (0.5) Phase 1: interactive transform gizmo handles. Left-drag only, and
    // only when a layer is selected and we are NOT in canvas pin mode (the pin
    // handle owns the pointer then). HitTest resolves move (box edge ring),
    // rotate (top handle), scale (bottom-right corner); the box INTERIOR is a
    // deliberate miss so part glyphs behind it stay clickable (P1 one-map).
    // The transform is snapshotted once at mouse-down and recomputed from that
    // snapshot every move (never incremental), so the drag cannot drift.
    if (bLeft && !bCanvasPinMode && Owner && Owner->GetSelectedLayerName().IsValid())
    {
        const int32 Hit = UFaceParallaxEditorWidget::GizmoHitTest(
            Owner->GetGizmoTransform(), Local, CanvasSize);
        if (Hit != UFaceParallaxEditorWidget::kGizmoHitNone)
        {
            GizmoDragMode = Hit;
            GizmoDragStart = Owner->GetGizmoTransform();
            GizmoDragStartPx = Local;
            return FReply::Handled().CaptureMouse(AsShared());
        }
    }

    // (1) Schematic glyph (lens- and filter-aware) — P1 one-map: the glyph
    // is the SINGLE canvas map. Left-click selects the resolved layer (and
    // opens the import wizard when it has no art); right-click opens the
    // remap menu. W2: stacked glyphs under one point cycle on repeat clicks
    // (or Alt-click) — a click at a NEW spot resets to the topmost part, a
    // repeat click at the same spot advances through the stack. The old
    // named-region and layer-art-quad click layers are gone — no other
    // modifier paths remain.
    if (Schematic.IsValid())
    {
        // W2 repeat detection: a click counts as "same spot" when it lands
        // within a small UV tolerance of the previous glyph click.
        const bool bRepeatClick = (UV - SchematicLastClickUV).Size() < 0.03f
            || Ev.IsAltDown();
        const int StackDepth = Schematic->AllowedStackDepth(UV);
        SchematicCycleIndex = FPLayout::FPSchematicCycleIndex(
            StackDepth, SchematicCycleIndex, bRepeatClick);
        const FPSchematic::FPSchematicPart* Part = Schematic->HitTestCycle(UV, SchematicCycleIndex);
        if (!Part)
        {
            // Fall back to the plain topmost hit (miss-only when nothing is
            // under the cursor at all, or every stack member is filtered).
            Part = Schematic->HitTest(UV);
        }
        if (Part && Part->Name && Part->Name[0])
        {
            const FString PartName(Part->Name);
            SchematicLastClickUV = UV;
            if (bRight)
                Owner->OpenHotspotRemapMenu(PartName, Ev);
            else
                Owner->HandleSchematicPartClick(PartName);
            return FReply::Handled();
        }
        SchematicLastClickUV = UV;
    }

    // (2) Miss: swallow. SOverlay does NOT re-route Unhandled to sibling
    // layers — without this the click would be dead (and the SImages below
    // would swallow it instead).
    return FReply::Handled();
}

inline FReply UFaceParallaxEditorWidget::SFaceHotspotLayer::OnMouseMove(
    const FGeometry& Geo, const FPointerEvent& Ev)
{
    if (GizmoDragMode != 0 && Owner)
    {
        const FVector2D CanvasSize = Geo.GetLocalSize();
        if (CanvasSize.X >= 1.0f && CanvasSize.Y >= 1.0f)
        {
            Owner->SetGizmoTransform(UFaceParallaxEditorWidget::GizmoApplyDrag(
                GizmoDragStart, GizmoDragMode, GizmoDragStartPx,
                Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()), CanvasSize));
        }
        return FReply::Handled();
    }
    if (PinDragMode == 1 && Owner)
    {
        const FVector2D CanvasSize = Geo.GetLocalSize();
        if (CanvasSize.X < 1.0f || CanvasSize.Y < 1.0f) return FReply::Handled();
        Owner->SetGizmoPinUV(UFaceParallaxEditorWidget::GizmoPixelsToUV(
            Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()), CanvasSize));
        return FReply::Handled();
    }
    if (Schematic.IsValid())
    {
        const FVector2D CanvasSize = Geo.GetLocalSize();
        if (CanvasSize.X >= 1.0f && CanvasSize.Y >= 1.0f)
        {
            const FVector2D UV = UFaceParallaxEditorWidget::GizmoPixelsToUV(
                Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()), CanvasSize);
            Schematic->SetHoveredAt(UV);
        }
    }
    return FReply::Unhandled();
}

inline FReply UFaceParallaxEditorWidget::SFaceHotspotLayer::OnMouseButtonUp(
    const FGeometry&, const FPointerEvent&)
{
    if (GizmoDragMode != 0)
    {
        GizmoDragMode = 0;
        return FReply::Handled().ReleaseMouseCapture();
    }
    if (PinDragMode == 0) return FReply::Unhandled();
    PinDragMode = 0;
    return FReply::Handled().ReleaseMouseCapture();
}

inline void UFaceParallaxEditorWidget::SFaceHotspotLayer::OnMouseCaptureLost(
    const FCaptureLostEvent&)
{
    GizmoDragMode = 0;
    PinDragMode = 0;
}

inline void UFaceParallaxEditorWidget::SFaceHotspotLayer::OnMouseLeave(const FPointerEvent&)
{
    if (Schematic.IsValid()) Schematic->ClearHover();
}

inline FCursorReply UFaceParallaxEditorWidget::SFaceHotspotLayer::OnCursorQuery(
    const FGeometry& Geo, const FPointerEvent& Ev) const
{
    // Phase 1: handle-specific cursors for the interactive gizmo (priority 1 —
    // a handle is a bigger target than the glyph under it). Only outside pin
    // mode, where the pin handle owns the pointer.
    if (Owner && Owner->GetSelectedLayerName().IsValid() && !bCanvasPinMode)
    {
        const FVector2D CanvasSize = Geo.GetLocalSize();
        if (CanvasSize.X >= 1.0f && CanvasSize.Y >= 1.0f)
        {
            const int32 Hit = UFaceParallaxEditorWidget::GizmoHitTest(
                Owner->GetGizmoTransform(),
                Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()), CanvasSize);
            if (Hit == UFaceParallaxEditorWidget::kGizmoHitMove)
                return FCursorReply::Cursor(EMouseCursor::CardinalCross);
            if (Hit == UFaceParallaxEditorWidget::kGizmoHitRotate)
                return FCursorReply::Cursor(EMouseCursor::Crosshairs);
            if (Hit == UFaceParallaxEditorWidget::kGizmoHitScale)
                return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
        }
    }
    if (Schematic.IsValid() && Schematic->HasHover())
        return FCursorReply::Cursor(EMouseCursor::Hand);
    if (Owner && bCanvasPinMode)
    {
        const FVector2D CanvasSize = Geo.GetLocalSize();
        if (CanvasSize.X >= 1.0f && CanvasSize.Y >= 1.0f
            && NearPin(Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()), CanvasSize))
        {
            return FCursorReply::Cursor(EMouseCursor::Hand);
        }
    }
    return FCursorReply::Unhandled();
}

// Phase 2: does the drag payload carry anything assignable? OS image files
// (FExternalDragOperation) and Content Browser texture assets (legacy
// FAssetDragDropOp + FContentBrowserDataDragDropOp).
inline bool UFaceParallaxEditorWidget::SFaceHotspotLayer::DragHasImagePayload(
    const FDragDropEvent& Ev)
{
    if (TSharedPtr<FExternalDragOperation> FileOp = Ev.GetOperationAs<FExternalDragOperation>())
    {
        if (FileOp->HasFiles())
        {
            for (const FString& File : FileOp->GetFiles())
                if (IsDroppableImageFile(File)) return true;
        }
        return false;
    }
    if (TSharedPtr<FAssetDragDropOp> AssetOp = Ev.GetOperationAs<FAssetDragDropOp>())
    {
        for (const FAssetData& Asset : AssetOp->GetAssets())
            if (Asset.GetClass() && Asset.GetClass()->IsChildOf(UTexture2D::StaticClass())) return true;
        return false;
    }
    if (TSharedPtr<FContentBrowserDataDragDropOp> DataOp = Ev.GetOperationAs<FContentBrowserDataDragDropOp>())
    {
        for (const FAssetData& Asset : DataOp->GetAssets())
            if (Asset.GetClass() && Asset.GetClass()->IsChildOf(UTexture2D::StaticClass())) return true;
        return false;
    }
    return false;
}

inline void UFaceParallaxEditorWidget::SFaceHotspotLayer::OnDragEnter(
    const FGeometry&, const FDragDropEvent& Ev)
{
    const bool bOk = Owner && DragHasImagePayload(Ev);
    if (bOk != bDragActive)
    {
        bDragActive = bOk;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

inline FReply UFaceParallaxEditorWidget::SFaceHotspotLayer::OnDragOver(
    const FGeometry&, const FDragDropEvent& Ev)
{
    const bool bOk = Owner && DragHasImagePayload(Ev);
    if (bOk != bDragActive)
    {
        bDragActive = bOk;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    return bOk ? FReply::Handled() : FReply::Unhandled();
}

inline FReply UFaceParallaxEditorWidget::SFaceHotspotLayer::OnDrop(
    const FGeometry& Geo, const FDragDropEvent& Ev)
{
    bDragActive = false;
    Invalidate(EInvalidateWidgetReason::Paint);
    if (!Owner || !DragHasImagePayload(Ev)) return FReply::Unhandled();
    const FVector2D CanvasSize = Geo.GetLocalSize();
    if (CanvasSize.X < 1.0f || CanvasSize.Y < 1.0f) return FReply::Unhandled();
    // Resolve the part glyph under the drop point (lens-aware, same hit-test
    // the click path uses) so a drop lands on the part the cursor is over.
    const FVector2D UV = UFaceParallaxEditorWidget::GizmoPixelsToUV(
        Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()), CanvasSize);
    FString PartName;
    FName PartLayer;
    if (Schematic.IsValid())
    {
        const FPSchematic::FPSchematicPart* Part = Schematic->HitTest(UV);
        if (Part && Part->Name && Part->Name[0])
        {
            PartName = FString(Part->Name);
            PartLayer = Owner->ResolveHotspotLayer(PartName);
        }
    }
    Owner->HandleCanvasDrop(PartName, PartLayer, Ev);
    return FReply::Handled();
}

inline void UFaceParallaxEditorWidget::SFaceHotspotLayer::OnDragLeave(const FDragDropEvent&)
{
    if (bDragActive)
    {
        bDragActive = false;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

// SFaceCanvasResizer — drag handle between the canvas and the parts strip
// (central-view redesign): drag vertically to resize the preview canvas
// height, clamped to [MinCanvasHeight, MaxCanvasHeight] and applied live via
// the PreviewHost SBox's HeightOverride lambda (no tree rebuild, no manifest
// change — the 450px design constant stays the default).
class UFaceParallaxEditorWidget::SFaceCanvasResizer : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SFaceCanvasResizer) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) {}

    UFaceParallaxEditorWidget* Owner = nullptr;

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(0.0f, 6.0f);
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
        const FSlateRect&, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle&, bool) const override
    {
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        if (!Brush) return LayerId;
        const FVector2D Sz = AllottedGeometry.GetLocalSize();
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
            AllottedGeometry.ToPaintGeometry(Sz, FSlateLayoutTransform(FVector2D(0, 0))),
            Brush, ESlateDrawEffect::None,
            bDragging ? FLinearColor(0.4f, 0.6f, 1.0f, 0.9f) : FLinearColor(0.10f, 0.10f, 0.12f));
        return LayerId + 1;
    }

    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
    {
        bDragging = true;
        DragStartY = E.GetScreenSpacePosition().Y;
        DragStartHeight = Owner ? Owner->GetCanvasHeight() : 450.0f;
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
    {
        bDragging = false;
        return FReply::Handled().ReleaseMouseCapture();
    }

    virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent& E) override
    {
        if (bDragging && Owner)
        {
            Owner->SetCanvasHeight(DragStartHeight + (E.GetScreenSpacePosition().Y - DragStartY));
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }

    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override
    {
        bDragging = false;
    }

    virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
    {
        return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
    }

private:
    bool bDragging = false;
    float DragStartY = 0.0f;
    float DragStartHeight = 450.0f;
};

// SZoneBoundaryOverlay - transparent drag layer over the zone diagram (P3):
// paints the 8 zone-boundary lines (from the 4 ZoneBoundaryMultipliers via
// GetBoundaryOrDefault, mirrored ±) plus the live yaw cursor, and lets the
// user drag any boundary line to remap its multiplier live. Pixel deltas are
// converted to degrees (the full 360° spans the diagram width) and fed through
// FPLayout::ZoneBoundaryAfterDrag — the same math the mirrors/tests cover — so
// a negative-side boundary dragged toward the center shrinks its multiplier.
// During the drag the Owner live-updates the diagram bar and the Camera rail
// text editors; on release it commits (RefreshUI). Lives above the diagram
// Bar in the zone SOverlay (which RebuildZoneDiagram swaps into the slotted
// SBox in place).
class UFaceParallaxEditorWidget::SZoneBoundaryOverlay : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SZoneBoundaryOverlay) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) {}

    UFaceParallaxEditorWidget* Owner = nullptr;

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(400.0f, 20.0f);
    }

    // Boundary sides: -6..-1 mirror indices 0..5 on the negative angle side,
    // +1..+6 on the positive side; 0 means no hit. The six boundary angles
    // are the WI1 14-state swap set — {BM0*HZW/2, BM0*HZW, 1.5*BM0*HZW,
    // BM1*HZW, BM2*HZW, BM3*HZW} = 22.5/45/67.5/90/135/180 at defaults. The
    // Narrow (index 0) and Sliver (index 2) lines derive from BM0; the other
    // four are the primary multiplier handles.
    // Req 5: pixels come from FPLayout::FPZoneStripPixelForYaw, so the strip
    // is rebased to start at the Left profile (left edge = -135, camera-orbit
    // order Left -> 3/4L -> Front -> 3/4R -> Right -> BackR -> Back -> BackL,
    // right edge wrapping back to the left).
    static float BoundaryAngle(int32 Index, const TArray<float>& Mults, float HZW)
    {
        const float BM0 = UFaceParallaxComponent::GetBoundaryOrDefault(Mults, 0);
        const float BM1 = UFaceParallaxComponent::GetBoundaryOrDefault(Mults, 1);
        const float BM2 = UFaceParallaxComponent::GetBoundaryOrDefault(Mults, 2);
        const float BM3 = UFaceParallaxComponent::GetBoundaryOrDefault(Mults, 3);
        switch (Index)
        {
            case 0: return BM0 * HZW * 0.5f;
            case 1: return BM0 * HZW;
            case 2: return BM0 * HZW * 1.5f;
            case 3: return BM1 * HZW;
            case 4: return BM2 * HZW;
            default: return BM3 * HZW;
        }
    }
    static float BoundaryPixel(int32 Side, const TArray<float>& Mults,
        float HalfZoneWidth, float DiagramWidth)
    {
        const float Ang = BoundaryAngle(FMath::Abs(Side) - 1, Mults, HalfZoneWidth);
        const float Signed = Side < 0 ? -Ang : Ang;
        return (float)FPLayout::FPZoneStripPixelForYaw((double)Signed, (double)DiagramWidth);
    }

    // Side 1..6 -> ZoneBoundaryMultipliers index (Narrow/Sliver share BM0).
    static int32 BoundarySideToMultiplierIndex(int32 Side)
    {
        static const int32 SideToMult[6] = {0, 0, 0, 1, 2, 3};
        const int32 I = FMath::Abs(Side) - 1;
        return (I >= 0 && I < 6) ? SideToMult[I] : -1;
    }

    // Degrees of boundary travel per unit multiplier change for each side
    // (Narrow moves half as fast as BM0, Sliver 1.5x — both still drive BM0).
    static float BoundarySideDegPerMultiplier(int32 Side)
    {
        static const float DegPerMult[6] = {0.5f, 1.0f, 1.5f, 1.0f, 1.0f, 1.0f};
        const int32 I = FMath::Abs(Side) - 1;
        return (I >= 0 && I < 6) ? DegPerMult[I] : 1.0f;
    }

    int32 HitTest(const FGeometry& Geo, const FVector2D& Local) const
    {
        const float W = Geo.GetLocalSize().X;
        if (W <= 1.0f) return 0;
        UFaceParallaxComponent* Comp = Owner ? Owner->GetParallaxComponent() : nullptr;
        const TArray<float>& Mults = Comp ? Comp->ZoneBoundaryMultipliers : TArray<float>();
        const float HZW = Comp ? Comp->HalfZoneWidth : 45.0f;
        int32 BestSide = 0;
        float BestDist = 12.0f;
        for (int32 Side = -6; Side <= 6; ++Side)
        {
            if (Side == 0) continue;
            const float Dist = FMath::Abs(BoundaryPixel(Side, Mults, HZW, W) - Local.X);
            if (Dist < BestDist) { BestDist = Dist; BestSide = Side; }
        }
        return BestSide;
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
        const FSlateRect&, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle&, bool) const override
    {
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        if (!Brush) return LayerId;
        const FVector2D Sz = AllottedGeometry.GetLocalSize();
        if (Sz.X <= 1.0f) return LayerId;
        UFaceParallaxComponent* Comp = Owner ? Owner->GetParallaxComponent() : nullptr;
        const TArray<float>& Mults = Comp ? Comp->ZoneBoundaryMultipliers : TArray<float>();
        const float HZW = Comp ? Comp->HalfZoneWidth : 45.0f;
        for (int32 Side = -6; Side <= 6; ++Side)
        {
            if (Side == 0) continue;
            const float Px = BoundaryPixel(Side, Mults, HZW, Sz.X);
            if (Px < 0.0f || Px > Sz.X) continue;
            const bool bDrag = Side == DragSide;
            const float W = bDrag ? 3.0f : 1.0f;
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
                AllottedGeometry.ToPaintGeometry(FVector2D(W, Sz.Y),
                    FSlateLayoutTransform(FVector2D(Px - W * 0.5f, 0.0f))),
                Brush, ESlateDrawEffect::None,
                bDrag ? FLinearColor(1.0f, 0.85f, 0.35f, 0.95f)
                      : FLinearColor(0.04f, 0.04f, 0.07f, 0.85f));
        }
        const float CursorYaw = (Owner && Owner->IsZoneScrubbing())
            ? Owner->GetZoneScrubYaw()
            : (Owner ? Owner->GetOrbitYaw() : (Comp ? Comp->CurrentYaw : 0.0f));
        if (CursorYaw == CursorYaw)   // NaN guard
        {
            // Req 5: rebase the cursor onto the camera-orbit strip (left edge
            // = Left profile, -135) so it always sits on the diagram; the pure
            // FPZoneStripPixelForYaw contract wraps the back for us.
            const float YPx = (float)FPLayout::FPZoneStripPixelForYaw((double)CursorYaw, (double)Sz.X);
            if (YPx >= 0.0f && YPx <= Sz.X)
                FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
                    AllottedGeometry.ToPaintGeometry(FVector2D(3.0f, Sz.Y),
                        FSlateLayoutTransform(FVector2D(YPx - 1.5f, 0.0f))),
                    Brush, ESlateDrawEffect::None, FLinearColor(1.0f, 0.2f, 0.2f, 0.9f));
        }
        return LayerId + 2;
    }

    virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override
    {
        if (Ev.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
        const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
        const int32 Side = HitTest(Geo, Local);
        if (Side == 0)
        {
            // Phase 1: press in empty space = rotation scrub, not a boundary
            // edit. The drag is relative (no jump to the press pixel) and
            // drives the orbit + active view state live.
            DragSide = 0;
            DragStartPx = Local.X;
            bScrubDrag = true;
            if (Owner) Owner->BeginZoneScrub();
            return FReply::Handled().CaptureMouse(SharedThis(this));
        }
        DragSide = Side;
        DragStartPx = Local.X;
        UFaceParallaxComponent* Comp = Owner ? Owner->GetParallaxComponent() : nullptr;
        const TArray<float>& Mults = Comp ? Comp->ZoneBoundaryMultipliers : TArray<float>();
        const int32 MIdx = BoundarySideToMultiplierIndex(Side);
        DragStartMultiplier = UFaceParallaxComponent::GetBoundaryOrDefault(Mults, MIdx < 0 ? 0 : MIdx);
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev) override
    {
        const float W = Geo.GetLocalSize().X;
        if (W <= 1.0f) return FReply::Handled();
        if (bScrubDrag)
        {
            const float DeltaPx = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()).X - DragStartPx;
            if (Owner) Owner->ScrubZoneYawDelta(DeltaPx, W);
            return FReply::Handled();
        }
        if (DragSide == 0) return FReply::Unhandled();
        const float DeltaPx = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()).X - DragStartPx;
        const double DeltaDeg = (DragSide < 0 ? -1.0 : 1.0) * (double)DeltaPx * 360.0 / (double)W;
        UFaceParallaxComponent* Comp = Owner ? Owner->GetParallaxComponent() : nullptr;
        const float HZW = Comp ? Comp->HalfZoneWidth : 45.0f;
        // The drag applies to the boundary line's OWN angular travel: the
        // Narrow/Sliver lines move at 0.5x / 1.5x the BM0 handle rate (they
        // derive from BM0), so the multiplier delta is normalized by the
        // side's degrees-per-multiplier before writing the multiplier.
        const float EffHZW = HZW * BoundarySideDegPerMultiplier(DragSide);
        const float Mult = (float)FPLayout::ZoneBoundaryAfterDrag(
            DragStartMultiplier, DeltaDeg, EffHZW);
        const int32 MIdx = BoundarySideToMultiplierIndex(DragSide);
        if (Owner) Owner->ApplyZoneBoundaryDrag(MIdx, Mult);
        return FReply::Handled();
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
    {
        if (bScrubDrag)
        {
            bScrubDrag = false;
            if (Owner) Owner->CommitZoneScrub();
            return FReply::Handled().ReleaseMouseCapture();
        }
        if (DragSide == 0) return FReply::Unhandled();
        DragSide = 0;
        if (Owner) Owner->CommitZoneBoundaryDrag();
        return FReply::Handled().ReleaseMouseCapture();
    }

    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override
    {
        if (bScrubDrag)
        {
            bScrubDrag = false;
            if (Owner) Owner->CommitZoneScrub();
        }
        DragSide = 0;
    }

    virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Ev) const override
    {
        const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
        const int32 Side = HitTest(Geo, Local);
        return FCursorReply::Cursor(Side != 0
            ? EMouseCursor::ResizeLeftRight : EMouseCursor::Hand);
    }

private:
    int32 DragSide = 0;
    bool bScrubDrag = false;        // Phase 1: rotation scrub (empty-space drag)
    float DragStartPx = 0.0f;
    float DragStartMultiplier = 1.0f;
};

// SFacePitchStrip - Phase C: dedicated up/down view slider (the vertical pitch
// mirror of the yaw zone scrub). A slim vertical strip mounted LEFT of the
// canvas (the yaw zone bar sits above the main row); dragging up lifts the
// head (toward Top), dragging down lowers it (toward Bottom). Relative pixel
// drags map through the pure
// FPLayout::FPZoneScrubPitchAfterDrag contract (full strip height = 180 deg,
// clamped to [-90,90], no wrap), drive the orbit pitch live, and repoint the
// ActiveViewState at the Top/Bottom art states past their thresholds so the
// schematic + per-view art follow the up/down rotation. The strip paints a
// track with tick marks at every 30 deg, a Top/Bottom label, and a live cursor
// (red while scrubbing, grey at rest).
class UFaceParallaxEditorWidget::SFacePitchStrip : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SFacePitchStrip) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) {}

    UFaceParallaxEditorWidget* Owner = nullptr;

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D(14.0f, 108.0f);
    }

    // Map pitch (deg, [-90,90]) to a vertical pixel offset: +90 sits at the
    // very top of the strip, -90 at the very bottom.
    static float PitchToPixel(float Pitch, float HeightPx)
    {
        return (90.0f - FMath::Clamp(Pitch, -90.0f, 90.0f)) / 180.0f * HeightPx;
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
        const FSlateRect&, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle&, bool) const override
    {
        const FSlateBrush* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
        if (!Brush) return LayerId;
        const FVector2D Sz = AllottedGeometry.GetLocalSize();
        if (Sz.Y <= 1.0f) return LayerId;

        // Track.
        FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
            AllottedGeometry.ToPaintGeometry(FVector2D(3.0f, Sz.Y),
                FSlateLayoutTransform(FVector2D((Sz.X - 3.0f) * 0.5f, 0.0f))),
            Brush, ESlateDrawEffect::None, FLinearColor(0.04f, 0.04f, 0.07f, 0.85f));

        // Tick marks every 30 deg (Top/Bottom ends get a brighter cap).
        const FSlateFontInfo TickFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);
        for (int32 P = -90; P <= 90; P += 30)
        {
            const float Y = PitchToPixel((float)P, Sz.Y);
            const bool bEnd = (P == -90 || P == 90);
            const float TW = bEnd ? 8.0f : 6.0f;
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
                AllottedGeometry.ToPaintGeometry(FVector2D(TW, 1.0f),
                    FSlateLayoutTransform(FVector2D((Sz.X - TW) * 0.5f, Y))),
                Brush, ESlateDrawEffect::None,
                bEnd ? FLinearColor(0.6f, 0.85f, 0.6f, 0.95f)
                     : FLinearColor(0.35f, 0.35f, 0.45f, 0.9f));
            if (P == 90)
            {
                FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
                    AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(FVector2D(1.0f, 0.0f))),
                    FText::FromString(TEXT("T")), TickFont,
                    ESlateDrawEffect::None, FLinearColor(0.7f, 1.0f, 0.7f, 1.0f));
            }
            else if (P == -90)
            {
                FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
                    AllottedGeometry.ToPaintGeometry(FSlateLayoutTransform(
                        FVector2D(1.0f, Sz.Y - 9.0f))),
                    FText::FromString(TEXT("B")), TickFont,
                    ESlateDrawEffect::None, FLinearColor(0.7f, 1.0f, 0.7f, 1.0f));
            }
        }

        // Live cursor: the scrub pitch while dragging, else the orbit pitch.
        const float CursorPitch = (Owner && Owner->IsPitchScrubbing())
            ? Owner->GetPitchScrubPitch()
            : (Owner ? Owner->GetOrbitPitch() : 0.0f);
        if (CursorPitch == CursorPitch)   // NaN guard
        {
            const bool bScrubbing = Owner && Owner->IsPitchScrubbing();
            const float Y = PitchToPixel(CursorPitch, Sz.Y);
            FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
                AllottedGeometry.ToPaintGeometry(FVector2D(Sz.X, 3.0f),
                    FSlateLayoutTransform(FVector2D(0.0f, Y - 1.5f))),
                Brush, ESlateDrawEffect::None,
                bScrubbing ? FLinearColor(1.0f, 0.2f, 0.2f, 0.95f)
                           : FLinearColor(0.55f, 0.55f, 0.6f, 0.9f));
        }
        return LayerId + 3;
    }

    virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override
    {
        if (Ev.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
        const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
        DragStartPy = Local.Y;
        bScrubDrag = true;
        if (Owner) Owner->BeginPitchScrub();
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev) override
    {
        const float H = Geo.GetLocalSize().Y;
        if (H <= 1.0f) return FReply::Handled();
        if (bScrubDrag)
        {
            // Moving UP (negative screen delta) raises the pitch toward Top.
            const float DeltaPx = -(Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()).Y - DragStartPy);
            if (Owner) Owner->ScrubPitchDelta(DeltaPx, H);
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
    {
        if (bScrubDrag)
        {
            bScrubDrag = false;
            if (Owner) Owner->CommitPitchScrub();
            return FReply::Handled().ReleaseMouseCapture();
        }
        return FReply::Unhandled();
    }

    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override
    {
        if (bScrubDrag)
        {
            bScrubDrag = false;
            if (Owner) Owner->CommitPitchScrub();
        }
    }

    virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
    {
        return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
    }

private:
    bool bScrubDrag = false;        // Phase C: pitch scrub (vertical drag)
    float DragStartPy = 0.0f;
};
#endif
