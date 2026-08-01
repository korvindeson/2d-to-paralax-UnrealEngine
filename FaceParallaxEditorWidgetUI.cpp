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
    BuildPanelRailContainers(Root);
    BuildPanelLayers(Root);
    TSharedRef<SVerticalBox> CenterCol = BuildPanelCanvas(Root);
    TSharedRef<SVerticalBox> PropPanel = SNew(SVerticalBox);
    BuildPanelSlotProps(Root, PropPanel);
    BuildPanelTransformRail();
    BuildPanelDebugRail();
    BuildPanelCameraRail();
    BuildPanelAdvancedRail();
    BuildPanelTimeline(Root);
    BuildPanelBottomBar(Root);

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
                [SNew(SBox).WidthOverride(340)
                    [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                        .BorderBackgroundColor(FLinearColor(0.07f,0.07f,0.07f))
                        .Padding(FMargin(0, 0, FPLayout::PropsRightGap, 0))
                        [PropPanel]]];
        Root->AddSlot().AutoHeight()
            [SNew(SBox).HeightOverride(560)[MainRow]];
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
#endif
