#pragma once

#include "CoreMinimal.h"
#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxPreset.h"
#include "FaceParallaxComponent.h"
#include "FaceParallaxLayoutSpec.h"
#include "FaceParallaxSchematic.h"
#include <functional>

#if WITH_EDITOR
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateLayoutTransform.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"
#include "Widgets/SCompoundWidget.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ContentBrowserDataDragDropOp.h"
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
// INTERACTIVITY (Phase 0): the gizmo is PAINT-ONLY. Its visibility is
// EVisibility::SelfHitTestInvisible, so it can never intercept a click;
// every canvas click is routed by SFaceHotspotLayer (the topmost
// interactive overlay), which handles pin-drag in pin mode.
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
        return LayerId + 1;
    }

    // Phase 0 click router: pin -> region -> glyph -> quad -> miss. Bodies of
    // the schematic-touching handlers live after SFaceSchematicLayer below
    // (the schematic must be complete before its methods are called).
    virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override;
    virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev) override;
    virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Ev) override;
    virtual void OnMouseLeave(const FPointerEvent& Ev) override;
    virtual FCursorReply OnCursorQuery(const FGeometry& Geo, const FPointerEvent& Ev) const override;

private:
    bool NearPin(const FVector2D& Local, const FVector2D& CanvasSize) const
    {
        if (!Owner || !bCanvasPinMode) return false;
        const FVector2D PinUV = Owner->GetSelectedPinUV();
        if (PinUV.X < 0.0f) return false;
        return FVector2D::Distance(Local,
            UFaceParallaxEditorWidget::GizmoUVToPixels(PinUV, CanvasSize)) < 12.0f;
    }

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
};

// SFaceSchematicLayer - the central-canvas DEFAULT VIEW (redesign): paints the
// part schematic glyphs (FaceParallaxSchematic.h) for every part — P1 one-map:
// the glyph layer is the SINGLE map (artful parts render solid instead of
// dashed — the live preview art replaces the outline visually, but the map
// stays clickable). Glyph color encodes the Phase I EDGE MAP by default:
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
    // Front albedo). Artful glyphs render SOLID (art replaces the outline on
    // the live preview) while artless ones stay dashed — one map, two states.
    void SetPartStatus(const std::vector<char>& InStatus)
    {
        PartStatus = InStatus;
    }

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
            const bool bHovered = (int32)i == HoveredIndex;
            const bool bSelected = !SelTag.IsEmpty()
                && (size_t)i < PartLayerTags.size() && PartLayerTags[i] == TCHAR_TO_UTF8(*SelTag);
            // P1 one-map: artful parts draw SOLID (their art replaced the
            // outline on the live preview); artless parts stay dashed.
            const bool bArt = (size_t)i < PartStatus.size() && PartStatus[i] != 0;
            FLinearColor Color = bEdgeMap
                ? EdgeMapColor(P) : DepthClassColor(P.DepthClass);
            if (bSelected)
            {
                Color.A = 1.0f;
                // Soft fill pass: a thick low-alpha halo behind the crisp
                // outline makes the selected layer read as "filled".
                DrawDashedLoop(AllottedGeometry, OutDrawElements, LayerId,
                    P.Outline, Size, FLinearColor(Color.R, Color.G, Color.B, 0.08f), 14.0f,
                    bArt);
            }
            else
            {
                // Edge-map mode paints at full alpha so the group coloring
                // actually reads; the legacy depth-class tint keeps its
                // subdued look.
                Color.A = bHovered ? 1.0f : (bEdgeMap ? 0.9f : 0.25f);
            }
            const float Thickness = bSelected ? 3.0f
                : (bHovered ? 2.5f : (bEdgeMap ? 2.0f : 1.5f));
            DrawDashedLoop(AllottedGeometry, OutDrawElements, LayerId,
                P.Outline, Size, Color, Thickness, bArt);
            // P1 click pulse: bright fading ring on the part just picked
            // (0.5s) — inline feedback at the point of action.
            if (FlashAge >= 0.0 && FlashAge < 0.5
                && Owner && Owner->GetSchematicFlashPart() == UTF8_TO_TCHAR(P.Name))
            {
                const float Alpha = 1.0f - (float)(FlashAge / 0.5);
                DrawDashedLoop(AllottedGeometry, OutDrawElements, LayerId + 1,
                    P.Outline, Size, FLinearColor(1.0f, 1.0f, 0.7f, Alpha), 4.0f, true);
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
    // drawn (closed loop — first point repeated at the end). P1: artful parts
    // pass bSolid=true to draw full edges instead (one map, solid = has art).
    // The focus lens is applied to every point here so hover/hit and paint
    // stay in sync.
    void DrawDashedLoop(const FGeometry& G, FSlateWindowElementList& L, int32 Id,
        const std::vector<FPSchematic::FPSchematicPoint>& Loop,
        const FVector2D& Size, const FLinearColor& Color, float Thickness,
        bool bSolid = false) const
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

    std::vector<FPSchematic::FPSchematicPart> Parts;
    std::vector<std::string> PartLayerTags;      // Phase 0/3: resolved tag per part (parallel)
    std::vector<char> PartStatus;                // P2: per-part slot completeness in the ACTIVE view (0 none, 1 partial, 2 full)
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

    // (1) Schematic glyph (lens- and filter-aware) — P1 one-map: the glyph
    // is the SINGLE canvas map. Left-click selects the resolved layer (and
    // opens the import wizard when it has no art); right-click opens the
    // remap menu. The old named-region and layer-art-quad click layers are
    // gone — no Alt/Ctrl modifier paths remain.
    if (Schematic.IsValid())
    {
        const FPSchematic::FPSchematicPart* Part = Schematic->HitTest(UV);
        if (Part && Part->Name && Part->Name[0])
        {
            const FString PartName(Part->Name);
            if (bRight)
                Owner->OpenHotspotRemapMenu(PartName, Ev);
            else
                Owner->HandleSchematicPartClick(PartName);
            return FReply::Handled();
        }
    }

    // (2) Miss: swallow. SOverlay does NOT re-route Unhandled to sibling
    // layers — without this the click would be dead (and the SImages below
    // would swallow it instead).
    return FReply::Handled();
}

inline FReply UFaceParallaxEditorWidget::SFaceHotspotLayer::OnMouseMove(
    const FGeometry& Geo, const FPointerEvent& Ev)
{
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
    if (PinDragMode == 0) return FReply::Unhandled();
    PinDragMode = 0;
    return FReply::Handled().ReleaseMouseCapture();
}

inline void UFaceParallaxEditorWidget::SFaceHotspotLayer::OnMouseLeave(const FPointerEvent&)
{
    if (Schematic.IsValid()) Schematic->ClearHover();
}

inline FCursorReply UFaceParallaxEditorWidget::SFaceHotspotLayer::OnCursorQuery(
    const FGeometry& Geo, const FPointerEvent& Ev) const
{
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

    // Boundary sides: -4..-1 mirror indices 0..3 on the negative angle side,
    // +1..+4 on the positive side; 0 means no hit.
    static float BoundaryPixel(int32 Side, const TArray<float>& Mults,
        float HalfZoneWidth, float DiagramWidth)
    {
        const float Ang = UFaceParallaxComponent::GetBoundaryOrDefault(Mults, FMath::Abs(Side) - 1)
            * HalfZoneWidth;
        const float Signed = Side < 0 ? -Ang : Ang;
        return (Signed + 180.0f) / 360.0f * DiagramWidth;
    }

    int32 HitTest(const FGeometry& Geo, const FVector2D& Local) const
    {
        const float W = Geo.GetLocalSize().X;
        if (W <= 1.0f) return 0;
        UFaceParallaxComponent* Comp = Owner ? Owner->GetParallaxComponent() : nullptr;
        const TArray<float>& Mults = Comp ? Comp->ZoneBoundaryMultipliers : TArray<float>();
        const float HZW = Comp ? Comp->HalfZoneWidth : 22.5f;
        int32 BestSide = 0;
        float BestDist = 12.0f;
        for (int32 Side = -4; Side <= 4; ++Side)
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
        const float HZW = Comp ? Comp->HalfZoneWidth : 22.5f;
        for (int32 Side = -4; Side <= 4; ++Side)
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
        if (Comp)
        {
            const float YPx = (Comp->CurrentYaw + 180.0f) / 360.0f * Sz.X;
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
        if (Side == 0) return FReply::Unhandled();
        DragSide = Side;
        DragStartPx = Local.X;
        UFaceParallaxComponent* Comp = Owner ? Owner->GetParallaxComponent() : nullptr;
        const TArray<float>& Mults = Comp ? Comp->ZoneBoundaryMultipliers : TArray<float>();
        DragStartMultiplier = UFaceParallaxComponent::GetBoundaryOrDefault(Mults, FMath::Abs(Side) - 1);
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev) override
    {
        if (DragSide == 0) return FReply::Unhandled();
        const float W = Geo.GetLocalSize().X;
        if (W <= 1.0f) return FReply::Handled();
        const float DeltaPx = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition()).X - DragStartPx;
        const double DeltaDeg = (DragSide < 0 ? -1.0 : 1.0) * (double)DeltaPx * 360.0 / (double)W;
        UFaceParallaxComponent* Comp = Owner ? Owner->GetParallaxComponent() : nullptr;
        const float HZW = Comp ? Comp->HalfZoneWidth : 22.5f;
        const float Mult = (float)FPLayout::ZoneBoundaryAfterDrag(
            DragStartMultiplier, DeltaDeg, HZW);
        if (Owner) Owner->ApplyZoneBoundaryDrag(FMath::Abs(DragSide) - 1, Mult);
        return FReply::Handled();
    }

    virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
    {
        if (DragSide == 0) return FReply::Unhandled();
        DragSide = 0;
        if (Owner) Owner->CommitZoneBoundaryDrag();
        return FReply::Handled().ReleaseMouseCapture();
    }

    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override
    {
        DragSide = 0;
    }

    virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
    {
        return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
    }

private:
    int32 DragSide = 0;
    float DragStartPx = 0.0f;
    float DragStartMultiplier = 1.0f;
};
#endif
