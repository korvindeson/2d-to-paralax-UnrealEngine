#pragma once
// FaceParallaxLayoutSpec.h
// ----------------------------------------------------------------------------
// Pure C++17 layout manifest + design-principle validator for the
// FaceParallax editor widget. NO UE dependencies: this file is included both
// by the editor widget (FaceParallaxEditorWidgetUI.cpp performs a runtime
// self-check in RebuildWidget) and by the standalone test harness
// (Tests/ParallaxMathTests.cpp, Phase H).
//
// The manifest (BuildSpec) is the DESIGN CONTRACT for the widget UI: it
// declares the panel tree, spacings, paddings and fixed sizes that
// RebuildWidget() constructs. The validator (ValidateDesign) enforces the
// following checkable design principles over the contract:
//
//   P1  NoSiblingOverlap - non-overlay containers: sibling boxes never
//                          intersect (overlay containers - preview canvas
//                          stack, rail switcher - allow overlap by design).
//   P2  Containment      - every child box (incl. slot margins) stays inside
//                          its parent's content box. Scroll viewports
//                          (bClipH) are exempt: their content is clipped,
//                          never constrained, and never checked.
//   P3  PositiveSize     - leaves and containers have positive extents in
//                          both axes (spacers / flex scroll views excluded).
//   P4  SpacingSanity    - sibling spacing >= 0 and never sub-pixel
//                          (0 < s < 1 is a cramped, imprecise gap).
//   P5  SpacingPalette   - sibling spacing comes from {0,1,2,3,4,6,8}:
//                          consistent rhythm across the whole UI.
//   P6  MarginBudget     - slot margins and container padding <= 8 px.
//   P7  ReadOrder        - flow children advance monotonically in the flow
//                          direction (top-to-bottom / left-to-right).
//   P8  GridAlignment    - grid children on the same column share x,
//                          children on the same row share y.
//   P9  SectionTitleFirst- section containers start with a title child.
//   P10 FitNoClip        - fixed-size containers never clip their children
//                          (scroll viewports excluded).
//   P11 MinimalSpace     - fixed-size containers leave no more than
//                          max(8 px, 50%) unused extent (no wasted space;
//                          scroll viewports excluded).
//   P12 NoGlobalOverlap  - across the WHOLE resolved tree, no two unrelated
//                          visible boxes may intersect. Exempt: ancestor
//                          pairs, siblings stacked under one overlay
//                          container (mutually exclusive views), and content
//                          inside a scroll viewport (clipped, never drawn
//                          outside its rect).
//   P13 WithinScreenBounds- every visible box must lie inside the root
//                          (screen) rect - nothing may drift off the screen,
//                          right edge included. Scroll-viewport content is
//                          exempt (clipped at the viewport rect).
//   P16 SectionDensity    - a clipped viewport may hold at most 4 plain
//                          sections; anything denser must be marked bAccordion
//                          (one-open-per-group collapsible section stack).
//                          Dense plain stacks paint over each other in a
//                          560px-tall viewport - the "rail explosion" defect.
//
// Scroll-viewport model: the rails are fixed 180x560 viewports whose content
// (wide button rows, tall section stacks) scrolls; bClipH marks the viewport
// and exempts its whole subtree from P2/P10/P11/P12/P13 - exactly what a
// clipped/scrollable Slate widget does visually.

#include <cstddef>
#include <cmath>
#include <string>
#include <vector>
#include <initializer_list>
#include <utility>

namespace FPLayout {

// ----------------------------------------------------------------------------
// Design-system constants. These mirror the real widget construction in
// FaceParallaxEditorWidgetUI.cpp (RebuildWidget) - keep in sync; the Phase H
// tests assert the mirrored values explicitly.
// ----------------------------------------------------------------------------
inline constexpr double ToolbarItemPad      = 2.0;    // FMargin(2) toolbar slots
inline constexpr double ToolbarPadL         = 4.0;    // toolbar border padding
inline constexpr double ToolbarPadV         = 3.0;
inline constexpr double SearchBoxWidth      = 140.0;  // SSearchBox WidthOverride
inline constexpr double ActorComboWidth     = 170.0;  // actor combo WidthOverride
inline constexpr double StateStripHeight    = 26.0;   // view-state strip
inline constexpr double StateTabPad         = 1.0;    // tab slot spacing
inline constexpr double StateDotSize        = 8.0;    // state dot box
inline constexpr double ZoneDiagramHeight   = 20.0;   // zone strips row
inline constexpr double ModeTabPad          = 1.0;    // display-mode row spacing
inline constexpr double PreviewCanvasHeight = 450.0;  // SBox HeightOverride
inline constexpr double MainRowHeight       = 560.0;  // main area fixed height
inline constexpr double RailIconsWidth      = 36.0;   // rail icon column
inline constexpr double RailIconSize        = 30.0;   // rail icon buttons
inline constexpr double RailWidth           = 180.0;  // rail switcher width
inline constexpr double PropsWidth          = 340.0;  // slot properties pane
inline constexpr double ThumbSize           = 72.0;   // texture thumbnails
inline constexpr double TimelineHeight      = 90.0;   // timeline strip
inline constexpr double BotBarHeight        = 24.0;   // bottom action bar
inline constexpr double DiagnosticLogHeight = 100.0;  // diagnostic log
inline constexpr double SectionBorderPad    = 6.0;    // MakeSectionBox FMargin(6)
inline constexpr double SectionTitlePadL    = 4.0;    // section title padding
inline constexpr double SectionTitlePadV    = 6.0;
inline constexpr double PropsRightGap       = 8.0;    // right edge of the window (MainRow props slot padding)
inline constexpr double PropsScrollInsetR   = 8.0;    // gap before the vertical scrollbar inside PropScroll

inline constexpr double PaletteVals[] = { 0.0, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0 };
inline constexpr double MaxMargin = 8.0;

// ----------------------------------------------------------------------------
// Types
// ----------------------------------------------------------------------------
enum class ContainerKind : unsigned char { Leaf, HFlow, VFlow, Grid, Overlay };

enum class DesignRule : unsigned char {
    NoSiblingOverlap,   // P1
    OutsideParent,      // P2
    ZeroSize,           // P3
    SpacingSanity,      // P4
    OffPaletteSpacing,  // P5
    MarginOverBudget,   // P6
    ReadOrderBroken,    // P7
    GridMisaligned,     // P8
    SectionTitleFirst,  // P9
    FitNoClip,          // P10
    MinimalSpace,       // P11
    GlobalOverlap,      // P12
    WithinScreenBounds, // P13
    DensityOverflow     // P16
};

struct FPViolation {
    DesignRule  Rule;
    const char* Node;
    std::string Detail;
};

inline const char* RuleName(DesignRule r)
{
    switch (r)
    {
        case DesignRule::NoSiblingOverlap:  return "NoSiblingOverlap";
        case DesignRule::OutsideParent:     return "Containment";
        case DesignRule::ZeroSize:          return "PositiveSize";
        case DesignRule::SpacingSanity:     return "SpacingSanity";
        case DesignRule::OffPaletteSpacing: return "SpacingPalette";
        case DesignRule::MarginOverBudget:  return "MarginBudget";
        case DesignRule::ReadOrderBroken:   return "ReadOrder";
        case DesignRule::GridMisaligned:    return "GridAlignment";
        case DesignRule::SectionTitleFirst: return "SectionTitleFirst";
        case DesignRule::FitNoClip:         return "FitNoClip";
        case DesignRule::MinimalSpace:      return "MinimalSpace";
        case DesignRule::GlobalOverlap:     return "GlobalOverlap";
        case DesignRule::WithinScreenBounds:return "ScreenBounds";
        case DesignRule::DensityOverflow:    return "SectionDensity";
    }
    return "?";
}

struct FPLayoutNode {
    const char* Name = nullptr;
    ContainerKind Kind = ContainerKind::Leaf;
    double FixedW = 0.0;   // 0 = auto
    double FixedH = 0.0;   // 0 = auto
    double Spacing = 0.0;  // sibling gap (flows + grids)
    double PadL = 0.0, PadT = 0.0, PadR = 0.0, PadB = 0.0;                 // container border padding
    double MarginL = 0.0, MarginT = 0.0, MarginR = 0.0, MarginB = 0.0;     // slot margin
    std::vector<int> Children;   // child pool indices (empty = leaf)
    int GridCol = 0, GridRow = 0;  // grid placement (children of Grid containers)
    bool bSpacer = false;  // absorbs remaining flow extent
    bool bFlexW = false;   // stretches to available width
    bool bFlexH = false;   // stretches to available height
    bool bClipH = false;   // scroll-viewport: clips children (zero extent in parent metrics;
                           // subtree exempt from P2/P10/P11/P12/P13)
    bool bSection = false; // P9: first child must be a title
    bool bTitle = false;
    bool bAccordion = false; // P16: one-open-per-group collapsible section
};

struct FPRect { double X = 0, Y = 0, W = 0, H = 0; };

// ----------------------------------------------------------------------------
// Manifest builder helpers. Args are evaluated eagerly, so children are added
// to the pool BEFORE their parent (parents always get higher indices than
// their whole subtree). Metrics/placement walk the pool recursively, so this
// ordering is safe; never index a child via "parent index + N" arithmetic.
// ----------------------------------------------------------------------------
struct Builder {
    std::vector<FPLayoutNode> N;
    int Add(const char* name, ContainerKind k, double w = 0.0, double h = 0.0)
    {
        FPLayoutNode n;
        n.Name = name;
        n.Kind = k;
        n.FixedW = w;
        n.FixedH = h;
        N.push_back(n);
        return (int)N.size() - 1;
    }
};

template <typename... Kids>
inline int AddFlowImpl(Builder& b, const char* name, ContainerKind kind, Kids&&... kids)
{
    const int idx = b.Add(name, kind);
    FPLayoutNode& n = b.N[(size_t)idx];
    int count = 0;
    auto AddKid = [&](int kid)
    {
        n.Children.push_back(kid);
        ++count;
    };
    (void)std::initializer_list<int>{ (AddKid(kids), 0)... };
    (void)count;
    return idx;
}

template <typename... Kids>
inline int HF(Builder& b, const char* name, Kids&&... kids)
{ return AddFlowImpl(b, name, ContainerKind::HFlow, kids...); }

template <typename... Kids>
inline int VF(Builder& b, const char* name, Kids&&... kids)
{ return AddFlowImpl(b, name, ContainerKind::VFlow, kids...); }

template <typename... Kids>
inline int OV(Builder& b, const char* name, Kids&&... kids)
{ return AddFlowImpl(b, name, ContainerKind::Overlay, kids...); }

template <typename... Kids>
inline int GRID(Builder& b, const char* name, Kids&&... kids)
{ return AddFlowImpl(b, name, ContainerKind::Grid, kids...); }

inline int LF(Builder& b, const char* name, double w, double h)
{ return b.Add(name, ContainerKind::Leaf, w, h); }


// ----------------------------------------------------------------------------
// The layout manifest: mirrors UFaceParallaxEditorWidget::RebuildWidget().
// Section bodies use the real SectionBorderPad padding; titles carry the
// real FMargin(4,6,4,2) slot padding as margins.
// ----------------------------------------------------------------------------
inline std::vector<FPLayoutNode> BuildSpec()
{
    Builder b;

    auto P  = [&](int i, double l, double t, double r, double bo) { FPLayoutNode& n = b.N[(size_t)i]; n.PadL = l; n.PadT = t; n.PadR = r; n.PadB = bo; };
    auto M  = [&](int i, double l, double t, double r, double bo) { FPLayoutNode& n = b.N[(size_t)i]; n.MarginL = l; n.MarginT = t; n.MarginR = r; n.MarginB = bo; };
    auto S  = [&](int i, double s) { b.N[(size_t)i].Spacing = s; };
    auto Fx  = [&](int i) { FPLayoutNode& n = b.N[(size_t)i]; n.bFlexW = true; n.bFlexH = true; };
    auto FxW = [&](int i) { b.N[(size_t)i].bFlexW = true; };
    auto FxH = [&](int i) { b.N[(size_t)i].bFlexH = true; };
    auto Sx  = [&](int i) { b.N[(size_t)i].bSection = true; };
    auto Tl  = [&](int i) { b.N[(size_t)i].bTitle = true; };
    auto Sp  = [&](int i) { b.N[(size_t)i].bSpacer = true; };
    auto Clip = [&](int i) { b.N[(size_t)i].bClipH = true; };
    auto Acc = [&](int i) { b.N[(size_t)i].bAccordion = true; };
    auto GP  = [&](int i, int c, int r) { FPLayoutNode& n = b.N[(size_t)i]; n.GridCol = c; n.GridRow = r; };

    // Section helper: given the section container, configure title + body.
    // Children are evaluated eagerly, so Children[0] = title, Children[1] = body.
    auto Bod = [&](int sec) { return b.N[(size_t)sec].Children[1]; };
    auto SecSetup = [&](int sec, double bodySpacing)
    {
        Sx(sec);
        const int body = Bod(sec);
        Tl(b.N[(size_t)sec].Children[0]);
        M(b.N[(size_t)sec].Children[0], SectionTitlePadL, SectionTitlePadV, SectionTitlePadL, 2);
        M(body, SectionTitlePadL, 0, SectionTitlePadL, 4);
        P(body, SectionBorderPad, SectionBorderPad, SectionBorderPad, SectionBorderPad);
        S(body, bodySpacing);
    };

    // ============================ 1. TOOLBAR ============================
    const int Toolbar = HF(b, "Toolbar",
        LF(b, "TB-NewPreset", 84, 22),
        LF(b, "TB-Save", 42, 22),
        LF(b, "TB-Import", 97, 22),
        LF(b, "TB-Search", SearchBoxWidth, 22),
        LF(b, "TB-Spacer", 0, 0),
        LF(b, "TB-Help", 21, 22),
        LF(b, "TB-Spawn", 104, 22),
        LF(b, "TB-Find", 90, 22),
        LF(b, "TB-Quads", 90, 22),
        LF(b, "TB-ActorCombo", ActorComboWidth, 22),
        LF(b, "TB-ClearStale", 83, 22));
    S(Toolbar, ToolbarItemPad);
    P(Toolbar, ToolbarPadL, ToolbarPadV, ToolbarPadL, ToolbarPadV);
    Sp(b.N[(size_t)Toolbar].Children[4]);
    M(b.N[(size_t)Toolbar].Children[3], 6, 2, 6, 2);  // search box slot FMargin(6,2)
    // Toolbar clusters: 8px left margin opens each group (matches RebuildWidget).
    // Button widths above are tuned so the toolbar natural width stays 1001
    // (the design window width contract asserted by Phase H).
    M(b.N[(size_t)Toolbar].Children[2], 8, 2, 2, 2);  // Import Art
    M(b.N[(size_t)Toolbar].Children[5], 8, 2, 2, 2);  // Help
    M(b.N[(size_t)Toolbar].Children[6], 8, 2, 2, 2);  // Spawn Preview
    M(b.N[(size_t)Toolbar].Children[10], 8, 2, 2, 2); // Clear Stale

    // ========================= 2. VIEW STATE STRIP =========================
    const int StateStrip = HF(b, "StateStrip",
        LF(b, "ST-Tab0", 44, 20), LF(b, "ST-Tab1", 44, 20),
        LF(b, "ST-Tab2", 44, 20), LF(b, "ST-Tab3", 44, 20),
        LF(b, "ST-Tab4", 44, 20), LF(b, "ST-Tab5", 44, 20),
        LF(b, "ST-Tab6", 44, 20), LF(b, "ST-Tab7", 44, 20),
        LF(b, "ST-Tab8", 44, 20), LF(b, "ST-Tab9", 44, 20),
        LF(b, "ST-PickBtn", 52, 20));
    S(StateStrip, StateTabPad);
    P(StateStrip, 2, 3, 2, 3);
    b.N[(size_t)StateStrip].FixedH = StateStripHeight;
    M(b.N[(size_t)StateStrip].Children[10], 4, 0, 0, 0);

    // ========================== 2b. ZONE DIAGRAM ==========================
    const int ZoneDiagram = VF(b, "ZoneDiagram",
        LF(b, "ZD-YawLabel", 120, 12),
        LF(b, "ZD-PitchLabel", 120, 12),
        HF(b, "ZD-Strips",
            LF(b, "ZD-Strip0", 40, 20), LF(b, "ZD-Strip1", 40, 20),
            LF(b, "ZD-Strip2", 40, 20), LF(b, "ZD-Strip3", 40, 20),
            LF(b, "ZD-Strip4", 40, 20), LF(b, "ZD-Strip5", 40, 20),
            LF(b, "ZD-Strip6", 40, 20), LF(b, "ZD-Strip7", 40, 20),
            LF(b, "ZD-Strip8", 40, 20), LF(b, "ZD-Strip9", 40, 20)));
    S(ZoneDiagram, 1);
    P(ZoneDiagram, 4, 2, 4, 2);
    M(b.N[(size_t)ZoneDiagram].Children[0], 2, 0, 2, 0);
    M(b.N[(size_t)ZoneDiagram].Children[1], 2, 0, 2, 0);
    {
        const int Strips = b.N[(size_t)ZoneDiagram].Children[2];
        S(Strips, 2);
        b.N[(size_t)Strips].FixedH = ZoneDiagramHeight;
        M(Strips, 2, 1, 2, 1);
    }

    // ========================== 3. MAIN AREA ==========================
    const int MainRow = HF(b, "MainRow",
        // --- 3a. RAIL ICON COLUMN ---
        VF(b, "RI-Icons",
            LF(b, "RI-Layers", RailIconSize, RailIconSize),
            LF(b, "RI-Transform", RailIconSize, RailIconSize),
            LF(b, "RI-Camera", RailIconSize, RailIconSize),
            LF(b, "RI-Debug", RailIconSize, RailIconSize),
            LF(b, "RI-Advanced", RailIconSize, RailIconSize)),

        // --- 3b. RAIL SWITCHER (overlay: one rail visible at a time) ---
        OV(b, "RAIL-Switcher",
            VF(b, "RL-Layers",
                LF(b, "RL-LayersHeader", 120, 14),
                LF(b, "RL-LayersScroll", 0, 0),
                LF(b, "RL-AddLayerBtn", 70, 20),
                VF(b, "Sec-StatusDetail",
                    LF(b, "Sec-StatusDetail-Title", 120, 14),
                    LF(b, "Sec-StatusDetail-Body", 160, 24))),
            VF(b, "RL-Transform",
                VF(b, "Sec-QuickActions",
                    LF(b, "Sec-QA-Title", 120, 14),
                    VF(b, "Sec-QA-Body",
                        HF(b, "QA-Row0",
                            LF(b, "QA-AutoFitAll", 98, 20),
                            LF(b, "QA-SyncAllAll", 119, 20),
                            LF(b, "QA-ClearAll", 147, 20),
                            LF(b, "QA-DupFront", 168, 20),
                            LF(b, "QA-FillMissing", 140, 20),
                            LF(b, "QA-Spacer", 0, 0)))),
                VF(b, "Sec-CrossView",
                    LF(b, "Sec-CV-Title", 120, 14),
                    VF(b, "Sec-CV-Body",
                        HF(b, "CV-Row",
                            LF(b, "CV-CopyLbl", 64, 14),
                            LF(b, "CV-Combo", 90, 20),
                            LF(b, "CV-CopyBtn", 105, 20),
                            LF(b, "CV-Spacer", 0, 0),
                            LF(b, "CV-LinkChk", 20, 20),
                            LF(b, "CV-LinkLbl", 28, 14))))),
            VF(b, "RL-Camera",
                VF(b, "Sec-Camera",
                    LF(b, "Sec-Cam-Title", 120, 14),
                    VF(b, "Sec-Cam-Body",
                        HF(b, "CM-Yaw",
                            LF(b, "CM-YawLbl", 40, 14), LF(b, "CM-YawSlider", 0, 0), LF(b, "CM-YawVal", 44, 10)),
                        HF(b, "CM-Pitch",
                            LF(b, "CM-PitchLbl", 44, 14), LF(b, "CM-PitchSlider", 0, 0), LF(b, "CM-PitchVal", 44, 10)),
                        HF(b, "CM-Dist",
                            LF(b, "CM-DistLbl", 36, 14), LF(b, "CM-DistSlider", 0, 0), LF(b, "CM-DistVal", 44, 10)),
                        HF(b, "CM-AutoRow",
                            LF(b, "CM-AutoChk", 20, 20), LF(b, "CM-AutoLbl", 36, 14), LF(b, "CM-AutoSlider", 0, 0), LF(b, "CM-AutoSpd", 24, 10)),
                        HF(b, "CM-ZoneRow",
                            LF(b, "CM-ZoneLbl", 96, 14),
                            LF(b, "CM-Zone0", 36, 20), LF(b, "CM-Zone1", 36, 20),
                            LF(b, "CM-Zone2", 36, 20), LF(b, "CM-Zone3", 36, 20)))),
                VF(b, "Sec-BlendPreview",
                    LF(b, "Sec-BP-Title", 120, 14),
                    VF(b, "Sec-BP-Body",
                        HF(b, "BP-Row",
                            LF(b, "BP-Chk", 20, 20), LF(b, "BP-Lbl", 40, 14), LF(b, "BP-Slider", 0, 0), LF(b, "BP-Val", 40, 10)))),
                VF(b, "Sec-CameraFollow",
                    LF(b, "Sec-CF-Title", 120, 14),
                    VF(b, "Sec-CF-Body",
                        HF(b, "CF-Row",
                            LF(b, "CF-Chk", 20, 20), LF(b, "CF-Lbl", 112, 14), LF(b, "CF-Spacer", 0, 0), LF(b, "CF-SnapBtn", 84, 20))))),
            VF(b, "RL-Debug",
                VF(b, "Sec-Import",
                    LF(b, "Sec-IM-Title", 120, 14),
                    VF(b, "Sec-IM-Body",
                        HF(b, "IM-Row",
                            LF(b, "IM-ImportBtn", 105, 20), LF(b, "IM-ImportAssign", 119, 20), LF(b, "IM-AssignCB", 147, 20), LF(b, "IM-Spacer", 0, 0)))),
                VF(b, "Sec-Config",
                    LF(b, "Sec-CFG-Title", 120, 14),
                    VF(b, "Sec-CFG-Body",
                        HF(b, "CFG-R0", LF(b, "CFG-Chk0", 20, 20), LF(b, "CFG-Lbl0", 96, 14)),
                        HF(b, "CFG-R1", LF(b, "CFG-Chk1", 20, 20), LF(b, "CFG-Lbl1", 96, 14)),
                        HF(b, "CFG-R2", LF(b, "CFG-Chk2", 20, 20), LF(b, "CFG-Lbl2", 96, 14)),
                        HF(b, "CFG-R3", LF(b, "CFG-Chk3", 20, 20), LF(b, "CFG-Lbl3", 96, 14)),
                        HF(b, "CFG-R4", LF(b, "CFG-Chk4", 20, 20), LF(b, "CFG-Lbl4", 96, 14)),
                        HF(b, "CFG-R5", LF(b, "CFG-Chk5", 20, 20), LF(b, "CFG-Lbl5", 96, 14)),
                        HF(b, "CFG-R6", LF(b, "CFG-Chk6", 20, 20), LF(b, "CFG-Lbl6", 96, 14)),
                        HF(b, "CFG-R7", LF(b, "CFG-Chk7", 20, 20), LF(b, "CFG-Lbl7", 96, 14)))),
                VF(b, "Sec-EdgeAnalysis",
                    LF(b, "Sec-EA-Title", 120, 14),
                    VF(b, "Sec-EA-Body",
                        HF(b, "EA-Row",
                            LF(b, "EA-Chk", 20, 20), LF(b, "EA-EdgeLbl", 76, 14),
                            LF(b, "EA-Chk2", 20, 20), LF(b, "EA-HistLbl", 56, 14),
                            LF(b, "EA-Rebuild", 63, 20), LF(b, "EA-Spacer", 0, 0)))),
                VF(b, "Sec-OutlineDepth",
                    LF(b, "Sec-OD-Title", 120, 14),
                    VF(b, "Sec-OD-Body",
                        HF(b, "OD-Row",
                            LF(b, "OD-GenBtn", 210, 20), LF(b, "OD-GridEdit", 44, 20), LF(b, "OD-DetectBtn", 112, 20),
                            LF(b, "OD-Spacer", 0, 0), LF(b, "OD-Chk", 20, 20), LF(b, "OD-OverlayLbl", 44, 10)),
                        HF(b, "OD-ScopeRow",
                            LF(b, "OD-BakeLbl", 36, 10),
                            LF(b, "OD-Scope0", 20, 20), LF(b, "OD-FrontLbl", 60, 10),
                            LF(b, "OD-Scope1", 20, 20), LF(b, "OD-H8Lbl", 60, 10),
                            LF(b, "OD-Scope2", 20, 20), LF(b, "OD-AllLbl", 44, 10),
                            LF(b, "OD-Spacer2", 0, 0)),
                        LF(b, "OD-Stats", 200, 10))),
                VF(b, "Sec-DepthDebug",
                    LF(b, "Sec-DD-Title", 120, 14),
                    VF(b, "Sec-DD-Body",
                        HF(b, "DD-GridRes",
                            LF(b, "DD-GridLbl", 56, 14), LF(b, "DD-GridSlider", 0, 0), LF(b, "DD-GridVal", 44, 10)),
                        HF(b, "DD-MeshSize",
                            LF(b, "DD-MeshLbl", 68, 14), LF(b, "DD-MeshSlider", 0, 0), LF(b, "DD-MeshVal", 44, 10)),
                        HF(b, "DD-Height",
                            LF(b, "DD-HeightLbl", 84, 14), LF(b, "DD-HeightSlider", 0, 0), LF(b, "DD-HeightVal", 44, 10)),
                        HF(b, "DD-Offset",
                            LF(b, "DD-OffsetLbl", 64, 14), LF(b, "DD-OffsetSlider", 0, 0), LF(b, "DD-OffsetVal", 44, 10)),
                        HF(b, "DD-Colors",
                            LF(b, "DD-LowLbl", 64, 14), LF(b, "DD-LowEdit", 70, 20), LF(b, "DD-Spacer", 0, 0)),
                        HF(b, "DD-Btns",
                            LF(b, "DD-RebuildBtn", 91, 20), LF(b, "DD-ColorBtn", 105, 20), LF(b, "DD-Spacer2", 0, 0)))),
                VF(b, "Sec-HullReview",
                    LF(b, "Sec-HR-Title", 120, 14),
                    VF(b, "Sec-HR-Body",
                        HF(b, "HR-OrbitRow",
                            LF(b, "HR-Chk", 20, 20), LF(b, "HR-OrbitLbl", 56, 14),
                            LF(b, "HR-Slider", 0, 0), LF(b, "HR-SpdLbl", 24, 10), LF(b, "HR-SnapBtn", 42, 20)),
                        GRID(b, "HR-Thumbs",
                            LF(b, "HT-0", 64, 48), LF(b, "HT-1", 64, 48), LF(b, "HT-2", 64, 48),
                            LF(b, "HT-3", 64, 48), LF(b, "HT-4", 64, 48), LF(b, "HT-5", 64, 48),
                            LF(b, "HT-6", 64, 48), LF(b, "HT-7", 64, 48), LF(b, "HT-8", 64, 48),
                            LF(b, "HT-9", 64, 48)))),
                VF(b, "Sec-VisemeGrid",
                    LF(b, "Sec-VG-Title", 120, 14),
                    VF(b, "Sec-VG-Body",
                        HF(b, "VG-Row0",
                            LF(b, "VG-C0", 18, 18), LF(b, "VG-C1", 18, 18), LF(b, "VG-C2", 18, 18),
                            LF(b, "VG-C3", 18, 18), LF(b, "VG-C4", 18, 18), LF(b, "VG-C5", 18, 18),
                            LF(b, "VG-C6", 18, 18), LF(b, "VG-C7", 18, 18), LF(b, "VG-C8", 18, 18),
                            LF(b, "VG-C9", 18, 18)),
                        HF(b, "VG-Row1",
                            LF(b, "VG-D0", 18, 18), LF(b, "VG-D1", 18, 18), LF(b, "VG-D2", 18, 18),
                            LF(b, "VG-D3", 18, 18), LF(b, "VG-D4", 18, 18), LF(b, "VG-D5", 18, 18),
                            LF(b, "VG-D6", 18, 18), LF(b, "VG-D7", 18, 18), LF(b, "VG-D8", 18, 18),
                            LF(b, "VG-D9", 18, 18)))),
                VF(b, "Sec-Problems",
                    LF(b, "Sec-PR-Title", 120, 14),
                    LF(b, "Sec-PR-Body", 200, 60))),
            VF(b, "RL-Advanced",
                VF(b, "Sec-AllLayers",
                    LF(b, "Sec-AL-Title", 120, 14),
                    LF(b, "Sec-AL-Body", 200, 80)),
                VF(b, "Sec-ParamRef",
                    LF(b, "Sec-PRF-Title", 120, 14),
                    VF(b, "Sec-PRF-Body",
                        HF(b, "PRF-Row",
                            LF(b, "PRF-Edit", 100, 20), LF(b, "PRF-FindBtn", 63, 20)),
                        LF(b, "PRF-Results", 180, 32))),
                VF(b, "Sec-ParamTable",
                    LF(b, "Sec-PT-Title", 120, 14),
                    VF(b, "Sec-PT-Body",
                        HF(b, "PT-AddRow",
                            LF(b, "PT-Edit", 90, 20), LF(b, "PT-AddBtn", 56, 20)),
                        VF(b, "PT-Rows",
                            LF(b, "PT-Row0", 220, 16), LF(b, "PT-Row1", 220, 16), LF(b, "PT-Row2", 220, 16)))),
                VF(b, "Sec-NestedPins",
                    LF(b, "Sec-NP-Title", 120, 14),
                    VF(b, "Sec-NP-Body",
                        HF(b, "NP-Stepper",
                            LF(b, "NP-Prev", 24, 20), LF(b, "NP-Index", 28, 10), LF(b, "NP-Next", 24, 20)),
                        HF(b, "NP-PinnedRow",
                            LF(b, "NP-PinChk", 20, 20), LF(b, "NP-PinLbl", 56, 14)),
                        HF(b, "NP-PinX",
                            LF(b, "NP-XLbl", 56, 14), LF(b, "NP-XSlider", 0, 0), LF(b, "NP-XVal", 44, 10)),
                        HF(b, "NP-PinY",
                            LF(b, "NP-YLbl", 56, 14), LF(b, "NP-YSlider", 0, 0), LF(b, "NP-YVal", 44, 10)),
                        HF(b, "NP-PinZ",
                            LF(b, "NP-ZLbl", 56, 14), LF(b, "NP-ZSlider", 0, 0), LF(b, "NP-ZVal", 44, 10)),
                        HF(b, "NP-RotRow",
                            LF(b, "NP-RotChk", 20, 20), LF(b, "NP-RotLbl", 120, 14)),
                        HF(b, "NP-MinRot",
                            LF(b, "NP-MinLbl", 56, 14), LF(b, "NP-MinSlider", 0, 0), LF(b, "NP-MinVal", 44, 10)),
                        HF(b, "NP-MaxRot",
                            LF(b, "NP-MaxLbl", 56, 14), LF(b, "NP-MaxSlider", 0, 0), LF(b, "NP-MaxVal", 44, 10)),
                        HF(b, "NP-Sens",
                            LF(b, "NP-SensLbl", 56, 14), LF(b, "NP-SensSlider", 0, 0), LF(b, "NP-SensVal", 44, 10)),
                        LF(b, "NP-DetectBtn", 91, 20),
                        LF(b, "NP-Outliner", 200, 60))))),

        // --- 3c. CENTER COLUMN ---
        VF(b, "CENTER",
            HF(b, "CN-ModeRow",
                LF(b, "CN-Mode0", 64, 20), LF(b, "CN-Mode1", 64, 20),
                LF(b, "CN-Mode2", 64, 20), LF(b, "CN-Mode3", 64, 20),
                LF(b, "CN-DisplayLbl", 44, 12),
                HF(b, "CN-OnionRow",
                    LF(b, "CN-OnionChk", 20, 20),
                    LF(b, "CN-OnionSlider", 0, 0),
                    LF(b, "CN-OpacityLbl", 44, 10)),
                LF(b, "CN-Spacer", 0, 0)),
            OV(b, "CN-Preview",
                LF(b, "PV-Image", 0, 0),
                LF(b, "PV-Outline", 0, 0),
                LF(b, "PV-Onion", 0, 0),
                LF(b, "PV-Edge", 0, 0),
                LF(b, "PV-Gizmo", 0, 0)),
            LF(b, "CN-LayerLabel", 200, 14)),

        // --- 3d. SLOT PROPS (right pane) ---
        VF(b, "PROPS",
            LF(b, "PR-Header", 120, 14),
            HF(b, "PR-ThumbRow",
                VF(b, "PR-ThumbCol0",
                    LF(b, "PR-Thumb0", ThumbSize, ThumbSize),
                    LF(b, "PR-Pick0", 70, 18), LF(b, "PR-Clear0", 70, 18), LF(b, "PR-Status0", ThumbSize, 10)),
                VF(b, "PR-ThumbCol1",
                    LF(b, "PR-Thumb1", ThumbSize, ThumbSize),
                    LF(b, "PR-Pick1", 70, 18), LF(b, "PR-Clear1", 70, 18), LF(b, "PR-Status1", ThumbSize, 10)),
                VF(b, "PR-ThumbCol2",
                    LF(b, "PR-Thumb2", ThumbSize, ThumbSize),
                    LF(b, "PR-Pick2", 70, 18), LF(b, "PR-Clear2", 70, 18), LF(b, "PR-Status2", ThumbSize, 10))),
            HF(b, "PR-ActRow",
                LF(b, "PR-AutoFit", 70, 20), LF(b, "PR-Reset", 49, 20),
                LF(b, "PR-SyncAll", 77, 20), LF(b, "PR-SyncTexAll", 105, 20), LF(b, "PR-AFCheck", 20, 20)),
            VF(b, "PR-Scroll",
                VF(b, "Sec-Transform",
                    LF(b, "Sec-XF-Title", 140, 14),
                    VF(b, "Sec-XF-Body",
                        HF(b, "XF-PosXRow", LF(b, "XF-PosXLbl", 44, 14), LF(b, "XF-PosXEdit", 70, 20)),
                        HF(b, "XF-PosYRow", LF(b, "XF-PosYLbl", 44, 14), LF(b, "XF-PosYEdit", 70, 20)),
                        HF(b, "XF-ScaleXRow", LF(b, "XF-ScaleXLbl", 56, 14), LF(b, "XF-ScaleXEdit", 70, 20)),
                        HF(b, "XF-ScaleYRow", LF(b, "XF-ScaleYLbl", 56, 14), LF(b, "XF-ScaleYEdit", 70, 20)),
                        HF(b, "XF-RotRow", LF(b, "XF-RotLbl", 28, 14), LF(b, "XF-RotEdit", 70, 20)))),
                VF(b, "Sec-ViewOverride",
                    LF(b, "Sec-VO-Title", 140, 14),
                    VF(b, "Sec-VO-Body",
                        HF(b, "VO-Row",
                            LF(b, "VO-Check", 20, 20), LF(b, "VO-Lbl", 96, 14),
                            LF(b, "VO-Spacer", 0, 0), LF(b, "VO-ClearBtn", 119, 20)))),
                VF(b, "Sec-SyncToViews",
                    LF(b, "Sec-SY-Title", 140, 14),
                    VF(b, "Sec-SY-Body",
                        HF(b, "SY-SyncRow",
                            LF(b, "SY-Lbl", 72, 14), LF(b, "SY-TexCheck", 20, 20),
                            LF(b, "SY-TexLbl", 22, 10), LF(b, "SY-Spacer", 0, 0), LF(b, "SY-SyncBtn", 124, 20)),
                        HF(b, "SY-BothRow",
                            LF(b, "SY-BothBtn", 126, 20)))),
                VF(b, "Sec-Alignment",
                    LF(b, "Sec-AL2-Title", 140, 14),
                    VF(b, "Sec-AL2-Body",
                        HF(b, "AL-LinkRow",
                            LF(b, "AL-LinkChk", 20, 20), LF(b, "AL-LinkLbl", 128, 14))))),
            LF(b, "PR-Status", 220, 12)));

    // MainRow fixed height (real: SBox HeightOverride(560)); stretches to root width
    // (real: root SVerticalBox slot fills the window, CenterCol FillWidth(1.0)).
    b.N[(size_t)MainRow].FixedH = MainRowHeight;
    FxW(MainRow);

    // --- Rail icon column config ---
    {
        const int RI = b.N[(size_t)MainRow].Children[0];
        b.N[(size_t)RI].FixedW = RailIconsWidth;
        P(RI, 2, 4, 0, 4);
        S(RI, 2);
        FxH(RI);
        for (int c = 0; c < (int)b.N[(size_t)RI].Children.size(); ++c)
            M(b.N[(size_t)RI].Children[(size_t)c], 2, 3, 2, 3);
    }

    // --- Rail switcher config (overlay: fixed 180x560 scroll viewports) ---
    {
        const int RailSw = b.N[(size_t)MainRow].Children[1];
        b.N[(size_t)RailSw].FixedW = RailWidth;
        FxH(RailSw);

        const int RLLayers = b.N[(size_t)RailSw].Children[0];
        S(RLLayers, 2);
        Clip(RLLayers);
        b.N[(size_t)RLLayers].FixedH = MainRowHeight;
        b.N[(size_t)RLLayers].FixedW = RailWidth;
        M(b.N[(size_t)RLLayers].Children[0], 4, 4, 4, 2);
        Fx(b.N[(size_t)RLLayers].Children[1]);
        Clip(b.N[(size_t)RLLayers].Children[1]);
        M(b.N[(size_t)RLLayers].Children[2], 4, 2, 4, 2);
        SecSetup(b.N[(size_t)RLLayers].Children[3], 0);
        M(b.N[(size_t)RLLayers].Children[3], 2, 1, 2, 1);
        P(Bod(b.N[(size_t)RLLayers].Children[3]), SectionBorderPad, SectionBorderPad, SectionBorderPad, SectionBorderPad);

        const int RLTransform = b.N[(size_t)RailSw].Children[1];
        S(RLTransform, 2);
        Clip(RLTransform);
        b.N[(size_t)RLTransform].FixedH = MainRowHeight;
        b.N[(size_t)RLTransform].FixedW = RailWidth;
        {
            const int QA = b.N[(size_t)RLTransform].Children[0];
            SecSetup(QA, 2);
            const int QA0 = b.N[(size_t)Bod(QA)].Children[0];
            S(QA0, 4);
            Sp(b.N[(size_t)QA0].Children[5]);
            const int CV = b.N[(size_t)RLTransform].Children[1];
            SecSetup(CV, 2);
            const int CVR = b.N[(size_t)Bod(CV)].Children[0];
            S(CVR, 4);
            Sp(b.N[(size_t)CVR].Children[3]);
        }

        const int RLCamera = b.N[(size_t)RailSw].Children[2];
        S(RLCamera, 2);
        Clip(RLCamera);
        b.N[(size_t)RLCamera].FixedH = MainRowHeight;
        b.N[(size_t)RLCamera].FixedW = RailWidth;
        {
            const int Cam = b.N[(size_t)RLCamera].Children[0];
            SecSetup(Cam, 2);
            const int Body = Bod(Cam);
            for (int c = 0; c < 3; ++c)
            {
                const int row = b.N[(size_t)Body].Children[(size_t)c];
                S(row, 4);
                FxW(b.N[(size_t)row].Children[1]);
            }
            {
                const int AutoR = b.N[(size_t)Body].Children[3];
                S(AutoR, 4);
                FxW(b.N[(size_t)AutoR].Children[2]);
            }
            {
                const int ZoneR = b.N[(size_t)Body].Children[4];
                S(ZoneR, 4);
                for (int c = 0; c < 4; ++c) M(b.N[(size_t)ZoneR].Children[(size_t)c + 1], 2, 2, 2, 2);
            }
            const int BP = b.N[(size_t)RLCamera].Children[1];
            SecSetup(BP, 2);
            {
                const int BPR = b.N[(size_t)Bod(BP)].Children[0];
                S(BPR, 4);
                FxW(b.N[(size_t)BPR].Children[2]);
            }
            const int CF = b.N[(size_t)RLCamera].Children[2];
            SecSetup(CF, 2);
            {
                const int CFR = b.N[(size_t)Bod(CF)].Children[0];
                S(CFR, 4);
                Sp(b.N[(size_t)CFR].Children[2]);
            }
        }

        const int RLDebug = b.N[(size_t)RailSw].Children[3];
        S(RLDebug, 2);
        Clip(RLDebug);
        b.N[(size_t)RLDebug].FixedH = MainRowHeight;
        b.N[(size_t)RLDebug].FixedW = RailWidth;
        {
            for (int c = 0; c < (int)b.N[(size_t)RLDebug].Children.size(); ++c)
                Acc(b.N[(size_t)RLDebug].Children[(size_t)c]);
            const int Im = b.N[(size_t)RLDebug].Children[0];
            SecSetup(Im, 2);
            {
                const int IM0 = b.N[(size_t)Bod(Im)].Children[0];
                S(IM0, 4);
                Sp(b.N[(size_t)IM0].Children[3]);
            }
            const int Cfg = b.N[(size_t)RLDebug].Children[1];
            SecSetup(Cfg, 2);
            for (int c = 0; c < (int)b.N[(size_t)Bod(Cfg)].Children.size(); ++c)
                S(b.N[(size_t)Bod(Cfg)].Children[(size_t)c], 4);
            const int EA = b.N[(size_t)RLDebug].Children[2];
            SecSetup(EA, 2);
            {
                const int EAR = b.N[(size_t)Bod(EA)].Children[0];
                S(EAR, 4);
                Sp(b.N[(size_t)EAR].Children[5]);
                M(b.N[(size_t)EAR].Children[2], 8, 0, 0, 0);
            }
            const int OD = b.N[(size_t)RLDebug].Children[3];
            SecSetup(OD, 2);
            {
                const int OD1 = b.N[(size_t)Bod(OD)].Children[0];
                S(OD1, 4);
                Sp(b.N[(size_t)OD1].Children[3]);
                M(b.N[(size_t)OD1].Children[4], 2, 2, 2, 2);
                M(b.N[(size_t)OD1].Children[5], 2, 2, 2, 2);
                const int ScR = b.N[(size_t)Bod(OD)].Children[1];
                S(ScR, 4);
                Sp(b.N[(size_t)ScR].Children[7]);
                M(b.N[(size_t)ScR].Children[2], 2, 2, 2, 2);
                M(b.N[(size_t)ScR].Children[4], 2, 2, 2, 2);
                M(b.N[(size_t)ScR].Children[6], 2, 2, 2, 2);
            }
            const int DD = b.N[(size_t)RLDebug].Children[4];
            SecSetup(DD, 2);
            for (int c = 0; c < 4; ++c)
            {
                const int row = b.N[(size_t)Bod(DD)].Children[(size_t)c];
                S(row, 4);
                FxW(b.N[(size_t)row].Children[1]);
            }
            {
                const int Clr = b.N[(size_t)Bod(DD)].Children[4];
                S(Clr, 4);
                Sp(b.N[(size_t)Clr].Children[2]);
                const int Btn = b.N[(size_t)Bod(DD)].Children[5];
                S(Btn, 4);
                Sp(b.N[(size_t)Btn].Children[2]);
            }
            const int HR = b.N[(size_t)RLDebug].Children[5];
            SecSetup(HR, 2);
            {
                const int Orb = b.N[(size_t)Bod(HR)].Children[0];
                S(Orb, 4);
                FxW(b.N[(size_t)Orb].Children[2]);
                M(b.N[(size_t)Orb].Children[4], 4, 0, 0, 0);
                const int Th = b.N[(size_t)Bod(HR)].Children[1];
                S(Th, 2);
                b.N[(size_t)Th].FixedH = 98;
                for (int c = 0; c < (int)b.N[(size_t)Th].Children.size(); ++c)
                    GP(b.N[(size_t)Th].Children[(size_t)c], c % 5, c / 5);
            }
            const int VG = b.N[(size_t)RLDebug].Children[6];
            SecSetup(VG, 2);
            for (int c = 0; c < (int)b.N[(size_t)Bod(VG)].Children.size(); ++c)
                S(b.N[(size_t)Bod(VG)].Children[(size_t)c], 1);
            const int Prob = b.N[(size_t)RLDebug].Children[7];
            SecSetup(Prob, 0);
        }

        const int RLAdvanced = b.N[(size_t)RailSw].Children[4];
        S(RLAdvanced, 2);
        Clip(RLAdvanced);
        b.N[(size_t)RLAdvanced].FixedH = MainRowHeight;
        b.N[(size_t)RLAdvanced].FixedW = RailWidth;
        {
            for (int c = 0; c < (int)b.N[(size_t)RLAdvanced].Children.size(); ++c)
                Acc(b.N[(size_t)RLAdvanced].Children[(size_t)c]);
            const int AL = b.N[(size_t)RLAdvanced].Children[0];
            SecSetup(AL, 0);
            P(Bod(AL), SectionBorderPad, SectionBorderPad, SectionBorderPad, SectionBorderPad);
            const int PRF = b.N[(size_t)RLAdvanced].Children[1];
            SecSetup(PRF, 2);
            S(b.N[(size_t)Bod(PRF)].Children[0], 4);
            M(b.N[(size_t)Bod(PRF)].Children[1], 2, 2, 2, 2);
            const int PT = b.N[(size_t)RLAdvanced].Children[2];
            SecSetup(PT, 2);
            S(b.N[(size_t)Bod(PT)].Children[0], 4);
            S(b.N[(size_t)Bod(PT)].Children[1], 1);
            const int NP = b.N[(size_t)RLAdvanced].Children[3];
            SecSetup(NP, 2);
            for (int c = 0; c < (int)b.N[(size_t)Bod(NP)].Children.size(); ++c)
            {
                const int row = b.N[(size_t)Bod(NP)].Children[(size_t)c];
                if (b.N[(size_t)row].Kind == ContainerKind::HFlow && b.N[(size_t)row].Children.size() == 3
                    && b.N[(size_t)b.N[(size_t)row].Children[1]].FixedW == 0.0)
                {
                    S(row, 4);
                    FxW(b.N[(size_t)row].Children[1]);
                }
            }
            M(b.N[(size_t)Bod(NP)].Children[9], 0, 2, 0, 2);
            M(b.N[(size_t)Bod(NP)].Children[10], 0, 2, 0, 2);
        }
    }

    // --- Center column config ---
    {
        const int Center = b.N[(size_t)MainRow].Children[2];
        S(Center, 2);
        Fx(Center);
        {
            const int Mode = b.N[(size_t)Center].Children[0];
            S(Mode, ModeTabPad);
            M(b.N[(size_t)Mode].Children[4], 4, 2, 4, 2);
            {
                // Onion-skin control (moved next to Display Mode): checkbox +
                // flex slider + opacity label, kept compact so the mode row
                // never overflows the center column width budget.
                const int OnionRow = b.N[(size_t)Mode].Children[5];
                M(OnionRow, 4, 2, 4, 2);
                FxW(b.N[(size_t)OnionRow].Children[1]);
                M(b.N[(size_t)OnionRow].Children[2], 2, 2, 0, 2);
            }
            Sp(b.N[(size_t)Mode].Children[6]);
        }
        {
            const int Prev = b.N[(size_t)Center].Children[1];
            b.N[(size_t)Prev].FixedH = PreviewCanvasHeight;
            M(Prev, 2, 2, 2, 0);
            FxW(Prev);
            for (int c = 0; c < (int)b.N[(size_t)Prev].Children.size(); ++c)
                Fx(b.N[(size_t)Prev].Children[(size_t)c]);
        }
        M(b.N[(size_t)Center].Children[2], 4, 2, 4, 0);
    }

    // --- Props pane config ---
    {
        const int Props = b.N[(size_t)MainRow].Children[3];
        b.N[(size_t)Props].FixedW = PropsWidth;
        S(Props, 2);
        FxH(Props);
        M(Props, 0, 0, PropsRightGap, 0);
        M(b.N[(size_t)Props].Children[0], 4, 4, 4, 2);
        M(b.N[(size_t)Props].Children[4], 6, 2, 6, 2);
        {
            const int Scroll = b.N[(size_t)Props].Children[3];
            Fx(Scroll);      // real: PropScroll FillHeight(1.0)
            Clip(Scroll);    // real: SScrollBox clips the transform/override/sync sections
            P(Scroll, 2, 2, PropsScrollInsetR, 2);
            S(Scroll, 2);
            for (int c = 0; c < (int)b.N[(size_t)Scroll].Children.size(); ++c)
            {
                SecSetup(b.N[(size_t)Scroll].Children[(size_t)c], 2);
                Acc(b.N[(size_t)Scroll].Children[(size_t)c]);
            }
        }
        {
            const int Thr = b.N[(size_t)Props].Children[1];
            S(Thr, 1);
            M(Thr, 2, 2, 2, 2);
            for (int c = 0; c < 3; ++c)
            {
                const int col = b.N[(size_t)Thr].Children[(size_t)c];
                b.N[(size_t)col].FixedW = ThumbSize + 4;
                S(col, 2);
                for (int k = 0; k < (int)b.N[(size_t)col].Children.size(); ++k)
                    M(b.N[(size_t)col].Children[(size_t)k], 2, 2, 2, 2);
            }
        }
        {
            const int Act = b.N[(size_t)Props].Children[2];
            S(Act, 2);
            M(Act, 2, 2, 2, 2);
        }
        {
            const int Scroll = b.N[(size_t)Props].Children[3];
            const int XF = b.N[(size_t)Scroll].Children[0];
            for (int c = 0; c < (int)b.N[(size_t)Bod(XF)].Children.size(); ++c)
            {
                const int row = b.N[(size_t)Bod(XF)].Children[(size_t)c];
                S(row, 4);
                M(b.N[(size_t)row].Children[1], 4, 2, 4, 2);
            }
            const int VO = b.N[(size_t)Scroll].Children[1];
            {
                const int VOR = b.N[(size_t)Bod(VO)].Children[0];
                S(VOR, 4);
                Sp(b.N[(size_t)VOR].Children[2]);
            }
            const int SY = b.N[(size_t)Scroll].Children[2];
            {
                const int SR = b.N[(size_t)Bod(SY)].Children[0];
                S(SR, 4);
                M(b.N[(size_t)SR].Children[1], 8, 2, 0, 2);
                M(b.N[(size_t)SR].Children[2], 2, 2, 2, 2);
                Sp(b.N[(size_t)SR].Children[3]);
                const int BR = b.N[(size_t)Bod(SY)].Children[1];
                S(BR, 4);
            }
            const int AL2 = b.N[(size_t)Scroll].Children[3];
            {
                const int Link = b.N[(size_t)Bod(AL2)].Children[0];
                S(Link, 4);
            }
        }
    }

    // ========================== 4. TIMELINE ==========================
    const int Timeline = VF(b, "Timeline",
        LF(b, "TL-Title", 140, 14),
        LF(b, "TL-Scroll", 0, 0));
    b.N[(size_t)Timeline].FixedH = TimelineHeight + 8;
    P(Timeline, 4, 4, 4, 4);
    Fx(b.N[(size_t)Timeline].Children[1]);
    Clip(b.N[(size_t)Timeline].Children[1]);

    // ========================== 5. BOTTOM BAR ==========================
    const int FrameCounts = LF(b, "FrameCounts", 260, 12);
    M(FrameCounts, 6, 2, 6, 2);

    const int BotArea = VF(b, "BotArea",
        LF(b, "BA-TagValidator", 240, 20),
        LF(b, "BA-MatCrossRef", 240, 20),
        HF(b, "BA-BotBar",
            LF(b, "BB-Save", 84, 20),
            LF(b, "BB-Snapshot", 70, 20),
            LF(b, "BB-Restore", 119, 20),
            LF(b, "BB-ClearState", 84, 20),
            LF(b, "BB-ClearAll", 70, 20),
            LF(b, "BB-Log", 63, 20),
            LF(b, "BB-Spacer", 0, 0),
            LF(b, "BB-StatusDetail", 300, 12)));
    S(BotArea, 1);
    P(BotArea, 2, 2, 2, 2);
    M(b.N[(size_t)BotArea].Children[0], 2, 1, 2, 1);
    M(b.N[(size_t)BotArea].Children[1], 2, 1, 2, 1);
    {
        const int BotBar = b.N[(size_t)BotArea].Children[2];
        b.N[(size_t)BotBar].FixedH = BotBarHeight;
        P(BotBar, 4, 2, 4, 2);
        S(BotBar, 2);
        Sp(b.N[(size_t)BotBar].Children[6]);
        M(b.N[(size_t)BotBar].Children[7], 4, 2, 4, 2);
    }

    // ========================== 6. DIAGNOSTIC LOG ==========================
    const int DiagLog = LF(b, "DiagnosticLog", 0, DiagnosticLogHeight);
    Fx(DiagLog);

    // =========================== ROOT ===========================
    const int Root = VF(b, "Root",
        Toolbar, StateStrip, ZoneDiagram, MainRow, Timeline, FrameCounts, BotArea, DiagLog);
    (void)Root;

    return b.N;
}

// ----------------------------------------------------------------------------
// Resolution: computes final boxes (metrics) and pixel rects for the tree
// rooted at "Root". Pure functions - no mutable state, deterministic.
// ----------------------------------------------------------------------------
struct FPBox { double W = 0, H = 0; double NatW = 0, NatH = 0; };

inline int FindRootIndex(const std::vector<FPLayoutNode>& N)
{
    for (size_t i = 0; i < N.size(); ++i)
        if (N[i].Name != nullptr && std::string(N[i].Name) == "Root")
            return (int)i;
    return -1;
}

inline void ComputeMetricsRec(const std::vector<FPLayoutNode>& N, std::vector<FPBox>& M, int i)
{
    const FPLayoutNode& n = N[(size_t)i];
    for (int ci : n.Children) ComputeMetricsRec(N, M, ci);

    double natW = 0.0, natH = 0.0;
    if (n.Children.empty())
    {
        natW = n.FixedW;
        natH = n.FixedH;
    }
    else if (n.Kind == ContainerKind::HFlow || n.Kind == ContainerKind::VFlow)
    {
        double sum = 0.0, mx = 0.0;
        for (int ci : n.Children)
        {
            const FPLayoutNode& cn = N[(size_t)ci];
            const FPBox& cm = M[(size_t)ci];
            const double w = (cn.bFlexW || cn.bClipH) ? 0.0 : cm.W + cn.MarginL + cn.MarginR;
            const double h = (cn.bFlexH || cn.bClipH) ? 0.0 : cm.H + cn.MarginT + cn.MarginB;
            if (n.Kind == ContainerKind::HFlow) { sum += w; if (h > mx) mx = h; }
            else { sum += h; if (w > mx) mx = w; }
        }
        if (n.Children.size() > 1) sum += n.Spacing * (double)(n.Children.size() - 1);
        if (n.Kind == ContainerKind::HFlow) { natW = n.PadL + n.PadR + sum; natH = n.PadT + n.PadB + mx; }
        else { natH = n.PadT + n.PadB + sum; natW = n.PadL + n.PadR + mx; }
    }
    else if (n.Kind == ContainerKind::Grid)
    {
        int maxCol = 0, maxRow = 0;
        for (int ci : n.Children)
        {
            const FPLayoutNode& cn = N[(size_t)ci];
            if (cn.GridCol > maxCol) maxCol = cn.GridCol;
            if (cn.GridRow > maxRow) maxRow = cn.GridRow;
        }
        std::vector<double> colW((size_t)maxCol + 1, 0.0), rowH((size_t)maxRow + 1, 0.0);
        for (int ci : n.Children)
        {
            const FPLayoutNode& cn = N[(size_t)ci];
            const FPBox& cm = M[(size_t)ci];
            const double w = (cn.bFlexW || cn.bClipH) ? 0.0 : cm.W + cn.MarginL + cn.MarginR;
            const double h = (cn.bFlexH || cn.bClipH) ? 0.0 : cm.H + cn.MarginT + cn.MarginB;
            if (w > colW[(size_t)cn.GridCol]) colW[(size_t)cn.GridCol] = w;
            if (h > rowH[(size_t)cn.GridRow]) rowH[(size_t)cn.GridRow] = h;
        }
        double sw = 0.0, sh = 0.0;
        for (double v : colW) sw += v;
        for (double v : rowH) sh += v;
        natW = n.PadL + n.PadR + sw + n.Spacing * (double)maxCol;
        natH = n.PadT + n.PadB + sh + n.Spacing * (double)maxRow;
    }
    else // Overlay
    {
        double mxW = 0.0, mxH = 0.0;
        for (int ci : n.Children)
        {
            const FPLayoutNode& cn = N[(size_t)ci];
            const FPBox& cm = M[(size_t)ci];
            const double w = (cn.bFlexW || cn.bClipH) ? 0.0 : cm.W + cn.MarginL + cn.MarginR;
            const double h = (cn.bFlexH || cn.bClipH) ? 0.0 : cm.H + cn.MarginT + cn.MarginB;
            if (w > mxW) mxW = w;
            if (h > mxH) mxH = h;
        }
        natW = n.PadL + n.PadR + mxW;
        natH = n.PadT + n.PadB + mxH;
    }
    M[(size_t)i].NatW = natW;
    M[(size_t)i].NatH = natH;
    M[(size_t)i].W = n.FixedW > 0.0 ? n.FixedW : natW;
    M[(size_t)i].H = n.FixedH > 0.0 ? n.FixedH : natH;
}

inline void PlaceRec(const std::vector<FPLayoutNode>& N, const std::vector<FPBox>& M,
    std::vector<FPRect>& R, int i)
{
    const FPLayoutNode& n = N[(size_t)i];
    FPRect& r = R[(size_t)i];
    const double contW = r.W - n.PadL - n.PadR;
    const double contH = r.H - n.PadT - n.PadB;
    const double cw = contW > 0.0 ? contW : 0.0;
    const double ch = contH > 0.0 ? contH : 0.0;
    const double ox = r.X + n.PadL;
    const double oy = r.Y + n.PadT;

    if (n.Kind == ContainerKind::HFlow || n.Kind == ContainerKind::VFlow)
    {
        const int cc = (int)n.Children.size();
        const bool isH = (n.Kind == ContainerKind::HFlow);
        std::vector<double> fixedAfter((size_t)cc + 1, 0.0);
        for (int k = cc - 1; k >= 0; --k)
        {
            fixedAfter[(size_t)k] = fixedAfter[(size_t)k + 1];
            const int ki = n.Children[(size_t)k];
            const FPLayoutNode& kn = N[(size_t)ki];
            const FPBox& km = M[(size_t)ki];
            const bool absorb = isH ? (kn.bSpacer || kn.bFlexW) : (kn.bSpacer || kn.bFlexH);
            if (!absorb)
                fixedAfter[(size_t)k] += isH ? km.W + kn.MarginL + kn.MarginR
                                             : km.H + kn.MarginT + kn.MarginB;
        }
        const double cont = isH ? cw : ch;
        const double origin = isH ? ox : oy;
        double cursor = origin;
        for (int c = 0; c < cc; ++c)
        {
            const int ci = n.Children[(size_t)c];
            const FPLayoutNode& cn = N[(size_t)ci];
            const FPBox& cm = M[(size_t)ci];
            const bool absorb = isH ? (cn.bSpacer || cn.bFlexW) : (cn.bSpacer || cn.bFlexH);
            const double absorbExt = absorb
                ? std::max(0.0, cont - (cursor - origin) - fixedAfter[(size_t)c]
                    - (double)(cc - 1 - c) * n.Spacing)
                : 0.0;
            FPRect& cr = R[(size_t)ci];
            if (isH)
            {
                const double w = absorb ? absorbExt : cm.W;
                const double h = cn.bFlexH ? std::max(0.0, ch - cn.MarginT - cn.MarginB) : cm.H;
                cr.X = cursor + cn.MarginL;
                cr.Y = oy + cn.MarginT;
                cr.W = w;
                cr.H = h;
                cursor = cr.X + cr.W + cn.MarginR + n.Spacing;
            }
            else
            {
                const double h = absorb ? absorbExt : cm.H;
                const double w = cn.bFlexW ? std::max(0.0, cw - cn.MarginL - cn.MarginR) : cm.W;
                cr.X = ox + cn.MarginL;
                cr.Y = cursor + cn.MarginT;
                cr.W = w;
                cr.H = h;
                cursor = cr.Y + cr.H + cn.MarginB + n.Spacing;
            }
            PlaceRec(N, M, R, ci);
        }
    }
    else if (n.Kind == ContainerKind::Grid)
    {
        int maxCol = 0, maxRow = 0;
        for (int ci : n.Children)
        {
            const FPLayoutNode& cn = N[(size_t)ci];
            if (cn.GridCol > maxCol) maxCol = cn.GridCol;
            if (cn.GridRow > maxRow) maxRow = cn.GridRow;
        }
        std::vector<double> colW((size_t)maxCol + 1, 0.0), rowH((size_t)maxRow + 1, 0.0);
        for (int ci : n.Children)
        {
            const FPLayoutNode& cn = N[(size_t)ci];
            const FPBox& cm = M[(size_t)ci];
            const double w = (cn.bFlexW || cn.bClipH) ? 0.0 : cm.W + cn.MarginL + cn.MarginR;
            const double h = (cn.bFlexH || cn.bClipH) ? 0.0 : cm.H + cn.MarginT + cn.MarginB;
            if (w > colW[(size_t)cn.GridCol]) colW[(size_t)cn.GridCol] = w;
            if (h > rowH[(size_t)cn.GridRow]) rowH[(size_t)cn.GridRow] = h;
        }
        std::vector<double> colO((size_t)maxCol + 1, 0.0), rowO((size_t)maxRow + 1, 0.0);
        double x = ox;
        for (size_t c = 0; c < colO.size(); ++c) { colO[c] = x; x += colW[c] + n.Spacing; }
        double y = oy;
        for (size_t rr = 0; rr < rowO.size(); ++rr) { rowO[rr] = y; y += rowH[rr] + n.Spacing; }
        for (int ci : n.Children)
        {
            const FPLayoutNode& cn = N[(size_t)ci];
            const FPBox& cm = M[(size_t)ci];
            FPRect& cr = R[(size_t)ci];
            cr.X = colO[(size_t)cn.GridCol] + cn.MarginL;
            cr.Y = rowO[(size_t)cn.GridRow] + cn.MarginT;
            cr.W = cn.bFlexW ? colW[(size_t)cn.GridCol] : cm.W;
            cr.H = cn.bFlexH ? rowH[(size_t)cn.GridRow] : cm.H;
            PlaceRec(N, M, R, ci);
        }
    }
    else if (n.Kind == ContainerKind::Overlay)
    {
        for (int ci : n.Children)
        {
            const FPLayoutNode& cn = N[(size_t)ci];
            const FPBox& cm = M[(size_t)ci];
            FPRect& cr = R[(size_t)ci];
            cr.X = ox + cn.MarginL;
            cr.Y = oy + cn.MarginT;
            cr.W = (cn.bFlexW || cn.bSpacer) ? cw : cm.W;
            cr.H = (cn.bFlexH || cn.bSpacer) ? ch : cm.H;
            PlaceRec(N, M, R, ci);
        }
    }
}

inline std::vector<FPRect> ResolveLayout(const std::vector<FPLayoutNode>& Nodes)
{
    std::vector<FPRect> R(Nodes.size(), FPRect{});
    const int root = FindRootIndex(Nodes);
    if (root < 0) return R;
    std::vector<FPBox> M(Nodes.size(), FPBox{});
    ComputeMetricsRec(Nodes, M, root);
    R[(size_t)root].W = M[(size_t)root].W;
    R[(size_t)root].H = M[(size_t)root].H;
    PlaceRec(Nodes, M, R, root);
    return R;
}

inline int CountReachable(const std::vector<FPLayoutNode>& Nodes)
{
    const int root = FindRootIndex(Nodes);
    if (root < 0) return 0;
    std::vector<unsigned char> Vis(Nodes.size(), 0);
    int count = 0;
    auto Walk = [&](auto&& Self, int i) -> void
    {
        if (Vis[(size_t)i]) return;
        Vis[(size_t)i] = 1;
        ++count;
        for (int ci : Nodes[(size_t)i].Children) Self(Self, ci);
    };
    Walk(Walk, root);
    return count;
}

// ----------------------------------------------------------------------------
// Validator: enforces P1..P11 over the resolved tree.
// ----------------------------------------------------------------------------
inline std::vector<FPViolation> ValidateDesign(const std::vector<FPLayoutNode>& Nodes)
{
    std::vector<FPViolation> Out;
    const int root = FindRootIndex(Nodes);
    if (root < 0) return Out;

    std::vector<FPBox> M(Nodes.size(), FPBox{});
    ComputeMetricsRec(Nodes, M, root);
    std::vector<FPRect> R = ResolveLayout(Nodes);

    const double Eps = 1e-6;
    std::vector<unsigned char> Vis(Nodes.size(), 0);

    auto HasAbsorber = [&](int i)
    {
        for (int ci : Nodes[(size_t)i].Children)
        {
            const FPLayoutNode& cn = Nodes[(size_t)ci];
            if (cn.bSpacer || cn.bFlexW || cn.bFlexH || cn.bClipH) return true;
        }
        return false;
    };

    auto InPalette = [](double s)
    {
        for (double p : PaletteVals) if (std::abs(s - p) < 1e-9) return true;
        return false;
    };

    auto Walk = [&](auto&& Self, int i) -> void
    {
        if (Vis[(size_t)i]) return;
        Vis[(size_t)i] = 1;
        const FPLayoutNode& n = Nodes[(size_t)i];
        for (int ci : n.Children) Self(Self, ci);

        const FPBox& m = M[(size_t)i];
        const FPRect& r = R[(size_t)i];
        const bool isLeaf = n.Children.empty();

        // ---- P3 PositiveSize ----
        if (!n.bSpacer)
        {
            if (isLeaf)
            {
                if (!n.bFlexW && !n.bFlexH && (m.NatW <= Eps || m.NatH <= Eps))
                    Out.push_back({ DesignRule::ZeroSize, n.Name, "leaf has no extent" });
            }
            else if (m.W <= Eps || m.H <= Eps)
            {
                bool allVoid = true;
                for (int ci : n.Children)
                {
                    const FPLayoutNode& cn = Nodes[(size_t)ci];
                    if (!cn.bSpacer && !cn.bFlexW && !cn.bFlexH && !cn.bClipH) { allVoid = false; break; }
                }
                if (!allVoid)
                    Out.push_back({ DesignRule::ZeroSize, n.Name, "container has zero extent with real children" });
            }
        }

        // ---- P4 SpacingSanity + P5 SpacingPalette ----
        if (n.Children.size() > 1)
        {
            if (n.Spacing < 0.0)
                Out.push_back({ DesignRule::SpacingSanity, n.Name, "negative sibling spacing" });
            else if (n.Spacing > 0.0 && n.Spacing < 1.0)
                Out.push_back({ DesignRule::SpacingSanity, n.Name, "sub-pixel sibling spacing" });
            if (!InPalette(n.Spacing))
                Out.push_back({ DesignRule::OffPaletteSpacing, n.Name, "spacing off the rhythm palette" });
        }

        // ---- P6 MarginBudget ----
        if (n.PadL > MaxMargin || n.PadT > MaxMargin || n.PadR > MaxMargin || n.PadB > MaxMargin)
            Out.push_back({ DesignRule::MarginOverBudget, n.Name, "container padding exceeds 8px" });
        if (n.MarginL > MaxMargin || n.MarginT > MaxMargin || n.MarginR > MaxMargin || n.MarginB > MaxMargin)
            Out.push_back({ DesignRule::MarginOverBudget, n.Name, "slot margin exceeds 8px" });

        // ---- P16 SectionDensity: clipped viewports may hold at most 4
        // ---- plain sections; denser stacks must be accordion sections.
        if (n.bClipH)
        {
            int PlainSections = 0;
            for (int ci : n.Children)
            {
                const FPLayoutNode& cn = Nodes[(size_t)ci];
                if (cn.bSection && !cn.bAccordion) ++PlainSections;
            }
            if (PlainSections > 4)
                Out.push_back({ DesignRule::DensityOverflow, n.Name,
                    "more than 4 plain sections in a clipped viewport (use accordion sections)" });
        }

        // ---- P9 SectionTitleFirst ----
        if (n.bSection)
        {
            if (n.Children.empty() || !Nodes[(size_t)n.Children[0]].bTitle)
                Out.push_back({ DesignRule::SectionTitleFirst, n.Name, "section must start with a title child" });
        }

        // ---- P10 FitNoClip + P11 MinimalSpace (fixed-size containers) ----
        if (!isLeaf && !n.bClipH)
        {
            if (n.FixedW > 0.0 && m.NatW > n.FixedW + Eps)
                Out.push_back({ DesignRule::FitNoClip, n.Name, "children wider than fixed width" });
            if (n.FixedH > 0.0 && m.NatH > n.FixedH + Eps)
                Out.push_back({ DesignRule::FitNoClip, n.Name, "children taller than fixed height" });
        }
        if (!isLeaf && !n.bClipH && !HasAbsorber(i))
        {
            if (n.FixedW > 0.0)
            {
                const double unused = n.FixedW - m.NatW;
                if (unused > 8.0 && unused > 0.5 * n.FixedW)
                    Out.push_back({ DesignRule::MinimalSpace, n.Name, "excessive horizontal whitespace" });
            }
            if (n.FixedH > 0.0)
            {
                const double unused = n.FixedH - m.NatH;
                if (unused > 8.0 && unused > 0.5 * n.FixedH)
                    Out.push_back({ DesignRule::MinimalSpace, n.Name, "excessive vertical whitespace" });
            }
        }

        // ---- Per-child checks: P2 containment, P7 read order, P1 overlap ----
        if (!isLeaf)
        {
            const double contL = r.X + n.PadL;
            const double contT = r.Y + n.PadT;
            const double contR = contL + (r.W - n.PadL - n.PadR);
            const double contB = contT + (r.H - n.PadT - n.PadB);
            double prevPos = 0.0;
            bool havePrev = false;
            for (int c = 0; c < (int)n.Children.size(); ++c)
            {
                const int ci = n.Children[(size_t)c];
                const FPLayoutNode& cn = Nodes[(size_t)ci];
                const FPRect& cr = R[(size_t)ci];
                if (cn.bSpacer) continue;

                if (!n.bClipH && !cn.bFlexW && !cn.bFlexH && !cn.bClipH)
                {
                    if (cr.X < contL - Eps || cr.Y < contT - Eps ||
                        cr.X + cr.W > contR + Eps || cr.Y + cr.H > contB + Eps)
                        Out.push_back({ DesignRule::OutsideParent, n.Name, cn.Name });
                }

                const double pos = (n.Kind == ContainerKind::HFlow) ? cr.X : cr.Y;
                if (havePrev && pos < prevPos - Eps)
                    Out.push_back({ DesignRule::ReadOrderBroken, n.Name, cn.Name });
                prevPos = pos;
                havePrev = true;

                if (n.Kind != ContainerKind::Overlay)
                {
                    for (int c2 = 0; c2 < c; ++c2)
                    {
                        const int cj = n.Children[(size_t)c2];
                        const FPRect& or2 = R[(size_t)cj];
                        if (cr.X < or2.X + or2.W - Eps && or2.X < cr.X + cr.W - Eps &&
                            cr.Y < or2.Y + or2.H - Eps && or2.Y < cr.Y + cr.H - Eps)
                        {
                            Out.push_back({ DesignRule::NoSiblingOverlap, n.Name, cn.Name });
                            break;
                        }
                    }
                }
            }

            // ---- P8 GridAlignment ----
            if (n.Kind == ContainerKind::Grid)
            {
                for (int a = 0; a < (int)n.Children.size(); ++a)
                {
                    const FPLayoutNode& an = Nodes[(size_t)n.Children[(size_t)a]];
                    const FPRect& ar = R[(size_t)n.Children[(size_t)a]];
                    for (int b = a + 1; b < (int)n.Children.size(); ++b)
                    {
                        const FPLayoutNode& bn = Nodes[(size_t)n.Children[(size_t)b]];
                        const FPRect& br = R[(size_t)n.Children[(size_t)b]];
                        if (an.GridCol == bn.GridCol && std::abs(ar.X - br.X) > Eps)
                            Out.push_back({ DesignRule::GridMisaligned, n.Name, "same-column children at different x" });
                        if (an.GridRow == bn.GridRow && std::abs(ar.Y - br.Y) > Eps)
                            Out.push_back({ DesignRule::GridMisaligned, n.Name, "same-row children at different y" });
                    }
                }
            }
        }
    };

    Walk(Walk, root);

    // --- Ancestor / viewport tables for P12 + P13 ---
    std::vector<int> Parent(Nodes.size(), -1);
    std::vector<unsigned char> InViewport(Nodes.size(), 0);
    {
        std::vector<int> Stack;
        Stack.push_back(root);
        while (!Stack.empty())
        {
            const int cur = Stack.back();
            Stack.pop_back();
            for (int ci : Nodes[(size_t)cur].Children)
            {
                Parent[(size_t)ci] = cur;
                InViewport[(size_t)ci] = (unsigned char)
                    ((InViewport[(size_t)cur] != 0 || Nodes[(size_t)cur].bClipH) ? 1 : 0);
                Stack.push_back(ci);
            }
        }
    }
    auto IsAncestor = [&](int a, int b)
    {
        while (b >= 0) { if (b == a) return true; b = Parent[(size_t)b]; }
        return false;
    };
    auto SameOverlayStack = [&](int a, int b)
    {
        for (size_t i = 0; i < Nodes.size(); ++i)
        {
            if (Nodes[i].Kind != ContainerKind::Overlay) continue;
            int ia = -1, ib = -1;
            for (size_t c = 0; c < Nodes[i].Children.size(); ++c)
            {
                const int child = Nodes[i].Children[c];
                if (child == a || IsAncestor(child, a)) ia = (int)c;
                if (child == b || IsAncestor(child, b)) ib = (int)c;
            }
            if (ia >= 0 && ib >= 0 && ia != ib) return true;
        }
        return false;
    };

    // ---- P13 WithinScreenBounds: every visible box inside the root rect ----
    const FPRect& rr = R[(size_t)root];
    for (size_t i = 0; i < Nodes.size(); ++i)
    {
        if ((int)i == root || InViewport[i] != 0) continue;
        const FPLayoutNode& n = Nodes[i];
        const FPRect& r = R[i];
        if (n.bSpacer) continue;
        if (r.X < rr.X - Eps || r.Y < rr.Y - Eps ||
            r.X + r.W > rr.X + rr.W + Eps || r.Y + r.H > rr.Y + rr.H + Eps)
            Out.push_back({ DesignRule::WithinScreenBounds, n.Name,
                "outside the screen (root) rect" });
    }

    // ---- P12 NoGlobalOverlap: no two unrelated visible boxes intersect ----
    for (size_t i = 0; i < Nodes.size(); ++i)
    {
        const FPRect& ri = R[i];
        const FPLayoutNode& ni = Nodes[i];
        if (ri.W <= Eps || ri.H <= Eps || ni.bSpacer || InViewport[i] != 0) continue;
        for (size_t j = i + 1; j < Nodes.size(); ++j)
        {
            const FPRect& rj = R[j];
            const FPLayoutNode& nj = Nodes[j];
            if (rj.W <= Eps || rj.H <= Eps || nj.bSpacer || InViewport[j] != 0) continue;
            if (IsAncestor((int)i, (int)j) || IsAncestor((int)j, (int)i)) continue;
            if (SameOverlayStack((int)i, (int)j)) continue;
            if (ri.X < rj.X + rj.W - Eps && rj.X < ri.X + ri.W - Eps &&
                ri.Y < rj.Y + rj.H - Eps && rj.Y < ri.Y + ri.H - Eps)
            {
                Out.push_back({ DesignRule::GlobalOverlap, ni.Name,
                    std::string("overlaps ") + nj.Name });
                break;
            }
        }
    }

    return Out;
}

// ============================================================================
// Hotspot regions: named polygon buckets for spatial part selection (Phase 4).
// Pure C++17 - hit-tested by SFaceHotspotLayer in the preview canvas and by
// TestHotspotRegions in the math suite. Semantics:
//   - A region matches when the point is inside its outer loop (boundary
//     INCLUSIVE: a point exactly on an edge or vertex counts as inside) and
//     NOT inside any of its hole loops (hole boundary counts as inside the
//     hole, i.e. excluded).
//   - Overlapping regions: first match wins (table order).
//   - Degenerate loops (fewer than 3 distinct points) match only points that
//     lie on the loop itself (zero-area boundary).
// ============================================================================
struct FPHotspotPoint { double X = 0, Y = 0; };

struct FPHotspotRegion
{
    FPHotspotRegion() : Name(nullptr) {}
    FPHotspotRegion(const char* InName,
        std::vector<FPHotspotPoint> InOuter = {},
        std::vector<std::vector<FPHotspotPoint>> InHoles = {})
        : Name(InName), Outer(std::move(InOuter)), Holes(std::move(InHoles)) {}

    const char* Name = nullptr;
    std::vector<FPHotspotPoint> Outer;
    std::vector<std::vector<FPHotspotPoint>> Holes;
};

inline FPHotspotPoint HP(double X, double Y) { return { X, Y }; }

// Distance from P to the segment AB (squared); -1 when AB is degenerate.
inline double FPPointSegDist2(double PX, double PY, double AX, double AY, double BX, double BY)
{
    const double Dx = BX - AX, Dy = BY - AY;
    const double L2 = Dx * Dx + Dy * Dy;
    if (L2 <= 0.0) return -1.0;
    double T = ((PX - AX) * Dx + (PY - AY) * Dy) / L2;
    T = T < 0.0 ? 0.0 : (T > 1.0 ? 1.0 : T);
    const double CX = AX + T * Dx, CY = AY + T * Dy;
    const double Ex = PX - CX, Ey = PY - CY;
    return Ex * Ex + Ey * Ey;
}

// Boundary-inclusive point-in-polygon (even-odd rule, degenerate edges skipped).
// A point within EpsDist of the loop counts as ON the boundary (inside).
inline bool FPPointInPolygon(double X, double Y, const std::vector<FPHotspotPoint>& Pts)
{
    constexpr double EpsDist = 1e-9;   // real-space boundary tolerance
    constexpr double EpsDist2 = EpsDist * EpsDist;
    if (Pts.size() == 1)
    {
        const double Dx = X - Pts[0].X, Dy = Y - Pts[0].Y;
        return Dx * Dx + Dy * Dy <= EpsDist2; // zero-area loop: point itself is the boundary
    }
    bool bInside = false;
    for (size_t i = 0, j = Pts.size() - 1; i < Pts.size(); j = i++)
    {
        const double AX = Pts[j].X, AY = Pts[j].Y;
        const double BX = Pts[i].X, BY = Pts[i].Y;
        const double D = FPPointSegDist2(X, Y, AX, AY, BX, BY);
        if (D >= 0.0 && D <= EpsDist2) return true; // on edge or vertex -> inside
        if ((AY > Y) != (BY > Y))
        {
            const double Xint = AX + (Y - AY) * (BX - AX) / (BY - AY);
            if (Xint > X) bInside = !bInside;
        }
    }
    return bInside;
}

inline bool FPHotspotMatch(const FPHotspotRegion& R, double X, double Y)
{
    if (!FPPointInPolygon(X, Y, R.Outer)) return false;
    for (const std::vector<FPHotspotPoint>& Hole : R.Holes)
        if (FPPointInPolygon(X, Y, Hole)) return false;
    return true;
}

// First-match-wins named bucket lookup. Returns region index or -1.
inline int FPHotspotHitIndex(const std::vector<FPHotspotRegion>& Regions, double X, double Y)
{
    for (size_t i = 0; i < Regions.size(); ++i)
        if (FPHotspotMatch(Regions[i], X, Y)) return (int)i;
    return -1;
}

inline const char* FPHotspotHit(const std::vector<FPHotspotRegion>& Regions, double X, double Y)
{
    const int Idx = FPHotspotHitIndex(Regions, X, Y);
    return Idx >= 0 ? Regions[(size_t)Idx].Name : nullptr;
}

// Default face-template regions in UV space (0..1, y-down). Includes one
// concave bucket (Nose) and one bucket with a hole (Mouth, hole covered by
// Teeth which is listed after Mouth so first-match-wins yields Teeth).
inline std::vector<FPHotspotRegion> DefaultHotspotRegions()
{
    return {
        { "BrowL", { HP(0.14, 0.14), HP(0.20, 0.10), HP(0.30, 0.10), HP(0.34, 0.15),
                     HP(0.28, 0.18), HP(0.19, 0.18) } },
        { "BrowR", { HP(0.66, 0.10), HP(0.80, 0.10), HP(0.86, 0.14), HP(0.81, 0.18),
                     HP(0.72, 0.18) } },
        { "EyeL", { HP(0.15, 0.22), HP(0.20, 0.19), HP(0.31, 0.19), HP(0.35, 0.23),
                    HP(0.30, 0.29), HP(0.21, 0.29) } },
        { "EyeR", { HP(0.65, 0.19), HP(0.80, 0.19), HP(0.85, 0.22), HP(0.79, 0.29),
                    HP(0.70, 0.29) } },
        { "Nose", // concave: wide bridge, notched sides, narrow tip
            { HP(0.42, 0.24), HP(0.58, 0.24), HP(0.60, 0.33), HP(0.55, 0.35),
              HP(0.57, 0.44), HP(0.55, 0.52), HP(0.45, 0.52), HP(0.43, 0.44),
              HP(0.45, 0.35), HP(0.40, 0.33) } },
        { "CheekL", { HP(0.06, 0.30), HP(0.13, 0.22), HP(0.20, 0.34), HP(0.21, 0.52),
                      HP(0.15, 0.64), HP(0.07, 0.58) } },
        { "CheekR", { HP(0.80, 0.22), HP(0.94, 0.30), HP(0.93, 0.58), HP(0.85, 0.64),
                      HP(0.79, 0.52) } },
        { "Mouth", // outer loop with an open-mouth hole
            { HP(0.36, 0.62), HP(0.42, 0.58), HP(0.58, 0.58), HP(0.64, 0.62),
              HP(0.62, 0.68), HP(0.56, 0.73), HP(0.44, 0.73), HP(0.38, 0.68) },
            { { HP(0.43, 0.64), HP(0.57, 0.64), HP(0.59, 0.68), HP(0.41, 0.68) } } },
        { "Teeth", { HP(0.43, 0.64), HP(0.57, 0.64), HP(0.59, 0.68), HP(0.41, 0.68) } },
        { "Chin", { HP(0.40, 0.74), HP(0.60, 0.74), HP(0.57, 0.86), HP(0.43, 0.86) } },
        { "EarL", { HP(0.03, 0.22), HP(0.07, 0.13), HP(0.11, 0.26), HP(0.10, 0.44),
                    HP(0.06, 0.48) } },
        { "EarR", { HP(0.89, 0.13), HP(0.97, 0.22), HP(0.94, 0.48), HP(0.90, 0.44),
                    HP(0.89, 0.26) } },
        { "Neck", { HP(0.42, 0.88), HP(0.58, 0.88), HP(0.70, 0.98), HP(0.30, 0.98) } }
    };
}

} // namespace FPLayout

