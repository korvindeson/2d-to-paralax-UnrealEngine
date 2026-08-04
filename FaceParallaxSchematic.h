// ============================================================================
// FaceParallaxSchematic.h — central-canvas part schematic + yaw-motion rules
// (redesign: per-part default view, front/base/back yaw classes).
//
// Pure C++17, no UE dependencies — the same contract style as
// FaceParallaxLayoutSpec.h. Synced into the runtime Public dir (component
// depth-class defaults) AND included directly by the math test harness, so
// the rule table is a single source of truth with pinned tests.
//
// Contents:
//   1. FPDepthClass — front / base / back motion classes.
//   2. DefaultPartSchematics() — the 17 part glyphs that form the canvas
//      DEFAULT VIEW: the 13 anatomical parts mirroring the hotspot region
//      geometry (BrowL..Neck — click parity with the parts strip), plus the
//      four silhouette parts the expanded base preset needs (Bangs, Hair,
//      BackHair, Head). Each glyph is a closed UV-space polygon (0..1,
//      y-down); parts whose layer has assigned art are skipped at paint time,
//      so "assigned art replaces the default outline" comes for free.
//   3. FPPartInOutline / FPSchematicPartAt / FPSchematicFindPart — glyph
//      hit-testing (same even-odd, boundary-inclusive rule as FPLayout).
//   4. FPDepthClassForTag — the 10-layer base-preset class table (deploy.py
//      LAYERS drives the same tags; the component applies these defaults in
//      SyncLayerDefinitionsFromPreset).
//   5. FPSchematicLayerAlias — part-name coverage aliases (Teeth -> Mouth,
//      Chin -> Head, Neck -> Head) so every part resolves to a base-preset
//      layer (CheekL/R and EarL/R already resolve via FPHotspotLayerMatch).
//   6. FPHairLayerSet / FPSchematicIsHairLayer — the hair system contract:
//      Bangs = front hair (Front), Hair + BackHair = back hair (Back).
//   7. FPSchematicFilterAllows — the canvas filter row's pure mirror
//      (layer multi-select + depth-class radio).
//   8. FPEdgeMap — the group-colored edge map contract: every part/layer
//      resolves to a visual GROUP (Eyes / Mouth / Hair / Surface), each group
//      has a distinct base color, and the depth class scales luminance so
//      FRONT reads lighter than BACK. Hair is a separate system: its three
//      layers are detailed LEVELS (Bangs=0, Hair=1, BackHair=2), it has its
//      own color distinct from every other group, and it can be toggled off
//      entirely (hair edges hidden while everything else stays).
//   9. FPYawRule — the front/base/back yaw-motion rule as a pure mirror of
//      UFaceParallaxComponent::ComputeOffsetForState (non-vertical branch):
//      offset = NormalizedYaw * DepthScale * (bInvertParallax ? -1 : 1)
//      * MaxParallaxOffset. The classes encode the rule as data:
//        Front = DepthScale 1.0, no invert  → moves WITH yaw (strongest).
//        Base  = DepthScale 0.15, no invert → anchored (near-zero residual,
//               the component has no dead-zone term, so "base" is a small
//               scale, not a hard lock).
//        Back  = DepthScale 1.0, invert     → moves AGAINST yaw (inverse).
//      TestPhaseSchematic pins the signs/magnitudes; the same header is what
//      the runtime component consults, so rule and engine cannot drift.
// ============================================================================
#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace FPSchematic {

enum class FPDepthClass : unsigned char
{
    Front,
    Base,
    Back,
    MAX
};

inline const char* FPDepthClassName(FPDepthClass C)
{
    switch (C)
    {
    case FPDepthClass::Front: return "Front";
    case FPDepthClass::Back:  return "Back";
    default:                  return "Base";
    }
}

struct FPSchematicPoint { double X = 0, Y = 0; };

struct FPSchematicPart
{
    FPSchematicPart() : Name(nullptr), DepthClass(FPDepthClass::Base) {}
    FPSchematicPart(const char* InName,
        std::vector<FPSchematicPoint> InOutline,
        FPDepthClass InClass)
        : Name(InName), Outline(std::move(InOutline)), DepthClass(InClass) {}

    const char* Name = nullptr;
    std::vector<FPSchematicPoint> Outline;
    FPDepthClass DepthClass = FPDepthClass::Base;
};

inline FPSchematicPoint SPT(double X, double Y) { return { X, Y }; }

// The 17-part default schematic. Part order = hit priority (first match
// wins), so the most specific glyphs (brows/eyes/.../chin) come before the
// silhouette buckets (Bangs, Hair, BackHair, Head). Teeth is listed BEFORE
// Mouth: the schematic glyphs have no hole concept, so the open-mouth hole
// points fall to Teeth first and the ring points fall to Mouth — the same
// first-match-wins result the region hole achieves (Mouth has the hole). The
// 13 anatomical parts reuse the exact hotspot-region coordinates so a
// schematic hit resolves to the same layer as the parts strip
// (ResolveHotspotLayer in the widget). Glyph classes follow the layer they
// resolve to: CheekL/R -> Cheeks (Front), Chin -> Head (Base), Teeth -> Mouth
// (Front), EarL/R -> Ears (Back). Depth classes follow the base-preset yaw
// rule: facial features move with yaw (Front), the head silhouette is
// anchored (Base), and hair/ears sit on the far side of the head so they
// move against yaw (Back).
inline std::vector<FPSchematicPart> DefaultPartSchematics()
{
    return {
        // Anime-girl line-art rework (aesthetic guide, front view): the head
        // stays a WIDE sphere with a soft continuous cheek curve (no
        // cheekbones) tapering to a BLUNTED, rounded V chin with a smooth jaw
        // transition; the eyes sit on the head's absolute vertical center with
        // a one-eye-width gap and carry a THICK upper-lash contour flaring
        // into two heavy curved spikes per eye (tapered tips; the interior
        // iris/catchlight/pupil detail is approximated by the shaped closed
        // loop since the schematic is outline data); the nose is a SHADOW-ONLY
        // caret/dash (a shallow V hint, not a physical bump) halfway between
        // the eye line and the chin; the mouth is slightly open with a center
        // gap splitting the line, lip-corner flicks reading as small
        // dots/upward ticks, much closer to the chin than the nose; each ear
        // spans the eye top to the nose bottom; thin sweeping brows wider than
        // the eyes arch over the bangs with tapered outer tails; the hair is
        // S-curved (never straight) with 3-4 flyaway spikes and crown ahoge
        // curls, and the top edge reads as a jagged hair-halo zig-zag over the
        // bangs/side hair (the bang wedges erase where they cross the eyes).
        // The hair stays an ANNULUS (outer mass + face cutout) so the face
        // interior never stacks hair, and the Mouth keeps its open-hole +
        // Teeth ring, so every probe, region-parity, stack-depth,
        // boundary-inclusive and cycle invariant still holds exactly.
        { "BrowL",  { SPT(0.20, 0.342), SPT(0.26, 0.300), SPT(0.33, 0.290), SPT(0.42, 0.320),
                      SPT(0.44, 0.340), SPT(0.40, 0.345), SPT(0.33, 0.318), SPT(0.27, 0.322),
                      SPT(0.205, 0.348) }, FPDepthClass::Front },
        { "BrowR",  { SPT(0.80, 0.342), SPT(0.74, 0.300), SPT(0.67, 0.290), SPT(0.58, 0.320),
                      SPT(0.56, 0.340), SPT(0.60, 0.345), SPT(0.67, 0.318), SPT(0.73, 0.322),
                      SPT(0.795, 0.348) }, FPDepthClass::Front },
        { "EyeL",   { SPT(0.245, 0.43), SPT(0.29, 0.372), SPT(0.315, 0.350), SPT(0.35, 0.368),
                      SPT(0.38, 0.352), SPT(0.395, 0.402), SPT(0.40, 0.43), SPT(0.375, 0.52),
                      SPT(0.30, 0.525), SPT(0.245, 0.43) }, FPDepthClass::Front },
        { "EyeR",   { SPT(0.755, 0.43), SPT(0.71, 0.372), SPT(0.685, 0.350), SPT(0.65, 0.368),
                      SPT(0.62, 0.352), SPT(0.605, 0.402), SPT(0.60, 0.43), SPT(0.625, 0.52),
                      SPT(0.70, 0.525), SPT(0.755, 0.43) }, FPDepthClass::Front },
        { "Nose",   { SPT(0.44, 0.63), SPT(0.50, 0.61), SPT(0.56, 0.63), SPT(0.545, 0.665),
                      SPT(0.50, 0.685), SPT(0.455, 0.665) }, FPDepthClass::Front },
        { "CheekL", { SPT(0.075, 0.46), SPT(0.13, 0.425), SPT(0.24, 0.50), SPT(0.28, 0.60),
                      SPT(0.25, 0.72), SPT(0.17, 0.76), SPT(0.10, 0.71), SPT(0.075, 0.62) },
                      FPDepthClass::Front },
        { "CheekR", { SPT(0.925, 0.46), SPT(0.87, 0.425), SPT(0.76, 0.50), SPT(0.72, 0.60),
                      SPT(0.75, 0.72), SPT(0.83, 0.76), SPT(0.90, 0.71), SPT(0.925, 0.62) },
                      FPDepthClass::Front },
        { "Teeth",  { SPT(0.46, 0.775), SPT(0.54, 0.775), SPT(0.56, 0.79), SPT(0.50, 0.80),
                      SPT(0.44, 0.79) }, FPDepthClass::Front },
        { "Mouth",  { SPT(0.40, 0.745), SPT(0.42, 0.738), SPT(0.50, 0.735), SPT(0.58, 0.738),
                      SPT(0.60, 0.745), SPT(0.605, 0.770), SPT(0.56, 0.815), SPT(0.50, 0.825),
                      SPT(0.44, 0.815), SPT(0.395, 0.770) }, FPDepthClass::Front },
        { "Chin",   { SPT(0.42, 0.815), SPT(0.58, 0.815), SPT(0.545, 0.845), SPT(0.52, 0.852),
                      SPT(0.50, 0.855), SPT(0.48, 0.852), SPT(0.455, 0.845) }, FPDepthClass::Base },
        { "EarL",   { SPT(0.02, 0.42), SPT(0.06, 0.36), SPT(0.11, 0.42), SPT(0.105, 0.60),
                      SPT(0.06, 0.68), SPT(0.03, 0.60) }, FPDepthClass::Back },
        { "EarR",   { SPT(0.98, 0.42), SPT(0.94, 0.36), SPT(0.89, 0.42), SPT(0.895, 0.60),
                      SPT(0.94, 0.68), SPT(0.97, 0.60) }, FPDepthClass::Back },
        { "Neck",   { SPT(0.43, 0.86), SPT(0.57, 0.86), SPT(0.68, 0.98), SPT(0.32, 0.98) },
                      FPDepthClass::Base },
        { "Bangs",  { SPT(0.22, 0.34), SPT(0.19, 0.12), SPT(0.24, 0.04), SPT(0.28, 0.03),
                      SPT(0.33, 0.02), SPT(0.37, 0.11), SPT(0.40, 0.03), SPT(0.45, 0.02),
                      SPT(0.50, 0.02), SPT(0.55, 0.02), SPT(0.60, 0.03), SPT(0.63, 0.11),
                      SPT(0.67, 0.02), SPT(0.72, 0.03), SPT(0.76, 0.04), SPT(0.81, 0.12),
                      SPT(0.78, 0.34), SPT(0.72, 0.28), SPT(0.65, 0.32), SPT(0.58, 0.30),
                      SPT(0.50, 0.35), SPT(0.42, 0.30), SPT(0.35, 0.32), SPT(0.28, 0.28) },
                      FPDepthClass::Front },
        { "Hair",   { SPT(0.03, 0.52), SPT(0.045, 0.28), SPT(0.05, 0.18), SPT(0.06, 0.13),
                      SPT(0.09, 0.09), SPT(0.13, 0.05), SPT(0.18, 0.03), SPT(0.24, 0.012),
                      SPT(0.28, 0.008), SPT(0.31, 0.005), SPT(0.36, 0.012), SPT(0.42, 0.010),
                      SPT(0.44, 0.004), SPT(0.47, 0.012), SPT(0.50, 0.010), SPT(0.53, 0.012),
                      SPT(0.56, 0.004), SPT(0.58, 0.010), SPT(0.64, 0.012), SPT(0.69, 0.005),
                      SPT(0.72, 0.008), SPT(0.76, 0.012), SPT(0.82, 0.03), SPT(0.87, 0.05),
                      SPT(0.91, 0.09), SPT(0.94, 0.13), SPT(0.95, 0.18), SPT(0.955, 0.28),
                      SPT(0.97, 0.52), SPT(0.90, 0.36), SPT(0.87, 0.30), SPT(0.84, 0.20),
                      SPT(0.80, 0.12), SPT(0.72, 0.07), SPT(0.64, 0.05), SPT(0.58, 0.09),
                      SPT(0.54, 0.06), SPT(0.50, 0.05), SPT(0.46, 0.06), SPT(0.42, 0.09),
                      SPT(0.36, 0.05), SPT(0.28, 0.07), SPT(0.20, 0.12), SPT(0.16, 0.20),
                      SPT(0.13, 0.30), SPT(0.10, 0.36), SPT(0.05, 0.55) }, FPDepthClass::Back },
        { "BackHair", { SPT(0.18, 0.90), SPT(0.30, 0.82), SPT(0.44, 0.80), SPT(0.56, 0.80),
                      SPT(0.70, 0.82), SPT(0.82, 0.90), SPT(0.74, 0.96), SPT(0.26, 0.96) },
                      FPDepthClass::Back },
        { "Head",   { SPT(0.50, 0.02), SPT(0.29, 0.076), SPT(0.136, 0.23), SPT(0.08, 0.44),
                      SPT(0.19, 0.61), SPT(0.32, 0.74), SPT(0.50, 0.86), SPT(0.68, 0.74),
                      SPT(0.81, 0.61), SPT(0.92, 0.44), SPT(0.864, 0.23), SPT(0.71, 0.076) },
                      FPDepthClass::Base },
    };
}

// Distance from P to segment AB (squared); -1 when AB is degenerate.
inline double FPPartSegDist2(double PX, double PY, double AX, double AY, double BX, double BY)
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

// Boundary-inclusive point-in-polygon (even-odd rule) over schematic points —
// same semantics as FPLayout::FPPointInPolygon so glyph hits and region hits
// agree wherever the geometry overlaps.
inline bool FPPartInOutline(double X, double Y, const std::vector<FPSchematicPoint>& Pts)
{
    constexpr double EpsDist = 1e-9;
    constexpr double EpsDist2 = EpsDist * EpsDist;
    if (Pts.size() == 1)
    {
        const double Dx = X - Pts[0].X, Dy = Y - Pts[0].Y;
        return Dx * Dx + Dy * Dy <= EpsDist2;
    }
    bool bInside = false;
    for (size_t i = 0, j = Pts.size() - 1; i < Pts.size(); j = i++)
    {
        const double AX = Pts[j].X, AY = Pts[j].Y;
        const double BX = Pts[i].X, BY = Pts[i].Y;
        const double D = FPPartSegDist2(X, Y, AX, AY, BX, BY);
        if (D >= 0.0 && D <= EpsDist2) return true;
        if ((AY > Y) != (BY > Y))
        {
            const double Xint = AX + (Y - AY) * (BX - AX) / (BY - AY);
            if (Xint > X) bInside = !bInside;
        }
    }
    return bInside;
}

// First-match-wins part lookup by UV point; nullptr on miss.
inline const FPSchematicPart* FPSchematicPartAt(
    const std::vector<FPSchematicPart>& Parts, double X, double Y)
{
    for (const FPSchematicPart& P : Parts)
    {
        if (P.Name && !P.Outline.empty() && FPPartInOutline(X, Y, P.Outline))
        {
            return &P;
        }
    }
    return nullptr;
}

// Number of schematic parts whose glyphs contain the point (stack depth).
// Part order is hit priority, so the stack enumerates topmost-first.
inline int FPSchematicPartStackCount(
    const std::vector<FPSchematicPart>& Parts, double X, double Y)
{
    int N = 0;
    for (const FPSchematicPart& P : Parts)
    {
        if (P.Name && !P.Outline.empty() && FPPartInOutline(X, Y, P.Outline))
            ++N;
    }
    return N;
}

// Nth-overlap part lookup (W2 cycle-through-stack): returns the part at
// stack index CycleIdx (0 = topmost, same as FPSchematicPartAt). The index is
// wrapped mod the stack depth so a repeated click cycles through every stacked
// glyph and returns to the top; nullptr only when nothing overlaps at all.
// This is the pure disambiguation primitive for stacked regions (e.g. Teeth
// under Mouth, Eyes under Bangs) — repeated clicks on one pixel walk the stack.
inline const FPSchematicPart* FPSchematicPartCycleAt(
    const std::vector<FPSchematicPart>& Parts, double X, double Y, int CycleIdx)
{
    const int Stack = FPSchematicPartStackCount(Parts, X, Y);
    if (Stack <= 0) return nullptr;
    int Wrapped = CycleIdx % Stack;
    if (Wrapped < 0) Wrapped += Stack;
    int Seen = 0;
    for (const FPSchematicPart& P : Parts)
    {
        if (P.Name && !P.Outline.empty() && FPPartInOutline(X, Y, P.Outline))
        {
            if (Seen == Wrapped) return &P;
            ++Seen;
        }
    }
    return nullptr;
}

// Named lookup; nullptr on miss.
inline const FPSchematicPart* FPSchematicFindPart(
    const std::vector<FPSchematicPart>& Parts, const char* Name)
{
    if (!Name || !Name[0]) return nullptr;
    for (const FPSchematicPart& P : Parts)
    {
        if (P.Name && std::string(P.Name) == Name) return &P;
    }
    return nullptr;
}

// ============================================================================
// Base-preset depth-class table (the 10 layers deploy.py builds). This is the
// single mapping the component uses when it seeds layer definitions from the
// preset (SyncLayerDefinitionsFromPreset), and it is what the math tests pin.
// Unknown tags fall back to Base (anchored — the safe default for arbitrary
// user layers).
// ============================================================================
struct FPTagClass { const char* Tag; FPDepthClass Class; };

inline const FPTagClass* FPTagClassForTag(const char* Tag)
{
    static const FPTagClass Table[] = {
        { "Eyes",     FPDepthClass::Front },
        { "Brows",    FPDepthClass::Front },
        { "Mouth",    FPDepthClass::Front },
        { "Bangs",    FPDepthClass::Front },
        { "Nose",     FPDepthClass::Front },
        { "Cheeks",   FPDepthClass::Front },
        { "Head",     FPDepthClass::Base  },
        { "Hair",     FPDepthClass::Back  },
        { "BackHair", FPDepthClass::Back  },
        { "Ears",     FPDepthClass::Back  },
    };
    if (!Tag || !Tag[0]) return nullptr;
    for (const FPTagClass& Entry : Table)
    {
        if (std::string(Entry.Tag) == Tag) return &Entry;
    }
    return nullptr;
}

inline FPDepthClass FPDepthClassForTag(const char* Tag)
{
    const FPTagClass* Entry = FPTagClassForTag(Tag);
    return Entry ? Entry->Class : FPDepthClass::Base;
}

// The 10 base-preset layer tags in deploy.py order (the canonical coverage
// set — every one of the 17 parts resolves to one of these).
inline const std::vector<std::string>& FPSchematicLayerSet()
{
    static const std::vector<std::string> Layers = {
        "Eyes", "Brows", "Mouth", "Bangs", "Nose", "Cheeks",
        "Head", "Hair", "BackHair", "Ears"
    };
    return Layers;
}

// ============================================================================
// Part-name coverage aliases (Phase 2). FPHotspotLayerMatch resolves
// CheekL/CheekR -> Cheeks and EarL/EarR -> Ears by derivation once those
// layers exist; the remaining three parts need explicit aliases so EVERY
// schematic part maps to a base-preset layer (all parts clickable, all parts
// assignable through the Import Folder Wizard).
// ============================================================================
inline const char* FPSchematicLayerAlias(const char* PartName)
{
    if (!PartName || !PartName[0]) return nullptr;
    if (std::string(PartName) == "Teeth") return "Mouth";
    if (std::string(PartName) == "Chin")  return "Head";
    if (std::string(PartName) == "Neck")  return "Head";
    return nullptr;
}

// ============================================================================
// Hair system contract (Phase 2): the three hair layers and their motion
// classes. Bangs = front hair (moves WITH yaw — Front class); Hair + BackHair
// = back hair (move AGAINST yaw — Back class). The camera-sync, auto-fit,
// bulk-assign, nested-pin, visibility and problems-panel integrations all
// operate on these layers through the normal per-layer pipeline — this set
// only pins WHICH layers are hair and WHAT class they carry.
// ============================================================================
inline const std::vector<std::string>& FPHairLayerSet()
{
    static const std::vector<std::string> Layers = { "Bangs", "Hair", "BackHair" };
    return Layers;
}

inline bool FPSchematicIsHairLayer(const char* Tag)
{
    if (!Tag || !Tag[0]) return false;
    for (const std::string& L : FPHairLayerSet())
        if (L == Tag) return true;
    return false;
}

// ============================================================================
// Hair-chain jiggle midpoint ramp (Phase 7): given chain progress [0..1] from
// the hair root to the tip, return the blend toward the End* spring fields.
//   0.0 below/at Midpoint (base spring params), 1.0 at the tip (End* params),
//   smoothstep between. Midpoint >= 1.0 disables the split (identity, legacy
//   uniform spring). ChainProgress is clamped to [0..1] defensively.
// ============================================================================
inline double FPHairSegmentRamp(double Midpoint, double ChainProgress)
{
    if (Midpoint >= 1.0) return 0.0;              // feature disabled
    const double T = ChainProgress < 0.0 ? 0.0 : (ChainProgress > 1.0 ? 1.0 : ChainProgress);
    if (T <= Midpoint) return 0.0;                // root side of the midpoint
    if (T >= 1.0) return 1.0;                     // tip side fully blends
    const double X = (T - Midpoint) / (1.0 - Midpoint); // 0..1 across the split zone
    return X * X * (3.0 - 2.0 * X);               // smoothstep
}

// Blend one scalar between its base and end values by the midpoint ramp.
inline double FPHairSegmentBlend(double Base, double End, double Ramp)
{
    return Base + (End - Base) * Ramp;
}

// ============================================================================
// Canvas filter row mirror (Phase 3): does a part glyph pass the filters?
//   DepthFilter: 0 = all classes, 1 = Front, 2 = Base, 3 = Back.
//   LayerFilter: empty = all layers; otherwise the part's RESOLVED layer must
//   be listed. Unmapped parts (empty ResolvedLayerTag) show only when the
//   layer filter is empty — there is no layer to select for them.
// ============================================================================
inline bool FPSchematicFilterAllows(FPDepthClass Cls, const char* ResolvedLayerTag,
    const std::vector<std::string>& LayerFilter, int DepthFilter)
{
    if (DepthFilter != 0 && (int)Cls + 1 != DepthFilter) return false;
    if (!LayerFilter.empty())
    {
        if (!ResolvedLayerTag || !ResolvedLayerTag[0]) return false;
        bool bFound = false;
        for (const std::string& L : LayerFilter)
            if (L == ResolvedLayerTag) { bFound = true; break; }
        if (!bFound) return false;
    }
    return true;
}

// ============================================================================
// Edge-map group contract (Phase I): the part edge map colors every glyph by
// its visual GROUP — eyes and mouth get distinct hues, the depth class scales
// the LUMINANCE (front lighter, back darker), and hair is its own system:
// three detailed levels (Bangs = 0, Hair = 1, BackHair = 2) with its own
// color distinct from everything else, toggleable off wholesale. Pure mirror
// of what SFaceSchematicLayer::OnPaint draws; the widget resolves a part to
// its group via the same aliases FPSchematicLayerAlias provides, so paint
// and test can never drift.
// ============================================================================
enum class FPEdgeGroup : unsigned char
{
    Eyes,    // brows + eyes (EyeL/EyeR/BrowL/BrowR + Eyes/Brows layers)
    Mouth,   // mouth + teeth (Mouth/Teeth + Mouth layer)
    Hair,    // the hair system (Bangs/Hair/BackHair + Bangs/Hair/BackHair layers)
    Surface, // everything else (silhouette + residual anatomy)
    MAX
};

inline const char* FPEdgeGroupName(FPEdgeGroup G)
{
    switch (G)
    {
    case FPEdgeGroup::Eyes:   return "Eyes";
    case FPEdgeGroup::Mouth:  return "Mouth";
    case FPEdgeGroup::Hair:   return "Hair";
    default:                  return "Surface";
    }
}

// Part name -> group. Uses the same alias table as layer resolution
// (Teeth -> Mouth) so the schematic's 17 glyphs all land in a group.
inline FPEdgeGroup FPEdgeGroupForPartName(const char* Name)
{
    if (!Name || !Name[0]) return FPEdgeGroup::Surface;
    if (std::string(Name) == "EyeL" || std::string(Name) == "EyeR"
        || std::string(Name) == "BrowL" || std::string(Name) == "BrowR")
        return FPEdgeGroup::Eyes;
    if (std::string(Name) == "Mouth"
        || (FPSchematicLayerAlias(Name) && std::string(FPSchematicLayerAlias(Name)) == "Mouth"))
        return FPEdgeGroup::Mouth;
    if (FPSchematicIsHairLayer(Name)) return FPEdgeGroup::Hair;
    return FPEdgeGroup::Surface;
}

// Resolved layer tag -> group. The hair set check keeps the three hair
// layers in their own group regardless of their depth class.
inline FPEdgeGroup FPEdgeGroupForTag(const char* Tag)
{
    if (!Tag || !Tag[0]) return FPEdgeGroup::Surface;
    if (std::string(Tag) == "Eyes" || std::string(Tag) == "Brows")
        return FPEdgeGroup::Eyes;
    if (std::string(Tag) == "Mouth") return FPEdgeGroup::Mouth;
    if (FPSchematicIsHairLayer(Tag)) return FPEdgeGroup::Hair;
    return FPEdgeGroup::Surface;
}

// Hair detail level: Bangs = 0 (front), Hair = 1, BackHair = 2; -1 for
// non-hair. The hair system's "detailed levels" — three layers, each with
// its own edges — are addressable by this level so the UI can dim/emphasize
// per level.
inline int FPHairLevelForTag(const char* Tag)
{
    if (!Tag || !Tag[0]) return -1;
    if (std::string(Tag) == "Bangs")    return 0;
    if (std::string(Tag) == "Hair")     return 1;
    if (std::string(Tag) == "BackHair") return 2;
    return -1;
}

inline int FPHairLevelForPartName(const char* Name)
{
    return FPHairLevelForTag(Name);
}

// Per-level luminance for the hair system (front lighter than back, the
// same rule as the depth classes): Bangs (0) = full luminance (lightest),
// Hair (1) = mid, BackHair (2) = dimmed (darkest); non-hair (-1) = full.
// The three detailed levels stay in the hair's own color family while each
// level's edges read as a distinct step — level drives the brightness, the
// depth class never dims hair.
inline double FPHairLevelLuminance(int Level)
{
    switch (Level)
    {
    case 0:  return 1.0;   // Bangs: front hair — full luminance (lightest)
    case 1:  return 0.72;  // Hair: mid level
    case 2:  return 0.45;  // BackHair: back hair — dimmed (darkest)
    default: return 1.0;   // non-hair: no level — full luminance
    }
}

// Luminance scale per depth class: FRONT is LIGHTER than BACK (the edge-map
// brightness rule). Base sits between; hair is exempt (it carries its own
// per-level color) so the toggle is pure and the tests can pin the order.
inline double FPEdgeLuminanceForClass(FPDepthClass C)
{
    switch (C)
    {
    case FPDepthClass::Front: return 1.0;   // front: full luminance (lightest)
    case FPDepthClass::Back:  return 0.45;  // back: dimmed (darkest)
    default:                  return 0.72;  // base: mid luminance
    }
}

// Group -> base edge color (RGB 0..1, y-up friendly hues). Eyes and Mouth
// are the two named facial-feature groups; Surface is the neutral silhouette
// grey; Hair gets a color DISTINCT from every other group.
struct FPEdgeColor { double R = 0, G = 0, B = 0; };

inline FPEdgeColor FPEdgeGroupColor(FPEdgeGroup G)
{
    switch (G)
    {
    case FPEdgeGroup::Eyes:   return { 0.35, 0.85, 0.40 };  // green — eyes
    case FPEdgeGroup::Mouth:  return { 0.95, 0.45, 0.45 };  // red — mouth
    case FPEdgeGroup::Hair:   return { 0.85, 0.55, 0.95 };  // violet — hair (distinct)
    default:                  return { 0.60, 0.63, 0.68 };  // grey-blue — surface
    }
}

// Final edge color: group base color scaled by the depth-class luminance
// (front lighter than back). Hair parts ignore the class scale — their
// color is driven by the hair DETAIL LEVEL instead (FPHairLevelLuminance:
// Bangs lightest, BackHair darkest), so the hair system's three detailed
// levels stay recognizable within its own distinct color family.
inline FPEdgeColor FPEdgeColorForPart(const char* Name, FPDepthClass C)
{
    const FPEdgeGroup G = FPEdgeGroupForPartName(Name);
    const double Lum = (G == FPEdgeGroup::Hair)
        ? FPHairLevelLuminance(FPHairLevelForPartName(Name))
        : FPEdgeLuminanceForClass(C);
    const FPEdgeColor Base = FPEdgeGroupColor(G);
    return { Base.R * Lum, Base.G * Lum, Base.B * Lum };
}

// Edge-map visibility: hair edges can be toggled off wholesale; every other
// group is always visible.
inline bool FPEdgeMapShows(FPEdgeGroup G, bool bHairEdgesVisible)
{
    return G != FPEdgeGroup::Hair || bHairEdgesVisible;
}


struct FPYawRule
{
    // Mirror of UFaceParallaxComponent::MaxParallaxOffset (component default).
    static constexpr double MaxOffset = 5.0;

    // Per-class DepthScale the component applies to new layer definitions.
    static constexpr double FrontDepthScale = 1.0;
    static constexpr double BaseDepthScale = 0.15;   // anchored: residual motion only
    static constexpr double BackDepthScale = 1.0;

    static inline double ClampYaw(double V)
    {
        return V < -1.0 ? -1.0 : (V > 1.0 ? 1.0 : V);
    }

    // Mirror of UFaceParallaxComponent::ComputeOffsetForState (non-vertical
    // branch): NormalizedYaw * DepthScale * (invert ? -1 : 1) * MaxOffset.
    static inline double ComputeYawOffset(double DepthScale, bool bInvertParallax,
        double NormalizedYaw, double MaxOffsetV = MaxOffset)
    {
        const double DepthFactor = DepthScale * (bInvertParallax ? -1.0 : 1.0);
        return ClampYaw(NormalizedYaw) * DepthFactor * MaxOffsetV;
    }

    static inline double DepthScaleForClass(FPDepthClass C)
    {
        switch (C)
        {
        case FPDepthClass::Front: return FrontDepthScale;
        case FPDepthClass::Back:  return BackDepthScale;
        default:                  return BaseDepthScale;
        }
    }

    static inline bool InvertsParallaxForClass(FPDepthClass C)
    {
        return C == FPDepthClass::Back;
    }

    static inline double DepthScaleForTag(const char* Tag)
    {
        return DepthScaleForClass(FPDepthClassForTag(Tag));
    }

    static inline bool InvertsParallaxForTag(const char* Tag)
    {
        return InvertsParallaxForClass(FPDepthClassForTag(Tag));
    }

    // The rule in one call: front moves WITH yaw, back mirrors it, base is
    // anchored (|offset| < front's at every yaw).
    static inline double ApplyClass(FPDepthClass C, double NormalizedYaw)
    {
        return ComputeYawOffset(DepthScaleForClass(C), InvertsParallaxForClass(C), NormalizedYaw);
    }
};

// ============================================================================
// Phase B/C: the billboard "turn to face the camera" contract. The canvas
// placeholder used to be a static FRONT glyph set that a continuous
// 3D-projection formula (arc-based foreshortening, per-glyph centroid warping)
// rotated into every view. That projection is gone: real 2D art is a set of
// flat, billboarded layers, so the placeholder does the same — every one of
// the 8 yaw states + Top/Bottom resolves to its OWN authored 2D layout and
// the left half of the turn is the horizontal mirror of the right. Scrubbing
// blends between neighboring state layouts (a pure 2D morph, never a
// projection), so the outline reads as 2D art FLIPPING as the head turns:
//
//   Billboard rule:          every part is a flat card facing the camera —
//                            the surface normal never turns edge-on, never
//                            foreshortens paper-thin; the TURN is faked by
//                            sliding the flat layers against each other.
//   Z-depth hierarchy:       5 flat planes, closest -> farthest —
//                            1 Nose (tip) / Front Bangs
//                            2 Near Eyelash / Iris / Eyebrow / Mouth
//                            3 Face Base (cranium + jaw) + the far-side eye
//                            4 Near Ear / Side Hair
//                            5 Neck / Back Hair
//   Camera translation:      layers slide OPPOSITE the camera orbit; the
//                            closest Z slides furthest, the farthest trails.
//   Yaw:                     layers slide toward the far edge as the camera
//                            orbits; the far-side pair of a paired part
//                            compresses to zero by the 90° profile (one
//                            eye/ear/cheek) and STAYS folded through the back;
//                            features fade past the 135° back-corner;
//                            silhouettes survive the back (back hair, full
//                            width).
//   Pitch:                   Top view sinks the features + hair DOWN onto the
//                            face (+Y), lifts the ears + the V-chin UP
//                            (tucking them under), squashes vertically;
//                            Bottom view drops the chin / reveals the
//                            under-jaw and neck, raises the features + bangs.
//   Flip thresholds:         45° = Front->3/4, 90° = 3/4->Profile,
//                            135° = Profile->Back-corner, 180° = Back,
//                            ±45° = Top/Bottom — each exact state center
//                            resolves to ITS OWN authored 2D layout.
//
// All results are clamped into [0,1]^2 and keep the front glyph's point count
// (per-view layer transforms rely on that).
// ============================================================================

struct FPOrientationParams
{
    // Profile silhouette width (about the head centerline): the 2D side view
    // of the head is this fraction of the front/back width.
    static constexpr double SilAtProfile = 0.55;
    // Vertical squash scale at top/bottom (rigid about the head centerline).
    static constexpr double PitchAtExtreme = 0.70;

    // Yaw far-edge slide peaks (UV units at the profile). The face content
    // slides toward the far edge as the camera orbits (opposite the camera),
    // and the closest Z slides furthest:
    //   Z-1 Nose / Front Bangs     0.18   (the nose darts toward the far edge)
    //   Z-2 Near Features          0.12
    //   Z-3 Face Base              0.02   (near-static, stays on-camera)
    //   Z-4 Ear / Side Hair        0.06
    //   Z-5 Neck / Back Hair       0.00   (the backdrop never slides)
    static constexpr double NoseSlide     = 0.18;
    static constexpr double FeatureSlide  = 0.12;
    static constexpr double FaceBaseSlide = 0.02;
    static constexpr double EarSlide      = 0.06;
    static constexpr double FarSlide      = 0.0;

    // Pitch vertical-shift magnitudes (UV units at ±90° pitch, +Y = down).
    // Features + hair ENCROACH (down at the top view, up at the bottom); the
    // ears + V-chin COUNTER-translate (up at the top — ear tops rise above the
    // eye line, the chin tucks under the cheeks — down at the bottom); the
    // face base stays near-static (its contour SWAPS at the ±45 thresholds).
    static constexpr double NosePitch     = 0.18;
    static constexpr double FeaturePitch  = 0.12;
    static constexpr double FarPitch      = 0.06;
    static constexpr double EarPitch      = 0.08;
    static constexpr double ChinPitch     = 0.08;
    static constexpr double FaceBasePitch = 0.02;
};

// Normalized rotation factor: yaw/90 clamped to [-1,1] (1 = profile right).
inline double FPOrientationRotFactor(double YawDeg)
{
    double N = YawDeg / 90.0;
    return N < -1.0 ? -1.0 : (N > 1.0 ? 1.0 : N);
}

// Piecewise 2D-layout ramp. Control points sit on the exact state centers of
// the right half of the turn (0/45/90/135/180); the segment interpolation is
// smoothstepped so rotation reads 3D-smooth (ease in/out) while each exact
// state center still resolves to its OWN authored 2D layout (the flip).
struct FPRampPoint { double X; double V; };
inline double FPRampEval(const FPRampPoint* Pts, int N, double X)
{
    if (N <= 0) return 0.0;
    if (X <= Pts[0].X) return Pts[0].V;
    if (X >= Pts[N - 1].X) return Pts[N - 1].V;
    for (int i = 1; i < N; ++i)
    {
        if (X <= Pts[i].X)
        {
            const double D = Pts[i].X - Pts[i - 1].X;
            double T = D > 0.0 ? (X - Pts[i - 1].X) / D : 0.0;
            T = T * T * (3.0 - 2.0 * T);          // smoothstep: ease like a turn
            return Pts[i - 1].V + (Pts[i].V - Pts[i - 1].V) * T;
        }
    }
    return Pts[N - 1].V;
}

// Five-level Z-depth hierarchy (the billboard contract's stacking), closest ->
// farthest. Flat layers slide against each other (closest furthest, farthest
// anchored) to fake the turn.
enum class FPZDepth : unsigned char
{
    Closest      = 1,   // Nose (tip) / Front Bangs
    NearFeatures = 2,   // Near Eyelash / Iris / Eyebrow / Mouth
    FaceBase     = 3,   // Cranium + Jaw contour (far eye/brow sit here when far)
    EarSideHair  = 4,   // Near Ear / Side Hair
    Farthest     = 5    // Neck / Back Hair
};

// Map a part name to its Z-depth plane.
inline FPZDepth FPZDepthForPart(const char* Name)
{
    if (!Name || !Name[0]) return FPZDepth::FaceBase;
    const std::string N(Name);
    if (N == "Nose" || N == "Bangs") return FPZDepth::Closest;
    if (N == "EyeL" || N == "EyeR" || N == "BrowL" || N == "BrowR"
        || N == "Teeth" || N == "Mouth") return FPZDepth::NearFeatures;
    if (N == "Head" || N == "Chin" || N == "CheekL" || N == "CheekR")
        return FPZDepth::FaceBase;
    if (N == "EarL" || N == "EarR" || N == "Hair") return FPZDepth::EarSideHair;
    return FPZDepth::Farthest;   // Neck, BackHair
}

// Peak far-edge slide (UV units at the profile) for a Z-depth plane.
inline double FPYawSlidePeak(FPZDepth L)
{
    switch (L)
    {
    case FPZDepth::Closest:      return FPOrientationParams::NoseSlide;
    case FPZDepth::NearFeatures: return FPOrientationParams::FeatureSlide;
    case FPZDepth::EarSideHair:  return FPOrientationParams::EarSlide;
    case FPZDepth::Farthest:     return FPOrientationParams::FarSlide;
    default:                     return FPOrientationParams::FaceBaseSlide;
    }
}

// Far-edge slide magnitude (UV units) at |yaw|: grows through the 3/4, peaks
// at the profile, holds into the back-corner, and releases at the true back
// (the backdrop re-centers) — the camera-translation parallax of the flat
// layers, closest Z sliding furthest.
inline double FPYawSlideAt(FPZDepth L, double YawAbs)
{
    static const FPRampPoint K[] = { {0, 0.0}, {45, 0.62}, {90, 1.0},
                                     {135, 1.0}, {180, 0.0} };
    return FPYawSlidePeak(L) * FPRampEval(K, 5, YawAbs);
}

// Vertical parallax ROLE: features + hair ENCROACH on the face (down at the
// top view, up at the bottom), the ears + V-chin COUNTER-translate, and the
// face base stays near-static (its contour swaps at the ±45 thresholds).
enum class FPPitchRole : unsigned char
{
    Encroach,   // Nose, Bangs, Eyes, Brows, Teeth, Mouth, Hair, BackHair, Neck
    Counter,    // EarL/R (rise at top), Chin (tucks at top)
    FaceBase    // Head, CheekL/R (near-static)
};

inline FPPitchRole FPPitchRoleForPart(const char* Name)
{
    if (!Name || !Name[0]) return FPPitchRole::FaceBase;
    const std::string N(Name);
    if (N == "EarL" || N == "EarR" || N == "Chin") return FPPitchRole::Counter;
    if (N == "Head" || N == "CheekL" || N == "CheekR") return FPPitchRole::FaceBase;
    return FPPitchRole::Encroach;
}

inline double FPPitchMagnitude(const char* Name)
{
    if (!Name || !Name[0]) return FPOrientationParams::FaceBasePitch;
    const std::string N(Name);
    if (N == "Chin") return FPOrientationParams::ChinPitch;
    if (N == "EarL" || N == "EarR") return FPOrientationParams::EarPitch;
    if (N == "Hair" || N == "BackHair" || N == "Neck")
        return FPOrientationParams::FarPitch;
    if (N == "Head" || N == "CheekL" || N == "CheekR")
        return FPOrientationParams::FaceBasePitch;
    return (FPZDepthForPart(Name) == FPZDepth::Closest)
        ? FPOrientationParams::NosePitch : FPOrientationParams::FeaturePitch;
}

// Vertical (pitch) shift in UV units (Phase C up/down parallax, +Y = down).
// Positive pitch (Top view) sinks the features + hair DOWN onto the face
// (+Y), lifts the ears + V-chin UP (-Y, tucking them under); negative pitch
// (Bottom view) mirrors — the chin drops and the neck/under-jaw shows. The
// face base is near-static (its authored contour swaps at ±45).
inline double FPOrientationVerticalShift(const char* Name, double PitchDeg)
{
    double N = PitchDeg / 90.0;
    N = N < -1.0 ? -1.0 : (N > 1.0 ? 1.0 : N);
    return (FPPitchRoleForPart(Name) == FPPitchRole::Counter)
        ? -FPPitchMagnitude(Name) * N
        : FPPitchMagnitude(Name) * N;
}

// Vertical height scale for pitch (top/bottom squash toward the head
// centerline, rigid 2D; 0 pitch keeps full height).
inline double FPOrientationPitchScale(double PitchDeg)
{
    double P = PitchDeg < -90.0 ? -90.0 : (PitchDeg > 90.0 ? 90.0 : PitchDeg);
    const double F = P < 0.0 ? -P : P;
    return 1.0 - (1.0 - FPOrientationParams::PitchAtExtreme) * (F / 90.0);
}

// Is a paired part on the rotation's far side? Paired parts end in 'L' or
// 'R'; positive yaw turns the face to the RIGHT, so LEFT parts become the far
// side and fold at the right profile (negative yaw mirrors).
inline bool FPSchematicIsFarSide(const char* Name, double YawDeg)
{
    if (!Name || !Name[0]) return false;
    const std::string N(Name);
    const char Last = N[N.size() - 1];
    if (Last == 'L') return YawDeg > 0.0;
    if (Last == 'R') return YawDeg < 0.0;
    return false;
}

// Is the part a left/right paired part at all (uppercase L/R suffix)?
inline bool FPSchematicIsPairedPart(const char* Name)
{
    if (!Name || !Name[0]) return false;
    const std::string N(Name);
    const char Last = N[N.size() - 1];
    return Last == 'L' || Last == 'R';
}

// Is the part a silhouette (head + the three hair layers)? Silhouettes never
// fade at back orientations and scale rigidly about the head centerline.
inline bool FPSchematicIsSilhouette(const char* Name)
{
    return Name && Name[0] && (std::string(Name) == "Head"
        || FPSchematicIsHairLayer(Name));
}

// ============================================================================
// Phase 1: anchor classification. Is a layer LOAD-BEARING for the outline read
// at every angle? ANCHOR-critical layers (the head + hair silhouettes and the
// ears) define the silhouette itself — when they pop, lag or blur the whole
// read looks wrong, so a large silhouette delta between states must force the
// fast-crossfade / Swoosh path (Phase 4). BRIDGE-safe layers (the interior
// facial features — brows/eyes/nose/mouth/teeth + the anchored cheeks/chin/
// neck) are either hidden by FPFeatureAlphaAt past the back-corner or stay
// face-relative, so a plain crossfade at any delta reads fine. The tag mirror
// uses the base-preset layer tags (deploy.py LAYERS).
// ============================================================================
enum class FPSchematicAnchorClass : unsigned char
{
    AnchorCritical,
    BridgeSafe,
    MAX
};

inline FPSchematicAnchorClass FPSchematicAnchorClassForPart(const char* Name)
{
    if (!Name || !Name[0]) return FPSchematicAnchorClass::BridgeSafe;
    const std::string N(Name);
    if (N == "Head" || N == "Bangs" || N == "Hair" || N == "BackHair"
        || N == "EarL" || N == "EarR")
        return FPSchematicAnchorClass::AnchorCritical;
    return FPSchematicAnchorClass::BridgeSafe;
}

inline FPSchematicAnchorClass FPSchematicAnchorClassForTag(const char* Tag)
{
    if (!Tag || !Tag[0]) return FPSchematicAnchorClass::BridgeSafe;
    const std::string T(Tag);
    if (T == "Head" || T == "Bangs" || T == "Hair" || T == "BackHair"
        || T == "Ears")
        return FPSchematicAnchorClass::AnchorCritical;
    return FPSchematicAnchorClass::BridgeSafe;
}

// ============================================================================
// Phase 3: per-state visibility + Z-order (hide, not just occlude). Real 2D
// art cards cannot fold to a dot the way the placeholder formula does, so at
// the profile the FAR-side pair and in walk-behind states the FEATURES would
// otherwise keep rendering their last art and EDGE-PEEK through the crossfade.
// The billboard-correct answer is to HIDE those layers per state. State
// indices mirror EFaceAngleState's order in FaceParallaxTypes.h:
//   0 Front, 1 3/4R, 2 RightProfile, 3 BackRight, 4 Back, 5 BackLeft,
//   6 LeftProfile, 7 3/4L, 8 Top, 9 Bottom
// Zone centers use the default multipliers (1/3/5/7 x HalfZoneWidth 22.5):
// 0/45/90/135/180/-135/-90/-45; Top/Bottom park at yaw 0 with pitch ±90.
// ============================================================================
inline double FPSchematicStateCenterYaw(int StateIdx)
{
    switch (StateIdx)
    {
    case 1:  return 45.0;
    case 2:  return 90.0;
    case 3:  return 135.0;
    case 4:  return 180.0;
    case 5:  return -135.0;
    case 6:  return -90.0;
    case 7:  return -45.0;
    default: return 0.0;   // Front + Top/Bottom
    }
}

inline double FPSchematicStateCenterPitch(int StateIdx)
{
    return StateIdx == 8 ? 90.0 : (StateIdx == 9 ? -90.0 : 0.0);
}

// Walk-behind states (BackRight / Back / BackLeft): |center yaw| >= 135°, the
// states where features fade out (the placeholder's FPFeatureAlphaAt is 0).
inline bool FPSchematicStateIsWalkBehind(int StateIdx)
{
    return std::abs(FPSchematicStateCenterYaw(StateIdx)) >= 135.0;
}

// Is the layer RENDERED in this state? The silhouette mass (Head + the hair
// layers) is present in every state; the far-side member of a paired layer
// (eyes/brows/cheeks/ears) is HIDDEN (it folds); walk-behind states hide every
// non-paired feature (Nose/Mouth/Teeth/Chin/Neck). Unknown/null names default
// to hidden (a layer not in the canonical set is not part of the read).
inline bool FPSchematicLayerVisibleInState(int StateIdx, const char* LayerName)
{
    if (!LayerName || !LayerName[0]) return false;
    if (FPSchematicIsSilhouette(LayerName)) return true;
    if (FPSchematicIsPairedPart(LayerName))
        return !FPSchematicIsFarSide(LayerName, FPSchematicStateCenterYaw(StateIdx));
    if (FPSchematicAnchorClassForPart(LayerName) == FPSchematicAnchorClass::BridgeSafe)
        return !FPSchematicStateIsWalkBehind(StateIdx);
    return false;
}

// Per-state Z-order of the RENDERED layers: smaller = rendered on top (closer
// to the camera). The order is the FPZDepth plane hierarchy (1 Nose/Front
// Bangs .. 5 Neck/Back Hair) applied to the state's visible set; the far-side
// pair is hidden rather than occluded, so the only per-state difference is
// WHICH layers render. Hidden layers return -1 (not rendered).
inline int FPSchematicLayerOrderInState(int StateIdx, const char* LayerName)
{
    if (!FPSchematicLayerVisibleInState(StateIdx, LayerName)) return -1;
    return (int)FPZDepthForPart(LayerName);
}

// TAG-level variant for the RUNTIME: the component spawns ONE quad per
// base-preset tag (Eyes/Brows/Mouth/Bangs/Nose/Cheeks/Head/Hair/BackHair/
// Ears), so it cannot hide a single member of a pair — the whole card hides
// or shows. The paired fold is already handled by the placeholder slide; what
// this table adds is the WALK-BEHIND FADE: the feature cards (BridgeSafe
// tags) hide in the walk-behind states so their front art cannot edge-peek
// around the skull (the placeholder's FPFeatureAlphaAt is 0 there), while the
// silhouette + ear cards (AnchorCritical tags) stay in the read.
inline bool FPSchematicLayerVisibleInTag(int StateIdx, const char* Tag)
{
    if (!Tag || !Tag[0]) return false;
    if (FPSchematicAnchorClassForTag(Tag) == FPSchematicAnchorClass::AnchorCritical)
        return true;
    return !FPSchematicStateIsWalkBehind(StateIdx);
}

// Tag-level per-state Z-order: representative FPZDepth plane per base-preset
// tag (the tag's primary part). Hidden tags return -1 (not rendered).
inline int FPSchematicLayerOrderInTag(int StateIdx, const char* Tag)
{
    if (!FPSchematicLayerVisibleInTag(StateIdx, Tag)) return -1;
    const std::string T(Tag ? Tag : "");
    if (T == "Nose" || T == "Bangs") return (int)FPZDepth::Closest;
    if (T == "Eyes" || T == "Brows" || T == "Mouth" || T == "Cheeks")
        return (int)FPZDepth::NearFeatures;
    if (T == "Head") return (int)FPZDepth::FaceBase;
    if (T == "Ears" || T == "Hair") return (int)FPZDepth::EarSideHair;
    return (int)FPZDepth::Farthest;   // BackHair / default
}

// ============================================================================
// Phase 4: silhouette-delta crossfade / swoosh. A slow crossfade is fine when
// From and To are the same structural shape (Front -> 3/4R); it looks broken
// when the silhouettes are structurally DIFFERENT (Front -> Back hides the
// features, Top -> Back reverses the whole read) because the in-between frames
// linger on a shape that never exists. FPSilhouetteDelta scores that gap
// (0..1): 60% of the SHAPE term (fraction of base-preset tags whose per-state
// visibility flips between the two states) + 40% of the ANGLE term (wrap-aware
// yaw distance + pitch distance, / 360). FPSchematicShouldSwoosh is the
// trigger extension: a transition whose delta clears the threshold wants a
// fast sweep rather than a slow blend.
// ============================================================================
inline double FPSchematicYawDistance(int FromState, int ToState)
{
    double D = fabs(FPSchematicStateCenterYaw(FromState)
        - FPSchematicStateCenterYaw(ToState));
    if (D > 180.0) D = 360.0 - D;
    return D;   // [0, 180]
}

inline double FPSilhouetteDelta(int FromState, int ToState)
{
    if (FromState == ToState) return 0.0;
    static const char* const BasePresetTags[10] = { "Eyes", "Brows", "Mouth",
        "Bangs", "Nose", "Cheeks", "Head", "Hair", "BackHair", "Ears" };
    int Changed = 0;
    for (const char* T : BasePresetTags)
        if (FPSchematicLayerVisibleInTag(FromState, T)
            != FPSchematicLayerVisibleInTag(ToState, T))
            ++Changed;
    const double ShapeTerm = (double)Changed / 10.0;
    const double AngleTerm = (FPSchematicYawDistance(FromState, ToState)
        + fabs(FPSchematicStateCenterPitch(FromState)
            - FPSchematicStateCenterPitch(ToState))) / 360.0;
    double Result = 0.6 * ShapeTerm + 0.4 * AngleTerm;
    if (Result > 1.0) Result = 1.0;
    return Result;
}

// Transition crossfade-speed bias: 1.0 (no bias) at delta 0 up to 2.5x faster
// at delta 1 — a structural gap blends quicker so the in-between never lingers.
inline double FPSchematicTransitionBlendRate(int FromState, int ToState)
{
    return 1.0 + 1.5 * FPSilhouetteDelta(FromState, ToState);
}

// Swoosh trigger extension: a structurally-different transition sweeps fast.
inline bool FPSchematicShouldSwoosh(int FromState, int ToState)
{
    static const double DeltaThreshold = 0.4;
    return FPSilhouetteDelta(FromState, ToState) >= DeltaThreshold;
}

// ----------------------------------------------------------------------------
// The authored 2D layout ramps (right half of the turn; the left half mirrors
// by sign). Control points sit on the exact state centers 0 (Front), 45
// (3/4), 90 (Profile), 135 (Back-corner) and 180 (Back), so each view state
// flips to ITS OWN 2D layout while scrubbing 2D-blends (smoothstep) between
// neighbors — the "smooth parallax with a flip on view" contract.
// ----------------------------------------------------------------------------

// Silhouette width (about the head centerline X=0.5): full at front AND back,
// narrowest at the profiles — a back-of-head is a normal-width oval.
inline double FPSilhouetteWidthAt(double YawAbs)
{
    static const FPRampPoint K[] = { {0, 1.0}, {45, 0.80}, {90, FPOrientationParams::SilAtProfile},
                                     {135, 0.82}, {180, 1.0} };
    return FPRampEval(K, 5, YawAbs);
}

// Near-side / center-feature width scale (about the turned-head center):
// readable through the profile, then held (alpha hides it in walk-behind).
inline double FPNearFeatureWidthAt(double YawAbs)
{
    static const FPRampPoint K[] = { {0, 1.0}, {45, 0.90}, {90, 0.80}, {135, 0.80}, {180, 0.80} };
    return FPRampEval(K, 5, YawAbs);
}

// Far-side paired-part fold (about the turned-head center): full at front,
// already narrowing at 3/4, fully folded (zero width) by the profile so a
// profile shows exactly ONE eye/ear/cheek — and it STAYS folded through the
// back (the far pair sits behind the nose bridge / back hair for the whole
// second half of the turn, per the billboard layer rules).
inline double FPFarFeatureWidthAt(double YawAbs)
{
    static const FPRampPoint K[] = { {0, 1.0}, {45, 0.55}, {90, 0.0}, {180, 0.0} };
    return FPRampEval(K, 4, YawAbs);
}

// Anatomical-feature visibility: 1 through both profiles, fading past the
// back-corner so walk-behind states show silhouette + hair only.
inline double FPFeatureAlphaAt(double YawAbs)
{
    static const FPRampPoint K[] = { {0, 1.0}, {112.5, 1.0}, {135, 0.0}, {180, 0.0} };
    return FPRampEval(K, 4, YawAbs);
}

// ============================================================================
// Phase 5: authored per-state key silhouettes. The squish/slide formula below
// derives every angle from the FRONT glyph — that is structurally wrong for
// the silhouette parts: a profile silhouette is a real forehead-nose-chin
// contour (vs a skull-nape contour for the back), NOT a squished front, and
// no continuous formula can produce it (the old squish made BackHair shrink
// in place under the head at the profile instead of trailing behind it).
// Head / Bangs / Hair / BackHair therefore carry EXACT authored 2D layouts at
// each state center (0/45/90/135/180 for yaw, plus dedicated Top/Bottom pitch
// poses), each a closed ring with the SAME point count and vertex order as
// the front glyph (P0 == the front outline exactly), all inside [0,1]^2. The
// positive-yaw half is authored; negative yaw is the exact horizontal mirror
// (X -> 1-X). FPOrientationOutline morphs (smoothstep) between the bracketing
// authored poses for these parts and keeps the squish/slide formula for every
// other part (the per-part fallback is the P1..P24-safe default).
// ============================================================================

struct FPSchematicPoseSet
{
    std::vector<FPSchematicPoint> P0;       // Front (== the front glyph exactly)
    std::vector<FPSchematicPoint> P45;      // 3/4
    std::vector<FPSchematicPoint> P90;      // Profile
    std::vector<FPSchematicPoint> P135;     // Back-corner
    std::vector<FPSchematicPoint> P180;     // Back
    std::vector<FPSchematicPoint> PTop;     // Top view (pitch +90, yaw 0)
    std::vector<FPSchematicPoint> PBottom;  // Bottom view (pitch -90, yaw 0)
};

struct FPSchematicPoseEntry { const char* Name; FPSchematicPoseSet Pose; };

// The authored silhouette pose table (sentinel-terminated: Name == nullptr).
// Head/Bangs/Hair/BackHair carry authored key poses; every anatomical feature
// stays out of the table and falls back to the squish/slide formula. The table
// is the single source of truth for both FPSchematicAuthoredPoses and the
// Phase 6 pose validator.
inline const FPSchematicPoseEntry* FPSchematicAuthoredPoseTable()
{
    static const FPSchematicPoseEntry Table[] = {
        // Head: front-wide sphere -> profile forehead-nose-chin -> full back.
        { "Head", {
            { SPT(0.50, 0.02), SPT(0.29, 0.076), SPT(0.136, 0.23), SPT(0.08, 0.44),
              SPT(0.19, 0.61), SPT(0.32, 0.74), SPT(0.50, 0.86), SPT(0.68, 0.74),
              SPT(0.81, 0.61), SPT(0.92, 0.44), SPT(0.864, 0.23), SPT(0.71, 0.076) },
            { SPT(0.52, 0.02), SPT(0.33, 0.09), SPT(0.20, 0.23), SPT(0.13, 0.42),
              SPT(0.17, 0.58), SPT(0.27, 0.70), SPT(0.44, 0.83), SPT(0.62, 0.76),
              SPT(0.75, 0.63), SPT(0.87, 0.46), SPT(0.86, 0.24), SPT(0.70, 0.08) },
            { SPT(0.52, 0.02), SPT(0.36, 0.10), SPT(0.25, 0.22), SPT(0.17, 0.36),
              SPT(0.10, 0.47), SPT(0.17, 0.56), SPT(0.14, 0.63), SPT(0.28, 0.71),
              SPT(0.46, 0.75), SPT(0.62, 0.70), SPT(0.70, 0.42), SPT(0.68, 0.14) },
            { SPT(0.50, 0.02), SPT(0.38, 0.08), SPT(0.26, 0.18), SPT(0.20, 0.34),
              SPT(0.17, 0.50), SPT(0.22, 0.63), SPT(0.34, 0.74), SPT(0.52, 0.75),
              SPT(0.66, 0.66), SPT(0.76, 0.50), SPT(0.78, 0.26), SPT(0.66, 0.08) },
            { SPT(0.50, 0.02), SPT(0.29, 0.076), SPT(0.14, 0.23), SPT(0.09, 0.44),
              SPT(0.20, 0.62), SPT(0.33, 0.75), SPT(0.50, 0.86), SPT(0.67, 0.75),
              SPT(0.80, 0.62), SPT(0.91, 0.44), SPT(0.86, 0.23), SPT(0.71, 0.076) },
            { SPT(0.50, 0.20), SPT(0.31, 0.24), SPT(0.17, 0.32), SPT(0.11, 0.46),
              SPT(0.18, 0.58), SPT(0.32, 0.66), SPT(0.50, 0.70), SPT(0.68, 0.66),
              SPT(0.82, 0.58), SPT(0.89, 0.46), SPT(0.83, 0.32), SPT(0.69, 0.24) },
            { SPT(0.50, 0.16), SPT(0.31, 0.20), SPT(0.18, 0.28), SPT(0.12, 0.42),
              SPT(0.19, 0.55), SPT(0.33, 0.64), SPT(0.50, 0.68), SPT(0.67, 0.64),
              SPT(0.81, 0.55), SPT(0.88, 0.42), SPT(0.82, 0.28), SPT(0.69, 0.20) }
        } },
        // Bangs: forehead fringe -> compact profile wedge over the brow -> back hairline.
        { "Bangs", {
            { SPT(0.22, 0.34), SPT(0.19, 0.12), SPT(0.24, 0.04), SPT(0.28, 0.03),
              SPT(0.33, 0.02), SPT(0.37, 0.11), SPT(0.40, 0.03), SPT(0.45, 0.02),
              SPT(0.50, 0.02), SPT(0.55, 0.02), SPT(0.60, 0.03), SPT(0.63, 0.11),
              SPT(0.67, 0.02), SPT(0.72, 0.03), SPT(0.76, 0.04), SPT(0.81, 0.12),
              SPT(0.78, 0.34), SPT(0.72, 0.28), SPT(0.65, 0.32), SPT(0.58, 0.30),
              SPT(0.50, 0.35), SPT(0.42, 0.30), SPT(0.35, 0.32), SPT(0.28, 0.28) },
            { SPT(0.23, 0.32), SPT(0.23, 0.16), SPT(0.27, 0.07), SPT(0.31, 0.04),
              SPT(0.36, 0.03), SPT(0.41, 0.04), SPT(0.45, 0.03), SPT(0.50, 0.03),
              SPT(0.55, 0.03), SPT(0.59, 0.05), SPT(0.63, 0.08), SPT(0.65, 0.13),
              SPT(0.67, 0.18), SPT(0.68, 0.24), SPT(0.67, 0.30), SPT(0.65, 0.35),
              SPT(0.58, 0.33), SPT(0.52, 0.29), SPT(0.46, 0.27), SPT(0.40, 0.26),
              SPT(0.34, 0.27), SPT(0.30, 0.28), SPT(0.26, 0.30), SPT(0.24, 0.31) },
            { SPT(0.24, 0.30), SPT(0.26, 0.20), SPT(0.30, 0.12), SPT(0.35, 0.07),
              SPT(0.40, 0.04), SPT(0.45, 0.03), SPT(0.50, 0.03), SPT(0.55, 0.04),
              SPT(0.59, 0.06), SPT(0.63, 0.09), SPT(0.66, 0.13), SPT(0.68, 0.18),
              SPT(0.69, 0.23), SPT(0.69, 0.28), SPT(0.68, 0.33), SPT(0.66, 0.38),
              SPT(0.58, 0.34), SPT(0.52, 0.30), SPT(0.46, 0.27), SPT(0.41, 0.25),
              SPT(0.36, 0.25), SPT(0.31, 0.26), SPT(0.27, 0.28), SPT(0.25, 0.30) },
            { SPT(0.25, 0.30), SPT(0.25, 0.18), SPT(0.28, 0.08), SPT(0.33, 0.04),
              SPT(0.38, 0.03), SPT(0.43, 0.03), SPT(0.50, 0.03), SPT(0.56, 0.03),
              SPT(0.61, 0.03), SPT(0.66, 0.05), SPT(0.70, 0.09), SPT(0.73, 0.15),
              SPT(0.73, 0.22), SPT(0.71, 0.29), SPT(0.68, 0.33), SPT(0.64, 0.36),
              SPT(0.57, 0.32), SPT(0.51, 0.29), SPT(0.45, 0.28), SPT(0.39, 0.28),
              SPT(0.33, 0.29), SPT(0.29, 0.31), SPT(0.27, 0.32), SPT(0.26, 0.31) },
            { SPT(0.26, 0.30), SPT(0.24, 0.16), SPT(0.28, 0.06), SPT(0.33, 0.03),
              SPT(0.38, 0.02), SPT(0.43, 0.02), SPT(0.50, 0.02), SPT(0.57, 0.02),
              SPT(0.62, 0.02), SPT(0.67, 0.03), SPT(0.72, 0.06), SPT(0.76, 0.16),
              SPT(0.74, 0.30), SPT(0.68, 0.26), SPT(0.62, 0.28), SPT(0.55, 0.27),
              SPT(0.50, 0.30), SPT(0.44, 0.27), SPT(0.38, 0.28), SPT(0.33, 0.26),
              SPT(0.30, 0.30), SPT(0.28, 0.32), SPT(0.27, 0.33), SPT(0.26, 0.31) },
            { SPT(0.20, 0.34), SPT(0.20, 0.24), SPT(0.24, 0.16), SPT(0.30, 0.12),
              SPT(0.36, 0.10), SPT(0.42, 0.10), SPT(0.48, 0.10), SPT(0.55, 0.10),
              SPT(0.61, 0.10), SPT(0.67, 0.12), SPT(0.72, 0.16), SPT(0.76, 0.24),
              SPT(0.76, 0.34), SPT(0.70, 0.32), SPT(0.64, 0.33), SPT(0.57, 0.34),
              SPT(0.50, 0.35), SPT(0.43, 0.34), SPT(0.36, 0.33), SPT(0.30, 0.32),
              SPT(0.26, 0.34), SPT(0.23, 0.35), SPT(0.21, 0.36), SPT(0.20, 0.35) },
            { SPT(0.22, 0.36), SPT(0.21, 0.26), SPT(0.25, 0.18), SPT(0.31, 0.14),
              SPT(0.37, 0.12), SPT(0.43, 0.12), SPT(0.50, 0.12), SPT(0.57, 0.12),
              SPT(0.63, 0.12), SPT(0.69, 0.14), SPT(0.74, 0.18), SPT(0.78, 0.26),
              SPT(0.78, 0.36), SPT(0.71, 0.34), SPT(0.65, 0.35), SPT(0.58, 0.36),
              SPT(0.50, 0.37), SPT(0.42, 0.36), SPT(0.35, 0.35), SPT(0.29, 0.34),
              SPT(0.26, 0.36), SPT(0.24, 0.37), SPT(0.23, 0.38), SPT(0.22, 0.37) }
        } },
        // Hair: full annulus (outer mass + face cutout) -> big mass trailing the profile -> solid back mass.
        { "Hair", {
            { SPT(0.03, 0.52), SPT(0.045, 0.28), SPT(0.05, 0.18), SPT(0.06, 0.13),
              SPT(0.09, 0.09), SPT(0.13, 0.05), SPT(0.18, 0.03), SPT(0.24, 0.012),
              SPT(0.28, 0.008), SPT(0.31, 0.005), SPT(0.36, 0.012), SPT(0.42, 0.010),
              SPT(0.44, 0.004), SPT(0.47, 0.012), SPT(0.50, 0.010), SPT(0.53, 0.012),
              SPT(0.56, 0.004), SPT(0.58, 0.010), SPT(0.64, 0.012), SPT(0.69, 0.005),
              SPT(0.72, 0.008), SPT(0.76, 0.012), SPT(0.82, 0.03), SPT(0.87, 0.05),
              SPT(0.91, 0.09), SPT(0.94, 0.13), SPT(0.95, 0.18), SPT(0.955, 0.28),
              SPT(0.97, 0.52), SPT(0.90, 0.36), SPT(0.87, 0.30), SPT(0.84, 0.20),
              SPT(0.80, 0.12), SPT(0.72, 0.07), SPT(0.64, 0.05), SPT(0.58, 0.09),
              SPT(0.54, 0.06), SPT(0.50, 0.05), SPT(0.46, 0.06), SPT(0.42, 0.09),
              SPT(0.36, 0.05), SPT(0.28, 0.07), SPT(0.20, 0.12), SPT(0.16, 0.20),
              SPT(0.13, 0.30), SPT(0.10, 0.36), SPT(0.05, 0.55) },
            { SPT(0.14, 0.50), SPT(0.13, 0.30), SPT(0.14, 0.20), SPT(0.16, 0.14),
              SPT(0.19, 0.10), SPT(0.24, 0.06), SPT(0.30, 0.04), SPT(0.37, 0.03),
              SPT(0.43, 0.03), SPT(0.48, 0.03), SPT(0.54, 0.03), SPT(0.60, 0.03),
              SPT(0.65, 0.04), SPT(0.69, 0.06), SPT(0.73, 0.08), SPT(0.77, 0.11),
              SPT(0.81, 0.16), SPT(0.84, 0.22), SPT(0.87, 0.30), SPT(0.90, 0.40),
              SPT(0.93, 0.50), SPT(0.95, 0.62), SPT(0.94, 0.74), SPT(0.90, 0.85),
              SPT(0.84, 0.93), SPT(0.76, 0.97), SPT(0.66, 0.98), SPT(0.56, 0.97),
              SPT(0.48, 0.94), SPT(0.42, 0.86), SPT(0.36, 0.78), SPT(0.31, 0.68),
              SPT(0.27, 0.58), SPT(0.24, 0.50), SPT(0.24, 0.42), SPT(0.26, 0.35),
              SPT(0.30, 0.29), SPT(0.34, 0.25), SPT(0.38, 0.23), SPT(0.40, 0.23),
              SPT(0.40, 0.28), SPT(0.37, 0.34), SPT(0.33, 0.42), SPT(0.30, 0.52),
              SPT(0.32, 0.62), SPT(0.36, 0.72), SPT(0.42, 0.82) },
            { SPT(0.72, 0.40), SPT(0.74, 0.28), SPT(0.74, 0.20), SPT(0.75, 0.14),
              SPT(0.77, 0.10), SPT(0.80, 0.07), SPT(0.84, 0.05), SPT(0.88, 0.045),
              SPT(0.92, 0.05), SPT(0.94, 0.06), SPT(0.95, 0.07), SPT(0.96, 0.10),
              SPT(0.965, 0.14), SPT(0.97, 0.18), SPT(0.975, 0.24), SPT(0.98, 0.30),
              SPT(0.985, 0.38), SPT(0.985, 0.46), SPT(0.98, 0.55), SPT(0.975, 0.64),
              SPT(0.97, 0.72), SPT(0.96, 0.80), SPT(0.94, 0.88), SPT(0.91, 0.94),
              SPT(0.86, 0.97), SPT(0.80, 0.97), SPT(0.72, 0.96), SPT(0.66, 0.92),
              SPT(0.62, 0.86), SPT(0.30, 0.78), SPT(0.26, 0.70), SPT(0.22, 0.62),
              SPT(0.20, 0.54), SPT(0.18, 0.47), SPT(0.20, 0.40), SPT(0.24, 0.33),
              SPT(0.29, 0.27), SPT(0.34, 0.23), SPT(0.38, 0.21), SPT(0.40, 0.21),
              SPT(0.40, 0.26), SPT(0.38, 0.33), SPT(0.34, 0.42), SPT(0.32, 0.52),
              SPT(0.34, 0.62), SPT(0.38, 0.72), SPT(0.42, 0.80) },
            { SPT(0.20, 0.42), SPT(0.19, 0.24), SPT(0.20, 0.15), SPT(0.23, 0.09),
              SPT(0.27, 0.05), SPT(0.33, 0.03), SPT(0.40, 0.03), SPT(0.46, 0.03),
              SPT(0.52, 0.03), SPT(0.57, 0.03), SPT(0.62, 0.04), SPT(0.67, 0.05),
              SPT(0.72, 0.07), SPT(0.76, 0.10), SPT(0.80, 0.13), SPT(0.84, 0.18),
              SPT(0.87, 0.24), SPT(0.90, 0.32), SPT(0.92, 0.40), SPT(0.94, 0.50),
              SPT(0.95, 0.60), SPT(0.95, 0.71), SPT(0.93, 0.81), SPT(0.88, 0.90),
              SPT(0.81, 0.96), SPT(0.72, 0.98), SPT(0.62, 0.98), SPT(0.52, 0.96),
              SPT(0.44, 0.92), SPT(0.38, 0.84), SPT(0.33, 0.76), SPT(0.29, 0.66),
              SPT(0.26, 0.56), SPT(0.24, 0.47), SPT(0.24, 0.39), SPT(0.26, 0.32),
              SPT(0.29, 0.27), SPT(0.33, 0.23), SPT(0.37, 0.22), SPT(0.40, 0.22),
              SPT(0.39, 0.27), SPT(0.36, 0.33), SPT(0.32, 0.40), SPT(0.30, 0.49),
              SPT(0.32, 0.59), SPT(0.35, 0.69), SPT(0.40, 0.79) },
            { SPT(0.10, 0.42), SPT(0.09, 0.24), SPT(0.10, 0.15), SPT(0.13, 0.09),
              SPT(0.17, 0.05), SPT(0.24, 0.03), SPT(0.30, 0.03), SPT(0.36, 0.03),
              SPT(0.42, 0.03), SPT(0.48, 0.03), SPT(0.54, 0.03), SPT(0.60, 0.03),
              SPT(0.65, 0.04), SPT(0.70, 0.05), SPT(0.75, 0.08), SPT(0.80, 0.11),
              SPT(0.85, 0.16), SPT(0.89, 0.22), SPT(0.92, 0.30), SPT(0.94, 0.38),
              SPT(0.95, 0.48), SPT(0.95, 0.58), SPT(0.94, 0.70), SPT(0.90, 0.82),
              SPT(0.84, 0.92), SPT(0.74, 0.97), SPT(0.64, 0.98), SPT(0.54, 0.98),
              SPT(0.46, 0.96), SPT(0.40, 0.06), SPT(0.36, 0.07), SPT(0.32, 0.08),
              SPT(0.30, 0.10), SPT(0.32, 0.13), SPT(0.36, 0.15), SPT(0.40, 0.15),
              SPT(0.43, 0.15), SPT(0.46, 0.14), SPT(0.48, 0.12), SPT(0.47, 0.10),
              SPT(0.45, 0.08), SPT(0.42, 0.06), SPT(0.40, 0.05), SPT(0.38, 0.05),
              SPT(0.37, 0.06), SPT(0.38, 0.07), SPT(0.40, 0.07) },
            { SPT(0.10, 0.46), SPT(0.09, 0.28), SPT(0.11, 0.18), SPT(0.15, 0.12),
              SPT(0.20, 0.08), SPT(0.28, 0.05), SPT(0.35, 0.04), SPT(0.42, 0.04),
              SPT(0.48, 0.04), SPT(0.54, 0.04), SPT(0.61, 0.04), SPT(0.67, 0.05),
              SPT(0.72, 0.06), SPT(0.77, 0.09), SPT(0.82, 0.12), SPT(0.86, 0.18),
              SPT(0.89, 0.28), SPT(0.91, 0.46), SPT(0.92, 0.54), SPT(0.91, 0.62),
              SPT(0.88, 0.70), SPT(0.84, 0.78), SPT(0.78, 0.85), SPT(0.70, 0.90),
              SPT(0.60, 0.93), SPT(0.50, 0.94), SPT(0.40, 0.93), SPT(0.30, 0.90),
              SPT(0.22, 0.85), SPT(0.16, 0.78), SPT(0.22, 0.72), SPT(0.30, 0.68),
              SPT(0.38, 0.66), SPT(0.44, 0.66), SPT(0.50, 0.66), SPT(0.56, 0.66),
              SPT(0.62, 0.68), SPT(0.66, 0.70), SPT(0.68, 0.73), SPT(0.66, 0.77),
              SPT(0.60, 0.79), SPT(0.52, 0.80), SPT(0.44, 0.80), SPT(0.36, 0.78),
              SPT(0.28, 0.76), SPT(0.21, 0.75), SPT(0.16, 0.76) },
            { SPT(0.10, 0.48), SPT(0.10, 0.30), SPT(0.12, 0.20), SPT(0.16, 0.13),
              SPT(0.22, 0.09), SPT(0.29, 0.06), SPT(0.36, 0.05), SPT(0.43, 0.05),
              SPT(0.50, 0.05), SPT(0.57, 0.05), SPT(0.63, 0.06), SPT(0.69, 0.08),
              SPT(0.74, 0.11), SPT(0.79, 0.15), SPT(0.83, 0.20), SPT(0.87, 0.30),
              SPT(0.89, 0.48), SPT(0.90, 0.56), SPT(0.89, 0.64), SPT(0.86, 0.71),
              SPT(0.82, 0.78), SPT(0.76, 0.84), SPT(0.68, 0.89), SPT(0.58, 0.92),
              SPT(0.48, 0.93), SPT(0.38, 0.92), SPT(0.28, 0.89), SPT(0.20, 0.84),
              SPT(0.14, 0.78), SPT(0.18, 0.70), SPT(0.24, 0.64), SPT(0.32, 0.61),
              SPT(0.40, 0.60), SPT(0.46, 0.60), SPT(0.52, 0.60), SPT(0.58, 0.60),
              SPT(0.63, 0.62), SPT(0.66, 0.65), SPT(0.66, 0.68), SPT(0.63, 0.71),
              SPT(0.56, 0.73), SPT(0.48, 0.74), SPT(0.40, 0.74), SPT(0.32, 0.72),
              SPT(0.25, 0.70), SPT(0.20, 0.69), SPT(0.18, 0.69) }
        } },
        // BackHair: bottom cascade -> narrow band TRAILING the profile skull -> full back mass.
        { "BackHair", {
            { SPT(0.18, 0.90), SPT(0.30, 0.82), SPT(0.44, 0.80), SPT(0.56, 0.80),
              SPT(0.70, 0.82), SPT(0.82, 0.90), SPT(0.74, 0.96), SPT(0.26, 0.96) },
            { SPT(0.36, 0.74), SPT(0.46, 0.66), SPT(0.58, 0.63), SPT(0.67, 0.64),
              SPT(0.75, 0.70), SPT(0.80, 0.84), SPT(0.66, 0.95), SPT(0.38, 0.95) },
            { SPT(0.72, 0.30), SPT(0.80, 0.40), SPT(0.85, 0.55), SPT(0.87, 0.70),
              SPT(0.87, 0.84), SPT(0.84, 0.94), SPT(0.72, 0.96), SPT(0.66, 0.70) },
            { SPT(0.34, 0.62), SPT(0.46, 0.56), SPT(0.60, 0.55), SPT(0.70, 0.58),
              SPT(0.78, 0.64), SPT(0.80, 0.84), SPT(0.66, 0.95), SPT(0.36, 0.95) },
            { SPT(0.14, 0.72), SPT(0.28, 0.64), SPT(0.42, 0.62), SPT(0.58, 0.62),
              SPT(0.72, 0.64), SPT(0.86, 0.72), SPT(0.80, 0.94), SPT(0.20, 0.94) },
            { SPT(0.30, 0.72), SPT(0.44, 0.66), SPT(0.56, 0.66), SPT(0.68, 0.68),
              SPT(0.78, 0.74), SPT(0.80, 0.86), SPT(0.60, 0.94), SPT(0.34, 0.94) },
            { SPT(0.34, 0.74), SPT(0.46, 0.68), SPT(0.58, 0.68), SPT(0.68, 0.70),
              SPT(0.78, 0.76), SPT(0.80, 0.88), SPT(0.62, 0.94), SPT(0.36, 0.94) }
        } },
        { nullptr, {} }   // table sentinel
    };
    return Table;
}

// nullptr (fall back to the squish/slide formula) for any part not in the
// table — including every anatomical feature.
inline const FPSchematicPoseSet* FPSchematicAuthoredPoses(const char* Name)
{
    if (!Name || !Name[0]) return nullptr;
    for (const FPSchematicPoseEntry* E = FPSchematicAuthoredPoseTable(); E->Name; ++E)
        if (std::string(E->Name) == Name) return &E->Pose;
    return nullptr;
}

// ============================================================================
// Phase 6: authored-pose validation. Every ring must be a valid closed
// silhouette (>= 3 finite points inside [0,1]^2; the ring is implicitly closed
// last->first) and every pose must keep the front point count so the morph
// never degenerates. The back pose must differ from the front by the measured
// 41% gate (fraction of ring points displaced by > 10% of the canvas): real 2D
// art has structurally different back silhouettes, so a back pose that is
// nearly a copy of the front is a data error the validator must catch.
// ============================================================================
inline bool FPOutlineIsValidClosedRing(const std::vector<FPSchematicPoint>& Ring)
{
    if (Ring.size() < 3) return false;
    for (const FPSchematicPoint& P : Ring)
    {
        if (!(P.X == P.X) || !(P.Y == P.Y)) return false;            // NaN guard
        if (P.X < 0.0 || P.X > 1.0 || P.Y < 0.0 || P.Y > 1.0) return false;
    }
    return true;
}

struct FPSchematicPoseValidation
{
    bool bAllRingsValid = false;       // all 7 rings valid AND count-matched
    int InvalidRingCount = 0;
    int RingPointCount = 0;            // the front ring's point count
    int BackMovedPoints = 0;           // front->back points displaced > threshold
    static constexpr double BackChangeThreshold = 0.41;   // measured 41% gate
    static constexpr double PointDisplacementThreshold = 0.10;   // 10% of canvas
};

inline FPSchematicPoseValidation FPSchematicValidatePoseSet(const FPSchematicPoseSet& S)
{
    FPSchematicPoseValidation V;
    const std::vector<FPSchematicPoint>* Rings[7] = {
        &S.P0, &S.P45, &S.P90, &S.P135, &S.P180, &S.PTop, &S.PBottom };
    for (const std::vector<FPSchematicPoint>* R : Rings)
        if (!FPOutlineIsValidClosedRing(*R)) ++V.InvalidRingCount;
    V.RingPointCount = (int)S.P0.size();
    for (const std::vector<FPSchematicPoint>* R : Rings)
        if ((int)R->size() != V.RingPointCount) ++V.InvalidRingCount;
    V.bAllRingsValid = (V.InvalidRingCount == 0);
    if (V.RingPointCount >= 2 && (int)S.P180.size() == V.RingPointCount)
    {
        for (int i = 0; i < V.RingPointCount; ++i)
        {
            const double dx = S.P0[i].X - S.P180[i].X;
            const double dy = S.P0[i].Y - S.P180[i].Y;
            if (std::sqrt(dx * dx + dy * dy)
                > FPSchematicPoseValidation::PointDisplacementThreshold)
                ++V.BackMovedPoints;
        }
    }
    return V;
}

struct FPSchematicPoseValidationSummary
{
    int TotalPoseSets = 0;
    int ValidPoseSets = 0;
    int TotalRings = 0;
    int InvalidRings = 0;
    int TotalBackPoints = 0;
    int TotalBackMoved = 0;

    bool bAllAuthoredPosesValid() const { return TotalPoseSets > 0 && InvalidRings == 0; }
    double AggregateBackChange() const
    {
        return TotalBackPoints > 0 ? (double)TotalBackMoved / (double)TotalBackPoints : 0.0;
    }
    bool bBackDiffersFromFront() const
    {
        return AggregateBackChange() >= FPSchematicPoseValidation::BackChangeThreshold;
    }
};

inline FPSchematicPoseValidationSummary FPSchematicValidateAllAuthoredPoses()
{
    FPSchematicPoseValidationSummary S;
    for (const FPSchematicPoseEntry* E = FPSchematicAuthoredPoseTable(); E->Name; ++E)
    {
        const FPSchematicPoseValidation V = FPSchematicValidatePoseSet(E->Pose);
        ++S.TotalPoseSets;
        S.TotalRings += 7;
        S.InvalidRings += V.InvalidRingCount;
        if (V.bAllRingsValid) ++S.ValidPoseSets;
        S.TotalBackPoints += V.RingPointCount;
        S.TotalBackMoved += V.BackMovedPoints;
    }
    return S;
}

// Clamped smoothstep in [0,1] (the turn-ease used for the morph weights).
inline double FPSmoothstep01(double T)
{
    T = T < 0.0 ? 0.0 : (T > 1.0 ? 1.0 : T);
    return T * T * (3.0 - 2.0 * T);
}

// Per-vertex smoothstep morph between the two authored yaw poses bracketing
// |yaw| (exact at the 0/45/90/135/180 state centers). Output keeps the front
// point count; a size mismatch in the table degrades to the smaller ring.
inline void FPSchematicYawMorph(const FPSchematicPoseSet& S, double YawAbs,
    std::vector<FPSchematicPoint>& Out)
{
    const size_t N = S.P0.size();
    Out.resize(N);
    const std::vector<FPSchematicPoint>* A = &S.P0;
    const std::vector<FPSchematicPoint>* B = &S.P0;
    double T = 0.0;
    if (YawAbs <= 0.0)
    {
        A = &S.P0; B = &S.P0;
    }
    else if (YawAbs >= 180.0)
    {
        A = &S.P180; B = &S.P180;
    }
    else if (YawAbs <= 45.0)
    {
        A = &S.P0;  B = &S.P45;  T = FPSmoothstep01(YawAbs / 45.0);
    }
    else if (YawAbs <= 90.0)
    {
        A = &S.P45; B = &S.P90;  T = FPSmoothstep01((YawAbs - 45.0) / 45.0);
    }
    else if (YawAbs <= 135.0)
    {
        A = &S.P90; B = &S.P135; T = FPSmoothstep01((YawAbs - 90.0) / 45.0);
    }
    else
    {
        A = &S.P135; B = &S.P180; T = FPSmoothstep01((YawAbs - 135.0) / 45.0);
    }
    const size_t NA = std::min(A->size(), B->size());
    const size_t NM = std::min(N, NA);
    for (size_t i = 0; i < NM; ++i)
    {
        const FPSchematicPoint& Pa = (*A)[i];
        const FPSchematicPoint& Pb = (*B)[i];
        Out[i] = { Pa.X + (Pb.X - Pa.X) * T, Pa.Y + (Pb.Y - Pa.Y) * T };
    }
    if (NM > 0)
        for (size_t i = NM; i < Out.size(); ++i) Out[i] = Out[NM - 1];
}

// Full authored silhouette orientation: yaw morph (with exact horizontal mirror
// for negative yaw) then a pitch blend toward the authored Top/Bottom pose
// whose weight is smoothstep(|pitch|/90) and FADES with |yaw| (the profile
// keeps its authored profile shape at high yaw; the Top/Bottom poses are exact
// only at yaw 0 — "blended at yaw > 0"). Always clamped into [0,1]^2.
inline std::vector<FPSchematicPoint> FPOrientationAuthoredMorph(
    const FPSchematicPoseSet& S, double YawDeg, double PitchDeg)
{
    const double A = YawDeg < 0.0 ? -YawDeg : YawDeg;
    const bool bMirror = YawDeg < 0.0;

    std::vector<FPSchematicPoint> M;
    FPSchematicYawMorph(S, A, M);

    const double P = PitchDeg < -90.0 ? -90.0 : (PitchDeg > 90.0 ? 90.0 : PitchDeg);
    double PW = FPSmoothstep01(P < 0.0 ? -P / 90.0 : P / 90.0);
    PW *= (1.0 - (A > 180.0 ? 180.0 : A) / 180.0);

    std::vector<FPSchematicPoint> Out = M;
    if (PW > 0.0)
    {
        const std::vector<FPSchematicPoint>& Tgt =
            (P >= 0.0) ? S.PTop : S.PBottom;
        const size_t NB = std::min(M.size(), Tgt.size());
        for (size_t i = 0; i < Out.size(); ++i)
        {
            if (i >= NB) continue;
            const FPSchematicPoint& Pb = Tgt[i];
            Out[i].X += (Pb.X - Out[i].X) * PW;
            Out[i].Y += (Pb.Y - Out[i].Y) * PW;
        }
    }

    if (bMirror)
        for (FPSchematicPoint& p : Out) p.X = 1.0 - p.X;
    for (FPSchematicPoint& p : Out)
    {
        p.X = p.X < 0.0 ? 0.0 : (p.X > 1.0 ? 1.0 : p.X);
        p.Y = p.Y < 0.0 ? 0.0 : (p.Y > 1.0 ? 1.0 : p.Y);
    }
    return Out;
}

// Flip the FRONT-facing glyph to the given orientation: a smooth parallax
// blend between the per-view 2D state layouts, with a flip at each exact
// state center. Every part is BILLBOARDED (a flat card always facing the
// camera) — the turn is faked by sliding the five Z-depth planes against each
// other along the far-edge direction (camera-translation parallax, closest
// furthest) plus the pitch encroach/counter vertical shift. Returns the same
// number of points as the input (point order preserved) with every point
// clamped into [0,1]^2 — the per-view layer transform is applied afterwards
// by the widget, so the placeholder's shape follows the head while the
// authored slot transform keeps placing it.
inline std::vector<FPSchematicPoint> FPOrientationOutline(
    const char* Name, const std::vector<FPSchematicPoint>& Front,
    FPDepthClass, double YawDeg, double PitchDeg)
{
    std::vector<FPSchematicPoint> Out;
    if (Front.size() < 3) return Out;

    // Phase 5: silhouette parts with authored per-state key poses morph
    // between EXACT state shapes (a profile is structurally different from a
    // squished front — forehead-nose-chin vs skull-nape); everything else
    // falls back to the billboard squish/slide formula below.
    if (const FPSchematicPoseSet* Authored = FPSchematicAuthoredPoses(Name))
        return FPOrientationAuthoredMorph(*Authored, YawDeg, PitchDeg);

    const double A = YawDeg < 0.0 ? -YawDeg : YawDeg;   // |yaw| for the ramps
    const double Sign = YawDeg >= 0.0 ? 1.0 : -1.0;     // left half = mirror
    const bool bSil = FPSchematicIsSilhouette(Name);
    // Billboard camera-translation parallax: the flat layers slide toward the
    // far edge (OPPOSITE the orbit), the closest Z sliding furthest.
    const double SlideX = -Sign * FPYawSlideAt(FPZDepthForPart(Name), A);
    const double ShiftY = FPOrientationVerticalShift(Name, PitchDeg);
    const double PScale = FPOrientationPitchScale(PitchDeg);

    const double SilW = FPSilhouetteWidthAt(A);
    const double FeatW = FPNearFeatureWidthAt(A);
    const double FarW = FPFarFeatureWidthAt(A);
    const double Alpha = FPFeatureAlphaAt(A);

    Out.reserve(Front.size());
    for (const FPSchematicPoint& P : Front)
    {
        double X;
        if (bSil)
        {
            // Head + hair: rigid 2D scale about the head centerline plus the
            // plane slide. Silhouettes never fade at the back (full width).
            X = 0.5 + (P.X - 0.5) * SilW + SlideX;
        }
        else
        {
            // Feature: scale about the turned-head center (near side stays
            // readable, the far side of a paired part folds to the nose
            // bridge by the profile and STAYS folded through the back), then
            // the plane slide; alpha folds the whole feature in walk-behind
            // states.
            const bool bFar = FPSchematicIsPairedPart(Name) && FPSchematicIsFarSide(Name, YawDeg);
            const double W = bFar ? FarW : FeatW;
            X = 0.5 + SlideX + (P.X - 0.5) * W * Alpha;
        }
        // Vertical: rigid 2D top/bottom squash about the head centerline,
        // then the plane's encroach/counter up/down parallax shift.
        double Y = 0.5 + (P.Y - 0.5) * PScale + ShiftY;
        X = X < 0.0 ? 0.0 : (X > 1.0 ? 1.0 : X);
        Y = Y < 0.0 ? 0.0 : (Y > 1.0 ? 1.0 : Y);
        Out.push_back({ X, Y });
    }
    return Out;
}

} // namespace FPSchematic
