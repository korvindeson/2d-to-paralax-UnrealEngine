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
//   P17 FitNoVScroll      - UI testing procedure, step 1: a bNoVScroll
//                          viewport is arranged so its content fits with NO
//                          vertical scroll bar. Checkable: the summed natural
//                          height of the viewport's non-accordion children
//                          (accordion sections collapse to their headers) must
//                          stay inside FixedH. The 5 rails (P6: Animated
//                          Variants merged into Nested) are bNoVScroll:
//                          fit-packed stacks, one-open accordions and carousel
//                          pages replace the old vertical rails scroll.
//   P18 CarouselFallback  - UI testing procedure, step 2: content that cannot
//                          fit (dynamic row lists) must use a page-flip
//                          carousel (bCarousel fixed-height page viewport +
//                          bCarouselNav prev/page/next strip AFTER it) instead
//                          of a vertical scroll. Checkable: every bCarousel
//                          node has FixedH > 0 and a bCarouselNav sibling.
//   P19 ScrollbarReserve  - UI testing procedure, step 3: a carousel viewport
//                          keeps a bottom padding reserve (ScrollReserveBottom
//                          = 8 px) so the page content never blocks the nav
//                          buttons under it. Checkable: every bCarousel node
//                          has PadB >= ScrollReserveBottom.
//   P20 PageWhitespaceReview - per-tab whitespace review: fixed section pages
//                          of a carousel must be packed so no two adjacent
//                          pages that fit inside the page viewport stay
//                          separate (excessive whitespace per tab). Checkable:
//                          deterministic greedy pack (CarouselMinPages) - the
//                          page count must equal the achievable minimum.
//   P22 NoHorizontalOverflow - a clipped viewport's content must fit its fixed
//                          width: no row may be wider than the rail, so the
//                          rail NEVER scrolls left-to-right under neighboring
//                          panels (the rail horizontal-scroll defect). The
//                          widget mirrors this by building rails WITHOUT a
//                          horizontal SScrollBox; wide rows are redesigned
//                          (short labels, stacked rows) instead of scrolled.
//                          Checkable: every non-flex child of a bClipH node
//                          with FixedW > 0 keeps its natural width + margins
//                          inside FixedW - PadL - PadR.
//   P23 AspectRatioBroken - an aspect-locked node (bAspectRatio - the face
//                          schematic canvas) must keep FaceAspectRatio in its
//                          resolved rect, so the face is never stretched.
//                          Checkable: the resolved rect ratio of every
//                          bAspectRatio node equals FaceAspectRatio within 2%.
//   P24 NoTerminalOverlap - nothing under the schematic may slide into the
//                          timeline / terminal output window below the main
//                          row. Every MainRow column and every row of the
//                          CENTER column (mode row, schematic filter, canvas,
//                          legends, parts strip, layer label) must resolve
//                          inside the MainRowHeight band. Checkable: the
//                          resolved bottom edge of every MainRow/CENTER child
//                          stays within its parent's resolved bottom edge.
//
// Scroll-viewport model: the rails are fixed 180xMainRowHeight viewports whose
// content (wide button rows, tall section stacks) scrolls; bClipH marks the
// viewport
// and exempts its whole subtree from P2/P10/P11/P12/P13 - exactly what a
// clipped/scrollable Slate widget does visually. Vertical scroll is only
// modelled this way as an exemption; the widget itself must not construct
// vertical SScrollBoxes (SyntaxValidator rule 8). WIDTH is never exempt:
// P22 keeps rail content inside the 180px contract so nothing can scroll
// under the schematic.

#include <cstddef>
#include <cctype>
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
inline constexpr double ActorComboWidth     = 162.0;  // actor combo WidthOverride
inline constexpr double StateStripHeight    = 26.0;   // view-state strip
inline constexpr double StateTabPad         = 1.0;    // tab slot spacing
inline constexpr double StateDotSize        = 8.0;    // state dot box
inline constexpr double ZoneDiagramHeight   = 20.0;   // zone strips row
inline constexpr double ModeTabPad          = 1.0;    // display-mode row spacing
inline constexpr double PreviewCanvasHeight = 450.0;  // SBox HeightOverride
// Main area fixed height. The band must fit the FULL rail-0 content (layer
// carousel + nav + add button + collapsed pins + the paged Status Detail
// carousel + the All Layers carousel) plus the 26px rail-chips row above the
// rail clip, so the Status Detail matrix can never slide under the terminal
// output window (P17 fit-first + P24 NoTerminalOverlap).
inline constexpr double MainRowHeight       = 800.0;  // main area fixed height (fits the full center column + rails)
inline constexpr double PinnedStripHeight   = 26.0;   // pinned action strip (P21)
inline constexpr double TabBarHeight        = 26.0;   // top-level rail tab bar (Phase B)
inline constexpr double RailIconsWidth      = 36.0;   // rail icon column
inline constexpr double RailIconSize        = 30.0;   // rail icon buttons
inline constexpr double MainRowWidth        = 1089.0; // toolbar natural width (root width) - the fixed design band
inline constexpr double RailWidthMin        = 180.0;  // rail width range (Phase 4 slider - library only, not resizable)
inline constexpr double RailWidthMax        = 360.0;  // rail width range (Phase 4 slider - library only, not resizable)
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
inline constexpr double FaceAspectRatio      = 1.0;    // square face schematic canvas (mirrors the 1024x1024 render target)
inline constexpr double FaceCanvasWidth      = 450.0;  // P23: canvas width = height x aspect - the face is never stretched
// The center column's widest fixed row is CN-ModeRow (5 x 76 display-mode
// buttons + the Canvas-Options overflow row + slot margins = 468); the canvas,
// filter row, legends and parts strip are all narrower.
inline constexpr double CenterColumnMinWidth = 468.0;  // center column needs this much or its rows overlap the props pane
// Rail width is FIXED and derived from the empty space of the edge-schematic
// section: it takes the maximum width that still leaves the center column its
// CenterColumnMinWidth (mode row + 450px aspect-locked canvas) AND the 340px
// props pane (+ PropsRightGap) fully visible - no row may ever reach the props
// pane (P24 NoTerminalOverlap defect class). It is NOT manually resizable - no
// internal splitter; resizing only happens at the very outside of the widget
// (the window/tab edge). A resizable rail lets users steal the canvas's space,
// clipping the edge map and breaking the paged carousels.
inline constexpr double RailWidth           = MainRowWidth - PropsWidth - PropsRightGap - CenterColumnMinWidth;

// W1 (context panel): the right-side rail switcher (273px) + props pane
// (340px + 8px edge gap) merge into ONE 621px context panel switched by the
// top tab row. The width derives from the edge-schematic section's empty
// space exactly like the old rail: MainRowWidth - CenterColumnMinWidth
// (= 1089 - 468 = 621 = RailWidth 273 + PropsWidth 340 + PropsRightGap 8).
// The panel is FIXED - no internal splitter; resizing happens only at the
// very outside of the widget (P24 keeps every MainRow column inside the
// MainRowHeight band).
inline constexpr double ContextPanelWidth   = MainRowWidth - CenterColumnMinWidth;

// UI testing procedures (fit-first / carousel fallback / padding reserve):
// P17 FitNoVScroll: content that fits is packed with no vertical scroll bar.
// P18 CarouselFallback: dynamic row lists flip through pages instead.
// P19 ScrollbarReserve: a page viewport keeps ScrollReserveBottom padding
// below its content so the nav strip (and buttons under it) are never blocked.
inline constexpr double CarouselRowHeight     = 22.0;   // nominal row height of a carousel page
inline constexpr int    CarouselRowsPerPage   = 8;      // rows per carousel page (layers/issues/cross-layer)
inline constexpr double CarouselViewportH     = 184.0;  // page viewport: 8 x 22 = 176 content + 8 reserve
inline constexpr double CarouselNavHeight     = 22.0;   // prev/page/next strip height
inline constexpr double ScrollReserveBottom   = 8.0;    // P19: reserve between page content and nav/buttons
inline constexpr double CarouselMergeSpacing  = 4.0;    // P20: gap between page bodies when merging adjacent pages

// Status Detail matrix (real: RebuildStatusMatrix): a fixed-height carousel
// viewport pages N layer rows (StatusMatrixRowH each) below a 28px header row
// (corner label + state abbrs) with a bottom reserve (P19) - the same budget
// as the other rail carousels. The real matrix is UNBOUNDED (one 44px row per
// layer, last row = "Hair"), so the mirror keeps it page-bounded or P17 fires
// the "slides under the terminal" overlap defect class (P24). Rows per page
// = (CarouselViewportH - header - reserve) / rowH = 3.
inline constexpr double StatusMatrixHeaderH   = 28.0;   // "STATE \ LAYER" + state abbrs row
inline constexpr double StatusMatrixRowH      = 44.0;   // one layer row (texture thumb cell)
inline constexpr int    StatusMatrixRowsPerPage = 3;    // header + 3 data rows = 160 <= 176 page content

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
    DensityOverflow,    // P16
    FitNoVScroll,       // P17
    CarouselFallback,   // P18
    ScrollbarReserve,   // P19
    PageWhitespaceReview, // P20
    PinnedActionsNeverInScroll, // P21
    NoHorizontalOverflow, // P22
    AspectRatioBroken,    // P23
    NoTerminalOverlap     // P24
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
        case DesignRule::FitNoVScroll:       return "FitNoVScroll";
        case DesignRule::CarouselFallback:   return "CarouselFallback";
        case DesignRule::ScrollbarReserve:   return "ScrollbarReserve";
        case DesignRule::PageWhitespaceReview: return "PageWhitespaceReview";
        case DesignRule::PinnedActionsNeverInScroll: return "PinnedActionsNeverInScroll";
        case DesignRule::NoHorizontalOverflow: return "NoHorizontalOverflow";
        case DesignRule::AspectRatioBroken: return "AspectRatioBroken";
        case DesignRule::NoTerminalOverlap: return "NoTerminalOverlap";
    }
    return "?";
}

// Carousel page math (P18). Empty lists still have one page.
inline int CarouselPageCount(int N)
{
    return N <= 0 ? 1 : (N + CarouselRowsPerPage - 1) / CarouselRowsPerPage;
}

// Clamp a page index into [0, Pages-1]; Pages <= 0 yields 0.
inline int ClampCarouselPage(int Page, int Pages)
{
    if (Pages < 1) Pages = 1;
    return Page < 0 ? 0 : (Page >= Pages ? Pages - 1 : Page);
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
    bool bNoVScroll = false; // P17: clipped viewport content fits - no vertical scroll
    bool bCarousel = false;  // P18: fixed-height page viewport (page-flip, no vertical scroll)
    bool bCarouselNav = false; // P18/P19: prev/page/next strip that must follow its carousel
    bool bSection = false; // P9: first child must be a title
    bool bTitle = false;
    bool bAccordion = false; // P16: one-open-per-group collapsible section
    bool bPinnedAction = false; // P21: canonical pinned action (strip-only)
    bool bAspectRatio = false; // P23: fixed box must keep FaceAspectRatio (face canvas)
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
    auto NoV = [&](int i) { b.N[(size_t)i].bNoVScroll = true; };
    auto Car = [&](int i) { b.N[(size_t)i].bCarousel = true; };
    auto Nav = [&](int i) { b.N[(size_t)i].bCarouselNav = true; };
    auto Acc = [&](int i) { b.N[(size_t)i].bAccordion = true; };
    auto PK = [&](int i) { b.N[(size_t)i].bPinnedAction = true; };
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
        LF(b, "TB-NewPreset", 76, 22),
        LF(b, "TB-Save", 40, 22),
        LF(b, "TB-Undo", 40, 22),
        LF(b, "TB-Redo", 40, 22),
        LF(b, "TB-History", 56, 22),
        LF(b, "TB-Import", 90, 22),
        LF(b, "TB-Search", SearchBoxWidth, 22),
        LF(b, "TB-Spacer", 0, 0),
        LF(b, "TB-Help", 20, 22),
        LF(b, "TB-Spawn", 96, 22),
        LF(b, "TB-Find", 84, 22),
        LF(b, "TB-Quads", 84, 22),
        LF(b, "TB-ActorCombo", ActorComboWidth, 22),
        LF(b, "TB-ClearStale", 75, 22));
    S(Toolbar, ToolbarItemPad);
    P(Toolbar, ToolbarPadL, ToolbarPadV, ToolbarPadL, ToolbarPadV);
    Sp(b.N[(size_t)Toolbar].Children[7]);
    M(b.N[(size_t)Toolbar].Children[6], 6, 2, 6, 2);  // search box slot FMargin(6,2)
    // Toolbar clusters: 8px left margin opens each group (matches RebuildWidget).
    // Button widths above are tuned so the toolbar natural width stays 1089
    // (the design window width contract asserted by Phase H; Undo/Redo buttons
    // widen the contract from 1001; the P6 History menu button joins them and
    // the 14-button row is retuned −66 so the natural width returns to 1089).
    M(b.N[(size_t)Toolbar].Children[5], 8, 2, 2, 2);  // Import Art
    M(b.N[(size_t)Toolbar].Children[8], 8, 2, 2, 2);  // Help
    M(b.N[(size_t)Toolbar].Children[9], 8, 2, 2, 2);  // Spawn Preview
    M(b.N[(size_t)Toolbar].Children[13], 8, 2, 2, 2); // Clear Stale

    // ========================= 2. VIEW STATE STRIP =========================
    const int StateStrip = HF(b, "StateStrip",
        LF(b, "ST-Tab0", 44, 20), LF(b, "ST-Tab1", 44, 20),
        LF(b, "ST-Tab2", 44, 20), LF(b, "ST-Tab3", 44, 20),
        LF(b, "ST-Tab4", 44, 20), LF(b, "ST-Tab5", 44, 20),
        LF(b, "ST-Tab6", 44, 20), LF(b, "ST-Tab7", 44, 20),
        LF(b, "ST-Tab8", 44, 20), LF(b, "ST-Tab9", 44, 20));
    S(StateStrip, StateTabPad);
    P(StateStrip, 2, 3, 2, 3);
    b.N[(size_t)StateStrip].FixedH = StateStripHeight;

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
        // --- 3a. CENTER COLUMN ---
        // P5: the 5-way preview selector is the primary canvas control (large
        // labeled segmented buttons); onion-skin/show-pins/depth-overlay/
        // filter demote into the collapsed Canvas Options overflow menu.
        VF(b, "CENTER",
            HF(b, "CN-ModeRow",
                LF(b, "CN-Mode0", 76, 24), LF(b, "CN-Mode1", 76, 24),
                LF(b, "CN-Mode2", 76, 24), LF(b, "CN-Mode3", 76, 24),
                LF(b, "CN-Mode4", 76, 24),
                HF(b, "CN-OptsRow",
                    LF(b, "CN-OptsChk", 20, 20),
                    LF(b, "CN-OptsSlider", 0, 0),
                    LF(b, "CN-OptsLbl", 44, 10)),
                LF(b, "CN-Spacer", 0, 0)),
            LF(b, "CN-FilterRow", 420, 28),
            OV(b, "CN-Preview",
                LF(b, "PV-Image", 0, 0),
                LF(b, "PV-Outline", 0, 0),
                LF(b, "PV-Onion", 0, 0),
                LF(b, "PV-Edge", 0, 0),
                LF(b, "PV-Gizmo", 0, 0)),
            LF(b, "CN-Legend", 300, 16),
            LF(b, "CN-EdgeLegend", 420, 18),
            LF(b, "CN-PartsStrip", 420, 26),
            LF(b, "CN-LayerLabel", 200, 20)),
        // --- 3b. CONTEXT PANEL (621px) ---
        // W1: the old 5-rail switcher (RAIL-Switcher / RL-*) + the 340px props
        // pane (PROPS) merge into ONE 621px context panel switched by the
        // CT-TabRow above (4 task pages + the closed-by-default Developer
        // drawer). The width derives from the edge-schematic section's empty
        // space (P24): ContextPanelWidth = MainRowWidth - CenterColumnMinWidth
        // = 1089 - 468 = 621 = old RailWidth 273 + PropsWidth 340 +
        // PropsRightGap 8. Fixed - no internal splitter; resizing happens only
        // at the very outside of the widget.
        VF(b, "CP-ContextPanel",
            OV(b, "CP-Switcher",
                // ---- Page 0 "Assign": selected-layer props + layer list +
                // pins + Import / Outline->Depth / Bulk Assign / Assign Ops.
                VF(b, "CP-P0-Assign",
                    VF(b, "Sec-SelectedLayer",
                        LF(b, "SL-Header", 120, 14),
                        VF(b, "Sec-SL-Body",
                            HF(b, "SL-ThumbRow",
                                VF(b, "SL-ThumbCol0",
                                    LF(b, "SL-Thumb0", ThumbSize, ThumbSize),
                                    LF(b, "SL-Pick0", 70, 18), LF(b, "SL-Clear0", 70, 18), LF(b, "SL-Status0", ThumbSize, 10)),
                                VF(b, "SL-ThumbCol1",
                                    LF(b, "SL-Thumb1", ThumbSize, ThumbSize),
                                    LF(b, "SL-Pick1", 70, 18), LF(b, "SL-Clear1", 70, 18), LF(b, "SL-Status1", ThumbSize, 10)),
                                VF(b, "SL-ThumbCol2",
                                    LF(b, "SL-Thumb2", ThumbSize, ThumbSize),
                                    LF(b, "SL-Pick2", 70, 18), LF(b, "SL-Clear2", 70, 18), LF(b, "SL-Status2", ThumbSize, 10))),
                            HF(b, "SL-ActRow",
                                LF(b, "SL-AutoFit", 70, 20), LF(b, "SL-Reset", 49, 20),
                                LF(b, "SL-SyncTexAll", 105, 20), LF(b, "SL-AFCheck", 20, 20)),
                            LF(b, "SL-Status", 220, 12))),
                    VF(b, "Sec-Layers",
                        LF(b, "Sec-Layers-Title", 120, 14),
                        VF(b, "Sec-Layers-Body",
                            LF(b, "SL-Carousel", 0, 0),
                            LF(b, "SL-CarouselNav", 120, 22),
                            LF(b, "SL-AddLayerBtn", 70, 20),
                            VF(b, "Sec-Pins",
                                LF(b, "Sec-PI-Title", 120, 14),
                                VF(b, "Sec-PI-Body",
                                    HF(b, "PI-AddRow",
                                        LF(b, "PI-AddBtn", 119, 20),
                                        LF(b, "PI-Spacer", 0, 0)),
                                    LF(b, "PI-List", 0, 0))))),
                    VF(b, "Sec-Import",
                        LF(b, "Sec-IM-Title", 120, 14),
                        VF(b, "Sec-IM-Body",
                            HF(b, "IM-Row",
                                LF(b, "IM-ImportBtn", 60, 20), LF(b, "IM-DropBtn", 76, 20), LF(b, "IM-Spacer", 0, 0)))),
                    VF(b, "Sec-OutlineDepth",
                        LF(b, "Sec-OD-Title", 120, 14),
                        VF(b, "Sec-OD-Body",
                            HF(b, "OD-Row",
                                LF(b, "OD-GenBtn", 60, 20), LF(b, "OD-GridEdit", 40, 20), LF(b, "OD-DetectBtn", 52, 20),
                                LF(b, "OD-Spacer", 0, 0)),
                            HF(b, "OD-ChkRow",
                                LF(b, "OD-Chk", 16, 20), LF(b, "OD-OverlayLbl", 40, 10), LF(b, "OD-Spacer1", 0, 0)),
                            HF(b, "OD-ScopeRow",
                                LF(b, "OD-BakeLbl", 22, 10),
                                LF(b, "OD-Scope0", 16, 20), LF(b, "OD-FrontLbl", 30, 10),
                                LF(b, "OD-Scope1", 16, 20), LF(b, "OD-H8Lbl", 18, 10),
                                LF(b, "OD-Scope2", 16, 20), LF(b, "OD-AllLbl", 18, 10),
                                LF(b, "OD-Spacer2", 0, 0)),
                            LF(b, "OD-Stats", 156, 10))),
                    VF(b, "Sec-AssignGrid",
                        LF(b, "Sec-AG-Title", 120, 14),
                        VF(b, "Sec-AG-Body",
                            GRID(b, "AG-Grid",
                                LF(b, "AG-H0", 16, 14), LF(b, "AG-H1", 16, 14),
                                LF(b, "AG-H2", 16, 14), LF(b, "AG-H3", 16, 14),
                                LF(b, "AG-H4", 16, 14), LF(b, "AG-H5", 16, 14),
                                LF(b, "AG-H6", 16, 14), LF(b, "AG-H7", 16, 14),
                                LF(b, "AG-H8", 16, 14), LF(b, "AG-H9", 16, 14),
                                LF(b, "AG-C00", 16, 16), LF(b, "AG-C01", 16, 16),
                                LF(b, "AG-C02", 16, 16), LF(b, "AG-C03", 16, 16),
                                LF(b, "AG-C04", 16, 16), LF(b, "AG-C05", 16, 16),
                                LF(b, "AG-C06", 16, 16), LF(b, "AG-C07", 16, 16),
                                LF(b, "AG-C08", 16, 16), LF(b, "AG-C09", 16, 16),
                                LF(b, "AG-C10", 16, 16), LF(b, "AG-C11", 16, 16),
                                LF(b, "AG-C12", 16, 16), LF(b, "AG-C13", 16, 16),
                                LF(b, "AG-C14", 16, 16), LF(b, "AG-C15", 16, 16),
                                LF(b, "AG-C16", 16, 16), LF(b, "AG-C17", 16, 16),
                                LF(b, "AG-C18", 16, 16), LF(b, "AG-C19", 16, 16),
                                LF(b, "AG-C20", 16, 16), LF(b, "AG-C21", 16, 16),
                                LF(b, "AG-C22", 16, 16), LF(b, "AG-C23", 16, 16),
                                LF(b, "AG-C24", 16, 16), LF(b, "AG-C25", 16, 16),
                                LF(b, "AG-C26", 16, 16), LF(b, "AG-C27", 16, 16),
                                LF(b, "AG-C28", 16, 16), LF(b, "AG-C29", 16, 16)),
                            HF(b, "AG-RowL",
                                LF(b, "AG-Lbl0", 44, 14), LF(b, "AG-Lbl1", 44, 14), LF(b, "AG-Lbl2", 44, 14),
                                LF(b, "AG-Spacer", 0, 0)),
                            LF(b, "AG-Coverage", 140, 14))),
                    VF(b, "Sec-AssignOps",
                        LF(b, "Sec-AO-Title", 120, 14),
                        VF(b, "Sec-AO-Body",
                            HF(b, "AO-Row0",
                                LF(b, "AO-ClearRow", 64, 20),
                                LF(b, "AO-Spacer", 0, 0)),
                            HF(b, "AO-Row1",
                                LF(b, "AO-PerfLbl", 20, 14),
                                LF(b, "AO-PerfCombo", 50, 20),
                                LF(b, "AO-CamLbl", 20, 14),
                                LF(b, "AO-CamCombo", 50, 20))))),
                // ---- Page 1 "Transform & Sync": the ONE Copy/Sync panel
                // (op selector, dest grid, Apply, Copy-from, Fill, Link).
                VF(b, "CP-P1-Transform",
                    VF(b, "Sec-Transform",
                        LF(b, "Sec-XF-Title", 140, 14),
                        VF(b, "Sec-XF-Body",
                            HF(b, "XF-VORow",
                                LF(b, "XF-VOCheck", 20, 20), LF(b, "XF-VOLbl", 96, 14),
                                LF(b, "XF-VOSpacer", 0, 0), LF(b, "XF-VOClearBtn", 119, 20)),
                            HF(b, "XF-PosXRow", LF(b, "XF-PosXLbl", 44, 14), LF(b, "XF-PosXEdit", 70, 20)),
                            HF(b, "XF-PosYRow", LF(b, "XF-PosYLbl", 44, 14), LF(b, "XF-PosYEdit", 70, 20)),
                            HF(b, "XF-ScaleXRow", LF(b, "XF-ScaleXLbl", 56, 14), LF(b, "XF-ScaleXEdit", 70, 20)),
                            HF(b, "XF-ScaleYRow", LF(b, "XF-ScaleYLbl", 56, 14), LF(b, "XF-ScaleYEdit", 70, 20)),
                            HF(b, "XF-RotRow", LF(b, "XF-RotLbl", 28, 14), LF(b, "XF-RotEdit", 70, 20)))),
                    VF(b, "Sec-SyncAlign",
                        LF(b, "Sec-SA-Title", 140, 14),
                        VF(b, "Sec-SA-Body",
                            HF(b, "SA-OpRow",
                                LF(b, "SA-OpLbl", 72, 14), LF(b, "SA-OpTr", 48, 20),
                                LF(b, "SA-OpTex", 48, 20), LF(b, "SA-OpBoth", 40, 20),
                                LF(b, "SA-OpSpacer", 0, 0)),
                            HF(b, "SA-Hdr",
                                LF(b, "SA-HdrLbl", 104, 14), LF(b, "SA-DstClear", 56, 16),
                                LF(b, "SA-HdrSpacer", 0, 0)),
                            HF(b, "SA-Dst0",
                                LF(b, "SA-D0C0", 18, 16), LF(b, "SA-D0L0", 30, 12),
                                LF(b, "SA-D0C1", 18, 16), LF(b, "SA-D0L1", 30, 12),
                                LF(b, "SA-D0C2", 18, 16), LF(b, "SA-D0L2", 30, 12),
                                LF(b, "SA-D0C3", 18, 16), LF(b, "SA-D0L3", 30, 12),
                                LF(b, "SA-D0C4", 18, 16), LF(b, "SA-D0L4", 30, 12)),
                            HF(b, "SA-Dst1",
                                LF(b, "SA-D1C0", 18, 16), LF(b, "SA-D1L0", 30, 12),
                                LF(b, "SA-D1C1", 18, 16), LF(b, "SA-D1L1", 30, 12),
                                LF(b, "SA-D1C2", 18, 16), LF(b, "SA-D1L2", 30, 12),
                                LF(b, "SA-D1C3", 18, 16), LF(b, "SA-D1L3", 30, 12),
                                LF(b, "SA-D1C4", 18, 16), LF(b, "SA-D1L4", 30, 12)),
                            HF(b, "SA-ActRow",
                                LF(b, "SA-ActPicked", 84, 20), LF(b, "SA-ActAll", 84, 20),
                                LF(b, "SA-ActSpacer", 0, 0)),
                            HF(b, "SA-CpyRow",
                                LF(b, "SA-CopyLbl", 30, 14), LF(b, "SA-CopyCombo", 60, 20),
                                LF(b, "SA-CopyBtn", 40, 20), LF(b, "SA-FillBtn", 70, 20),
                                LF(b, "SA-CopySpacer", 0, 0)),
                            HF(b, "SA-LnkRow",
                                LF(b, "SA-LinkChk", 18, 16), LF(b, "SA-LinkLbl", 86, 12),
                                LF(b, "SA-DriftLbl", 56, 12), LF(b, "SA-LinkSpacer", 0, 0))))),
                // ---- Page 2 "Expression/Blink/Viseme": nested pins + viseme
                // grid + hull review (P6: Animated Variants folded in).
                VF(b, "CP-P2-Expression",
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
                            LF(b, "NP-Outliner", 156, 60))),
                    VF(b, "Sec-VisemeGrid",
                        LF(b, "Sec-VG-Title", 120, 14),
                        VF(b, "Sec-VG-Body",
                            HF(b, "VG-Row0",
                                LF(b, "VG-C0", 15, 15), LF(b, "VG-C1", 15, 15), LF(b, "VG-C2", 15, 15),
                                LF(b, "VG-C3", 15, 15), LF(b, "VG-C4", 15, 15), LF(b, "VG-C5", 15, 15),
                                LF(b, "VG-C6", 15, 15), LF(b, "VG-C7", 15, 15), LF(b, "VG-C8", 15, 15),
                                LF(b, "VG-C9", 15, 15)),
                            HF(b, "VG-Row1",
                                LF(b, "VG-D0", 15, 15), LF(b, "VG-D1", 15, 15), LF(b, "VG-D2", 15, 15),
                                LF(b, "VG-D3", 15, 15), LF(b, "VG-D4", 15, 15), LF(b, "VG-D5", 15, 15),
                                LF(b, "VG-D6", 15, 15), LF(b, "VG-D7", 15, 15), LF(b, "VG-D8", 15, 15),
                                LF(b, "VG-D9", 15, 15)))),
                    VF(b, "Sec-HullReview",
                        LF(b, "Sec-HR-Title", 120, 14),
                        VF(b, "Sec-HR-Body",
                            HF(b, "HR-OrbitRow",
                                LF(b, "HR-Chk", 20, 20), LF(b, "HR-OrbitLbl", 56, 14),
                                LF(b, "HR-Slider", 0, 0), LF(b, "HR-SpdLbl", 24, 10), LF(b, "HR-SnapBtn", 42, 20)),
                            GRID(b, "HR-Thumbs",
                                LF(b, "HT-0", 28, 48), LF(b, "HT-1", 28, 48), LF(b, "HT-2", 28, 48),
                                LF(b, "HT-3", 28, 48), LF(b, "HT-4", 28, 48), LF(b, "HT-5", 28, 48),
                                LF(b, "HT-6", 28, 48), LF(b, "HT-7", 28, 48), LF(b, "HT-8", 28, 48),
                                LF(b, "HT-9", 28, 48))))),
                // ---- Page 3 "Preview & Debug": camera follow + camera +
                // blend preview + edge analysis + depth debug.
                VF(b, "CP-P3-Preview",
                    VF(b, "Sec-CameraFollow",
                        LF(b, "Sec-CF-Title", 120, 14),
                        VF(b, "Sec-CF-Body",
                            HF(b, "CF-Row",
                                LF(b, "CF-Chk", 16, 20), LF(b, "CF-Lbl", 56, 14), LF(b, "CF-Spacer", 0, 0), LF(b, "CF-SnapBtn", 72, 20)))),
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
                                LF(b, "CM-ZoneLbl", 30, 14),
                                LF(b, "CM-Zone0", 28, 20), LF(b, "CM-Zone1", 28, 20),
                                LF(b, "CM-Zone2", 28, 20), LF(b, "CM-Zone3", 28, 20)))),
                    VF(b, "Sec-BlendPreview",
                        LF(b, "Sec-BP-Title", 120, 14),
                        VF(b, "Sec-BP-Body",
                            HF(b, "BP-Row",
                                LF(b, "BP-Chk", 20, 20), LF(b, "BP-Lbl", 40, 14), LF(b, "BP-Slider", 0, 0), LF(b, "BP-Val", 40, 10)))),
                    VF(b, "Sec-EdgeAnalysis",
                        LF(b, "Sec-EA-Title", 120, 14),
                        VF(b, "Sec-EA-Body",
                            HF(b, "EA-Row",
                                LF(b, "EA-Chk", 14, 20), LF(b, "EA-EdgeLbl", 40, 14),
                                LF(b, "EA-Chk2", 14, 20), LF(b, "EA-HistLbl", 24, 14),
                                LF(b, "EA-Rebuild", 46, 20), LF(b, "EA-Spacer", 0, 0)))),
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
                                LF(b, "DD-RebuildBtn", 72, 20), LF(b, "DD-ColorBtn", 78, 20), LF(b, "DD-Spacer2", 0, 0))))),
                // ---- Page 4 "Developer" drawer (closed by default): review
                // tools, config, problems + the overview matrices (Status
                // Detail / All Layers moved out of the old View & Layer rail).
                VF(b, "CP-DevDrawer",
                    VF(b, "Sec-TagValidator",
                        LF(b, "Sec-TV-Title", 120, 14),
                        VF(b, "Sec-TV-Body",
                            LF(b, "TV-Report", 152, 20))),
                    VF(b, "Sec-MatCrossRef",
                        LF(b, "Sec-MC-Title", 120, 14),
                        VF(b, "Sec-MC-Body",
                            LF(b, "MC-Report", 152, 20))),
                    VF(b, "Sec-ParamRef",
                        LF(b, "Sec-PRF-Title", 120, 14),
                        VF(b, "Sec-PRF-Body",
                            HF(b, "PRF-Row",
                                LF(b, "PRF-Edit", 90, 20), LF(b, "PRF-FindBtn", 56, 20)),
                            LF(b, "PRF-Results", 152, 32))),
                    VF(b, "Sec-Config",
                        LF(b, "Sec-CFG-Title", 120, 14),
                        VF(b, "Sec-CFG-Body",
                            HF(b, "CFG-R0", LF(b, "CFG-Chk0", 20, 20), LF(b, "CFG-Lbl0", 96, 14)),
                            HF(b, "CFG-R1", LF(b, "CFG-Chk1", 20, 20), LF(b, "CFG-Lbl1", 96, 14)),
                            HF(b, "CFG-R2", LF(b, "CFG-Chk2", 20, 20), LF(b, "CFG-Lbl2", 96, 14)),
                            HF(b, "CFG-R3", LF(b, "CFG-Chk3", 20, 20), LF(b, "CFG-Lbl3", 96, 14)))),
                    VF(b, "Sec-ParamTable",
                        LF(b, "Sec-PT-Title", 120, 14),
                        VF(b, "Sec-PT-Body",
                            HF(b, "PT-AddRow",
                                LF(b, "PT-Edit", 90, 20), LF(b, "PT-AddBtn", 56, 20)),
                            VF(b, "PT-Rows",
                                LF(b, "PT-Row0", 156, 16), LF(b, "PT-Row1", 156, 16), LF(b, "PT-Row2", 156, 16)))),
                    VF(b, "Sec-Problems",
                        LF(b, "Sec-PR-Title", 120, 14),
                        VF(b, "Sec-PR-Body",
                            LF(b, "PB-Carousel", 0, 0),
                            LF(b, "PB-CarouselNav", 120, 22))),
                    VF(b, "Sec-StatusDetail",
                        LF(b, "Sec-StatusDetail-Title", 120, 14),
                        VF(b, "Sec-StatusDetail-Body",
                            LF(b, "SD-Carousel", 0, 0),
                            LF(b, "SD-CarouselNav", 120, 22))),
                    VF(b, "Sec-AllLayers",
                        LF(b, "Sec-AL-Title", 120, 14),
                        VF(b, "Sec-AL-Body",
                            LF(b, "AL-Carousel", 0, 0),
                            LF(b, "AL-CarouselNav", 120, 22)))))));

    // MainRow fixed height (real: SBox HeightOverride(MainRowHeight)); stretches
    // to root width (real: root SVerticalBox slot fills the window, CenterCol
    // FillWidth(1.0)). P24 keeps every column + center row inside this band so
    // nothing slides under the timeline / terminal output window below.
    b.N[(size_t)MainRow].FixedH = MainRowHeight;
    FxW(MainRow);

    // --- Context panel config (fixed 621x800 context pages, no splitter) ---
    // W1: CP-ContextPanel is MainRow child 1 (the CENTER column is child 0,
    // the flexible FillWidth column). Every page is a fixed 621x800 clipped
    // bNoVScroll stack; accordion sections collapse (P17), carousels page
    // (P18) inside the band, so nothing slides under the terminal (P24).
    {
        const int CtxPanel = b.N[(size_t)MainRow].Children[1];
        b.N[(size_t)CtxPanel].FixedW = ContextPanelWidth;
        FxH(CtxPanel);
        const int Sw = b.N[(size_t)CtxPanel].Children[0];
        Fx(Sw);
        for (int Pi = 0; Pi < (int)b.N[(size_t)Sw].Children.size(); ++Pi)
        {
            const int Page = b.N[(size_t)Sw].Children[(size_t)Pi];
            S(Page, 2);
            Clip(Page);
            NoV(Page);
            b.N[(size_t)Page].FixedH = MainRowHeight;
            b.N[(size_t)Page].FixedW = ContextPanelWidth;
        }
        // ---- Assign page (P0) ----
        {
            const int P0 = b.N[(size_t)Sw].Children[0];
            // Sec-SelectedLayer (child 0): props header / thumbs / actions.
            {
                const int Sel = b.N[(size_t)P0].Children[0];
                SecSetup(Sel, 2);
                P(Bod(Sel), SectionBorderPad, SectionBorderPad, SectionBorderPad, SectionBorderPad);
                {
                    const int Thr = b.N[(size_t)Bod(Sel)].Children[0];
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
                    const int Act = b.N[(size_t)Bod(Sel)].Children[1];
                    S(Act, 2);
                    M(Act, 2, 2, 2, 2);
                }
            }
            // Sec-Layers (child 1): paged layer carousel + add button + Pins.
            {
                const int Layers = b.N[(size_t)P0].Children[1];
                SecSetup(Layers, 2);
                P(Bod(Layers), SectionBorderPad, SectionBorderPad, SectionBorderPad, SectionBorderPad);
                {
                    const int LBody = Bod(Layers);
                    const int LCar = b.N[(size_t)LBody].Children[0];
                    FxW(LCar);
                    b.N[(size_t)LCar].FixedH = CarouselViewportH;
                    Car(LCar);
                    P(LCar, 0, 0, 0, ScrollReserveBottom);
                    const int LNav = b.N[(size_t)LBody].Children[1];
                    b.N[(size_t)LNav].FixedH = CarouselNavHeight;
                    Nav(LNav);
                    M(b.N[(size_t)LBody].Children[2], 4, 2, 4, 2);
                    // P7-C: Sec-Pins folds under the Layers section (named pins
                    // list accordion, collapsed while the layer has no pins).
                    const int Pins = b.N[(size_t)LBody].Children[3];
                    Acc(Pins);
                    SecSetup(Pins, 2);
                    P(Bod(Pins), SectionBorderPad, SectionBorderPad, SectionBorderPad, SectionBorderPad);
                    {
                        const int PBody = Bod(Pins);
                        const int AddRow = b.N[(size_t)PBody].Children[0];
                        Sp(b.N[(size_t)AddRow].Children[1]);   // PI-Spacer absorbs the Add row's trailing width
                        const int PList = b.N[(size_t)PBody].Children[1];
                        FxW(PList);
                        b.N[(size_t)PList].FixedH = 100;       // 5 pin rows; accordion collapse keeps the page fitting (P17)
                    }
                }
            }
            // Sec-Import (child 2): accordion.
            {
                const int Im = b.N[(size_t)P0].Children[2];
                SecSetup(Im, 2);
                Acc(Im);
                {
                    const int IM0 = b.N[(size_t)Bod(Im)].Children[0];
                    S(IM0, 4);
                    Sp(b.N[(size_t)IM0].Children[2]);
                }
            }
            // Sec-OutlineDepth (child 3): accordion.
            {
                const int OD = b.N[(size_t)P0].Children[3];
                SecSetup(OD, 2);
                Acc(OD);
                {
                    const int OD1 = b.N[(size_t)Bod(OD)].Children[0];
                    S(OD1, 2);
                    Sp(b.N[(size_t)OD1].Children[3]);
                    const int ODChk = b.N[(size_t)Bod(OD)].Children[1];
                    S(ODChk, 4);
                    Sp(b.N[(size_t)ODChk].Children[2]);
                    const int ScR = b.N[(size_t)Bod(OD)].Children[2];
                    S(ScR, 1);
                    Sp(b.N[(size_t)ScR].Children[7]);
                    M(b.N[(size_t)ScR].Children[2], 2, 2, 2, 2);
                    M(b.N[(size_t)ScR].Children[4], 2, 2, 2, 2);
                    M(b.N[(size_t)ScR].Children[6], 2, 2, 2, 2);
                }
            }
            // Sec-AssignGrid (child 4): plain grid + row labels + coverage.
            {
                const int AG = b.N[(size_t)P0].Children[4];
                SecSetup(AG, 0);
                {
                    const int GB = Bod(AG);
                    const int Grid = b.N[(size_t)GB].Children[0];
                    // 10 state columns (H0..H9 header + C00..C29 = 3 layer rows).
                    for (int c = 0; c < (int)b.N[(size_t)Grid].Children.size(); ++c)
                        GP(b.N[(size_t)Grid].Children[(size_t)c], c % 10, c / 10);
                    const int RowL = b.N[(size_t)GB].Children[1];
                    S(RowL, 4);
                    Sp(b.N[(size_t)RowL].Children[3]);
                }
            }
            // Sec-AssignOps (child 5): accordion.
            {
                const int AO = b.N[(size_t)P0].Children[5];
                SecSetup(AO, 2);
                Acc(AO);
                {
                    const int AORow0 = b.N[(size_t)Bod(AO)].Children[0];
                    S(AORow0, 4);
                    Sp(b.N[(size_t)AORow0].Children[1]);
                    const int AORow1 = b.N[(size_t)Bod(AO)].Children[1];
                    S(AORow1, 4);
                }
            }
        }
        // ---- Transform & Sync page (P1) ----
        {
            const int P1 = b.N[(size_t)Sw].Children[1];
            {
                const int XF = b.N[(size_t)P1].Children[0];
                SecSetup(XF, 2);
                Acc(XF);
                for (int c = 0; c < (int)b.N[(size_t)Bod(XF)].Children.size(); ++c)
                {
                    const int row = b.N[(size_t)Bod(XF)].Children[(size_t)c];
                    S(row, 4);
                    if (c == 0)
                        Sp(b.N[(size_t)row].Children[2]);
                    else
                        M(b.N[(size_t)row].Children[1], 4, 0, 4, 0);
                }
            }
            {
                const int SA = b.N[(size_t)P1].Children[1];
                SecSetup(SA, 2);
                Acc(SA);
                {
                    const int OpR = b.N[(size_t)Bod(SA)].Children[0];
                    S(OpR, 4);
                    Sp(b.N[(size_t)OpR].Children[4]);
                    const int Hdr = b.N[(size_t)Bod(SA)].Children[1];
                    S(Hdr, 4);
                    Sp(b.N[(size_t)Hdr].Children[2]);
                    const int Dst0 = b.N[(size_t)Bod(SA)].Children[2];
                    S(Dst0, 2);
                    const int Dst1 = b.N[(size_t)Bod(SA)].Children[3];
                    S(Dst1, 2);
                    const int ActR = b.N[(size_t)Bod(SA)].Children[4];
                    S(ActR, 4);
                    Sp(b.N[(size_t)ActR].Children[2]);
                    const int CpyR = b.N[(size_t)Bod(SA)].Children[5];
                    S(CpyR, 4);
                    Sp(b.N[(size_t)CpyR].Children[4]);
                    const int LnkR = b.N[(size_t)Bod(SA)].Children[6];
                    S(LnkR, 4);
                    Sp(b.N[(size_t)LnkR].Children[3]);
                }
            }
        }
        // ---- Expression/Blink/Viseme page (P2) ----
        {
            const int P2 = b.N[(size_t)Sw].Children[2];
            {
                const int NP = b.N[(size_t)P2].Children[0];
                SecSetup(NP, 2);
                Acc(NP);
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
            {
                const int VG = b.N[(size_t)P2].Children[1];
                SecSetup(VG, 2);
                Acc(VG);
                for (int c = 0; c < (int)b.N[(size_t)Bod(VG)].Children.size(); ++c)
                    S(b.N[(size_t)Bod(VG)].Children[(size_t)c], 1);
            }
            {
                const int HR = b.N[(size_t)P2].Children[2];
                SecSetup(HR, 2);
                Acc(HR);
                {
                    const int Orb = b.N[(size_t)Bod(HR)].Children[0];
                    S(Orb, 2);
                    FxW(b.N[(size_t)Orb].Children[2]);
                    M(b.N[(size_t)Orb].Children[4], 4, 0, 0, 0);
                    const int Th = b.N[(size_t)Bod(HR)].Children[1];
                    S(Th, 2);
                    b.N[(size_t)Th].FixedH = 98;
                    for (int c = 0; c < (int)b.N[(size_t)Th].Children.size(); ++c)
                        GP(b.N[(size_t)Th].Children[(size_t)c], c % 5, c / 5);
                }
            }
        }
        // ---- Preview & Debug page (P3) ----
        {
            const int P3 = b.N[(size_t)Sw].Children[3];
            {
                const int CF = b.N[(size_t)P3].Children[0];
                SecSetup(CF, 2);
                {
                    const int CFR = b.N[(size_t)Bod(CF)].Children[0];
                    S(CFR, 4);
                    Sp(b.N[(size_t)CFR].Children[2]);
                }
            }
            {
                const int Cam = b.N[(size_t)P3].Children[1];
                SecSetup(Cam, 2);
                {
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
                    }
                }
            }
            {
                const int BP = b.N[(size_t)P3].Children[2];
                SecSetup(BP, 2);
                {
                    const int BPR = b.N[(size_t)Bod(BP)].Children[0];
                    S(BPR, 4);
                    FxW(b.N[(size_t)BPR].Children[2]);
                }
            }
            {
                const int EA = b.N[(size_t)P3].Children[3];
                SecSetup(EA, 2);
                Acc(EA);
                {
                    const int EAR = b.N[(size_t)Bod(EA)].Children[0];
                    S(EAR, 4);
                    Sp(b.N[(size_t)EAR].Children[5]);
                }
            }
            {
                const int DD = b.N[(size_t)P3].Children[4];
                SecSetup(DD, 2);
                Acc(DD);
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
            }
        }
        // ---- Developer drawer (P4, closed by default) ----
        {
            const int Dev = b.N[(size_t)Sw].Children[4];
            for (int c = 0; c < (int)b.N[(size_t)Dev].Children.size(); ++c)
                Acc(b.N[(size_t)Dev].Children[(size_t)c]);
            {
                const int TV = b.N[(size_t)Dev].Children[0];
                SecSetup(TV, 2);
                M(b.N[(size_t)Bod(TV)].Children[0], 2, 1, 2, 1);
            }
            {
                const int MC = b.N[(size_t)Dev].Children[1];
                SecSetup(MC, 2);
                M(b.N[(size_t)Bod(MC)].Children[0], 2, 1, 2, 1);
            }
            {
                const int PRF = b.N[(size_t)Dev].Children[2];
                SecSetup(PRF, 2);
                S(b.N[(size_t)Bod(PRF)].Children[0], 4);
                M(b.N[(size_t)Bod(PRF)].Children[1], 2, 2, 2, 2);
            }
            {
                const int Cfg = b.N[(size_t)Dev].Children[3];
                SecSetup(Cfg, 2);
                for (int c = 0; c < (int)b.N[(size_t)Bod(Cfg)].Children.size(); ++c)
                    S(b.N[(size_t)Bod(Cfg)].Children[(size_t)c], 4);
            }
            {
                const int PT = b.N[(size_t)Dev].Children[4];
                SecSetup(PT, 2);
                S(b.N[(size_t)Bod(PT)].Children[0], 4);
                S(b.N[(size_t)Bod(PT)].Children[1], 1);
            }
            // Problems / Status Detail / All Layers are paged carousels (P18)
            // inside accordion sections (P16/P17: collapsed headers keep the
            // drawer inside 800px; opening one pages its rows).
            {
                const int Prob = b.N[(size_t)Dev].Children[5];
                SecSetup(Prob, 0);
                {
                    const int PBody = Bod(Prob);
                    const int PCar = b.N[(size_t)PBody].Children[0];
                    FxW(PCar);
                    b.N[(size_t)PCar].FixedH = CarouselViewportH;
                    Car(PCar);
                    P(PCar, 0, 0, 0, ScrollReserveBottom);
                    const int PNav = b.N[(size_t)PBody].Children[1];
                    b.N[(size_t)PNav].FixedH = CarouselNavHeight;
                    Nav(PNav);
                }
            }
            {
                const int SD = b.N[(size_t)Dev].Children[6];
                SecSetup(SD, 0);
                {
                    const int SDBody = Bod(SD);
                    const int SDCar = b.N[(size_t)SDBody].Children[0];
                    FxW(SDCar);
                    b.N[(size_t)SDCar].FixedH = CarouselViewportH;
                    Car(SDCar);
                    P(SDCar, 0, 0, 0, ScrollReserveBottom);
                    const int SDNav = b.N[(size_t)SDBody].Children[1];
                    b.N[(size_t)SDNav].FixedH = CarouselNavHeight;
                    Nav(SDNav);
                }
            }
            {
                const int AL = b.N[(size_t)Dev].Children[7];
                SecSetup(AL, 0);
                {
                    const int ABody = Bod(AL);
                    const int ACar = b.N[(size_t)ABody].Children[0];
                    FxW(ACar);
                    b.N[(size_t)ACar].FixedH = CarouselViewportH;
                    Car(ACar);
                    P(ACar, 0, 0, 0, ScrollReserveBottom);
                    const int ANav = b.N[(size_t)ABody].Children[1];
                    b.N[(size_t)ANav].FixedH = CarouselNavHeight;
                    Nav(ANav);
                }
            }
        }
    }

    // --- Center column config ---
    {
        const int Center = b.N[(size_t)MainRow].Children[0];
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
            const int Prev = b.N[(size_t)Center].Children[2];
            b.N[(size_t)Prev].FixedH = PreviewCanvasHeight;
            b.N[(size_t)Prev].FixedW = FaceCanvasWidth;
            b.N[(size_t)Prev].bAspectRatio = true;   // P23: square canvas - the face is never stretched
            M(Prev, 2, 2, 2, 0);
            for (int c = 0; c < (int)b.N[(size_t)Prev].Children.size(); ++c)
                Fx(b.N[(size_t)Prev].Children[(size_t)c]);
        }
        // Text under the schematic (mirrors the real center column): the
        // schematic filter row, the always-visible legend lines, the fixed-
        // height parts strip row and the layer label sit below the canvas.
        // P2/P12/P24 keep the whole column inside the MainRowHeight band so
        // nothing can overlap the timeline / terminal output window below
        // (the mirror rows make that overlap detectable - a taller canvas or
        // a wrapping parts strip fires).
        M(b.N[(size_t)Center].Children[2], 4, 2, 4, 0);   // CN-FilterRow
        M(b.N[(size_t)Center].Children[3], 4, 2, 4, 0);   // CN-Legend
        M(b.N[(size_t)Center].Children[4], 2, 2, 2, 0);   // CN-PartsStrip
        M(b.N[(size_t)Center].Children[5], 4, 2, 4, 0);   // CN-EdgeLegend
        M(b.N[(size_t)Center].Children[6], 4, 2, 4, 0);   // CN-LayerLabel
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
    {
        const int BotBar = b.N[(size_t)BotArea].Children[0];
        b.N[(size_t)BotBar].FixedH = BotBarHeight;
        P(BotBar, 4, 2, 4, 2);
        S(BotBar, 2);
        Sp(b.N[(size_t)BotBar].Children[6]);
        M(b.N[(size_t)BotBar].Children[7], 4, 2, 4, 2);
    }

    // ========================== 6. DIAGNOSTIC LOG ==========================
    const int DiagLog = LF(b, "DiagnosticLog", 0, DiagnosticLogHeight);
    Fx(DiagLog);

    // =================== PINNED ACTION STRIP (P21) ===================
    // Full-width row above the main row: the canonical quick actions live
    // HERE and only here. They are pinned (never inside a scroll viewport),
    // mirroring the widget strip built from FPLayout::QuickActionLabels().
    // P7-B: 3 actions (Sync All -> All removed; sync/copy consolidated in the
    // ONE Copy/Sync panel on the props pane's Sync + Align page, Phase 3).
    const int PinnedStrip = HF(b, "PinnedStrip",
        LF(b, "Import Art...", 97, 20),
        LF(b, "Auto-Fit All", 98, 20),
        LF(b, "Clear All Overrides", 147, 20),
        LF(b, "PS-Spacer", 0, 0));
    b.N[(size_t)PinnedStrip].FixedH = PinnedStripHeight;
    FxW(PinnedStrip);
    S(PinnedStrip, 2);
    Sp(b.N[(size_t)PinnedStrip].Children[3]);
    for (int c = 0; c < 3; ++c)
    {
        PK(b.N[(size_t)PinnedStrip].Children[(size_t)c]);
        M(b.N[(size_t)PinnedStrip].Children[(size_t)c], 2, 2, 2, 2);
    }

    // ====================== TOP-LEVEL TAB BAR (W1) ======================
    // Full-width labeled tab row above the main row: one button per user-facing
    // page, driving the context-panel switcher (SetActivePageIndex). W1: the
    // old 5-rail tab labels are replaced by the 4 task pages + the Developer
    // drawer; the manifest CT-TabRow row mirrors the widget's Root slot exactly
    // (SBox HeightOverride FPLayout::TabBarHeight).
    const int CTTabRow = HF(b, "CT-TabRow",
        LF(b, "CT-Tab0", 56, 22),       // Assign
        LF(b, "CT-Tab1", 118, 22),      // Transform & Sync
        LF(b, "CT-Tab2", 148, 22),      // Expression/Blink/Viseme
        LF(b, "CT-Tab3", 106, 22),      // Preview & Debug
        LF(b, "CT-DevTab", 84, 22),     // Developer (drawer, closed by default)
        LF(b, "CT-Spacer", 0, 0));
    b.N[(size_t)CTTabRow].FixedH = TabBarHeight;
    FxW(CTTabRow);
    S(CTTabRow, 2);
    Sp(b.N[(size_t)CTTabRow].Children[5]);
    for (int c = 0; c < 5; ++c)
        M(b.N[(size_t)CTTabRow].Children[(size_t)c], 2, 2, 2, 2);

    // =========================== ROOT ===========================
    const int Root = VF(b, "Root",
        Toolbar, StateStrip, ZoneDiagram, PinnedStrip, CTTabRow, MainRow, Timeline, FrameCounts, BotArea, DiagLog);
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

// ----------------------------------------------------------------------------
// P20 PageWhitespaceReview: per-tab whitespace review. For a carousel whose
// pages are sections, adjacent pages that fit inside the page viewport when
// merged (title of the first + all bodies + CarouselMergeSpacing gaps) must be
// combined; CarouselMinPages returns the minimum achievable page count using
// the exact same rect metrics as P17. Deterministic: pure function of the
// resolved layout - the tests assert the count for concrete fixtures.
// ----------------------------------------------------------------------------
inline int CarouselMinPages(const std::vector<FPLayoutNode>& Nodes, const FPLayoutNode* car)
{
    const size_t n = car ? car->Children.size() : 0u;
    if (n < 2) return (int)n;
    const int root = FindRootIndex(Nodes);
    if (root < 0) return (int)n;
    std::vector<FPBox> M(Nodes.size(), FPBox{});
    ComputeMetricsRec(Nodes, M, root);
    const std::vector<FPRect> R = ResolveLayout(Nodes);
    const double inner = car->FixedH - car->PadT - car->PadB;
    const double Eps = 1e-6;
    int pages = 0;
    size_t p = 0;
    while (p < n)
    {
        ++pages;
        const FPLayoutNode& first = Nodes[(size_t)car->Children[p]];
        if (!first.bSection || first.Children.size() < 2) { ++p; continue; }
        const int ft = first.Children[0];
        const int fb = first.Children[1];
        const FPLayoutNode& ftN = Nodes[(size_t)ft];
        const FPLayoutNode& fbN = Nodes[(size_t)fb];
        double h = R[(size_t)ft].H + ftN.MarginT + ftN.MarginB
                 + R[(size_t)fb].H + fbN.MarginT + fbN.MarginB;
        size_t q = p + 1;
        while (q < n)
        {
            const FPLayoutNode& nx = Nodes[(size_t)car->Children[q]];
            if (!nx.bSection || nx.Children.size() < 2) break;
            const int nb = nx.Children[1];
            const FPLayoutNode& nbN = Nodes[(size_t)nb];
            const double addH = R[(size_t)nb].H + nbN.MarginT + nbN.MarginB
                              + CarouselMergeSpacing;
            if (h + addH > inner + Eps) break;
            h += addH;
            ++q;
        }
        p = q;
    }
    return pages;
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
// Validator: enforces P1..P23 over the resolved tree.
// ----------------------------------------------------------------------------
inline const std::vector<std::string>& QuickActionLabels();
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

        // ---- P22 NoHorizontalOverflow: a clipped viewport with a fixed width
        // ---- (rail) must fit its content horizontally - a wider row would
        // ---- scroll left-to-right under neighboring panels (the rail
        // ---- horizontal-scroll defect). Flex/spacer children absorb the
        // ---- remaining width and can never overflow.
        if (n.bClipH && n.FixedW > 0.0 && !isLeaf)
        {
            const double availW = n.FixedW - n.PadL - n.PadR;
            for (int ci : n.Children)
            {
                const FPLayoutNode& cn = Nodes[(size_t)ci];
                if (cn.bSpacer || cn.bFlexW || cn.bClipH) continue;
                const double cw = M[(size_t)ci].W + cn.MarginL + cn.MarginR;
                if (cw > availW + Eps)
                    Out.push_back({ DesignRule::NoHorizontalOverflow, n.Name, cn.Name });
            }
        }

        // ---- P23 AspectRatioBroken: an aspect-locked node (bAspectRatio -
        // ---- the face schematic canvas) must keep FaceAspectRatio in its
        // ---- resolved rect, so the face is never stretched.
        if (n.bAspectRatio && r.W > Eps && r.H > Eps)
        {
            const double Ratio = r.W / r.H;
            if (std::abs(Ratio - FaceAspectRatio) > 0.02)
                Out.push_back({ DesignRule::AspectRatioBroken, n.Name,
                    "canvas aspect ratio broken (face stretched)" });
        }

        // ---- P17 FitNoVScroll: a bNoVScroll viewport must fit without a
        // ---- vertical scroll bar. Accordion children collapse to their
        // ---- headers (one-open-per-group bounds the open stack).
        if (n.bNoVScroll && n.bClipH && n.FixedH > 0.0 && !isLeaf)
        {
            double contentH = n.PadT + n.PadB;
            int nonAcc = 0;
            for (int ci : n.Children)
            {
                const FPLayoutNode& cn = Nodes[(size_t)ci];
                if (cn.bSpacer || cn.bAccordion) continue;
                contentH += R[(size_t)ci].H + cn.MarginT + cn.MarginB;
                ++nonAcc;
            }
            if (nonAcc > 1) contentH += n.Spacing * (nonAcc - 1);
            if (contentH > n.FixedH + Eps)
                Out.push_back({ DesignRule::FitNoVScroll, n.Name,
                    "content does not fit without a vertical scroll bar" });
        }

        // ---- P20 PageWhitespaceReview: section pages of a carousel must be
        // ---- packed so no two adjacent pages that fit together stay separate
        // ---- (per-tab whitespace review - deterministic greedy pack).
        if (n.bCarousel && n.Children.size() >= 2)
        {
            const int minPages = CarouselMinPages(Nodes, &n);
            if (minPages < (int)n.Children.size())
                Out.push_back({ DesignRule::PageWhitespaceReview, n.Name,
                    "under-packed carousel pages (minimum " + std::to_string(minPages) + " page(s))" });
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

    // ---- P21 PinnedActionsNeverInScroll: canonical quick actions live ONLY
    // ---- in the PinnedStrip node. A node that is a pinned action (flagged
    // ---- bPinnedAction, or named exactly like a canonical QuickActionLabels
    // ---- entry) must never be inside a clipped scroll viewport, and must be
    // ---- a direct child of PinnedStrip - regardless of which panel or rail
    // ---- a duplicate is placed in.
    {
        const std::vector<std::string>& Labels = QuickActionLabels();
        int Strip = -1;
        for (size_t i = 0; i < Nodes.size(); ++i)
            if (Nodes[i].Name && std::string(Nodes[i].Name) == "PinnedStrip") { Strip = (int)i; break; }
        auto IsPinnedAction = [&](const FPLayoutNode& n)
        {
            if (n.bPinnedAction) return true;
            if (!n.Name) return false;
            const std::string nm(n.Name);
            for (const std::string& L : Labels) if (nm == L) return true;
            return false;
        };
        for (size_t i = 0; i < Nodes.size(); ++i)
        {
            const FPLayoutNode& n = Nodes[i];
            if (!IsPinnedAction(n)) continue;
            if (InViewport[i] != 0)
                Out.push_back({ DesignRule::PinnedActionsNeverInScroll, n.Name,
                    "pinned action inside a scroll viewport" });
            if (Strip < 0 || Parent[i] != Strip)
                Out.push_back({ DesignRule::PinnedActionsNeverInScroll, n.Name,
                    "pinned action outside the PinnedStrip node" });
        }
    }

    // ---- P24 NoTerminalOverlap: the main row is a fixed-height band with
    // ---- nothing below it inside the root except the timeline + terminal
    // ---- output window. Every MainRow column and every row of the center
    // ---- column must therefore resolve INSIDE the band - a taller canvas
    // ---- (interior drag-resize), a taller filter/legend row, or a wrapping
    // ---- parts strip would otherwise slide under the terminal (the overlap
    // ---- defect class). Unlike P2/P10 this also covers the flexed main row
    // ---- and its clipped columns.
    {
        int MR = -1, CN = -1;
        for (size_t i = 0; i < Nodes.size(); ++i)
        {
            if (Nodes[i].Name && std::string(Nodes[i].Name) == "MainRow") MR = (int)i;
            if (Nodes[i].Name && std::string(Nodes[i].Name) == "CENTER") CN = (int)i;
        }
        if (MR >= 0 && CN >= 0)
        {
            const FPRect& mr = R[(size_t)MR];
            for (int ci : Nodes[(size_t)MR].Children)
            {
                const FPRect& cr = R[(size_t)ci];
                if (cr.Y + cr.H > mr.Y + mr.H + Eps)
                    Out.push_back({ DesignRule::NoTerminalOverlap, Nodes[(size_t)MR].Name,
                        Nodes[(size_t)ci].Name });
            }
            const FPRect& cnr = R[(size_t)CN];
            for (int ci : Nodes[(size_t)CN].Children)
            {
                const FPRect& cr = R[(size_t)ci];
                if (cr.Y + cr.H > cnr.Y + cnr.H + Eps)
                    Out.push_back({ DesignRule::NoTerminalOverlap, Nodes[(size_t)CN].Name,
                        Nodes[(size_t)ci].Name });
            }
        }
    }

    // ---- P18 CarouselFallback + P19 ScrollbarReserve: every carousel page
    // ---- viewport has a fixed height, a nav strip right after it, and a
    // ---- bottom padding reserve so the page never blocks the nav buttons.
    for (size_t i = 0; i < Nodes.size(); ++i)
    {
        const FPLayoutNode& n = Nodes[i];
        if (!n.bCarousel) continue;
        if (n.FixedH <= Eps)
            Out.push_back({ DesignRule::CarouselFallback, n.Name,
                "carousel page viewport needs a fixed height" });
        int myIdx = -1;
        if (Parent[i] >= 0)
        {
            const int pi = Parent[i];
            for (size_t c = 0; c < Nodes[(size_t)pi].Children.size(); ++c)
                if (Nodes[(size_t)pi].Children[c] == (int)i) { myIdx = (int)c; break; }
        }
        bool bNavAfter = false;
        if (myIdx >= 0)
        {
            const int pi = Parent[i];
            for (size_t c = (size_t)myIdx + 1; c < Nodes[(size_t)pi].Children.size(); ++c)
            {
                const int ci = Nodes[(size_t)pi].Children[c];
                if (Nodes[(size_t)ci].bCarouselNav) { bNavAfter = true; break; }
            }
        }
        if (!bNavAfter)
            Out.push_back({ DesignRule::CarouselFallback, n.Name,
                "carousel page viewport needs a nav strip after it" });
        if (n.PadB < ScrollReserveBottom - Eps)
            Out.push_back({ DesignRule::ScrollbarReserve, n.Name,
                "carousel page viewport needs a bottom padding reserve" });
    }

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

// ============================================================================
// W2 assigning: hover region label + cycle-through-stack (pure contracts).
// ----------------------------------------------------------------------------
// The hover label is what the canvas paints at the cursor while a part is
// hovered: "<PartName>" or "<PartName> -> <LayerTag>" when the part resolves
// to a different layer. Used by SFaceSchematicLayer::OnPaint; the same text
// (and the empty-part guard) is what the math tests pin.
// ============================================================================
inline std::string FPHoverPartLabel(const char* PartName, const char* ResolvedLayer)
{
    if (!PartName || !PartName[0]) return std::string();
    std::string Out = PartName;
    if (ResolvedLayer && ResolvedLayer[0] && std::string(ResolvedLayer) != PartName)
    {
        Out += " -> ";
        Out += ResolvedLayer;
    }
    return Out;
}

// W2 cycle-through-stack: given the stack of parts under one point, the
// current cycle index, and whether this click repeats the previous click
// position (or holds a modifier that forces cycling), return the next stack
// index to resolve. Clicking a NEW spot always resets to the top (0). A
// repeat click advances one position and wraps modulo the stack depth; a
// single-part stack never cycles (always returns 0).
inline int FPSchematicCycleIndex(int StackDepth, int CurrentCycle, bool bRepeatClick)
{
    if (StackDepth <= 0) return 0;
    if (StackDepth <= 1) return 0;
    if (!bRepeatClick) return 0;
    int Next = CurrentCycle + 1;
    Next %= StackDepth;
    if (Next < 0) Next += StackDepth;
    return Next;
}

// ============================================================================
// W8 status badge: the compact persistent line near the view strip that
// summarizes the preset's art coverage — "8/10 states, 3/4 layers". The
// widget computes (StatesWithAllArt, TotalStates, LayersWithArtInActiveView,
// TotalLayers) and formats them here; this pure contract is what the math
// tests pin (including the guarded forms: everything empty/zero yields
// "0/0 states, 0/0 layers", counts are clamped to the totals).
// ============================================================================
inline std::string FPStatusSummary(int StatesReady, int TotalStates,
    int LayersWithArt, int TotalLayers)
{
    const int S = StatesReady < 0 ? 0 : (StatesReady > TotalStates ? TotalStates : StatesReady);
    const int L = LayersWithArt < 0 ? 0 : (LayersWithArt > TotalLayers ? TotalLayers : LayersWithArt);
    return std::string(std::to_string(S)) + "/" + std::to_string(TotalStates)
        + " states, " + std::to_string(L) + "/" + std::to_string(TotalLayers) + " layers";
}

// Phase 0 glyph-warning fix: the disclosure chevron painted on the two popup
// buttons (Canvas Options, History). U+25BE (▾) is absent from the fallback
// font (DroidSansFallback) the editor uses, so every paint of the button
// logged a LogSlate "font lacks glyph" warning. Latin-1 U+00BB (») is
// present in every fallback the editor can reach and reads as a disclosure
// chevron; it is the single source of truth so the math tests can pin exactly
// what the two buttons paint (and negative-test that the missing ▾ never
// returns).
inline const char* FPDisclosureGlyph()
{
    return "\u00BB";
}

// Phase 1 zone-strip rotation scrub. Dragging the zone diagram in EMPTY space
// (away from the boundary lines) rotates the preview: the scrub is a relative
// drag from the press point, so the full strip width maps to 360° and the
// orbit never jumps when the press lands away from the current cursor. The
// result is wrapped to [-180, 180) so rotation is continuous across the back.
// A non-positive width (degenerate geometry) returns the start unchanged and
// a NaN start is guarded to 0 — the scrub must never poison the orbit.
inline double FPZoneScrubYawAfterDrag(double StartYaw, double DeltaPx, double WidthPx)
{
    if (StartYaw != StartYaw) return 0.0;                    // NaN guard
    if (WidthPx <= 0.0) return StartYaw;                     // degenerate width
    double Yaw = StartYaw + (DeltaPx / WidthPx) * 360.0;
    while (Yaw >= 180.0) Yaw -= 360.0;                       // wrap to [-180, 180)
    while (Yaw < -180.0) Yaw += 360.0;
    return Yaw;
}

// Phase C up/down view scrub (the vertical mirror of the yaw scrub). The full
// strip height maps to the 180° pitch span and the result CLAMPS to
// [-90, 90] — up/down has no wrap (a head can't tilt past straight-down then
// come back from the other side), so reaching the top/bottom edge simply
// parks at the Top/Bottom view state. A non-positive height (degenerate
// geometry) returns the start unchanged and a NaN start is guarded to 0 — the
// scrub must never poison the orbit pitch.
inline double FPZoneScrubPitchAfterDrag(double StartPitch, double DeltaPx, double HeightPx)
{
    if (StartPitch != StartPitch) return 0.0;                // NaN guard
    if (HeightPx <= 0.0) return StartPitch;                  // degenerate height
    double Pitch = StartPitch + (DeltaPx / HeightPx) * 180.0;
    if (Pitch > 90.0) Pitch = 90.0;                          // clamp to [-90, 90]
    if (Pitch < -90.0) Pitch = -90.0;
    return Pitch;
}

// W6 canvas transform readout: a compact one-line label painted beside the
// gizmo on the canvas, "P(x,y) S(x,y) R(deg)", angles normalized to -180..180
// so the readout never drifts past a full turn. The gizmo layer formats the
// active layer's FFaceArtTransform through this pure contract; the math tests
// pin the exact strings (guarded form: nothing selected yields a dash).
inline std::string FPTransformReadout(float PosX, float PosY, float ScaleX,
    float ScaleY, float RotationDeg)
{
    if (RotationDeg != RotationDeg) return "-";   // NaN guard
    float R = RotationDeg;
    while (R > 180.0f) R -= 360.0f;
    while (R < -180.0f) R += 360.0f;
    char Buf[96];
    const float ScalePctX = ScaleX * 100.0f;
    const float ScalePctY = ScaleY * 100.0f;
    std::snprintf(Buf, sizeof(Buf), "P(%.0f, %.0f) S(%.0f%%, %.0f%%) R(%.0f)",
        PosX, PosY, ScalePctX, ScalePctY, R);
    return Buf;
}

// Default face-template regions in UV space (0..1, y-down). Includes one
// concave bucket (Nose) and one bucket with a hole (Mouth, hole covered by
// Teeth which is listed after Mouth so first-match-wins yields Teeth).
inline std::vector<FPHotspotRegion> DefaultHotspotRegions()
{
    return {
        { "BrowL", { HP(0.22, 0.335), HP(0.27, 0.295), HP(0.34, 0.29), HP(0.42, 0.325),
                     HP(0.39, 0.345), HP(0.34, 0.315), HP(0.27, 0.325), HP(0.24, 0.35) } },
        { "BrowR", { HP(0.78, 0.335), HP(0.73, 0.295), HP(0.66, 0.29), HP(0.58, 0.325),
                     HP(0.61, 0.345), HP(0.66, 0.315), HP(0.73, 0.325), HP(0.76, 0.35) } },
        { "EyeL", { HP(0.26, 0.355), HP(0.37, 0.355), HP(0.40, 0.43), HP(0.37, 0.52),
                    HP(0.26, 0.52), HP(0.245, 0.43) } },
        { "EyeR", { HP(0.63, 0.355), HP(0.74, 0.355), HP(0.755, 0.43), HP(0.74, 0.52),
                    HP(0.63, 0.52), HP(0.60, 0.43) } },
        { "Nose", // minuscule triangle hint: bridge between the eyes, tiny tip
            { HP(0.43, 0.61), HP(0.57, 0.61), HP(0.54, 0.67), HP(0.50, 0.69),
              HP(0.46, 0.67) } },
        { "CheekL", { HP(0.07, 0.47), HP(0.15, 0.43), HP(0.26, 0.52), HP(0.255, 0.66),
                      HP(0.20, 0.73), HP(0.11, 0.69), HP(0.07, 0.62) } },
        { "CheekR", { HP(0.93, 0.47), HP(0.85, 0.43), HP(0.74, 0.52), HP(0.745, 0.66),
                      HP(0.80, 0.73), HP(0.89, 0.69), HP(0.93, 0.62) } },
        { "Mouth", // shallow lip ring with a tiny open-mouth hole
            { HP(0.41, 0.75), HP(0.50, 0.74), HP(0.59, 0.75), HP(0.61, 0.78),
              HP(0.55, 0.82), HP(0.45, 0.82), HP(0.39, 0.78) },
            { { HP(0.46, 0.775), HP(0.54, 0.775), HP(0.56, 0.79), HP(0.50, 0.80), HP(0.44, 0.79) } } },
        { "Teeth", { HP(0.46, 0.775), HP(0.54, 0.775), HP(0.56, 0.79), HP(0.50, 0.80), HP(0.44, 0.79) } },
        { "Chin", { HP(0.43, 0.81), HP(0.57, 0.81), HP(0.53, 0.85), HP(0.50, 0.86), HP(0.47, 0.85) } },
        { "EarL", { HP(0.02, 0.42), HP(0.06, 0.36), HP(0.11, 0.42), HP(0.105, 0.60),
                    HP(0.06, 0.68), HP(0.03, 0.60) } },
        { "EarR", { HP(0.98, 0.42), HP(0.94, 0.36), HP(0.89, 0.42), HP(0.895, 0.60),
                    HP(0.94, 0.68), HP(0.97, 0.60) } },
        { "Neck", { HP(0.43, 0.86), HP(0.57, 0.86), HP(0.68, 0.98), HP(0.32, 0.98) } }
    };
}

// ============================================================================
// Hotspot → layer mapping (Phase 4 remediation): derives the primary layer
// tag an anatomical region name should route to, so canvas/parts-strip picks
// select a real layer instead of unconditionally opening the import wizard.
// Pure C++17; driven by the preset's overridable HotspotLayerMap (the widget
// merges: explicit map first, then this derivation). Precedence:
//   1. Exact match (case-sensitive) — "Mouth" → "Mouth", "Brows" → "Brows".
//   2. Singular/plural normalize: region+"s" in layers, or region minus one
//      trailing 's' in layers — "Brow" → "Brows".
//   3. L/R collapse: a region ending in 'L'/'R' (length >= 3) strips the
//      suffix and re-runs the plural normalize — "EyeL"/"EyeR" → "Eyes",
//      "BrowL"/"BrowR" → "Brows". "EyeLash" ends in 'h', never collapses.
//   4. Prefix: the region name is a prefix of EXACTLY ONE layer tag —
//      "Eye" → "Eyes", "Brow" → "Brows". Ambiguous prefixes match nothing.
//   5. No match: nullptr (widget falls back to import-assignment flow).
// ============================================================================
inline const char* FPHotspotLayerMatch(const std::vector<std::string>& Layers, const char* RegionName)
{
    if (!RegionName || !RegionName[0]) return nullptr;
    const std::string Region(RegionName);
    if (Region.empty()) return nullptr;

    // 1. Exact (case-sensitive) — full pass first so a literal layer tag
    //    beats any derivation regardless of vector order ("Eye" > "Eyes").
    for (const std::string& L : Layers)
        if (L == Region) return L.c_str();

    // 2. Singular/plural normalize.
    for (const std::string& L : Layers)
    {
        if (Region.size() + 1 == L.size() && L.compare(0, Region.size(), Region) == 0)
            return L.c_str();   // region + 's' == layer ("Brow" -> "Brows")
        if (Region.size() == L.size() + 1 &&
            Region.compare(0, L.size(), L) == 0 && Region.back() == 's')
            return L.c_str();   // region minus trailing 's' == layer
    }

    // 3. L/R collapse: strip the directional suffix, then re-run 1+2 on the base.
    if (Region.size() >= 3)
    {
        const char Last = Region.back();
        if (Last == 'L' || Last == 'R')
        {
            const std::string Base = Region.substr(0, Region.size() - 1);
            for (const std::string& L : Layers)
                if (L == Base) return L.c_str();   // exact base wins over plural
            for (const std::string& L : Layers)
                if (Base.size() + 1 == L.size() && L.compare(0, Base.size(), Base) == 0)
                    return L.c_str();   // base + 's' == layer ("EyeL" -> "Eyes")
        }
    }

    // 4. Prefix: the region name is a prefix of EXACTLY ONE layer tag.
    const char* BestPrefix = nullptr;
    for (const std::string& L : Layers)
    {
        if (L.size() > Region.size() && L.compare(0, Region.size(), Region) == 0)
        {
            if (BestPrefix) return nullptr;   // ambiguous -> none
            BestPrefix = L.c_str();
        }
    }

    // 5. No match.
    return BestPrefix;
}

// ============================================================================
// Hotspot transform mirror (Phase 2): reproduces the master material's UV
// chain exactly as built by deploy.py:
//   TexCoord → Subtract(Pivot) → Add(ArtPos) → Multiply(ArtScale)
//            → Add(Pivot) → Add(ParallaxOffset) → Rotate(ArtRot) → UVs
// with ParallaxOffset = 0 in the tool and the Rotator node's built-in
// CenterX/CenterY = 0.5 (rotation around the UV center, clockwise for a
// positive angle in degrees, per the official Rotator docs). The widget
// transforms each hotspot region's outline by its mapped layer's effective
// transform so the overlay hugs the rendered art; the math suite pins this
// mirror against the graph structure.
// ============================================================================
inline FPHotspotPoint FPHotspotTransformPoint(FPHotspotPoint P,
    double PosX, double PosY, double ScaleX, double ScaleY, double RotDeg,
    double PivotX = 0.5, double PivotY = 0.5,
    double RotCenterX = 0.5, double RotCenterY = 0.5)
{
    double X = P.X, Y = P.Y;
    X = (X - PivotX + PosX) * ScaleX + PivotX;
    Y = (Y - PivotY + PosY) * ScaleY + PivotY;
    if (RotDeg != 0.0)
    {
        const double A = RotDeg * 3.14159265358979323846 / 180.0;
        const double C = std::cos(A), S = std::sin(A);
        const double RX = X - RotCenterX, RY = Y - RotCenterY;
        X = RotCenterX + RX * C - RY * S;   // clockwise (y-down UV space)
        Y = RotCenterY + RX * S + RY * C;
    }
    return { X, Y };
}

inline FPHotspotRegion FPHotspotTransformRegion(const FPHotspotRegion& R,
    double PosX, double PosY, double ScaleX, double ScaleY, double RotDeg,
    double PivotX = 0.5, double PivotY = 0.5)
{
    FPHotspotRegion Out(R.Name);
    for (const FPHotspotPoint& P : R.Outer)
        Out.Outer.push_back(FPHotspotTransformPoint(P, PosX, PosY, ScaleX, ScaleY, RotDeg, PivotX, PivotY));
    for (const std::vector<FPHotspotPoint>& H : R.Holes)
    {
        std::vector<FPHotspotPoint> HOut;
        for (const FPHotspotPoint& P : H)
            HOut.push_back(FPHotspotTransformPoint(P, PosX, PosY, ScaleX, ScaleY, RotDeg, PivotX, PivotY));
        Out.Holes.push_back(std::move(HOut));
    }
    return Out;
}

// ============================================================================
// Phase C: canvas selection + unified inspect mode (design review).
//   - FLayerQuadFromTransform: the selected layer's art quad in UV space,
//     built from the layer's effective transform for the active view state
//     with the SAME FPHotspotTransformPoint chain the hotspot regions use, so
//     the canvas selection outline hugs the art in EVERY view state (the
//     cross-view outline constraint) and quad hit-testing selects exactly the
//     pixels the master material paints.
//   - FPPointInQuad: boundary-inclusive containment (a quad is a convex
//     4-gon, so the even-odd polygon test applies).
//   - FPHitTopmostQuad: topmost hit = the LAST matching quad in draw order
//     (the layer list paints bottom-to-top, so the last entry is on top).
//   - FPCycleQuadHit: right/ctrl-click cycling through overlapping layers:
//     the hit AFTER the current selection in the list, wrapping; when the
//     current selection is not among the hits, start at the topmost hit.
//   - Inspect mode: the canvas's single segmented control
//     Textured / Outline / Depth / Wireframe / Depth Heatmap. DeriveInspectMode
//     maps the five source toggles (Show Textures, Depth Mesh, Wireframe,
//     Outline overlay, Color by Depth) to the canonical mode; every
//     other combination (e.g. the legacy Split = textures+depth) is Custom.
//     W7: the segmented canvas row is the SOLE display-mode control — the four
//     display-mode Config checks were retired from the Diagnostics rail (the
//     rail Config keeps only Blinking/Swoosh/Nested Art/Params, see the 4-row
//     Sec-CFG manifest). InspectComboForMode returns the canonical toggle set
//     a segment applies; the bLocal* flags are private intermediate state only.
// ============================================================================
enum FInspectMode : int { InspectTextured = 0, InspectOutline = 1, InspectDepth = 2,
                          InspectWireframe = 3, InspectDepthHeatmap = 4, InspectCustom = -1 };

inline const char* InspectModeLabel(int Mode)
{
    switch (Mode)
    {
    case 0: return "Textured";
    case 1: return "Outline";
    case 2: return "Depth";
    case 3: return "Wireframe";
    case 4: return "Heatmap";
    }
    return "Custom";
}

struct FPLayerQuad { FPHotspotPoint C[4]; }; // TL, TR, BR, BL (UV space, y-down)

inline FPLayerQuad FLayerQuadFromTransform(double PosX, double PosY,
    double ScaleX, double ScaleY, double RotDeg, double PivotX = 0.5, double PivotY = 0.5)
{
    FPLayerQuad Q;
    Q.C[0] = FPHotspotTransformPoint({ 0.0, 0.0 }, PosX, PosY, ScaleX, ScaleY, RotDeg, PivotX, PivotY);
    Q.C[1] = FPHotspotTransformPoint({ 1.0, 0.0 }, PosX, PosY, ScaleX, ScaleY, RotDeg, PivotX, PivotY);
    Q.C[2] = FPHotspotTransformPoint({ 1.0, 1.0 }, PosX, PosY, ScaleX, ScaleY, RotDeg, PivotX, PivotY);
    Q.C[3] = FPHotspotTransformPoint({ 0.0, 1.0 }, PosX, PosY, ScaleX, ScaleY, RotDeg, PivotX, PivotY);
    return Q;
}

inline bool FPPointInQuad(double X, double Y, const FPLayerQuad& Q)
{
    const std::vector<FPHotspotPoint> Loop = { Q.C[0], Q.C[1], Q.C[2], Q.C[3] };
    return FPPointInPolygon(X, Y, Loop);
}

inline int FPHitTopmostQuad(double X, double Y, const std::vector<FPLayerQuad>& Quads)
{
    int Top = -1;
    for (size_t i = 0; i < Quads.size(); ++i)
        if (FPPointInQuad(X, Y, Quads[i])) Top = (int)i;
    return Top; // last hit in draw order = topmost
}

inline int FPCycleQuadHit(const std::vector<int>& Hits, int Current)
{
    if (Hits.empty()) return -1;
    for (size_t i = 0; i < Hits.size(); ++i)
        if (Hits[i] == Current)
            return Hits[(i + 1) % Hits.size()];
    return Hits[0];
}

// Canonical toggles for an inspect mode, in the widget's toggle order:
// textures, depth mesh, wireframe, outline overlay, color-by-depth.
struct FPInspectCombo { bool T = false, D = false, W = false, O = false, C = false; };

inline FPInspectCombo InspectComboForMode(int Mode)
{
    FPInspectCombo B;
    switch (Mode)
    {
    case InspectTextured:     B.T = true; break;
    case InspectOutline:      B.T = true; B.O = true; break;
    case InspectDepth:        B.D = true; break;
    case InspectWireframe:    B.W = true; break;
    case InspectDepthHeatmap: B.D = true; B.C = true; break;
    default: break;
    }
    return B;
}

inline int DeriveInspectMode(bool bShowTextures, bool bShowDepthMesh, bool bShowWireframe,
    bool bOutlineOverlay, bool bColorByDepth)
{
    if (bShowTextures && !bShowDepthMesh && !bShowWireframe && bOutlineOverlay && !bColorByDepth) return InspectOutline;
    if (!bShowTextures && bShowDepthMesh && !bShowWireframe && !bOutlineOverlay && bColorByDepth) return InspectDepthHeatmap;
    if (!bShowTextures && bShowDepthMesh && !bShowWireframe && !bOutlineOverlay && !bColorByDepth) return InspectDepth;
    if (!bShowTextures && !bShowDepthMesh && bShowWireframe && !bOutlineOverlay && !bColorByDepth) return InspectWireframe;
    if (bShowTextures && !bShowDepthMesh && !bShowWireframe && !bOutlineOverlay && !bColorByDepth) return InspectTextured;
    return InspectCustom; // custom combos (incl. legacy Split = textures+depth)
}

// ============================================================================
// Pin drift mirror (Phase 3): the editor's sync-drift indicator counts how
// many of the OTHER view states carry a canonical transform that differs
// from the active view's for the selected layer. Pure C++17 mirror of
// RefreshSyncDriftIndicator so the math is pinned by the test suite.
// ============================================================================
struct FPMirrorTransform
{
    double PosX = 0, PosY = 0, ScaleX = 1, ScaleY = 1, Rot = 0;
};

inline bool FPMirrorTransformEqual(const FPMirrorTransform& A, const FPMirrorTransform& B)
{
    // Exact equality — mirrors the widget's FVector2D != comparison.
    return A.PosX == B.PosX && A.PosY == B.PosY
        && A.ScaleX == B.ScaleX && A.ScaleY == B.ScaleY && A.Rot == B.Rot;
}

// Drift count: views (excluding ActiveIndex) whose transform differs from
// the active view's. Out-of-range ActiveIndex -> 0 (widget guards too).
inline int FPPinDriftCount(const std::vector<FPMirrorTransform>& Views, int ActiveIndex)
{
    if (Views.empty() || ActiveIndex < 0 || ActiveIndex >= (int)Views.size()) return 0;
    int Drifted = 0;
    for (size_t i = 0; i < Views.size(); ++i)
        if ((int)i != ActiveIndex && !FPMirrorTransformEqual(Views[i], Views[ActiveIndex]))
            ++Drifted;
    return Drifted;
}

// ============================================================================
// Rail width clamp mirror (Phase 4): the Diagnostics rail's "Rail Width" slider
// lives in [RailWidthMin, RailWidthMax] with RailWidth as the default.
// Mirrors the widget's FMath::Clamp + NaN guard so the range is pinned by
// the test suite.
// ============================================================================
inline double ClampRailWidth(double W)
{
    if (W != W) return RailWidth;                     // NaN -> default
    if (W < RailWidthMin) return RailWidthMin;
    if (W > RailWidthMax) return RailWidthMax;
    return W;
}

// ============================================================================
// Accessibility mirrors (Phase 4b, re-based for W1): the UI remediation
// recommendations. Every helper below is pure C++17 and mirrored 1:1 by the
// widget code:
//   - PageSectionTitles()   - canonical per-page section title registry that
//                             drives the page chips + cross-page search jump.
//                             The widget registers the same titles in the same
//                             order (Panels.cpp RegisterPageSection call sites).
//                             W1: the registry is grouped by the 4 task pages
//                             plus the closed-by-default Developer drawer
//                             (page 4), replacing the old 5-rail registry.
//   - FindPageSectionByTitle() - case-insensitive substring search across the
//                             registry in page order; first match wins.
//                             Mirrors UFaceParallaxEditorWidget::OnPageSearchCommitted.
//   - ConfigSummary()       - "K of 4 on" summary line for the collapsed
//                             Config disclosure (Mirrors RefreshConfigCheckboxes).
//   - VisemeSummary()       - "N viseme rows" summary for the collapsed Viseme
//                             grid disclosure (Mirrors RebuildVisemeGrid).
//   - RailWidthAfterDrag()  - drag-resize math: start width + pixel delta,
//                             rounded and clamped (Mirrors SFaceRailResizer).
//   - QuickActionLabels()   - persistent quick-actions bar button set shown
//                             above the context-panel switcher on every page.
// ============================================================================
inline const std::vector<std::vector<std::string>>& PageSectionTitles()
{
    // W1: 4 task pages + the Developer drawer (closed by default). Section order
    // per page mirrors the widget's RegisterPageSection / RegisterAccordionPageSections
    // call order in Panels.cpp (chips are built from that registration order).
    static const std::vector<std::vector<std::string>> Titles = {
        /* page 0 Assign */         { "Selected Layer", "Layers", "Import",
                                      "Outline -> Depth", "Bulk Assign", "Assign Ops" },
        /* page 1 Transform & Sync */ { "Transform", "Sync + Align" },
        /* page 2 Expression/Blink/Viseme */ { "Nested Art / Pins",
                                      "Viseme Frames (click filled cell = play)",
                                      "Hull Review (click thumb = jump)" },
        /* page 3 Preview & Debug */ { "Camera Follow", "Camera", "Blend Preview",
                                      "Edge Analysis", "Depth Debug" },
        /* page 4 Developer (drawer) */ { "Tag Validator", "Material Cross-Reference",
                                      "Param Reference", "Config",
                                      "Param Bindings (state + layer)",
                                      "Problems (click row = jump)",
                                      "Status Detail",
                                      "All Layers (current state)" }
    };
    return Titles;
}

inline int FindPageSectionByTitle(const std::string& Query, int& OutPage, int& OutIdx)
{
    if (Query.empty()) return -1;             // widget guards empty queries too
    const std::vector<std::vector<std::string>>& Titles = PageSectionTitles();
    std::string Q = Query;
    for (size_t i = 0; i < Q.size(); ++i)
        Q[i] = (char)std::tolower((unsigned char)Q[i]);
    for (size_t Pi = 0; Pi < Titles.size(); ++Pi)
    {
        for (size_t Si = 0; Si < Titles[Pi].size(); ++Si)
        {
            std::string T = Titles[Pi][Si];
            for (size_t c = 0; c < T.size(); ++c)
                T[c] = (char)std::tolower((unsigned char)T[c]);
            if (T.find(Q) != std::string::npos)
            {
                OutPage = (int)Pi;
                OutIdx = (int)Si;
                return 0;
            }
        }
    }
    return -1;
}

inline std::string ConfigSummary(int NumEnabled)
{
    return std::string(std::to_string(NumEnabled)) + " of 4 on";
}

inline std::string VisemeSummary(int NumRows)
{
    if (NumRows <= 0) return "No viseme frames";
    return std::string(std::to_string(NumRows)) + " viseme rows";
}

inline double RailWidthAfterDrag(double StartWidth, double DeltaPx)
{
    return ClampRailWidth(std::round(StartWidth + DeltaPx));
}

inline const std::vector<std::string>& QuickActionLabels()
{
    // P7-B: the pinned strip is 3 actions. "Sync All -> All" was removed -
    // sync/copy lives in the ONE Copy/Sync panel on the props pane's
    // Sync + Align page (Phase 3: one apply model, no hidden full-sync
    // duplicate).
    static const std::vector<std::string> Labels = {
        "Import Art...", "Auto-Fit All", "Clear All Overrides"
    };
    return Labels;
}

// ============================================================================
// Preview mode mirrors (Phase 4b): the tool's two preview modes over the same
// four live animation systems (blink / expression / viseme / orbit).
//   - Cycle Preview (Phase 2) sequences them ONE AT A TIME, 2s per phase,
//     in the pinned order below (mirror of StartCyclePreview/NativeTick).
//   - Live Preview (Phase 4b) enables ALL FOUR AT ONCE — the assembled
//     result check — with a 2.5s viseme re-trigger cadence and an 8s orbit
//     sweep period (mirror of StartLivePreview/NativeTick).
// ============================================================================
inline const std::vector<std::string>& PreviewSystems()
{
    static const std::vector<std::string> S = { "blink", "expression", "viseme", "orbit" };
    return S;
}

inline double PreviewCyclePhaseDuration() { return 2.0; }

inline double LivePreviewVisemeCadence() { return 2.5; }

inline double LivePreviewOrbitPeriod() { return 8.0; }

// Mode -> per-system enable flags. "cycle" at phase P is one-hot (only the
// P-th system runs); "live" enables every system simultaneously.
inline std::vector<bool> PreviewModeSystemFlags(const std::string& Mode, int Phase = 0)
{
    const int N = (int)PreviewSystems().size();
    std::vector<bool> F((size_t)N, false);
    if (Mode == "live")
    {
        for (int i = 0; i < N; ++i) F[(size_t)i] = true;
    }
    else if (Phase >= 0 && Phase < N)
    {
        F[(size_t)Phase] = true;
    }
    return F;
}

// ============================================================================
// Per-axis sync mirrors (P3): SyncCanonicalAxisToAllViews propagates ONE axis
// of the source canonical into each other state's view override, leaving the
// other axes' existing overrides untouched. Axis codes: 0 = Position X,
// 1 = Position Y, 2 = Scale X, 3 = Scale Y, 4 = Rotation (degrees).
// Position deltas subtract; scale deltas divide (ratio); rotation subtracts.
// ============================================================================
inline void SyncAxisDelta(double SrcX, double SrcY, double SrcRot,
                          double DstX, double DstY, double DstRot,
                          int Axis, double& OutDX, double& OutDY, double& OutRot)
{
    OutDX = 0.0; OutDY = 0.0; OutRot = 0.0;
    switch (Axis)
    {
        case 0: OutDX = SrcX - DstX; break;
        case 1: OutDY = SrcY - DstY; break;
        case 2: OutDX = (DstX > 1e-6) ? SrcX / DstX : 1.0; break;
        case 3: OutDY = (DstY > 1e-6) ? SrcY / DstY : 1.0; break;
        case 4: OutRot = SrcRot - DstRot; break;
        default: break;
    }
}

// ============================================================================
// Base-layer pin mirrors (P3): the layer pin uses the same zone-frame authoring
// math as nested pins (SetNestedPinFromUV), plus the projection used by the
// gizmo to place the handle (ProjectPinToUVAtAngles). Zone yaw decides which
// of the head axes the horizontal UV axis maps to.
// ============================================================================
inline void LayerPinFromUV(double UVX, double UVY, double ZoneYawDeg,
                           double& OutX, double& OutY, double& OutZ)
{
    const double UVCenterX = UVX - 0.5, UVCenterY = UVY - 0.5;
    const double AY = ZoneYawDeg < 0.0 ? -ZoneYawDeg : ZoneYawDeg;
    if (AY < 45.0)
    {
        OutX = UVCenterX * 2.0; OutY = UVCenterY * 2.0; OutZ = 0.0;
    }
    else if (AY >= 45.0 && AY < 135.0)
    {
        OutX = 0.0; OutY = UVCenterY * 2.0; OutZ = UVCenterX * 2.0;
        if (ZoneYawDeg < 0.0) OutZ = -OutZ;
    }
    else
    {
        OutX = -UVCenterX * 2.0; OutY = UVCenterY * 2.0; OutZ = 0.0;
    }
}

inline void PinProjectToUV(double PX, double PY, double PZ, double YawDeg, double PitchDeg,
                           double HalfW, double HalfH, double HalfD,
                           double& OutU, double& OutV)
{
    const double Cy = cos(YawDeg * 3.14159265358979323846 / 180.0);
    const double Sy = sin(YawDeg * 3.14159265358979323846 / 180.0);
    const double Cp = cos(PitchDeg * 3.14159265358979323846 / 180.0);
    const double Sp = sin(PitchDeg * 3.14159265358979323846 / 180.0);
    const double WX = PX * HalfW, WY = PY * HalfH, WZ = PZ * HalfD;
    const double ViewX = WX * Cy + WZ * Sy;
    const double VisibleX = HalfW * fabs(Cy) + HalfD * fabs(Sy);
    const double ViewY = WY * Cp;
    const double VisibleY = HalfH * fabs(Cp) + HalfD * fabs(Sp);
    OutU = 0.5 + 0.5 * ViewX / (VisibleX > 1e-9 ? VisibleX : 1.0);
    OutV = 0.5 + 0.5 * ViewY / (VisibleY > 1e-9 ? VisibleY : 1.0);
    if (OutU < 0.0) { OutU = 0.0; }
    if (OutU > 1.0) { OutU = 1.0; }
    if (OutV < 0.0) { OutV = 0.0; }
    if (OutV > 1.0) { OutV = 1.0; }
}

// ============================================================================
// Import completion mirrors (P3): the coverage summary the import paths print
// after an apply ("albedo A/10, normal N/10, depth D/10" over the 10 states).
// ============================================================================
inline std::string ImportCoverageSummary(int TotalStates, int WithAlbedo, int WithNormal, int WithDepth)
{
    if (TotalStates <= 0) return "no states";
    return std::string("albedo ") + std::to_string(WithAlbedo) + "/" + std::to_string(TotalStates)
         + ", normal " + std::to_string(WithNormal) + "/" + std::to_string(TotalStates)
         + ", depth " + std::to_string(WithDepth) + "/" + std::to_string(TotalStates);
}

// ============================================================================
// Camera source mirrors (P3): the six ECameraSource entries in the widget's
// Camera rail combo, in pinned order (default PlayerCamera0 first).
// ============================================================================
inline const std::vector<std::string>& CameraSourceLabels()
{
    static const std::vector<std::string> Labels = {
        "PlayerCamera0", "PlayerCamera1", "SpecifiedActor",
        "SequencerCamera", "PreviewActor", "Custom"
    };
    return Labels;
}

// ============================================================================
// Zone drag mirrors (P3): dragging a zone-boundary handle changes the matching
// ZoneBoundaryMultipliers entry by the dragged angle (multiplier = boundary
// degrees / HalfZoneWidth), clamped to the [0.5, 20] editor range. NaN input
// keeps the existing multiplier (mirror of ApplyZoneBoundaryDrag).
// ============================================================================
inline double ZoneBoundaryAfterDrag(double Multiplier, double DeltaDegrees, double HalfZoneWidth)
{
    if (Multiplier != Multiplier) return Multiplier;  // NaN guard
    const double HZW = (HalfZoneWidth > 1e-6) ? HalfZoneWidth : 22.5;
    double Deg = Multiplier * HZW + (DeltaDegrees != DeltaDegrees ? 0.0 : DeltaDegrees);
    double M = Deg / HZW;
    if (M < 0.5) M = 0.5;
    if (M > 20.0) M = 20.0;
    return M;
}

// ============================================================================
// Performance tier mirrors (P3): tier 0 Low / 1 Medium / 2 High map to an
// async texture-cache budget and a default outline->depth grid resolution.
// Medium (the pre-tier defaults) keeps cache 256 / grid 64.
// ============================================================================
inline int PerformanceTierCacheSize(int Tier) { return Tier <= 0 ? 64 : (Tier >= 2 ? 512 : 256); }
inline int PerformanceTierGridSize(int Tier)  { return Tier <= 0 ? 32 : (Tier >= 2 ? 128 : 64); }

// ============================================================================
// Display-mode dedupe mirror (P3): the three debug toggles (textures / depth
// mesh / wireframe) derive the exclusive display mode. Standard combos map to
// 0 Textured, 1 Depth, 2 Wireframe, 3 Split (textures+depth); anything else
// (e.g. textures+wireframe) is a custom combo and yields -1, which clears the
// mode-row highlight instead of pretending one mode is active.
// ============================================================================
inline int DeriveDisplayMode(bool bTex, bool bDepth, bool bWire)
{
    if (bTex && !bDepth && !bWire) return 0;
    if (!bTex && bDepth && !bWire) return 1;
    if (!bTex && !bDepth && bWire) return 2;
    if (bTex && bDepth && !bWire) return 3;
    return -1;
}

// ============================================================================
// Assign-grid mirror (P3): each cell of the bulk-assign grid summarizes one
// (state, layer) slot as 2 = fully assigned (albedo+normal+depth), 1 = partial
// (any channel present), 0 = empty. The real grid paints the same three states
// and the row summary line prints Filled/Total via AssignCoverageText.
// ============================================================================
inline int AssignCellState(bool bAlbedo, bool bNormal, bool bDepth)
{
    return (bAlbedo && bNormal && bDepth) ? 2 : ((bAlbedo || bNormal || bDepth) ? 1 : 0);
}

inline std::string AssignCoverageText(int Filled, int Total)
{
    return std::to_string(Filled) + "/" + std::to_string(Total);
}

// ============================================================================
// Phase D sync-op mirrors: the sync row is a single grouped control with an
// explicit op choice. 0 Transform, 1 Textures, 2 Both; invalid op values are
// treated as Both (the canonical default). SyncOpHasTransform/Textures derive
// which channel the op touches.
// ============================================================================
enum FSyncOp : int
{
    SyncOpTransform = 0,
    SyncOpTextures  = 1,
    SyncOpBoth      = 2
};

inline int SyncOpNormalized(int Op)
{
    return (Op >= SyncOpTransform && Op <= SyncOpBoth) ? Op : SyncOpBoth;
}

inline const char* SyncOpLabel(int Op)
{
    switch (SyncOpNormalized(Op))
    {
    case SyncOpTransform: return "Transform";
    case SyncOpTextures:  return "Textures";
    }
    return "Both";
}

inline bool SyncOpHasTransform(int Op)
{
    const int N = SyncOpNormalized(Op);
    return N == SyncOpTransform || N == SyncOpBoth;
}
inline bool SyncOpHasTextures(int Op)
{
    const int N = SyncOpNormalized(Op);
    return N == SyncOpTextures || N == SyncOpBoth;
}

// ============================================================================
// W4 sync two-defaults + more-disclosure mirror: the Sync + Align page's op
// selector shows the two common defaults (Transform, Textures) as prominent
// buttons and tucks the combined op (Both) behind a "more..." disclosure. The
// widget renders SyncOpDefaultOps() first, then a "more..." toggle that
// reveals SyncOpMoreOps(). Pure contract: the default set is exactly the two
// single-channel ops, the more set is exactly the combined op, and an op is a
// default iff it is a single-channel op.
// ============================================================================
inline const std::vector<int>& SyncOpDefaultOps()
{
    static const std::vector<int> Ops = { SyncOpTransform, SyncOpTextures };
    return Ops;
}
inline const std::vector<int>& SyncOpMoreOps()
{
    static const std::vector<int> Ops = { SyncOpBoth };
    return Ops;
}
inline bool SyncOpIsDefault(int Op)
{
    const int N = SyncOpNormalized(Op);
    return N == SyncOpTransform || N == SyncOpTextures;
}
inline bool SyncOpIsMore(int Op)
{
    const int N = SyncOpNormalized(Op);
    return N == SyncOpBoth;
}

// ============================================================================
// W4 view-strip drift mirror: the persistent view strip (one tab per state)
// marks a state as DRIFTED when the selected layer's slot differs from the
// active state (transform OR art). This is the per-state classification the
// strip badge consumes; it reuses the exact-equality semantics of the sync
// drift indicator so the strip always agrees with the Sync + Align panel.
//   FPViewStripDrift(ActiveHasArt, DestHasArt, TransformEqual, TextureEqual)
//   = 0 SyncDriftSame   - no difference from the active state
//   = 1 SyncDriftArt    - art presence differs (dest missing art the active has)
//   = 2 SyncDriftXform  - transform differs (position/scale/rotation)
//   = 3 SyncDriftBoth   - both differ
// ============================================================================
enum FSyncDrift : int
{
    SyncDriftSame = 0,
    SyncDriftArt = 1,
    SyncDriftXform = 2,
    SyncDriftBoth = 3
};

inline int FPViewStripDrift(bool bActiveHasArt, bool bDestHasArt,
    bool bTransformEqual, bool bTextureEqual)
{
    const bool bArtDiff = (bActiveHasArt != bDestHasArt)
        || (!bTextureEqual && bActiveHasArt && bDestHasArt);
    const bool bXformDiff = !bTransformEqual;
    if (bArtDiff && bXformDiff) return SyncDriftBoth;
    if (bArtDiff) return SyncDriftArt;
    if (bXformDiff) return SyncDriftXform;
    return SyncDriftSame;
}

// ============================================================================
// Sync destination diff mirror (Phase 3): the Sync + Align page's destination
// grid colors each view by whether applying the current op would change it.
//   0 SyncDestSame     - nothing would change (dest already matches)
//   1 SyncDestDiffers  - transform and/or textures differ (apply overwrites)
//   2 SyncDestMissing  - active has art the destination lacks (apply fills it)
// Pure C++17 mirror of RefreshSyncDriftIndicator's per-view diff so the math
// is pinned by the test suite. Missing outranks Differs for texture ops.
// ============================================================================
enum FSyncDestDiff : int
{
    SyncDestSame = 0,
    SyncDestDiffers = 1,
    SyncDestMissing = 2
};

inline int FPSyncDestDiff(int Op, bool bActiveHasArt, bool bDestHasArt, bool bTransformEqual)
{
    const int N = SyncOpNormalized(Op);
    const bool bTr = SyncOpHasTransform(N);
    const bool bTex = SyncOpHasTextures(N);
    if (bTex)
    {
        if (bActiveHasArt && !bDestHasArt) return SyncDestMissing;
        if (bActiveHasArt != bDestHasArt) return SyncDestDiffers;
    }
    if (bTr && !bTransformEqual) return SyncDestDiffers;
    return SyncDestSame;
}

// ============================================================================
// Phase D link-target mirrors: linked editing (the sync-row Link checkbox)
// broadcasts the edited canonical transform to the PICKED destination views
// (the state-strip checklist), excluding the active view. When no view is
// picked the broadcast falls back to all other views - the legacy
// GetLinkTargets behavior, so link+no-picks keeps the Phase B contract.
// ============================================================================
inline int FPLinkDestCount(const std::vector<int>& Picked, int ActiveIndex, int TotalViews)
{
    int PickedOther = 0;
    for (size_t i = 0; i < Picked.size() && (int)i < TotalViews; ++i)
        if (Picked[i] != 0 && (int)i != ActiveIndex) ++PickedOther;
    if (PickedOther > 0) return PickedOther;
    return TotalViews > 0 ? TotalViews - 1 : 0;  // fallback: all other views
}

inline bool FPLinkDestIsPicked(const std::vector<int>& Picked, int ActiveIndex, int View)
{
    if (View == ActiveIndex) return false;
    if (View < 0 || Picked.size() <= (size_t)View) return false;  // outside the checklist
    bool bAnyPicked = false;
    for (size_t i = 0; i < Picked.size(); ++i)
        if (Picked[i] != 0 && (int)i != ActiveIndex) bAnyPicked = true;
    if (!bAnyPicked) return true;  // fallback: link to every other view
    return Picked[(size_t)View] != 0;
}

// ============================================================================
// Phase E layer-badge mirror: each layer-tree row carries a completeness badge
// for the active view - 2 = Assigned (albedo+normal+depth), 1 = Partial,
// 0 = Missing. AssignCellLabel turns the AssignCellState cell into the badge
// tooltip/color key; any out-of-range cell reads as Missing.
// ============================================================================
inline const char* AssignCellLabel(int Cell)
{
    if (Cell == 2) return "Assigned";
    if (Cell == 1) return "Partial";
    return "Missing";
}

// ============================================================================
// Phase E pin-manager mirror: the manager lists one row per pinned item - the
// whole-layer pin (when pinned) plus every top-level element and child whose
// Pin3D.bPinned is set. FPPinnedRowCount totals the rows so the manager's
// header ("Pinned: k/n") and the mirror agree.
// ============================================================================
inline int FPPinnedRowCount(bool bLayerPin, const std::vector<int>& ElPins,
    const std::vector<std::vector<int> >& ChildPins)
{
    int Rows = bLayerPin ? 1 : 0;
    for (size_t i = 0; i < ElPins.size(); ++i)
    {
        if (ElPins[i] != 0) ++Rows;
        if (i < ChildPins.size())
            for (size_t c = 0; c < ChildPins[i].size(); ++c)
                if (ChildPins[i][c] != 0) ++Rows;
    }
    return Rows;
}

// ============================================================================
// Phase F undo-shortcut mirror: the widget's NativeOnKeyDown key table.
// Ctrl+Z -> 1 Undo, Ctrl+Shift+Z -> 2 Redo, Ctrl+Y -> 2 Redo, everything
// else -> 0 (unhandled). Matches the editor convention so the global editor
// undo stays untouched while the widget is focused elsewhere.
// ============================================================================
enum FUndoShortcut : int
{
    FUndoShortcutNone  = 0,
    FUndoShortcutUndo  = 1,
    FUndoShortcutRedo  = 2
};

inline int UndoShortcutAction(bool bControl, bool bShift, bool bKeyZ, bool bKeyY)
{
    if (!bControl) return FUndoShortcutNone;
    if (bKeyZ && !bShift) return FUndoShortcutUndo;
    if ((bKeyZ && bShift) || bKeyY) return FUndoShortcutRedo;
    return FUndoShortcutNone;
}

} // namespace FPLayout
