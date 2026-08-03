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
#include "Input/DragAndDrop.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "ContentBrowserDataDragDropOp.h"
#include "Misc/Paths.h"

// Drag-drop wrapper (SFaceDropTarget) + IsDroppableImageFile now live in
// FaceParallaxEditorWidgetShared.h so every widget TU — the slot thumbs here,
// the hull-review thumbs and the import wizard — shares one drop contract.
void UFaceParallaxEditorWidget::RefreshUI()
{
    if (bIsRefreshing) return;
    bIsRefreshing = true;

    RefreshActorSelector();
    RefreshLayerList();
    RebuildPartsStrip();
    RebuildPinList();
    RefreshHotspotRegions();
    RefreshSchematic();
    RefreshTextureThumbs();
    RefreshCanvasPreview();
    RefreshViewStripDots();
    RefreshSlotPropStatus();
    RefreshOnionSkin();
    if (GizmoWidget.IsValid())
    {
        // P7-C: pin mode is now ALWAYS live and context-derived — the old
        // canvas-strip Pin Mode toggle is gone. The gizmo edits the pin when
        // the selected nested element (or the layer itself) has one, and the
        // hotspot layer only moves pins when a click lands on a pin handle;
        // every other canvas click still selects parts (one-map).
        FFaceNestedArt NestedElement;
        const bool bPinActive = SelectedNestedElementIndex >= 0
            ? GetSelectedPinElement(NestedElement, SelectedNestedElementIndex)
            : (ActivePreset && SelectedLayerName.IsValid()
                ? ActivePreset->GetSlot(ActiveViewState, SelectedLayerName).LayerPin3D.bPinned
                : false);
        GizmoWidget->SetPinMode(bPinActive);
        GizmoWidget->Invalidate(EInvalidateWidgetReason::Paint);
        // Phase 0: the gizmo is paint-only — the hotspot layer routes the
        // pin-drag (and every other canvas click).
        if (HotspotLayer.IsValid())
            HotspotLayer->SetCanvasPinMode(bPinActive);
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
    RebuildPinManager();
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
    // Display-mode dedupe (P3): the Diagnostics rail's Config checks (Show
    // Textures / Depth Mesh / Wireframe) are the source of truth; the canvas
    // mode row is derived from them (custom combos and
    // all-off clear the row highlight via -1), mirroring DeriveDisplayMode.
    DisplayMode = FPLayout::DeriveDisplayMode(bLocalShowTextures, bLocalShowDepthMesh, bLocalShowWireframe);
    // Phase C: the unified inspect row adds the Outline overlay + Color by
    // Depth to the same five-toggle source set (DeriveInspectMode).
    InspectMode = FPLayout::DeriveInspectMode(bLocalShowTextures, bLocalShowDepthMesh,
        bLocalShowWireframe, bOutlineOverlayVisible, bLocalColorByDepth);
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
    LayerRowBrushes.SetNum(FMath::Max(0, End - Start));
    for (int32 li = Start; li < End; ++li)
    {
        const FName& Tag = LayerNames[li];
        bool bSelected = (Tag == SelectedLayerName);
        FString TagStr = Tag.ToString();
        bool bLayerHidden = Comp && !Comp->GetLayerVisibility(Tag);

        // Phase E: per-row albedo thumbnail (active view) + completeness
        // badge (Assigned / Partial / Missing for the active view).
        UTexture2D* RowAlbedo = nullptr;
        bool bA = false, bN = false, bD = false;
        if (Comp && Comp->ActivePreset)
        {
            const FFaceArtSlot& RS = Comp->ActivePreset->GetSlot(ActiveViewState, Tag);
            bA = RS.Textures.Albedo != nullptr;
            bN = RS.Textures.Normal != nullptr;
            bD = RS.Textures.Depth != nullptr;
            RowAlbedo = RS.Textures.Albedo;
        }
        FSlateBrush& RowBrush = LayerRowBrushes[li - Start];
        RowBrush = FSlateBrush();
        if (RowAlbedo)
        {
            RowBrush.SetResourceObject(RowAlbedo);
            RowBrush.ImageSize = FVector2D(18, 14);
            RowBrush.DrawAs = ESlateBrushDrawType::Image;
        }
        const int RowCell = FPLayout::AssignCellState(bA, bN, bD);
        const FLinearColor BadgeCol = (RowCell == 2) ? FLinearColor(0.35f,0.75f,0.35f)
            : (RowCell == 1) ? FLinearColor(1.0f,0.75f,0.25f)
            : FLinearColor(0.8f,0.35f,0.35f);
        const FString BadgeTip = FString::Printf(TEXT("%s in %s (A/N/D: %s)"),
            UTF8_TO_TCHAR(FPLayout::AssignCellLabel(RowCell)),
            Comp ? *Comp->GetStateLabel(ActiveViewState).ToString() : TEXT("?"),
            *FString::Printf(TEXT("%c%c%c"), bA ? TCHAR('A') : TCHAR('-'),
                bN ? TCHAR('N') : TCHAR('-'), bD ? TCHAR('D') : TCHAR('-')));

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
        // Phase E: albedo thumbnail (gray block when the view has none)
        Row->AddSlot().AutoWidth().Padding(FMargin(3,0,0,0)).VAlign(VAlign_Center)
            [SNew(SBox).WidthOverride(18).HeightOverride(14)
                [SNew(SImage)
                    .Image(&RowBrush)
                    .ColorAndOpacity(RowAlbedo ? FLinearColor(1,1,1) : FLinearColor(0.16f,0.16f,0.16f))]];
        // Phase E: completeness badge dot (active view)
        Row->AddSlot().AutoWidth().Padding(FMargin(3,0,0,0)).VAlign(VAlign_Center)
            [SNew(SBox).WidthOverride(9).HeightOverride(9)
                [SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(BadgeCol)
                    .Padding(FMargin(0))]];
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
                .ToolTipText(FText::FromString(BadgeTip))
                .Content()
                [SNew(STextBlock)
                    .Text(FText::FromString(bLayerHidden ? (TagStr + TEXT(" [hidden]")) : TagStr))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", bSelected ? 10 : 9))
                    .ColorAndOpacity(bLayerHidden ? FLinearColor(0.35f,0.35f,0.35f) :
                        (bSelected ? FLinearColor(1,1,1) : FLinearColor(0.7f,0.7f,0.7f)))]];
        // P7-F: whole layer row is an image-drop host — a dropped asset/file
        // fills the row's layer (active view) Albedo/Normal/Depth by suffix.
        // P18/P19: each row is EXACTLY CarouselRowHeight (22px) tall — no
        // vertical slot padding — so 8 rows (176px) fit the fixed 184px page
        // viewport with the 8px reserve and the Page x/y nav strip never
        // intersects the last row (Hair).
        LayerPanelBox->AddSlot().AutoHeight().Padding(FMargin(0))
            [SNew(SBox).HeightOverride(22)
                [SNew(SFaceDropTarget)
                    .OnFaceDragOver_Lambda([](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                    {
                        if (Evt.GetOperationAs<FAssetDragDropOp>().IsValid()) return FReply::Handled();
                        if (Evt.GetOperationAs<FContentBrowserDataDragDropOp>().IsValid()) return FReply::Handled();
                        TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
                        if (FileOp.IsValid() && FileOp->HasFiles())
                        {
                            for (const FString& File : FileOp->GetFiles())
                                if (IsDroppableImageFile(File)) return FReply::Handled();
                        }
                        return FReply::Unhandled();
                    })
                    .OnFaceDrop_Lambda([this, Tag](const FGeometry&, const FDragDropEvent& Ev) -> FReply
                    {
                        return AssignImageDropToSlot(ActiveViewState, Tag, Ev)
                            ? FReply::Handled() : FReply::Unhandled();
                    })
                    [Row]]];
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
            // P7-F: blink frames are dedicated drop hosts — dropping a
            // texture (content-browser or files, assigned by channel suffix)
            // lands on exactly this frame.
            TSharedRef<SFaceDropTarget> FrameDrop = SNew(SFaceDropTarget)
                .OnFaceDragOver_Lambda([](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                {
                    if (Evt.GetOperationAs<FAssetDragDropOp>().IsValid()) return FReply::Handled();
                    if (Evt.GetOperationAs<FContentBrowserDataDragDropOp>().IsValid()) return FReply::Handled();
                    TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
                    if (FileOp.IsValid() && FileOp->HasFiles())
                    {
                        for (const FString& File : FileOp->GetFiles())
                            if (IsDroppableImageFile(File)) return FReply::Handled();
                    }
                    return FReply::Unhandled();
                })
                .OnFaceDrop_Lambda([this, Idx](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                {
                    return AssignImageDropToBlinkFrame(ActiveViewState, SelectedLayerName, Idx, Evt)
                        ? FReply::Handled() : FReply::Unhandled();
                })
                [FrameBox];
            FrameRow->AddSlot().Padding(FMargin(1))[FrameDrop];
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

bool UFaceParallaxEditorWidget::AssignImageDropToSlot(EFaceAngleState State, const FName& LayerTag,
    const FDragDropEvent& Ev)
{
    // P7-F shared drop pipeline: layer rows, the assign grid, the hull cells
    // and the slot props all route dropped textures through here. Dropping
    // multiple assets with channel suffixes fills all of Albedo/Normal/Depth.
    if (!LayerTag.IsValid())
    {
        SetStatus(TEXT("Drop ignored: no target layer"), FLinearColor::Yellow);
        return false;
    }
    int32 Assigned = 0;
    TArray<UTexture2D*> Dropped;
    if (TSharedPtr<FAssetDragDropOp> AssetOp = Ev.GetOperationAs<FAssetDragDropOp>())
    {
        for (const FAssetData& Asset : AssetOp->GetAssets())
        {
            if (UTexture2D* T = Cast<UTexture2D>(Asset.GetAsset())) Dropped.Add(T);
        }
    }
    else if (TSharedPtr<FContentBrowserDataDragDropOp> DataOp = Ev.GetOperationAs<FContentBrowserDataDragDropOp>())
    {
        for (const FAssetData& Asset : DataOp->GetAssets())
        {
            if (UTexture2D* T = Cast<UTexture2D>(Asset.GetAsset())) Dropped.Add(T);
        }
    }
    if (TSharedPtr<FExternalDragOperation> FileOp = Ev.GetOperationAs<FExternalDragOperation>())
    {
        TArray<FString> Files;
        if (FileOp.IsValid() && FileOp->HasFiles())
        {
            for (const FString& File : FileOp->GetFiles())
            {
                if (IsDroppableImageFile(File)) Files.Add(File);
            }
        }
        if (Files.Num() > 0) Dropped.Append(ImportTexturesFromFiles(Files));
    }
    if (Dropped.Num() > 0)
    {
        FWidgetUndoScope UndoScope(this, TEXT("Assign Dropped Textures"));
        for (UTexture2D* Tex : Dropped)
        {
            const FString Channel = ChannelFromTextureName(Tex->GetName());
            if (AssignTextureToSlot(Tex, State, LayerTag, Channel)) ++Assigned;
        }
    }
    if (Assigned > 0)
    {
        SetStatus(FString::Printf(TEXT("Drop: assigned %d texture(s) to %s by channel suffix"),
            Assigned, *LayerTag.ToString()), FLinearColor(0.3f, 1.0f, 0.3f));
        RefreshUI();
    }
    else if (Dropped.Num() > 0)
    {
        SetStatus(TEXT("Drop imported but nothing assigned"), FLinearColor::Red);
    }
    return Assigned > 0;
}

bool UFaceParallaxEditorWidget::AssignImageDropToBlinkFrame(EFaceAngleState State, const FName& Tag,
    int32 FrameIdx, const FDragDropEvent& Ev)
{
    if (!Tag.IsValid() || FrameIdx < 0)
    {
        SetStatus(TEXT("Drop ignored: no target frame"), FLinearColor::Yellow);
        return false;
    }
    TArray<UTexture2D*> Dropped;
    if (TSharedPtr<FAssetDragDropOp> AssetOp = Ev.GetOperationAs<FAssetDragDropOp>())
    {
        for (const FAssetData& Asset : AssetOp->GetAssets())
        {
            if (UTexture2D* T = Cast<UTexture2D>(Asset.GetAsset())) Dropped.Add(T);
        }
    }
    else if (TSharedPtr<FContentBrowserDataDragDropOp> DataOp = Ev.GetOperationAs<FContentBrowserDataDragDropOp>())
    {
        for (const FAssetData& Asset : DataOp->GetAssets())
        {
            if (UTexture2D* T = Cast<UTexture2D>(Asset.GetAsset())) Dropped.Add(T);
        }
    }
    if (TSharedPtr<FExternalDragOperation> FileOp = Ev.GetOperationAs<FExternalDragOperation>())
    {
        TArray<FString> Files;
        if (FileOp.IsValid() && FileOp->HasFiles())
        {
            for (const FString& File : FileOp->GetFiles())
            {
                if (IsDroppableImageFile(File)) Files.Add(File);
            }
        }
        if (Files.Num() > 0) Dropped.Append(ImportTexturesFromFiles(Files));
    }
    if (Dropped.Num() > 0)
    {
        FFaceTextureSet Set = GetBlinkFrameTextures(State, Tag, FrameIdx);
        FWidgetUndoScope Scope(this, TEXT("Assign Dropped Blink Textures"));
        for (UTexture2D* Tex : Dropped)
        {
            const FString Channel = ChannelFromTextureName(Tex->GetName());
            if (Channel == TEXT("Normal")) Set.Normal = Tex;
            else if (Channel == TEXT("Depth")) Set.Depth = Tex;
            else Set.Albedo = Tex;
        }
        SetBlinkFrameTextures(State, Tag, FrameIdx, Set);
        SetStatus(FString::Printf(TEXT("Dropped %d texture(s) onto blink frame %d"), Dropped.Num(), FrameIdx),
            FLinearColor(0.3f, 1.0f, 0.3f));
        RefreshUI();
        return true;
    }
    return false;
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

    // Data rows — one per layer, carousel-paged (P17/P18): the matrix's natural
    // height (header 28px + one 44px row per layer) is UNBOUNDED, so the real
    // table must page layer rows inside the fixed StatusMatrixHeaderH-viewport
    // budget or the last layer row ("Hair") slides under the terminal section.
    // The viewport clips the whole grid; each page re-renders the 28px header
    // row plus StatusMatrixRowsPerPage data rows (3 x 44 = 132 <= 176 content).
    {
        const int32 RowsPerPage = FPLayout::StatusMatrixRowsPerPage;
        const int32 TotalRows = Tags.Num();
        const int32 TotalPages = FMath::Max(1, FMath::DivideAndRoundUp(TotalRows, RowsPerPage));
        StatusMatrixPageIndex = FMath::Clamp(StatusMatrixPageIndex, 0, TotalPages - 1);
        if (StatusMatrixPageLabel.IsValid())
            StatusMatrixPageLabel->SetText(FText::FromString(FString::Printf(TEXT("Page %d/%d"),
                StatusMatrixPageIndex + 1, TotalPages)));
        const int32 Start = StatusMatrixPageIndex * RowsPerPage;
        const int32 End = FMath::Min(Start + RowsPerPage, TotalRows);
        for (int32 li = Start; li < End; ++li)
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
    ExpandAccordion(ArtAccordion);
    ExpandAccordion(NestedAccordion);
    ExpandAccordion(DiagnosticsAccordion);

    for (int32 Ri = 0; Ri < PropTabContent.Num(); ++Ri)
    {
        // Accordion-managed rails (Art/Animated/NestedPins/Diagnostics = tabs
        // 2,3,4,6) are expanded above; plain-section rails (Layers/Camera =
        // tabs 1,5) still use the visibility-toggle contract. Slot 0 is the
        // shared right pane - never visibility-toggled.
        if (Ri == 0 || Ri == 2 || Ri == 3 || Ri == 4 || Ri == 6) continue;
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
        // Unified inspect-mode row (Phase C) + P5 promotion/demotion: the
        // 5-way Textured / Outline / Depth / Wireframe / Heatmap selector is
        // the PRIMARY canvas control — large, clearly-labeled segmented
        // buttons. Onion-skin, Show Pins, Depth overlay and the Filter row
        // demote into a collapsed "Canvas Options" overflow menu. The
        // Diagnostics rail Config checks stay the single source of truth (P3
        // dedupe): the row only applies canonical combos via SetInspectMode,
        // and the highlights derive from the toggles via DeriveInspectMode.
        {
            TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
            ModeRow->AddSlot().Padding(FMargin(0,2)).AutoWidth().VAlign(VAlign_Center)
                [MakeLbl(TEXT("Preview:"), 9, FLinearColor(0.85f,0.85f,0.85f))];
            for (int32 M = 0; M < 5; ++M)
            {
                const int32 MM = M;
                ModeRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [SNew(SBox).WidthOverride(76).HeightOverride(24)
                        [SNew(SButton)
                            .ButtonColorAndOpacity_Lambda([this, MM]()
                            {
                                return InspectMode == MM ? AccentBlue() : FLinearColor(0.13f, 0.13f, 0.15f);
                            })
                            .OnClicked_Lambda([this, MM](){ SetInspectMode(MM); return FReply::Handled(); })
                            .Content()
                            [SNew(STextBlock)
                                .Text(FText::FromString(UTF8_TO_TCHAR(FPLayout::InspectModeLabel(MM))))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                                .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f))]]];
            }
            // P5: Canvas Options overflow — the demoted preview modifiers live
            // in one collapsed menu; the menu is rebuilt fresh on every open so
            // checkbox states always reflect the current model.
            ModeRow->AddSlot().Padding(FMargin(8,2)).AutoWidth().VAlign(VAlign_Center)
                [SNew(SMenuAnchor)
                    .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
                    {
                        TSharedRef<SVerticalBox> Opts = SNew(SVerticalBox);
                        // Onion skin: checkbox + slider + opacity label.
                        {
                            TSharedRef<SCheckBox> OnionCheck = SNew(SCheckBox)
                                .IsChecked(bOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                                { ToggleOnionSkin(S == ECheckBoxState::Checked); RefreshUI(); });
                            OnionCheck->SetToolTipText(FText::FromString(TEXT(
                                "Onion skin: ghosts the adjacent view's albedo at low opacity for alignment")));
                            OnionCheckBox = OnionCheck;
                            TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
                            Row->AddSlot().Padding(FMargin(4,3)).AutoWidth().VAlign(VAlign_Center)[OnionCheck];
                            Row->AddSlot().Padding(FMargin(2,3)).AutoWidth().VAlign(VAlign_Center)
                                [MakeLbl(TEXT("Onion skin"), 8, FLinearColor(0.8f,0.8f,0.8f))];
                            Row->AddSlot().Padding(FMargin(6,3)).FillWidth(1.0f).VAlign(VAlign_Center)
                                [SNew(SSlider).Value(OnionSkinOpacity)
                                    .OnValueChanged_Lambda([this](float V){ SetOnionSkinOpacity(V); })];
                            Row->AddSlot().Padding(FMargin(4,3)).AutoWidth().VAlign(VAlign_Center)
                                [MakeLbl(TEXT("opacity"), 8, FLinearColor(0.6f,0.6f,0.6f))];
                            Opts->AddSlot().AutoHeight()
                                [SNew(SBox).WidthOverride(240)[Row]];
                        }
                        // Show Pins: paint all nested-element pin markers.
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
                            TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
                            Row->AddSlot().Padding(FMargin(4,3)).AutoWidth().VAlign(VAlign_Center)[ShowPinsCheck];
                            Row->AddSlot().Padding(FMargin(2,3)).AutoWidth().VAlign(VAlign_Center)
                                [MakeLbl(TEXT("Show Pins"), 8, FLinearColor(0.8f,0.8f,0.8f))];
                            Opts->AddSlot().AutoHeight()[Row];
                        }
                        // Depth overlay: composite the layer's depth map.
                        {
                            TSharedRef<SCheckBox> DepthCheck = SNew(SCheckBox)
                                .IsChecked(bDepthOverlayVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                                {
                                    ToggleDepthOverlay(S == ECheckBoxState::Checked);
                                });
                            DepthCheck->SetToolTipText(FText::FromString(TEXT(
                                "Depth Overlay: composite the selected layer's depth map over the live preview")));
                            CheckDepthOverlay = DepthCheck;
                            TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
                            Row->AddSlot().Padding(FMargin(4,3)).AutoWidth().VAlign(VAlign_Center)[DepthCheck];
                            Row->AddSlot().Padding(FMargin(2,3)).AutoWidth().VAlign(VAlign_Center)
                                [MakeLbl(TEXT("Depth overlay"), 8, FLinearColor(0.8f,0.8f,0.8f))];
                            Opts->AddSlot().AutoHeight()[Row];
                        }
                        // Phase I edge map: group-colored part edges (eyes /
                        // mouth / hair / surface), depth-class luminance
                        // (front lighter than back). Hair edges are a
                        // sub-toggle that only matters while the map is on.
                        {
                            TSharedRef<SCheckBox> EdgeMapCheck = SNew(SCheckBox)
                                .IsChecked(bSchematicEdgeMap ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                                {
                                    SetSchematicEdgeMap(S == ECheckBoxState::Checked);
                                });
                            EdgeMapCheck->SetToolTipText(FText::FromString(TEXT(
                                "Edge Map: color every part edge by group (eyes, mouth, hair, surface) with front lighter than back")));
                            TSharedRef<SCheckBox> HairCheck = SNew(SCheckBox)
                                .IsChecked(bEdgeMapHairEdges ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                                {
                                    SetEdgeMapHairEdges(S == ECheckBoxState::Checked);
                                });
                            HairCheck->SetToolTipText(FText::FromString(TEXT(
                                "Hair Edges: show the hair system's detailed edge levels (Bangs/Hair/BackHair) in the edge map")));
                            TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
                            {
                                TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
                                Row->AddSlot().Padding(FMargin(4,3)).AutoWidth().VAlign(VAlign_Center)[EdgeMapCheck];
                                Row->AddSlot().Padding(FMargin(2,3)).AutoWidth().VAlign(VAlign_Center)
                                    [MakeLbl(TEXT("Edge map"), 8, FLinearColor(0.8f,0.8f,0.8f))];
                                Rows->AddSlot().AutoHeight()[Row];
                            }
                            {
                                TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
                                Row->AddSlot().Padding(FMargin(4,3)).AutoWidth().VAlign(VAlign_Center)[HairCheck];
                                Row->AddSlot().Padding(FMargin(2,3)).AutoWidth().VAlign(VAlign_Center)
                                    [MakeLbl(TEXT("Hair edges"), 8, FLinearColor(0.8f,0.8f,0.8f))];
                                Rows->AddSlot().AutoHeight()[Row];
                            }
                            Opts->AddSlot().AutoHeight()
                                [SNew(SBox).WidthOverride(240)[Rows]];
                        }
                        // Filter row: shows/hides the schematic filter row.
                        {
                            TSharedRef<SCheckBox> FilterCheck = SNew(SCheckBox)
                                .IsChecked(bSchematicFilterVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                                .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                                {
                                    bSchematicFilterVisible = (S == ECheckBoxState::Checked);
                                });
                            FilterCheck->SetToolTipText(FText::FromString(TEXT(
                                "Filter: show/hide the schematic filter row (depth classes, layer chips, Focus zoom, Clear)")));
                            TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
                            Row->AddSlot().Padding(FMargin(4,3)).AutoWidth().VAlign(VAlign_Center)[FilterCheck];
                            Row->AddSlot().Padding(FMargin(2,3)).AutoWidth().VAlign(VAlign_Center)
                                [MakeLbl(TEXT("Filter row"), 8, FLinearColor(0.8f,0.8f,0.8f))];
                            Opts->AddSlot().AutoHeight()[Row];
                        }
                        return Opts;
                    })
                    [SNew(SButton)
                        .ButtonColorAndOpacity(FLinearColor(0.12f,0.12f,0.14f))
                        .Content()
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)
                                [MakeLbl(TEXT("Canvas Options"), 8, FLinearColor(0.75f,0.75f,0.75f))]
                            + SHorizontalBox::Slot().Padding(FMargin(2,2,6,2)).AutoWidth().VAlign(VAlign_Center)
                                [MakeLbl(TEXT("\u25BE"), 8, FLinearColor(0.6f,0.6f,0.6f))]]]];
            ModeRow->AddSlot().FillWidth(1.0f);
            CenterCol->AddSlot().AutoHeight().Padding(FMargin(2,2,2,0))
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.1f,0.1f,0.1f))
                    .Padding(FMargin(2,1))
                    [ModeRow]];
            // Redesign: schematic filter row (Phase 3) — depth radio (All/
            // Front/Base/Back), one toggle chip per base-preset layer (colored
            // by depth class), the Focus zoom toggle, and Clear. Lives
            // between the mode row and the canvas; hidden via the Filter
            // checkbox on the mode row.
            SchematicFilterBox = SNew(SWrapBox).UseAllottedSize(true);
            CenterCol->AddSlot().AutoHeight().Padding(FMargin(2,0,2,0))
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.09f,0.09f,0.09f))
                    .Padding(FMargin(2,1))
                    .Visibility_Lambda([this]()
                    {
                        return bSchematicFilterVisible ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                    [SchematicFilterBox.ToSharedRef()]];
            RebuildSchematicFilterRow();
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
        // Redesign: depth-map composite over the live preview (Depth checkbox).
        DepthOverlayImage = SNew(SImage)
            .Image(&DepthOverlayBrush)
            .ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.55f))
            .Visibility_Lambda([this]()
            {
                return bDepthOverlayVisible && DepthOverlayTexture ? EVisibility::Visible : EVisibility::Collapsed;
            });
        GizmoWidget = SNew(SFaceLayerGizmo);
        GizmoWidget->Owner = this;
        // Phase 0: the gizmo may never intercept a click — it is paint-only
        // (SelfHitTestInvisible on both the SBox and the leaf).
        GizmoWidget->SetVisibility(EVisibility::SelfHitTestInvisible);
        GizmoLayer = SNew(SBox)
            .Visibility_Lambda([this]() { return SelectedLayerName.IsValid() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
            [GizmoWidget.ToSharedRef()];
        HotspotLayer = SNew(SFaceHotspotLayer);
        HotspotLayer->Owner = this;
        HotspotLayer->SetRegions(FPLayout::DefaultHotspotRegions());
        // Redesign: the schematic default-view layer — P1 one-map: paints
        // EVERY part glyph above the edge overlay but below the hotspot
        // layer, so the hotspot routes every click (pin-drag -> glyph ->
        // miss) and forwards hover here. SelfHitTestInvisible: never
        // hit-testable itself, so it can never become a dead-click swallow.
        SchematicLayer = SNew(SFaceSchematicLayer);
        SchematicLayer->Owner = this;
        SchematicLayer->SetVisibility(EVisibility::SelfHitTestInvisible);
        HotspotLayer->SetSchematicLayer(SchematicLayer);
        PreviewHost = SNew(SBox)
            .WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return FOptionalSize(CanvasHeight); }))
            .HeightOverride(TAttribute<FOptionalSize>::CreateLambda([this]() { return FOptionalSize(CanvasHeight); }))
            [SNew(SOverlay)
                + SOverlay::Slot()[PreviewImageWidget.ToSharedRef()]
                + SOverlay::Slot()[OutlinePreviewImage.ToSharedRef()]
                + SOverlay::Slot()[OnionSkinImage.ToSharedRef()]
                + SOverlay::Slot()[EdgeOverlayImage.ToSharedRef()]
                + SOverlay::Slot()[DepthOverlayImage.ToSharedRef()]
                + SOverlay::Slot()[SchematicLayer.ToSharedRef()]
                + SOverlay::Slot()[HotspotLayer.ToSharedRef()]
                + SOverlay::Slot()[GizmoLayer.ToSharedRef()]];
        // P23: the canvas is aspect-locked SQUARE (mirrors the 1024x1024 render
        // target) — the face schematic can never be stretched; the slot centers
        // the 450px box in the center column.
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(2)).HAlign(HAlign_Center)
            [PreviewHost.ToSharedRef()];
        // Resizing is only allowed at the very outside of the widget (the
        // window/tab edge — no internal splitter anywhere, rail included).
        // The canvas itself is fixed at the 450px design constant: an interior
        // drag-resize handle here let the canvas grow past the MainRow band
        // and broke the paged
        // carousels + pushed the rows under the terminal (P24 defect class).

        // P7-A legend: one always-visible line under the canvas stating the
        // one-map interaction (the schematic glyphs and the part chips below
        // share this model). Mirrors the P1 comment contract; kept outside
        // the manifest like the parts strip it annotates.
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(4, 2, 4, 0))
            [SNew(STextBlock)
                .Text(FText::FromString(TEXT("Click a part to assign art \xB7 Right-click to remap")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                .ColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.78f))];

        // Phase I edge map legend: group-color key under the canvas, visible
        // only while the edge map is on. Swatches come from the same pure
        // contract the paint path uses (FPEdgeColorForPart), so the legend
        // shows exactly what the glyphs paint — including the hair system's
        // three detailed levels (Bangs lightest -> BackHair darkest).
        {
            auto Swatch = [](const FPSchematic::FPEdgeColor& C, const char* Label)
            {
                return SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(SBox)
                            .WidthOverride(8.0f)
                            .HeightOverride(8.0f)
                            [SNew(SImage)
                                .Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                .ColorAndOpacity(FLinearColor((float)C.R, (float)C.G, (float)C.B, 1.0f))]]
                    + SHorizontalBox::Slot().Padding(FMargin(3, 0, 8, 0)).AutoWidth().VAlign(VAlign_Center)
                        [MakeLbl(Label, 8, FLinearColor(0.78f, 0.78f, 0.82f))];
            };
            CenterCol->AddSlot().AutoHeight().Padding(FMargin(4, 0, 4, 0))
                [SNew(SBox)
                    .Visibility_Lambda([this]()
                    {
                        return bSchematicEdgeMap ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 6, 0))
                            [MakeLbl(TEXT("Edge map:"), 8, FLinearColor(0.6f, 0.6f, 0.66f))]
                        + SHorizontalBox::Slot().AutoWidth()
                            [Swatch(FPSchematic::FPEdgeColorForPart("EyeL", FPSchematic::FPDepthClass::Front), "Eyes")]
                        + SHorizontalBox::Slot().AutoWidth()
                            [Swatch(FPSchematic::FPEdgeColorForPart("Mouth", FPSchematic::FPDepthClass::Front), "Mouth")]
                        + SHorizontalBox::Slot().AutoWidth()
                            [Swatch(FPSchematic::FPEdgeColorForPart("Bangs", FPSchematic::FPDepthClass::Front), "Bangs")]
                        + SHorizontalBox::Slot().AutoWidth()
                            [Swatch(FPSchematic::FPEdgeColorForPart("Hair", FPSchematic::FPDepthClass::Back), "Hair")]
                        + SHorizontalBox::Slot().AutoWidth()
                            [Swatch(FPSchematic::FPEdgeColorForPart("BackHair", FPSchematic::FPDepthClass::Back), "Back")]
                        + SHorizontalBox::Slot().AutoWidth()
                            [Swatch(FPSchematic::FPEdgeColorForPart("Head", FPSchematic::FPDepthClass::Back), "Surface")]]];
        }

        // P1 one-map legend strip: the 17 schematic part chips under the
        // canvas (RebuildPartsStrip). Each chip shows 'Part -> Layer' and
        // mirrors the glyph map: left-click selects/imports, right-click
        // remaps. Trailing button: Cycle Preview (Phase 2) — blink/
        // expression/viseme/orbit sweep tour of the live animation systems.
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
        // P7-C: the Pin Mode toggle is gone — pin placement/move is always
        // live and context-derived (see RefreshUI). A click on a pin handle
        // moves that pin; clicks elsewhere select parts.
        // The strip row is bound to ONE 26px clipped row (manifest
        // CN-PartsStrip): wrapping chips can never grow past the main-row
        // height and spill over the diagnostic log below.
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(2, 0, 2, 2))
            [SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.09f))
                .Padding(FMargin(2, 1))
                [SNew(SBox)
                    .HeightOverride(26.0f)
                    .Clipping(EWidgetClipping::ClipToBounds)
                    [StripRow]]];

        // Layer label + P1 breadcrumb ('Front → Eyes'): the visible tab-switch
        // transition showing where the picked part sits in the map.
        TextLayerName = SNew(STextBlock)
            .Text(FText::FromString(SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : TEXT("(no layer)")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            .ColorAndOpacity(FLinearColor(0.9f,0.9f,0.9f));
        BreadcrumbText = SNew(STextBlock)
            .Text(FText::FromString(SchematicBreadcrumb))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
            .ColorAndOpacity(FLinearColor(0.65f, 0.65f, 0.72f));
        TSharedRef<SHorizontalBox> LabelRow = SNew(SHorizontalBox);
        LabelRow->AddSlot().AutoWidth().VAlign(VAlign_Center)[TextLayerName.ToSharedRef()];
        LabelRow->AddSlot().Padding(FMargin(8, 0, 0, 0)).AutoWidth().VAlign(VAlign_Center)
            [BreadcrumbText.ToSharedRef()];
        LabelRow->AddSlot().FillWidth(1.0f);
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(4,2,4,0))
            [LabelRow];
    }

    return CenterCol;
}

// Phase 2: recompute the canvas hotspot outlines for the current view.
// Each region whose hotspot maps to a layer is transformed by that layer's
// effective transform (master-material UV chain mirror); unmapped regions
// keep the default front-facing template pose. Phase C: also rebuilds the
// per-layer art quads (draw order = layer list order, last on top) used for
// canvas click-to-select and the persistent selection outline — both are
// computed from the ACTIVE VIEW STATE's stored transforms, so the outline
// hugs the art in Profile/Back/¾/Top/Bottom too (cross-view constraint).
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
    std::vector<FPLayout::FPLayerQuad> Quads;
    TArray<FString> Tags;
    if (ActivePreset)
    {
        for (const FName& Tag : GetUILayerTags())
        {
            const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(ActiveViewState, Tag);
            const FFaceArtTransform T = ArtSlot.GetEffectiveTransform(ActiveViewState);
            Quads.push_back(FPLayout::FLayerQuadFromTransform(
                T.Position.X, T.Position.Y, T.Scale.X, T.Scale.Y, T.Rotation));
            Tags.Add(Tag.ToString());
        }
    }
    HotspotLayer->SetLayerQuads(Quads, Tags);
    HotspotLayer->SetSelectedLayerTag(SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : FString());
}

// Redesign: repaint the canvas schematic default view. P1 one-map: EVERY
// part keeps its glyph — artful parts draw solid (their art replaced the
// outline on the live preview, but the map stays complete and clickable),
// artless parts stay dashed. Glyphs are transformed by the mapped layer's
// effective transform for the active view state (same UV chain as
// RefreshHotspotRegions, so the schematic hugs the rendered head in every
// view). Unmapped parts keep the default template pose. Phase 0/3: also
// feeds the resolved per-part layer tags (selection emphasis + filter drops)
// and the Focus lens box (the selected layer's transformed glyph bounds).
// Runs on every RefreshUI.
void UFaceParallaxEditorWidget::RefreshSchematic()
{
    if (!SchematicLayer.IsValid()) return;
    std::vector<FPSchematic::FPSchematicPart> Parts = FPSchematic::DefaultPartSchematics();
    std::vector<std::string> Tags;
    std::vector<char> PartStatus;
    Tags.reserve(Parts.size());
    PartStatus.reserve(Parts.size());
    double BMinX = 2.0, BMinY = 2.0, BMaxX = -1.0, BMaxY = -1.0;
    bool bHaveFocusBox = false;
    const FString SelTag = SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : FString();
    if (ActivePreset)
    {
        for (FPSchematic::FPSchematicPart& P : Parts)
        {
            if (!P.Name || !P.Name[0] || P.Outline.empty())
            {
                Tags.emplace_back();
                PartStatus.push_back(0);
                continue;
            }
            const FString PartName = UTF8_TO_TCHAR(P.Name);
            const FName LayerTag = ResolveHotspotLayer(PartName);
            if (!LayerTag.IsValid())
            {
                Tags.emplace_back();
                PartStatus.push_back(0);
                continue;
            }
            const std::string ResolvedTag = TCHAR_TO_UTF8(*LayerTag.ToString());
            // P2 per-part status chip: slot completeness (A/N/D) in the
            // ACTIVE view, mirroring the Bulk Assign cell state.
            const FFaceTextureSet Tex = ActivePreset->GetSlot(ActiveViewState, LayerTag).Textures;
            PartStatus.push_back((char)FPLayout::AssignCellState(
                Tex.Albedo != nullptr, Tex.Normal != nullptr, Tex.Depth != nullptr));
            const FFaceArtSlot& ArtSlot = ActivePreset->GetSlot(ActiveViewState, LayerTag);
            const FFaceArtTransform T = ArtSlot.GetEffectiveTransform(ActiveViewState);
            FPSchematic::FPSchematicPart Transformed;
            Transformed.Name = P.Name;
            Transformed.DepthClass = P.DepthClass;
            Transformed.Outline.reserve(P.Outline.size());
            const bool bInFocusBox = bSchematicFocus && !SelTag.IsEmpty() && LayerTag.ToString() == SelTag;
            for (const FPSchematic::FPSchematicPoint& Pt : P.Outline)
            {
                const FPLayout::FPHotspotPoint H = FPLayout::FPHotspotTransformPoint(
                    FPLayout::FPHotspotPoint{ Pt.X, Pt.Y }, T.Position.X, T.Position.Y,
                    T.Scale.X, T.Scale.Y, T.Rotation);
                Transformed.Outline.push_back({ H.X, H.Y });
                if (bInFocusBox)
                {
                    BMinX = FMath::Min(BMinX, H.X);
                    BMaxX = FMath::Max(BMaxX, H.X);
                    BMinY = FMath::Min(BMinY, H.Y);
                    BMaxY = FMath::Max(BMaxY, H.Y);
                    bHaveFocusBox = true;
                }
            }
            P = Transformed;
            Tags.push_back(ResolvedTag);
        }
    }
    SchematicLayer->SetParts(Parts);
    SchematicLayer->SetPartLayerTags(Tags);
    SchematicLayer->SetPartStatus(PartStatus);
    SchematicLayer->SetEdgeMap(bSchematicEdgeMap, bEdgeMapHairEdges);
    std::vector<std::string> LayerFilter;
    LayerFilter.reserve(SchematicLayerFilter.Num());
    for (const FString& F : SchematicLayerFilter)
        LayerFilter.emplace_back(TCHAR_TO_UTF8(*F));
    SchematicLayer->SetFilters(LayerFilter, SchematicDepthFilter);
    if (bSchematicFocus && bHaveFocusBox)
        SchematicLayer->SetFocus(true, { BMinX, BMinY }, { BMaxX, BMaxY });
    else
        SchematicLayer->SetFocus(false, { 0.0, 0.0 }, { 1.0, 1.0 });
}

// Redesign: rebuild the schematic filter row (Phase 3). Depth radio chips
// (All/Front/Base/Back) drive the depth class filter; one toggle chip per
// base-preset layer (colored by its depth class — amber/grey/cyan, the same
// encoding as the glyphs) drives the layer multi-select; Focus toggles the
// zoom-to-fit lens; Clear resets everything. Every chip re-enters through the
// widget's public filter methods, so the schematic repaint always mirrors
// the row exactly.
void UFaceParallaxEditorWidget::RebuildSchematicFilterRow()
{
    if (!SchematicFilterBox.IsValid()) return;
    SchematicFilterBox->ClearChildren();
    auto Chip = [this](const FString& Label, FLinearColor Bg, bool bActive,
        TFunction<void()> OnClick, const FString& Tip) -> TSharedRef<SWidget>
    {
        const FLinearColor Fg = bActive
            ? FLinearColor(1.0f, 1.0f, 1.0f)
            : FLinearColor(0.75f, 0.75f, 0.78f);
        return SNew(SButton)
            .ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
            .ContentPadding(FMargin(3, 1))
            .OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
            .ToolTipText(FText::FromString(Tip))
            [SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(bActive ? Bg : Bg * 0.45f)
                .Padding(FMargin(5, 2))
                [SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .Font(FCoreStyle::GetDefaultFontStyle(bActive ? "Bold" : "Regular", 8))
                    .ColorAndOpacity(Fg)]];
    };
    static const FLinearColor Amber(1.0f, 0.72f, 0.25f);   // Front
    static const FLinearColor Grey(0.5f, 0.5f, 0.55f);     // Base
    static const FLinearColor Cyan(0.35f, 0.85f, 1.0f);    // Back

    SchematicFilterBox->AddSlot().Padding(FMargin(1))[MakeLbl(TEXT("Filter:"), 8, FLinearColor(0.6f,0.6f,0.6f))];
    const TFunction<void(int32)> DepthClick = [this](int32 D) { SetSchematicDepthFilter(D); };
    SchematicFilterBox->AddSlot().Padding(FMargin(1))[
        Chip(TEXT("All"), Grey, SchematicDepthFilter == 0, [this]() { SetSchematicDepthFilter(0); },
            TEXT("All: show every depth class"))];
    SchematicFilterBox->AddSlot().Padding(FMargin(1))[
        Chip(TEXT("Front"), Amber, SchematicDepthFilter == 1, [DepthClick]() { DepthClick(1); },
            TEXT("Front: only layers that move WITH yaw (amber)"))];
    SchematicFilterBox->AddSlot().Padding(FMargin(1))[
        Chip(TEXT("Base"), Grey, SchematicDepthFilter == 2, [DepthClick]() { DepthClick(2); },
            TEXT("Base: only anchored layers (grey)"))];
    SchematicFilterBox->AddSlot().Padding(FMargin(1))[
        Chip(TEXT("Back"), Cyan, SchematicDepthFilter == 3, [DepthClick]() { DepthClick(3); },
            TEXT("Back: only layers that move AGAINST yaw (cyan)"))];
    SchematicFilterBox->AddSlot().Padding(FMargin(1))[
        Chip(TEXT("|"), Grey, false, []() {}, TEXT(""))];
    for (const std::string& Tag : FPSchematic::FPSchematicLayerSet())
    {
        const FString Label = UTF8_TO_TCHAR(Tag.c_str());
        const FLinearColor Col = FPSchematic::FPDepthClassForTag(Tag.c_str()) == FPSchematic::FPDepthClass::Front
            ? Amber : (FPSchematic::FPDepthClassForTag(Tag.c_str()) == FPSchematic::FPDepthClass::Back ? Cyan : Grey);
        const bool bActive = SchematicLayerFilter.Contains(Label);
        SchematicFilterBox->AddSlot().Padding(FMargin(1))[
            Chip(Label, Col, bActive, [this, Label]() { ToggleSchematicLayerFilter(Label); },
                FString::Printf(TEXT("Toggle '%s' in the schematic filter (empty = all layers)"), *Label))];
    }
    SchematicFilterBox->AddSlot().Padding(FMargin(1))[
        Chip(TEXT("Focus"), Amber, bSchematicFocus, [this]() { ToggleSchematicFocus(); },
            TEXT("Focus: zoom-to-fit the selected layer's glyphs"))];
    SchematicFilterBox->AddSlot().Padding(FMargin(1))[
        Chip(TEXT("Clear"), Grey, false, [this]() { ClearSchematicFilters(); },
            TEXT("Clear: reset the depth radio and all layer chips"))];
}

// P1 one-map legend: the 17 schematic part chips under the canvas mirror the
// glyph map EXACTLY (same part set, same colors: stable per-layer hue, dark
// gray = unmapped), so the strip reads as the map's legend: 'Part -> Layer'.
// Chip color reflects the mapped layer (stable per-layer hue from
// GetTypeHash); unmapped parts are dark gray. Left-click = the canvas glyph
// behavior (select the mapped layer + import wizard for artless layers);
// right-click opens the remap menu. Called on build and on every RefreshUI
// so explicit HotspotLayerMap edits repaint immediately.
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
    for (const FPSchematic::FPSchematicPart& P : FPSchematic::DefaultPartSchematics())
    {
        if (!P.Name || !P.Name[0]) continue;
        const FString PartName = UTF8_TO_TCHAR(P.Name);
        const FName LayerTag = ResolveHotspotLayer(PartName);
        const bool bMapped = LayerTag.IsValid();
        // P2 per-part status chip: completeness (A/N/D) of the mapped layer
        // in the ACTIVE view (green full / amber partial / red missing).
        FLinearColor StatusCol(0.45f, 0.45f, 0.5f);
        FString StatusTxt = TEXT("unmapped");
        if (bMapped && ActivePreset)
        {
            const FFaceTextureSet Tex = ActivePreset->GetSlot(ActiveViewState, LayerTag).Textures;
            const bool bA = Tex.Albedo != nullptr, bN = Tex.Normal != nullptr, bD = Tex.Depth != nullptr;
            const int32 Cell = FPLayout::AssignCellState(bA, bN, bD);
            StatusCol = Cell == 2 ? FLinearColor(0.35f, 0.85f, 0.45f)
                : (Cell == 1 ? FLinearColor(1.0f, 0.75f, 0.25f) : FLinearColor(0.8f, 0.35f, 0.35f));
            StatusTxt = FString::Printf(TEXT("%s (A/N/D: %c%c%c)"),
                UTF8_TO_TCHAR(FPLayout::AssignCellLabel(Cell)),
                bA ? TCHAR('A') : TCHAR('-'), bN ? TCHAR('N') : TCHAR('-'), bD ? TCHAR('D') : TCHAR('-'));
        }
        const FString Tip = FString::Printf(TEXT("%s -> %s\n%s\nLeft: select + import art   Right-click: map to layer"),
            *PartName, bMapped ? *LayerTag.ToString() : TEXT("unmapped"), *StatusTxt);
        TSharedRef<SImage> StatusDot = SNew(SImage)
            .Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .ColorAndOpacity(StatusCol);
        PartsStrip->AddSlot().Padding(FMargin(1))
            [SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(PartsChipColor(LayerTag, bMapped))
                .Padding(FMargin(6, 3))
                .OnMouseButtonDown_Lambda([this, PartName](const FGeometry&, const FPointerEvent& E) -> FReply
                {
                    if (E.GetEffectingButton() == EKeys::RightMouseButton)
                    {
                        OpenHotspotRemapMenu(PartName, E);
                        return FReply::Handled();
                    }
                    HandleHotspotClick(PartName);
                    return FReply::Handled();
                })
                .ToolTipText(FText::FromString(Tip))
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 5, 0))
                        [SNew(SBox).WidthOverride(8).HeightOverride(8)[StatusDot]]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(STextBlock)
                            .Text(FText::FromString(bMapped
                                ? FString::Printf(TEXT("%s → %s"), *PartName, *LayerTag.ToString())
                                : FString::Printf(TEXT("%s → unmapped"), *PartName)))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                            .ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))]]];
    }
}

// P2: THE apply-to-views picker. One component, three mounts (state-tab
// "v" menu, Art rail Quick Actions, Bulk Assign ops row). It shows the 10
// view picks (SyncViewCheckBoxes - the same state the state-strip pick mode
// toggles) and applies the ACTIVE slot's transform + textures to them. All
// apply actions pass through the canonical Sync*/Duplicate*/Fill* API, so
// every copy path is one map.
TSharedRef<SWidget> UFaceParallaxEditorWidget::BuildApplyToViewsContent()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    const FString ActiveName = StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)ActiveViewState);
    Box->AddSlot().AutoHeight().Padding(FMargin(6, 4, 6, 2))
        [MakeLbl(FString::Printf(TEXT("Apply %s -> views:"), *ActiveName), 9, FLinearColor(0.9f, 0.8f, 0.5f))];
    struct { EFaceAngleState S; const TCHAR* T; } Picked[] = {
        {EFaceAngleState::Front, TEXT("Front")},
        {EFaceAngleState::ThreeQuarterRight, TEXT("3/4R")},
        {EFaceAngleState::RightProfile, TEXT("ProfR")},
        {EFaceAngleState::BackRight, TEXT("BackR")},
        {EFaceAngleState::Back, TEXT("Back")},
        {EFaceAngleState::BackLeft, TEXT("BackL")},
        {EFaceAngleState::ThreeQuarterLeft, TEXT("3/4L")},
        {EFaceAngleState::LeftProfile, TEXT("ProfL")},
        {EFaceAngleState::Top, TEXT("Top")},
        {EFaceAngleState::Bottom, TEXT("Bot")},
    };
    // 2x5 checkbox grid - toggles the SAME SyncViewCheckBoxes state as the
    // state-strip pick mode; the active view reads "This" and is excluded
    // from destinations.
    for (int32 Row = 0; Row < 5; ++Row)
    {
        TSharedRef<SHorizontalBox> GridRow = SNew(SHorizontalBox);
        for (int32 Col = 0; Col < 2; ++Col)
        {
            const int32 Idx = Row * 2 + Col;
            const EFaceAngleState S = Picked[Idx].S;
            const bool bSelf = (S == ActiveViewState);
            GridRow->AddSlot().AutoWidth().Padding(FMargin(2, 1))
                [SNew(SCheckBox)
                    .IsChecked_Lambda([this, S]()
                    {
                        return SyncViewCheckBoxes.IsValidIndex((int32)S)
                            && SyncViewCheckBoxes[(int32)S].IsValid()
                            && SyncViewCheckBoxes[(int32)S]->IsChecked()
                            ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this, S](ECheckBoxState NewState)
                    {
                        if (SyncViewCheckBoxes.IsValidIndex((int32)S) && SyncViewCheckBoxes[(int32)S].IsValid())
                            SyncViewCheckBoxes[(int32)S]->SetIsChecked(NewState == ECheckBoxState::Checked);
                    })];
            GridRow->AddSlot().AutoWidth().Padding(FMargin(0, 1, 6, 1)).VAlign(VAlign_Center)
                [MakeLbl(bSelf ? TEXT("This") : Picked[Idx].T, 8,
                    bSelf ? FLinearColor(0.9f, 0.8f, 0.5f) : FLinearColor(0.8f, 0.8f, 0.8f))];
        }
        Box->AddSlot().AutoHeight()[GridRow];
    }
    Box->AddSlot().AutoHeight().Padding(FMargin(2, 1))
        [MakeBtn(TEXT("Apply to picked views (transform + textures)"), [this]()
        {
            if (!SelectedLayerName.IsValid() || !ActivePreset) return;
            TArray<EFaceAngleState> Dests;
            for (int32 i = 0; i < SyncViewCheckBoxes.Num() && i < 10; ++i)
            {
                if (i == (int32)ActiveViewState) continue;
                if (SyncViewCheckBoxes[i].IsValid() && SyncViewCheckBoxes[i]->IsChecked())
                    Dests.Add((EFaceAngleState)i);
            }
            if (Dests.Num() == 0)
            {
                SetStatus(TEXT("No views picked - pick destinations or use All views"), FLinearColor::Yellow);
                return;
            }
            SyncLayerToSelectedViews(ActiveViewState, SelectedLayerName, Dests, true);
            RefreshUI();
        }, FLinearColor(0.6f, 0.9f, 1.0f))];
    Box->AddSlot().AutoHeight().Padding(FMargin(2, 1))
        [MakeBtn(TEXT("All views (transform + textures)"), [this]()
        {
            if (!SelectedLayerName.IsValid()) return;
            SyncLayerToAllViews(ActiveViewState, SelectedLayerName);
            SyncTexturesLayerToAllViews(ActiveViewState, SelectedLayerName);
            RefreshUI();
        }, FLinearColor(0.6f, 0.9f, 1.0f))];
    if (ActiveViewState != EFaceAngleState::Front)
    {
        Box->AddSlot().AutoHeight().Padding(FMargin(2, 1))
            [MakeBtn(TEXT("Copy from Front -> This"), [this]()
            {
                if (!SelectedLayerName.IsValid()) return;
                DuplicateState(EFaceAngleState::Front, ActiveViewState);
                RefreshUI();
            }, FLinearColor(0.9f, 0.85f, 0.7f))];
    }
    Box->AddSlot().AutoHeight().Padding(FMargin(2, 1))
        [MakeBtn(TEXT("Fill missing views from This"), [this]()
        {
            if (!SelectedLayerName.IsValid()) return;
            FillMissingViewsFromActiveSlot();
            RefreshUI();
        }, FLinearColor(0.7f, 0.9f, 0.7f))];
    return Box;
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
                const FString L = Label;
                Col->AddSlot().AutoHeight().Padding(FMargin(2))
                    [SNew(SFaceDropTarget)
                        .OnFaceDragOver_Lambda([](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                        {
                            if (Evt.GetOperationAs<FAssetDragDropOp>().IsValid()) return FReply::Handled();
                            if (Evt.GetOperationAs<FContentBrowserDataDragDropOp>().IsValid()) return FReply::Handled();
                            TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
                            if (FileOp.IsValid() && FileOp->HasFiles())
                            {
                                for (const FString& File : FileOp->GetFiles())
                                {
                                    if (IsDroppableImageFile(File)) return FReply::Handled();
                                }
                            }
                            return FReply::Unhandled();
                        })
                        .OnFaceDrop_Lambda([this, L](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                        {
                            if (!SelectedLayerName.IsValid())
                            {
                                SetStatus(TEXT("Drop ignored: no layer selected"), FLinearColor::Yellow);
                                return FReply::Handled();
                            }

                            UTexture2D* DroppedTex = nullptr;
                            if (TSharedPtr<FAssetDragDropOp> AssetOp = Evt.GetOperationAs<FAssetDragDropOp>())
                            {
                                for (const FAssetData& Asset : AssetOp->GetAssets())
                                {
                                    DroppedTex = Cast<UTexture2D>(Asset.GetAsset());
                                    if (DroppedTex) break;
                                }
                            }
                            else if (TSharedPtr<FContentBrowserDataDragDropOp> DataOp = Evt.GetOperationAs<FContentBrowserDataDragDropOp>())
                            {
                                for (const FAssetData& Asset : DataOp->GetAssets())
                                {
                                    DroppedTex = Cast<UTexture2D>(Asset.GetAsset());
                                    if (DroppedTex) break;
                                }
                            }

                            if (DroppedTex)
                            {
                                FWidgetUndoScope UndoScope(this, TEXT("Assign Dropped Texture"));
                                FFaceTextureSet Cur = GetSlotTextures(ActiveViewState, SelectedLayerName);
                                if (L == TEXT("Albedo")) Cur.Albedo = DroppedTex;
                                else if (L == TEXT("Normal")) Cur.Normal = DroppedTex;
                                else if (L == TEXT("Depth")) Cur.Depth = DroppedTex;
                                SetSlotTextures(ActiveViewState, SelectedLayerName, Cur);
                                if (bAutoFitOnAssign) ApplyAutoFit(ActiveViewState, SelectedLayerName);
                                RefreshUI();
                                SetStatus(FString::Printf(TEXT("Dropped %s -> %s"), *DroppedTex->GetName(), *L),
                                    FLinearColor(0.3f, 1.0f, 0.3f));
                                return FReply::Handled();
                            }

                            TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
                            if (FileOp.IsValid() && FileOp->HasFiles())
                            {
                                TArray<FString> Matching;
                                for (const FString& File : FileOp->GetFiles())
                                {
                                    if (IsDroppableImageFile(File)) Matching.Add(File);
                                }
                                if (Matching.Num() > 0)
                                {
                                    TArray<UTexture2D*> Imported = ImportTexturesFromFiles(Matching);
                                    int32 Assigned = 0;
                                    if (Imported.Num() > 0)
                                    {
                                        FWidgetUndoScope UndoScope(this, TEXT("Assign Dropped Texture Files"));
                                        for (UTexture2D* Tex : Imported)
                                        {
                                            if (ChannelFromTextureName(Tex->GetName()) == L)
                                            {
                                                if (AssignTextureToSlot(Tex, ActiveViewState, SelectedLayerName, L)) ++Assigned;
                                            }
                                        }
                                    }
                                    SetStatus(FString::Printf(TEXT("Dropped %d file(s): imported %d, assigned %d to %s"),
                                        Matching.Num(), Imported.Num(), Assigned, *L), FLinearColor(0.3f, 1.0f, 0.3f));
                                    RefreshUI();
                                    return FReply::Handled();
                                }
                            }
                            return FReply::Unhandled();
                        })
                        [SNew(SBox).WidthOverride(72).HeightOverride(72)[Thumb]]];
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

                // Phase D: the sync row is one grouped control - an explicit
                // Transform / Textures / Both op selector plus the apply
                // buttons. "Sync -> Selected" applies the chosen op to the
                // views picked on the state strip; "Sync Both -> All" stays
                // the canonical everything-everywhere quick action.
                TSharedRef<SHorizontalBox> SyncRow = SNew(SHorizontalBox);
                TSharedRef<STextBlock> SyncLbl = MakeLbl(TEXT("Sync layer to:"), 9, FLinearColor(0.6f,0.8f,1.0f));
                SyncLbl->SetToolTipText(FText::FromString(TEXT("Choose what to copy (Transform / Textures / Both), then apply to the "
                    "views picked on the state strip or to all views.")));
                SyncRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[SyncLbl];
                for (int32 O = 0; O < 3; ++O)
                {
                    const int32 OO = O;
                    SyncRow->AddSlot().Padding(FMargin(6,2)).AutoWidth()
                        [SNew(SButton)
                            .ButtonColorAndOpacity_Lambda([this, OO]()
                            {
                                return SyncOp == OO ? AccentBlue() : FLinearColor(0.13f, 0.13f, 0.15f);
                            })
                            .OnClicked_Lambda([this, OO](){ SyncOp = OO; return FReply::Handled(); })
                            .Content()
                            [SNew(STextBlock)
                                .Text(FText::FromString(UTF8_TO_TCHAR(FPLayout::SyncOpLabel(OO))))
                                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                                .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f))]];
                }
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
                        const bool bTr = FPLayout::SyncOpHasTransform(SyncOp);
                        const bool bTex = FPLayout::SyncOpHasTextures(SyncOp);
                        if (bTr)
                            SyncLayerToSelectedViews(ActiveViewState, SelectedLayerName, Dests, bTex);
                        else
                            SyncTexturesToSelectedViews(ActiveViewState, SelectedLayerName, Dests);
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
                LinkCheck->SetToolTipText(FText::FromString(TEXT("Edits in this state are broadcast live to the views picked on the "
                    "state strip; when none are picked, to all other states (Phase D)")));
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
        // P6: History menu — the History group moved out of the Diagnostics rail
        // into the toolbar: snapshot actions plus the full multi-step
        // undo/redo stack (click an entry to revert/re-apply to that point).
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
            [SNew(SMenuAnchor)
                .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
                {
                    TSharedRef<SVerticalBox> H = SNew(SVerticalBox);
                    H->AddSlot().AutoHeight().Padding(FMargin(8,5,8,2))
                        [MakeLbl(TEXT("HISTORY"), 8, FLinearColor(0.6f,0.7f,0.85f))];
                    H->AddSlot().AutoHeight().Padding(FMargin(2,1))
                        [SNew(SBox).WidthOverride(260)
                            [MakeBtn(TEXT("Snapshot current state"), [this]()
                            {
                                SnapshotPreset();
                                RefreshUI();
                            })]];
                    H->AddSlot().AutoHeight().Padding(FMargin(2,1))
                        [SNew(SBox).WidthOverride(260)
                            [MakeBtn(TEXT("Restore snapshot"), [this]()
                            {
                                RestoreSnapshot();
                            }, FLinearColor(1.0f,0.7f,0.3f))]
                            .IsEnabled(HasSnapshot())];
                    H->AddSlot().AutoHeight().Padding(FMargin(8,6,8,1))
                        [MakeLbl(TEXT("UNDO (click = revert to this point)"), 8, FLinearColor(0.6f,0.6f,0.6f))];
                    if (UndoStack.Num() == 0)
                    {
                        H->AddSlot().AutoHeight().Padding(FMargin(8,1,8,2))
                            [MakeLbl(TEXT("Nothing to undo"), 8, FLinearColor(0.4f,0.4f,0.4f))];
                    }
                    else
                    {
                        for (int32 i = UndoStack.Num() - 1; i >= 0; --i)
                        {
                            const FString Label = UndoStack[i].Label;
                            H->AddSlot().AutoHeight().Padding(FMargin(2,1))
                                [SNew(SBox).WidthOverride(260)
                                    [MakeBtn(FString::Printf(TEXT("%d. %s"), UndoStack.Num() - i, *Label), [this, Label]()
                                    {
                                        int32 Guard = 0;
                                        while (CanUndo() && GetUndoLabel() != Label && Guard++ < MaxUndoEntries)
                                        {
                                            if (!Undo()) break;
                                        }
                                        if (CanUndo() && GetUndoLabel() == Label) Undo();
                                        RefreshUI();
                                    })]];
                        }
                    }
                    H->AddSlot().AutoHeight().Padding(FMargin(8,6,8,1))
                        [MakeLbl(TEXT("REDO (click = re-apply to this point)"), 8, FLinearColor(0.6f,0.6f,0.6f))];
                    if (RedoStack.Num() == 0)
                    {
                        H->AddSlot().AutoHeight().Padding(FMargin(8,1,8,2))
                            [MakeLbl(TEXT("Nothing to redo"), 8, FLinearColor(0.4f,0.4f,0.4f))];
                    }
                    else
                    {
                        for (int32 i = RedoStack.Num() - 1; i >= 0; --i)
                        {
                            const FString Label = RedoStack[i].Label;
                            H->AddSlot().AutoHeight().Padding(FMargin(2,1))
                                [SNew(SBox).WidthOverride(260)
                                    [MakeBtn(FString::Printf(TEXT("%d. %s"), RedoStack.Num() - i, *Label), [this, Label]()
                                    {
                                        int32 Guard = 0;
                                        while (CanRedo() && GetRedoLabel() != Label && Guard++ < MaxUndoEntries)
                                        {
                                            if (!Redo()) break;
                                        }
                                        if (CanRedo() && GetRedoLabel() == Label) Redo();
                                        RefreshUI();
                                    })]];
                        }
                    }
                    return H;
                })
                [SNew(SButton)
                    .ButtonColorAndOpacity(FLinearColor(0.15f,0.15f,0.15f))
                    .Content()
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().Padding(FMargin(6,2,2,2)).AutoWidth().VAlign(VAlign_Center)
                            [MakeLbl(TEXT("History"), 10, FLinearColor(0.9f,0.85f,0.7f))]
                        + SHorizontalBox::Slot().Padding(FMargin(2,2,6,2)).AutoWidth().VAlign(VAlign_Center)
                            [MakeLbl(TEXT("\u25BE"), 8, FLinearColor(0.6f,0.6f,0.6f))]]]];
        TB->AddSlot().Padding(FMargin(8,2)).AutoWidth()
            [MakeBtn(TEXT("Import Art..."), [this]()
            {
                // P4: the Import Folder Wizard is THE one and only import
                // entry point — folder scan, per-part preview, drag-drop zone.
                OpenImportFolderWizard(TEXT(""));
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
                    "2. Assign textures by drag-dropping images onto the layer\n"
                    "   row, the canvas state thumbs, or the slot Albedo/Normal/\n"
                    "   Depth boxes (channel is read from the name suffix).\n"
                    "3. Adjust Position/Scale/Rotation in Properties.\n"
                    "4. Use Timeline to author Blink/Swoosh animation frames\n"
                    "   (drop textures directly onto a frame number).\n"
                    "5. Import Folder... scans a folder into layers+views.\n"
                    "6. Save Preset when done.\n\n"
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
                [MakeBtn(TEXT("Clear overrides (this slot)"), [this, S]()
                {
                    if (SelectedLayerName.IsValid()) ClearAllOverridesForSlot(S, SelectedLayerName);
                    RefreshUI();
                }, FLinearColor(1.0f,0.6f,0.6f), FLinearColor(0.1f,0.1f,0.1f))];
            // P2: the apply-to-views picker replaces the old sync-all /
            // fill-missing / 9-button duplicate section - every copy path
            // now lives in ONE picker (10 views + All + copy-from-Front).
            Menu->AddSlot().AutoHeight().Padding(FMargin(0, 4, 0, 0))
                [BuildApplyToViewsContent()];
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
    RailContent.SetNum(5);
    for (int32 Ri = 0; Ri < 5; ++Ri)
        RailContent[Ri] = SNew(SVerticalBox);
    RailSwitcher = SNew(SWidgetSwitcher).WidgetIndex(ActiveRailIndex);
    // Phase 4b: per-rail section registry + jump chips are rebuilt with the tree.
    RailSections.SetNum(5);
    for (auto& R : RailSections) R.Reset();
    RailChipsRows.SetNum(5);
    // P17 fit-first: each rail is a fit-packed stack (no vertical scroll bar).
    // P22: rail content NEVER scrolls left-to-right — rows are trimmed to the
    // rail width and the stack is clipped at RailWidth, so no element can pan
    // under the face schematic in the center column. Tall content stays
    // bounded because the rails are fit-packed, accordion-collapsed, or
    // carousel-paged (P18).
    // A pinned jump-chip row sits above the rail so section navigation never
    // scrolls away (Phase 4b accessibility); the chips strip scrolls inside
    // the 180px rail box (it never leaves the rail).
    for (int32 Ri = 0; Ri < 5; ++Ri)
    {
        TSharedRef<SBox> RailClip = SNew(SBox)
            .WidthOverride(FPLayout::RailWidth)
            .Clipping(EWidgetClipping::ClipToBounds)
            [RailContent[Ri].ToSharedRef()];
        TSharedRef<SHorizontalBox> Chips = SNew(SHorizontalBox);
        RailChipsRows[Ri] = Chips;
        TSharedRef<SScrollBox> ChipsScroll = SNew(SScrollBox).Orientation(Orient_Horizontal);
        ChipsScroll->AddSlot()[Chips];
        TSharedRef<SVerticalBox> RailStack = SNew(SVerticalBox);
        RailStack->AddSlot().AutoHeight()
            [SNew(SBox).HeightOverride(26)[ChipsScroll]];
        RailStack->AddSlot().FillHeight(1.0f)[RailClip];
        RailSwitcher->AddSlot()[RailStack];
    }
    SlotPropsBox = SNew(SVerticalBox);
    PropTabContent.Reset();
    PropTabContent.Add(SlotPropsBox);        // [0] right pane
    PropTabContent.Add(RailContent[0]);      // [1] View & Layer
    PropTabContent.Add(RailContent[1]);      // [2] Art
    PropTabContent.Add(RailContent[2]);      // [3] Nested & Animated (P6)
    PropTabContent.Add(RailContent[3]);      // [4] Camera/Preview
    PropTabContent.Add(RailContent[4]);      // [5] Diagnostics

    ArtAccordion = SNew(SFaceAccordion);
    NestedAccordion = SNew(SFaceAccordion);
    DiagnosticsAccordion = SNew(SFaceAccordion);

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
            [SNew(SFaceFlashButton).Text(TEXT("+ Add Layer"))
                .OnClicked_Lambda([this]()
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
                    return FReply::Handled();
                })];
        RegisterRailSection(0, TEXT("Layers"), LayerViewport);

        // P7-C: Pins — one row per pinned nested element of the selected
        // layer (name + thumbnail + Select/Delete) plus the whole-layer pin
        // row, and a [+ Add Pin] button. Select jumps the pin sliders to the
        // Nested rail (rail 2); Delete unpins. The section is an accordion
        // collapsed while the layer has no pins (RebuildPinList drives
        // SetExpanded); rows are rebuilt by RebuildPinList() on every
        // RefreshUI.
        PinListBox = SNew(SVerticalBox);
        LayersPinsAccordion = SNew(SFaceAccordion);
        LayersPinsAccordion->AddSection(TEXT("Pins"), PinListBox.ToSharedRef());
        RebuildPinList();
        LayerPanel->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 2))[LayersPinsAccordion.ToSharedRef()];
        RegisterRailSection(0, TEXT("Pins"), LayersPinsAccordion.ToSharedRef(), LayersPinsAccordion, 0);
    }

}

// ====================================================================
// P7-C: PINS LIST (View & Layer rail) — one row per pinned nested element
// of the selected layer (name + thumbnail + Select/Delete) plus the amber
// whole-layer pin row, and a [+ Add Pin] button. Select jumps the Nested
// rail's pin sliders (FlashTab rail 2); Delete unpins; Add pins the layer
// at screen center.
// ====================================================================
void UFaceParallaxEditorWidget::RebuildPinList()
{
    PinThumbBrushes.Reset();
    if (!PinListBox.IsValid()) return;
    PinListBox->ClearChildren();
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !SelectedLayerName.IsValid() || !ActivePreset)
    {
        PinListBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("Select a layer to see its pins."), 8, FLinearColor(0.55f, 0.55f, 0.55f))];
        if (LayersPinsAccordion.IsValid()) LayersPinsAccordion->SetExpanded(0, false);
        return;
    }
    const FFaceArtSlot SlotRec = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
    const int32 N = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
    int32 Rows = 0;
    auto AddPinRow = [&](const FString& Name, UTexture2D* ThumbTex, int32 ElementIndex, bool bLayerPin)
    {
        ++Rows;
        TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
        TSharedPtr<FSlateBrush> Br = MakeShared<FSlateBrush>();
        Br->SetResourceObject(ThumbTex);
        Br->ImageSize = FVector2D(14, 14);
        PinThumbBrushes.Add(Br);
        Row->AddSlot().AutoWidth().Padding(FMargin(0, 1, 4, 1)).VAlign(VAlign_Center)
            [SNew(SBox).WidthOverride(14).HeightOverride(14)
                [SNew(SImage)
                    .Image(ThumbTex ? Br.Get() : FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .ColorAndOpacity(ThumbTex ? FLinearColor(1, 1, 1) : (bLayerPin
                        ? FLinearColor(1.0f, 0.7f, 0.3f) : FLinearColor(0.7f, 0.7f, 0.7f)))]];
        Row->AddSlot().AutoWidth().Padding(FMargin(0, 1, 2, 1)).VAlign(VAlign_Center)
            [SNew(SBox).WidthOverride(44)
                [SNew(STextBlock)
                    .Text(FText::FromString(Name))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(bLayerPin ? FLinearColor(1.0f, 0.8f, 0.4f) : FLinearColor(0.85f, 0.85f, 0.9f))]];
        const int32 EI = ElementIndex;
        const bool bPin = bLayerPin;
        Row->AddSlot().AutoWidth().Padding(FMargin(2, 1, 0, 1)).VAlign(VAlign_Center)
            [MakeBtn(TEXT("Sel"), [this, EI, bPin]()
            {
                SelectedNestedElementIndex = EI;   // -1 = layer pin
                if (bPin)
                    SetStatus(FString::Printf(TEXT("Layer pin selected on '%s' - the Nested rail pin sliders edit it"),
                        *SelectedLayerName.ToString()), FLinearColor(1.0f, 0.8f, 0.4f));
                else
                    SetStatus(FString::Printf(TEXT("Pin selected - the Nested rail pin sliders edit it")),
                        FLinearColor(0.5f, 1.0f, 0.5f));
                FlashTab(2);
                SetActiveRailIndex(2);
                return FReply::Handled();
            }, FLinearColor(0.45f, 0.6f, 0.85f))];
        Row->AddSlot().AutoWidth().Padding(FMargin(2, 1, 0, 1)).VAlign(VAlign_Center)
            [MakeBtn(TEXT("Delete"), [this, EI, bPin]()
            {
                if (!ActivePreset || !SelectedLayerName.IsValid()) return FReply::Handled();
                FWidgetUndoScope UndoScope(this, bPin ? TEXT("Delete Layer Pin") : TEXT("Delete Pin"));
                if (bPin)
                {
                    FFaceArtSlot SlotRec2 = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                    SlotRec2.LayerPin3D.bPinned = false;
                    ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, SlotRec2);
                    if (SelectedNestedElementIndex < 0) SelectedNestedElementIndex = 0;
                    SetStatus(FString::Printf(TEXT("Layer pin removed from '%s'"), *SelectedLayerName.ToString()),
                        FLinearColor(1.0f, 0.7f, 0.5f));
                }
                else
                {
                    UFaceParallaxComponent* C2 = GetParallaxComponent();
                    if (C2 && EI >= 0 && EI < C2->GetNestedElementCount(ActiveViewState, SelectedLayerName))
                    {
                        FFaceNestedArt El = C2->GetNestedElement(ActiveViewState, SelectedLayerName, EI);
                        El.Pin3D.bPinned = false;
                        ActivePreset->SetNestedElement(ActiveViewState, SelectedLayerName, EI, El);
                        if (SelectedNestedElementIndex == EI) SelectedNestedElementIndex = 0;
                        SetStatus(FString::Printf(TEXT("Pin removed from element %d on '%s'"), EI + 1,
                            *SelectedLayerName.ToString()), FLinearColor(1.0f, 0.7f, 0.5f));
                    }
                }
                RefreshUI();
                return FReply::Handled();
            }, FLinearColor(0.8f, 0.4f, 0.4f))];
        PinListBox->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 1))[Row];
    };
    for (int32 i = 0; i < N; ++i)
    {
        const FFaceNestedArt El = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
        if (!El.Pin3D.bPinned) continue;
        AddPinRow(El.ElementName.IsValid() ? El.ElementName.ToString()
            : FString::Printf(TEXT("Element %d"), i + 1), El.Textures.Albedo, i, false);
    }
    if (SlotRec.LayerPin3D.bPinned)
        AddPinRow(TEXT("Layer pin"), nullptr, -1, true);
    if (Rows == 0)
        PinListBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No pins yet - Add Pin pins this layer at screen center."), 8, FLinearColor(0.5f, 0.5f, 0.5f))];
    PinListBox->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 0))
        [MakeBtn(TEXT("+ Add Pin"), [this]()
        {
            AddLayerPinAtDefault();
            return FReply::Handled();
        }, FLinearColor(0.6f, 0.9f, 1.0f))];
    // P7-C: the Pins section collapses while the layer has no pins.
    if (LayersPinsAccordion.IsValid()) LayersPinsAccordion->SetExpanded(0, Rows > 0);
}

// P7-C: [+ Add Pin] - pins the selected layer at screen center (front-state
// UV 0.5/0.5), mirroring PlacePinAtUV's zone-frame math. Selecting the amber
// "Layer pin" row afterwards drives the Nested rail's pin sliders.
void UFaceParallaxEditorWidget::AddLayerPinAtDefault()
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !SelectedLayerName.IsValid() || !ActivePreset) return;
    FWidgetUndoScope UndoScope(this, TEXT("Add Layer Pin"));
    FFaceArtSlot SlotRec = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
    double PX = 0.0, PY = 0.0, PZ = 0.0;
    FPLayout::LayerPinFromUV(0.5, 0.5, Comp->GetZoneCenterYaw(ActiveViewState), PX, PY, PZ);
    SlotRec.LayerPin3D.bPinned = true;
    SlotRec.LayerPin3D.Position3D = FVector((float)PX, (float)PY, (float)PZ);
    ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, SlotRec);
    SelectedNestedElementIndex = -1;   // pin sliders now edit the layer pin
    SetStatus(FString::Printf(TEXT("Layer pin added to '%s' - drag the amber handle on the canvas to move it"),
        *SelectedLayerName.ToString()), FLinearColor(0.5f, 1.0f, 0.5f));
    RefreshUI();
}

void UFaceParallaxEditorWidget::BuildPanelArtRail()
{
            // Quick actions (batch operations) - the canonical pinned actions
            // (Import Art... / Auto-Fit All / Clear All Overrides, P7-B)
            // live ONLY in the pinned strip above the main row
            // (P21 PinnedActionsNeverInScroll); this rail section keeps the
            // rail-local operations that are not part of that canonical set.
            {
                TSharedRef<SVerticalBox> QaBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> QaRow = SNew(SHorizontalBox);
                // P2: ONE apply-to-views picker replaces "Duplicate Front ->
                // This" + "Fill Missing Views" - 10 views + All + copy-from-
                // Front in a single popup (same component as the v-menu).
                TSharedRef<SMenuAnchor> ApplyAnchor = SNew(SMenuAnchor)
                    .Placement(MenuPlacement_BelowAnchor)
                    .OnGetMenuContent_Lambda([this]() { return BuildApplyToViewsContent(); });
                ApplyAnchor->SetContent(MakeBtn(TEXT("Apply to views..."),
                    [ApplyAnchor]()
                    {
                        ApplyAnchor->SetIsOpen(true, true);
                    }, FLinearColor(0.6f, 0.9f, 1.0f)));
                QaRow->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
                    [ApplyAnchor];
                QaRow->AddSlot().Padding(FMargin(4, 2)).FillWidth(1.0f);
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
                    [MakeLbl(TEXT("Copy:"), 9, FLinearColor(0.6f,0.9f,0.7f))];
                XvRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [SNew(SBox).WidthOverride(64)[CopyCombo]];
                XvRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [SNew(SButton)
                        .ButtonColorAndOpacity(FLinearColor(0.15f,0.15f,0.15f))
                        .ToolTipText(FText::FromString(TEXT("Copy the selected view's transform into the active view")))
                        .OnClicked_Lambda([this]()
                        {
                            if (!CopyFromSelection.IsValid()) return FReply::Handled();
                            for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
                            {
                                if (i == (int32)ActiveViewState) continue;
                                if (StaticEnum<EFaceAngleState>()->GetNameStringByValue(i).Equals(*CopyFromSelection))
                                {
                                    CopyTransformFromView((EFaceAngleState)i, ActiveViewState);
                                    RefreshUI();
                                    return FReply::Handled();
                                }
                            }
                            return FReply::Handled();
                        })
                        .Content()
                        [MakeLbl(TEXT("Copy"), 9, FLinearColor(0.6f,1.0f,0.6f))]];
                XvRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                TSharedRef<SCheckBox> LinkCheck = SNew(SCheckBox)
                    .IsChecked(bLinkAcrossViews ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { bLinkAcrossViews = (S == ECheckBoxState::Checked); });
                LinkCheck->SetToolTipText(FText::FromString(TEXT("Edits in this state are broadcast to all other states")));
                XvBox->AddSlot().AutoHeight()[XvRow];
                // P22: the Link toggle sits on its own second row so the whole
                // Cross-View section fits the 180px rail width.
                TSharedRef<SHorizontalBox> XvRowB = SNew(SHorizontalBox);
                XvRowB->AddSlot().Padding(FMargin(0,2)).AutoWidth()[LinkCheck];
                XvRowB->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Link"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                XvBox->AddSlot().AutoHeight()[XvRowB];
                TSharedRef<SWidget> XvSection = MakeSectionBox(TEXT("Cross-View Transform"), XvBox);
                RailContent[1]->AddSlot().AutoHeight()
                    [XvSection];
                RegisterRailSection(1, TEXT("Cross-View Transform"), XvSection);
            }


        // Import + Outline -> Depth: accordion-collapsed sections (P16)
        // on the Art rail; Import open by default. P4: the folder wizard is
        // the ONE entry point (folder scan + drop zone + per-part preview).
        {
            TSharedRef<SVerticalBox> ImportBox = SNew(SVerticalBox);
            TSharedRef<SHorizontalBox> ImpRow = SNew(SHorizontalBox);
            ImpRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                [MakeBtn(TEXT("Folder..."), [this]()
                {
                    OpenImportFolderWizard(TEXT(""));
                }, FLinearColor(0.6f,1.0f,0.6f))];
            ImpRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [SNew(SFaceDropTarget)
                    .OnFaceDragOver_Lambda([](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                    {
                        // File drops only — asset drops belong on the texture
                        // slots themselves, so pass them through unhandled.
                        TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
                        if (FileOp.IsValid() && FileOp->HasFiles())
                        {
                            for (const FString& File : FileOp->GetFiles())
                                if (IsDroppableImageFile(File)) return FReply::Handled();
                        }
                        return FReply::Unhandled();
                    })
                    .OnFaceDrop_Lambda([this](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                    {
                        // P4: dragging files onto the Import section routes
                        // them through the wizard — the one import pipeline.
                        TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
                        if (FileOp.IsValid() && FileOp->HasFiles())
                        {
                            TArray<FString> Files;
                            for (const FString& File : FileOp->GetFiles())
                                if (IsDroppableImageFile(File)) Files.Add(File);
                            if (Files.Num() > 0)
                            {
                                OpenImportFolderWizard(FPaths::GetPath(Files[0]));
                                return FReply::Handled();
                            }
                        }
                        return FReply::Unhandled();
                    })
                    [MakeBtn(TEXT("Drop files"), [this]()
                    {
                        OpenImportFolderWizard(TEXT(""));
                    }, FLinearColor(0.4f,0.5f,0.6f))]];
            ImpRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
            ImportBox->AddSlot().AutoHeight()[ImpRow];
            ArtAccordion->AddSection(TEXT("Import"), ImportBox);

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
                    [SNew(SButton)
                        .ButtonColorAndOpacity(FLinearColor(0.15f,0.15f,0.15f))
                        .ToolTipText(FText::FromString(TEXT("Generate Depth from Outlines: bakes the outline silhouettes into a depth buffer")))
                        .OnClicked_Lambda([this, ReadGrid](){ GenerateDepthFromOutlines(ReadGrid()); return FReply::Handled(); })
                        .Content()
                        [MakeLbl(TEXT("Generate"), 9, FLinearColor(0.5f,1.0f,0.7f))]];
                OdRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()[GridBox];
                OdRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [SNew(SButton)
                        .ButtonColorAndOpacity(FLinearColor(0.15f,0.15f,0.15f))
                        .ToolTipText(FText::FromString(TEXT("Detect Profile: auto-detect the face profile, then generate the depth buffer")))
                        .OnClicked_Lambda([this, ReadGrid](){ DetectFaceProfile(); GenerateDepthFromOutlinesImpl(ReadGrid()); RefreshUI(); return FReply::Handled(); })
                        .Content()
                        [MakeLbl(TEXT("Detect"), 9, FLinearColor(0.5f,1.0f,0.7f))]];
                OdRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                TSharedRef<SCheckBox> OdCheck = SNew(SCheckBox)
                    .IsChecked(bOutlineOverlayVisible ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { SetOutlineOverlayVisible(S == ECheckBoxState::Checked); });
                CheckOutlineOverlay = OdCheck;
                // P22: the overlay toggle sits on its own row so the row above
                // (Generate + grid + Detect) fits the 180px rail width.
                TSharedRef<SHorizontalBox> OdChkRow = SNew(SHorizontalBox);
                OdChkRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[OdCheck];
                OdChkRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Overlay"), 8, FLinearColor(0.7f,0.7f,0.7f))];
                OdBox->AddSlot().AutoHeight()[OdRow];
                OdBox->AddSlot().AutoHeight()[OdChkRow];

                // Scope selector: which view states the depth bake overwrites.
                // P22: three compact check+letter pairs (tooltips carry the
                // full scope names) so the row fits the 180px rail width.
                TSharedRef<SHorizontalBox> ScopeRow = SNew(SHorizontalBox);
                auto MakeScopeCheck = [this, &ScopeRow](const TCHAR* Label, const TCHAR* Tip, int32 Value, const FLinearColor& Color)
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
                    C->SetToolTipText(FText::FromString(FString::Printf(TEXT("Depth bake scope: %s"), Tip)));
                    ScopeRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()[C];
                    ScopeRow->AddSlot().Padding(FMargin(1,2)).AutoWidth()
                        [MakeLbl(Label, 8, Color)];
                };
                MakeScopeCheck(TEXT("F"), TEXT("Front only"), 0, FLinearColor(0.6f,0.9f,0.7f));
                MakeScopeCheck(TEXT("8h"), TEXT("8 horizontal states"), 1, FLinearColor(0.6f,0.8f,1.0f));
                MakeScopeCheck(TEXT("All"), TEXT("All 10 states"), 2, FLinearColor(0.9f,0.8f,0.6f));
                ScopeRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                OdBox->AddSlot().AutoHeight()[ScopeRow];

                TextOutlineStats = MakeLbl(TEXT("No depth buffer generated yet"), 8, FLinearColor(0.5f,0.5f,0.5f));
                OdBox->AddSlot().AutoHeight().Padding(FMargin(2,1))
                    [TextOutlineStats.ToSharedRef()];

                // P22: the per-state silhouette picker is 10 checks in two
                // 5-wide rows (checks only, tooltips carry the state names);
                // All/None sit on their own row below. All rows fit 168px.
                TSharedRef<SHorizontalBox> OvRow0 = SNew(SHorizontalBox);
                TSharedRef<SHorizontalBox> OvRow1 = SNew(SHorizontalBox);
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
                for (int32 Oi = 0; Oi < 10; ++Oi)
                {
                    auto& PS = OvPickStates[Oi];
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
                    (Oi < 5 ? OvRow0 : OvRow1)->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                        [Chk.ToSharedRef()];
                }
                OdBox->AddSlot().AutoHeight()[OvRow0];
                OdBox->AddSlot().AutoHeight()[OvRow1];
                TSharedRef<SHorizontalBox> OvAllRow = SNew(SHorizontalBox);
                OvAllRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeBtn(TEXT("All"), [this]()
                    {
                        for (int32 i = 0; i < 10; ++i)
                        {
                            if (UFaceParallaxComponent* C = GetParallaxComponent()) C->SetOutlineViewEnabled((EFaceAngleState)i, true);
                        }
                        RefreshOutlineViewChecks();
                    })];
                OvAllRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("None"), [this]()
                    {
                        if (UFaceParallaxComponent* C = GetParallaxComponent()) C->ClearOutlineViewStates();
                        RefreshOutlineViewChecks();
                    })];
                OvAllRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                OdBox->AddSlot().AutoHeight()[OvAllRow];
                ArtAccordion->AddSection(TEXT("Outline -> Depth"), OdBox);
            }
            ArtAccordion->SetExpanded(0, true); // Import section open by default
            RailContent[1]->AddSlot().AutoHeight()[ArtAccordion.ToSharedRef()];
            RegisterAccordionSections(1, ArtAccordion);
        }

        // Bulk Assign + Assign Ops (moved from the old Assign rail)
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
                    // P7-F: each grid cell is a drop host scoped to that
                    // state column + layer row (assign fills the slot by suffix).
                    [SNew(SBox).WidthOverride(16).HeightOverride(16)
                        [SNew(SFaceDropTarget)
                            .OnFaceDragOver_Lambda([](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                            {
                                if (Evt.GetOperationAs<FAssetDragDropOp>().IsValid()) return FReply::Handled();
                                if (Evt.GetOperationAs<FContentBrowserDataDragDropOp>().IsValid()) return FReply::Handled();
                                TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
                                if (FileOp.IsValid() && FileOp->HasFiles())
                                {
                                    for (const FString& File : FileOp->GetFiles())
                                        if (IsDroppableImageFile(File)) return FReply::Handled();
                                }
                                return FReply::Unhandled();
                            })
                            .OnFaceDrop_Lambda([this, S, Tag](const FGeometry&, const FDragDropEvent& Evt) -> FReply
                            {
                                if (!Tag.IsValid()) return FReply::Unhandled();
                                return AssignImageDropToSlot(S, Tag, Evt) ? FReply::Handled() : FReply::Unhandled();
                            })
                            [CellBtn]]];
            }
            GB->AddSlot().AutoHeight()[Row];
        }

        TSharedRef<SHorizontalBox> RowLbls = SNew(SHorizontalBox);
        for (int32 Ri = 0; Ri < NumRows; ++Ri)
        {
            const FName Tag = LayerNames.IsValidIndex(Ri) ? LayerNames[Ri] : FName();
            const FString Label = Tag.IsValid() ? Tag.ToString() : TEXT("(no layer)");
            RowLbls->AddSlot().AutoWidth().Padding(FMargin(0, 2))
                [SNew(SBox).WidthOverride(44)
                    [MakeLbl(*Label, 7, FLinearColor(0.6f, 0.6f, 0.7f))]];
        }
        RowLbls->AddSlot().Padding(FMargin(4, 2)).FillWidth(1.0f);
        GB->AddSlot().AutoHeight()[RowLbls];
        // P22: the coverage summary sits on its own row so the label row
        // above stays at 132px inside the 168px rail budget.
        TSharedRef<SHorizontalBox> CovRow = SNew(SHorizontalBox);
        TextAssignCoverage = MakeLbl(TEXT("Filled 0/30"), 8, FLinearColor(0.7f, 0.9f, 0.7f));
        CovRow->AddSlot().Padding(FMargin(0, 2)).AutoWidth()[TextAssignCoverage.ToSharedRef()];
        CovRow->AddSlot().Padding(FMargin(4, 2)).FillWidth(1.0f);
        GB->AddSlot().AutoHeight()[CovRow];

        TSharedRef<SWidget> GridSection = MakeSectionBox(TEXT("Bulk Assign"), GB);
        RailContent[1]->AddSlot().AutoHeight().Padding(FMargin(2, 1, 2, 1))[GridSection];
        RegisterRailSection(1, TEXT("Bulk Assign"), GridSection);
        RefreshAssignGrid();
    }

    // Assign Ops: fill-missing / clear-row / slot-to-all bulk actions, plus
    // the performance tier and camera source combos (P3).
    {
        TSharedRef<SVerticalBox> OB = SNew(SVerticalBox);
        TSharedRef<SHorizontalBox> Row0 = SNew(SHorizontalBox);
        Row0->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
            [MakeBtn(TEXT("Clear"), [this]()
            {
                if (!SelectedLayerName.IsValid()) return;
                FWidgetUndoScope UndoScope(this, TEXT("Clear Row"));
                for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
                    ClearAllOverridesForSlot((EFaceAngleState)i, SelectedLayerName);
                RefreshUI();
            }, FLinearColor(1.0f, 0.6f, 0.6f))];
        // P2: ONE apply-to-views picker replaces "Fill Missing" + "Slot ->
        // All" - same component as the v-menu and the Quick Actions row.
        // P22: shortened to "Apply views" so the row fits the rail.
        TSharedRef<SMenuAnchor> OpsAnchor = SNew(SMenuAnchor)
            .Placement(MenuPlacement_BelowAnchor)
            .OnGetMenuContent_Lambda([this]() { return BuildApplyToViewsContent(); });
        OpsAnchor->SetContent(MakeBtn(TEXT("Apply views"),
            [OpsAnchor]()
            {
                OpsAnchor->SetIsOpen(true, true);
            }, FLinearColor(0.8f, 0.9f, 1.0f)));
        Row0->AddSlot().Padding(FMargin(4, 2)).AutoWidth()
            [OpsAnchor];
        Row0->AddSlot().FillWidth(1.0f);
        OB->AddSlot().AutoHeight()[Row0];

        TSharedRef<SHorizontalBox> Row1 = SNew(SHorizontalBox);
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

        Row1->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
            [MakeLbl(TEXT("Cam source"), 8, FLinearColor(0.6f, 0.6f, 0.7f))];
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
            [SNew(SBox).WidthOverride(90)[CamCombo]];
        // P22: perf tier and camera source each own a row so both combos
        // keep a readable width inside the 168px rail budget.
        OB->AddSlot().AutoHeight()[Row1];

        TSharedRef<SHorizontalBox> Row2 = SNew(SHorizontalBox);
        Row2->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
            [MakeLbl(TEXT("Perf tier"), 8, FLinearColor(0.6f, 0.6f, 0.7f))];
        Row2->AddSlot().Padding(FMargin(4, 2)).AutoWidth()
            [SNew(SBox).WidthOverride(90)[PerfCombo]];
        Row2->AddSlot().Padding(FMargin(4, 2)).FillWidth(1.0f);
        OB->AddSlot().AutoHeight()[Row2];

        TSharedRef<SWidget> OpsSection = MakeSectionBox(TEXT("Assign Ops"), OB);
        RailContent[1]->AddSlot().AutoHeight().Padding(FMargin(2, 1, 2, 1))[OpsSection];
        RegisterRailSection(1, TEXT("Assign Ops"), OpsSection);
    }
}

void UFaceParallaxEditorWidget::BuildPanelAnimatedSections()
{
            // P6: Animated Variants folded into the Nested rail's accordion
            // (tab 2 "Nested & Animated"); these sections register directly
            // into NestedAccordion, after "Nested Art / Pins".
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
                NestedAccordion->AddSection(
                    TEXT("Viseme Frames (click filled cell = play)"), VisemeDisclosure.ToSharedRef());
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
                NestedAccordion->AddSection(TEXT("Hull Review (click thumb = jump)"), HrBox);
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
                    [MakeLbl(TEXT("Follows"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                CfRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f);
                // P22: "Snap Camera" is a compact 72px button (tooltip carries
                // the full name) so the row fits the 168px rail budget.
                CfRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [SNew(SButton)
                        .ButtonColorAndOpacity(FLinearColor(0.15f,0.15f,0.15f))
                        .ToolTipText(FText::FromString(TEXT("Snap Camera to Active View")))
                        .OnClicked_Lambda([this](){ SnapCameraToActiveView(); return FReply::Handled(); })
                        .Content()
                        [SNew(SBox).WidthOverride(64)
                            [MakeLbl(TEXT("Snap"), 9, FLinearColor(0.8f,0.9f,1.0f))]]];
                CfBox->AddSlot().AutoHeight()[CfRow];
                TSharedRef<SWidget> CfSection = MakeSectionBox(TEXT("Camera Follow"), CfBox);
                RailContent[3]->AddSlot().AutoHeight()
                    [CfSection];
                RegisterRailSection(3, TEXT("Camera Follow"), CfSection);
            }

        // ============ CAMERA/Preview RAIL (rail 3) ============
        {
            TSharedRef<SVerticalBox> T2 = RailContent[3].ToSharedRef();

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
                // P22: compact "Zones:" label (tooltip explains the order)
                // + four 24px edit boxes keep the row at ~158px.
                TSharedRef<SHorizontalBox> ZoneRow = SNew(SHorizontalBox);
                TSharedRef<STextBlock> ZoneLbl = MakeLbl(TEXT("Zones:"), 9, FLinearColor(0.6f,0.8f,1.0f));
                ZoneLbl->SetToolTipText(FText::FromString(TEXT("Zone boundary multipliers: Front / 3-Quarter / Profile / Back")));
                ZoneRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[ZoneLbl];
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
                        [SNew(SBox).WidthOverride(24)[Edit]];
                    if (ZoneEditBoxes.Num() < 4) ZoneEditBoxes.SetNum(4);
                    ZoneEditBoxes[Zi] = Edit;
                };
                AddZoneEdit(0); AddZoneEdit(1); AddZoneEdit(2); AddZoneEdit(3);
                Cam->AddSlot().AutoHeight()[ZoneRow];

                // Preview FOV slider
                TSharedRef<SHorizontalBox> FOVRow = SNew(SHorizontalBox);
                FOVRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeLbl(TEXT("FOV"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                TSharedRef<STextBlock> FOVLabel = MakeLbl(
                    FString::Printf(TEXT("%.0f"), FMath::Clamp(GetPreviewFOV(), 10.0f, 90.0f)),
                    9, FLinearColor(0.8f,0.8f,0.8f));
                FOVRow->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)
                    [SNew(SSlider).Value((FMath::Clamp(GetPreviewFOV(), 10.0f, 90.0f) - 10.0f) / 80.0f)
                        .OnValueChanged_Lambda([this, FOVLabel](float V)
                        {
                            float FOV = FMath::Clamp(V * 80.0f + 10.0f, 10.0f, 90.0f);
                            FOVLabel->SetText(FText::FromString(FString::Printf(TEXT("%.0f"), FOV)));
                            SetPreviewFOV(FOV);
                        })];
                FOVRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [FOVLabel];
                Cam->AddSlot().AutoHeight()[FOVRow];

                TSharedRef<SWidget> CamSection = MakeSectionBox(TEXT("Camera"), Cam);
                T2->AddSlot().AutoHeight()
                    [CamSection];
                RegisterRailSection(3, TEXT("Camera"), CamSection);
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
                RegisterRailSection(3, TEXT("Blend Preview"), BlendSection);
            }
        }

}


void UFaceParallaxEditorWidget::BuildPanelNestedRail()
{
        // ============ NESTED & ANIMATED RAIL (rail 2) ============
        // P6: the old "Nested Elements & Pins" and "Animated Variants" rails
        // merged into one tab ("Nested & Animated") with a single accordion:
        // Nested Art / Pins, then Viseme Frames, then Hull Review.
        {
            TSharedRef<SVerticalBox> T3 = RailContent[2].ToSharedRef();

            // Nested Art / Pin section
            {
                TSharedRef<SVerticalBox> Pin = SNew(SVerticalBox);

                // Phase E: pane switcher — [Elements] shows the per-element
                // pin controls + outliner, [Pins] shows the pin manager (one
                // row per pinned item across the layer).
                {
                    TSharedRef<SHorizontalBox> ModeRow = SNew(SHorizontalBox);
                    for (int32 M = 0; M < 2; ++M)
                    {
                        const int32 MM = M;
                        ModeRow->AddSlot().Padding(FMargin(1)).AutoWidth()
                            [SNew(SBox).WidthOverride(64).HeightOverride(18)
                                [SNew(SButton)
                                    .ButtonColorAndOpacity_Lambda([this, MM]()
                                    {
                                        return NestedPaneMode == MM ? AccentBlue() : FLinearColor(0.13f, 0.13f, 0.15f);
                                    })
                                    .OnClicked_Lambda([this, MM]()
                                    {
                                        SetNestedPaneMode(MM);
                                        return FReply::Handled();
                                    })
                                    .Content()
                                    [SNew(STextBlock)
                                        .Text(FText::FromString(MM == 0 ? TEXT("Elements") : TEXT("Pins")))
                                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                                        .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f))]]];
                    }
                    Pin->AddSlot().AutoHeight()[ModeRow];
                }

                TSharedRef<SVerticalBox> EditPane = SNew(SVerticalBox);

                // P3: pin editing is split into three visual sub-sections:
                // PLACE (where the pin sits), PHYSICS (view-angle rotation +
                // scale), MOTION (jiggle spring + idle animation) — two
                // different tasks, two different forms.
                EditPane->AddSlot().AutoHeight().Padding(FMargin(0,2,0,0))
                    [MakeLbl(TEXT("PLACE"), 8, FLinearColor(0.55f, 0.6f, 0.7f))];
                EditPane->AddSlot().AutoHeight().Padding(FMargin(0,0,0,2))
                    [MakeLbl(TEXT("Canvas Pin Mode places/moves this pin at 0-100% of the layer's frame"),
                        7, FLinearColor(0.5f, 0.5f, 0.55f))];

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
                    EditPane->AddSlot().AutoHeight()[StepRow];
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
                    TFunction<void(float)>&& OnChange, const FString& Suffix = FString())
                {
                    TSharedRef<STextBlock> ValLbl = MakeLbl(TEXT(""), 8, FLinearColor(0.7f,0.7f,0.7f));
                    TextOut = ValLbl;
                    TSharedRef<SSlider> Sl = SNew(SSlider).Value(PinSliderNorm(Def, Min, Max))
                        .OnValueChanged_Lambda([Fn = MoveTemp(OnChange), Min, Max, Precision, ValLbl, Suffix](float V)
                        {
                            const float Out = Min + V * (Max - Min);
                            if (Precision == 1)
                            {
                                ValLbl->SetText(FText::FromString(FString::Printf(TEXT("%.1f%s"), Out, *Suffix)));
                            }
                            else
                            {
                                ValLbl->SetText(FText::FromString(FString::Printf(TEXT("%.2f%s"), Out, *Suffix)));
                            }
                            Fn(Out);
                        });
                    SliderOut = Sl;
                    TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                    R->AddSlot().Padding(FMargin(0,2)).AutoWidth()[MakeLbl(Label, 9)];
                    R->AddSlot().Padding(FMargin(4,2)).FillWidth(1.0f)[Sl];
                    R->AddSlot().Padding(FMargin(4,2)).AutoWidth()[ValLbl];
                    EditPane->AddSlot().AutoHeight()[R];
                };

                FFaceNestedArt InitEl;
                int32 InitCount = 0;
                GetSelectedPinElement(InitEl, InitCount);

                AddPinSliderRow(TEXT("Pin X"), 0.0f, 100.0f, (InitEl.Pin3D.Position3D.X + 1.0f) * 50.0f, 0, SliderPinX, TextPinX,
                    [this](float Pct)
                    {
                        const float V = Pct / 50.0f - 1.0f;
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
                    }, TEXT("%"));
                AddPinSliderRow(TEXT("Pin Y"), 0.0f, 100.0f, (InitEl.Pin3D.Position3D.Y + 1.0f) * 50.0f, 0, SliderPinY, TextPinY,
                    [this](float Pct)
                    {
                        const float V = Pct / 50.0f - 1.0f;
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
                    }, TEXT("%"));
                AddPinSliderRow(TEXT("Pin Z"), 0.0f, 100.0f, (InitEl.Pin3D.Position3D.Z + 1.0f) * 50.0f, 0, SliderPinZ, TextPinZ,
                    [this](float Pct)
                    {
                        const float V = Pct / 50.0f - 1.0f;
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
                    }, TEXT("%"));

                EditPane->AddSlot().AutoHeight().Padding(FMargin(0,6,0,0))
                    [MakeLbl(TEXT("PHYSICS"), 8, FLinearColor(0.55f, 0.6f, 0.7f))];

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
                AddPinSliderRow(TEXT("Min Scale"), 0.05f, 1.0f, InitEl.Pin3D.MinScale, 2, SliderPinMinScale, TextPinMinScale,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.Pin3D.MinScale = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                            return;
                        }
                        if (SelectedLayerName.IsValid() && ActivePreset)
                        {
                            FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                            LS.LayerPin3D.MinScale = V;
                            ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, LS);
                            RefreshUI();
                        }
                    });

                EditPane->AddSlot().AutoHeight().Padding(FMargin(0,6,0,0))
                    [MakeLbl(TEXT("MOTION"), 8, FLinearColor(0.55f, 0.6f, 0.7f))];

                // Jiggle controls (nested elements only — layer pins have no
                // jiggle). Writes go through the jiggle API so the component's
                // JiggleStates re-apply with the new spring params.
                auto SetJiggleField = [this](TFunction<void(FFaceJiggleSettings&)>&& Mutate)
                {
                    FFaceNestedArt El;
                    int32 Count = 0;
                    if (GetSelectedPinElement(El, Count))
                    {
                        FFaceJiggleSettings JS = GetNestedJiggleSettings(
                            ActiveViewState, SelectedLayerName, SelectedNestedElementIndex);
                        Mutate(JS);
                        SetNestedJiggleSettings(
                            ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, JS);
                        RefreshUI();
                    }
                };
                {
                    TSharedRef<SHorizontalBox> JigRow = SNew(SHorizontalBox);
                    CheckJiggleEnabled = SNew(SCheckBox)
                        .IsChecked(InitEl.bJiggleEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                        {
                            FFaceNestedArt El;
                            int32 Count = 0;
                            if (GetSelectedPinElement(El, Count))
                            {
                                SetNestedJiggleEnabled(ActiveViewState, SelectedLayerName,
                                    SelectedNestedElementIndex, S == ECheckBoxState::Checked);
                                RefreshUI();
                            }
                        });
                    JigRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[CheckJiggleEnabled.ToSharedRef()];
                    JigRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                        [MakeLbl(TEXT("Jiggle"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                    EditPane->AddSlot().AutoHeight()[JigRow];
                }
                AddPinSliderRow(TEXT("Jiggle Stiff"), 0.0f, 20.0f, InitEl.JiggleSettings.Stiffness, 2,
                    SliderJiggleStiffness, TextJiggleStiffness,
                    [SetJiggleField](float V) mutable
                    {
                        SetJiggleField([V](FFaceJiggleSettings& S) { S.Stiffness = V; });
                    });
                AddPinSliderRow(TEXT("Jiggle Damp"), 0.0f, 5.0f, InitEl.JiggleSettings.Damping, 2,
                    SliderJiggleDamping, TextJiggleDamping,
                    [SetJiggleField](float V) mutable
                    {
                        SetJiggleField([V](FFaceJiggleSettings& S) { S.Damping = V; });
                    });
                AddPinSliderRow(TEXT("Jiggle Imp"), 0.0f, 10.0f, InitEl.JiggleSettings.ImpulseScale, 2,
                    SliderJiggleImpulse, TextJiggleImpulse,
                    [SetJiggleField](float V) mutable
                    {
                        SetJiggleField([V](FFaceJiggleSettings& S) { S.ImpulseScale = V; });
                    });
                AddPinSliderRow(TEXT("Jiggle Mid"), 0.0f, 1.0f, InitEl.JiggleSettings.Midpoint, 2,
                    SliderJiggleMidpoint, TextJiggleMidpoint,
                    [SetJiggleField](float V) mutable
                    {
                        SetJiggleField([V](FFaceJiggleSettings& S) { S.Midpoint = V; });
                    });
                AddPinSliderRow(TEXT("End Stiff"), 0.0f, 20.0f, InitEl.JiggleSettings.EndStiffness, 2,
                    SliderJiggleEndStiffness, TextJiggleEndStiffness,
                    [SetJiggleField](float V) mutable
                    {
                        SetJiggleField([V](FFaceJiggleSettings& S) { S.EndStiffness = V; });
                    });
                AddPinSliderRow(TEXT("End Damp"), 0.0f, 5.0f, InitEl.JiggleSettings.EndDamping, 2,
                    SliderJiggleEndDamping, TextJiggleEndDamping,
                    [SetJiggleField](float V) mutable
                    {
                        SetJiggleField([V](FFaceJiggleSettings& S) { S.EndDamping = V; });
                    });
                AddPinSliderRow(TEXT("End Imp"), 0.0f, 10.0f, InitEl.JiggleSettings.EndImpulseScale, 2,
                    SliderJiggleEndImpulse, TextJiggleEndImpulse,
                    [SetJiggleField](float V) mutable
                    {
                        SetJiggleField([V](FFaceJiggleSettings& S) { S.EndImpulseScale = V; });
                    });

                // P3: idle animation (nested elements only) — frame duration +
                // speed multiplier for the element's looping IdleFrames.
                AddPinSliderRow(TEXT("Idle Frame"), 0.001f, 2.0f, InitEl.IdleFrameDuration, 1,
                    SliderIdleDuration, TextIdleDuration,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.IdleFrameDuration = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                        }
                    }, TEXT("s"));
                AddPinSliderRow(TEXT("Idle Speed"), 0.0f, 4.0f, InitEl.IdleSpeedMultiplier, 2,
                    SliderIdleSpeed, TextIdleSpeed,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        if (GetSelectedPinElement(El, Count))
                        {
                            El.IdleSpeedMultiplier = V;
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
                        }
                    }, TEXT("x"));

                EditPane->AddSlot().AutoHeight().Padding(FMargin(0,2))
                    [MakeBtn(TEXT("Detect Profile"), [this](){ DetectFaceProfile(); RefreshUI(); })];
                EditPane->AddSlot().AutoHeight().Padding(FMargin(0,4,0,0))
                    [MakeLbl(TEXT("Nested Elements"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                NestedOutlinerBox = SNew(SVerticalBox);
                RebuildNestedOutliner();
                EditPane->AddSlot().AutoHeight().Padding(FMargin(0,2,0,0))
                    [NestedOutlinerBox.ToSharedRef()];

                // Phase E: pin-manager pane (second pane of the switcher).
                NestedPaneSwitcher = SNew(SWidgetSwitcher);
                NestedPaneSwitcher->AddSlot()[EditPane];
                PinManagerBox = SNew(SVerticalBox);
                RebuildPinManager();
                NestedPaneSwitcher->AddSlot()[PinManagerBox.ToSharedRef()];
                NestedPaneSwitcher->SetActiveWidgetIndex(NestedPaneMode);
                Pin->AddSlot().AutoHeight().Padding(FMargin(0,2,0,0))
                    [NestedPaneSwitcher.ToSharedRef()];
                NestedAccordion->AddSection(TEXT("Nested Art / Pins"), Pin);

                RefreshPinControls();
            }

            // P6: Animated Variants sections (Viseme Frames + Hull Review)
            // folded into this rail's accordion, after "Nested Art / Pins".
            BuildPanelAnimatedSections();

            T3->AddSlot().AutoHeight()[NestedAccordion.ToSharedRef()];
            RegisterAccordionSections(2, NestedAccordion);
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

        // Status matrix detail (inside Layers rail panel): the layer x state
        // grid is UNBOUNDED (one 44px row per layer), so it is carousel-paged
        // (P18) inside the same fixed viewport + nav strip + bottom reserve
        // (P19) as the other rail carousels - every layer row, including the
        // last ("Hair"), stays reachable above the terminal section (P17/P24).
        StatusMatrixGrid = SNew(SGridPanel);
        StatusMatrixScroll = SNew(SScrollBox).Orientation(Orient_Horizontal);
        StatusMatrixScroll->AddSlot() [StatusMatrixGrid.ToSharedRef()];
        RebuildStatusMatrix();
        TSharedRef<SVerticalBox> SDBox = SNew(SVerticalBox);
        SDBox->AddSlot().AutoHeight()
            [SNew(SBox)
                .HeightOverride(FPLayout::CarouselViewportH)
                .Padding(FMargin(0, 0, 0, FPLayout::ScrollReserveBottom))
                [StatusMatrixScroll.ToSharedRef()]];
        TSharedRef<SFaceCarouselNav> SDNav = SNew(SFaceCarouselNav)
            .OnPrev_Lambda([this]()
            {
                StatusMatrixPageIndex = FMath::Max(0, StatusMatrixPageIndex - 1);
                RebuildStatusMatrix();
                return FReply::Handled();
            })
            .OnNext_Lambda([this]()
            {
                StatusMatrixPageIndex = StatusMatrixPageIndex + 1;
                RebuildStatusMatrix();
                return FReply::Handled();
            });
        StatusMatrixPageLabel = SDNav->Label;
        SDBox->AddSlot().AutoHeight().Padding(FMargin(4, 0, 4, 2))[SDNav];
        TSharedRef<SWidget> StatusSection = MakeSectionBox(TEXT("Status Detail"), SDBox);
        RailContent[0]->AddSlot().AutoHeight().Padding(FMargin(2,1))
            [StatusSection];
        RegisterRailSection(0, TEXT("Status Detail"), StatusSection);

        // All Layers (current state): per-layer cross-layer overlay rows flip
        // through carousel pages (P18) inside a fixed page viewport with a
        // bottom reserve (P19); no vertical scroll bar (P17).
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
TSharedRef<SWidget> AlSection = MakeSectionBox(TEXT("All Layers (current state)"), ALBox);
RailContent[0]->AddSlot().AutoHeight().Padding(FMargin(2,1))
[AlSection];
RegisterRailSection(0, TEXT("All Layers (current state)"), AlSection);


        // Tag validator + material cross-referencer live in the Diagnostics rail
        // (BuildPanelDiagnosticsRail) - the bottom bar holds only workflow actions.

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

}

void UFaceParallaxEditorWidget::BuildPanelDiagnosticsRail()
{
        // ============ DIAGNOSTICS RAIL (rail 4) ============
        // P6: the Diagnostics group leads (Tag Validator, Material
        // Cross-Reference, Param Reference, Edge Analysis); then Config,
        // Param Bindings, Depth Debug, Problems. Mirrors the manifest
        // RL-Diagnostics section order (children 0..7).
        {
            TSharedRef<SVerticalBox> T5 = RailContent[4].ToSharedRef();

            // Tag validator (dev tool; Diagnostics group, Diagnostics rail)
            {
                TSharedRef<SVerticalBox> TV = SNew(SVerticalBox);
                TextTagValidator = SNew(STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.8f));
                TV->AddSlot().AutoHeight().Padding(FMargin(2,1))
                    [TextTagValidator.ToSharedRef()];
                RebuildTagValidator();
                DiagnosticsAccordion->AddSection(TEXT("Tag Validator"), TV);
            }

            // Material cross-referencer (dev tool; Diagnostics group)
            {
                TSharedRef<SVerticalBox> MC = SNew(SVerticalBox);
                TextMaterialCrossRef = SNew(STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(FLinearColor(0.8f,0.8f,0.8f));
                MC->AddSlot().AutoHeight().Padding(FMargin(2,1))
                    [TextMaterialCrossRef.ToSharedRef()];
                RebuildMaterialCrossRef();
                DiagnosticsAccordion->AddSection(TEXT("Material Cross-Reference"), MC);
            }

            // Param Reference (dev tool; Diagnostics group - params live with the
            // diagnostics per the review grouping, removed from the pin flow)
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
                DiagnosticsAccordion->AddSection(TEXT("Param Reference"), RefBox);
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
    if (bDepthOverlayVisible) BuildDepthOverlay();
                        RefreshUI();
                    });
                EdRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[CheckEdgeOverlay.ToSharedRef()];
                EdRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                    [MakeLbl(TEXT("Edge"), 9, FLinearColor(0.7f,0.8f,1.0f))];
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
                    [MakeLbl(TEXT("Hist"), 9, FLinearColor(0.7f,0.8f,1.0f))];
                // P22: "Edge"/"Hist" (tooltips: Edge Overlay / Luminance
                // Histogram) + a compact Rebuild keep this row inside 168px.
                TSharedRef<SButton> RebuildBtn = MakeBtn(TEXT("Rebuild"), [this]()
                {
                    BuildEdgeOverlay();
                    SetStatus(TEXT("Edge overlay + histogram rebuilt"), FLinearColor(0.6f,1.0f,0.6f));
                });
                RebuildBtn->SetToolTipText(FText::FromString(TEXT("Rebuild edge overlay + histogram")));
                EdRow->AddSlot().Padding(FMargin(6,2)).AutoWidth()[RebuildBtn];
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
                DiagnosticsAccordion->AddSection(TEXT("Edge Analysis"), EdBox);
            }

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
                DiagnosticsAccordion->AddSection(TEXT("Config"), ConfigDisclosure.ToSharedRef());
                UpdateDisclosureSummaries();
            }

            // Param bindings table (Phase E; Diagnostics rail)
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
                    [SNew(SFaceFlashButton).Text(TEXT("Add"))
                        .OnClicked_Lambda([this]()
                        {
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (!Comp || !SelectedLayerName.IsValid() || !EditParamAddName.IsValid()) return FReply::Handled();
                            TArray<FFaceParamBinding> All = GetParamBindings(ActiveViewState, SelectedLayerName);
                            FFaceParamBinding NewB;
                            NewB.ParamName = FName(*EditParamAddName->GetText().ToString());
                            if (NewB.ParamName.IsNone()) NewB.ParamName = FName(TEXT("Param"));
                            All.Add(NewB);
                            SetParamBindings(ActiveViewState, SelectedLayerName, All);
                            RebuildParamTable();
                            SetStatus(TEXT("Param binding added"), FLinearColor(0.6f,1.0f,0.6f));
                            RefreshUI();
                            return FReply::Handled();
                        })];
                PbBox->AddSlot().AutoHeight()[AddRow];
                ParamTableBox = SNew(SVerticalBox);
                RebuildParamTable();
                PbBox->AddSlot().AutoHeight().Padding(FMargin(0,2,0,0))
                    [ParamTableBox.ToSharedRef()];
                DiagnosticsAccordion->AddSection(TEXT("Param Bindings (state + layer)"), PbBox);
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
                // P22: compact labels (tooltips carry "Rebuild Mesh"/"Color by
                // Depth") so the row fits the 168px rail budget.
                TSharedRef<SButton> RebuildMeshBtn = MakeBtn(TEXT("Rebuild"), [this, RebuildFromActiveDepth]()
                {
                    RebuildFromActiveDepth();
                    SetStatus(TEXT("Depth debug mesh rebuilt"), FLinearColor(0.6f,1.0f,0.6f));
                }, FLinearColor(0.6f,0.8f,1.0f));
                RebuildMeshBtn->SetToolTipText(FText::FromString(TEXT("Rebuild Mesh from active depth")));
                DdRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[RebuildMeshBtn];
                TSharedRef<SButton> ColorDepthBtn = MakeBtn(TEXT("Color"), [this]()
                {
                    if (ValidatePreviewActor() && PreviewActor->DepthDebug)
                        ColorByDepth(true);
                });
                ColorDepthBtn->SetToolTipText(FText::FromString(TEXT("Color by Depth")));
                DdRow->AddSlot().Padding(FMargin(6,2)).AutoWidth()[ColorDepthBtn];
                DdRow->AddSlot().FillWidth(1.0f);
                DdBox->AddSlot().AutoHeight()[DdRow];
                DiagnosticsAccordion->AddSection(TEXT("Depth Debug"), DdBox);
            }

            // Problems panel (Phase F) - the issue rows flip through carousel
            // pages inside the Issues section (P18); the panel itself is a
            // fit-packed stack, so no vertical scroll bar (P17).
            {
                TSharedRef<SVerticalBox> PrBox = SNew(SVerticalBox);
                ProblemsPanelBox = SNew(SVerticalBox);
                RebuildProblemsPanel();
                PrBox->AddSlot().AutoHeight()[ProblemsPanelBox.ToSharedRef()];
                DiagnosticsAccordion->AddSection(TEXT("Problems (click row = jump)"), PrBox);
                RefreshProblemsSummary();
            }

            T5->AddSlot().AutoHeight()[DiagnosticsAccordion.ToSharedRef()];
            RegisterAccordionSections(4, DiagnosticsAccordion);
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