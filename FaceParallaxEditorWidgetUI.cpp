#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxEditorWidgetShared.h"
#include "FaceParallaxPreviewActor.h"
#include "FaceParallaxComponent.h"
#include "FaceParallaxPreset.h"
#include "FaceParallaxLayoutSpec.h"
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
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/ObjectSaveContext.h"
#include "Rendering/DrawElements.h"


// ====================================================================
// REBUILDWIDGET — Full editor layout
// The per-panel construction blocks live in
// FaceParallaxEditorWidgetPanels.cpp (MakeSectionBox + BuildPanel*);
// this file only wires them together in the fixed assembly order.
// ====================================================================

TSharedRef<SWidget> UFaceParallaxEditorWidget::RebuildWidget()
{
    UE_LOG(LogTemp, Log, TEXT("[FaceParallaxWidget] REBUILD DOCKED-TAB v3 (marker 0xV3)"));

    // Phase H self-check: the layout manifest must satisfy design principles
    // P1..P11. Mirrors Tests/ParallaxMathTests.cpp::TestPhaseHUIDesign().
    {
        const std::vector<FPLayout::FPLayoutNode> Spec = FPLayout::BuildSpec();
        const int RootIdx = FPLayout::FindRootIndex(Spec);
        const int Reachable = FPLayout::CountReachable(Spec);
        const std::vector<FPLayout::FPViolation> Violations = FPLayout::ValidateDesign(Spec);
        UE_LOG(LogTemp, Log, TEXT("[FaceParallax] LayoutSpec self-check: nodes=%d reachable=%d violations=%d"),
            (int)Spec.size(), Reachable, (int)Violations.size());
        if (RootIdx < 0 || Reachable != (int)Spec.size() || !Violations.empty())
        {
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallax] LayoutSpec self-check FAILED: root=%d reachable=%d/%d violations=%d"),
                RootIdx, Reachable, (int)Spec.size(), (int)Violations.size());
            for (const FPLayout::FPViolation& V : Violations)
            {
                UE_LOG(LogTemp, Warning, TEXT("[FaceParallax]   violation: %s on %s (%s)"),
                    UTF8_TO_TCHAR(FPLayout::RuleName(V.Rule)), UTF8_TO_TCHAR(V.Node),
                    UTF8_TO_TCHAR(V.Detail.c_str()));
            }
        }
    }

    if (IsTemplate())
    {
        return SNew(SBox).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
            [SNew(STextBlock).Text(FText::FromString(TEXT("Face Parallax Editor")))];
    }

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
    // Fallback: use the default preset so the editor works even without an actor.
    // Chain: deploy asset -> any preset in the registry -> in-memory default.
    if (!ActivePreset)
    {
        UFaceParallaxPreset* DefaultPreset = LoadObject<UFaceParallaxPreset>(nullptr,
            TEXT("/Game/FaceParallax/Presets/DA_FaceParallaxPreset.DA_FaceParallaxPreset"));
        if (!DefaultPreset)
        {
            FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
            TArray<FAssetData> PresetAssets;
            ARModule.Get().GetAssetsByClass(UFaceParallaxPreset::StaticClass()->GetClassPathName(), PresetAssets);
            for (const FAssetData& Asset : PresetAssets)
            {
                DefaultPreset = Cast<UFaceParallaxPreset>(Asset.GetAsset());
                if (DefaultPreset) break;
            }
        }
        if (!DefaultPreset)
        {
            // Last resort: an in-memory default so the editor is always usable.
            DefaultPreset = NewObject<UFaceParallaxPreset>(GetTransientPackage(), TEXT("FallbackPreset"), RF_Transient);
            DefaultPreset->PopulateDefaultAssignments(TArray<FString>{ TEXT("Eyes"), TEXT("Brows"), TEXT("Mouth"), TEXT("Hair") });
        }
        ActivePreset = DefaultPreset;
        if (ActivePreset)
        {
            UE_LOG(LogTemp, Log, TEXT("[FaceParallaxEditorWidget] Fallback ActivePreset = %s"),
                *ActivePreset->GetName());
        }
    }

    LayerNames = GetUILayerTags();
    if (!SelectedLayerName.IsValid() && LayerNames.Num() > 0) SelectedLayerName = LayerNames[0];

    // ========================
    // ROOT
    // ========================

    TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

    // ========================
    // PANEL BUILDERS (defined in FaceParallaxEditorWidgetPanels.cpp)
    // ========================

    BuildPanelToolbar(Root);
    BuildPanelStateStrip(Root);
    BuildPanelZoneDiagram(Root);
    BuildPanelContextPages(Root);
    BuildPanelLayers(Root);
    TSharedRef<SVerticalBox> CenterCol = BuildPanelCanvas(Root);
    TSharedRef<SVerticalBox> PropPanel = SNew(SVerticalBox);
    BuildPanelSlotProps(Root, PropPanel);
    BuildPanelAssignSections();
    BuildPanelExpressionPage();
    BuildPanelPreviewPage();
    BuildPanelDeveloperPage();
    BuildPanelTimeline(Root);
    BuildPanelBottomBar(Root);
    // Phase 4b: page chips are built after every panel builder has registered
    // its sections; a pending cross-page search jump is consumed last.
    BuildPageSectionChips();
    ConsumePendingJump();

    // --- Pinned quick-actions strip: full-width Root row above the main row.
    // P21 (PinnedActionsNeverInScroll): the canonical quick actions live HERE
    // and only here - never inside a scroll viewport. Button set mirrors
    // FPLayout::QuickActionLabels(); the manifest PinnedStrip node mirrors
    // this exact row (no SScrollBox, fixed height FPLayout::PinnedStripHeight).
    {
        TSharedRef<SHorizontalBox> QB = SNew(SHorizontalBox);
        const std::vector<std::string>& QLabels = FPLayout::QuickActionLabels();
        TFunction<void()> QHandlers[] = {
            [this]() { OpenImportArtDialog(); },      // W3: native picker is the primary import path; folder wizard stays bulk-only (Art rail)
            [this]() { ApplyAutoFitToAllSlots(); RefreshUI(); },
            [this]() { ClearAllOverrides(); RefreshUI(); },
        };
        for (int32 Qi = 0; Qi < (int32)QLabels.size(); ++Qi)
        {
            FString Label = FString(UTF8_TO_TCHAR(QLabels[Qi].c_str()));
            const FLinearColor BtnFG = (Qi == (int32)QLabels.size() - 1) ? FLinearColor(1.0f,0.6f,0.6f) : FLinearColor(0.85f,0.85f,0.85f);
            TSharedRef<SButton> QBt = SNew(SButton)
                .ButtonColorAndOpacity(FLinearColor(0.15f,0.15f,0.15f))
                .OnClicked_Lambda([this, Q = QHandlers[Qi]]()
                {
                    Q();
                    return FReply::Handled();
                })
                .Content()
                [SNew(STextBlock)
                    .Text(FText::FromString(Label))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(BtnFG)];
            QB->AddSlot().Padding(FMargin(2, 2)).AutoWidth()[QBt];
        }
        QB->AddSlot().FillWidth(1.0f);
        Root->AddSlot().AutoHeight()
            [SNew(SBox).HeightOverride(FPLayout::PinnedStripHeight)[QB]];
    }

    // --- Top-level page tab bar (CT-TabRow): labeled tabs switching the
    // context panel. Mirrors the manifest CT-TabRow exactly (CT-Tab0..3 +
    // CT-DevTab + CT-Spacer, fixed height FPLayout::TabBarHeight).
    // W1: 4 task pages + the closed-by-default Developer drawer.
    {
        TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
        TopTabBar = Tabs;
        struct { int32 I; const TCHAR* T; float W; } TabDefs[] = {
            {0, TEXT("Assign"), 56.0f},
            {1, TEXT("Transform & Sync"), 118.0f},
            {2, TEXT("Expression/Blink/Viseme"), 148.0f},
            {3, TEXT("Preview & Debug"), 106.0f},
            {4, TEXT("Developer"), 84.0f},
        };
        for (auto& Td : TabDefs)
        {
            const int32 TabIdx = Td.I;
            // P7-A: programmatic page jumps (canvas -> Assign) flash the
            // destination tab amber for ~0.9s (TabFlashUntil), so the tab
            // switch is visible even when the user isn't looking at the bar.
            TSharedRef<SButton> TabBtn = SNew(SButton)
                .OnClicked_Lambda([this, TabIdx]()
                {
                    SetActivePageIndex(TabIdx);
                    return FReply::Handled();
                })
                .ButtonColorAndOpacity_Lambda([this, TabIdx]()
                {
                    const bool bFlash = TabFlashUntil > FSlateApplication::Get().GetCurrentTime()
                        && TabFlashIndex == TabIdx;
                    if (bFlash) return FLinearColor(1.0f, 0.7f, 0.2f);
                    return ActivePageIndex == TabIdx ? AccentBlue() : FLinearColor(0.14f, 0.14f, 0.16f);
                })
                .Content()
                [SNew(STextBlock)
                    .Text(FText::FromString(Td.T))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f))];
            Tabs->AddSlot().Padding(FMargin(2)).AutoWidth()
                [SNew(SBox).WidthOverride(Td.W).HeightOverride(22)[TabBtn]];
        }
        Tabs->AddSlot().FillWidth(1.0f);    // CT-Spacer
        Root->AddSlot().AutoHeight()
            [SNew(SBox).HeightOverride(FPLayout::TabBarHeight)[Tabs]];
    }

    // --- Assemble main row: center canvas | context panel (W1) ---
    {
        TSharedRef<SHorizontalBox> MainRow = SNew(SHorizontalBox);
        MainRow->AddSlot().FillWidth(1.0f).VAlign(VAlign_Fill)
            [CenterCol];
        // The context panel is FIXED at FPLayout::ContextPanelWidth (621px =
        // the merged old 273px rail + 340px props + 8px edge gap; fills the
        // edge-schematic section's empty space while leaving the 450px canvas
        // fully visible). NO splitter here: a drag-resize handle between the
        // panel and the canvas lets users steal the canvas's space, which
        // clips the edge map and breaks the paged carousels (P24 defect class).
        // Resizing only happens at the very outside of the widget.
        MainRow->AddSlot().AutoWidth().VAlign(VAlign_Fill)
            [SAssignNew(ContextWidthBox, SBox).WidthOverride(FPLayout::ContextPanelWidth)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                    .BorderBackgroundColor(FLinearColor(0.07f,0.07f,0.07f))
                    .Padding(FMargin(0))
                    [PageSwitcher.ToSharedRef()]]];
        Root->AddSlot().AutoHeight()
            [SNew(SBox).HeightOverride((float)FPLayout::MainRowHeight)
                .Clipping(EWidgetClipping::ClipToBounds)[MainRow]];
    }

    // --- Diagnostic Log ---
    DiagnosticLog = SNew(SMultiLineEditableTextBox)
        .Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
        .IsReadOnly(true);
    DiagnosticLogBox = SNew(SBox)
        .HeightOverride((float)FPLayout::DiagnosticLogHeight)
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

    // Main window vertical scroll: the fixed design height (884) clips the
    // bottom rows (timeline, bottom bar, diagnostic log) when the tab is
    // pinned/docked short, leaving the bottom section invisible and
    // unreachable. The window container scrolls vertically so every section
    // stays reachable; panel content still packs to fit (P17/P18/P19).
    TSharedRef<SScrollBox> MainWindowScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
    MainWindowScroll->AddSlot()[Root];
    return MainWindowScroll;
}
#endif
