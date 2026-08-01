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
            {
                FFaceLayerDef Def = Comp->GetLayerDefinition(i);
                if (IsSeedPlaceholderLayerDef(Def)) continue;
                Tags.Add(Def.LayerTag);
            }
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
#endif
