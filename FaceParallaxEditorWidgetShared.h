#pragma once

#include "CoreMinimal.h"
#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxPreset.h"
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
#endif
