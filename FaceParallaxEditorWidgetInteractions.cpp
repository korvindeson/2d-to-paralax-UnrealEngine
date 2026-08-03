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
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Containers/Ticker.h"


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

// P1 one-map: the schematic glyph on the canvas and the legend chip under it
// share ONE interaction model — left-click picks the part. Resolve the part
// to its layer (explicit HotspotLayerMap first, then the derived match),
// select it, jump to the Art rail, set the 'Front -> Eyes' breadcrumb, pulse
// the glyph, and — when the layer still has NO art — open the native OS file
// picker (Phase 2, the primary import path: click part -> pick file -> done).
// Layers that already have art just get selected (the live preview is the
// review surface). The bulk Folder Wizard remains the secondary path from the
// Art rail ("Import Folder...") for multi-view folder scans. The old
// hotspot-region and layer-art-quad click layers were deleted: this core is
// the only pick path left.
void UFaceParallaxEditorWidget::SelectPartOrImport(const FString& PartName)
{
    if (PartName.IsEmpty()) return;
    const FName LayerTag = ResolveHotspotLayer(PartName);
    if (!LayerTag.IsValid())
    {
        SetSelectedLayer(FString());
        SetBreadcrumb(FString::Printf(TEXT("Unmapped → %s"), *PartName));
        SetStatus(FString::Printf(
            TEXT("Part '%s' has no mapped layer — right-click it (or the legend chip) to map it, or import art to assign one"),
            *PartName), AccentBlue());
        return;
    }
    SetSelectedLayer(LayerTag.ToString());
    FlashTab(1);                 // P7-A: amber pulse on the Art tab — visible rail-jump transition
    SetActiveRailIndex(1);   // Art rail: import + tweak controls
    const FString ClassName = UTF8_TO_TCHAR(FPSchematic::FPDepthClassName(
        FPSchematic::FPDepthClassForTag(TCHAR_TO_UTF8(*LayerTag.ToString()))));
    SetBreadcrumb(LayerTag.ToString() == PartName
        ? FString::Printf(TEXT("%s → %s"), *ClassName, *LayerTag.ToString())
        : FString::Printf(TEXT("%s → %s (%s)"), *ClassName, *LayerTag.ToString(), *PartName));
    if (SchematicLayer.IsValid())
    {
        SchematicFlashPart = PartName;
        SchematicFlashTimestamp = FSlateApplication::Get().GetCurrentTime();
        SchematicLayer->Invalidate(EInvalidateWidgetReason::Paint);
    }
    if (LayerHasFrontArt(LayerTag))
    {
        SetStatus(FString::Printf(TEXT("Part '%s' -> layer '%s' (art assigned — reviewing)"),
            *PartName, *LayerTag.ToString()), AccentBlue());
        return;
    }
    SetStatus(FString::Printf(TEXT("Part '%s' -> layer '%s' — import art for this part"),
        *PartName, *LayerTag.ToString()), AccentBlue());
    OpenImportArtDialog();
}

// Legend chip left-click: identical semantics to the canvas glyph (one map).
void UFaceParallaxEditorWidget::HandleHotspotClick(const FString& RegionName)
{
    SelectPartOrImport(RegionName);
}

// Canvas glyph left-click: identical semantics to the legend chip (one map).
void UFaceParallaxEditorWidget::HandleSchematicPartClick(const FString& PartName)
{
    SelectPartOrImport(PartName);
}

// P1 one-map breadcrumb: '<DepthClass> → <Layer>' (part shown in parens when
// the alias differs, e.g. 'Front → Mouth (Teeth)'). Shown next to the layer
// label above the canvas — the visible tab-switch transition.
void UFaceParallaxEditorWidget::SetBreadcrumb(const FString& Text)
{
    SchematicBreadcrumb = Text;
    if (BreadcrumbText.IsValid())
        BreadcrumbText->SetText(FText::FromString(Text));
}

// P7-A tab flash: amber-pulse the destination rail tab for ~0.9s so a
// programmatic rail jump (canvas click -> Art rail) is visually traceable.
// NativeTick repaints the tab bar while the pulse is live.
void UFaceParallaxEditorWidget::FlashTab(int32 RailIndex)
{
    TabFlashIndex = RailIndex;
    TabFlashUntil = FSlateApplication::Get().GetCurrentTime() + 0.9;
    if (TopTabBar.IsValid())
        TopTabBar->Invalidate(EInvalidateWidgetReason::Paint);
}

// Redesign: does the layer's Front-state slot carry an albedo texture? The
// schematic default view paints glyphs only for layers WITHOUT art, so this
// is the "art replaces the outline" gate.
bool UFaceParallaxEditorWidget::LayerHasFrontArt(FName LayerTag) const
{
    if (!ActivePreset || LayerTag.IsNone()) return false;
    return ActivePreset->GetSlot(EFaceAngleState::Front, LayerTag).Textures.Albedo != nullptr;
}

// ===== PHASE 2: DIRECT ART IMPORT =====

// Pure drop-target resolution (mirrored by TestPhase2DirectImportMirrors): a
// part hit under the drop point wins — its resolved layer becomes the target;
// otherwise the currently selected layer is the fallback; NAME_None means no
// target and the drop is rejected with a status hint.
FName UFaceParallaxEditorWidget::CanvasDropTargetLayer(const FName& ResolvedPartLayer,
    const FName& SelectedLayer)
{
    return ResolvedPartLayer.IsValid() ? ResolvedPartLayer : SelectedLayer;
}

// Canvas drop entry point: resolve the target layer (part hit wins, else the
// selection), select it when the drop landed on a part (the part's layer is
// then highlighted on canvas), and route the payload through the shared
// AssignImageDropToSlot pipeline (OS files imported, channels read from name
// suffixes, Content Browser textures assigned directly).
bool UFaceParallaxEditorWidget::HandleCanvasDrop(const FString& PartUnderCursor,
    const FName& ResolvedPartLayer, const FDragDropEvent& Ev)
{
    FName Target = CanvasDropTargetLayer(ResolvedPartLayer, SelectedLayerName);
    if (!Target.IsValid())
    {
        SetStatus(TEXT("Drop ignored: drop on a part (or select a layer first)"),
            FLinearColor::Yellow);
        return false;
    }
    if (ResolvedPartLayer.IsValid())
    {
        SetSelectedLayer(Target.ToString());
        RefreshSchematic();
    }
    return AssignImageDropToSlot(ActiveViewState, Target, Ev);
}

// ===== CENTRAL CANVAS REDESIGN: FILTERS + FOCUS (Phase 3) =====

// Layer chip clicked: add the layer to the multi-select, or remove it when
// already active (toggle). The empty selection = "all layers".
void UFaceParallaxEditorWidget::ToggleSchematicLayerFilter(const FString& LayerTag)
{
    if (LayerTag.IsEmpty()) return;
    const int32 Found = SchematicLayerFilter.IndexOfByPredicate(
        [&LayerTag](const FString& T) { return T == LayerTag; });
    if (Found == INDEX_NONE)
        SchematicLayerFilter.Add(LayerTag);
    else
        SchematicLayerFilter.RemoveAt(Found);
    RebuildSchematicFilterRow();
    RefreshSchematic();
}

// Depth radio: 0 = all classes, 1 = Front, 2 = Base, 3 = Back.
void UFaceParallaxEditorWidget::SetSchematicDepthFilter(int32 Depth)
{
    SchematicDepthFilter = FMath::Clamp(Depth, 0, 3);
    RebuildSchematicFilterRow();
    RefreshSchematic();
}

// Clear chip: back to "everything shows".
void UFaceParallaxEditorWidget::ClearSchematicFilters()
{
    SchematicDepthFilter = 0;
    SchematicLayerFilter.Reset();
    RebuildSchematicFilterRow();
    RefreshSchematic();
}

// Focus toggle: zoom-to-fit the selected layer's glyphs (no-op without a
// selection — the lens stays off until a layer is selected).
void UFaceParallaxEditorWidget::ToggleSchematicFocus()
{
    bSchematicFocus = !bSchematicFocus;
    RebuildSchematicFilterRow();
    RefreshSchematic();
}

// Cycle Preview (Phase 2): a scripted 8-second tour of the live animation
// systems — blink 2s, expression 2s, viseme 2s, orbit sweep 2s. All systems
// are already concurrent on the preview actor (Component.cpp), this just
// sequences them from the tool tab via NativeTick.
void UFaceParallaxEditorWidget::StartCyclePreview()
{
    if (bCyclePreviewActive) return;
    if (bLivePreviewActive) StopLivePreview();
    bCyclePreviewActive = true;
    CyclePhase = 0;
    CyclePhaseTime = 0.0f;
    SetBlinkingEnabled(true);
    SetExpressionByName(FName(TEXT("Neutral")));
    SetVisemeEnabled(false);
    SetOrbitYaw(0.0f);
    SetStatus(TEXT("Cycle Preview: blink -> expression -> viseme -> orbit sweep"), AccentBlue());
}

void UFaceParallaxEditorWidget::StopCyclePreview()
{
    bCyclePreviewActive = false;
    CyclePhase = -1;
    CyclePhaseTime = 0.0f;
    SetBlinkingEnabled(false);
    SetExpressionByName(FName(TEXT("Neutral")));
    SetVisemeEnabled(false);
    SetOrbitYaw(0.0f);
}

// Live Preview (Phase 4b): blink + expression + viseme + orbit all run at
// the same time so the assembled result can be checked in one go, unlike
// Cycle Preview which sequences the same four systems one at a time.
void UFaceParallaxEditorWidget::StartLivePreview()
{
    if (bLivePreviewActive) return;
    if (bCyclePreviewActive) StopCyclePreview();
    bLivePreviewActive = true;
    LivePreviewTime = 0.0f;
    SetBlinkingEnabled(true);
    SetExpressionByName(FName(TEXT("Smile")));
    SetVisemeEnabled(true);
    PlayVisemeByName(FName(TEXT("Ah")));
    SetOrbitYaw(0.0f);
    SetStatus(TEXT("Live Preview: blink + smile + viseme + orbit sweep together"), AccentBlue());
}

void UFaceParallaxEditorWidget::StopLivePreview()
{
    bLivePreviewActive = false;
    LivePreviewTime = 0.0f;
    SetBlinkingEnabled(false);
    SetExpressionByName(FName(TEXT("Neutral")));
    SetVisemeEnabled(false);
    SetOrbitYaw(0.0f);
}

void UFaceParallaxEditorWidget::NativeTick(const FGeometry&, float InDeltaTime)
{
    // Redesign: post-assign flash ring — keep the hotspot layer repainting
    // while the 1.5s pulse is live, then clear the flash state.
    if (AssignFlashTimestamp >= 0.0)
    {
        const double FlashAge = FSlateApplication::Get().GetCurrentTime() - AssignFlashTimestamp;
        if (FlashAge < 1.5)
        {
            if (HotspotLayer.IsValid())
                HotspotLayer->Invalidate(EInvalidateWidgetReason::Paint);
        }
        else
        {
            AssignFlashLayer.Reset();
            AssignFlashTimestamp = -1.0;
        }
    }
    // P1 glyph click pulse — keep the schematic layer repainting while the
    // 0.5s pulse is live, then clear it.
    if (SchematicFlashTimestamp >= 0.0)
    {
        const double FlashAge = FSlateApplication::Get().GetCurrentTime() - SchematicFlashTimestamp;
        if (FlashAge < 0.5)
        {
            if (SchematicLayer.IsValid())
                SchematicLayer->Invalidate(EInvalidateWidgetReason::Paint);
        }
        else
        {
            SchematicFlashPart.Reset();
            SchematicFlashTimestamp = -1.0;
        }
    }
    // P7-A tab flash - keep the top tab bar repainting while the ~0.9s
    // amber pulse is live, then clear it.
    if (TabFlashUntil > 0.0)
    {
        if (TabFlashUntil > FSlateApplication::Get().GetCurrentTime())
        {
            if (TopTabBar.IsValid())
                TopTabBar->Invalidate(EInvalidateWidgetReason::Paint);
        }
        else
        {
            TabFlashUntil = 0.0;
            TabFlashIndex = -1;
            if (TopTabBar.IsValid())
                TopTabBar->Invalidate(EInvalidateWidgetReason::Paint);
        }
    }
    // Live canvas: no setter re-arms the scene capture, so poll it here -
    // the render target re-captures at ~30Hz, keeping the canvas in sync
    // with texture/transform/orbit/view-state edits (imports, sliders, etc.).
    if (PreviewActor.IsValid())
    {
        CaptureCooldown -= InDeltaTime;
        if (CaptureCooldown <= 0.0f)
        {
            PreviewActor->RequestCapture();
            CaptureCooldown = 0.033f;
        }
    }
    if (bLivePreviewActive)
    {
        // Combined mode: blink keeps looping on the component, the Smile
        // expression stays applied, the "Ah" viseme re-triggers on a speech
        // cadence, and the orbit sweeps continuously (-45..+45 over 8s).
        SetOrbitYaw(45.0f * FMath::Sin(LivePreviewTime * 2.0f * PI / 8.0f));
        LivePreviewTime += InDeltaTime;
        if (LivePreviewTime >= 2.5f)
        {
            LivePreviewTime = 0.0f;
            PlayVisemeByName(FName(TEXT("Ah")));
        }
        return;
    }
    if (!bCyclePreviewActive) return;
    constexpr float PhaseDuration = 2.0f;
    const int32 Phase = CyclePhase;

    // Orbit phase sweeps yaw continuously (-45..+45) across its 2 seconds.
    if (Phase == 3)
        SetOrbitYaw(45.0f * FMath::Sin(CyclePhaseTime * 2.0f * PI / PhaseDuration));

    CyclePhaseTime += InDeltaTime;
    if (CyclePhaseTime < PhaseDuration) return;

    CyclePhaseTime = 0.0f;
    ++CyclePhase;
    switch (Phase)
    {
    case 0:   // blink done -> expression
        SetBlinkingEnabled(false);
        SetExpressionByName(FName(TEXT("Smile")));
        break;
    case 1:   // expression done -> viseme
        SetExpressionByName(FName(TEXT("Neutral")));
        PlayVisemeByName(FName(TEXT("Ah")));
        SetVisemeEnabled(true);
        break;
    case 2:   // viseme done -> orbit sweep
        SetVisemeEnabled(false);
        SetOrbitYaw(0.0f);
        break;
    case 3:   // orbit done -> reset everything
    default:
        StopCyclePreview();
        SetStatus(TEXT("Cycle Preview complete"), AccentBlue());
        break;
    }
}

// Explicit map first (persisted in the preset), then derived match against
// the union of all view layer tags, then the part-name alias table
// (Teeth->Mouth, Chin/Neck->Head — Phase 2 coverage). None when all three
// yield nothing.
FName UFaceParallaxEditorWidget::ResolveHotspotLayer(const FString& RegionName) const
{
    if (!ActivePreset) return FName();
    if (const FName* Mapped = ActivePreset->HotspotLayerMap.Find(RegionName))
    {
        if (Mapped->IsValid()) return *Mapped;
    }
    std::vector<std::string> TagStrings;
    for (const FName& Tag : GetUILayerTags())
        TagStrings.emplace_back(TCHAR_TO_UTF8(*Tag.ToString()));
    const char* Derived = FPLayout::FPHotspotLayerMatch(TagStrings, TCHAR_TO_UTF8(*RegionName));
    if (Derived) return FName(UTF8_TO_TCHAR(Derived));
    const char* Aliased = FPSchematic::FPSchematicLayerAlias(TCHAR_TO_UTF8(*RegionName));
    return Aliased ? FName(UTF8_TO_TCHAR(Aliased)) : FName();
}

// Persist an explicit region -> layer mapping (or clear when LayerTag is
// None), then refresh the strip so chip colors/labels update.
void UFaceParallaxEditorWidget::RemapHotspotLayer(const FString& RegionName, FName LayerTag)
{
    if (RegionName.IsEmpty()) return;
    if (!ActivePreset) return;
    if (LayerTag.IsValid())
        ActivePreset->HotspotLayerMap.Add(RegionName, LayerTag);
    else
        ActivePreset->HotspotLayerMap.Remove(RegionName);
    ActivePreset->MarkPackageDirty();
    RebuildPartsStrip();
    SetStatus(FString::Printf(TEXT("Hotspot '%s' -> %s"), *RegionName,
        LayerTag.IsValid() ? *LayerTag.ToString() : TEXT("derived (default)")), AccentBlue());
}

// Right-click remap menu for a parts-strip chip: one entry per primary layer
// plus an "auto (derived)" reset entry. Pushed as a Slate context menu.
void UFaceParallaxEditorWidget::OpenHotspotRemapMenu(const FString& RegionName, const FPointerEvent& Ev)
{
    if (!ActivePreset || !PartsStrip.IsValid()) return;
    FMenuBuilder Menu(true, nullptr);
    Menu.BeginSection("HotspotRemap",
        FText::FromString(FString::Printf(TEXT("Map '%s' to layer"), *RegionName)));
    const FName Current = ResolveHotspotLayer(RegionName);
    Menu.AddMenuEntry(FText::FromString(TEXT("Auto (derived)")),
        FText::FromString(TEXT("Use the automatic exact/plural/L-R/prefix derivation")),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([this, RegionName]()
        {
            RemapHotspotLayer(RegionName, FName());
        }),
        FCanExecuteAction(),
        FIsActionChecked::CreateLambda([this, RegionName]()
        {
            return !ActivePreset->HotspotLayerMap.Contains(RegionName);
        })));
    Menu.AddMenuSeparator();
    for (const FName& Tag : GetUILayerTags())
    {
        if (Tag.ToString() == RegionName) continue;   // already exact by name
        Menu.AddMenuEntry(FText::FromName(Tag),
            FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, RegionName, Tag]()
            {
                RemapHotspotLayer(RegionName, Tag);
            }),
            FCanExecuteAction(),
            FIsActionChecked::CreateLambda([this, RegionName, Tag]()
            {
                return ActivePreset->HotspotLayerMap.FindRef(RegionName) == Tag;
            })));
    }
    Menu.EndSection();
    FSlateApplication::Get().PushMenu(PartsStrip.ToSharedRef(), FWidgetPath(),
        Menu.MakeWidget(), Ev.GetScreenSpacePosition(),
        FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

// ====================================================================
// PHASE A: WORKSPACE RAIL
// ====================================================================

void UFaceParallaxEditorWidget::SetActiveRailIndex(int32 Index)
{
    ActiveRailIndex = FMath::Clamp(Index, 0, 4);
    RebuildWidget();
}

// ====================================================================
// PHASE 4b: RAIL ACCESSIBILITY (chips / jump / search / resizer)
// Mirrors: FPLayout::RailSectionTitles / FindRailSectionByTitle /
// ConfigSummary / VisemeSummary / RailWidthAfterDrag / QuickActionLabels
// (Tests/ParallaxMathTests.cpp::TestAccessibilityMirrors).
// ====================================================================

void UFaceParallaxEditorWidget::RegisterRailSection(int32 RailIdx, const FString& Title,
    TSharedRef<SWidget> Target, const TSharedPtr<SFaceAccordion>& Accordion, int32 AccordionIdx)
{
    if (!RailSections.IsValidIndex(RailIdx)) return;
    FFaceRailSection Sec(Title, Target);
    Sec.Accordion = Accordion;
    Sec.AccordionIdx = AccordionIdx;
    RailSections[RailIdx].Add(MoveTemp(Sec));
}

void UFaceParallaxEditorWidget::RegisterAccordionSections(int32 RailIdx, const TSharedPtr<SFaceAccordion>& Accordion)
{
    if (!Accordion.IsValid()) return;
    for (int32 i = 0; i < Accordion->NumSections(); ++i)
        RegisterRailSection(RailIdx, Accordion->SectionTitle(i),
            Accordion->GetSectionHeader(i), Accordion, i);
}

void UFaceParallaxEditorWidget::BuildRailSectionChips()
{
    for (int32 Ri = 0; Ri < 5 && Ri < RailChipsRows.Num() && Ri < RailSections.Num(); ++Ri)
    {
        RailChipsRows[Ri]->ClearChildren();
        for (int32 Si = 0; Si < RailSections[Ri].Num(); ++Si)
        {
            const FString Title = RailSections[Ri][Si].Title;
            const bool bActive = (ActiveChipRail == Ri && ActiveChipIdx == Si);
            TSharedRef<SButton> Chip = SNew(SButton)
                .ButtonColorAndOpacity(bActive ? AccentBlue() : FLinearColor(0.1f, 0.1f, 0.12f))
                .OnClicked_Lambda([this, Ri, Si]()
                {
                    JumpToRailSection(Ri, Si);
                    return FReply::Handled();
                })
                .Content()
                [SNew(STextBlock)
                    .Text(FText::FromString(Title))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.8f))];
            Chip->SetToolTipText(FText::FromString(TEXT("Jump to section: ") + Title));
            RailChipsRows[Ri]->AddSlot().AutoWidth().Padding(FMargin(2, 2, 0, 2))[Chip];
        }
    }
}

void UFaceParallaxEditorWidget::JumpToRailSection(int32 RailIdx, int32 SectionIdx)
{
    if (!RailSections.IsValidIndex(RailIdx) || !RailSections[RailIdx].IsValidIndex(SectionIdx))
        return;
    ActiveChipRail = RailIdx;
    ActiveChipIdx = SectionIdx;
    if (RailIdx != ActiveRailIndex)
    {
        // Rail switch rebuilds the tree; queue the jump for the rebuild end.
        PendingJumpRail = RailIdx;
        PendingJumpTitle = RailSections[RailIdx][SectionIdx].Title;
        SetActiveRailIndex(RailIdx);
        return;
    }
    const FFaceRailSection& Sec = RailSections[RailIdx][SectionIdx];
    if (Sec.Accordion.IsValid() && Sec.AccordionIdx >= 0)
        Sec.Accordion->SetExpanded(Sec.AccordionIdx, true);
    // No rail scrolling (P17 fit-first): every section is reachable without
    // a vertical scroll bar, so the jump only opens the accordion section.
    BuildRailSectionChips();
}

void UFaceParallaxEditorWidget::ConsumePendingJump()
{
    if (PendingJumpRail < 0) return;
    const int32 Ri = PendingJumpRail;
    const FString Title = PendingJumpTitle;
    PendingJumpRail = -1;
    PendingJumpTitle.Empty();
    if (!RailSections.IsValidIndex(Ri)) return;
    for (int32 Si = 0; Si < RailSections[Ri].Num(); ++Si)
    {
        if (RailSections[Ri][Si].Title == Title)
        {
            JumpToRailSection(Ri, Si);
            return;
        }
    }
}

void UFaceParallaxEditorWidget::OnRailSearchCommitted(const FString& Query)
{
    if (Query.IsEmpty()) return;
    int32 OutRail = -1;
    int32 OutIdx = -1;
    const std::string Q(TCHAR_TO_UTF8(*Query));
    if (FPLayout::FindRailSectionByTitle(Q, OutRail, OutIdx) == 0)
    {
        JumpToRailSection(OutRail, OutIdx);
        return;
    }
    if (TextStatus.IsValid())
        TextStatus->SetText(FText::FromString(
            FString::Printf(TEXT("No section matches '%s'"), *Query)));
}

void UFaceParallaxEditorWidget::UpdateDisclosureSummaries()
{
    if (!ConfigDisclosure.IsValid()) return;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    int32 NumOn = 0;
    if (Comp)
    {
        if (Comp->GetBlinkingEnabled()) ++NumOn;
        if (Comp->GetSwooshEnabled()) ++NumOn;
        if (Comp->GetNestedArtEnabled()) ++NumOn;
        if (Comp->GetParamsEnabled()) ++NumOn;
    }
    if (bLocalShowTextures) ++NumOn;
    if (bLocalShowDepthMesh) ++NumOn;
    if (bLocalShowWireframe) ++NumOn;
    if (bLocalColorByDepth) ++NumOn;
    ConfigDisclosure->SetSummary(FString(UTF8_TO_TCHAR(FPLayout::ConfigSummary(NumOn).c_str())));
}

float UFaceParallaxEditorWidget::GetRailWidthPx() const
{
    return RailWidthPx;
}

void UFaceParallaxEditorWidget::SetRailWidthLive(float W)
{
    RailWidthPx = (float)FPLayout::ClampRailWidth((double)W);
    if (RailWidthBox.IsValid())
        RailWidthBox->SetWidthOverride(RailWidthPx);
}

void UFaceParallaxEditorWidget::ApplyRailWidthDelta(float DeltaPx)
{
    SetRailWidthLive(RailWidthPx + DeltaPx);
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
    FWidgetUndoScope UndoScope(this, TEXT("Fill Missing Views"));
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

TArray<bool> UFaceParallaxEditorWidget::GetPickedSyncViews() const
{
    TArray<bool> Out;
    for (int32 i = 0; i < SyncViewCheckBoxes.Num() && i < 10; ++i)
        Out.Add(SyncViewCheckBoxes[i].IsValid() && SyncViewCheckBoxes[i]->IsChecked());
    return Out;
}

TArray<EFaceAngleState> UFaceParallaxEditorWidget::GetLinkTargetsForEditing(EFaceAngleState Active) const
{
    TArray<EFaceAngleState> Out;
    const TArray<bool> Picked = GetPickedSyncViews();
    std::vector<int> PickedVec;
    for (bool B : Picked) PickedVec.push_back(B ? 1 : 0);
    for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
    {
        if (FPLayout::FPLinkDestIsPicked(PickedVec, (int32)Active, i))
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

int32 UFaceParallaxEditorWidget::GizmoHitTest(const FFaceArtTransform& T,
    const FVector2D& Local, const FVector2D& CanvasSize)
{
    if (CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f) return kGizmoHitNone;
    const FVector2D Center = CanvasSize * 0.5f + GizmoUVToPixels(T.Position, CanvasSize);
    const FVector2D Half = GizmoUVToPixels(T.Scale, CanvasSize) * 0.5f;
    if (Half.X < 1.0f || Half.Y < 1.0f) return kGizmoHitNone;   // degenerate box: no handles

    const float Rad = FMath::DegreesToRadians(T.Rotation);
    const float CosR = FMath::Cos(Rad), SinR = FMath::Sin(Rad);
    auto Rot = [CosR, SinR](const FVector2D& V)
    {
        return FVector2D(V.X * CosR - V.Y * SinR, V.X * SinR + V.Y * CosR);
    };
    const FVector2D Corners[4] = {
        Center + Rot(FVector2D(-Half.X, -Half.Y)),
        Center + Rot(FVector2D(Half.X, -Half.Y)),
        Center + Rot(FVector2D(Half.X, Half.Y)),
        Center + Rot(FVector2D(-Half.X, Half.Y)),
    };

    // Handles take priority over the move ring (same 14px radius as the pin
    // drag handle, and drawn larger than the box edges so they are easy to hit).
    const FVector2D RotHandle = Center + Rot(FVector2D(0.0f, -Half.Y - 14.0f));
    if (FVector2D::Distance(Local, RotHandle) <= 14.0f) return kGizmoHitRotate;
    if (FVector2D::Distance(Local, Corners[2]) <= 14.0f) return kGizmoHitScale;

    // Move ring: the four box edges, +/-7px. The interior is a deliberate miss
    // so part glyphs behind the box stay clickable (P1 one-map).
    for (int32 e = 0; e < 4; ++e)
    {
        const FVector2D A = Corners[e];
        const FVector2D B = Corners[(e + 1) % 4];
        const FVector2D AB = B - A;
        const float Len2 = AB.SizeSquared();
        if (Len2 < 1.0f) continue;
        const float Tt = FMath::Clamp(FVector2D::DotProduct(Local - A, AB) / Len2, 0.0f, 1.0f);
        if (FVector2D::Distance(Local, A + AB * Tt) <= 7.0f) return kGizmoHitMove;
    }
    return kGizmoHitNone;
}

FFaceArtTransform UFaceParallaxEditorWidget::GizmoApplyDrag(const FFaceArtTransform& StartT,
    int32 Mode, const FVector2D& StartPx, const FVector2D& CurPx, const FVector2D& CanvasSize)
{
    FFaceArtTransform T = StartT;
    if (CanvasSize.X <= 0.0f || CanvasSize.Y <= 0.0f) return T;
    const FVector2D Center = CanvasSize * 0.5f + GizmoUVToPixels(StartT.Position, CanvasSize);

    if (Mode == kGizmoHitMove)
    {
        // Pixel delta -> UV delta, applied on top of the drag-start position
        // (never incremental, so a mid-drag re-entry cannot drift).
        const FVector2D DeltaUV = GizmoPixelsToUV(CurPx - StartPx, CanvasSize);
        T.Position = StartT.Position + DeltaUV;
    }
    else if (Mode == kGizmoHitRotate)
    {
        const float D0 = FVector2D::Distance(StartPx, Center);
        if (D0 < 1.0f) return T;   // degenerate grab point: keep the starting transform
        const float A0 = FMath::Atan2(StartPx.Y - Center.Y, StartPx.X - Center.X);
        const float A1 = FMath::Atan2(CurPx.Y - Center.Y, CurPx.X - Center.X);
        float Delta = FMath::RadiansToDegrees(A1 - A0);
        // Normalize the per-drag delta to [-180, 180] so a full circle sweeps
        // without a jump, then clamp the accumulated rotation to +/-360.
        Delta = FMath::Fmod(Delta + 540.0f, 360.0f) - 180.0f;
        T.Rotation = FMath::Clamp(StartT.Rotation + Delta, -360.0f, 360.0f);
    }
    else if (Mode == kGizmoHitScale)
    {
        const float D0 = FVector2D::Distance(StartPx, Center);
        if (D0 < 1.0f) return T;   // degenerate anchor: keep the starting transform
        float Factor = FVector2D::Distance(CurPx, Center) / D0;
        Factor = FMath::Clamp(Factor, 0.02f, 50.0f);
        // Uniform scale (single corner), then the per-axis UPROPERTY clamps.
        T.Scale = FVector2D(
            FMath::Clamp(StartT.Scale.X * Factor, 0.01f, 100.0f),
            FMath::Clamp(StartT.Scale.Y * Factor, 0.01f, 100.0f));
    }
    return T;
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
    FWidgetUndoScope UndoScope(this, TEXT("Set Layer Transform"));
    if (bLinkAcrossViews)
    {
        // Phase D: link broadcasts to the PICKED destination views; with no
        // picks it falls back to every other view (the Phase B contract).
        for (EFaceAngleState S : GetLinkTargetsForEditing(State))
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
        ? GetLinkTargetsForEditing(ActiveViewState)
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
    if (OnionCheckBox.IsValid())
    {
        OnionCheckBox->SetIsChecked(bOnionSkin ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    }
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
    RefreshSyncDestDiff();
}

void UFaceParallaxEditorWidget::RefreshSyncDestDiff()
{
    for (TSharedPtr<STextBlock>& Lbl : SyncDestLabels)
    {
        if (Lbl.IsValid())
            Lbl->SetColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.8f));
    }
    if (!SelectedLayerName.IsValid() || !ActivePreset) return;
    const FFaceArtSlot& ActiveSlot = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
    const FFaceArtTransform Active = ActiveSlot.CanonicalTransform;
    const bool bActiveHasArt = ActiveSlot.Textures.Albedo != nullptr;
    for (int32 i = 0; i < SyncDestLabels.Num() && i <= (int32)EFaceAngleState::Bottom; ++i)
    {
        if (i == (int32)ActiveViewState) continue;
        TSharedPtr<STextBlock>& Lbl = SyncDestLabels[i];
        if (!Lbl.IsValid()) continue;
        const FFaceArtSlot& Dest = ActivePreset->GetSlot((EFaceAngleState)i, SelectedLayerName);
        const FFaceArtTransform DT = Dest.CanonicalTransform;
        const bool bTransformEqual = DT.Position == Active.Position && DT.Scale == Active.Scale && DT.Rotation == Active.Rotation;
        const int Diff = FPLayout::FPSyncDestDiff(SyncOp, bActiveHasArt, Dest.Textures.Albedo != nullptr, bTransformEqual);
        switch (Diff)
        {
        case FPLayout::SyncDestMissing:
            Lbl->SetColorAndOpacity(FLinearColor(1.0f, 0.4f, 0.4f));    // red = apply would fill empty art
            break;
        case FPLayout::SyncDestDiffers:
            Lbl->SetColorAndOpacity(FLinearColor(1.0f, 0.7f, 0.3f));    // amber = apply overwrites
            break;
        default:
            Lbl->SetColorAndOpacity(FLinearColor(0.6f, 0.85f, 0.6f));   // green = already matches
            break;
        }
    }
}

void UFaceParallaxEditorWidget::OpenImportFolderWizard(const FString& PreselectPart)
{
    if (!GEditor) return;

    struct FWizardState
    {
        FString Folder;
        TArray<FString> Files;              // full paths (images found in folder)
        TArray<int32> PartOfFile;           // part index per file
        TArray<FString> Parts;              // unique part names
        int32 SelectedPart = -1;
        std::function<void(const TArray<FString>&)> ApplyFiles;  // shared scan/drop parse
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

    // Folder row — P4: the whole row is the wizard's drop zone (drag image
    // files straight in; they are parsed exactly like a Scan).
    TSharedRef<SEditableTextBox> FolderEdit = SNew(SEditableTextBox)
        .Text(FText::FromString(TEXT("")))
        .HintText(FText::FromString(TEXT("Folder with textures (or drop files here)...")));
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
    TSharedRef<SFaceDropTarget> FolderDrop = SNew(SFaceDropTarget)
        .OnFaceDragOver_Lambda([](const FGeometry&, const FDragDropEvent& Evt) -> FReply
        {
            TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
            if (FileOp.IsValid() && FileOp->HasFiles())
            {
                for (const FString& File : FileOp->GetFiles())
                    if (IsDroppableImageFile(File)) return FReply::Handled();
            }
            return FReply::Unhandled();
        })
        .OnFaceDrop_Lambda([W, CB, FolderEdit](const FGeometry&, const FDragDropEvent& Evt) -> FReply
        {
            TSharedPtr<FExternalDragOperation> FileOp = Evt.GetOperationAs<FExternalDragOperation>();
            if (!FileOp.IsValid() || !FileOp->HasFiles()) return FReply::Unhandled();
            TArray<FString> Files;
            for (const FString& File : FileOp->GetFiles())
                if (IsDroppableImageFile(File)) Files.Add(File);
            if (Files.Num() == 0) return FReply::Handled();
            W->Folder = FPaths::GetPath(Files[0]);
            FolderEdit->SetText(FText::FromString(W->Folder));
            if (W->ApplyFiles) W->ApplyFiles(Files);
            return FReply::Handled();
        })
        [FolderRow];
    RootV->AddSlot().AutoHeight()[FolderDrop];

    // Scan row
    TSharedRef<STextBlock> WizardStatus = SNew(STextBlock)
        .Text(FText::FromString(TEXT("Pick a folder, then Scan.")))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
        .ColorAndOpacity(FLinearColor(0.7f,0.7f,0.7f));

    // Shared parse step for both Scan and dropped files: split file names
    // into parts (channel + view-state suffixes stripped), preselect the
    // requested part, and rebuild the parts carousel + preview grid.
    W->ApplyFiles = [W, CB, WizardStatus, PreselectPart](const TArray<FString>& Found)
    {
        W->Files.Reset(); W->PartOfFile.Reset(); W->Parts.Reset(); W->SelectedPart = -1;
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
        if (!PreselectPart.IsEmpty())
        {
            const int32 PreIdx = W->Parts.IndexOfByKey(PreselectPart);
            if (PreIdx != INDEX_NONE)
            {
                W->SelectedPart = PreIdx;
                WizardStatus->SetText(FText::FromString(FString::Printf(
                    TEXT("Scanned %s: %d parts, %d matching files - part '%s' preselected."),
                    *W->Folder, W->Parts.Num(), W->Files.Num(), *PreselectPart)));
            }
        }
        if (CB->RebuildParts) CB->RebuildParts();
        if (CB->RebuildPreview) CB->RebuildPreview();
    };

    TSharedRef<SHorizontalBox> ScanRow = SNew(SHorizontalBox);
    ScanRow->AddSlot().Padding(FMargin(8,4)).FillWidth(1.0f).VAlign(VAlign_Center)[WizardStatus];
    ScanRow->AddSlot().Padding(FMargin(0,4,8,4)).AutoWidth()
        [SNew(SButton)
            .OnClicked_Lambda([W, FolderEdit]()
            {
                W->Folder = FolderEdit->GetText().ToString();
                if (W->Folder.IsEmpty()) return FReply::Handled();
                TArray<FString> Found;
                for (const TCHAR* Ext : {TEXT("*.png"), TEXT("*.jpg"), TEXT("*.jpeg"), TEXT("*.tga")})
                    IFileManager::Get().FindFilesRecursive(Found, *W->Folder, Ext, true, false);
                if (W->ApplyFiles) W->ApplyFiles(Found);
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
    PreviewBox->AddSlot().AutoHeight().Padding(FMargin(8,2,8,0))
        [SNew(SBox).HeightOverride(240)[PreviewRows]];
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
        [SNew(SFaceFlashButton).Text(TEXT("Apply to Active Layer"))
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
                if (PreviewActor.IsValid() && PreviewActor->FaceParallax)
                {
                    PreviewActor->FaceParallax->ApplyCurrentStateTextures();
                    PreviewActor->RequestCapture();
                }
                RefreshTextureThumbs();
                RefreshUI();
                // Import completion (P3): report the post-apply coverage across
                // all 10 states on the active layer, mirroring
                // FPLayout::ImportCoverageSummary.
                {
                    int32 WithA = 0, WithN = 0, WithD = 0;
                    for (int32 S = 0; S <= (int32)EFaceAngleState::Bottom; ++S)
                    {
                        const FFaceTextureSet T = ActivePreset->GetTexturesForSlot((EFaceAngleState)S, SelectedLayerName);
                        if (T.Albedo) ++WithA;
                        if (T.Normal) ++WithN;
                        if (T.Depth) ++WithD;
                    }
                    const std::string Cov = FPLayout::ImportCoverageSummary(10, WithA, WithN, WithD);
                    SetStatus(FString::Printf(TEXT("Wizard: imported %d, assigned %d for part '%s' | %s"),
                        Imported.Num(), Assigned, *W->Parts[W->SelectedPart], UTF8_TO_TCHAR(Cov.c_str())),
                        FLinearColor(0.3f, 1.0f, 0.3f));
                }
                // P6: the button flashes "\u2713" for 0.6s before the window
                // closes, so the confirmation is seen at the point of action.
                FTSTicker::GetCoreTicker().AddTicker(
                    FTickerDelegate::CreateLambda([Window](float) -> bool
                    {
                        Window->RequestDestroyWindow();
                        return false;
                    }), 0.6f);
                return FReply::Handled();
            })];
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

// Phase C: unified inspect mode (0 Textured, 1 Outline, 2 Depth, 3 Wireframe,
// 4 Depth Heatmap). Applies the canonical toggle combo (FPLayout::
// InspectComboForMode) to the SAME five booleans the Diagnostics rail Config
// checks own — the checks stay the single source of truth, and the canvas
// row highlight re-derives from them on RefreshUI.
void UFaceParallaxEditorWidget::SetInspectMode(int32 Mode)
{
    if (Mode < 0 || Mode > 4) return;
    const FPLayout::FPInspectCombo B = FPLayout::InspectComboForMode(Mode);
    bLocalShowTextures = B.T;
    bLocalShowDepthMesh = B.D;
    bLocalShowWireframe = B.W;
    bLocalColorByDepth = B.C;
    if (CheckShowTextures.IsValid()) CheckShowTextures->SetIsChecked(B.T);
    if (CheckDepthMesh.IsValid()) CheckDepthMesh->SetIsChecked(B.D);
    if (CheckWireframe.IsValid()) CheckWireframe->SetIsChecked(B.W);
    if (CheckColorByDepth.IsValid()) CheckColorByDepth->SetIsChecked(B.C);
    SetOutlineOverlayVisible(B.O);
    SetStatus(FString::Printf(TEXT("Inspect mode: %s"),
        UTF8_TO_TCHAR(FPLayout::InspectModeLabel(Mode))), AccentBlue());
    RefreshUI();
}

// Phase I: group-colored edge map toggles (Canvas Options). Pushes both
// flags into the schematic layer so the paint path and the menu agree.
void UFaceParallaxEditorWidget::SetSchematicEdgeMap(bool bOn)
{
    bSchematicEdgeMap = bOn;
    if (SchematicLayer.IsValid())
        SchematicLayer->SetEdgeMap(bSchematicEdgeMap, bEdgeMapHairEdges);
    RefreshUI();
}

void UFaceParallaxEditorWidget::SetEdgeMapHairEdges(bool bOn)
{
    bEdgeMapHairEdges = bOn;
    if (SchematicLayer.IsValid())
        SchematicLayer->SetEdgeMap(bSchematicEdgeMap, bEdgeMapHairEdges);
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

// Redesign: Depth Overlay checkbox on the canvas mode row — composites the
// selected layer's depth map over the live preview (live + depth in one
// view) at low opacity.
void UFaceParallaxEditorWidget::ToggleDepthOverlay(bool bEnable)
{
    bDepthOverlayVisible = bEnable;
    SetStatus(FString::Printf(TEXT("Depth overlay %s"),
        bEnable ? TEXT("on — composited over the live preview") : TEXT("off")), AccentBlue());
    RefreshUI();
}

// Redesign: rebuild the depth-composite overlay texture from the selected
// layer's depth map (raw pass-through — the Depth checkbox just wants the
// map visible over the live view, so no processing is needed).
void UFaceParallaxEditorWidget::BuildDepthOverlay()
{
    DepthOverlayTexture = nullptr;
    if (!ValidatePreset() || !SelectedLayerName.IsValid()) return;
    UTexture2D* Tex = ActivePreset->GetTexturesForSlot(ActiveViewState, SelectedLayerName).Depth;
    if (!Tex) return;
    DepthOverlayTexture = Tex;
    DepthOverlayBrush.SetResourceObject(Tex);
    DepthOverlayBrush.ImageSize = FVector2D((float)Tex->GetSizeX(), (float)Tex->GetSizeY());
    DepthOverlayBrush.DrawAs = ESlateBrushDrawType::Image;
}

// Redesign: canvas drag-resize (SFaceCanvasResizer). Widget state only —
// the Phase H design constant stays the default; clamps keep the canvas
// usable.
void UFaceParallaxEditorWidget::SetCanvasHeight(float Height)
{
    const float NewHeight = FMath::Clamp(Height, 220.0f, 900.0f);
    if (NewHeight != CanvasHeight)
    {
        CanvasHeight = NewHeight;
        if (PreviewHost.IsValid())
        {
            PreviewHost->Invalidate(EInvalidateWidgetReason::Layout);
        }
    }
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
    // P22: five 28px-wide thumbs per row (28x21, 4:3) + 2px gaps keep the
    // grid at 160px inside the 168px rail budget.
    TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
    static const TCHAR* HullShort[] = {
        TEXT("F"), TEXT("3R"), TEXT("PR"), TEXT("BR"), TEXT("B"),
        TEXT("BL"), TEXT("PL"), TEXT("3L"), TEXT("TP"), TEXT("BT"),
    };
    for (int32 i = 0; i <= (int32)EFaceAngleState::Bottom; ++i)
    {
        HullThumbBrushes[i] = FSlateBrush();
        if (ValidatePreset() && SelectedLayerName.IsValid())
        {
            UTexture2D* T = ActivePreset->GetTexturesForSlot((EFaceAngleState)i, SelectedLayerName).Albedo;
            if (T)
            {
                HullThumbBrushes[i].SetResourceObject(T);
                HullThumbBrushes[i].ImageSize = FVector2D(28.0f, 21.0f);
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
.OnFaceDrop_Lambda([this, St](const FGeometry&, const FDragDropEvent& Evit) -> FReply
                        {
                            // P7-F: hull thumbs drop into exactly this state —
                            // the shared pipeline maps the dropped assets/files
                            // onto the layer's Albedo/Normal/Depth by suffix.
                            return AssignImageDropToSlot(St, SelectedLayerName, Evit)
                                ? FReply::Handled() : FReply::Unhandled();
                        })
                        [SNew(SBox).WidthOverride(28).HeightOverride(21)
                            [SNew(SImage).Image(&HullThumbBrushes[i])]]]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [SNew(STextBlock)
                        .Text(FText::FromString(HullShort[i]))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                        .ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))]];
        Btn->SetToolTipText(FText::FromString(Name));
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
    // P7-C: a negative index now means "the layer pin itself" (the Pins list
    // "Layer pin" row) instead of silently clamping to element 0 — the pin
    // sliders then edit the whole-layer pin.
    if (SelectedNestedElementIndex < 0) return false;
    OutEl = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex);
    return true;
}

FVector2D UFaceParallaxEditorWidget::GetSelectedPinUV()
{
    FFaceNestedArt El;
    int32 Count = 0;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return FVector2D(-1.0f, -1.0f);
    if (GetSelectedPinElement(El, Count) && El.Pin3D.bPinned)
        return Comp->ProjectPinToUVForState(El.Pin3D.Position3D, ActiveViewState);
    // P3: whole-layer pin — draggable even when the layer has no nested
    // elements (projection mirrors FPLayout::PinProjectToUV).
    if (SelectedLayerName.IsValid() && ActivePreset)
    {
        const FFaceArtSlot SlotRec = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
        if (SlotRec.LayerPin3D.bPinned)
            return Comp->ProjectPinToUVForState(SlotRec.LayerPin3D.Position3D, ActiveViewState);
    }
    return FVector2D(-1.0f, -1.0f);
}

void UFaceParallaxEditorWidget::SetGizmoPinUV(const FVector2D& UV)
{
    FFaceNestedArt El;
    int32 Count = 0;
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp) return;
    const FVector2D Clamped(FMath::Clamp(UV.X, 0.0f, 1.0f), FMath::Clamp(UV.Y, 0.0f, 1.0f));
    if (GetSelectedPinElement(El, Count) && El.Pin3D.bPinned)
    {
        SetNestedPinFromUV(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex,
            ActiveViewState, Clamped);
        RefreshPinControls();
        return;
    }
    // P3: whole-layer pin (LayerPin3D), authored with the mirrored zone-frame
    // math (FPLayout::LayerPinFromUV) that SetNestedPinFromUV inlines for
    // nested elements. Zone yaw decides which head axis the U axis maps to.
    if (!SelectedLayerName.IsValid() || !ActivePreset) return;
    FFaceArtSlot SlotRec = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
    if (!SlotRec.LayerPin3D.bPinned) return;
    double PX = 0.0, PY = 0.0, PZ = 0.0;
    FPLayout::LayerPinFromUV(Clamped.X, Clamped.Y, Comp->GetZoneCenterYaw(ActiveViewState), PX, PY, PZ);
    SlotRec.LayerPin3D.Position3D = FVector((float)PX, (float)PY, (float)PZ);
    ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, SlotRec);
    RefreshPinControls();
}

// P3: pin-mode primary interaction — clicking empty canvas PLACES a pin at
// that UV (the selected nested element, or the whole-layer pin when no
// element is selected). Same zone-frame math as SetGizmoPinUV, wrapped in an
// undo scope; the gizmo/hotspot layers pick it up on the next RefreshUI.
bool UFaceParallaxEditorWidget::ConsumePendingPinPlacement()
{
    const bool bArmed = bPendingPinPlacement;
    bPendingPinPlacement = false;
    return bArmed;
}

void UFaceParallaxEditorWidget::PlacePinAtUV(const FVector2D& UV)
{
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !SelectedLayerName.IsValid() || !ActivePreset) return;
    FWidgetUndoScope UndoScope(this, TEXT("Place Pin"));
    const FVector2D Clamped(FMath::Clamp(UV.X, 0.0f, 1.0f), FMath::Clamp(UV.Y, 0.0f, 1.0f));
    FFaceNestedArt El;
    int32 Count = 0;
    if (GetSelectedPinElement(El, Count))
    {
        SetNestedPinFromUV(ActiveViewState, SelectedLayerName, SelectedNestedElementIndex,
            ActiveViewState, Clamped);
        SetStatus(FString::Printf(TEXT("Pin placed on '%s' (element %d/%d)"),
            *SelectedLayerName.ToString(), SelectedNestedElementIndex + 1, Count),
            FLinearColor(0.5f, 1.0f, 0.5f));
        RefreshUI();
        return;
    }
    FFaceArtSlot SlotRec = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
    double PX = 0.0, PY = 0.0, PZ = 0.0;
    FPLayout::LayerPinFromUV(Clamped.X, Clamped.Y, Comp->GetZoneCenterYaw(ActiveViewState), PX, PY, PZ);
    SlotRec.LayerPin3D.bPinned = true;
    SlotRec.LayerPin3D.Position3D = FVector((float)PX, (float)PY, (float)PZ);
    ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, SlotRec);
    SetStatus(FString::Printf(TEXT("Layer pin placed on '%s'"), *SelectedLayerName.ToString()),
        FLinearColor(0.5f, 1.0f, 0.5f));
    RefreshUI();
}

void UFaceParallaxEditorWidget::RefreshPinControls()
{
    FFaceNestedArt El;
    int32 Count = 0;
    const bool bHasElement = GetSelectedPinElement(El, Count);
    // P3: with no nested element selected, the pin controls edit the slot's
    // whole-layer pin (LayerPin3D) instead of staying disabled.
    FFaceArtSlot LS;
    const bool bHasLayerPin = !bHasElement && SelectedLayerName.IsValid() && ActivePreset;
    if (bHasLayerPin) LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
    const bool bControls = bHasElement || bHasLayerPin;

    if (TextPinIndex.IsValid())
    {
        FString IdxStr = bHasElement
            ? FString::Printf(TEXT("%d/%d"), SelectedNestedElementIndex + 1, Count)
            : (bHasLayerPin ? TEXT("Layer") : TEXT("0/0"));
        TextPinIndex->SetText(FText::FromString(IdxStr));
    }
    if (CheckPinPinned.IsValid())
    {
        const bool bPinned = bHasElement ? El.Pin3D.bPinned
            : (bHasLayerPin ? LS.LayerPin3D.bPinned : false);
        CheckPinPinned->SetIsChecked(bPinned ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    }
    if (CheckPinRotEnabled.IsValid())
    {
        const bool bRot = bHasElement ? El.Pin3D.bEnableViewAngleRotation
            : (bHasLayerPin ? LS.LayerPin3D.bEnableViewAngleRotation : false);
        CheckPinRotEnabled->SetIsChecked(bRot ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
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
    SetCtrlEnabled(CheckPinPinned, bControls);
    SetCtrlEnabled(CheckPinRotEnabled, bControls);
    SetCtrlEnabled(SliderPinX, bControls);
    SetCtrlEnabled(SliderPinY, bControls);
    SetCtrlEnabled(SliderPinZ, bControls);
    SetCtrlEnabled(SliderPinMinRot, bControls);
    SetCtrlEnabled(SliderPinMaxRot, bControls);
    SetCtrlEnabled(SliderPinRotSens, bControls);
    SetCtrlEnabled(SliderPinMinScale, bControls);
    // P3: X/Y/Z are shown as 0-100% of the layer's own UV frame (Position3D
    // spans -1..1 after the SetNestedPinFromUV round-trip), so the numbers
    // mean something without opening the 3D view: 50% = center, 0%/100% =
    // left/right (X), top/bottom (Y), back/front (Z).
    auto PctReadout = [](float V) { return FString::Printf(TEXT("%.0f%%"), (V + 1.0f) * 50.0f); };
    const FFacePin3D P = bHasElement ? El.Pin3D : LS.LayerPin3D;
    if (bHasElement || bHasLayerPin)
    {
        SetSliderReadout(SliderPinX, TextPinX, (P.Position3D.X + 1.0f) * 50.0f, 0.0f, 100.0f,
            PctReadout(P.Position3D.X));
        SetSliderReadout(SliderPinY, TextPinY, (P.Position3D.Y + 1.0f) * 50.0f, 0.0f, 100.0f,
            PctReadout(P.Position3D.Y));
        SetSliderReadout(SliderPinZ, TextPinZ, (P.Position3D.Z + 1.0f) * 50.0f, 0.0f, 100.0f,
            PctReadout(P.Position3D.Z));
        SetSliderReadout(SliderPinMinRot, TextPinMinRot, P.MinRotation, -180.0f, 180.0f,
            FString::Printf(TEXT("%.1f\u00b0"), P.MinRotation));
        SetSliderReadout(SliderPinMaxRot, TextPinMaxRot, P.MaxRotation, -180.0f, 180.0f,
            FString::Printf(TEXT("%.1f\u00b0"), P.MaxRotation));
        SetSliderReadout(SliderPinRotSens, TextPinRotSens, P.RotationSensitivity, -10.0f, 10.0f,
            FString::Printf(TEXT("%.2f"), P.RotationSensitivity));
        SetSliderReadout(SliderPinMinScale, TextPinMinScale, P.MinScale, 0.05f, 1.0f,
            FString::Printf(TEXT("%.2f"), P.MinScale));
    }
    else
    {
        SetSliderReadout(SliderPinX, TextPinX, 50.0f, 0.0f, 100.0f, TEXT("50%"));
        SetSliderReadout(SliderPinY, TextPinY, 50.0f, 0.0f, 100.0f, TEXT("50%"));
        SetSliderReadout(SliderPinZ, TextPinZ, 50.0f, 0.0f, 100.0f, TEXT("50%"));
        SetSliderReadout(SliderPinMinRot, TextPinMinRot, 0.0f, -180.0f, 180.0f, FString::Printf(TEXT("%.1f\u00b0"), 0.0f));
        SetSliderReadout(SliderPinMaxRot, TextPinMaxRot, 0.0f, -180.0f, 180.0f, FString::Printf(TEXT("%.1f\u00b0"), 0.0f));
        SetSliderReadout(SliderPinRotSens, TextPinRotSens, 1.0f, -10.0f, 10.0f, FString::Printf(TEXT("%.2f"), 1.0f));
        SetSliderReadout(SliderPinMinScale, TextPinMinScale, 0.5f, 0.05f, 1.0f, FString::Printf(TEXT("%.2f"), 0.5f));
    }

    // Jiggle controls — nested elements only (layer pins have no jiggle).
    if (CheckJiggleEnabled.IsValid())
    {
        CheckJiggleEnabled->SetIsChecked(bHasElement && El.bJiggleEnabled
            ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
    }
    SetCtrlEnabled(CheckJiggleEnabled, bHasElement);
    const bool bJiggleActive = bHasElement && El.bJiggleEnabled;
    for (const TSharedPtr<SSlider>& Sl : { SliderJiggleStiffness, SliderJiggleDamping,
        SliderJiggleImpulse, SliderJiggleMidpoint, SliderJiggleEndStiffness,
        SliderJiggleEndDamping, SliderJiggleEndImpulse })
    {
        SetCtrlEnabled(Sl, bJiggleActive);
    }
    if (bHasElement)
    {
        SetSliderReadout(SliderJiggleStiffness, TextJiggleStiffness, El.JiggleSettings.Stiffness, 0.0f, 20.0f,
            FString::Printf(TEXT("%.2f"), El.JiggleSettings.Stiffness));
        SetSliderReadout(SliderJiggleDamping, TextJiggleDamping, El.JiggleSettings.Damping, 0.0f, 5.0f,
            FString::Printf(TEXT("%.2f"), El.JiggleSettings.Damping));
        SetSliderReadout(SliderJiggleImpulse, TextJiggleImpulse, El.JiggleSettings.ImpulseScale, 0.0f, 10.0f,
            FString::Printf(TEXT("%.2f"), El.JiggleSettings.ImpulseScale));
        SetSliderReadout(SliderJiggleMidpoint, TextJiggleMidpoint, El.JiggleSettings.Midpoint, 0.0f, 1.0f,
            FString::Printf(TEXT("%.2f"), El.JiggleSettings.Midpoint));
        SetSliderReadout(SliderJiggleEndStiffness, TextJiggleEndStiffness, El.JiggleSettings.EndStiffness, 0.0f, 20.0f,
            FString::Printf(TEXT("%.2f"), El.JiggleSettings.EndStiffness));
        SetSliderReadout(SliderJiggleEndDamping, TextJiggleEndDamping, El.JiggleSettings.EndDamping, 0.0f, 5.0f,
            FString::Printf(TEXT("%.2f"), El.JiggleSettings.EndDamping));
        SetSliderReadout(SliderJiggleEndImpulse, TextJiggleEndImpulse, El.JiggleSettings.EndImpulseScale, 0.0f, 10.0f,
            FString::Printf(TEXT("%.2f"), El.JiggleSettings.EndImpulseScale));
    }
    else
    {
        SetSliderReadout(SliderJiggleStiffness, TextJiggleStiffness, 5.0f, 0.0f, 20.0f, FString::Printf(TEXT("%.2f"), 5.0f));
        SetSliderReadout(SliderJiggleDamping, TextJiggleDamping, 0.5f, 0.0f, 5.0f, FString::Printf(TEXT("%.2f"), 0.5f));
        SetSliderReadout(SliderJiggleImpulse, TextJiggleImpulse, 1.0f, 0.0f, 10.0f, FString::Printf(TEXT("%.2f"), 1.0f));
        SetSliderReadout(SliderJiggleMidpoint, TextJiggleMidpoint, 1.0f, 0.0f, 1.0f, FString::Printf(TEXT("%.2f"), 1.0f));
        SetSliderReadout(SliderJiggleEndStiffness, TextJiggleEndStiffness, 5.0f, 0.0f, 20.0f, FString::Printf(TEXT("%.2f"), 5.0f));
        SetSliderReadout(SliderJiggleEndDamping, TextJiggleEndDamping, 0.5f, 0.0f, 5.0f, FString::Printf(TEXT("%.2f"), 0.5f));
        SetSliderReadout(SliderJiggleEndImpulse, TextJiggleEndImpulse, 1.0f, 0.0f, 10.0f, FString::Printf(TEXT("%.2f"), 1.0f));
    }

    // P3: idle-animation controls (nested elements only; layer pins have no
    // idle). Writes go through the element struct so IdleFrames playback
    // picks up the new timings on the next tick.
    SetCtrlEnabled(SliderIdleDuration, bHasElement);
    SetCtrlEnabled(SliderIdleSpeed, bHasElement);
    if (bHasElement)
    {
        SetSliderReadout(SliderIdleDuration, TextIdleDuration, El.IdleFrameDuration, 0.001f, 2.0f,
            FString::Printf(TEXT("%.3fs"), El.IdleFrameDuration));
        SetSliderReadout(SliderIdleSpeed, TextIdleSpeed, El.IdleSpeedMultiplier, 0.0f, 4.0f,
            FString::Printf(TEXT("%.2fx"), El.IdleSpeedMultiplier));
    }
    else
    {
        SetSliderReadout(SliderIdleDuration, TextIdleDuration, 0.1f, 0.001f, 2.0f, FString::Printf(TEXT("%.3fs"), 0.1f));
        SetSliderReadout(SliderIdleSpeed, TextIdleSpeed, 1.0f, 0.0f, 4.0f, FString::Printf(TEXT("%.2fx"), 1.0f));
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
    if (VisemeDisclosure.IsValid())
        VisemeDisclosure->SetSummary(FString(UTF8_TO_TCHAR(FPLayout::VisemeSummary(0).c_str())));
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
        if (VisemeDisclosure.IsValid())
            VisemeDisclosure->SetSummary(FString(UTF8_TO_TCHAR(FPLayout::VisemeSummary(0).c_str())));
        return;
    }
    const int32 Cols = ClampGridCols(MaxFrames);
    const bool bPlaying = Comp->IsVisemePlaying();
    const EViseme CurV = Comp->GetCurrentViseme();
    // P22: each row holds at most 8 cells (14px + 1px gap) plus a 32px label
    // box, so a full 16-frame viseme wraps into two rows that both fit the
    // 168px rail budget; the fill % moved into the cell tooltip.
    const int32 CellsPerRow = 8;
    for (const FVisemeRow& Row : Rows)
    {
        if (Row.Occupied.Num() == 0) continue;
        const int32 RowCount = FMath::Max(1, (Cols + CellsPerRow - 1) / CellsPerRow);
        for (int32 rr = 0; rr < RowCount; ++rr)
        {
            TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
            if (rr == 0)
            {
                TSharedRef<STextBlock> Lbl = SNew(STextBlock)
                    .Text(FText::FromString(Row.Label))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                    .ColorAndOpacity(!Row.bNamed && bPlaying && Row.Enum == CurV
                        ? FLinearColor(1.0f,0.8f,0.4f) : FLinearColor(0.8f,0.8f,0.8f));
                Lbl->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s: %d/%d frames (%.0f%%)"),
                    *Row.Label, Row.Occupied.Num(), Cols, FrameFillRatio(Row.Occupied) * 100.0f)));
                R->AddSlot().Padding(FMargin(0,1)).AutoWidth()
                    [SNew(SBox).WidthOverride(32)[Lbl]];
            }
            else
            {
                R->AddSlot().Padding(FMargin(0,1)).AutoWidth()
                    [SNew(SBox).WidthOverride(32)];
            }
            for (int32 c = rr * CellsPerRow; c < FMath::Min((rr + 1) * CellsPerRow, Cols); ++c)
            {
                const bool Filled = Row.Occupied.IsValidIndex(c) && Row.Occupied[c];
                TSharedRef<SBox> Cell = SNew(SBox).WidthOverride(14).HeightOverride(14)
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
            VisemeGridBox->AddSlot().AutoHeight()[R];
        }
    }
    if (VisemeDisclosure.IsValid())
    {
        int32 NumRows = 0;
        for (const FVisemeRow& VR : Rows)
            if (VR.Occupied.Num() > 0) ++NumRows;
        VisemeDisclosure->SetSummary(FString(UTF8_TO_TCHAR(FPLayout::VisemeSummary(NumRows).c_str())));
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
        // P22: the element name is clipped to 72px (full name in the tooltip,
        // along with the Pin/Jiggle badges) so the row fits the rail; the
        // badges moved out of the row entirely.
        TSharedRef<STextBlock> NameLbl = SNew(STextBlock)
            .Text(FText::FromString(ElName))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
            .ColorAndOpacity(i == SelectedNestedElementIndex
                ? FLinearColor(1.0f,0.8f,0.4f) : FLinearColor(0.8f,0.8f,0.8f));
        NameLbl->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s%s%s"),
            *ElName, El.Pin3D.bPinned ? TEXT(" [Pinned]") : TEXT(""),
            El.bJiggleEnabled ? TEXT(" [Jiggle]") : TEXT(""))));
        R->AddSlot().Padding(FMargin(4,2)).AutoWidth()
            [SNew(SButton)
                .ButtonColorAndOpacity(FLinearColor(0.15f,0.15f,0.15f))
                .OnClicked_Lambda([this, i]()
                {
                    SelectedNestedElementIndex = i;
                    RefreshUI();
                    return FReply::Handled();
                })
                .Content()
                [SNew(SBox).WidthOverride(68)[NameLbl]]];
        R->AddSlot().FillWidth(1.0f);
        R->AddSlot().Padding(FMargin(4,2)).AutoWidth()
            [MakeBtn(TEXT("Dup"), [this, i]()
            {
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (!Comp || !SelectedLayerName.IsValid()) return;
                if (i < 0 || i >= Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName)) return;
                int32 NewIndex = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
                DuplicateNestedElement(ActiveViewState, SelectedLayerName, i, NewIndex);
                SelectedNestedElementIndex = NewIndex;
                RefreshUI();
            }, FLinearColor(0.6f,1.0f,0.6f), FLinearColor(0.1f,0.1f,0.1f))];
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

void UFaceParallaxEditorWidget::SetNestedPaneMode(int32 Mode)
{
    NestedPaneMode = (Mode == 1) ? 1 : 0;
    if (NestedPaneSwitcher.IsValid())
        NestedPaneSwitcher->SetActiveWidgetIndex(NestedPaneMode);
    RefreshUI();
}

void UFaceParallaxEditorWidget::RebuildPinManager()
{
    if (!PinManagerBox.IsValid()) return;
    PinManagerBox->ClearChildren();
    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !SelectedLayerName.IsValid() || !ActivePreset)
    {
        PinManagerBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No layer selected."), 8, FLinearColor(0.5f,0.5f,0.5f))];
        return;
    }

    // Copy-target options: every top-level element of the layer/state.
    const int32 N = Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName);
    FString PrevSel = PinCopyTarget.IsValid() ? *PinCopyTarget : FString();
    PinCopyTargets.Reset();
    for (int32 i = 0; i < N; ++i)
    {
        FFaceNestedArt E = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
        FString Ename = E.ElementName.IsValid() ? E.ElementName.ToString()
            : FString::Printf(TEXT("Element %d"), i);
        PinCopyTargets.Add(MakeShared<FString>(Ename));
    }
    PinCopyTarget.Reset();
    if (N > 0)
    {
        PinCopyTarget = PinCopyTargets[0];
        if (!PrevSel.IsEmpty())
            for (const TSharedPtr<FString>& Opt : PinCopyTargets)
                if (*Opt == PrevSel) { PinCopyTarget = Opt; break; }
    }

    int32 Rows = 0;
    const FFaceArtSlot LS = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
    if (LS.LayerPin3D.bPinned) ++Rows;
    for (int32 i = 0; i < N; ++i)
    {
        FFaceNestedArt E = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
        if (E.Pin3D.bPinned) ++Rows;
        for (int32 c = 0; c < E.Children.Num(); ++c)
            if (E.Children[c].Pin3D.bPinned) ++Rows;
    }

    // P3: per-layer count header — "Pins on <Layer>: N" (rows mirror
    // FPLayout::FPPinnedRowCount: layer + pinned elements + pinned children).
    TSharedRef<STextBlock> Info = MakeLbl(
        FString::Printf(TEXT("Pins on %s: %d"), *SelectedLayerName.ToString(), Rows),
        8, FLinearColor(0.8f, 0.8f, 0.8f));
    Info->SetToolTipText(FText::FromString(TEXT("One row per pinned item. Click a row to jump to its pin "
        "controls; the visibility checkbox and Unpin act on the row's element. "
        "Add Pin creates a new element; the next canvas click places its pin at the cursor.")));
    PinManagerBox->AddSlot().AutoHeight().Padding(FMargin(0,2))
        [Info];

    // P3: Add Pin — a new nested element (pinned, at origin) plus a one-shot
    // arm: the NEXT canvas click places the pin at the cursor (PlacePinAtUV),
    // then the arm clears and one-map selection resumes.
    PinManagerBox->AddSlot().AutoHeight().Padding(FMargin(0,1))
        [SNew(SFaceFlashButton).Text(TEXT("Add Pin"))
            .OnClicked_Lambda([this]()
            {
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (!Comp || !SelectedLayerName.IsValid()) return FReply::Handled();
                FWidgetUndoScope UndoScope(this, TEXT("Add Pin"));
                FFaceNestedArt El;
                El.ElementName = FName(*FString::Printf(TEXT("Pin %d"),
                    Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName) + 1));
                El.Pin3D.bPinned = true;
                El.Pin3D.Position3D = FVector(0.0f, 0.0f, 0.0f);
                Comp->AddNestedElement(ActiveViewState, SelectedLayerName, El);
                SelectedNestedElementIndex = FMath::Max(0,
                    Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName) - 1);
                bPendingPinPlacement = true;   // arm the one-shot: the next canvas click places the pin at the cursor
                SetStatus(FString::Printf(TEXT("Pin '%s' added - click the canvas to place it"),
                    *El.ElementName.ToString()), FLinearColor(0.5f, 1.0f, 0.5f));
                RefreshUI();
                return FReply::Handled();
            })];

    auto AddRow = [&](const FString& Name, bool bVis,
        TFunction<void()>&& OnJump, TFunction<void()>&& OnToggle, TFunction<void()>&& OnUnpin)
    {
        TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
        R->AddSlot().Padding(FMargin(0,1)).AutoWidth()
            [SNew(SCheckBox).IsChecked(bVis ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([Fn = MoveTemp(OnToggle)](ECheckBoxState S){ Fn(); })];
        R->AddSlot().Padding(FMargin(4,1)).AutoWidth()
            [MakeBtn(Name, [Fn = MoveTemp(OnJump)](){ Fn(); },
                FLinearColor(0.75f,0.75f,0.85f), FLinearColor(0.12f,0.12f,0.12f))];
        R->AddSlot().Padding(FMargin(4,1)).AutoWidth()
            [MakeBtn(TEXT("Unpin"), [Fn = MoveTemp(OnUnpin)](){ Fn(); },
                FLinearColor(1.0f,0.5f,0.5f), FLinearColor(0.1f,0.1f,0.1f))];
        PinManagerBox->AddSlot().AutoHeight().Padding(FMargin(0,1))[R];
    };

    // Whole-layer pin row.
    if (LS.LayerPin3D.bPinned)
    {
        AddRow(TEXT("Layer Pin"), true,
            [this]()
            {
                SetNestedPaneMode(0);
                RefreshUI();
            },
            [](){},
            [this]()
            {
                if (!ActivePreset || !SelectedLayerName.IsValid()) return;
                FWidgetUndoScope UndoScope(this, TEXT("Unpin Layer"));
                FFaceArtSlot S = ActivePreset->GetSlot(ActiveViewState, SelectedLayerName);
                S.LayerPin3D.bPinned = false;
                ActivePreset->SetSlot(ActiveViewState, SelectedLayerName, S);
                RefreshUI();
            });
    }

    // Element + child pin rows.
    for (int32 i = 0; i < N; ++i)
    {
        FFaceNestedArt E = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
        FString Ename = E.ElementName.IsValid() ? E.ElementName.ToString()
            : FString::Printf(TEXT("Element %d"), i);
        if (E.Pin3D.bPinned)
        {
            bool bVis = true;
            if (const bool* V = E.ViewVisibility.Find(ActiveViewState)) bVis = *V;
            AddRow(Ename, bVis,
                [this, i]()   // jump: select element + show its controls
                {
                    SelectedNestedElementIndex = i;
                    SelectedPinRow = i;
                    SetNestedPaneMode(0);
                },
                [this, i]()   // visibility toggle
                {
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    if (!Comp || !SelectedLayerName.IsValid()) return;
                    if (i >= Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName)) return;
                    FFaceNestedArt E2 = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
                    bool bNow = true;
                    if (const bool* V = E2.ViewVisibility.Find(ActiveViewState)) bNow = *V;
                    E2.ViewVisibility.FindOrAdd(ActiveViewState) = !bNow;
                    Comp->SetNestedElement(ActiveViewState, SelectedLayerName, i, E2);
                    RefreshUI();
                },
                [this, i]()   // unpin
                {
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    if (!Comp || !SelectedLayerName.IsValid()) return;
                    if (i >= Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName)) return;
                    FWidgetUndoScope UndoScope(this, TEXT("Unpin Element"));
                    FFaceNestedArt E2 = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
                    E2.Pin3D.bPinned = false;
                    Comp->SetNestedElement(ActiveViewState, SelectedLayerName, i, E2);
                    RefreshUI();
                });
        }
        for (int32 c = 0; c < E.Children.Num(); ++c)
        {
            if (!E.Children[c].Pin3D.bPinned) continue;
            FString CName = E.Children[c].ElementName.IsValid()
                ? E.Children[c].ElementName.ToString()
                : FString::Printf(TEXT("Child %d"), c);
            bool bVis = true;
            if (const bool* V = E.Children[c].ViewVisibility.Find(ActiveViewState)) bVis = *V;
            AddRow(FString(TEXT("  \u2514 ")) + CName, bVis,
                [this, i]()   // jump: select the parent element
                {
                    SelectedNestedElementIndex = i;
                    SelectedPinRow = i;
                    SetNestedPaneMode(0);
                },
                [this, i, c]()   // visibility toggle
                {
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    if (!Comp || !SelectedLayerName.IsValid()) return;
                    if (i >= Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName)) return;
                    FFaceNestedArt E2 = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
                    if (c >= E2.Children.Num()) return;
                    bool bNow = true;
                    if (const bool* V = E2.Children[c].ViewVisibility.Find(ActiveViewState)) bNow = *V;
                    E2.Children[c].ViewVisibility.FindOrAdd(ActiveViewState) = !bNow;
                    Comp->SetNestedElement(ActiveViewState, SelectedLayerName, i, E2);
                    RefreshUI();
                },
                [this, i, c]()   // unpin child
                {
                    UFaceParallaxComponent* Comp = GetParallaxComponent();
                    if (!Comp || !SelectedLayerName.IsValid()) return;
                    if (i >= Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName)) return;
                    FWidgetUndoScope UndoScope(this, TEXT("Unpin Child"));
                    FFaceNestedArt E2 = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, i);
                    if (c >= E2.Children.Num()) return;
                    E2.Children[c].Pin3D.bPinned = false;
                    Comp->SetNestedElement(ActiveViewState, SelectedLayerName, i, E2);
                    RefreshUI();
                });
        }
    }

    // Copy selected pin to another element (Phase E duplicate-to-other-element).
    if (N > 1)
    {
        TSharedRef<SHorizontalBox> CopyRow = SNew(SHorizontalBox);
        CopyRow->AddSlot().Padding(FMargin(0,2)).AutoWidth()
            [MakeLbl(TEXT("Copy pin \u2192"), 8, FLinearColor(0.7f,0.8f,1.0f))];
        if (PinCopyTargets.Num() > 0 && PinCopyTarget.IsValid())
        {
            CopyRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
                [SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&PinCopyTargets)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> In)
                    {
                        return SNew(STextBlock).Text(FText::FromString(*In))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8));
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> In, ESelectInfo::Type)
                    {
                        if (In.IsValid()) PinCopyTarget = In;
                    })
                    [SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return PinCopyTarget.IsValid()
                                ? FText::FromString(*PinCopyTarget) : FText::FromString(TEXT("-"));
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))]];
        }
        CopyRow->AddSlot().Padding(FMargin(4,2)).AutoWidth()
            [MakeBtn(TEXT("Copy"), [this]()
            {
                UFaceParallaxComponent* Comp = GetParallaxComponent();
                if (!Comp || !SelectedLayerName.IsValid() || SelectedPinRow < 0) return;
                const int32 Src = SelectedPinRow;
                if (Src >= Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName)) return;
                int32 Dst = -1;
                for (int32 i = 0; i < PinCopyTargets.Num() && i < Comp->GetNestedElementCount(ActiveViewState, SelectedLayerName); ++i)
                {
                    if (PinCopyTarget.IsValid() && PinCopyTargets[i] == PinCopyTarget) { Dst = i; break; }
                }
                if (Dst < 0 || Dst == Src) return;
                FWidgetUndoScope UndoScope(this, TEXT("Copy Pin to Element"));
                FFaceNestedArt S = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, Src);
                FFaceNestedArt D = Comp->GetNestedElement(ActiveViewState, SelectedLayerName, Dst);
                D.Pin3D = S.Pin3D;
                D.Pin3D.bPinned = true;
                Comp->SetNestedElement(ActiveViewState, SelectedLayerName, Dst, D);
                if (TextStatus.IsValid())
                    TextStatus->SetText(FText::FromString(TEXT("Pin copied to target element.")));
                RefreshUI();
            }, FLinearColor(0.6f,1.0f,0.6f), FLinearColor(0.1f,0.1f,0.1f))];
        PinManagerBox->AddSlot().AutoHeight().Padding(FMargin(0,4,0,0))[CopyRow];
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
            [SNew(SBox).WidthOverride(70)
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

    // ---- Phase 4: quick-actions bar (rail jump chips + tools) ----
    {
        // P22: five rail chips in two rows (3+2) so every chip fits the
        // 168px rail budget; Import + Clear Stale live on the toolbar only.
        static const TCHAR* ChipNames[5] = { TEXT("Layers"), TEXT("Transform"), TEXT("Camera"), TEXT("Debug"), TEXT("Adv") };
        static const int32 ChipRails[5] = { 0, 1, 3, 4, 4 };
        static const int32 RowA[3] = { 0, 2, 3 };
        static const int32 RowB[2] = { 1, 4 };
        auto AddChips = [this](TSharedRef<SHorizontalBox> Row, const int32* Idx, int32 N)
        {
            for (int32 k = 0; k < N; ++k)
            {
                const int32 r = Idx[k];
                const int32 Ri = ChipRails[r];
                Row->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
                    [MakeBtn(ChipNames[r], [this, Ri]()
                    {
                        SetActiveRailIndex(Ri);
                    }, ActiveRailIndex == Ri ? AccentBlue() : FLinearColor(0.12f, 0.12f, 0.14f))];
            }
        };
        TSharedRef<SHorizontalBox> QaRowA = SNew(SHorizontalBox);
        AddChips(QaRowA, RowA, 3);
        QaRowA->AddSlot().Padding(FMargin(4, 2)).FillWidth(1.0f);
        TSharedRef<SHorizontalBox> QaRowB = SNew(SHorizontalBox);
        AddChips(QaRowB, RowB, 2);
        QaRowB->AddSlot().Padding(FMargin(4, 2)).FillWidth(1.0f);
        TSharedRef<SVerticalBox> QaV = SNew(SVerticalBox);
        QaV->AddSlot().AutoHeight()[QaRowA];
        QaV->AddSlot().AutoHeight()[QaRowB];
        ProblemsPanelBox->AddSlot().AutoHeight()
            [MakeSectionBox(TEXT("Quick Actions"), QaV)];
    }

    // ---- Phase 4: layout group (ValidateDesign rows from the manifest) ----
    {
        TSharedRef<SVerticalBox> LgBox = SNew(SVerticalBox);
        const std::vector<FPLayout::FPLayoutNode> Spec = FPLayout::BuildSpec();
        const std::vector<FPLayout::FPViolation> V = FPLayout::ValidateDesign(Spec);
        if (V.empty())
        {
            LgBox->AddSlot().AutoHeight().Padding(FMargin(0, 2))
                [MakeLbl(TEXT("Design contract OK (P1..P23, 0 violations)"), 8, FLinearColor(0.5f, 1.0f, 0.5f))];
        }
        else
        {
            for (const FPLayout::FPViolation& Vi : V)
            {
                TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
                R->AddSlot().Padding(FMargin(0, 1)).AutoWidth()
                    [SNew(SBox).WidthOverride(10).HeightOverride(10)
                        [SNew(SBorder)
                            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                            .BorderBackgroundColor(FLinearColor(1.0f, 0.35f, 0.35f))
                            .Padding(FMargin(0))]];
                R->AddSlot().Padding(FMargin(4, 1)).AutoWidth()
                    [MakeLbl(FString::Printf(TEXT("P%d"), (int32)Vi.Rule + 1), 8, FLinearColor(1.0f, 0.6f, 0.6f))];
                R->AddSlot().Padding(FMargin(4, 1)).FillWidth(1.0f)
                    [SNew(STextBlock)
                        .Text(FText::FromString(FString::Printf(TEXT("%s on %s (%s)"),
                            UTF8_TO_TCHAR(FPLayout::RuleName(Vi.Rule)),
                            UTF8_TO_TCHAR(Vi.Node),
                            UTF8_TO_TCHAR(Vi.Detail.c_str()))))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                        .ColorAndOpacity(FLinearColor(1.0f, 0.6f, 0.6f))
                        .AutoWrapText(true)];
                LgBox->AddSlot().AutoHeight()[R];
            }
        }
        ProblemsPanelBox->AddSlot().AutoHeight()
            [MakeSectionBox(TEXT("Layout Group"), LgBox)];
    }

    // ---- Phase 4: search + issue list ----
    TSharedRef<SVerticalBox> IssuesBox = SNew(SVerticalBox);
    {
        ProblemsSearchBox = SNew(SSearchBox)
            .HintText(FText::FromString(TEXT("Filter issues...")))
            .OnTextChanged_Lambda([this](const FText& T)
            {
                ProblemsFilter = T.ToString();
                IssuesPageIndex = 0;
                RebuildProblemsPanel();
            });
        TSharedRef<SHorizontalBox> SearchRow = SNew(SHorizontalBox);
        SearchRow->AddSlot().Padding(FMargin(0, 2)).FillWidth(1.0f)[ProblemsSearchBox.ToSharedRef()];
        IssuesBox->AddSlot().AutoHeight()[SearchRow];
        ProblemsPanelBox->AddSlot().AutoHeight()
            [MakeSectionBox(TEXT("Issues"), IssuesBox)];
    }

    UFaceParallaxComponent* Comp = GetParallaxComponent();
    if (!Comp || !Comp->ActivePreset)
    {
        IssuesBox->AddSlot().AutoHeight()
            [MakeLbl(TEXT("No preset active."), 8, FLinearColor(1.0f,0.5f,0.5f))];
        ProblemsSummaryText = TEXT("no preset");
        ProblemsSummaryColor = FLinearColor(1.0f, 0.5f, 0.5f);
        RefreshProblemsSummary();
        return;
    }
    auto StateLabel = [](EFaceAngleState S) -> const TCHAR*
    {
        switch (S)
        {
            case EFaceAngleState::Front:            return TEXT("Front");
            case EFaceAngleState::ThreeQuarterRight:return TEXT("3/4 R");
            case EFaceAngleState::RightProfile:     return TEXT("Profile R");
            case EFaceAngleState::BackRight:        return TEXT("Back R");
            case EFaceAngleState::Back:             return TEXT("Back");
            case EFaceAngleState::BackLeft:         return TEXT("Back L");
            case EFaceAngleState::LeftProfile:      return TEXT("Profile L");
            case EFaceAngleState::ThreeQuarterLeft: return TEXT("3/4 L");
            case EFaceAngleState::Top:              return TEXT("Top");
            case EFaceAngleState::Bottom:           return TEXT("Bottom");
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
    // ---- Phase 4: filtered issue list inside the Issues section ----
    int32 ErrorCount = 0;
    for (const FProblem& P : Rows) if (P.bError) ++ErrorCount;
    const bool bFiltering = !ProblemsFilter.IsEmpty();
    const FString FilterLower = ProblemsFilter.ToLower();
    TArray<const FProblem*> Filtered;
    for (const FProblem& P : Rows)
    {
        if (bFiltering && !P.Text.ToLower().Contains(FilterLower))
            continue;
        Filtered.Add(&P);
    }
    FString Summary = bFiltering
        ? FString::Printf(TEXT("%d issues (%d errors) - %d match \"%s\""),
            Rows.Num(), ErrorCount, Filtered.Num(), *ProblemsFilter)
        : FString::Printf(TEXT("%d issues (%d errors, %d warnings)"),
            Rows.Num(), ErrorCount, Rows.Num() - ErrorCount);
    TextProblemsSummary = MakeLbl(Summary, 9, FLinearColor(0.9f,0.7f,0.3f));
    IssuesBox->AddSlot().AutoHeight().Padding(FMargin(0, 2))
        [TextProblemsSummary.ToSharedRef()];
    ProblemsSummaryText = Summary;
    ProblemsSummaryColor = (ErrorCount > 0)
        ? FLinearColor(1.0f, 0.6f, 0.6f)
        : (Rows.Num() > 0 ? FLinearColor(1.0f, 0.85f, 0.3f) : FLinearColor(0.5f, 1.0f, 0.5f));
    RefreshProblemsSummary();

    // P17/P18: issue rows flip through carousel pages inside a fixed page
    // viewport; the 8px bottom reserve (P19) keeps the last row clear of the
    // nav strip below it - no vertical scroll bar.
    TSharedRef<SVerticalBox> IssuesRowsBox = SNew(SVerticalBox);
    IssuesBox->AddSlot().AutoHeight()
        [SNew(SBox)
            .HeightOverride(FPLayout::CarouselViewportH)
            .Padding(FMargin(0, 0, 0, FPLayout::ScrollReserveBottom))
            [IssuesRowsBox]];
    TSharedRef<SFaceCarouselNav> IssuesNav = SNew(SFaceCarouselNav)
        .OnPrev_Lambda([this]()
        {
            IssuesPageIndex = FMath::Max(0, IssuesPageIndex - 1);
            RebuildProblemsPanel();
            return FReply::Handled();
        })
        .OnNext_Lambda([this]()
        {
            IssuesPageIndex = IssuesPageIndex + 1;
            RebuildProblemsPanel();
            return FReply::Handled();
        });
    IssuesPageLabel = IssuesNav->Label;
    IssuesBox->AddSlot().AutoHeight().Padding(FMargin(4, 0, 4, 2))[IssuesNav];

    if (Filtered.Num() == 0)
    {
        IssuesRowsBox->AddSlot().AutoHeight()
            [MakeLbl(bFiltering ? TEXT("No matching issues.") : TEXT("No problems found."),
                8, FLinearColor(0.5f,1.0f,0.5f))];
    }
    const int32 TotalPages = FPLayout::CarouselPageCount(Filtered.Num());
    IssuesPageIndex = FPLayout::ClampCarouselPage(IssuesPageIndex, TotalPages);
    const int32 Start = IssuesPageIndex * FPLayout::CarouselRowsPerPage;
    const int32 End = FMath::Min(Start + FPLayout::CarouselRowsPerPage, Filtered.Num());
    for (int32 i = Start; i < End; ++i)
    {
        const FProblem& P = *Filtered[i];
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
        IssuesRowsBox->AddSlot().AutoHeight()[R];
    }
    if (IssuesPageLabel.IsValid())
        IssuesPageLabel->SetText(FText::FromString(FString::Printf(TEXT("Page %d/%d"),
            IssuesPageIndex + 1, TotalPages)));
}

// Phase 4: shows the issues summary in the Problems accordion header
// (Problems is Diagnostics-rail section index 3 per FPLayout::RailSectionTitles()).
void UFaceParallaxEditorWidget::RefreshProblemsSummary()
{
    if (!DiagnosticsAccordion.IsValid()) return;
    if (DiagnosticsAccordion->NumSections() <= 3) return;
    DiagnosticsAccordion->SetSectionSummary(3, ProblemsSummaryText, ProblemsSummaryColor);
}
#endif
