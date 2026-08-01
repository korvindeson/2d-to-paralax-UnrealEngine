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
// HELPERS
// ====================================================================

static FLinearColor EditorBg(float L) { return FLinearColor(L, L, L); }

static FLinearColor AccentGreen() { return FLinearColor(0.3f, 0.8f, 0.3f); }

static TSharedRef<SCheckBox> MakeToggle(bool Def, TFunction<void(bool)>&& Fn)
{
    return SNew(SCheckBox)
        .IsChecked(Def ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
        .OnCheckStateChanged_Lambda([Fn = MoveTemp(Fn)](ECheckBoxState S)
        { Fn(S == ECheckBoxState::Checked); });
}

static auto MakeSectionLbl(const FString& T, int32 S) -> TSharedRef<STextBlock>
{
    return SNew(STextBlock)
        .Text(FText::FromString(T))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", S > 0 ? S : 10))
        .ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f));
}

// ====================================================================
// REBUILDWIDGET — Full editor layout
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
    // LAMBDAS (RebuildWidget-only helpers)
    // ========================

    auto MakeSlider = [](float Val, TFunction<void(float)>&& Fn) -> TSharedRef<SSlider>
    {
        return SNew(SSlider).Value(Val)
            .OnValueChanged_Lambda(MoveTemp(Fn));
    };

    auto MakeSectionBox = [this](const FString& Title, TSharedRef<SWidget> Content) -> TSharedRef<SWidget>
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
    };

    // ========================
    // ROOT
    // ========================

    TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

    // ========================
    // 1. TOOLBAR
    // ========================

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
            [MakeBtn(TEXT("Import Art..."), [this]()
            {
                OpenImportArtDialog();
            })];
        SearchBox = SNew(SSearchBox)
            .HintText(FText::FromString(TEXT("Search settings...")))
            .OnTextChanged_Lambda([this](const FText& T) { ApplySearchFilter(T.ToString()); });
        TB->AddSlot().Padding(FMargin(6,2)).AutoWidth()
            [SNew(SBox).WidthOverride(140)[SearchBox.ToSharedRef()]];
        TB->AddSlot().FillWidth(1.0f);
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
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
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
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
        TB->AddSlot().Padding(FMargin(2)).AutoWidth()
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
        for (auto& St : States)
        {
            EFaceAngleState S = St.S;
            bool IsActive = (S == ActiveViewState);
            TSharedRef<SImage> DotImg = SNew(SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush"));
            DotImg->SetColorAndOpacity(GetStateDotColor(S));
            ViewTabDots.Add(DotImg);
            TSharedRef<SBox> Dot = SNew(SBox).WidthOverride(8).HeightOverride(8)[DotImg];
            TSharedRef<SButton> TabBtn = SNew(SButton)
                .ButtonColorAndOpacity(IsActive ? AccentBlue() : FLinearColor(0.12f,0.12f,0.12f))
                .OnClicked_Lambda([this, S](){ SetActiveViewState(S); RefreshUI(); return FReply::Handled(); })
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
            TSharedRef<SScrollBox> DupScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
            for (int32 Di = 0; Di < 10; ++Di)
            {
                EFaceAngleState DS = (EFaceAngleState)Di;
                if (DS == S) continue;
                FString DSName = StaticEnum<EFaceAngleState>()->GetNameStringByValue((int64)DS);
                DupScroll->AddSlot().Padding(FMargin(2,1))
                    [MakeBtn(FString::Printf(TEXT("Copy %s -> %s"), *DSName, *StateName),
                        [this, DS, S]()
                        {
                            if (SelectedLayerName.IsValid()) DuplicateState(DS, S);
                            RefreshUI();
                        }, FLinearColor(0.6f,0.8f,1.0f), FLinearColor(0.08f,0.08f,0.08f))];
            }
            Menu->AddSlot().AutoHeight()
                [SNew(SBox).HeightOverride(140)[DupScroll]];
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
        Root->AddSlot().AutoHeight()
            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
                .BorderBackgroundColor(FLinearColor(0.1f,0.1f,0.1f))
                .Padding(FMargin(2,3))
                [SNew(SBox).HeightOverride(26)[StateBar]]];
    }

    // ========================
    // 2b. ZONE DIAGRAM
    // ========================

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

    // ========================
    // 3. MAIN AREA: RAIL | CANVAS | SLOT PROPS
    // ========================

    // --- Rail containers (created up-front so every section below can target them) ---
    RailContent.SetNum(5);
    for (int32 Ri = 0; Ri < 5; ++Ri)
        RailContent[Ri] = SNew(SVerticalBox);
    RailSwitcher = SNew(SWidgetSwitcher).WidgetIndex(ActiveRailIndex);
    // Each rail is a scroll viewport (vertical + horizontal): wide button rows
    // and tall section stacks stay inside the 180x560 rail area instead of
    // being drawn over the canvas / timeline / bottom bar (P12/P13 contract).
    for (int32 Ri = 0; Ri < 5; ++Ri)
    {
        TSharedRef<SScrollBox> RailV = SNew(SScrollBox).Orientation(Orient_Vertical);
        TSharedRef<SScrollBox> RailH = SNew(SScrollBox).Orientation(Orient_Horizontal);
        RailH->AddSlot()[RailContent[Ri].ToSharedRef()];
        RailV->AddSlot()[RailH];
        RailSwitcher->AddSlot()[RailV];
    }
    SlotPropsBox = SNew(SVerticalBox);
    PropTabContent.Reset();
    PropTabContent.Add(SlotPropsBox);        // [0] right pane
    PropTabContent.Add(RailContent[0]);      // [1] Layers
    PropTabContent.Add(RailContent[1]);      // [2] Transform
    PropTabContent.Add(RailContent[2]);      // [3] Camera
    PropTabContent.Add(RailContent[3]);      // [4] Debug
    PropTabContent.Add(RailContent[4]);      // [5] Advanced

    // --- 3a. LAYERS PANEL (rail 0) ---
    TSharedRef<SVerticalBox> LayerPanel = RailContent[0].ToSharedRef();
    {
        LayerPanel->AddSlot().AutoHeight().Padding(FMargin(4,4,4,2))
            [MakeLbl(TEXT("LAYERS"), 10, FLinearColor(0.7f,0.7f,0.9f))];
        LayerScrollBox = SNew(SScrollBox)
            .Orientation(Orient_Vertical);
        LayerPanelBox = SNew(SVerticalBox);
        LayerScrollBox->AddSlot() [LayerPanelBox.ToSharedRef()];
        RefreshLayerList();
        LayerPanel->AddSlot().FillHeight(1.0f)
            [LayerScrollBox.ToSharedRef()];
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
                }
                RefreshUI();
            }, FLinearColor(0.6f,0.8f,0.6f), FLinearColor(0.08f,0.08f,0.08f))];
    }

    // --- 3b. CENTER CANVAS ---
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
                        .ButtonColorAndOpacity(DisplayMode == M ? AccentBlue() : FLinearColor(0.13f,0.13f,0.15f))
                        .OnClicked_Lambda([this, M](){ SetDisplayMode(M); return FReply::Handled(); })
                        .Content()
                        [SNew(STextBlock)
                            .Text(FText::FromString(Mo.T))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            .ColorAndOpacity(FLinearColor(0.85f,0.85f,0.85f))]];
            }
            ModeRow->AddSlot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)
                [MakeLbl(TEXT("Display"), 8, FLinearColor(0.6f,0.6f,0.6f))];
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
        PreviewHost = SNew(SBox).HeightOverride(450)
            [SNew(SOverlay)
                + SOverlay::Slot()[PreviewImageWidget.ToSharedRef()]
                + SOverlay::Slot()[OutlinePreviewImage.ToSharedRef()]
                + SOverlay::Slot()[OnionSkinImage.ToSharedRef()]
                + SOverlay::Slot()[EdgeOverlayImage.ToSharedRef()]
                + SOverlay::Slot()[GizmoLayer.ToSharedRef()]];
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(2))
            [PreviewHost.ToSharedRef()];

        // Layer label + view info
        TextLayerName = SNew(STextBlock)
            .Text(FText::FromString(SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : TEXT("(no layer)")))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            .ColorAndOpacity(FLinearColor(0.9f,0.9f,0.9f));
        CenterCol->AddSlot().AutoHeight().Padding(FMargin(4,2,4,0))
            [TextLayerName.ToSharedRef()];
    }

    // --- 3c. SLOT PROPERTIES (right pane) ---
    TSharedRef<SVerticalBox> PropPanel = SNew(SVerticalBox).Visibility(EVisibility::Visible);
    PropScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
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

        // ============ RIGHT PANE: TRANSFORM / OVERRIDE / SYNC ============
        {
            TSharedRef<SVerticalBox> T0 = SlotPropsBox.ToSharedRef();

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
                T0->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Transform"), XForm)];
            }

            // View override toggle + clear
            {
                TSharedRef<SVerticalBox> OvBox = SNew(SVerticalBox);
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
                OvBox->AddSlot().AutoHeight()[OvRow];
                T0->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("View Override"), OvBox)];
            }

            // Sync picker
            {
                TSharedRef<SVerticalBox> SyncBox = SNew(SVerticalBox);
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
                SyncBox->AddSlot().AutoHeight()[SyncRow];

                TSharedRef<SHorizontalBox> PickRow = SNew(SHorizontalBox);
                struct { EFaceAngleState S; const TCHAR* T; } PickStates[] = {
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
                SyncViewCheckBoxes.Reset();
                for (auto& PS : PickStates)
                {
                    TSharedPtr<SCheckBox> Chk = SNew(SCheckBox)
                        .IsChecked(ECheckBoxState::Unchecked);
                    SyncViewCheckBoxes.Add(Chk);
                    PickRow->AddSlot().Padding(FMargin(2,2)).AutoWidth()
                        [SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth()[Chk.ToSharedRef()]
                            + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(1,0,0,0))
                                [MakeLbl(PS.T, 8, FLinearColor(0.7f,0.7f,0.7f))]];
                }
                SyncBox->AddSlot().AutoHeight()[PickRow];
                T0->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Sync to Views"), SyncBox)];
            }

            // Alignment (onion skin + link) — right pane
            {
                TSharedRef<SVerticalBox> AlignBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> OnRow = SNew(SHorizontalBox);
                TSharedRef<SCheckBox> OnionCheck = SNew(SCheckBox)
                    .IsChecked(bOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { ToggleOnionSkin(S == ECheckBoxState::Checked); RefreshUI(); });
                OnionCheck->SetToolTipText(FText::FromString(TEXT("Ghosts the adjacent view's albedo at low opacity for alignment")));
                OnRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[OnionCheck];
                OnRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeLbl(TEXT("Onion skin"), 9, FLinearColor(0.6f,0.9f,0.7f))];
                OnRow->AddSlot().Padding(FMargin(6,2)).FillWidth(1.0f).VAlign(VAlign_Center)
                    [SNew(SSlider).Value(OnionSkinOpacity)
                        .OnValueChanged_Lambda([this](float V){ SetOnionSkinOpacity(V); })];
                OnRow->AddSlot().Padding(FMargin(4,2)).AutoWidth().VAlign(VAlign_Center)
                    [MakeLbl(TEXT("opacity"), 8, FLinearColor(0.6f,0.6f,0.6f))];
                AlignBox->AddSlot().AutoHeight()[OnRow];
                TSharedRef<SHorizontalBox> LinkRow = SNew(SHorizontalBox);
                TSharedRef<SCheckBox> LinkCheck = SNew(SCheckBox)
                    .IsChecked(bLinkAcrossViews ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                    { bLinkAcrossViews = (S == ECheckBoxState::Checked); });
                LinkCheck->SetToolTipText(FText::FromString(TEXT("Edits in this state are broadcast to all other states (Phase B)")));
                LinkRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()[LinkCheck];
                LinkRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeLbl(TEXT("Link transform across views"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                AlignBox->AddSlot().AutoHeight()[LinkRow];
                T0->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Alignment"), AlignBox)];
            }

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
                RailContent[3]->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Outline -> Depth"), OdBox)];
            }

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
                RailContent[2]->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Camera Follow"), CfBox)];
            }

            // Quick actions (batch operations)
            {
                TSharedRef<SVerticalBox> QaBox = SNew(SVerticalBox);
                TSharedRef<SHorizontalBox> QaRow = SNew(SHorizontalBox);
                QaRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                    [MakeBtn(TEXT("Auto-Fit All"), [this](){ ApplyAutoFitToAllSlots(); RefreshUI(); })];
                QaRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Sync All -> All"), [this](){ SyncAllLayersToAllViews(); RefreshUI(); })];
                QaRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                    [MakeBtn(TEXT("Clear All Overrides"), [this](){ ClearAllOverrides(); RefreshUI(); }, FLinearColor(1.0f,0.6f,0.6f))];
                QaRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
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
                RailContent[1]->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Quick Actions"), QaBox)];
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
                RailContent[1]->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Cross-View Transform"), XvBox)];
            }
        }

        // ============ DEBUG RAIL (import + config) ============
        {
            TSharedRef<SVerticalBox> T1 = SNew(SVerticalBox);

            TSharedRef<SVerticalBox> ImportBox = SNew(SVerticalBox);
            TSharedRef<SHorizontalBox> ImpRow = SNew(SHorizontalBox);
            ImpRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
                [MakeBtn(TEXT("Import Art..."), [this](){ OpenImportArtDialog(); }, FLinearColor(0.6f,0.8f,1.0f))];
            ImpRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
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
            T1->AddSlot().AutoHeight()
                [MakeSectionBox(TEXT("Import"), ImportBox)];

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

                T1->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Config"), CfgBox.ToSharedRef())];
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
                T1->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Edge Analysis"), EdBox)];
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
                T1->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Depth Debug"), DdBox)];
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
                T1->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Hull Review (click thumb = jump)"), HrBox)];
            }

            // Viseme frames grid (Phase E)
            {
                TSharedRef<SVerticalBox> VgBox = SNew(SVerticalBox);
                TSharedRef<SScrollBox> VgScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
                VisemeGridBox = SNew(SVerticalBox);
                VgScroll->AddSlot()[VisemeGridBox.ToSharedRef()];
                RebuildVisemeGrid();
                VgBox->AddSlot().AutoHeight()[VgScroll];
                T1->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Viseme Frames (click filled cell = play)"), VgBox)];
            }

            // Problems panel (Phase F)
            {
                TSharedRef<SVerticalBox> PrBox = SNew(SVerticalBox);
                TSharedRef<SScrollBox> PrScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
                ProblemsPanelBox = SNew(SVerticalBox);
                PrScroll->AddSlot()[ProblemsPanelBox.ToSharedRef()];
                RebuildProblemsPanel();
                PrBox->AddSlot().AutoHeight()[PrScroll];
                T1->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Problems (click row = jump)"), PrBox)];
            }

            TSharedRef<SScrollBox> DebugScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
            DebugScroll->AddSlot()[T1];
            RailContent[3]->AddSlot()[DebugScroll];
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
                };
                AddZoneEdit(0); AddZoneEdit(1); AddZoneEdit(2); AddZoneEdit(3);
                Cam->AddSlot().AutoHeight()[ZoneRow];

                T2->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Camera"), Cam)];
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
                T2->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Blend Preview"), BlendBox)];
            }
        }

        // ============ ADVANCED RAIL ============
        {
            TSharedRef<SVerticalBox> T3 = RailContent[4].ToSharedRef();

            // Cross-layer overlay
            CrossLayerBox = SNew(SVerticalBox);
            CrossLayerScroll = SNew(SScrollBox).Orientation(Orient_Vertical);
            CrossLayerScroll->AddSlot() [CrossLayerBox.ToSharedRef()];
            TextCrossLayer = SNew(STextBlock)
                .Text(FText::FromString(TEXT("Select a layer to show overlay")))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FLinearColor(0.5f,0.5f,0.5f));
            CrossLayerBox->AddSlot().AutoHeight().Padding(FMargin(2))
                [TextCrossLayer.ToSharedRef()];
            T3->AddSlot().AutoHeight()
                [MakeSectionBox(TEXT("All Layers (current state)"), CrossLayerScroll.ToSharedRef())];

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
                T3->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Param Reference"), RefBox)];
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
                T3->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Param Bindings (state + layer)"), PbBox)];
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
                            if (!GetSelectedPinElement(El, Count)) return;
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (!Comp) return;
                            El.Pin3D.bPinned = (S == ECheckBoxState::Checked);
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
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
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.Position3D.X = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });
                AddPinSliderRow(TEXT("Pin Y"), -2.0f, 2.0f, InitEl.Pin3D.Position3D.Y, 2, SliderPinY, TextPinY,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.Position3D.Y = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });
                AddPinSliderRow(TEXT("Pin Z"), -2.0f, 2.0f, InitEl.Pin3D.Position3D.Z, 2, SliderPinZ, TextPinZ,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.Position3D.Z = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });

                // View-angle rotation controls
                {
                    TSharedRef<SHorizontalBox> RotRow = SNew(SHorizontalBox);
                    CheckPinRotEnabled = SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState S)
                        {
                            FFaceNestedArt El;
                            int32 Count = 0;
                            if (!GetSelectedPinElement(El, Count)) return;
                            UFaceParallaxComponent* Comp = GetParallaxComponent();
                            if (!Comp) return;
                            El.Pin3D.bEnableViewAngleRotation = (S == ECheckBoxState::Checked);
                            Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                            RefreshUI();
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
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.MinRotation = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });
                AddPinSliderRow(TEXT("Max Rot"), -180.0f, 180.0f, InitEl.Pin3D.MaxRotation, 1, SliderPinMaxRot, TextPinMaxRot,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.MaxRotation = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });
                AddPinSliderRow(TEXT("Sens"), -10.0f, 10.0f, InitEl.Pin3D.RotationSensitivity, 2, SliderPinRotSens, TextPinRotSens,
                    [this](float V)
                    {
                        FFaceNestedArt El;
                        int32 Count = 0;
                        if (!GetSelectedPinElement(El, Count)) return;
                        UFaceParallaxComponent* Comp = GetParallaxComponent();
                        if (!Comp) return;
                        El.Pin3D.RotationSensitivity = V;
                        Comp->SetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex, El);
                        RefreshUI();
                    });

                Pin->AddSlot().AutoHeight().Padding(FMargin(0,2))
                    [MakeBtn(TEXT("Detect Profile"), [this](){ DetectFaceProfile(); RefreshUI(); })];
                Pin->AddSlot().AutoHeight().Padding(FMargin(0,4,0,0))
                    [MakeLbl(TEXT("Nested Elements"), 9, FLinearColor(0.6f,0.8f,1.0f))];
                NestedOutlinerBox = SNew(SVerticalBox);
                RebuildNestedOutliner();
                Pin->AddSlot().AutoHeight().Padding(FMargin(0,2,0,0))
                    [NestedOutlinerBox.ToSharedRef()];
                T3->AddSlot().AutoHeight()
                    [MakeSectionBox(TEXT("Nested Art / Pins"), Pin)];

                RefreshPinControls();
            }
        }

        PropScroll->AddSlot()
            [SNew(SBox).Padding(FMargin(0, 0, FPLayout::PropsScrollInsetR, 0))[SlotPropsBox.ToSharedRef()]];
        PropPanel->AddSlot().FillHeight(1.0f)[PropScroll.ToSharedRef()];
        TextStatus = MakeLbl(TEXT("Ready"), 9, FLinearColor(0.5f,0.8f,0.5f));
        PropPanel->AddSlot().AutoHeight().Padding(FMargin(6,2))
            [TextStatus.ToSharedRef()];
    }

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
        RailContent[0]->AddSlot().AutoHeight().Padding(FMargin(2,1))
            [MakeSectionBox(TEXT("Status Detail"), StatusMatrixScroll.ToSharedRef())];

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
            [MakeBtn(TEXT("Snapshot"), [this](){ SnapshotPreset(); if (TextStatus.IsValid()) TextStatus->SetText(FText::FromString(TEXT("Snapshot saved."))); })];
        TSharedRef<SButton> RestoreBtn = MakeBtn(TEXT("Restore Snapshot"), [this](){ RestoreSnapshot(); RefreshUI(); }, FLinearColor(1.0f,0.7f,0.3f));
        RestoreBtn->SetToolTipText(FText::FromString(TEXT("Restores the preset to the last saved Snapshot (this is not a generic undo)")));
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
