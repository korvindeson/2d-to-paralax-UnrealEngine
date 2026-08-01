#pragma once

#include "CoreMinimal.h"
#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxPreset.h"
#include "FaceParallaxLayoutSpec.h"
#include <functional>

#if WITH_EDITOR
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateLayoutTransform.h"
#include "Editor.h"

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
                    [SNew(STextBlock)
                        .Text(FText::FromString(Title))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                        .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))]
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
    TArray<TSharedRef<SVerticalBox>> Bodies;
    TArray<FString> Titles;
    TArray<bool> Open;
};

// SFaceHotspotLayer - transparent canvas overlay (Phase 4): maps clicks to UV
// space and hit-tests FPLayout named polygon buckets (holes/concave supported).
// Lives below the gizmo in the preview SOverlay, so gizmo drags win; clicks the
// gizmo rejects fall through to hotspot hit-testing and call
// Owner->HandleHotspotClick(name). Faint outlines paint the region bounds.
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

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        return FVector2D::ZeroVector;
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
        const FSlateRect&, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle&, bool) const override
    {
        const FVector2D Size = AllottedGeometry.GetLocalSize();
        if (Size.X <= 0.0f || Size.Y <= 0.0f || Regions.empty()) return LayerId;
        const FLinearColor Tint(0.6f, 0.8f, 1.0f, 0.14f);
        for (const FPLayout::FPHotspotRegion& R : Regions)
        {
            DrawLoop(AllottedGeometry, OutDrawElements, LayerId, R.Outer, Size, Tint);
            for (const std::vector<FPLayout::FPHotspotPoint>& Hole : R.Holes)
                DrawLoop(AllottedGeometry, OutDrawElements, LayerId, Hole, Size, Tint);
        }
        return LayerId + 1;
    }

    virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override
    {
        if (!Owner || Ev.GetEffectingButton() != EKeys::LeftMouseButton)
            return FReply::Unhandled();
        const FVector2D CanvasSize = Geo.GetLocalSize();
        if (CanvasSize.X < 1.0f || CanvasSize.Y < 1.0f) return FReply::Unhandled();
        const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
        const FVector2D UV = UFaceParallaxEditorWidget::GizmoPixelsToUV(Local, CanvasSize);
        const char* Name = FPLayout::FPHotspotHit(Regions, UV.X, UV.Y);
        if (!Name || !Name[0]) return FReply::Unhandled();
        Owner->HandleHotspotClick(FString(Name));
        return FReply::Handled();
    }

private:
    void DrawLoop(const FGeometry& G, FSlateWindowElementList& L, int32 Id,
        const std::vector<FPLayout::FPHotspotPoint>& Loop,
        const FVector2D& Size, const FLinearColor& Tint) const
    {
        if (Loop.size() < 2) return;
        TArray<FVector2D> Pts;
        Pts.Reserve((int32)Loop.size() + 1);
        for (const FPLayout::FPHotspotPoint& P : Loop)
            Pts.Add(FVector2D((float)(P.X * Size.X), (float)(P.Y * Size.Y)));
        const FVector2D First = Pts[0];
        Pts.Add(First);
        FSlateDrawElement::MakeLines(L, Id, G.ToPaintGeometry(), Pts,
            ESlateDrawEffect::None, Tint, true, 1.0f);
    }

    std::vector<FPLayout::FPHotspotRegion> Regions;
};
#endif
