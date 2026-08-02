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


void UFaceParallaxEditorWidget::RefreshUI()
{
    if (bIsRefreshing) return;
    bIsRefreshing = true;

    RefreshActorSelector();
    RefreshLayerList();
    RebuildPartsStrip();
    RefreshHotspotRegions();
    RefreshTextureThumbs();
    RefreshCanvasPreview();
    RefreshViewStripDots();
    RefreshSlotPropStatus();
    RefreshOnionSkin();
    if (GizmoWidget.IsValid())
    {
        // Pin mode: when the selected nested element is pinned — or the
        // slot's whole-layer pin (LayerPin3D, P3) is pinned — the gizmo
        // edits the pin (drag handle at its projected UV) instead of the
        // layer transform.
        FFaceNestedArt PinEl;
        int32 PinCount = 0;
        bool bPinMode = GetSelectedPinElement(PinEl, PinCount) && PinEl.Pin3D.bPinned;
        if (!bPinMode && ActivePreset && SelectedLayerName.IsValid())
            bPinMode = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName).LayerPin3D.bPinned;
        GizmoWidget->SetPinMode(bPinMode);
        GizmoWidget->Invalidate(EInvalidateWidgetReason::Paint);
    }
    RefreshTimeline();
    RefreshTransformSliders();
    RefreshConfigCheckboxes();
    RefreshDebugSliders();
    RefreshHullThumbnails();
    RefreshPinControls();
    RefreshSyncDriftIndicator();
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
    RefreshAssignGrid();
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
    // Display-mode dedupe (P3): the Debug rail's three toggles are the source
    // of truth; the canvas mode row is derived from them (custom combos and
    // all-off clear the row highlight via -1), mirroring DeriveDisplayMode.
    DisplayMode = FPLayout::DeriveDisplayMode(bLocalShowTextures, bLocalShowDepthMesh, bLocalShowWireframe);
    UpdateDisclosureSummaries();
}

void UFaceParallaxEditorWidget::RefreshLayerList()
{
    if (!LayerPanelBox.IsValid()) return;
    LayerPanelBox->ClearChildren();

    if (!SelectedLayerName.IsValid() && LayerNames.Num() > 0)
        SelectedLayerName = LayerNames[0];

    // P18: the layer list is a carousel - 8 rows per page, no vertical scroll.
    const int32 TotalPages = FPLayout::CarouselPageCount(LayerNames.Num());
    LayerPageIndex = FPLayout::ClampCarouselPage(LayerPageIndex, TotalPages);
    const int32 Start = LayerPageIndex * FPLayout::CarouselRowsPerPage;
    const int32 End = FMath::Min(Start + FPLayout::CarouselRowsPerPage, LayerNames.Num());

    UFaceParallaxComponent* Comp = GetParallaxComponent();
    for (int32 li = Start; li < End; ++li)
    {
        const FName& Tag = LayerNames[li];
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
    if (LayerPageLabel.IsValid())
        LayerPageLabel->SetText(FText::FromString(FString::Printf(TEXT("Page %d/%d"),
            LayerPageIndex + 1, TotalPages)));
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
    RefreshCanvasPreview();
}

void UFaceParallaxEditorWidget::RefreshCanvasPreview()
{
    if (!PreviewImageWidget.IsValid()) return;

    if (RenderTargetTexture)
    {
        PreviewBrush.SetResourceObject(RenderTargetTexture);
        PreviewBrush.ImageSize = FVector2D((float)RenderTargetTexture->SizeX, (float)RenderTargetTexture->SizeY);
        PreviewImageWidget->SetImage(&PreviewBrush);
        return;
    }

    // No actor / no render target: fall back to the selected layer's albedo so
    // the canvas is never a blank white field.
    UTexture2D* Albedo = nullptr;
    if (SelectedLayerName.IsValid())
    {
        Albedo = GetSlotAlbedo(ActiveViewState, SelectedLayerName);
    }
    if (Albedo)
    {
        PreviewBrush.SetResourceObject(Albedo);
        PreviewBrush.ImageSize = FVector2D((float)Albedo->GetSizeX(), (float)Albedo->GetSizeY());
        PreviewImageWidget->SetImage(&PreviewBrush);
    }
    else
    {
        PreviewBrush.SetResourceObject(nullptr);
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

// Phase 3: gather one marker per nested element of the selected layer so the
// canvas gizmo can paint every pin (pinned/rotation/jiggle/plain) at once.
// The effective pivot IS the projected pin UV for pinned elements.
void UFaceParallaxEditorWidget::GetLayerPinMarkers(TArray<FFacePinMarker>& Out)
{
    Out.Reset();
    if (!ActivePreset || !SelectedLayerName.IsValid()) return;
    const int32 Count = ActivePreset->GetNestedElementCount(ActiveViewState, SelectedLayerName);
    for (int32 i = 0; i < Count; ++i)
    {
        const FFaceNestedArt El = ActivePreset->GetNestedElement(ActiveViewState, SelectedLayerName, i);
        FFacePinMarker M;
        M.bPinned = El.Pin3D.bPinned;
        M.bRotation = El.Pin3D.bEnableViewAngleRotation;
        M.bJiggle = El.bJiggleEnabled;
        M.UV = GetNestedEffectivePivot(ActiveViewState, SelectedLayerName, i);
        Out.Add(M);
    }
    // P3: whole-layer pin — one white marker at the LayerPin3D's projection
    // for the active view state (ProjectPinToUVForState mirrors
    // FPLayout::PinProjectToUV). Draggable even with no nested elements.
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    const FFaceArtSlot SlotRec = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
    if (Comp && SlotRec.LayerPin3D.bPinned)
    {
        FFacePinMarker M;
        M.bPinned = true;
        M.bRotation = SlotRec.LayerPin3D.bEnableViewAngleRotation;
        M.bLayerPin = true;
        M.UV = Comp->ProjectPinToUVForState(SlotRec.LayerPin3D.Position3D, ActiveViewState);
        Out.Add(M);
    }
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
    if (!bOutlineDepthArmed)
    {
        bOutlineDepthArmed = true;
        const int32 TargetCount = OutlineDepthScope == 0 ? 1 : (OutlineDepthScope == 1 ? 8 : 10);
        SetStatus(FString::Printf(
            TEXT("ARMED — click Generate Depth from Outlines again to confirm (overwrites the depth channel of %d view state(s))"),
            TargetCount), FLinearColor::Yellow);
        return false;
    }
    return GenerateDepthFromOutlinesImpl(GridSize);
}

void UFaceParallaxEditorWidget::SetOutlineDepthScope(int32 Scope)
{
    OutlineDepthScope = FMath::Clamp(Scope, 0, 2);
    bOutlineDepthArmed = false;
}

int32 UFaceParallaxEditorWidget::GetOutlineDepthScope() const
{
    return OutlineDepthScope;
}

bool UFaceParallaxEditorWidget::GetOutlineDepthArmed() const
{
    return bOutlineDepthArmed;
}

bool UFaceParallaxEditorWidget::GenerateDepthFromOutlinesImpl(int32 GridSize)
{
    bOutlineDepthArmed = false;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp)
    {
        SetStatus(TEXT("Generate depth: no parallax component"), FLinearColor::Red);
        return false;
    }

    // Front map drives the preview; it must exist regardless of scope.
    TArray<float> FrontDepth;
    float CellSize = 0.0f;
    if (!Comp->GenerateDepthBufferFromOutlines(GridSize, FrontDepth, CellSize))
    {
        SetStatus(TEXT("No outline art found — assign a Front view with an albedo first"),
            FLinearColor::Yellow);
        return false;
    }
    BuildOutlineDepthTexture(FrontDepth, GridSize, true);

    // Scope: 0 = front only, 1 = 8 horizontal states, 2 = all 10 states.
    TArray<EFaceAngleState> Targets;
    if (OutlineDepthScope == 0)
    {
        Targets.Add(EFaceAngleState::Front);
    }
    else
    {
        const int32 Count = OutlineDepthScope == 1 ? 8 : 10;
        for (int32 i = 0; i < Count; ++i)
        {
            Targets.Add((EFaceAngleState)i);
        }
    }

    // Bake a view-consistent depth map into every target state's slots:
    // each target gets its own hull computed in that view's camera frame
    // instead of a verbatim copy of the front map.
    FWidgetUndoScope UndoScope(this, TEXT("Generate Depth From Outlines"));
    int32 LayersUpdated = 0;
    if (ValidatePreset())
    {
        for (EFaceAngleState State : Targets)
        {
            const FFaceViewStateLayerSet* StateArt = ActivePreset->ViewAssignments.Find(State);
            if (!StateArt || StateArt->Layers.Num() == 0) continue;
            TArray<float> StateDepth;
            float StateCell = 0.0f;
            if (!Comp->GenerateDepthBufferFromOutlinesForView(GridSize,
                UFaceParallaxComponent::VisualHullYawForState(State),
                UFaceParallaxComponent::VisualHullPitchForState(State),
                StateDepth, StateCell))
            {
                SetStatus(FString::Printf(TEXT("Generate depth: hull failed for state %d"), (int32)State),
                    FLinearColor::Red);
                return false;
            }
            UTexture2D* DepthTex = BuildOutlineDepthTexture(StateDepth, GridSize, false);
            if (!DepthTex) continue;
            for (const auto& LayerPair : StateArt->Layers)
            {
                FFaceTextureSet Textures = ActivePreset->GetTexturesForSlot(State, LayerPair.Key);
                Textures.Depth = DepthTex;
                ActivePreset->SetTexturesForSlot(State, LayerPair.Key, Textures);
                ++LayersUpdated;
            }
        }
        if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
        {
            PreviewActor->FaceParallax->ApplyCurrentStateTextures();
        }
    }

    SetStatus(FString::Printf(TEXT("Depth map %dx%d generated from silhouettes (%d layer(s) updated across %d view state(s))"),
        GridSize, GridSize, LayersUpdated, Targets.Num()), FLinearColor(0.3f, 1.0f, 0.3f));
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

UTexture2D* UFaceParallaxEditorWidget::BuildOutlineDepthTexture(const TArray<float>& Depth,
    int32 GridSize, bool bUpdatePreview)
{
    if (Depth.Num() != GridSize * GridSize) return nullptr;

    UTexture2D* Tex = UTexture2D::CreateTransient(GridSize, GridSize, PF_B8G8R8A8);
    if (!Tex) return nullptr;
    Tex->Source.Init(GridSize, GridSize, 1, 1, TSF_BGRA8);
    uint8* Pixels = (uint8*)Tex->Source.LockMip(0);
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
    Tex->Source.UnlockMip(0);
    Tex->UpdateResource();

    OutlineDepthTextures.Add(Tex);
    if (bUpdatePreview)
    {
        OutlineDepthTexture = Tex;
        OutlineDepthBrush.SetResourceObject(Tex);
        OutlineDepthBrush.ImageSize = FVector2D(128.0f, 128.0f);
        OutlineDepthBrush.DrawAs = ESlateBrushDrawType::Image;

        if (OutlinePreviewImage.IsValid())
        {
            OutlinePreviewImage->SetImage(&OutlineDepthBrush);
        }
        if (TextOutlineStats.IsValid())
        {
            int32 NonZero = 0;
            for (float V : Depth) if (V > 0.01f) ++NonZero;
            TextOutlineStats->SetText(FText::FromString(FString::Printf(
                TEXT("%dx%d depth buffer — silhouette edge visual hull (%d active cells)"),
                GridSize, GridSize, NonZero)));
        }
    }
    return Tex;
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
    for (const FZoneSeg& Seg : Segs)
    {
        float Frac = (Seg.End - Seg.Start) / TotalAngle;
        FLinearColor C = Seg.Color;

        // Blend window tint
        if (BlendW > 0.0f)
        {
            float SegLen = Seg.End - Seg.Start;
            float BlendFrac = BlendW / SegLen;
            if (BlendFrac > 0.1f)
                C = C * 0.7f + FLinearColor(0.5f, 0.5f, 0.3f) * 0.3f;
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
    }

    // Compose the diagram: color bar below, drag layer above (P3). The
    // overlay paints the 8 boundary lines + yaw cursor itself. The content is
    // swapped INTO the slotted SBox in place so the visible tree always shows
    // the current bar (never an orphaned rebuild).
    TSharedRef<SWidget> Diagram = Bar;
    if (ZoneDragOverlay.IsValid())
    {
        Diagram = SNew(SOverlay)
            + SOverlay::Slot()[Bar]
            + SOverlay::Slot()[ZoneDragOverlay.ToSharedRef()];
    }
    ZoneDiagramWidget->SetContent(Diagram);
}

// Phase P3: live zone-boundary multiplier write from the diagram drag layer.
// Mirrors the Camera rail text-edit commit path (clamp 0.5..20, defaults for
// missing entries) so both editors stay consistent; the matching Camera rail
// text editor (if built) and the diagram update live during the drag.
void UFaceParallaxEditorWidget::ApplyZoneBoundaryDrag(int32 Idx, float Multiplier)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || Idx < 0 || Idx > 3) return;
    const float V = FMath::Clamp(Multiplier, 0.5f, 20.0f);
    if (!Comp->ZoneBoundaryMultipliers.IsValidIndex(Idx))
    {
        Comp->ZoneBoundaryMultipliers.SetNum(4);
        static const float DefaultZoneMults[4] = {1.0f, 3.0f, 5.0f, 7.0f};
        for (int32 i = 0; i < 4; ++i)
            if (Comp->ZoneBoundaryMultipliers[i] == 0.0f)
                Comp->ZoneBoundaryMultipliers[i] = DefaultZoneMults[i];
    }
    Comp->ZoneBoundaryMultipliers[Idx] = V;
    if (ZoneEditBoxes.IsValidIndex(Idx) && ZoneEditBoxes[Idx].IsValid())
        ZoneEditBoxes[Idx]->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), V)));
    RebuildZoneDiagram();
}

void UFaceParallaxEditorWidget::CommitZoneBoundaryDrag()
{
    RefreshUI();
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
            {
                FFaceLayerDef Def = Comp->GetLayerDefinition(i);
                if (IsSeedPlaceholderLayerDef(Def)) continue;
                Tags.Add(Def.LayerTag);
            }
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
            {
                FFaceLayerDef Def = Comp->GetLayerDefinition(i);
                if (IsSeedPlaceholderLayerDef(Def)) continue;
                AllTags.Add(Def.LayerTag);
            }
        }
        if (AllTags.Num() == 0) AllTags = LayerNames;
    }

    TArray<FString> Lines;
    for (const FName& Tag : AllTags)
    {
        if (!ActivePreset->HasSlot(ActiveViewState, Tag)) continue;
        FFaceArtTransform T = GetEffectiveLayerTransform(ActiveViewState, Tag);
        FString Line = FString::Printf(TEXT("%s: P(%.2f,%.2f) S(%.2f,%.2f) R(%.1f)"),
            *Tag.ToString(),
            T.Position.X, T.Position.Y,
            T.Scale.X, T.Scale.Y,
            T.Rotation);
        // Check for nested elements
        int32 NE = ActivePreset->GetNestedElementCount(ActiveViewState, Tag);
        if (NE > 0)
        {
            Line += TEXT(" [");
            for (int32 ni = 0; ni < NE; ++ni)
            {
                FFaceNestedArt N = ActivePreset->GetNestedElement(ActiveViewState, Tag, ni);
                if (ni > 0) Line += TEXT(", ");
                Line += FString::Printf(TEXT("%s P(%.2f,%.2f)"),
                    *N.ElementName.ToString(),
                    N.RelativeTransform.Position.X, N.RelativeTransform.Position.Y);
            }
            Line += TEXT("]");
        }
        Lines.Add(Line);
    }

    // P17/P18: the layer lines flip through carousel pages (8 per page)
    // inside the fixed viewport - no vertical scroll bar (P19 reserve).
    const int32 TotalPages = FPLayout::CarouselPageCount(Lines.Num());
    CrossLayerPageIndex = FPLayout::ClampCarouselPage(CrossLayerPageIndex, TotalPages);
    const int32 Start = CrossLayerPageIndex * FPLayout::CarouselRowsPerPage;
    const int32 End = FMath::Min(Start + FPLayout::CarouselRowsPerPage, Lines.Num());
    FString Output;
    for (int32 i = Start; i < End; ++i)
    {
        Output += Lines[i];
        if (i < End - 1) Output += TEXT("\n");
    }

    if (Output.IsEmpty())
        Output = TEXT("No layers assigned for this state.");

    TextCrossLayer->SetText(FText::FromString(Output));
    CrossLayerBox->AddSlot().AutoHeight()[TextCrossLayer.ToSharedRef()];
    if (CrossLayerPageLabel.IsValid())
        CrossLayerPageLabel->SetText(FText::FromString(FString::Printf(TEXT("Page %d/%d"),
            CrossLayerPageIndex + 1, TotalPages)));
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
        {
            FFaceLayerDef Def = Comp->GetLayerDefinition(i);
            if (IsSeedPlaceholderLayerDef(Def)) continue;
            DefTags.Add(Def.LayerTag);
        }
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

    // Accordion panels manage their own section visibility: search hits
    // expand matching sections instead of toggling visibility.
    auto ExpandAccordion = [&](TSharedPtr<SFaceAccordion> Acc)
    {
        if (!Acc.IsValid()) return;
        if (Filter.IsEmpty())
        {
            Acc->SetExpanded(0, true);
            return;
        }
        bool bHit = false;
        for (int32 i = 0; i < Acc->NumSections(); ++i)
        {
            if (Acc->SectionTitle(i).Contains(Filter, ESearchCase::IgnoreCase))
            {
                Acc->SetExpanded(i, true);
                bHit = true;
            }
        }
        if (!bHit && MatchingLayers.Num() > 0) Acc->ExpandAll();
    };
    ExpandAccordion(DebugAccordion);
    ExpandAccordion(AdvancedAccordion);

    for (int32 Ri = 0; Ri < PropTabContent.Num(); ++Ri)
    {
        // Accordion-managed panels are expanded above; plain sections still
        // use the visibility-toggle contract. Rail 0 (Layers) hosts the
        // carousel page viewport - never visibility-toggled.
        if (Ri == 0 || Ri == 4 || Ri == 5) continue;
        ApplyToChildren(PropTabContent[Ri]);
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
            {
                FFaceLayerDef Def = Comp->GetLayerDefinition(i);
                if (IsSeedPlaceholderLayerDef(Def)) continue;
                AllTags.Add(Def.LayerTag);
            }
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
            {
                FFaceLayerDef Def = Comp->GetLayerDefinition(i);
                if (IsSeedPlaceholderLayerDef(Def)) continue;
                AllTags.Add(Def.LayerTag);
            }
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
// BACKUP POINT (single-slot manual safety copy; the real multi-step undo
// lives in UFaceParallaxEditorWidget::Undo/Redo over the undo stack)
// ====================================================================

void UFaceParallaxEditorWidget::SnapshotPreset()
{
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Save Backup Point"));
    if (!ActivePreset) return;
    // Duplicate the preset into a temporary package
    UPackage* TempPkg = CreatePackage(TEXT("/Temp/FaceParallaxSnapshot"));
    TempPkg->SetFlags(RF_Transient);
    SnapshotPresetBackup = DuplicateObject<UFaceParallaxPreset>(ActivePreset, TempPkg, TEXT("SnapshotBackup"));
    if (TextStatus.IsValid())
        TextStatus->SetText(FText::FromString(TEXT("Backup point saved.")));
}

void UFaceParallaxEditorWidget::RestoreSnapshot()
{
    if (!SnapshotPresetBackup || !ActivePreset) return;
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Restore Backup Point"));
    bIsRestoringUndo = true;
    RestoreFromBackup(SnapshotPresetBackup, TEXT("Restore Backup Point"));
    bIsRestoringUndo = false;
    RefreshUI();
    if (TextStatus.IsValid())
        TextStatus->SetText(FText::FromString(TEXT("Backup point restored.")));
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
    UndoStack.Empty();
    RedoStack.Empty();
    SnapshotPresetBackup = nullptr;
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



// ====================================================================
// REBUILD PANEL BUILDERS - RebuildWidget's per-panel construction.
// Called from UFaceParallaxEditorWidget::RebuildWidget (UI.cpp) in the
// order below. Each builder reproduces exactly one block of the former
// monolithic RebuildWidget, so the widget tree - and with it the Phase H
// design contract (FaceParallaxLayoutSpec.h + TestPhaseHUIDesign) - is
// byte-for-byte unchanged.
// ====================================================================

TSharedRef<SWidget> UFaceParallaxEditorWidget::MakeSectionBox(const FString& Title, TSharedRef<SWidget> Content)
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
}

TSharedRef<SVerticalBox> UFaceParallaxEditorWidget::BuildPanelCanvas(const TSharedRef<SVerticalBox>& Root)
{
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
                        .ButtonColorAndOpacity_Lambda([this, M]()
                        {
                            return DisplayMode == M ? AccentBlue() : FLinearColor(0.13f,0.13f,0.15f);
                        })
                        .OnClicked_Lambda([this, M](){ SetDisplayMode(M); return FReply::Handled(); })
                        .Content()
                        [SNew(STextBlock)
                            .Text(FText::FromString(Mo.T))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            .ColorAndOpacity(FLinearColor(0.85f,0.85f,0.85f))]];
            }
            ModeRow->AddSlot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)
                [MakeLbl(TEXT("Display"), 8, FLinearColor(0.6f,0.6f,0.6f))];
            // Onion-skin control lives next to Display Mode: checkbox + flex
            // slider + opacity label (kept compact for the center width budget).
            {
                TSharedRef<SHorizontalBox> OnionRow = SNew(SHorizontalBox);
                TSharedRef<SCheckBox> OnionCheck = SNew(SCheckBox)
                    .IsChecked(bOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { ToggleOnionSkin(S == ECheckBoxState::Checked); RefreshUI(); });
                OnionCheck->SetToolTipText(FText::FromString(TEXT("Onion skin: ghosts the adjacent view's albedo at low opacity for alignment")));
                OnionCheckBox = OnionCheck;
                OnionRow->AddSlot().Padding(FMargin(0,2)).AutoWidth().VAlign(VAlign_Center)[OnionCheck];
                OnionRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f).VAlign(VAlign_Center)
                    [SNew(SSlider).Value(OnionSkinOpacity)
                        .OnValueChanged_Lambda([this](float V){ SetOnionSkinOpacity(V); })];
                OnionRow->AddSlot().Padding(FMargin(2,2)).AutoWidth().VAlign(VAlign_Center)
                    [MakeLbl(TEXT("opacity"), 8, FLinearColor(0.6f,0.6f,0.6f))];
                ModeRow->AddSlot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)[OnionRow];
            }
            // Phase 3: Show Pins — paints all nested-element pin markers on the
            // gizmo (amber static / cyan rotation / purple jiggle / red plain).
            {
                TSharedRef<SCheckBox> ShowPinsCheck = SNew(SCheckBox)
                    .IsChecked(bShowPins ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    {
                        bShowPins = (S == ECheckBoxState::Checked);
                        RefreshUI();
                    });
                ShowPinsCheck->SetToolTipText(FText::FromString(TEXT(
                    "Show Pins: paint all nested-element markers (amber static pin / cyan rotation pin / purple jiggle / red plain anchor)")));
                CheckShowPins = ShowPinsCheck;
                ModeRow->AddSlot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)
                    [ShowPinsCheck];
                ModeRow->AddSlot().Padding(FMargin(0,2)).AutoWidth().VAlign(VAlign_Center)
                    [MakeLbl(TEXT("Pins"), 8, FLinearColor(0.6f,0.6f,0.6f))];
            }
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
        HotspotLayer = SNew(SFaceHotspotLayer);
        HotspotLayer->Owner = this;
        HotspotLayer->SetRegions(FPLayout::DefaultHotspotRegions());
        PreviewHost = SNew(SBox).HeightOverride(450)
            [SNew(SOverlay)
                + SOverlay::Slot()[PreviewImageWidget.ToSharedRef()]
                + SOverlay::Slot()[OutlinePreviewImage.ToSharedRef()]
                + SOverlay::Slot()[OnionSkinImage.ToSharedRef()]
                + SOverlay::Slot()[EdgeOverlayImage.ToSharedRef()]
                + SOverlay::Slot()[HotspotLayer.ToSharedRef()]
                + SOverlay::Slot()[GizmoLayer.ToSharedRef()]];
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(2))
            [PreviewHost.ToSharedRef()];

        // Parts strip: the 13 anatomical part chips under the canvas (Phase 1).
        // Left-click selects the mapped layer; Alt+click imports art for that
        // part; right-click remaps the region to a layer via a context menu.
        // Trailing button: Cycle Preview (Phase 2) — blink/expression/viseme/
        // orbit sweep tour of the live animation systems.
        PartsStrip = SNew(SWrapBox).UseAllottedSize(true);
        RebuildPartsStrip();
        TSharedRef<SHorizontalBox> StripRow = SNew(SHorizontalBox);
        StripRow->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
            [PartsStrip.ToSharedRef()];
        StripRow->AddSlot().AutoWidth().Padding(FMargin(4, 0, 0, 0)).VAlign(VAlign_Center)
            [SNew(SButton)
                .ButtonColorAndOpacity_Lambda([this]()
                {
                    return bCyclePreviewActive ? AccentBlue() : FLinearColor(0.13f, 0.13f, 0.15f);
                })
                .OnClicked_Lambda([this]()
                {
                    if (bCyclePreviewActive) StopCyclePreview(); else StartCyclePreview();
                    return FReply::Handled();
                })
                .ToolTipText(FText::FromString(TEXT("Cycle Preview: blink 2s -> expression 2s -> viseme 2s -> orbit sweep 2s")))
                [SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return FText::FromString(bCyclePreviewActive ? TEXT("Stop Cycle") : TEXT("Cycle Preview"));
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))]];
        StripRow->AddSlot().AutoWidth().Padding(FMargin(4, 0, 0, 0)).VAlign(VAlign_Center)
            [SNew(SButton)
                .ButtonColorAndOpacity_Lambda([this]()
                {
                    return bLivePreviewActive ? AccentBlue() : FLinearColor(0.13f, 0.13f, 0.15f);
                })
                .OnClicked_Lambda([this]()
                {
                    if (bLivePreviewActive) StopLivePreview(); else StartLivePreview();
                    return FReply::Handled();
                })
                .ToolTipText(FText::FromString(TEXT(
                    "Live Preview: blink + expression + viseme + orbit running TOGETHER "
                    "(assembled result check; Cycle Preview runs them one at a time)")))
                [SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return FText::FromString(bLivePreviewActive ? TEXT("Stop Live") : TEXT("Live Preview"));
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))]];
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(2, 0, 2, 2))
            [SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.09f))
                .Padding(FMargin(2, 1))
                [StripRow]];

        // Layer label + view info
        TextLayerName = SNew(STextBlock)
            .Text(FText::FromString(SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : TEXT("(no layer)")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            .ColorAndOpacity(FLinearColor(0.9f,0.9f,0.9f));
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(4,2,4,0))
            [TextLayerName.ToSharedRef()];
    }

    return CenterCol;
}

// Phase 2: recompute the canvas hotspot outlines for the current view.
// Each region whose hotspot maps to a layer is transformed by that layer's
// effective transform (master-material UV chain mirror); unmapped regions
// keep the default front-facing template pose.
void UFaceParallaxEditorWidget::RefreshHotspotRegions()
{
    if (!HotspotLayer.IsValid()) return;
    std::vector<FPLayout::FPHotspotRegion> Regions = FPLayout::DefaultHotspotRegions();
    if (ActivePreset)
    {
        for (FPLayout::FPHotspotRegion& R : Regions)
        {
            if (!R.Name || !R.Name[0]) continue;
            const FString RegionName = UTF8_TO_TCHAR(R.Name);
            const FName LayerTag = ResolveHotspotLayer(RegionName);
            if (!LayerTag.IsValid()) continue;
            const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(ActiveViewState, LayerTag);
            const FFaceArtTransform T = ArtSlot.GetEffectiveTransform(ActiveViewState);
            R = FPLayout::FPHotspotTransformRegion(R, T.Position.X, T.Position.Y,
                T.Scale.X, T.Scale.Y, T.Rotation);
        }
    }
    HotspotLayer->SetRegions(Regions);
}

// Rebuild the 13 anatomical part chips (Phase 1). Chip color reflects the
// mapped layer (stable per-layer hue from GetTypeHash); unmapped parts are
// dark gray. Left-click selects the mapped layer, Alt+click imports art for
// that part, right-click opens the remap menu. Called on build and on every
// RefreshUI so explicit HotspotLayerMap edits repaint immediately.
void UFaceParallaxEditorWidget::RebuildPartsStrip()
{
    if (!PartsStrip.IsValid()) return;
    PartsStrip->ClearChildren();
    static const FLinearColor UnmappedChip(0.15f, 0.15f, 0.17f);
    const auto PartsChipColor = [](FName Layer, bool bMapped) -> FLinearColor
    {
        if (!bMapped) return UnmappedChip;
        const float Hue = (float)(GetTypeHash(Layer) % 360u) / 360.0f;
        return FLinearColor(Hue, 0.5f, 0.35f).HSVToLinearRGB();
    };
    for (const FPLayout::FPHotspotRegion& R : FPLayout::DefaultHotspotRegions())
    {
        if (!R.Name || !R.Name[0]) continue;
        const FString RegionName = UTF8_TO_TCHAR(R.Name);
        const FName LayerTag = ResolveHotspotLayer(RegionName);
        const bool bMapped = LayerTag.IsValid();
        const FString Tip = FString::Printf(TEXT("%s -> %s\nLeft: select + import art   Right-click: map to layer"),
            *RegionName, bMapped ? *LayerTag.ToString() : TEXT("unmapped"));
        PartsStrip->AddSlot().Padding(FMargin(1))
            [SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(PartsChipColor(LayerTag, bMapped))
                .Padding(FMargin(6, 3))
                .OnMouseButtonDown_Lambda([this, RegionName](const FGeometry&, const FPointerEvent& E) -> FReply
                {
                    if (E.GetEffectingButton() == EKeys::RightMouseButton)
                    {
                        OpenHotspotRemapMenu(RegionName, E);
                        return FReply::Handled();
                    }
                    if (E.IsAltDown())
                    {
                        ImportHotspotRegion(RegionName);
                        return FReply::Handled();
                    }
                    HandleHotspotClick(RegionName);
                    return FReply::Handled();
                })
                .ToolTipText(FText::FromString(Tip))
                [SNew(STextBlock)
                    .Text(FText::FromString(RegionName))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))]];
    }
}

void UFaceParallaxEditorWidget::BuildPanelSlotProps(const TSharedRef<SVerticalBox>& Root, TSharedRef<SVerticalBox>& PropPanelOut)
{
    // --- 3c. SLOT PROPERTIES (right pane) ---
    TSharedRef<SVerticalBox> PropPanel = SNew(SVerticalBox).Visibility(EVisibility::Visible);
    PropsPages.Reset();

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

    }

        // ============ RIGHT PANE: TRANSFORM / OVERRIDE / SYNC ============
        {
            // P17/P18: the four sections are carousel pages - one visible at
            // a time inside the fixed page viewport, flipped by the nav strip.

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
                PropsPages.Add(MakeSectionBox(TEXT("Transform"), XForm));
            }

            // View override + sync-to-views + alignment, packed into one
            // carousel page (P20 whitespace review: the three sections fit a
            // single page viewport, so they must not stay separate tabs).
            {
                TSharedRef<SVerticalBox> SaBox = SNew(SVerticalBox);
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
                SaBox->AddSlot().AutoHeight()[OvRow];

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
                SaBox->AddSlot().AutoHeight()[SyncRow];

                TSharedRef<SHorizontalBox> BothRow = SNew(SHorizontalBox);
                BothRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeLbl(TEXT("Destinations picked on the state strip"), 8, FLinearColor(0.6f,0.6f,0.6f))];
                SaBox->AddSlot().AutoHeight()[BothRow];

                TSharedRef<SHorizontalBox> LinkRow = SNew(SHorizontalBox);
                TSharedRef<SCheckBox> LinkCheck = SNew(SCheckBox)
                    .IsChecked(bLinkAcrossViews ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { bLinkAcrossViews = (S == ECheckBoxState::Checked); });
                LinkCheck->SetToolTipText(FText::FromString(TEXT("Edits in this state are broadcast to all other states (Phase B)")));
                LinkRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[LinkCheck];
                LinkRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeLbl(TEXT("Link transform across views"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                SaBox->AddSlot().AutoHeight()[LinkRow];
                PropsPages.Add(MakeSectionBox(TEXT("Sync + Align"), SaBox));
            }

        }

        // P17/P18: fixed page viewport (right inset keeps the old scrollbar
        // gap - P15) + prev/page/next strip; the bottom reserve (P19) keeps
        // the last row clear of the nav and the status line under it.
        PropsPageBox = SNew(SVerticalBox);
        SlotPropsBox->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, FPLayout::ScrollReserveBottom))
            [SNew(SBox)
                .HeightOverride(FPLayout::CarouselViewportH)
                .Padding(FMargin(0, 0, FPLayout::PropsScrollInsetR, 0))
                [PropsPageBox.ToSharedRef()]];
        TSharedRef<SFaceCarouselNav> PropsNav = SNew(SFaceCarouselNav)
            .OnPrev_Lambda([this]()
            {
                ShowPropsPage(PropsPageIndex - 1);
                return FReply::Handled();
            })
            .OnNext_Lambda([this]()
            {
                ShowPropsPage(PropsPageIndex + 1);
                return FReply::Handled();
            });
        PropsPageLabel = PropsNav->Label;
        SlotPropsBox->AddSlot().AutoHeight().Padding(FMargin(4, 0, 4, 2))[PropsNav];
        ShowPropsPage(0);
        PropPanel->AddSlot().AutoHeight()
            [SlotPropsBox.ToSharedRef()];
        TextStatus = MakeLbl(TEXT("Ready"), 9, FLinearColor(0.5f,0.8f,0.5f));
        PropPanel->AddSlot().AutoHeight().Padding(FMargin(6,2))
            [TextStatus.ToSharedRef()];

    PropPanelOut = PropPanel;
}

// P18: flips the right-pane carousel to the given page (clamped).
void UFaceParallaxEditorWidget::ShowPropsPage(int32 Page)
{
    PropsPageIndex = FPLayout::ClampCarouselPage(Page, PropsPages.Num());
    if (PropsPageBox.IsValid())
    {
        PropsPageBox->ClearChildren();
        if (PropsPages.IsValidIndex(PropsPageIndex))
            PropsPageBox->AddSlot().AutoHeight()[PropsPages[PropsPageIndex]];
    }
    if (PropsPageLabel.IsValid())
        PropsPageLabel->SetText(FText::FromString(FString::Printf(TEXT("Page %d/%d"),
            PropsPageIndex + 1, PropsPages.Num())));
}

void UFaceParallaxEditorWidget::BuildPanelToolbar(const TSharedRef<SVerticalBox>& Root)
{
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
            [MakeBtn(TEXT("Undo"), [this](){ Undo(); }, FLinearColor(0.9f,0.85f,0.7f))];
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [MakeBtn(TEXT("Redo"), [this](){ Redo(); }, FLinearColor(0.9f,0.85f,0.7f))];
        TB->AddSlot().Padding(FMargin(8,2)).AutoWidth()
            [MakeBtn(TEXT("Import Art..."), [this]()
            {
                OpenImportArtDialog();
            })];
        SearchBox = SNew(SSearchBox)
            .HintText(FText::FromString(TEXT("Search settings (Enter = jump)")))
            .OnTextChanged_Lambda([this](const FText& T) { ApplySearchFilter(T.ToString()); })
            .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type C)
            {
                if (C == ETextCommit::OnEnter)
                    OnRailSearchCommitted(T.ToString());
            });
        TB->AddSlot().Padding(FMargin(6,2)).AutoWidth()
            [SNew(SBox).WidthOverride(140)[SearchBox.ToSharedRef()]];
        TB->AddSlot().FillWidth(1.0f);
        TB->AddSlot().Padding(FMargin(8,2)).AutoWidth()
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
        TB->AddSlot().Padding(FMargin(8,2)).AutoWidth()
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
        TB->AddSlot().Padding(FMargin(8,2)).AutoWidth()
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

}

void UFaceParallaxEditorWidget::BuildPanelStateStrip(const TSharedRef<SVerticalBox>& Root)
{
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
        SyncViewCheckBoxes.Reset();
        for (int32 Si = 0; Si < 10; ++Si)
        {
            SyncViewCheckBoxes.Add(SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked));
        }
        for (auto& St : States)
        {
            EFaceAngleState S = St.S;
            bool IsActive = (S == ActiveViewState);
            TSharedRef<SImage> DotImg = SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush"));
            DotImg->SetColorAndOpacity(GetStateDotColor(S));
            ViewTabDots.Add(DotImg);
            TSharedRef<SBox> Dot = SNew(SBox).WidthOverride(8).HeightOverride(8)[DotImg];
            TSharedRef<SButton> TabBtn = SNew(SButton)
                .ButtonColorAndOpacity_Lambda([this, S]()
                {
                    if (bStatePickMode && SyncViewCheckBoxes.IsValidIndex((int32)S)
                        && SyncViewCheckBoxes[(int32)S].IsValid()
                        && SyncViewCheckBoxes[(int32)S]->IsChecked())
                        return FLinearColor(1.0f, 0.7f, 0.3f); // picked sync destination
                    return (S == ActiveViewState) ? AccentBlue() : FLinearColor(0.12f,0.12f,0.12f);
                })
                .OnClicked_Lambda([this, S]()
                {
                    if (bStatePickMode)
                    {
                        if (SyncViewCheckBoxes.IsValidIndex((int32)S) && SyncViewCheckBoxes[(int32)S].IsValid())
                        {
                            SyncViewCheckBoxes[(int32)S]->ToggleCheckedState();
                            if (TextStatus.IsValid())
                                TextStatus->SetText(FText::FromString(FString::Printf(
                                    TEXT("Pick: %s is now a sync destination"),
                                    *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)S))));
                        }
                        return FReply::Handled();
                    }
                    SetActiveViewState(S);
                    RefreshUI();
                    return FReply::Handled();
                })
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
            // P17 fit-first: the 9 duplicate buttons stack without a scroll
            // bar (context menus auto-size; 140px box + scroll removed).
            TSharedRef<SVerticalBox> DupButtons = SNew(SVerticalBox);
            for (int32 Di = 0; Di < 10; ++Di)
            {
                EFaceAngleState DS = (EFaceAngleState)Di;
                if (DS == S) continue;
                FString DSName = StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)DS);
                DupButtons->AddSlot().Padding(FMargin(2,1))
                    [MakeBtn(FString::Printf(TEXT("Copy %s -> %s"), *DSName, *StateName),
                        [this, DS, S]()
                        {
                            if (SelectedLayerName.IsValid()) DuplicateState(DS, S);
                            RefreshUI();
                        }, FLinearColor(0.6f,0.8f,1.0f), FLinearColor(0.08f,0.08f,0.08f))];
            }
            Menu->AddSlot().AutoHeight()
                [SNew(SBox).HeightOverride(224)[DupButtons]];
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
        // Pick mode toggle: when on, state tabs pick sync destinations instead
        // of switching the active view (replaces the old props-pane checkbox row).
        {
            TSharedRef<SButton> PickBtn = SNew(SButton)
                .ButtonColorAndOpacity_Lambda([this]()
                { return bStatePickMode ? FLinearColor(1.0f, 0.7f, 0.3f) : FLinearColor(0.08f,0.08f,0.08f); })
                .OnClicked_Lambda([this]()
                {
                    bStatePickMode = !bStatePickMode;
                    if (TextStatus.IsValid())
                        TextStatus->SetText(FText::FromString(bStatePickMode
                            ? TEXT("Pick mode: click state tabs to choose sync destinations")
                            : TEXT("Pick mode off")));
                    RefreshUI();
                    return FReply::Handled();
                })
                .Content()
                [MakeLbl(TEXT("Pick"), 9, FLinearColor(0.9f,0.9f,0.9f))];
            PickBtn->SetToolTipText(FText::FromString(TEXT("Pick mode: click state tabs to toggle them as sync destinations (Sync -> Selected uses them)")));
            StateBar->AddSlot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)[PickBtn];
        }
        Root->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.1f,0.1f,0.1f))
                .Padding(FMargin(2,3))
                [SNew(SBox).HeightOverride(26)[StateBar]]];
    }

}

void UFaceParallaxEditorWidget::BuildPanelZoneDiagram(const TSharedRef<SVerticalBox>& Root)
{
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
        ZoneDragOverlay = SNew(SZoneBoundaryOverlay);
        ZoneDragOverlay->Owner = this;
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

}

void UFaceParallaxEditorWidget::BuildPanelRailContainers(const TSharedRef<SVerticalBox>& Root)
{
    // --- Rail containers (created up-front so every section below can target them) ---
    RailContent.SetNum(6);
    for (int32 Ri = 0; Ri < 6; ++Ri)
        RailContent[Ri] = SNew(SVerticalBox);
    RailSwitcher = SNew(SWidgetSwitcher).WidgetIndex(ActiveRailIndex);
    // Phase 4b: per-rail section registry + jump chips are rebuilt with the tree.
    RailSections.SetNum(6);
    for (auto& R : RailSections) R.Reset();
    RailChipsRows.SetNum(6);
    // P17 fit-first: each rail is a fit-packed stack (no vertical scroll bar).
    // Wide button rows stay inside the rail width via the horizontal scroll
    // viewport (RailH); tall content stays bounded because the rails are
    // fit-packed, accordion-collapsed, or carousel-paged (P18).
    // A pinned jump-chip row sits above the rail so section navigation never
    // scrolls away (Phase 4b accessibility).
    for (int32 Ri = 0; Ri < 6; ++Ri)
    {
        TSharedRef<SScrollBox> RailH = SNew(SScrollBox).Orientation(Orient_Horizontal);
        RailH->AddSlot()[RailContent[Ri].ToSharedRef()];
        TSharedRef<SHorizontalBox> Chips = SNew(SHorizontalBox);
        RailChipsRows[Ri] = Chips;
        TSharedRef<SScrollBox> ChipsScroll = SNew(SScrollBox).Orientation(Orient_Horizontal);
        ChipsScroll->AddSlot()[Chips];
        TSharedRef<SVerticalBox> RailStack = SNew(SVerticalBox);
        RailStack->AddSlot().AutoHeight()
            [SNew(SBox).HeightOverride(26)[ChipsScroll]];
        RailStack->AddSlot().FillHeight(1.0f)[RailH];
        RailSwitcher->AddSlot()[RailStack];
    }
    SlotPropsBox = SNew(SVerticalBox);
    PropTabContent.Reset();
    PropTabContent.Add(SlotPropsBox);        // [0] right pane
    PropTabContent.Add(RailContent[0]);      // [1] Layers
    PropTabContent.Add(RailContent[1]);      // [2] Transform
    PropTabContent.Add(RailContent[2]);      // [3] Camera
    PropTabContent.Add(RailContent[3]);      // [4] Debug
    PropTabContent.Add(RailContent[4]);      // [5] Advanced
    PropTabContent.Add(RailContent[5]);      // [6] Assign
    DebugAccordion = SNew(SFaceAccordion);
    AdvancedAccordion = SNew(SFaceAccordion);

}

void UFaceParallaxEditorWidget::BuildPanelLayers(const TSharedRef<SVerticalBox>& Root)
{
    // --- 3a. LAYERS PANEL (rail 0) ---
    TSharedRef<SVerticalBox> LayerPanel = RailContent[0].ToSharedRef();
    {
        LayerPanel->AddSlot().AutoHeight().Padding(FMargin(4,4,4,2))
            [MakeLbl(TEXT("LAYERS"), 10, FLinearColor(0.7f,0.7f,0.9f))];
        // P17/P18: the layer list is a paged carousel - a fixed 8-row page
        // viewport (CarouselViewportH) with a bottom reserve (P19) so the
        // last row never blocks the nav strip / Add button under it.
        LayerPanelBox = SNew(SVerticalBox);
        const TSharedRef<SWidget> LayerViewport = SNew(SBox)
            .HeightOverride(FPLayout::CarouselViewportH)
            .Padding(FMargin(0, 0, 0, FPLayout::ScrollReserveBottom))
            [LayerPanelBox.ToSharedRef()];
        RefreshLayerList();
        LayerPanel->AddSlot().AutoHeight()[LayerViewport];
        TSharedRef<SFaceCarouselNav> LayerNav = SNew(SFaceCarouselNav)
            .OnPrev_Lambda([this]()
            {
                LayerPageIndex = FMath::Max(0, LayerPageIndex - 1);
                RefreshLayerList();
                return FReply::Handled();
            })
            .OnNext_Lambda([this]()
            {
                LayerPageIndex = LayerPageIndex + 1;
                RefreshLayerList();
                return FReply::Handled();
            });
        LayerPageLabel = LayerNav->Label;
        LayerPanel->AddSlot().AutoHeight().Padding(FMargin(4, 0, 4, 2))[LayerNav];
        LayerPanel->AddSlot().AutoHeight().Padding(FMargin(4,2))
            [MakeBtn(TEXT("+ Add Layer"), [this]()
            {
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (Comp)
                {
                    int32 N = 0;
                    for (int32 i = 0; i < Comp->GetNumLayerDefinitions(); ++i)
                        if (!IsSeedPlaceholderLayerDef(Comp->GetLayerDefinition(i))) ++N;
                    FName NewTag(*FString::Printf(TEXT("Layer_%d"), N));
                    FFaceLayerDef NewDef;
                    NewDef.LayerTag = NewTag;
                    Comp->AddLayerDefinition(NewDef);
                    LayerNames.Add(NewTag);
                    SelectedLayerName = NewTag;
                    LayerPageIndex = FPLayout::CarouselPageCount(LayerNames.Num()) - 1;
                }
                RefreshUI();
            }, FLinearColor(0.6f,0.8f,0.6f), FLinearColor(0.08f,0.08f,0.08f))];
        RegisterRailSection(0, TEXT("Layers"), LayerViewport);
    }

}

void UFaceParallaxEditorWidget::BuildPanelTransformRail()
{
            // Quick actions (batch operations) - the canonical pinned actions
            // (Import Art... / Sync All -> All / Auto-Fit All / Clear All
            // Overrides) live ONLY in the pinned strip above the main row
            // (P21 PinnedActionsNeverInScroll); this rail section keeps the
            // rail-local operations that are not part of that canonical set.
            {
                TSharedRef<SVerticalBox> QaBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> QaRow = SNew(SHorizontalBox);
                QaRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
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
                // Sync drift indicator (Phase C): counts views whose transforms
                // have drifted from the active view state (mirror math in
                // FaceParallaxLayoutSpec.h, TestPinDriftMirror).
                {
                    TSharedRef<SHorizontalBox> DriftRow = SNew(SHorizontalBox);
                    DriftRow->AddSlot().AutoWidth().Padding(FMargin(0,3,4,2))
                        [MakeLbl(TEXT("Sync:"), 8, FLinearColor(0.6f,0.6f,0.6f))];
                    TextSyncDrift = SNew(STextBlock)
                        .Text(FText::FromString(TEXT("")))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
                    DriftRow->AddSlot().AutoWidth().Padding(FMargin(0,3,0,2))
                        [TextSyncDrift.ToSharedRef()];
                    QaBox->AddSlot().AutoHeight()[DriftRow];
                }
                TSharedRef<SWidget> QaSection = MakeSectionBox(TEXT("Quick Actions"), QaBox);
                RailContent[1]->AddSlot().AutoHeight()
                    [QaSection];
                RegisterRailSection(1, TEXT("Quick Actions"), QaSection);
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
                // Per-axis sync (P3): one button per axis, mirroring
                // FPLayout::SyncAxisDelta via SyncLayerAxisToAllViews.
                {
                    TSharedRef<SHorizontalBox> AxisRow = SNew(SHorizontalBox);
                    AxisRow->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
                        [MakeLbl(TEXT("Sync axis:"), 8, FLinearColor(0.6f, 0.6f, 0.7f))];
                    struct { const TCHAR* T; int32 Axis; const TCHAR* Tip; } Axes[] = {
                        {TEXT("X"), 0, TEXT("Sync Position X to all views")},
                        {TEXT("Y"), 1, TEXT("Sync Position Y to all views")},
                        {TEXT("SX"), 2, TEXT("Sync Scale X to all views")},
                        {TEXT("SY"), 3, TEXT("Sync Scale Y to all views")},
                        {TEXT("R"), 4, TEXT("Sync Rotation to all views")},
                    };
                    for (auto& Ax : Axes)
                    {
                        TSharedRef<SButton> AxisBtn = MakeBtn(Ax.T, [this, Axis = Ax.Axis]()
                        {
                            if (!SelectedLayerName.IsValid()) return;
                            SyncLayerAxisToAllViews(ActiveViewState, SelectedLayerName, Axis);
                            RefreshUI();
                            if (TextStatus.IsValid())
                                TextStatus->SetText(FText::FromString(FString::Printf(
                                    TEXT("Synced axis %d (%s) to all views"),
                                    Axis, Axis == 4 ? TEXT("Rot") : TEXT("Pos/Scale"))));
                        }, FLinearColor(0.8f, 0.9f, 1.0f));
                        AxisBtn->SetToolTipText(FText::FromString(Ax.Tip));
                        AxisRow->AddSlot().Padding(FMargin(2, 2)).AutoWidth()
                            [SNew(SBox).WidthOverride(Ax.Axis >= 2 ? 32.0f : 26.0f)[AxisBtn]];
                    }
                    AxisRow->AddSlot().FillWidth(1.0f);
                    XvBox->AddSlot().AutoHeight()[AxisRow];
                }
                TSharedRef<SWidget> XvSection = MakeSectionBox(TEXT("Cross-View Transform"), XvBox);
                RailContent[1]->AddSlot().AutoHeight()
                    [XvSection];
                RegisterRailSection(1, TEXT("Cross-View Transform"), XvSection);
            }

}

void UFaceParallaxEditorWidget::BuildPanelDebugRail()
{
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
                    [MakeBtn(TEXT("Detect Profile"), [this, ReadGrid](){ DetectFaceProfile(); GenerateDepthFromOutlinesImpl(ReadGrid()); RefreshUI(); })];
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

                // Scope selector: which view states the depth bake overwrites.
                TSharedRef<SHorizontalBox> ScopeRow = SNew(SHorizontalBox);
                ScopeRow->AddSlot().Padding(FMargin(0,2)).AutoWidth().VAlign(VAlign_Center)
                    [MakeLbl(TEXT("Bake:"), 8, FLinearColor(0.7f,0.7f,0.7f))];
                auto MakeScopeCheck = [this, &ScopeRow](const TCHAR* Label, int32 Value, const FLinearColor& Color)
                {
                    TSharedRef<SCheckBox> C = SNew(SCheckBox)
                        .IsChecked(OutlineDepthScope == Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this, Value](ECheckBoxState S)
                        {
                            if (S == ECheckBoxState::Checked)
                            {
                                SetOutlineDepthScope(Value);
                                RefreshUI();
                            }
                        });
                    ScopeRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()[C];
                    ScopeRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                        [MakeLbl(Label, 8, Color)];
                };
                MakeScopeCheck(TEXT("Front only"), 0, FLinearColor(0.6f,0.9f,0.7f));
                MakeScopeCheck(TEXT("8 h-states"), 1, FLinearColor(0.6f,0.8f,1.0f));
                MakeScopeCheck(TEXT("All 10"), 2, FLinearColor(0.9f,0.8f,0.6f));
                ScopeRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                OdBox->AddSlot().AutoHeight()[ScopeRow];

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
                DebugAccordion->AddSection(TEXT("Outline -> Depth"), OdBox);
            }

        // ============ DEBUG RAIL (import + config) ============
        {
            TSharedRef<SVerticalBox> T1 = SNew(SVerticalBox);

            TSharedRef<SVerticalBox> ImportBox = SNew(SVerticalBox);
            TSharedRef<SHorizontalBox> ImpRow = SNew(SHorizontalBox);
            ImpRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
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
            DebugAccordion->AddSection(TEXT("Import"), ImportBox);

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

                // Phase 4b: progressive disclosure - the 8 config checks collapse
                // into a summary line ("K of 8 on") until the header is clicked.
                ConfigDisclosure = SNew(SFaceDisclosure);
                ConfigDisclosure->SetTitle(TEXT("Config"));
                ConfigDisclosure->SetBody(CfgBox.ToSharedRef());
                DebugAccordion->AddSection(TEXT("Config"), ConfigDisclosure.ToSharedRef());
                UpdateDisclosureSummaries();
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
                DebugAccordion->AddSection(TEXT("Edge Analysis"), EdBox);
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
                DebugAccordion->AddSection(TEXT("Depth Debug"), DdBox);
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
                DebugAccordion->AddSection(TEXT("Hull Review (click thumb = jump)"), HrBox);
            }

            // Viseme frames grid (Phase E) - fits the disclosure body as-is:
            // no vertical scroll bar (P17 fit-first).
            {
                TSharedRef<SVerticalBox> VgBox = SNew(SVerticalBox);
                VisemeGridBox = SNew(SVerticalBox);
                RebuildVisemeGrid();
                VgBox->AddSlot().AutoHeight()[VisemeGridBox.ToSharedRef()];
                // Phase 4b: progressive disclosure - the grid collapses into a
                // "N viseme rows" summary line until the header is clicked.
                VisemeDisclosure = SNew(SFaceDisclosure);
                VisemeDisclosure->SetTitle(TEXT("Viseme Frames"));
                VisemeDisclosure->SetBody(VgBox);
                DebugAccordion->AddSection(
                    TEXT("Viseme Frames (click filled cell = play)"), VisemeDisclosure.ToSharedRef());
            }

            // Problems panel (Phase F) - the issue rows flip through carousel
            // pages inside the Issues section (P18); the panel itself is a
            // fit-packed stack, so no vertical scroll bar (P17).
            {
                TSharedRef<SVerticalBox> PrBox = SNew(SVerticalBox);
                ProblemsPanelBox = SNew(SVerticalBox);
                RebuildProblemsPanel();
                PrBox->AddSlot().AutoHeight()[ProblemsPanelBox.ToSharedRef()];
                DebugAccordion->AddSection(TEXT("Problems (click row = jump)"), PrBox);
                RefreshProblemsSummary();
            }

            DebugAccordion->SetExpanded(1, true); // Import section open by default
            T1->AddSlot().AutoHeight()[DebugAccordion.ToSharedRef()];
            RailContent[3]->AddSlot()[T1];
            // Phase 4b: register all accordion sections for chips + search jump
            // (titles/order mirrored by FPLayout::RailSectionTitles()).
            RegisterAccordionSections(3, DebugAccordion);
        }

}

void UFaceParallaxEditorWidget::BuildPanelCameraRail()
{
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
                TSharedRef<SWidget> CfSection = MakeSectionBox(TEXT("Camera Follow"), CfBox);
                RailContent[2]->AddSlot().AutoHeight()
                    [CfSection];
                RegisterRailSection(2, TEXT("Camera Follow"), CfSection);
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
                    if (ZoneEditBoxes.Num() < 4) ZoneEditBoxes.SetNum(4);
                    ZoneEditBoxes[Zi] = Edit;
                };
                AddZoneEdit(0); AddZoneEdit(1); AddZoneEdit(2); AddZoneEdit(3);
                Cam->AddSlot().AutoHeight()[ZoneRow];

                TSharedRef<SWidget> CamSection = MakeSectionBox(TEXT("Camera"), Cam);
                T2->AddSlot().AutoHeight()
                    [CamSection];
                RegisterRailSection(2, TEXT("Camera"), CamSection);
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
                TSharedRef<SWidget> BlendSection = MakeSectionBox(TEXT("Blend Preview"), BlendBox);
                T2->AddSlot().AutoHeight()
                    [BlendSection];
                RegisterRailSection(2, TEXT("Blend Preview"), BlendSection);
            }
        }

}

void UFaceParallaxEditorWidget::BuildPanelAdvancedRail()
{
        // ============ ADVANCED RAIL ============
        {
            TSharedRef<SVerticalBox> T3 = RailContent[4].ToSharedRef();

            // Cross-layer overlay - the per-layer lines flip through carousel
            // pages (P18) inside a fixed page viewport with a bottom reserve
            // (P19); no vertical scroll bar (P17).
            CrossLayerBox = SNew(SVerticalBox);
            TextCrossLayer = SNew(STextBlock)
                .Text(FText::FromString(TEXT("Select a layer to show overlay")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FLinearColor(0.5f,0.5f,0.5f));
            CrossLayerBox->AddSlot().AutoHeight().Padding(FMargin(2))
                [TextCrossLayer.ToSharedRef()];
            TSharedRef<SVerticalBox> ALBox = SNew(SVerticalBox);
            ALBox->AddSlot().AutoHeight()
                [SNew(SBox)
                    .HeightOverride(FPLayout::CarouselViewportH)
                    .Padding(FMargin(0, 0, 0, FPLayout::ScrollReserveBottom))
                    [CrossLayerBox.ToSharedRef()]];
            TSharedRef<SFaceCarouselNav> ALNav = SNew(SFaceCarouselNav)
                .OnPrev_Lambda([this]()
                {
                    CrossLayerPageIndex = FMath::Max(0, CrossLayerPageIndex - 1);
                    RebuildCrossLayerPanel();
                    return FReply::Handled();
                })
                .OnNext_Lambda([this]()
                {
                    CrossLayerPageIndex = CrossLayerPageIndex + 1;
                    RebuildCrossLayerPanel();
                    return FReply::Handled();
                });
            CrossLayerPageLabel = ALNav->Label;
            ALBox->AddSlot().AutoHeight().Padding(FMargin(4, 0, 4, 2))[ALNav];
            AdvancedAccordion->AddSection(TEXT("All Layers (current state)"), ALBox);

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
                AdvancedAccordion->AddSection(TEXT("Param Reference"), RefBox);
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
                AdvancedAccordion->AddSection(TEXT("Param Bindings (state + layer)"), PbBox);
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
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (!Comp) return;
                            if (GetSelectedPinElement(El, Count))
                            {
                                El.Pin3D.bPinned = (S == ECheckBoxState::Checked);
                                Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                                RefreshUI();
                                return;
                            }
                            // P3: no nested element — toggle the whole-layer pin.
                            if (SelectedLayerName.IsValid() && ActivePreset)
                            {
                                FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                                LS.LayerPin3D.bPinned = (S == ECheckBoxState::Checked);
                                ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                                RefreshUI();
                            }
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
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.Pin3D.Position3D.X = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                            return;
                        }
                        if (SelectedLayerName.IsValid() && ActivePreset)
                        {
                            FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                            LS.LayerPin3D.Position3D.X = V;
                            ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                            RefreshUI();
                        }
                    });
                AddPinSliderRow(TEXT("Pin Y"), -2.0f, 2.0f, InitEl.Pin3D.Position3D.Y, 2, SliderPinY, TextPinY,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.Pin3D.Position3D.Y = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                            return;
                        }
                        if (SelectedLayerName.IsValid() && ActivePreset)
                        {
                            FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                            LS.LayerPin3D.Position3D.Y = V;
                            ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                            RefreshUI();
                        }
                    });
                AddPinSliderRow(TEXT("Pin Z"), -2.0f, 2.0f, InitEl.Pin3D.Position3D.Z, 2, SliderPinZ, TextPinZ,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.Pin3D.Position3D.Z = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                            return;
                        }
                        if (SelectedLayerName.IsValid() && ActivePreset)
                        {
                            FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                            LS.LayerPin3D.Position3D.Z = V;
                            ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                            RefreshUI();
                        }
                    });

                // View-angle rotation controls
                {
                    TSharedRef<SHorizontalBox> RotRow = SNew(SHorizontalBox);
                    CheckPinRotEnabled = SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                        {
                            FFaceNestedArt El;
                            int32 Count = 0;
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (!Comp) return;
                            if (GetSelectedPinElement(El, Count))
                            {
                                El.Pin3D.bEnableViewAngleRotation = (S == ECheckBoxState::Checked);
                                Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                                RefreshUI();
                                return;
                            }
                            if (SelectedLayerName.IsValid() && ActivePreset)
                            {
                                FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                                LS.LayerPin3D.bEnableViewAngleRotation = (S == ECheckBoxState::Checked);
                                ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                                RefreshUI();
                            }
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
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.Pin3D.MinRotation = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                            return;
                        }
                        if (SelectedLayerName.IsValid() && ActivePreset)
                        {
                            FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                            LS.LayerPin3D.MinRotation = V;
                            ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                            RefreshUI();
                        }
                    });
                AddPinSliderRow(TEXT("Max Rot"), -180.0f, 180.0f, InitEl.Pin3D.MaxRotation, 1, SliderPinMaxRot, TextPinMaxRot,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.Pin3D.MaxRotation = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                            return;
                        }
                        if (SelectedLayerName.IsValid() && ActivePreset)
                        {
                            FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                            LS.LayerPin3D.MaxRotation = V;
                            ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                            RefreshUI();
                        }
                    });
                AddPinSliderRow(TEXT("Sens"), -10.0f, 10.0f, InitEl.Pin3D.RotationSensitivity, 2, SliderPinRotSens, TextPinRotSens,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.Pin3D.RotationSensitivity = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                            return;
                        }
                        if (SelectedLayerName.IsValid() && ActivePreset)
                        {
                            FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                            LS.LayerPin3D.RotationSensitivity = V;
                            ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                            RefreshUI();
                        }
                    });

                Pin->AddSlot().AutoHeight().Padding(FMargin(0,2))
                    [MakeBtn(TEXT("Detect Profile"), [this](){ DetectFaceProfile(); RefreshUI(); })];
                Pin->AddSlot().AutoHeight().Padding(FMargin(0,4,0,0))
                    [MakeLbl(TEXT("Nested Elements"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                NestedOutlinerBox = SNew(SVerticalBox);
                RebuildNestedOutliner();
                Pin->AddSlot().AutoHeight().Padding(FMargin(0,2,0,0))
                    [NestedOutlinerBox.ToSharedRef()];
                AdvancedAccordion->AddSection(TEXT("Nested Art / Pins"), Pin);

                RefreshPinControls();
            }
            T3->AddSlot().AutoHeight()[AdvancedAccordion.ToSharedRef()];
        }

}

void UFaceParallaxEditorWidget::BuildPanelTimeline(const TSharedRef<SVerticalBox>& Root)
{
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

}

void UFaceParallaxEditorWidget::BuildPanelBottomBar(const TSharedRef<SVerticalBox>& Root)
{
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
        TSharedRef<SWidget> StatusSection = MakeSectionBox(TEXT("Status Detail"), StatusMatrixScroll.ToSharedRef());
        RailContent[0]->AddSlot().AutoHeight().Padding(FMargin(2,1))
            [StatusSection];
        RegisterRailSection(0, TEXT("Status Detail"), StatusSection);

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
            [MakeBtn(TEXT("Backup Point"), [this](){ SnapshotPreset(); if (TextStatus.IsValid()) TextStatus->SetText(FText::FromString(TEXT("Backup point saved."))); })];
        TSharedRef<SButton> RestoreBtn = MakeBtn(TEXT("Restore Backup"), [this](){ RestoreSnapshot(); RefreshUI(); }, FLinearColor(1.0f,0.7f,0.3f));
        RestoreBtn->SetToolTipText(FText::FromString(TEXT("Restores the preset to the last saved Backup Point (single slot; use the Undo/Redo toolbar buttons for multi-step undo)")));
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

    // Phase 4b: register the Advanced rail accordion sections (chips + search).
    RegisterAccordionSections(4, AdvancedAccordion);

}

void UFaceParallaxEditorWidget::BuildPanelAssignRail()
{
    // ============ ASSIGN RAIL (rail 5) ============
    TSharedRef<SVerticalBox> T5 = RailContent[5].ToSharedRef();

    // Bulk Assign grid: 10 state columns x 3 layer rows, plus row labels and
    // the coverage summary. Cells mirror FPLayout::AssignCellState (2 full,
    // 1 partial, 0 empty); the coverage label mirrors AssignCoverageText.
    {
        struct { EFaceAngleState S; const TCHAR* T; } States[] = {
            {EFaceAngleState::Front, TEXT("Front")},
            {EFaceAngleState::ThreeQuarterRight, TEXT("3/4R")},
            {EFaceAngleState::RightProfile, TEXT("ProfR")},
            {EFaceAngleState::BackRight, TEXT("BackR")},
            {EFaceAngleState::Back, TEXT("Back")},
            {EFaceAngleState::BackLeft, TEXT("BackL")},
            {EFaceAngleState::LeftProfile, TEXT("ProfL")},
            {EFaceAngleState::ThreeQuarterLeft, TEXT("3/4L")},
            {EFaceAngleState::Top, TEXT("Top")},
            {EFaceAngleState::Bottom, TEXT("Bot")},
        };
        const int32 NumRows = 3;

        TSharedRef<SVerticalBox> GB = SNew(SVerticalBox);
        TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
        for (auto& St : States)
        {
            Header->AddSlot().AutoWidth().Padding(FMargin(0, 2))
                [SNew(SBox).WidthOverride(16)
                    [SNew(STextBlock)
                        .Text(FText::FromString(St.T))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
                        .Justification(ETextJustify::Center)
                        .ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.6f))]];
        }
        GB->AddSlot().AutoHeight()[Header];

        AssignCells.Reset();
        for (int32 Ri = 0; Ri < NumRows; ++Ri)
        {
            const FName Tag = LayerNames.IsValidIndex(Ri) ? LayerNames[Ri] : FName();
            TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
            for (int32 Ci = 0; Ci < 10; ++Ci)
            {
                const EFaceAngleState S = (EFaceAngleState)Ci;
                TSharedRef<SImage> Cell = SNew(SImage)
                    .Image(FCoreStyle::Get().GetBrush("WhiteBrush"));
                Cell->SetColorAndOpacity(FLinearColor(0.12f, 0.12f, 0.14f));
                AssignCells.Add(Cell);
                TSharedRef<SButton> CellBtn = SNew(SButton)
                    .ButtonColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f))
                    .OnClicked_Lambda([this, S, Tag]()
                    {
                        if (!Tag.IsValid()) return FReply::Handled();
                        SetActiveViewState(S);
                        SelectedLayerName = Tag;
                        RefreshUI();
                        if (TextStatus.IsValid())
                            TextStatus->SetText(FText::FromString(FString::Printf(
                                TEXT("Assign grid: %s / %s"),
                                *StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)S),
                                *Tag.ToString())));
                        return FReply::Handled();
                    })[Cell];
                Row->AddSlot().AutoWidth().Padding(FMargin(0, 1))
                    [SNew(SBox).WidthOverride(16).HeightOverride(16)[CellBtn]];
            }
            GB->AddSlot().AutoHeight()[Row];
        }

        TSharedRef<SHorizontalBox> RowLbls = SNew(SHorizontalBox);
        for (int32 Ri = 0; Ri < NumRows; ++Ri)
        {
            const FName Tag = LayerNames.IsValidIndex(Ri) ? LayerNames[Ri] : FName();
            const FString Label = Tag.IsValid() ? Tag.ToString() : TEXT("(no layer)");
            RowLbls->AddSlot().AutoWidth().Padding(FMargin(0, 2))
                [SNew(SBox).WidthOverride(60)
                    [MakeLbl(*Label, 7, FLinearColor(0.6f, 0.6f, 0.7f))]];
        }
        TextAssignCoverage = MakeLbl(TEXT("Filled 0/30"), 8, FLinearColor(0.7f, 0.9f, 0.7f));
        RowLbls->AddSlot().Padding(FMargin(4, 2)).AutoWidth()[TextAssignCoverage.ToSharedRef()];
        GB->AddSlot().AutoHeight()[RowLbls];

        TSharedRef<SWidget> GridSection = MakeSectionBox(TEXT("Bulk Assign"), GB);
        T5->AddSlot().AutoHeight().Padding(FMargin(2, 1, 2, 1))[GridSection];
        RegisterRailSection(5, TEXT("Bulk Assign"), GridSection);
        RefreshAssignGrid();
    }

    // Assign Ops: fill-missing / clear-row / slot-to-all bulk actions, plus
    // the performance tier and camera source combos (P3).
    {
        TSharedRef<SVerticalBox> OB = SNew(SVerticalBox);
        TSharedRef<SHorizontalBox> Row0 = SNew(SHorizontalBox);
        Row0->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
            [MakeBtn(TEXT("Fill Missing"), [this]()
            {
                FillMissingViewsFromActiveSlot();
                RefreshUI();
            }, FLinearColor(0.6f, 1.0f, 0.6f))];
        Row0->AddSlot().Padding(FMargin(4, 2)).AutoWidth()
            [MakeBtn(TEXT("Clear Row"), [this]()
            {
                if (!SelectedLayerName.IsValid()) return;
                FWidgetUndoScope UndoScope(this, TEXT("Clear Row"));
                for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
                    ClearAllOverridesForSlot((EFaceAngleState)i, SelectedLayerName);
                RefreshUI();
            }, FLinearColor(1.0f, 0.6f, 0.6f))];
        Row0->AddSlot().Padding(FMargin(4, 2)).AutoWidth()
            [MakeBtn(TEXT("Slot -> All"), [this]()
            {
                if (!SelectedLayerName.IsValid() || !ActivePreset) return;
                FWidgetUndoScope UndoScope(this, TEXT("Slot -> All States"));
                const FFaceArtSlot Src = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
                {
                    if (i == (int32)ActiveViewState) continue;
                    ActivePreset->SetSlot((EFaceAngleState)i, SelectedLayerName, Src);
                }
                RefreshUI();
            }, FLinearColor(0.8f, 0.9f, 1.0f))];
        Row0->AddSlot().FillWidth(1.0f);
        OB->AddSlot().AutoHeight()[Row0];

        TSharedRef<SHorizontalBox> Row1 = SNew(SHorizontalBox);
        Row1->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
            [MakeLbl(TEXT("Perf tier"), 8, FLinearColor(0.6f, 0.6f, 0.7f))];
        PerfTierOptions.Reset();
        PerfTierOptions.Add(MakeShared<FString>(TEXT("Low")));
        PerfTierOptions.Add(MakeShared<FString>(TEXT("Medium")));
        PerfTierOptions.Add(MakeShared<FString>(TEXT("High")));
        PerfTierSelection = PerfTierOptions[FMath::Clamp(PerformanceTier, 0, 2)];
        TSharedRef<SComboBox<TSharedPtr<FString>>> PerfCombo =
            SNew(SComboBox<TSharedPtr<FString>>)
            .OptionsSource(&PerfTierOptions)
            .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
            {
                return SNew(STextBlock)
                    .Text(FText::FromString(*Item))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
            })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
            {
                if (!Item.IsValid()) return;
                PerfTierSelection = Item;
                const int32 Tier = FMath::Clamp(Item->Equals(TEXT("Low")) ? 0
                    : (Item->Equals(TEXT("High")) ? 2 : 1), 0, 2);
                PerformanceTier = Tier;
                if (UFaceParallaxComponent* Comp = GetParallaxComponent())
                    Comp->MaxAsyncTextureCacheSize = FPLayout::PerformanceTierCacheSize(Tier);
                if (TextStatus.IsValid())
                    TextStatus->SetText(FText::FromString(FString::Printf(
                        TEXT("Performance tier: %s (cache %d)"),
                        *(*Item), FPLayout::PerformanceTierCacheSize(Tier))));
            })
            [SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return PerfTierSelection.IsValid()
                        ? FText::FromString(*PerfTierSelection)
                        : FText::FromString(TEXT("Medium"));
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))];
        Row1->AddSlot().Padding(FMargin(4, 2)).AutoWidth()
            [SNew(SBox).WidthOverride(70)[PerfCombo]];

        Row1->AddSlot().Padding(FMargin(8, 2)).AutoWidth()
            [MakeLbl(TEXT("Camera source"), 8, FLinearColor(0.6f, 0.6f, 0.7f))];
        CameraSourceOptions.Reset();
        for (const std::string& Lbl : FPLayout::CameraSourceLabels())
            CameraSourceOptions.Add(MakeShared<FString>(UTF8_TO_TCHAR(Lbl.c_str())));
        UFaceParallaxComponent* Comp = GetParallaxComponent();
        const int32 CurSrcIdx = Comp ? (int32)Comp->CameraSource : 0;
        CameraSourceSelection = CameraSourceOptions[FMath::Clamp(CurSrcIdx, 0, 5)];
        TSharedRef<SComboBox<TSharedPtr<FString>>> CamCombo =
            SNew(SComboBox<TSharedPtr<FString>>)
            .OptionsSource(&CameraSourceOptions)
            .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
            {
                return SNew(STextBlock)
                    .Text(FText::FromString(*Item))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
            })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
            {
                if (!Item.IsValid()) return;
                CameraSourceSelection = Item;
                for (int32 i = 0; i < (int32)CameraSourceOptions.Num(); ++i)
                {
                    if (CameraSourceOptions[i].IsValid()
                        && CameraSourceOptions[i]->Equals(*Item))
                    {
                        if (UFaceParallaxComponent* C = GetParallaxComponent())
                            C->SetCameraSource((ECameraSource)i);
                        break;
                    }
                }
                RefreshUI();
            })
            [SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return CameraSourceSelection.IsValid()
                        ? FText::FromString(*CameraSourceSelection)
                        : FText::FromString(TEXT("PlayerCamera0"));
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))];
        Row1->AddSlot().Padding(FMargin(4, 2)).AutoWidth()
            [SNew(SBox).WidthOverride(100)[CamCombo]];
        OB->AddSlot().AutoHeight()[Row1];

        TSharedRef<SWidget> OpsSection = MakeSectionBox(TEXT("Assign Ops"), OB);
        T5->AddSlot().AutoHeight().Padding(FMargin(2, 1, 2, 1))[OpsSection];
        RegisterRailSection(5, TEXT("Assign Ops"), OpsSection);
    }
}

void UFaceParallaxEditorWidget::RefreshAssignGrid()
{
    const int32 NumRows = 3;
    int32 Filled = 0;
    for (int32 Ri = 0; Ri < NumRows; ++Ri)
    {
        const FName Tag = LayerNames.IsValidIndex(Ri) ? LayerNames[Ri] : FName();
        for (int32 Ci = 0; Ci < 10; ++Ci)
        {
            const int32 CellIdx = Ri * 10 + Ci;
            if (CellIdx >= AssignCells.Num()) return;
            const EFaceAngleState S = (EFaceAngleState)Ci;
            int32 State = 0;
            if (Tag.IsValid() && ActivePreset && HasSlot(S, Tag))
                State = IsSlotFullyAssigned(S, Tag) ? 2 : 1;
            AssignCells[CellIdx]->SetColorAndOpacity(
                State == 2 ? FLinearColor(0.35f, 0.85f, 0.45f)
                : (State == 1 ? FLinearColor(0.95f, 0.75f, 0.25f)
                    : FLinearColor(0.12f, 0.12f, 0.14f)));
            if (State == 2) ++Filled;
        }
    }
    if (TextAssignCoverage.IsValid())
    {
        const FString C = UTF8_TO_TCHAR(FPLayout::AssignCoverageText(Filled, NumRows * 10).c_str());
        TextAssignCoverage->SetText(FText::FromString(FString::Printf(TEXT("Filled %s"), *C)));
    }
}

#endif // WITH_EDITOR
