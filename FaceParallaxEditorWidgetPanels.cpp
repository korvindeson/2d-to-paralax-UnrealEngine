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
    RefreshTextureThumbs();
    RefreshCanvasPreview();
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
    FPresetTransactionScope Transaction(ActivePreset, TEXT("Generate Depth From Outlines"));
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
