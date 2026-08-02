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

// Phase 4/1: a canvas hotspot region (or parts-strip chip) was clicked.
// Resolve the region to a primary layer via the preset's explicit
// HotspotLayerMap first, then FPLayout::FPHotspotLayerMatch derivation,
// select that layer, and — when the layer still has NO art — open the
// Import Folder Wizard preselected on the part: one click goes from "zone"
// straight to assigning art to it. Layers that already have art just get
// selected (the live preview is the review surface).
void UFaceParallaxEditorWidget::HandleHotspotClick(const FString& RegionName)
{
    if (RegionName.IsEmpty()) return;
    const FName LayerTag = ResolveHotspotLayer(RegionName);
    if (LayerTag.IsValid())
    {
        SetSelectedLayer(LayerTag.ToString());
        SetActiveRailIndex(1);   // Art rail: import + tweak controls
        if (LayerHasFrontArt(LayerTag))
        {
            SetStatus(FString::Printf(TEXT("Hotspot '%s' -> layer '%s' (art assigned — reviewing)"),
                *RegionName, *LayerTag.ToString()), AccentBlue());
            return;
        }
        SetStatus(FString::Printf(TEXT("Hotspot '%s' -> layer '%s' — import art for this zone"),
            *RegionName, *LayerTag.ToString()), AccentBlue());
    }
    else
    {
        SetSelectedLayer(FString());
        SetStatus(FString::Printf(
            TEXT("Hotspot '%s' is unmapped — right-click the chip to map it to a layer"),
            *RegionName), AccentBlue());
    }
    OpenImportFolderWizard(RegionName);
}

// Redesign: a schematic glyph on the canvas was clicked. Resolve the part to
// its layer (same derivation as the parts strip), select it, and — when the
// layer has no assigned art — open the Import Folder Wizard preselected on
// exactly the part the user clicked. Layers with art are just selected for
// review ("assigned art replaces the default outline").
void UFaceParallaxEditorWidget::HandleSchematicPartClick(const FString& PartName)
{
    if (PartName.IsEmpty()) return;
    const FName LayerTag = ResolveHotspotLayer(PartName);
    if (LayerTag.IsValid())
    {
        SetSelectedLayer(LayerTag.ToString());
        SetActiveRailIndex(1);   // Art rail: import + tweak controls
        if (LayerHasFrontArt(LayerTag))
        {
            SetStatus(FString::Printf(TEXT("Part '%s' -> layer '%s' (art assigned — reviewing)"),
                *PartName, *LayerTag.ToString()), AccentBlue());
            return;
        }
        SetStatus(FString::Printf(TEXT("Part '%s' -> layer '%s' — import art for this part"),
            *PartName, *LayerTag.ToString()), AccentBlue());
    }
    else
    {
        SetSelectedLayer(FString());
        SetStatus(FString::Printf(TEXT("Part '%s' has no mapped layer — import art to assign one"),
            *PartName), AccentBlue());
    }
    OpenImportFolderWizard(PartName);
}

// Redesign: does the layer's Front-state slot carry an albedo texture? The
// schematic default view paints glyphs only for layers WITHOUT art, so this
// is the "art replaces the outline" gate.
bool UFaceParallaxEditorWidget::LayerHasFrontArt(FName LayerTag) const
{
    if (!ActivePreset || LayerTag.IsNone()) return false;
    return ActivePreset->GetSlot(EFaceAngleState::Front, LayerTag).Textures.Albedo != nullptr;
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

// Alt+click on a hotspot or parts chip: open the Import Folder Wizard
// preselected on that part, so unassigned regions flow into assignment.
void UFaceParallaxEditorWidget::ImportHotspotRegion(const FString& RegionName)
{
    if (RegionName.IsEmpty()) return;
    OpenImportFolderWizard(RegionName);
    SetStatus(FString::Printf(TEXT("Import Art opened for hotspot '%s'"), *RegionName), AccentBlue());
}

// Phase C: canvas click-to-select. The point is in UV space (already
// converted by SFaceHotspotLayer); pick the TOPMOST layer whose transformed
// quad contains it (draw order = layer list order, last = on top —
// FPLayout::FPHitTopmostQuad). The quads were built from the active view
// state's stored transforms in RefreshHotspotRegions, so the click hits the
// pixels the master material actually paints in this view.
void UFaceParallaxEditorWidget::SelectCanvasLayerAt(const FVector2D& UV)
{
    if (!HotspotLayer.IsValid()) return;
    const std::vector<FPLayout::FPLayerQuad>& Quads = HotspotLayer->GetLayerQuads();
    const TArray<FString>& Tags = HotspotLayer->GetQuadLayerTags();
    const int32 Top = FPLayout::FPHitTopmostQuad(UV.X, UV.Y, Quads);
    if (Top >= 0 && Tags.IsValidIndex(Top))
    {
        SetSelectedLayer(Tags[Top]);
        SetStatus(FString::Printf(TEXT("Selected '%s' (canvas)"),
            *Tags[Top]), AccentBlue());
    }
}

// Phase C: right-click / ctrl+click on the canvas cycles through the layers
// overlapping the click point — FPLayout::FPCycleQuadHit semantics: the hit
// AFTER the current selection (wrapping), or the topmost hit when the
// selection is not among them. Same quad source as SelectCanvasLayerAt.
void UFaceParallaxEditorWidget::CycleCanvasLayerAt(const FVector2D& UV)
{
    if (!HotspotLayer.IsValid()) return;
    const std::vector<FPLayout::FPLayerQuad>& Quads = HotspotLayer->GetLayerQuads();
    const TArray<FString>& Tags = HotspotLayer->GetQuadLayerTags();
    if (Quads.empty()) return;
    std::vector<int> Hits;
    for (size_t i = 0; i < Quads.size(); ++i)
        if (FPLayout::FPPointInQuad(UV.X, UV.Y, Quads[i]))
            Hits.push_back((int)i);
    if (Hits.empty()) return;
    int32 Current = -1;
    const FString SelTag = SelectedLayerName.IsValid() ? SelectedLayerName.ToString() : FString();
    for (int32 i = 0; i < Tags.Num(); ++i)
        if (Tags[i] == SelTag)
        {
            Current = i;
            break;
        }
    const int32 Next = FPLayout::FPCycleQuadHit(Hits, Current);
    if (Next >= 0 && Tags.IsValidIndex(Next))
    {
        SetSelectedLayer(Tags[Next]);
        SetStatus(FString::Printf(TEXT("Selected '%s' (cycle)"),
            *Tags[Next]), AccentBlue());
    }
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
    // Live canvas: no setter re-arms the scene capture, so poll it here —
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
    ActiveRailIndex = FMath::Clamp(Index, 0, 5);
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
    for (int32 Ri = 0; Ri < 6 && Ri < RailChipsRows.Num() && Ri < RailSections.Num(); ++Ri)
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
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
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
            .OnClicked_Lambda([W, CB, WizardStatus, FolderEdit, PreselectPart]()
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

// Phase C: unified inspect mode (0 Textured, 1 Outline, 2 Depth, 3 Wireframe,
// 4 Depth Heatmap). Applies the canonical toggle combo (FPLayout::
// InspectComboForMode) to the SAME five booleans the Advanced rail Config
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
    const FFacePin3D P = bHasElement ? El.Pin3D : LS.LayerPin3D;
    if (bHasElement || bHasLayerPin)
    {
        SetSliderReadout(SliderPinX, TextPinX, P.Position3D.X, -2.0f, 2.0f,
            FString::Printf(TEXT("%.2f"), P.Position3D.X));
        SetSliderReadout(SliderPinY, TextPinY, P.Position3D.Y, -2.0f, 2.0f,
            FString::Printf(TEXT("%.2f"), P.Position3D.Y));
        SetSliderReadout(SliderPinZ, TextPinZ, P.Position3D.Z, -2.0f, 2.0f,
            FString::Printf(TEXT("%.2f"), P.Position3D.Z));
        SetSliderReadout(SliderPinMinRot, TextPinMinRot, P.MinRotation, -180.0f, 180.0f,
            FString::Printf(TEXT("%.1f"), P.MinRotation));
        SetSliderReadout(SliderPinMaxRot, TextPinMaxRot, P.MaxRotation, -180.0f, 180.0f,
            FString::Printf(TEXT("%.1f"), P.MaxRotation));
        SetSliderReadout(SliderPinRotSens, TextPinRotSens, P.RotationSensitivity, -10.0f, 10.0f,
            FString::Printf(TEXT("%.2f"), P.RotationSensitivity));
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

    TSharedRef<STextBlock> Info = MakeLbl(
        FString::Printf(TEXT("Pinned: %d item%s (layer + elements)"), Rows, Rows == 1 ? TEXT("") : TEXT("s")),
        8, FLinearColor(0.8f, 0.8f, 0.8f));
    Info->SetToolTipText(FText::FromString(TEXT("One row per pinned item. Click a row to jump to its pin "
        "controls; the visibility checkbox and Unpin act on the row's element.")));
    PinManagerBox->AddSlot().AutoHeight().Padding(FMargin(0,2))
        [Info];

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

    // ---- Phase 4: quick-actions bar (rail jump chips + tools) ----
    {
        TSharedRef<SHorizontalBox> QaRow = SNew(SHorizontalBox);
        static const TCHAR* ChipNames[5] = { TEXT("Layers"), TEXT("Transform"), TEXT("Camera"), TEXT("Debug"), TEXT("Adv") };
        for (int32 r = 0; r < 5; ++r)
        {
            const int32 Ri = r;
            QaRow->AddSlot().Padding(FMargin(0, 2)).AutoWidth()
                [MakeBtn(ChipNames[r], [this, Ri]()
                {
                    SetActiveRailIndex(Ri);
                }, ActiveRailIndex == Ri ? AccentBlue() : FLinearColor(0.12f, 0.12f, 0.14f))];
        }
        QaRow->AddSlot().Padding(FMargin(4, 2)).AutoWidth()
            [MakeBtn(TEXT("Import"), [this]() { OpenImportFolderWizard(TEXT("")); })];
        QaRow->AddSlot().Padding(FMargin(4, 2)).AutoWidth()
            [MakeBtn(TEXT("Clear Stale"), [this]() { ClearStaleTargets(); RefreshUI(); }, FLinearColor(1.0f, 0.7f, 0.5f))];
        QaRow->AddSlot().Padding(FMargin(4, 2)).FillWidth(1.0f);
        ProblemsPanelBox->AddSlot().AutoHeight()
            [MakeSectionBox(TEXT("Quick Actions"), QaRow)];
    }

    // ---- Phase 4: layout group (ValidateDesign rows from the manifest) ----
    {
        TSharedRef<SVerticalBox> LgBox = SNew(SVerticalBox);
        const std::vector<FPLayout::FPLayoutNode> Spec = FPLayout::BuildSpec();
        const std::vector<FPLayout::FPViolation> V = FPLayout::ValidateDesign(Spec);
        if (V.empty())
        {
            LgBox->AddSlot().AutoHeight().Padding(FMargin(0, 2))
                [MakeLbl(TEXT("Design contract OK (P1..P16, 0 violations)"), 8, FLinearColor(0.5f, 1.0f, 0.5f))];
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
        // Phase 4: rail width range control (clamped via the manifest mirror).
        TSharedRef<SHorizontalBox> LwRow = SNew(SHorizontalBox);
        LwRow->AddSlot().Padding(FMargin(0, 2)).AutoWidth().VAlign(VAlign_Center)
            [MakeLbl(TEXT("Rail Width"), 8, FLinearColor(0.7f, 0.7f, 0.7f))];
        TSharedRef<SSpinBox<float>> LwSpin = SNew(SSpinBox<float>)
            .Value(RailWidthPx)
            .MinValue(FPLayout::RailWidthMin)
            .MaxValue(FPLayout::RailWidthMax)
            .MinSliderValue(FPLayout::RailWidthMin)
            .MaxSliderValue(FPLayout::RailWidthMax)
            .Delta(10.0f)
            .OnValueCommitted_Lambda([this](float Val, ETextCommit::Type)
            {
                RailWidthPx = (float)FPLayout::ClampRailWidth((double)Val);
                RebuildWidget();
            });
        LwSpin->SetToolTipText(FText::FromString(TEXT(
            "Rail width 180-360 px (default 180). Rebuilds the editor layout.")));
        RailWidthSpin = LwSpin;
        LwRow->AddSlot().Padding(FMargin(4, 2)).AutoWidth()[LwSpin];
        LwRow->AddSlot().Padding(FMargin(4, 2)).FillWidth(1.0f);
        LgBox->AddSlot().AutoHeight()[LwRow];
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
// (Problems is Advanced-rail section index 3 per FPLayout::RailSectionTitles()).
void UFaceParallaxEditorWidget::RefreshProblemsSummary()
{
    if (!AdvancedAccordion.IsValid()) return;
    if (AdvancedAccordion->NumSections() <= 3) return;
    AdvancedAccordion->SetSectionSummary(3, ProblemsSummaryText, ProblemsSummaryColor);
}
#endif
