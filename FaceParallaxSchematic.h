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
#include <functional>

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
        // Construction geometry rework (art_guide.md Part I, front view): the
        // head follows I.2 exactly — the top half is a PERFECT CIRCLE (radius
        // 0.336, center (0.50, 0.356)), the JAW originates at the circle's
        // equator intersections ((0.164, 0.356)/(0.836, 0.356)), drops in a
        // smooth curve pushing slightly outside the cranium's vertical drop
        // before cutting inward, and the chin is a BLUNTED V whose tip sits
        // 0.5 cranium radii below the circle bottom (0.692 + 0.168 = 0.860).
        // The eyes sit on the head's ABSOLUTE MIDLINE (Y = 0.44, the exact
        // mid-point between the cranium top 0.02 and the chin tip 0.86) and
        // obey the 5-PART WIDTH RULE (Part I.4): the head width at the eye
        // line splits into five equal segments = one eye width each
        // (margin / eye / gap / eye / margin, W = 0.1372). Eye height runs
        // 70-80% of eye width (I.6); each upper lash is a closed geometric
        // wedge with the two curved spikes, the lower lash a disconnected
        // segment. The brow sits ONE FULL EYE-HEIGHT above the upper lash
        // (I.6), sweeps wider than the eye, and is not a mirror of its pair.
        // The nose is a minuscule caret/dash at the Center-Face Coordinate,
        // ~60% of the eye-baseline->chin distance (tip at 0.685); the mouth is
        // the open-hole + Teeth ring at 80-85% of that distance (center 0.78);
        // each ear spans the eye top to the nose bottom (I.6). The bangs hem
        // RISES above the brows (roots anchor on the hairline arc, I.2) with
        // the jagged crown zig-zag kept. The hair stays an ANNULUS (outer mass
        // + face cutout), so every probe, region-parity, stack-depth,
        // boundary-inclusive and cycle invariant still holds exactly.
        { "BrowL",  { SPT(0.28, 0.20), SPT(0.32, 0.17), SPT(0.36, 0.165), SPT(0.40, 0.17),
                      SPT(0.435, 0.195), SPT(0.42, 0.20), SPT(0.36, 0.175), SPT(0.32, 0.18),
                      SPT(0.285, 0.205) }, FPDepthClass::Front },
        { "BrowR",  { SPT(0.72, 0.20), SPT(0.68, 0.17), SPT(0.64, 0.165), SPT(0.60, 0.17),
                      SPT(0.565, 0.195), SPT(0.58, 0.20), SPT(0.64, 0.175), SPT(0.68, 0.18),
                      SPT(0.715, 0.205) }, FPDepthClass::Front },
        { "EyeL",     { SPT(0.244737, 0.485087), SPT(0.260263, 0.427626), SPT(0.274737, 0.412424), SPT(0.291842, 0.418171), SPT(0.310263, 0.401946), SPT(0.314539, 0.416107), SPT(0.318816, 0.425178), SPT(0.323092, 0.418980), SPT(0.327368, 0.417872), SPT(0.425263, 0.438312), SPT(0.351053, 0.508435), SPT(0.298421, 0.537340), SPT(0.244737, 0.485087) }, FPDepthClass::Front },
        { "EyeR",     { SPT(0.755263, 0.485087), SPT(0.739737, 0.427626), SPT(0.725263, 0.412424), SPT(0.708158, 0.418171), SPT(0.689737, 0.401946), SPT(0.685461, 0.416107), SPT(0.681184, 0.425178), SPT(0.676908, 0.418980), SPT(0.672632, 0.417872), SPT(0.574737, 0.438312), SPT(0.648947, 0.508435), SPT(0.701579, 0.537340), SPT(0.755263, 0.485087) }, FPDepthClass::Front },
        { "Nose",   { SPT(0.50, 0.675), SPT(0.495, 0.695), SPT(0.505, 0.695) }, FPDepthClass::Front },
        { "CheekL", { SPT(0.08, 0.48), SPT(0.14, 0.44), SPT(0.22, 0.50), SPT(0.26, 0.58),
                      SPT(0.24, 0.70), SPT(0.16, 0.74), SPT(0.10, 0.68), SPT(0.08, 0.58) },
                      FPDepthClass::Front },
        { "CheekR", { SPT(0.92, 0.48), SPT(0.86, 0.44), SPT(0.78, 0.50), SPT(0.74, 0.58),
                      SPT(0.76, 0.70), SPT(0.84, 0.74), SPT(0.90, 0.68), SPT(0.92, 0.58) },
                      FPDepthClass::Front },
        { "Teeth",  { SPT(0.475, 0.782), SPT(0.525, 0.782), SPT(0.53, 0.790), SPT(0.50, 0.794),
                      SPT(0.47, 0.790) }, FPDepthClass::Front },
        { "Mouth",    { SPT(0.465, 0.785), SPT(0.48, 0.780), SPT(0.50, 0.778), SPT(0.52, 0.780), SPT(0.535, 0.785), SPT(0.535, 0.805), SPT(0.52, 0.813), SPT(0.50, 0.815), SPT(0.48, 0.813), SPT(0.465, 0.805) }, FPDepthClass::Front },
        { "Chin",   { SPT(0.42, 0.815), SPT(0.58, 0.815), SPT(0.545, 0.845), SPT(0.52, 0.852),
                      SPT(0.50, 0.855), SPT(0.48, 0.852), SPT(0.455, 0.845) }, FPDepthClass::Base },
        { "EarL",   { SPT(0.05, 0.22), SPT(0.09, 0.20), SPT(0.12, 0.28), SPT(0.11, 0.48),
                      SPT(0.09, 0.70), SPT(0.06, 0.48) }, FPDepthClass::Back },
        { "EarR",   { SPT(0.95, 0.22), SPT(0.91, 0.20), SPT(0.88, 0.28), SPT(0.89, 0.48),
                      SPT(0.91, 0.70), SPT(0.94, 0.48) }, FPDepthClass::Back },
        { "Neck",   { SPT(0.365, 0.86), SPT(0.635, 0.86), SPT(0.62, 0.98), SPT(0.38, 0.98) },
                      FPDepthClass::Base },
        { "Bangs",    { SPT(0.21, 0.26), SPT(0.185, 0.1), SPT(0.23, 0.035), SPT(0.28, 0.028), SPT(0.33, 0.02), SPT(0.37, 0.1), SPT(0.4, 0.028), SPT(0.45, 0.02), SPT(0.455, 0.009), SPT(0.47, 0.005), SPT(0.484, 0.009), SPT(0.5, 0.02), SPT(0.55, 0.02), SPT(0.6, 0.028), SPT(0.63, 0.1), SPT(0.67, 0.02), SPT(0.72, 0.028), SPT(0.77, 0.035), SPT(0.815, 0.1), SPT(0.79, 0.26), SPT(0.72, 0.265), SPT(0.65, 0.252), SPT(0.58, 0.262), SPT(0.5, 0.258), SPT(0.42, 0.262), SPT(0.35, 0.252), SPT(0.28, 0.265) }, FPDepthClass::Front },
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
        { "Head",   { SPT(0.50, 0.020), SPT(0.2624, 0.1184), SPT(0.164, 0.356), SPT(0.157, 0.455),
                      SPT(0.23, 0.64), SPT(0.42, 0.845), SPT(0.50, 0.860), SPT(0.58, 0.845),
                      SPT(0.77, 0.64), SPT(0.843, 0.455), SPT(0.836, 0.356), SPT(0.7376, 0.1184) },
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


// Forward declaration: the full definition lives in the velocity-hierarchy
// block below, but FPYawRule::ComputeVelocityOffset (which mirrors the
// runtime's per-tag offset feed) is declared before it.
inline double FPSchematicTagParallaxRate(const char* Tag);

struct FPYawRule
{
    // Mirror of UFaceParallaxComponent::MaxParallaxOffset (component default).
    static constexpr double MaxOffset = 5.0;

    // Per-class DepthScale the component applies to new layer definitions.
    static constexpr double FrontDepthScale = 1.0;
    static constexpr double BaseDepthScale = 0.15;   // anchored: residual motion only
    static constexpr double BackDepthScale = 1.0;

    static constexpr double Pi = 3.14159265358979323846;

    static inline double ClampYaw(double V)
    {
        return V < -1.0 ? -1.0 : (V > 1.0 ? 1.0 : V);
    }

    // Per-zone REBASED SINE ramp (art_guide III.2 + III.6 Local Delta Reset /
    // Trajectory Matching): displacement eases in from 0% at each zone's start
    // and builds toward its peak as yaw approaches the profile limb, with
    // velocity ∝ cos(θ) — fastest at the front pole (θ=0), zero at the
    // 90 profile — the derivative of the Sine Rule itself (III.4), never a
    // generic symmetric ease (a symmetric ease is too slow at the very front,
    // where a real turn is actually fastest). Because Local Delta Reset zeroes
    // each layer's translation at every swap, the curve can't be one unbroken
    // sine over 0°..180°; it is rebuilt fresh at the start of every zone while
    // still reading as one continuous turn:
    //
    //   offset(θ) = Peak × [ sin(θ) − sin(θ_a) ], θ ∈ [θ_a, θ_b]
    //
    // the same global sine sampled-and-shifted down so it reads 0 at the
    // zone's own start. The vertical shift never touches the derivative
    // (Peak × cos(θ)), so the velocity the outgoing asset carries at θ_b is
    // mathematically identical to the velocity the incoming asset starts with
    // — boundary velocity continuity falls out of the formula, for any zone,
    // with no hand-tuned per-swap stitching. The earliest zone in the 0°→90°
    // sweep carries the steepest motion and the last zone before profile the
    // shallowest (motion visibly decelerates zone-over-zone; applying the peak
    // flat reads as mechanical sliding).
    //
    // SignedFraction is the signed distance from the current pose key in
    // half-zone units (0 = the key where the authored pose is EXACT,
    // +-1 = the next swap boundary). ZoneAnchorDeg is the SIGNED absolute
    // angle where the zone opens (the pose key; sign(Yaw) carries the left
    // half, so the −yaw turn mirrors +yaw exactly under the odd sine) and
    // HalfZoneWidthDeg is the zone's angular width in degrees. The sign of
    // depth inverts (Back layers) exactly as before; the sine slice itself is
    // sign-consistent with Yaw (a positive push stays positive through its
    // zone, the back half of the global sine returns toward 0 as the pose
    // converges to the exact back key).
    static inline double RampOffset(double SignedFraction, double DepthFactor,
        double MaxOffsetV = MaxOffset, double ZoneAnchorDeg = 0.0,
        double HalfZoneWidthDeg = 90.0)
    {
        const double F = ClampYaw(SignedFraction);
        const double Rad = Pi / 180.0;
        const double ThetaDeg = ZoneAnchorDeg + F * HalfZoneWidthDeg;
        return DepthFactor * MaxOffsetV
            * (std::sin(ThetaDeg * Rad) - std::sin(ZoneAnchorDeg * Rad));
    }

    // Mirror of UFaceParallaxComponent::ComputeOffsetForState's X term for
    // layers whose tag is NOT in the velocity hierarchy (unknown/user tags):
    // RampOffset(SignedFraction, DepthScale * (invert ? -1 : 1) * MaxOffset).
    // The input is the signed zone fraction (distance from the pose key in
    // half-zone units), not a clamped linear deviation.
    static inline double ComputeYawOffset(double DepthScale, bool bInvertParallax,
        double SignedFraction, double MaxOffsetV = MaxOffset)
    {
        const double DepthFactor = DepthScale * (bInvertParallax ? -1.0 : 1.0);
        return RampOffset(SignedFraction, DepthFactor, MaxOffsetV);
    }

    // Mirror of UFaceParallaxComponent::ComputeOffsetForState's X term for
    // layers whose tag IS in the velocity hierarchy: the per-tag rate IS the
    // displacement authority, replacing the DepthScale/invert composite
    // entirely (the rate's sign carries the direction).
    static inline double ComputeVelocityOffset(const char* Tag, double SignedFraction,
        double MaxOffsetV = MaxOffset)
    {
        return RampOffset(SignedFraction, FPSchematicTagParallaxRate(Tag), MaxOffsetV);
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

// The canonical left/right partner of a paired part (nullptr for unpaired
// parts). The left-half states resolve the PARTNER's ring mirrored so the
// near/far role split (Part IV Zone 1-3: Eye_Far_Narrow vs Eye_Near_3Q, far
// projection compression) stays role-correct on the mirrored turn — at −45
// the near card rides the left side exactly like the +45 view mirrored.
inline const char* FPSchematicPairPartner(const char* Name)
{
    if (!Name || !Name[0]) return nullptr;
    const std::string N(Name);
    if (N == "EyeL") return "EyeR";
    if (N == "EyeR") return "EyeL";
    if (N == "BrowL") return "BrowR";
    if (N == "BrowR") return "BrowL";
    if (N == "CheekL") return "CheekR";
    if (N == "CheekR") return "CheekL";
    if (N == "EarL") return "EarR";
    if (N == "EarR") return "EarL";
    return nullptr;
}

// Is the part a silhouette (head + the three hair layers)? Silhouettes never
// fade at back orientations and scale rigidly about the head centerline.
inline bool FPSchematicIsSilhouette(const char* Name)
{
    return Name && Name[0] && (std::string(Name) == "Head"
        || FPSchematicIsHairLayer(Name));
}

// Is the part a CENTERLINE facial feature? Per Part IV Zone 4, the profile
// hard swap merges the nose and mouth (and the teeth ring with it) into the
// profile contour line — they drop to 0% at the profile states, unlike the
// face-surface cards (cheeks/chin/neck) which keep a profile ring.
inline bool FPSchematicIsCenterFeature(const char* Name)
{
    if (!Name || !Name[0]) return false;
    const std::string N(Name);
    return N == "Nose" || N == "Mouth" || N == "Teeth";
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
// Zone pose keys use the default multipliers (1/2/3/4 x HalfZoneWidth 45) —
// the pose appears EXACTLY at its key angle (0/22.5/45/67.5/90/135/180 and
// the mirrored left half), which doubles as the hard swap into that view;
// Top/Bottom park at yaw 0 with pitch ±90.
// ============================================================================
inline double FPSchematicStateCenterYaw(int StateIdx)
{
    switch (StateIdx)
    {
    case 1:  return 22.5;    // NarrowRight
    case 2:  return 45.0;    // 3/4R
    case 3:  return 67.5;    // SliverRight
    case 4:  return 90.0;    // RightProfile
    case 5:  return 135.0;   // BackRight
    case 6:  return 180.0;   // Back
    case 7:  return -135.0;  // BackLeft
    case 8:  return -90.0;   // LeftProfile
    case 9:  return -67.5;   // SliverLeft
    case 10: return -45.0;   // 3/4L
    case 11: return -22.5;   // NarrowLeft
    default: return 0.0;     // Front + Top/Bottom
    }
}

inline double FPSchematicStateCenterPitch(int StateIdx)
{
    return StateIdx == 12 ? 90.0 : (StateIdx == 13 ? -90.0 : 0.0);
}

// Walk-behind states (BackRight / Back / BackLeft): |center yaw| >= 135°, the
// states where features fade out (the placeholder's FPFeatureAlphaAt is 0).
inline bool FPSchematicStateIsWalkBehind(int StateIdx)
{
    return std::abs(FPSchematicStateCenterYaw(StateIdx)) >= 135.0;
}

// Is the layer RENDERED in this state? The silhouette mass (Head + the hair
// layers) is present in every state. The far-side member of a paired layer
// (eyes/brows/cheeks/ears) is HIDDEN only AT the profile and beyond
// (|center yaw| >= 90) — the master blueprint keeps the far eye VISIBLE as a
// pre-drawn Eye_Narrow/Sliver through the 3/4, and the fold happens at the
// 90.1 hard swap, not the 45 one. Walk-behind states (BackRight/Back/BackLeft)
// hide the facial features AND the paired face cards, but the base-anchored
// projections (Ears) PERSIST as flat back-fuzz planes — they are AnchorCritical
// read-carriers, never "silhouette-only" (Phase 8: ears survive the back).
// Per-state visibility for a PART (the placeholder schematic + widget read).
// Master blueprint Part IV/V: the silhouette mass always renders; the ears
// (AnchorCritical projections) persist even walk-behind as flat back-fuzz
// planes; the far-side member of a pair hides at |yaw| >= 90 (the fold, not a
// dot); Part IV Zone 4 drops the centerline features (Nose/Mouth/Teeth) into
// the profile contour at the profile states; Part V.2 drops EVERY non-anchor
// card at the Top View (the crown converges to a near-featureless silhouette);
// walk-behind states hide every feature (Part IV Zone 5: 0% at 135/180).
// Unknown/null names default to hidden (a layer not in the canonical set is
// not part of the read).
inline bool FPSchematicLayerVisibleInState(int StateIdx, const char* LayerName)
{
    if (!LayerName || !LayerName[0]) return false;
    if (FPSchematicIsSilhouette(LayerName)) return true;
    if (FPSchematicStateIsWalkBehind(StateIdx))
        return FPSchematicAnchorClassForPart(LayerName) == FPSchematicAnchorClass::AnchorCritical;
    // Part V.2: at the Top View the face converges to the crown silhouette —
    // Primary Features (and the face-surface cards) drop to 0% in the same
    // keyframe (Swap Cohort). The ears stay: base-anchored projections still
    // read from above.
    if (StateIdx == 12
        && FPSchematicAnchorClassForPart(LayerName) == FPSchematicAnchorClass::BridgeSafe)
        return false;
    if (FPSchematicIsPairedPart(LayerName))
    {
        if (std::abs(FPSchematicStateCenterYaw(StateIdx)) < 90.0) return true;
        return !FPSchematicIsFarSide(LayerName, FPSchematicStateCenterYaw(StateIdx));
    }
    // Part IV Zone 4: the profile swap merges the nose and mouth into the
    // profile contour line — they drop to 0% at the profile states (4/8).
    if (FPSchematicIsCenterFeature(LayerName) && (StateIdx == 4 || StateIdx == 8))
        return false;
    if (FPSchematicAnchorClassForPart(LayerName) == FPSchematicAnchorClass::BridgeSafe)
        return true;
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
    // Master blueprint Part III Zone 5: at the TRUE back (state 6) the Back
    // Hair shifts to Layer 1 — from behind it IS the character's front plane.
    if (StateIdx == 6 && LayerName && std::string(LayerName) == "BackHair") return 1;
    return (int)FPZDepthForPart(LayerName);
}

// TAG-level variant for the RUNTIME: the component spawns ONE quad per
// base-preset tag (Eyes/Brows/Mouth/Bangs/Nose/Cheeks/Head/Hair/BackHair/
// Ears), so it cannot hide a single member of a pair — the whole card hides
// or shows. The paired fold is already handled by the placeholder slide; what
// this table adds is the WALK-BEHIND FADE + the TOP-VIEW DROP: the feature
// cards (BridgeSafe tags) hide in the walk-behind states so their front art
// cannot edge-peek around the skull (the placeholder's FPFeatureAlphaAt is 0
// there), and hide at the Top View (Part V.2 drops Primary Features to 0% in
// the crown swap), while the silhouette + ear cards (AnchorCritical tags)
// stay in the read.
inline bool FPSchematicLayerVisibleInTag(int StateIdx, const char* Tag)
{
    if (!Tag || !Tag[0]) return false;
    if (FPSchematicAnchorClassForTag(Tag) == FPSchematicAnchorClass::AnchorCritical)
        return true;
    if (FPSchematicStateIsWalkBehind(StateIdx)) return false;
    return StateIdx != 12;   // Top View: the feature cards drop with the swap
}

// Tag-level per-state Z-order: representative FPZDepth plane per base-preset
// tag (the tag's primary part). Hidden tags return -1 (not rendered).
inline int FPSchematicLayerOrderInTag(int StateIdx, const char* Tag)
{
    if (!FPSchematicLayerVisibleInTag(StateIdx, Tag)) return -1;
    // Master blueprint Part III Zone 5: Back Hair promotes to Layer 1 at the
    // true back (state 6), mirroring the per-part order contract.
    if (StateIdx == 6 && Tag && std::string(Tag) == "BackHair") return 1;
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
// B.2: Parameter-Space Crossfade (art_guide III.6 / IV.0). The crossfade
// opacity is a pure function of the ROTATION PARAMETER, never a frame count:
// a frame-count window is silently speed-dependent (the same 2-3 frames
// covers a much wider angular sweep during a fast drag than during a slow
// turn), so a parameter-space window fades the same angular sweep — and
// therefore looks the same — at any interaction speed. The fade is CENTERED
// on the hysteresis-adjusted trigger point (IV.0's directional Schmitt:
// forward at Boundary + Sign*1.5, reverse at Boundary - Sign*1.5), so alpha
// is exactly 0.5 at the trigger (the hard-swap key) and ramps linearly to
// 0 / 1 one half-window (0.75 deg) on either side. Retrace is implicit:
// reverse the camera and the opacity falls back in lockstep, so the blend
// never lingers on an in-between shape. This is the same linear ramp III.6's
// Trajectory Matching prescribes for a Peak-value change at a boundary,
// applied to the crossfade opacity.
// ----------------------------------------------------------------------------
static constexpr double FPSchematicCrossfadeHalfWindowDeg = 0.75;
static constexpr double FPSchematicCrossfadeSchmittDeg = 1.5;

inline double FPSchematicCrossfadeAlpha(double ParamDeg, double BoundaryDeg,
                                        double DirectionSign)
{
    const double Sign = (DirectionSign >= 0.0) ? 1.0 : -1.0;
    const double TriggerDeg = BoundaryDeg + Sign * FPSchematicCrossfadeSchmittDeg;
    // Wrap the sweep so a back-wrap crossing (boundary +180, param wrapped to
    // -175 after the trigger fired at 181.5) still measures the signed sweep.
    double Diff = ParamDeg - TriggerDeg;
    if (Diff > 180.0) Diff -= 360.0;
    else if (Diff < -180.0) Diff += 360.0;
    const double Alpha = 0.5 + Sign * Diff
        / (2.0 * FPSchematicCrossfadeHalfWindowDeg);
    return (Alpha < 0.0) ? 0.0 : ((Alpha > 1.0) ? 1.0 : Alpha);
}

// ----------------------------------------------------------------------------
// B.3: Directional Schmitt state-flip commit (art_guide IV.0). The view flip
// is NOT a frame-count debounce — a frame window is speed-dependent (the same
// 3 frames covers a wide angular sweep during a fast drag and a sliver during
// a slow hover), the same defect class the B.2 crossfade removed. Instead the
// flip COMMITS only once the live rotation parameter has passed the shared
// swap boundary by the hysteresis margin in the direction of travel:
// forward at Boundary + Sign*1.5, reverse at Boundary - Sign*1.5. The margin
// is the SAME constant the parameter-space crossfade centers on
// (FPSchematicCrossfadeSchmittDeg), so the commit key coincides exactly with
// the alpha = 0.5 crossfade key: the instant the view flips, the incoming
// card is already half-swapped in, and the sweep from commit to full swap is
// the same 0.75-degree window at any interaction speed. Retrace is implicit —
// reverse the camera and the flip re-arms only after passing back through the
// trigger on the other side, so a slow hover at a threshold never toggles.
// ----------------------------------------------------------------------------
inline double FPSchematicSchmittTriggerAt(double BoundaryDeg, double DirectionSign)
{
    const double Sign = (DirectionSign >= 0.0) ? 1.0 : -1.0;
    return BoundaryDeg + Sign * FPSchematicCrossfadeSchmittDeg;
}

// The flip commits once the live parameter reaches/passes the directional
// trigger: Sign * (ParamDeg - TriggerDeg) >= 0. The sweep wraps across +-180
// so the Back<->BackLeft pair keeps measuring the signed travel (commit fires
// when the normalized param wraps past the trigger on the far side).
inline bool FPSchematicSchmittCrossed(double ParamDeg, double BoundaryDeg,
                                      double DirectionSign)
{
    const double Sign = (DirectionSign >= 0.0) ? 1.0 : -1.0;
    const double TriggerDeg = FPSchematicSchmittTriggerAt(BoundaryDeg, Sign);
    double Diff = ParamDeg - TriggerDeg;
    if (Diff > 180.0) Diff -= 360.0;
    else if (Diff < -180.0) Diff += 360.0;
    return Sign * Diff >= 0.0;
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
        // Head: cranium-circle front -> 3/4 -> profile forehead-nose-chin ->
        // back-3/4 -> featureless back sphere (Part I.2 / Part IV).
        { "Head", {
             { SPT(0.5, 0.02), SPT(0.2624, 0.1184), SPT(0.164, 0.356), SPT(0.157, 0.455), SPT(0.23, 0.64), SPT(0.42, 0.845), SPT(0.5, 0.86), SPT(0.58, 0.845), SPT(0.77, 0.64), SPT(0.843, 0.455), SPT(0.836, 0.356), SPT(0.7376, 0.1184) },
             { SPT(0.51, 0.02), SPT(0.32, 0.1), SPT(0.24, 0.33), SPT(0.2, 0.44), SPT(0.27, 0.62), SPT(0.42, 0.83), SPT(0.5, 0.85), SPT(0.59, 0.82), SPT(0.76, 0.63), SPT(0.845, 0.44), SPT(0.88, 0.34), SPT(0.79, 0.1) },
             { SPT(0.5, 0.02), SPT(0.33, 0.07), SPT(0.24, 0.2), SPT(0.22, 0.36), SPT(0.21, 0.52), SPT(0.20, 0.645), SPT(0.3, 0.84), SPT(0.62, 0.79), SPT(0.7, 0.64), SPT(0.74, 0.46), SPT(0.68, 0.22), SPT(0.6, 0.06) },
             { SPT(0.5, 0.02), SPT(0.38, 0.08), SPT(0.3, 0.22), SPT(0.22, 0.36), SPT(0.22, 0.5), SPT(0.33, 0.62), SPT(0.45, 0.72), SPT(0.62, 0.72), SPT(0.77, 0.6), SPT(0.85, 0.44), SPT(0.87, 0.28), SPT(0.8, 0.09) },
             { SPT(0.5, 0.02), SPT(0.3, 0.07), SPT(0.13, 0.22), SPT(0.065, 0.44), SPT(0.13, 0.66), SPT(0.3, 0.81), SPT(0.5, 0.86), SPT(0.7, 0.81), SPT(0.87, 0.66), SPT(0.935, 0.44), SPT(0.87, 0.22), SPT(0.7, 0.07) },
             { SPT(0.5, 0.12), SPT(0.33, 0.14), SPT(0.18, 0.22), SPT(0.12, 0.35), SPT(0.1, 0.5), SPT(0.12, 0.63), SPT(0.22, 0.73), SPT(0.36, 0.79), SPT(0.5, 0.8), SPT(0.64, 0.79), SPT(0.78, 0.73), SPT(0.88, 0.63) },
             { SPT(0.5, 0.16), SPT(0.33, 0.2), SPT(0.19, 0.3), SPT(0.1, 0.44), SPT(0.07, 0.6), SPT(0.15, 0.75), SPT(0.3, 0.86), SPT(0.5, 0.9), SPT(0.7, 0.86), SPT(0.85, 0.75), SPT(0.93, 0.6), SPT(0.9, 0.44) }
        } },
        // Bangs: raised forehead fringe -> compressed 3/4 -> compact profile wedge
        // over the brow -> back-3/4 sweep -> back hairline (Part I.6 / Part IV).
        { "Bangs", {
             { SPT(0.21, 0.26), SPT(0.185, 0.1), SPT(0.23, 0.035), SPT(0.28, 0.028), SPT(0.33, 0.02), SPT(0.37, 0.1), SPT(0.4, 0.028), SPT(0.45, 0.02), SPT(0.455, 0.009), SPT(0.47, 0.005), SPT(0.484, 0.009), SPT(0.5, 0.02), SPT(0.55, 0.02), SPT(0.6, 0.028), SPT(0.63, 0.1), SPT(0.67, 0.02), SPT(0.72, 0.028), SPT(0.77, 0.035), SPT(0.815, 0.1), SPT(0.79, 0.26), SPT(0.72, 0.265), SPT(0.65, 0.252), SPT(0.58, 0.262), SPT(0.5, 0.258), SPT(0.42, 0.262), SPT(0.35, 0.252), SPT(0.28, 0.265) },
             { SPT(0.3, 0.26), SPT(0.28, 0.14), SPT(0.31, 0.07), SPT(0.35, 0.04), SPT(0.4, 0.03), SPT(0.45, 0.04), SPT(0.49, 0.035), SPT(0.54, 0.035), SPT(0.544, 0.019), SPT(0.556, 0.015), SPT(0.5672, 0.019), SPT(0.58, 0.04), SPT(0.63, 0.05), SPT(0.67, 0.07), SPT(0.7, 0.11), SPT(0.73, 0.16), SPT(0.75, 0.21), SPT(0.76, 0.26), SPT(0.77, 0.31), SPT(0.72, 0.27), SPT(0.66, 0.25), SPT(0.6, 0.24), SPT(0.54, 0.25), SPT(0.48, 0.26), SPT(0.41, 0.24), SPT(0.35, 0.23), SPT(0.31, 0.25) },
             { SPT(0.4, 0.28), SPT(0.38, 0.16), SPT(0.4, 0.1), SPT(0.44, 0.06), SPT(0.49, 0.04), SPT(0.54, 0.04), SPT(0.58, 0.05), SPT(0.62, 0.07), SPT(0.624, 0.059), SPT(0.636, 0.055), SPT(0.6472, 0.059), SPT(0.66, 0.1), SPT(0.69, 0.14), SPT(0.71, 0.19), SPT(0.73, 0.24), SPT(0.74, 0.29), SPT(0.74, 0.34), SPT(0.72, 0.38), SPT(0.69, 0.41), SPT(0.63, 0.36), SPT(0.57, 0.33), SPT(0.52, 0.31), SPT(0.47, 0.3), SPT(0.42, 0.3), SPT(0.39, 0.32), SPT(0.38, 0.34), SPT(0.39, 0.31) },
             { SPT(0.3, 0.34), SPT(0.3, 0.22), SPT(0.33, 0.13), SPT(0.38, 0.08), SPT(0.44, 0.06), SPT(0.5, 0.06), SPT(0.55, 0.06), SPT(0.6, 0.07), SPT(0.605, 0.059), SPT(0.62, 0.055), SPT(0.634, 0.059), SPT(0.65, 0.09), SPT(0.7, 0.13), SPT(0.74, 0.18), SPT(0.77, 0.24), SPT(0.79, 0.3), SPT(0.79, 0.36), SPT(0.77, 0.41), SPT(0.73, 0.44), SPT(0.67, 0.39), SPT(0.6, 0.36), SPT(0.54, 0.34), SPT(0.47, 0.34), SPT(0.41, 0.35), SPT(0.35, 0.36), SPT(0.31, 0.37), SPT(0.29, 0.36) },
             { SPT(0.14, 0.6), SPT(0.13, 0.42), SPT(0.15, 0.28), SPT(0.2, 0.17), SPT(0.27, 0.1), SPT(0.34, 0.06), SPT(0.42, 0.04), SPT(0.5, 0.04), SPT(0.508, 0.029), SPT(0.532, 0.025), SPT(0.5544, 0.029), SPT(0.58, 0.04), SPT(0.66, 0.06), SPT(0.73, 0.1), SPT(0.8, 0.17), SPT(0.85, 0.28), SPT(0.87, 0.42), SPT(0.86, 0.6), SPT(0.83, 0.72), SPT(0.76, 0.68), SPT(0.68, 0.66), SPT(0.6, 0.65), SPT(0.52, 0.64), SPT(0.44, 0.65), SPT(0.36, 0.66), SPT(0.28, 0.68), SPT(0.2, 0.7) },
              { SPT(0.28, 0.18), SPT(0.28, 0.12), SPT(0.32, 0.07), SPT(0.38, 0.04), SPT(0.45, 0.03), SPT(0.5, 0.03), SPT(0.55, 0.03), SPT(0.62, 0.04), SPT(0.626, 0.029), SPT(0.644, 0.025), SPT(0.6608, 0.029), SPT(0.68, 0.07), SPT(0.72, 0.12), SPT(0.72, 0.18), SPT(0.69, 0.23), SPT(0.64, 0.26), SPT(0.58, 0.27), SPT(0.5, 0.28), SPT(0.42, 0.27), SPT(0.36, 0.26), SPT(0.31, 0.23), SPT(0.28, 0.2), SPT(0.285, 0.17), SPT(0.29, 0.15), SPT(0.3, 0.14), SPT(0.29, 0.16), SPT(0.285, 0.17) },
             { SPT(0.2, 0.4), SPT(0.19, 0.28), SPT(0.22, 0.18), SPT(0.27, 0.11), SPT(0.33, 0.07), SPT(0.39, 0.05), SPT(0.45, 0.05), SPT(0.51, 0.05), SPT(0.516, 0.039), SPT(0.534, 0.035), SPT(0.5508, 0.039), SPT(0.57, 0.05), SPT(0.62, 0.07), SPT(0.67, 0.11), SPT(0.72, 0.18), SPT(0.76, 0.28), SPT(0.77, 0.4), SPT(0.74, 0.44), SPT(0.69, 0.47), SPT(0.63, 0.42), SPT(0.56, 0.4), SPT(0.5, 0.38), SPT(0.43, 0.4), SPT(0.36, 0.42), SPT(0.3, 0.44), SPT(0.24, 0.45), SPT(0.2, 0.43) }
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
              SPT(0.65, 0.04), SPT(0.69, 0.06), SPT(0.76, 0.08), SPT(0.82, 0.11),
              SPT(0.85, 0.16), SPT(0.89, 0.22), SPT(0.92, 0.30), SPT(0.94, 0.40),
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
              SPT(0.77, 0.07), SPT(0.82, 0.10), SPT(0.86, 0.13), SPT(0.89, 0.18),
              SPT(0.91, 0.24), SPT(0.93, 0.32), SPT(0.92, 0.40), SPT(0.94, 0.50),
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
            { SPT(0.30, 0.72), SPT(0.44, 0.67), SPT(0.56, 0.67), SPT(0.68, 0.68),
              SPT(0.78, 0.74), SPT(0.80, 0.86), SPT(0.60, 0.94), SPT(0.34, 0.94) },
            { SPT(0.34, 0.74), SPT(0.46, 0.68), SPT(0.58, 0.68), SPT(0.68, 0.70),
              SPT(0.78, 0.76), SPT(0.80, 0.88), SPT(0.62, 0.94), SPT(0.36, 0.94) }
        } },
        // ====================================================================
        // Phase 2 authored FEATURE cards (guide Parts IV/V). The 13
        // anatomical features now carry EXPLICIT hand-authored per-view rings
        // (created art, not a runtime formula): each ring is the front glyph
        // constructed for that view following the guide's Compressed Grid
        // (Part I.4) + Yaw Zones (Part IV) + Pitch (Part V). All 13 cards'
        // P45/P90 slots are hand-authored; P135 stays the ear back-fuzz band
        // and P180/PTop/PBottom keep the formula results (see
        // FPSchematicFeatureRingAt). The P45 slot is the near/far role split
        // (near eye ~0.84 vs far eye ~0.50, near brow ~0.80 vs far brow
        // ~0.60, near ear ~0.91 vs far ear ~0.76, near cheek ~0.82 vs far
        // cheek ~0.70, compressed far projection), authored with PER-SEGMENT
        // compression (the outer/profile-side edge compresses
        // more than the nose-side edge — never a uniform scale); the near
        // member rides the turn side, the far member narrows; the centerline
        // features dart
        // toward the turn side (Nose +0.05, Mouth/Teeth +0.02); P90 is the
        // profile sliver (Eye_Profile single lash, brow/cheek slivers, tall
        // ear, narrow neck, tiny nose/mouth/teeth/chin placeholders that drop
        // INTO the profile contour); P135
        // is the ear back-fuzz band (wider + shorter, Part IV Zone 5) with
        // every other feature a placeholder (hidden walk-behind); P180 is the
        // folded card dropped -0.14 in Y so every vertex clears the >10%
        // back-change gate; PTop = PBottom = the front glyph (Top never
        // renders features per Part V.2, Bottom keeps the front read per V.4).
        // Paired cards mirror slot-for-slot for P0/P90/P135/P180/PTop/PBottom;
        // only P45 is independently authored (near vs far). Vertex
        // correspondence with the front glyph is preserved (clean morph).
        { "EyeL", {
             { SPT(0.244737, 0.485087), SPT(0.260263, 0.427626), SPT(0.274737, 0.412424), SPT(0.291842, 0.418171), SPT(0.310263, 0.401946), SPT(0.314539, 0.416107), SPT(0.318816, 0.425178), SPT(0.323092, 0.418980), SPT(0.327368, 0.417872), SPT(0.425263, 0.438312), SPT(0.351053, 0.508435), SPT(0.298421, 0.537340), SPT(0.244737, 0.485087) },
             { SPT(0.275643, 0.465334), SPT(0.283406, 0.436604), SPT(0.290643, 0.429003), SPT(0.299195, 0.431876), SPT(0.308406, 0.423764), SPT(0.310544, 0.430844), SPT(0.312682, 0.435380), SPT(0.314820, 0.432281), SPT(0.316958, 0.431727), SPT(0.365906, 0.441947), SPT(0.328801, 0.477008), SPT(0.302485, 0.491461), SPT(0.275643, 0.465334) },
             { SPT(0.347200, 0.441876), SPT(0.348800, 0.399447), SPT(0.351200, 0.389804), SPT(0.353200, 0.397268), SPT(0.355200, 0.387732), SPT(0.356400, 0.399411), SPT(0.357200, 0.407196), SPT(0.356000, 0.403518), SPT(0.355200, 0.403732), SPT(0.361200, 0.438124), SPT(0.359200, 0.478660), SPT(0.353200, 0.492268), SPT(0.347200, 0.441876) },
             { SPT(0.303036, 0.452065), SPT(0.312476, 0.415936), SPT(0.321276, 0.406378), SPT(0.331676, 0.409991), SPT(0.342876, 0.399790), SPT(0.345476, 0.411093), SPT(0.348076, 0.418397), SPT(0.350676, 0.413700), SPT(0.353276, 0.409803), SPT(0.412796, 0.422655), SPT(0.367676, 0.466745), SPT(0.335676, 0.484919), SPT(0.303036, 0.452065) },
             { SPT(0.303036, 0.312065), SPT(0.312476, 0.275936), SPT(0.321276, 0.266378), SPT(0.331676, 0.269991), SPT(0.342876, 0.259790), SPT(0.345476, 0.271093), SPT(0.348076, 0.278397), SPT(0.350676, 0.273700), SPT(0.353276, 0.269803), SPT(0.412796, 0.282655), SPT(0.367676, 0.326745), SPT(0.335676, 0.344919), SPT(0.303036, 0.312065) },
             { SPT(0.294200, 0.458381), SPT(0.306000, 0.413220), SPT(0.317000, 0.401272), SPT(0.330000, 0.405789), SPT(0.344000, 0.393037), SPT(0.347250, 0.404167), SPT(0.350500, 0.411296), SPT(0.353750, 0.406425), SPT(0.357000, 0.405554), SPT(0.431400, 0.421619), SPT(0.375000, 0.476731), SPT(0.335000, 0.499449), SPT(0.294200, 0.458381) },
             { SPT(0.294200, 0.458381), SPT(0.306000, 0.413220), SPT(0.317000, 0.401272), SPT(0.330000, 0.405789), SPT(0.344000, 0.393037), SPT(0.347250, 0.404167), SPT(0.350500, 0.411296), SPT(0.353750, 0.406425), SPT(0.357000, 0.405554), SPT(0.431400, 0.421619), SPT(0.375000, 0.476731), SPT(0.335000, 0.499449), SPT(0.294200, 0.458381) }
        } },
        { "EyeR", {
             { SPT(0.755263, 0.485087), SPT(0.739737, 0.427626), SPT(0.725263, 0.412424), SPT(0.708158, 0.418171), SPT(0.689737, 0.401946), SPT(0.685461, 0.416107), SPT(0.681184, 0.425178), SPT(0.676908, 0.418980), SPT(0.672632, 0.417872), SPT(0.574737, 0.438312), SPT(0.648947, 0.508435), SPT(0.701579, 0.537340), SPT(0.755263, 0.485087) },
             { SPT(0.745373, 0.478766), SPT(0.732331, 0.430499), SPT(0.720173, 0.417729), SPT(0.705805, 0.422557), SPT(0.690331, 0.408928), SPT(0.686739, 0.420823), SPT(0.683147, 0.428443), SPT(0.679555, 0.423236), SPT(0.675963, 0.422306), SPT(0.593731, 0.439475), SPT(0.656068, 0.498379), SPT(0.700279, 0.522659), SPT(0.745373, 0.478766) },
             { SPT(0.652800, 0.441876), SPT(0.651200, 0.399447), SPT(0.648800, 0.389804), SPT(0.646800, 0.397268), SPT(0.644800, 0.387732), SPT(0.643600, 0.399411), SPT(0.642800, 0.407196), SPT(0.644000, 0.403518), SPT(0.644800, 0.403732), SPT(0.638800, 0.438124), SPT(0.640800, 0.478660), SPT(0.646800, 0.492268), SPT(0.652800, 0.441876) },
             { SPT(0.696964, 0.452065), SPT(0.687524, 0.415936), SPT(0.678724, 0.406378), SPT(0.668324, 0.409991), SPT(0.657124, 0.399790), SPT(0.654524, 0.411093), SPT(0.651924, 0.418397), SPT(0.649324, 0.413700), SPT(0.646724, 0.409803), SPT(0.587204, 0.422655), SPT(0.632324, 0.466745), SPT(0.664324, 0.484919), SPT(0.696964, 0.452065) },
             { SPT(0.696964, 0.312065), SPT(0.687524, 0.275936), SPT(0.678724, 0.266378), SPT(0.668324, 0.269991), SPT(0.657124, 0.259790), SPT(0.654524, 0.271093), SPT(0.651924, 0.278397), SPT(0.649324, 0.273700), SPT(0.646724, 0.269803), SPT(0.587204, 0.282655), SPT(0.632324, 0.326745), SPT(0.664324, 0.344919), SPT(0.696964, 0.312065) },
             { SPT(0.755263, 0.485087), SPT(0.739737, 0.427626), SPT(0.725263, 0.412424), SPT(0.708158, 0.418171), SPT(0.689737, 0.401946), SPT(0.685461, 0.416107), SPT(0.681184, 0.425178), SPT(0.676908, 0.418980), SPT(0.672632, 0.417872), SPT(0.574737, 0.438312), SPT(0.648947, 0.508435), SPT(0.701579, 0.537340), SPT(0.755263, 0.485087) },
             { SPT(0.755263, 0.485087), SPT(0.739737, 0.427626), SPT(0.725263, 0.412424), SPT(0.708158, 0.418171), SPT(0.689737, 0.401946), SPT(0.685461, 0.416107), SPT(0.681184, 0.425178), SPT(0.676908, 0.418980), SPT(0.672632, 0.417872), SPT(0.574737, 0.438312), SPT(0.648947, 0.508435), SPT(0.701579, 0.537340), SPT(0.755263, 0.485087) }
        } },
        { "BrowL", {
             { SPT(0.28, 0.20), SPT(0.32, 0.17), SPT(0.36, 0.165), SPT(0.40, 0.17), SPT(0.435, 0.195), SPT(0.42, 0.20), SPT(0.36, 0.175), SPT(0.32, 0.18), SPT(0.285, 0.205) },
             { SPT(0.309333, 0.193778), SPT(0.333333, 0.175778), SPT(0.357333, 0.172778), SPT(0.381333, 0.175778), SPT(0.402333, 0.190778), SPT(0.393333, 0.193778), SPT(0.357333, 0.178778), SPT(0.333333, 0.181778), SPT(0.312333, 0.196778) },
             { SPT(0.347875, 0.215), SPT(0.353125, 0.185), SPT(0.360625, 0.180), SPT(0.380050, 0.185), SPT(0.387850, 0.210), SPT(0.381350, 0.215), SPT(0.362125, 0.185), SPT(0.353125, 0.190), SPT(0.348625, 0.215) },
             { SPT(0.282444, 0.215622), SPT(0.310444, 0.192822), SPT(0.350444, 0.188022), SPT(0.414444, 0.196022), SPT(0.438444, 0.215622), SPT(0.418444, 0.218822), SPT(0.358444, 0.196022), SPT(0.310444, 0.199222), SPT(0.286444, 0.218822) },
             { SPT(0.294667, 0.056889), SPT(0.326667, 0.032889), SPT(0.358667, 0.028889), SPT(0.390667, 0.032889), SPT(0.418667, 0.052889), SPT(0.406667, 0.056889), SPT(0.358667, 0.036889), SPT(0.326667, 0.040889), SPT(0.298667, 0.060889) },
             { SPT(0.28, 0.20), SPT(0.32, 0.17), SPT(0.36, 0.165), SPT(0.40, 0.17), SPT(0.435, 0.195), SPT(0.42, 0.20), SPT(0.36, 0.175), SPT(0.32, 0.18), SPT(0.285, 0.205) },
             { SPT(0.28, 0.20), SPT(0.32, 0.17), SPT(0.36, 0.165), SPT(0.40, 0.17), SPT(0.435, 0.195), SPT(0.42, 0.20), SPT(0.36, 0.175), SPT(0.32, 0.18), SPT(0.285, 0.205) }
        } },
        { "BrowR", {
             { SPT(0.72, 0.20), SPT(0.68, 0.17), SPT(0.64, 0.165), SPT(0.60, 0.17), SPT(0.565, 0.195), SPT(0.58, 0.20), SPT(0.64, 0.175), SPT(0.68, 0.18), SPT(0.715, 0.205) },
             { SPT(0.705333, 0.196889), SPT(0.673333, 0.172889), SPT(0.641333, 0.168889), SPT(0.609333, 0.172889), SPT(0.581333, 0.192889), SPT(0.593333, 0.196889), SPT(0.641333, 0.176889), SPT(0.673333, 0.180889), SPT(0.701333, 0.200889) },
             { SPT(0.652125, 0.215), SPT(0.646875, 0.185), SPT(0.639375, 0.180), SPT(0.619950, 0.185), SPT(0.612150, 0.210), SPT(0.618650, 0.215), SPT(0.637875, 0.185), SPT(0.646875, 0.190), SPT(0.651375, 0.215) },
             { SPT(0.717556, 0.215622), SPT(0.689556, 0.192822), SPT(0.649556, 0.188022), SPT(0.585556, 0.196022), SPT(0.561556, 0.215622), SPT(0.581556, 0.218822), SPT(0.641556, 0.196022), SPT(0.689556, 0.199222), SPT(0.713556, 0.218822) },
             { SPT(0.705333, 0.056889), SPT(0.673333, 0.032889), SPT(0.641333, 0.028889), SPT(0.609333, 0.032889), SPT(0.581333, 0.052889), SPT(0.593333, 0.056889), SPT(0.641333, 0.036889), SPT(0.673333, 0.040889), SPT(0.701333, 0.060889) },
             { SPT(0.72, 0.20), SPT(0.68, 0.17), SPT(0.64, 0.165), SPT(0.60, 0.17), SPT(0.565, 0.195), SPT(0.58, 0.20), SPT(0.64, 0.175), SPT(0.68, 0.18), SPT(0.715, 0.205) },
             { SPT(0.72, 0.20), SPT(0.68, 0.17), SPT(0.64, 0.165), SPT(0.60, 0.17), SPT(0.565, 0.195), SPT(0.58, 0.20), SPT(0.64, 0.175), SPT(0.68, 0.18), SPT(0.715, 0.205) }
        } },
        { "CheekL", {
             { SPT(0.08, 0.48), SPT(0.14, 0.44), SPT(0.22, 0.50), SPT(0.26, 0.58), SPT(0.24, 0.70), SPT(0.16, 0.74), SPT(0.10, 0.68), SPT(0.08, 0.58) },
             { SPT(0.115, 0.46), SPT(0.1535, 0.425), SPT(0.2305, 0.5), SPT(0.2585, 0.6), SPT(0.2375, 0.72), SPT(0.1815, 0.76), SPT(0.1325, 0.71), SPT(0.115, 0.62) },
             { SPT(0.102, 0.46), SPT(0.110, 0.425), SPT(0.115, 0.5), SPT(0.118, 0.6), SPT(0.114, 0.72), SPT(0.106, 0.76), SPT(0.100, 0.71), SPT(0.102, 0.62) },
             { SPT(0.093, 0.487875), SPT(0.137, 0.459875), SPT(0.225, 0.519875), SPT(0.257, 0.599875), SPT(0.233, 0.695875), SPT(0.169, 0.727875), SPT(0.113, 0.687875), SPT(0.093, 0.615875) },
             { SPT(0.093, 0.347875), SPT(0.137, 0.319875), SPT(0.225, 0.379875), SPT(0.257, 0.459875), SPT(0.233, 0.555875), SPT(0.169, 0.587875), SPT(0.113, 0.547875), SPT(0.093, 0.475875) },
             { SPT(0.08, 0.48), SPT(0.14, 0.44), SPT(0.22, 0.50), SPT(0.26, 0.58), SPT(0.24, 0.70), SPT(0.16, 0.74), SPT(0.10, 0.68), SPT(0.08, 0.58) },
             { SPT(0.08, 0.48), SPT(0.14, 0.44), SPT(0.22, 0.50), SPT(0.26, 0.58), SPT(0.24, 0.70), SPT(0.16, 0.74), SPT(0.10, 0.68), SPT(0.08, 0.58) }
        } },
        { "CheekR", {
             { SPT(0.92, 0.48), SPT(0.86, 0.44), SPT(0.78, 0.50), SPT(0.74, 0.58), SPT(0.76, 0.70), SPT(0.84, 0.74), SPT(0.90, 0.68), SPT(0.92, 0.58) },
             { SPT(0.9010, 0.46), SPT(0.8556, 0.425), SPT(0.7650, 0.5), SPT(0.7320, 0.6), SPT(0.7567, 0.72), SPT(0.8227, 0.76), SPT(0.8804, 0.71), SPT(0.9010, 0.62) },
             { SPT(0.898, 0.46), SPT(0.890, 0.425), SPT(0.885, 0.5), SPT(0.882, 0.6), SPT(0.886, 0.72), SPT(0.894, 0.76), SPT(0.900, 0.71), SPT(0.898, 0.62) },
             { SPT(0.907, 0.487875), SPT(0.863, 0.459875), SPT(0.775, 0.519875), SPT(0.743, 0.599875), SPT(0.767, 0.695875), SPT(0.831, 0.727875), SPT(0.887, 0.687875), SPT(0.907, 0.615875) },
             { SPT(0.907, 0.347875), SPT(0.863, 0.319875), SPT(0.775, 0.379875), SPT(0.743, 0.459875), SPT(0.767, 0.555875), SPT(0.831, 0.587875), SPT(0.887, 0.547875), SPT(0.907, 0.475875) },
             { SPT(0.92, 0.48), SPT(0.86, 0.44), SPT(0.78, 0.50), SPT(0.74, 0.58), SPT(0.76, 0.70), SPT(0.84, 0.74), SPT(0.90, 0.68), SPT(0.92, 0.58) },
             { SPT(0.92, 0.48), SPT(0.86, 0.44), SPT(0.78, 0.50), SPT(0.74, 0.58), SPT(0.76, 0.70), SPT(0.84, 0.74), SPT(0.90, 0.68), SPT(0.92, 0.58) }
        } },
        { "EarL", {
             { SPT(0.05, 0.22), SPT(0.09, 0.20), SPT(0.12, 0.28), SPT(0.11, 0.48), SPT(0.09, 0.70), SPT(0.06, 0.48) },
              { SPT(0.058800, 0.261600), SPT(0.089200, 0.246400), SPT(0.112000, 0.307200), SPT(0.104400, 0.459200), SPT(0.089200, 0.626400), SPT(0.066400, 0.459200) },
             { SPT(0.290, 0.18), SPT(0.296, 0.15), SPT(0.302, 0.18), SPT(0.300, 0.60), SPT(0.296, 0.70), SPT(0.286, 0.60) },
               { SPT(0.17175, 0.469167), SPT(0.22375, 0.451667), SPT(0.28875, 0.469167), SPT(0.28225, 0.559167), SPT(0.22375, 0.601667), SPT(0.18475, 0.559167) },
              { SPT(0.057333, 0.114667), SPT(0.089333, 0.098667), SPT(0.113333, 0.162667), SPT(0.105333, 0.322667), SPT(0.089333, 0.498667), SPT(0.065333, 0.322667) },
              { SPT(0.02, 0.42), SPT(0.06, 0.385), SPT(0.11, 0.42), SPT(0.105, 0.6), SPT(0.06, 0.685), SPT(0.03, 0.6) },
              { SPT(0.02, 0.42), SPT(0.06, 0.385), SPT(0.11, 0.42), SPT(0.105, 0.6), SPT(0.06, 0.685), SPT(0.03, 0.6) }
        } },
        { "EarR", {
             { SPT(0.95, 0.22), SPT(0.91, 0.20), SPT(0.88, 0.28), SPT(0.89, 0.48), SPT(0.91, 0.70), SPT(0.94, 0.48) },
              { SPT(0.946700, 0.235600), SPT(0.910300, 0.217400), SPT(0.883000, 0.290200), SPT(0.892100, 0.472200), SPT(0.910300, 0.672400), SPT(0.937600, 0.472200) },
             { SPT(0.710, 0.18), SPT(0.704, 0.15), SPT(0.698, 0.18), SPT(0.700, 0.60), SPT(0.704, 0.70), SPT(0.714, 0.60) },
               { SPT(0.82825, 0.469167), SPT(0.77625, 0.451667), SPT(0.71125, 0.469167), SPT(0.71775, 0.559167), SPT(0.77625, 0.601667), SPT(0.81525, 0.559167) },
              { SPT(0.942667, 0.114667), SPT(0.910667, 0.098667), SPT(0.886667, 0.162667), SPT(0.894667, 0.322667), SPT(0.910667, 0.498667), SPT(0.934667, 0.322667) },
              { SPT(0.98, 0.42), SPT(0.94, 0.385), SPT(0.89, 0.42), SPT(0.895, 0.6), SPT(0.94, 0.685), SPT(0.97, 0.6) },
              { SPT(0.98, 0.42), SPT(0.94, 0.385), SPT(0.89, 0.42), SPT(0.895, 0.6), SPT(0.94, 0.685), SPT(0.97, 0.6) }
        } },
        { "Nose", {
             { SPT(0.50, 0.675), SPT(0.495, 0.695), SPT(0.505, 0.695) },
             { SPT(0.515, 0.680), SPT(0.511, 0.693), SPT(0.519, 0.693) },
             { SPT(0.548, 0.682), SPT(0.547, 0.690), SPT(0.552, 0.690) },
             { SPT(0.492, 0.680), SPT(0.496, 0.693), SPT(0.488, 0.693) },
             { SPT(0.500000, 0.537667), SPT(0.496000, 0.553667), SPT(0.504000, 0.553667) },
             { SPT(0.50, 0.675), SPT(0.495, 0.695), SPT(0.505, 0.695) },
             { SPT(0.50, 0.675), SPT(0.495, 0.695), SPT(0.505, 0.695) }
        } },
        { "Mouth", {
             { SPT(0.465, 0.785), SPT(0.48, 0.780), SPT(0.50, 0.778), SPT(0.52, 0.780), SPT(0.535, 0.785), SPT(0.535, 0.805), SPT(0.52, 0.813), SPT(0.50, 0.815), SPT(0.48, 0.813), SPT(0.465, 0.805) },
             { SPT(0.490, 0.787), SPT(0.503, 0.782), SPT(0.520, 0.780), SPT(0.535, 0.782), SPT(0.546, 0.787), SPT(0.546, 0.803), SPT(0.535, 0.810), SPT(0.520, 0.812), SPT(0.503, 0.810), SPT(0.490, 0.803) },
             { SPT(0.560, 0.787), SPT(0.570, 0.782), SPT(0.578, 0.780), SPT(0.585, 0.782), SPT(0.590, 0.787), SPT(0.590, 0.803), SPT(0.585, 0.810), SPT(0.578, 0.812), SPT(0.570, 0.810), SPT(0.560, 0.803) },
             { SPT(0.455, 0.787), SPT(0.468, 0.782), SPT(0.485, 0.780), SPT(0.505, 0.782), SPT(0.516, 0.787), SPT(0.516, 0.803), SPT(0.505, 0.810), SPT(0.485, 0.812), SPT(0.468, 0.810), SPT(0.455, 0.803) },
             { SPT(0.472000, 0.647180), SPT(0.484000, 0.643180), SPT(0.500000, 0.641580), SPT(0.516000, 0.643180), SPT(0.528000, 0.647180), SPT(0.528000, 0.663180), SPT(0.516000, 0.669580), SPT(0.500000, 0.671180), SPT(0.484000, 0.669580), SPT(0.472000, 0.663180) },
             { SPT(0.465, 0.785), SPT(0.48, 0.780), SPT(0.50, 0.778), SPT(0.52, 0.780), SPT(0.535, 0.785), SPT(0.535, 0.805), SPT(0.52, 0.813), SPT(0.50, 0.815), SPT(0.48, 0.813), SPT(0.465, 0.805) },
             { SPT(0.465, 0.785), SPT(0.48, 0.780), SPT(0.50, 0.778), SPT(0.52, 0.780), SPT(0.535, 0.785), SPT(0.535, 0.805), SPT(0.52, 0.813), SPT(0.50, 0.815), SPT(0.48, 0.813), SPT(0.465, 0.805) }
        } },
        { "Teeth", {
             { SPT(0.475, 0.782), SPT(0.525, 0.782), SPT(0.53, 0.790), SPT(0.50, 0.794), SPT(0.47, 0.790) },
             { SPT(0.491, 0.785), SPT(0.539, 0.785), SPT(0.543, 0.791), SPT(0.515, 0.795), SPT(0.495, 0.791) },
             { SPT(0.555, 0.785), SPT(0.585, 0.785), SPT(0.589, 0.791), SPT(0.566, 0.795), SPT(0.559, 0.791) },
             { SPT(0.468, 0.7832), SPT(0.532, 0.7832), SPT(0.548, 0.7902), SPT(0.50, 0.7972), SPT(0.452, 0.7902) },
             { SPT(0.480000, 0.643120), SPT(0.520000, 0.643120), SPT(0.524000, 0.649520), SPT(0.500000, 0.652720), SPT(0.476000, 0.649520) },
             { SPT(0.475, 0.782), SPT(0.525, 0.782), SPT(0.53, 0.790), SPT(0.50, 0.794), SPT(0.47, 0.790) },
             { SPT(0.475, 0.782), SPT(0.525, 0.782), SPT(0.53, 0.790), SPT(0.50, 0.794), SPT(0.47, 0.790) }
        } },
        { "Chin", {
             { SPT(0.42, 0.815), SPT(0.58, 0.815), SPT(0.545, 0.845), SPT(0.52, 0.852), SPT(0.5, 0.855), SPT(0.48, 0.852), SPT(0.455, 0.845) },
             { SPT(0.470, 0.815), SPT(0.582, 0.815), SPT(0.538, 0.845), SPT(0.518, 0.852), SPT(0.503, 0.855), SPT(0.488, 0.852), SPT(0.472, 0.845) },
             { SPT(0.553, 0.815), SPT(0.593, 0.815), SPT(0.585, 0.845), SPT(0.578, 0.852), SPT(0.573, 0.855), SPT(0.568, 0.852), SPT(0.561, 0.845) },
             { SPT(0.436, 0.819971), SPT(0.564, 0.819971), SPT(0.536, 0.843971), SPT(0.516, 0.849571), SPT(0.5, 0.851971), SPT(0.484, 0.849571), SPT(0.464, 0.843971) },
             { SPT(0.436, 0.679971), SPT(0.564, 0.679971), SPT(0.536, 0.703971), SPT(0.516, 0.709571), SPT(0.5, 0.711971), SPT(0.484, 0.709571), SPT(0.464, 0.703971) },
             { SPT(0.42, 0.815), SPT(0.58, 0.815), SPT(0.545, 0.845), SPT(0.52, 0.852), SPT(0.5, 0.855), SPT(0.48, 0.852), SPT(0.455, 0.845) },
             { SPT(0.42, 0.815), SPT(0.58, 0.815), SPT(0.545, 0.845), SPT(0.52, 0.852), SPT(0.5, 0.855), SPT(0.48, 0.852), SPT(0.455, 0.845) }
        } },
        { "Neck", {
             { SPT(0.365, 0.86), SPT(0.635, 0.86), SPT(0.62, 0.98), SPT(0.38, 0.98) },
             { SPT(0.391, 0.86), SPT(0.617, 0.86), SPT(0.594, 0.98), SPT(0.407, 0.98) },
             { SPT(0.55, 0.86), SPT(0.575, 0.86), SPT(0.585, 0.98), SPT(0.545, 0.98) },
             { SPT(0.392, 0.872), SPT(0.608, 0.872), SPT(0.596, 0.968), SPT(0.404, 0.968) },
             { SPT(0.392, 0.732), SPT(0.608, 0.732), SPT(0.596, 0.828), SPT(0.404, 0.828) },
             { SPT(0.365, 0.86), SPT(0.635, 0.86), SPT(0.62, 0.98), SPT(0.38, 0.98) },
             { SPT(0.365, 0.86), SPT(0.635, 0.86), SPT(0.62, 0.98), SPT(0.38, 0.98) }
        } },
        { nullptr, {} }   // table sentinel
    };
    return Table;
}

// ============================================================================
// Phase 2: the authored FEATURE pose matrix (guide Parts IV/V). The 13
// anatomical feature cards (eye/brow/cheek/ear pairs + nose/mouth/teeth/
// chin/neck) get per-zone authored rings derived from their FRONT GLYPH by
// pure, mirror-consistent geometric transforms (scaling about the part's own
// centroid + a pure-Y shift; paired parts never translate in X, so a card's
// rings stay the exact horizontal mirror of its partner's because the front
// glyphs already mirror and every transform commutes with the mirror):
//   P0            the front glyph itself (the morph identity).
//   P45           3/4 compression — Part IV Zones 1-3: the NEAR member keeps
//                 the mild Eye_Near_3Q compression (~0.85, the compressed grid
//                 wraps the near side ~20%) while the FAR member swaps to the
//                 narrower Eye_Far_Narrow / Brow_Far_3Q / compressed far
//                 projection (canonical far card = the L-side, at +yaw); the
//                 mouth compresses into an off-center curve (Mouth_3Q), the
//                 nose darts toward the turn side. The left-half states
//                 resolve the PARTNER's P45 ring mirrored, so the −45 VIEW is
//                 still the exact horizontal mirror of +45 (near on the left).
//   P90           the profile sliver — Part IV Zone 4 (Eye_Profile single
//                 lash line, brow/cheek slivers, tall ear). The centerline
//                 features drop INTO the profile contour (hidden), so their
//                 P90 rings are valid placeholders only.
//   P135          flat back-fuzz for the ears (Part IV Zone 5 — the only
//                 feature card still rendering walk-behind); placeholder for
//                 the rest (hidden walk-behind).
//   P180          the folded card dropped by > 10% of the canvas so every
//                 back pose clears the Phase 6 back-change gate (never
//                 rendered — features hide walk-behind).
//   PTop/PBottom  the front glyph: Bottom keeps the feature read (Part
//                 V.3/V.4), Top never renders (Part V.2 drops Primary
//                 Features at the Top swap).
// ============================================================================

inline std::vector<FPSchematicPoint> FPSchematicScaleRingAboutCentroid(
    const std::vector<FPSchematicPoint>& R, double Sx, double Sy)
{
    if (R.empty()) return {};
    double Cx = 0.0, Cy = 0.0;
    for (const FPSchematicPoint& p : R) { Cx += p.X; Cy += p.Y; }
    Cx /= (double)R.size(); Cy /= (double)R.size();
    std::vector<FPSchematicPoint> O; O.reserve(R.size());
    for (const FPSchematicPoint& p : R)
        O.push_back({ Cx + (p.X - Cx) * Sx, Cy + (p.Y - Cy) * Sy });
    return O;
}

inline std::vector<FPSchematicPoint> FPSchematicShiftRing(
    const std::vector<FPSchematicPoint>& R, double Dx, double Dy)
{
    std::vector<FPSchematicPoint> O; O.reserve(R.size());
    for (const FPSchematicPoint& p : R)
        O.push_back({ p.X + Dx, p.Y + Dy });
    return O;
}

// E11 — canthus-preserving foreshortening: anisotropic scale about the ring
// centroid in a frame ALIGNED WITH THE RING'S OWN CANTHUS CHORD (the outer
// corner -> inner corner line, ring indices 0 -> 9 for both eyes; mirror
// states keep the same indices because the partner ring is mirrored in
// place). An axis-aligned Y squash rotates the chord — the 0.30 sliver
// steepens the 15° AP-E1 tareme canthus to ~40° and the eye stops reading as
// tareme. Scaling along the chord instead preserves the canthus angle
// EXACTLY while the card foreshortens (validator_silhouette.py gate 3). The
// chord is never perpendicular, so the frame is always well-formed.
inline std::vector<FPSchematicPoint> FPSchematicScaleRingAboutCanthus(
    const std::vector<FPSchematicPoint>& R, double Sx, double Sy)
{
    if (R.size() < 10) return FPSchematicScaleRingAboutCentroid(R, Sx, Sy);
    const double Dx = R[9].X - R[0].X;
    const double Dy = R[9].Y - R[0].Y;
    const double L = std::sqrt(Dx * Dx + Dy * Dy);
    if (L <= 0.0) return FPSchematicScaleRingAboutCentroid(R, Sx, Sy);
    const double C = Dx / L, S = Dy / L;
    double Cx = 0.0, Cy = 0.0;
    for (const FPSchematicPoint& p : R) { Cx += p.X; Cy += p.Y; }
    Cx /= (double)R.size(); Cy /= (double)R.size();
    std::vector<FPSchematicPoint> O; O.reserve(R.size());
    for (const FPSchematicPoint& p : R)
    {
        const double A = (p.X - Cx) * C + (p.Y - Cy) * S;    // along the chord
        const double B = -(p.X - Cx) * S + (p.Y - Cy) * C;   // across the chord
        O.push_back({ Cx + (Sx * A) * C - (Sy * B) * S,
                      Cy + (Sx * A) * S + (Sy * B) * C });
    }
    return O;
}

// The canonical 13 feature cards (sentinel-terminated).
inline const char* const* FPSchematicFeatureCardNames()
{
    static const char* const Names[] = {
        "EyeL", "EyeR", "BrowL", "BrowR", "CheekL", "CheekR", "EarL", "EarR",
        "Nose", "Mouth", "Teeth", "Chin", "Neck", nullptr
    };
    return Names;
}

// The per-part ring transform family for a feature card. All paired-part
// transforms keep Dx == 0 (centroid scaling + pure-Y shift) so the L/R pair
// contract holds exactly; unpaired centerline parts may translate in X (the
// state path mirrors their rings for the negative-yaw half).
inline std::vector<FPSchematicPoint> FPSchematicFeatureRingAt(const char* Name,
    const std::vector<FPSchematicPoint>& P0, int Slot)
{
    const std::string N(Name ? Name : "");
    switch (Slot)
    {
    case 1: // P45
        // Part IV Zones 1-3: the NEAR member keeps the mild compressed-grid
        // compression (Eye_Near_3Q 0.85 / near brow ~20%) while the FAR member
        // swaps to the narrower Eye_Far_Narrow / Brow_Far_3Q and the far
        // projection compresses (EarL is the canonical far card at +yaw).
        if (N == "EyeR") return FPSchematicScaleRingAboutCentroid(P0, 0.85, 0.95);  // Eye_Near_3Q
        if (N == "EyeL") return FPSchematicScaleRingAboutCentroid(P0, 0.70, 0.95);  // Eye_Far_Narrow
        if (N == "BrowR") return FPSchematicScaleRingAboutCentroid(P0, 0.80, 1.0);  // near browline
        if (N == "BrowL") return FPSchematicScaleRingAboutCentroid(P0, 0.70, 1.0);  // Brow_Far_3Q
        if (N == "CheekL" || N == "CheekR") return FPSchematicScaleRingAboutCentroid(P0, 0.90, 1.0);
        if (N == "EarR") return FPSchematicScaleRingAboutCentroid(P0, 0.90, 0.95);  // near projection
        if (N == "EarL") return FPSchematicScaleRingAboutCentroid(P0, 0.80, 0.95);  // far projection (compressed)
        if (N == "Nose") return FPSchematicShiftRing(FPSchematicScaleRingAboutCentroid(P0, 0.60, 0.90), 0.03, 0.0);
        if (N == "Mouth" || N == "Teeth") return FPSchematicShiftRing(FPSchematicScaleRingAboutCentroid(P0, 0.80, 0.85), 0.015, 0.0);
        if (N == "Chin") return FPSchematicScaleRingAboutCentroid(P0, 0.70, 0.95);
        if (N == "Neck") return FPSchematicScaleRingAboutCentroid(P0, 0.85, 1.0);
        return P0;
    case 2: // P90
        if (N == "EyeL" || N == "EyeR") return FPSchematicScaleRingAboutCentroid(P0, 0.10, 1.0);
        if (N == "BrowL" || N == "BrowR") return FPSchematicScaleRingAboutCentroid(P0, 0.15, 1.0);
        if (N == "CheekL" || N == "CheekR") return FPSchematicScaleRingAboutCentroid(P0, 0.15, 1.0);
        if (N == "EarL" || N == "EarR") return FPSchematicScaleRingAboutCentroid(P0, 0.80, 1.20);
        if (N == "Nose") return FPSchematicShiftRing(FPSchematicScaleRingAboutCentroid(P0, 0.30, 0.60), 0.05, 0.0);
        if (N == "Mouth" || N == "Teeth") return FPSchematicShiftRing(FPSchematicScaleRingAboutCentroid(P0, 0.20, 0.50), 0.06, 0.0);
        if (N == "Chin") return FPSchematicShiftRing(FPSchematicScaleRingAboutCentroid(P0, 0.35, 1.0), 0.05, 0.0);
        if (N == "Neck") return FPSchematicScaleRingAboutCentroid(P0, 0.50, 1.0);
        return P0;
    case 3: // P135
        if (N == "EarL" || N == "EarR") return FPSchematicScaleRingAboutCentroid(P0, 1.30, 0.50);
        return P0;   // never rendered (walk-behind hides every other feature)
    case 4: // P180
        return FPSchematicShiftRing(
            FPSchematicScaleRingAboutCentroid(P0, 0.80, 0.80), 0.0, -0.14);
    case 5: case 6: // PTop / PBottom
        return P0;   // Bottom keeps the front read; Top never renders it
    default:
        return P0;
    }
}

inline std::vector<FPSchematicPoseSet> FPSchematicBuildFeaturePoseSets()
{
    const std::vector<FPSchematicPart> Glyphs = DefaultPartSchematics();
    std::vector<FPSchematicPoseSet> Out;
    for (const char* const* N = FPSchematicFeatureCardNames(); *N; ++N)
    {
        const FPSchematicPart* G = FPSchematicFindPart(Glyphs, *N);
        FPSchematicPoseSet S;
        S.P0 = G ? G->Outline : std::vector<FPSchematicPoint>();
        S.P45 = FPSchematicFeatureRingAt(*N, S.P0, 1);
        S.P90 = FPSchematicFeatureRingAt(*N, S.P0, 2);
        S.P135 = FPSchematicFeatureRingAt(*N, S.P0, 3);
        S.P180 = FPSchematicFeatureRingAt(*N, S.P0, 4);
        S.PTop = FPSchematicFeatureRingAt(*N, S.P0, 5);
        S.PBottom = FPSchematicFeatureRingAt(*N, S.P0, 6);
        Out.push_back(S);
    }
    return Out;
}

// Y22/Y67 sub-threshold feature variants (WI1, art_guide Part IV Eye_Narrow /
// Eye_Sliver cards). PURE TRANSFORMS of the already-resolved state ring — no
// new authored rings (the 7-ring pose table stays). Applied by
// FPSchematicOutlineForState at the sub-threshold states:
//   - states 1/11 (the Narrow keys, 22.5°): the FAR eye narrows to the mild
//     Eye_Far_Narrow (0.85 width); the near eye keeps the parent pose.
//   - states 3/9 (the Sliver keys, 67.5°): the FAR eye compresses toward the
//     profile sliver (0.30 width) while the NEAR eye takes the Eye_Near_3Q
//     (0.88 width).
// Role-keyed via FPSchematicIsFarSide at the state's center yaw, so the
// left-half mirror states apply the same transform to the partner ring.
// E11: the scale is the canthus-aligned FPSchematicScaleRingAboutCanthus —
// the foreshortened cards keep the tareme canthus read instead of rotating
// the chord (the axis-aligned Y squash steepened the sliver to ~40°).
// Every other part returns the ring unchanged.
inline std::vector<FPSchematicPoint> FPSchematicFeatureVariantAt(
    const char* Name, const std::vector<FPSchematicPoint>& Ring, int StateIdx)
{
    if (!Name || !Name[0]) return Ring;
    const std::string N(Name);
    if (N != "EyeL" && N != "EyeR") return Ring;
    const double Cy = FPSchematicStateCenterYaw(StateIdx);
    const bool bFar = FPSchematicIsFarSide(Name, Cy);
    switch (StateIdx)
    {
    case 1: case 11:   // Y22: far eye narrows to the Eye_Far_Narrow card
        return bFar ? FPSchematicScaleRingAboutCanthus(Ring, 0.85, 0.95) : Ring;
    case 3: case 9:    // Y67: far eye sliver, near eye 3Q
        return bFar
            ? FPSchematicScaleRingAboutCanthus(Ring, 0.30, 0.95)
            : FPSchematicScaleRingAboutCanthus(Ring, 0.88, 0.95);
    default:
        return Ring;
    }
}

// The complete canonical authored set: the 4 authored silhouettes + the 13
// feature cards, in a stable order (the validator and the tests iterate the
// same 17 names).
inline const char* const* FPSchematicAllAuthoredNames()
{
    static const char* const Names[] = {
        "Head", "Bangs", "Hair", "BackHair",
        "EyeL", "EyeR", "BrowL", "BrowR", "CheekL", "CheekR",
        "EarL", "EarR", "Nose", "Mouth", "Teeth", "Chin", "Neck", nullptr
    };
    return Names;
}

// nullptr for any part outside the canonical 17. All 17 canonical parts now
// resolve from the authored pose table (explicit created art); the legacy
// formula fallback below stays as a safety net for any part that is ever
// added to FPSchematicFeatureCardNames() without an authored table entry.
inline const FPSchematicPoseSet* FPSchematicAuthoredPoses(const char* Name)
{
    if (!Name || !Name[0]) return nullptr;
    for (const FPSchematicPoseEntry* E = FPSchematicAuthoredPoseTable(); E->Name; ++E)
        if (std::string(E->Name) == Name) return &E->Pose;
    static const std::vector<FPSchematicPoseSet> Features = FPSchematicBuildFeaturePoseSets();
    static const std::vector<std::string> FeatureNames = []() {
        std::vector<std::string> V;
        for (const char* const* N = FPSchematicFeatureCardNames(); *N; ++N) V.push_back(*N);
        return V;
    }();
    for (size_t i = 0; i < FeatureNames.size(); ++i)
        if (FeatureNames[i] == Name) return &Features[i];
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
    for (const char* const* N = FPSchematicAllAuthoredNames(); *N; ++N)
    {
        const FPSchematicPoseSet* P = FPSchematicAuthoredPoses(*N);
        if (!P) continue;
        const FPSchematicPoseValidation V = FPSchematicValidatePoseSet(*P);
        ++S.TotalPoseSets;
        S.TotalRings += 7;
        S.InvalidRings += V.InvalidRingCount;
        if (V.bAllRingsValid) ++S.ValidPoseSets;
        S.TotalBackPoints += V.RingPointCount;
        S.TotalBackMoved += V.BackMovedPoints;
    }
    return S;
}

// ============================================================================
// Phase A.7: VECTOR Silhouette Read Test (art_guide I.7 checkable form /
// art_tech_guide XII.6 lines 945-949). The visible silhouette is flattened to
// a single-color VECTOR MASK and run through connected-component analysis —
// pure exact vector geometry, NO rasterization anywhere: the mask rings'
// edges are split at every exact proper crossing, the planar arrangement is
// walked into faces (half-edge walk; every angle test uses the ORIGINAL
// integer edge directions, so no rational angle comparisons), each face gets
// an even-odd parity per ring via a dual-graph traversal from the unbounded
// face, the fill = OR of the ring parities, and the filled components + holes
// come from a union-find over faces sharing arrangement edges (the closure of
// the union: filled faces merge across shared ring edges). Gates: exactly ONE
// filled component and no internal hole larger than the noise floor (0.5% of
// the mask's filled area). The mask per state = Head + Bangs + Hair + BackHair
// + the AnchorCritical Ears filtered by FPSchematicLayerVisibleInState (the
// ears are read carriers that persist even walk-behind, XII.4; the far-side
// ear hides at the profiles). Sub-threshold states reuse the parent authored
// ring, so their masks are identical by construction.
// ============================================================================

// ---- exact 64-bit rational + portable 128-bit product comparison (no
// __int128: the test harness compiles on both g++ and MSVC). Canonical
// coordinates are 4-decimal, so scale 10000 makes every ring vertex an exact
// grid integer; proper crossings are rational points with numerators <= ~4e12
// and denominators <= ~2e8, so every predicate below is exact. ----
struct FPSchematicRat
{
    long long Num = 0;   // reduced numerator (signed)
    long long Den = 1;   // > 0
};

inline long long FPSchematicGCD64(long long a, long long b)
{
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0)
    {
        const long long t = a % b;
        a = b;
        b = t;
    }
    return a < 0 ? -a : a;   // a can only be >= 0 here; keep the guard for -0
}

inline FPSchematicRat FPSchematicRatMake(long long Num, long long Den)
{
    FPSchematicRat R;
    if (Den < 0)
    {
        Num = -Num;
        Den = -Den;
    }
    if (Num == 0)
    {
        R.Num = 0;
        R.Den = 1;
        return R;
    }
    const long long g = FPSchematicGCD64(Num, Den);
    R.Num = Num / g;
    R.Den = Den / g;
    return R;
}

struct FPSchematicU128
{
    unsigned long long Lo = 0, Hi = 0;
};

// Exact unsigned 64x64 -> 128 product via 32-bit decomposition (portable).
inline FPSchematicU128 FPSchematicMulU64(unsigned long long a, unsigned long long b)
{
    const unsigned long long a0 = a & 0xFFFFFFFFull, a1 = a >> 32;
    const unsigned long long b0 = b & 0xFFFFFFFFull, b1 = b >> 32;
    const unsigned long long p00 = a0 * b0;
    const unsigned long long p01 = a0 * b1;
    const unsigned long long p10 = a1 * b0;
    const unsigned long long p11 = a1 * b1;
    FPSchematicU128 R;
    R.Lo = p00;
    R.Hi = p11;
    const unsigned long long t1 = p01 << 32;
    const unsigned long long old1 = R.Lo;
    R.Lo += t1;
    if (R.Lo < old1) ++R.Hi;
    R.Hi += p01 >> 32;
    const unsigned long long t2 = p10 << 32;
    const unsigned long long old2 = R.Lo;
    R.Lo += t2;
    if (R.Lo < old2) ++R.Hi;
    R.Hi += p10 >> 32;
    return R;
}

// Sign of a*b - c*d, exact for 64-bit inputs (products up to 128 bits).
inline int FPSchematicProductCmp(long long a, long long b, long long c, long long d)
{
    const bool sa = (a < 0) ^ (b < 0);
    const bool sc = (c < 0) ^ (d < 0);
    if (sa != sc) return sa ? -1 : 1;
    const unsigned long long ma = a < 0 ? (unsigned long long)(-(a + 1)) + 1ull : (unsigned long long)a;
    const unsigned long long mb = b < 0 ? (unsigned long long)(-(b + 1)) + 1ull : (unsigned long long)b;
    const unsigned long long mc = c < 0 ? (unsigned long long)(-(c + 1)) + 1ull : (unsigned long long)c;
    const unsigned long long md = d < 0 ? (unsigned long long)(-(d + 1)) + 1ull : (unsigned long long)d;
    const FPSchematicU128 A = FPSchematicMulU64(ma, mb);
    const FPSchematicU128 C = FPSchematicMulU64(mc, md);
    int r;
    if (A.Hi > C.Hi) r = 1;
    else if (A.Hi < C.Hi) r = -1;
    else if (A.Lo > C.Lo) r = 1;
    else if (A.Lo < C.Lo) r = -1;
    else r = 0;
    return sa ? -r : r;
}

// Exact comparison of two rationals (a.Num/a.Den vs b.Num/b.Den).
inline int FPSchematicRatCmp(const FPSchematicRat& a, const FPSchematicRat& b)
{
    if (a.Num < 0 && b.Num >= 0) return -1;
    if (a.Num >= 0 && b.Num < 0) return 1;
    if (a.Num == 0 && b.Num == 0) return 0;
    return FPSchematicProductCmp(a.Num, b.Den, b.Num, a.Den);
}

inline double FPSchematicRatToDouble(const FPSchematicRat& r)
{
    return (double)r.Num / (double)r.Den;
}

struct FPSchematicSilhouetteReadResult
{
    bool bMaskValid = false;       // >= 1 ring, every ring >= 3 distinct points
    int FilledComponents = 0;
    int HoleCount = 0;
    double TotalFilledArea = 0.0;  // fraction of the unit canvas
    double MaxHoleArea = 0.0;      // largest hole, fraction of the unit canvas
    static constexpr double NoiseFloorFraction = 0.005;   // 0.5% of filled area
    bool bPasses() const
    {
        if (!bMaskValid) return false;
        if (FilledComponents != 1) return false;
        return MaxHoleArea <= NoiseFloorFraction * TotalFilledArea;
    }
};

// The exact vector mask analyzer (I.7 / XII.6). MaskRings = closed cyclic
// rings in canonical order; every vertex coordinate must be exact at 4
// decimals (the authored table is; synthetic test rings must use <= 4
// decimals too — the grid scale is 10000).
inline FPSchematicSilhouetteReadResult FPSchematicVectorMaskAnalyze(
    const std::vector<std::vector<FPSchematicPoint>>& MaskRings)
{
    FPSchematicSilhouetteReadResult R;
    const long long SC = 10000;
    const double InvGridArea = 1.0 / ((double)SC * (double)SC);   // -> canvas fraction

    struct Seg
    {
        int Ring;
        long long Ax, Ay, Bx, By;
    };
    std::vector<Seg> Segs;
    for (size_t ri = 0; ri < MaskRings.size(); ++ri)
    {
        const std::vector<FPSchematicPoint>& RK = MaskRings[ri];
        if (RK.size() < 3) return R;   // bMaskValid stays false
        {
            // doc contract: every ring must have >= 3 DISTINCT points at the
            // grid scale (a 3-point ring with a duplicate vertex has only two
            // real corners and cannot bound a mask)
            struct GKey { long long X, Y; };
            std::vector<GKey> Seen;
            Seen.reserve(RK.size());
            for (const FPSchematicPoint& P : RK)
            {
                const long long gx = (long long)(P.X * (double)SC + 0.5);
                const long long gy = (long long)(P.Y * (double)SC + 0.5);
                bool dup = false;
                for (const GKey& S : Seen)
                    if (S.X == gx && S.Y == gy) { dup = true; break; }
                if (!dup) Seen.push_back({ gx, gy });
            }
            if (Seen.size() < 3) return R;
        }
        R.bMaskValid = true;   // at least one ring passed the per-ring gates
        for (size_t k = 0; k < RK.size(); ++k)
        {
            const FPSchematicPoint& P = RK[k];
            const FPSchematicPoint& Q = RK[(k + 1) % RK.size()];
            const long long ax = (long long)(P.X * (double)SC + 0.5);
            const long long ay = (long long)(P.Y * (double)SC + 0.5);
            const long long bx = (long long)(Q.X * (double)SC + 0.5);
            const long long by = (long long)(Q.Y * (double)SC + 0.5);
            if (ax == bx && ay == by) continue;
            Segs.push_back({ (int)ri, ax, ay, bx, by });
        }
    }
    if (Segs.empty()) return R;

    // ---- proper crossings: split every segment at every exact interior
    // crossing of its endpoints (strictly interior to BOTH segments). ----
    struct SplitPt
    {
        FPSchematicRat X, Y;
        double T = 0.0;   // parameter along the segment (sort key only)
    };
    std::vector<std::vector<SplitPt>> Splits(Segs.size());
    for (size_t i = 0; i < Segs.size(); ++i)
    {
        for (size_t j = i + 1; j < Segs.size(); ++j)
        {
            const Seg& A = Segs[i];
            const Seg& B = Segs[j];
            const long long D1x = A.Bx - A.Ax, D1y = A.By - A.Ay;
            const long long D2x = B.Bx - B.Ax, D2y = B.By - B.Ay;
            const long long Den = D1x * D2y - D1y * D2x;
            if (Den == 0) continue;   // parallel (collinear overlap is absent
                                      // from the canonical data and the controls)
            const long long QPx = B.Ax - A.Ax, QPy = B.Ay - A.Ay;
            const long long Tn = QPx * D2y - QPy * D2x;   // A-parameter numerator
            const long long Sn = QPx * D1y - QPy * D1x;   // B-parameter numerator
            const bool tIn = Den > 0 ? (Tn > 0 && Tn < Den) : (Tn < 0 && Tn > Den);
            const bool sIn = Den > 0 ? (Sn > 0 && Sn < Den) : (Sn < 0 && Sn > Den);
            if (!tIn || !sIn) continue;
            // crossing point = A + (Tn/Den)*D1  (exact, int64-safe)
            const FPSchematicRat PX = FPSchematicRatMake(A.Ax * Den + Tn * D1x, Den);
            const FPSchematicRat PY = FPSchematicRatMake(A.Ay * Den + Tn * D1y, Den);
            Splits[i].push_back({ PX, PY, (double)Tn / (double)Den });
            Splits[j].push_back({ PX, PY, (double)Sn / (double)Den });
        }
    }

    // ---- sub-segments (endpoints = grid vertices or exact crossings) ----
    struct VKey
    {
        FPSchematicRat X, Y;
    };
    struct RawSub
    {
        int Ring;
        VKey A, B;
        long long DirX, DirY;
    };
    std::vector<RawSub> Subs;
    for (size_t i = 0; i < Segs.size(); ++i)
    {
        const Seg& S = Segs[i];
        std::vector<SplitPt>& SP = Splits[i];
        std::sort(SP.begin(), SP.end(),
            [](const SplitPt& a, const SplitPt& b) { return a.T < b.T; });
        std::vector<VKey> Pts;
        Pts.reserve(SP.size() + 2);
        Pts.push_back({ FPSchematicRatMake(S.Ax, 1), FPSchematicRatMake(S.Ay, 1) });
        for (const SplitPt& P : SP) Pts.push_back({ P.X, P.Y });
        Pts.push_back({ FPSchematicRatMake(S.Bx, 1), FPSchematicRatMake(S.By, 1) });
        for (size_t k = 0; k + 1 < Pts.size(); ++k)
        {
            const VKey& A = Pts[k];
            const VKey& B = Pts[k + 1];
            if (FPSchematicRatCmp(A.X, B.X) == 0 && FPSchematicRatCmp(A.Y, B.Y) == 0)
                continue;
            Subs.push_back({ S.Ring, A, B, S.Bx - S.Ax, S.By - S.Ay });
        }
    }

    // ---- vertex dedupe by exact rational equality ----
    auto KeyLess = [](const VKey& a, const VKey& b) {
        const int cx = FPSchematicRatCmp(a.X, b.X);
        if (cx != 0) return cx < 0;
        return FPSchematicRatCmp(a.Y, b.Y) < 0;
    };
    auto KeyEqual = [](const VKey& a, const VKey& b) {
        return FPSchematicRatCmp(a.X, b.X) == 0 && FPSchematicRatCmp(a.Y, b.Y) == 0;
    };
    std::vector<VKey> Unique;
    for (const RawSub& S : Subs)
    {
        Unique.push_back(S.A);
        Unique.push_back(S.B);
    }
    std::sort(Unique.begin(), Unique.end(), KeyLess);
    Unique.erase(std::unique(Unique.begin(), Unique.end(), KeyEqual), Unique.end());
    auto FindId = [&](const VKey& K) -> int {
        const auto it = std::lower_bound(Unique.begin(), Unique.end(), K, KeyLess);
        if (it == Unique.end() || !KeyEqual(*it, K)) return -1;
        return (int)(it - Unique.begin());
    };
    struct SubV
    {
        int Ring;
        int AId, BId;
        long long DirX, DirY;
    };
    std::vector<SubV> SubV;
    SubV.reserve(Subs.size());
    for (const RawSub& S : Subs)
    {
        const int a = FindId(S.A);
        const int b = FindId(S.B);
        if (a < 0 || b < 0) { R.bMaskValid = false; return R; }
        SubV.push_back({ S.Ring, a, b, S.DirX, S.DirY });
    }
    if (SubV.empty()) return R;

    // ---- half-edge structure ----
    const int NV = (int)Unique.size();
    const int NH = (int)SubV.size() * 2;
    struct HalfEdge
    {
        int Next = -1;
        int Twin = -1;
        int Face = -1;
        int VFrom = -1, VTo = -1;
        int Ring = -1;
        long long DirX = 0, DirY = 0;
        bool bVisited = false;
    };
    std::vector<HalfEdge> HE(NH);
    for (size_t k = 0; k < SubV.size(); ++k)
    {
        const int h0 = (int)k * 2, h1 = h0 + 1;
        HE[h0] = { -1, h1, -1, SubV[k].AId, SubV[k].BId, SubV[k].Ring,
                   SubV[k].DirX, SubV[k].DirY, false };
        HE[h1] = { -1, h0, -1, SubV[k].BId, SubV[k].AId, SubV[k].Ring,
                   -SubV[k].DirX, -SubV[k].DirY, false };
    }

    // ---- next-edge: face on the left; at v pick the first outgoing edge
    // clockwise from the reversed incoming direction (all int64). ----
    auto AngleHalf = [](long long dx, long long dy) -> int {
        return (dy < 0 || (dy == 0 && dx < 0)) ? 1 : 0;
    };
    auto DirLess = [&](long long ax, long long ay, long long bx, long long by) -> bool {
        const int ha = AngleHalf(ax, ay);
        const int hb = AngleHalf(bx, by);
        if (ha != hb) return ha < hb;
        const long long c = ax * by - ay * bx;
        if (c != 0) return c > 0;
        return false;
    };
    {
        std::vector<std::vector<int>> OutAt((size_t)NV);
        for (int h = 0; h < NH; ++h) OutAt[(size_t)HE[h].VFrom].push_back(h);
        for (int v = 0; v < NV; ++v)
        {
            std::vector<int>& L = OutAt[(size_t)v];
            std::sort(L.begin(), L.end(), [&](int a, int b) {
                return DirLess(HE[a].DirX, HE[a].DirY, HE[b].DirX, HE[b].DirY);
            });
        }
        for (int h = 0; h < NH; ++h)
        {
            // next(h) continues the face on h's LEFT at the HEAD vertex:
            // first outgoing edge clockwise from the reversed incoming
            // direction (exact angle ordering; the twin is the exact reverse
            // and is skipped). All angle tests are int64.
            const int w = HE[h].VTo;
            const std::vector<int>& L = OutAt[(size_t)w];
            const long long rx = -HE[h].DirX;
            const long long ry = -HE[h].DirY;
            int best = -1;
            for (int o : L)
            {
                if (HE[o].DirX == rx && HE[o].DirY == ry) continue;
                if (DirLess(HE[o].DirX, HE[o].DirY, rx, ry)) best = o;
                else break;   // sorted: everything after has angle >= r
            }
            if (best < 0) best = L.back();
            HE[h].Next = best;
        }
    }

    // ---- face walk (shoelace sign = orientation; + = bounded CCW) ----
    std::vector<double> FaceArea;
    std::vector<int> FaceSign;
    const int WalkGuard = NH + 4;
    for (int h0 = 0; h0 < NH; ++h0)
    {
        if (HE[h0].bVisited) continue;
        int h = h0;
        double s = 0.0;
        int guard = 0;
        while (!HE[h].bVisited && guard < WalkGuard)
        {
            HE[h].bVisited = true;
            HE[h].Face = (int)FaceArea.size();
            const VKey& P1 = Unique[(size_t)HE[h].VFrom];
            const VKey& P2 = Unique[(size_t)HE[h].VTo];
            s += FPSchematicRatToDouble(P1.X) * FPSchematicRatToDouble(P2.Y)
               - FPSchematicRatToDouble(P1.Y) * FPSchematicRatToDouble(P2.X);
            h = HE[h].Next;
            ++guard;
        }
        if (h != h0 || guard >= WalkGuard) { R.bMaskValid = false; return R; }
        const double area = s * 0.5;
        FaceArea.push_back(area);
        FaceSign.push_back(area >= 0.0 ? 1 : -1);
    }
    const int Faces = (int)FaceArea.size();

    // ---- dual graph: per-ring even-odd parity from the unbounded face ----
    struct FaceEdgeRef
    {
        int F0, F1, Ring;
    };
    std::vector<FaceEdgeRef> Dual;
    for (int h = 0; h + 1 < NH; h += 2)
    {
        const int f0 = HE[h].Face;
        const int f1 = HE[h + 1].Face;
        if (f0 == f1) continue;
        Dual.push_back({ f0, f1, HE[h].Ring });
    }
    std::vector<std::vector<int>> EdgeAtFace((size_t)Faces);
    for (size_t e = 0; e < Dual.size(); ++e)
    {
        EdgeAtFace[(size_t)Dual[e].F0].push_back((int)e);
        EdgeAtFace[(size_t)Dual[e].F1].push_back((int)e);
    }
    const int RCount = (int)MaskRings.size();
    std::vector<std::vector<char>> Parity((size_t)Faces,
        std::vector<char>((size_t)RCount, 0));
    std::vector<char> SeenFace((size_t)Faces, 0);
    std::vector<int> Stack;
    // EVERY negative-sign face is part of the unbounded face: its boundary
    // walk may split into several cycles, one per disjoint ring blob (two
    // separated squares walk the outside twice). Seed them all with zero
    // parity so every ring interior is reached through its own outer cycle.
    for (int f = 0; f < Faces; ++f)
    {
        if (FaceSign[f] < 0)
        {
            Stack.push_back(f);
            SeenFace[(size_t)f] = 1;
        }
    }
    while (!Stack.empty())
    {
        const int f = Stack.back();
        Stack.pop_back();
        for (int e : EdgeAtFace[(size_t)f])
        {
            const FaceEdgeRef& E = Dual[(size_t)e];
            const int g = (E.F0 == f) ? E.F1 : E.F0;
            if (SeenFace[(size_t)g]) continue;
            SeenFace[(size_t)g] = 1;
            Parity[(size_t)g] = Parity[(size_t)f];
            Parity[(size_t)g][(size_t)E.Ring] ^= 1;
            Stack.push_back(g);
        }
    }

    // ---- fill = OR of the odd ring parities; DSU over filled faces ----
    std::vector<char> Filled((size_t)Faces, 0);
    for (int f = 0; f < Faces; ++f)
        for (int r = 0; r < RCount; ++r)
            if (Parity[(size_t)f][(size_t)r] & 1) { Filled[(size_t)f] = 1; break; }
    std::vector<int> Parent((size_t)Faces);
    for (int f = 0; f < Faces; ++f) Parent[(size_t)f] = f;
    std::function<int(int)> Find = [&](int x) {
        while (Parent[(size_t)x] != x)
        {
            Parent[(size_t)x] = Parent[(size_t)Parent[(size_t)x]];
            x = Parent[(size_t)x];
        }
        return x;
    };
    for (const FaceEdgeRef& E : Dual)
        if (Filled[(size_t)E.F0] && Filled[(size_t)E.F1])
        {
            const int a = Find(E.F0);
            const int b = Find(E.F1);
            if (a != b) Parent[(size_t)a] = b;
        }

    std::vector<char> IsFilledRoot((size_t)Faces, 0);
    std::vector<char> IsHoleRoot((size_t)Faces, 0);
    std::vector<double> RootArea((size_t)Faces, 0.0);
    for (int f = 0; f < Faces; ++f)
    {
        const int r = Find(f);
        if (Filled[(size_t)f])
        {
            IsFilledRoot[(size_t)r] = 1;
            R.TotalFilledArea += fabs(FaceArea[(size_t)f]) * InvGridArea;
        }
        else if (FaceSign[(size_t)f] >= 0)
        {
            // bounded face: a genuine hole root (the unbounded face — all
            // negative-sign cycles — can never be a hole)
            IsHoleRoot[(size_t)r] = 1;
            RootArea[(size_t)r] += fabs(FaceArea[(size_t)f]) * InvGridArea;
        }
    }
    for (int f = 0; f < Faces; ++f)
    {
        if (IsFilledRoot[(size_t)f]) ++R.FilledComponents;
        if (IsHoleRoot[(size_t)f])
        {
            ++R.HoleCount;
            if (RootArea[(size_t)f] > R.MaxHoleArea) R.MaxHoleArea = RootArea[(size_t)f];
        }
    }
    return R;
}

// Clamped smoothstep in [0,1] (the turn-ease used for the morph weights).
inline double FPSmoothstep01(double T)
{
    T = T < 0.0 ? 0.0 : (T > 1.0 ? 1.0 : T);
    return T * T * (3.0 - 2.0 * T);
}

// Per-vertex smoothstep morph between the two authored yaw poses bracketing
// |yaw| (exact at the 0/22.5/45/67.5/90/135/180 state centers). The
// sub-threshold zones carry the PARENT pose (the Narrow zone holds P0, the
// Sliver zone holds P45 — the intermediate keys are parallax states of the
// parent pose, never new authored rings), so the morph is identity inside
// them and blends only across the 22.5->45, 67.5->90, 90->135, 135->180
// spans. Output keeps the front point count; a size mismatch in the table
// degrades to the smaller ring.
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
    else if (YawAbs <= 22.5)
    {
        A = &S.P0; B = &S.P0;                 // Narrow zone: parent pose
    }
    else if (YawAbs <= 45.0)
    {
        A = &S.P0;  B = &S.P45; T = FPSmoothstep01((YawAbs - 22.5) / 22.5);
    }
    else if (YawAbs <= 67.5)
    {
        A = &S.P45; B = &S.P45;               // Sliver zone: parent pose
    }
    else if (YawAbs <= 90.0)
    {
        A = &S.P45; B = &S.P90; T = FPSmoothstep01((YawAbs - 67.5) / 22.5);
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

// ============================================================================
// Phase 7: discrete per-view art swap (freeze the card, flip the view). Real
// 2D art is a rigid billboarded card — it NEVER deforms for the turn. The
// Phase 2-5 morph/slide/fold/squash formulas only exist to fake a turn for the
// placeholder glyphs; once per-view art exists the ONLY legitimate "turn" is a
// discrete swap between the nearest view states, crossfaded by weight. This
// block provides the state geometry shared by the placeholder SNAP and the
// runtime blend:
//   - FPSchematicStateAtAngles   the nearest state for any yaw/pitch (mirror
//                                of DetermineStateFromAngles with the DEFAULT
//                                zone geometry: HalfZoneWidth 22.5 x
//                                multipliers {1,3,5,7}, Top/Bottom +-60).
//   - FPSchematicBracketStates   the two bracketing states + the smoothstep
//                                weight between their centers (the blend
//                                target; the "previous" art slot = the
//                                partner, never the last state).
//   - FPSchematicStatePoseOut    the exact authored pose for a state (the
//                                left half is the mirror of the right).
//   - FPSchematicOutlineForState the per-state placeholder outline: authored
//                                silhouettes snap to the state pose; features
//                                return the FROZEN front glyph when the state
//                                shows them and EMPTY when hidden.
//   - FPSchematicLayerArtAlpha   the per-state art availability (fade target).
//   - FPSchematicSwapModeFor     the per-layer swap gate (crossfade vs swoosh).
// ============================================================================

// Default zone geometry (master blueprint Part III Zone 5 hard swaps + the
// WI1 sub-thresholds): the view SWAP thresholds sit at 22.5/45/67.5/90/135/180
// — the front pose holds [0,22.5), the narrow view [22.5,45), the 3-4 pose
// [45,67.5), the sliver view [67.5,90), the profile [90,135), the back-right
// [135,180), and the FULL back at exactly 180 (|yaw| >= 180 wraps to Back).
// HalfZoneWidth is the PRIMARY swap spacing (45 degrees); the sub-thresholds
// are derived from it as HalfZoneWidth/2 and 1.5*HalfZoneWidth, so
// FPSchematicParallaxRamp peaks exactly where the next view flips.
// (Mirrored by the runtime UFaceParallaxComponent defaults: HalfZoneWidth 45,
// TopViewPitchThreshold 45, BottomViewPitchThreshold -45,
// ZoneBoundaryMultipliers {1,2,3,4}.)
struct FPSchematicViewZone
{
    static constexpr double HalfZoneWidth = 45.0;
    static constexpr double TopPitchThreshold = 45.0;
    static constexpr double BottomPitchThreshold = -45.0;
    static inline double BoundaryAt(int M) { return M * HalfZoneWidth; }   // 45/90/135/180
    // WI1 sub-thresholds (the Narrow/Sliver zone boundaries), derived from
    // the primary first boundary — 22.5 and 67.5 at the default geometry.
    static inline double NarrowBoundary()  { return BoundaryAt(1) * 0.5; }
    static inline double SliverBoundary()  { return BoundaryAt(1) * 1.5; }
};

// The nearest view state for any (yaw, pitch) using the DEFAULT zone geometry.
// Mirrors UFaceParallaxComponent::DetermineStateFromAngles with the default
// settings so the placeholder, the widget preview and the runtime all resolve
// the same state. Returns 0..13 (0 Front, 1 NarrowRight, 2 3/4R, 3 SliverRight,
// 4 RightProfile, 5 BackRight, 6 Back, 7 BackLeft, 8 LeftProfile,
// 9 SliverLeft, 10 3/4L, 11 NarrowLeft, 12 Top, 13 Bottom).
// The SWAP thresholds are the blueprint's hard swaps at 22.5/45/67.5/90/135/180
// and the boundary angle belongs to the NEXT view (half-open): the front pose
// shows while |yaw| < 22.5, the narrow view [22.5,45), the 3-4 pose [45,67.5),
// the sliver view [67.5,90), the profile [90,135), back-right [135,180), and
// the FULL back at exactly 180 (the +180/-180 wrap point). So the authored
// pose is EXACT exactly at its own key angle (slide 0) and slides to the peak
// right where the NEXT view takes over.
inline int FPSchematicStateAtAngles(double Yaw, double Pitch)
{
    using Z = FPSchematicViewZone;
    if (Pitch >  Z::TopPitchThreshold)    return 12;
    if (Pitch <  Z::BottomPitchThreshold) return 13;
    const double B0 = Z::BoundaryAt(1);   // 45
    const double B1 = Z::BoundaryAt(2);   // 90
    const double B2 = Z::BoundaryAt(3);   // 135
    const double B3 = Z::BoundaryAt(4);   // 180
    const double H  = Z::NarrowBoundary();  // 22.5
    const double Q  = Z::SliverBoundary();  // 67.5
    if (Yaw >  180.0 || Yaw < -180.0) return 6;   // beyond the wrap -> Back
    if (Yaw >= -B3 && Yaw <=  B3)
    {
        if (Yaw >  -H && Yaw <   H) return 0;    // Front
        if (Yaw >=  H && Yaw <  B0) return 1;    // NarrowRight
        if (Yaw >=  B0 && Yaw <  Q) return 2;    // 3/4R
        if (Yaw >=  Q && Yaw <  B1) return 3;    // SliverRight
        if (Yaw >=  B1 && Yaw <  B2) return 4;   // RightProfile
        if (Yaw >=  B2 && Yaw <  B3) return 5;   // BackRight
        if (Yaw >=  B3 || Yaw <= -B3) return 6;  // Back (the wrap point)
        if (Yaw >  -B3 && Yaw <= -B2) return 7;  // BackLeft
        if (Yaw >  -B2 && Yaw <= -B1) return 8;  // LeftProfile
        if (Yaw >  -B1 && Yaw <= -Q)  return 9;  // SliverLeft
        if (Yaw >  -Q && Yaw <= -B0)  return 10; // 3/4L
        if (Yaw >  -B0 && Yaw <= -H)  return 11; // NarrowLeft
    }
    return 0;
}

// ============================================================================
// Phase 2 parity — the at-rest committed state (widget/runtime parity).
// FPSchematicStateAtAngles above is the BAND/pose-key resolver (the authored
// pose truth). The RUNTIME's displayed cell at a static pose is the state the
// Schmitt machine committed to under forward travel: the band state SHIFTED
// right by the Schmitt margin (the commit fires at band edge + 1.5, so inside
// [edge, edge + 1.5) the machine still holds the previous state, and the
// commit key coincides with the crossfade alpha = 0.5 key).
// ============================================================================

// The shared swap edge between two adjacent ring states — the exact mirror of
// UFaceParallaxComponent::GetBoundaryBetweenStates at the default geometry
// (FPSchematicViewZone constants): Front|Narrow 22.5, Narrow|3Q 45, 3Q|Sliver
// 67.5, Sliver|Profile 90, Profile|BackR 135, BackR|Back 180, Back|BackL
// -180, BackL|ProfileL -135, ProfileL|SliverL -90, SliverL|3QL -67.5,
// 3QL|NarrowL -45, NarrowL|Front -22.5. (Distinct from the WI2 sub-swap table
// FPSchematicYawBoundaryForPair, which describes the 22.5-spaced art-row
// sub-swaps at 11.25/33.75 etc.)
inline double FPSchematicZoneEdgeForPair(int A, int B)
{
    using Z = FPSchematicViewZone;
    const double Table[12] = { Z::NarrowBoundary(), Z::BoundaryAt(1),
                               Z::SliverBoundary(), Z::BoundaryAt(2),
                               Z::BoundaryAt(3),    Z::BoundaryAt(4),
                              -Z::BoundaryAt(4),   -Z::BoundaryAt(3),
                              -Z::BoundaryAt(2),   -Z::SliverBoundary(),
                              -Z::BoundaryAt(1),   -Z::NarrowBoundary() };
    const int Lo = A < B ? A : B;
    const int Hi = A < B ? B : A;
    if (Lo == 6 && Hi == 7) return -Z::BoundaryAt(4);   // Back|BackL wrap pair
    if (Lo == 0 && Hi == 11) return -Z::NarrowBoundary(); // NarrowL|Front wrap pair
    return Table[Lo];
}

// The at-rest committed state under FORWARD (increasing-yaw / rising-pitch)
// travel: the raw band state of FPSchematicStateAtAngles shifted so each band
// opens at its LEFT edge + the Schmitt margin (edge + 1.5, forward direction)
// and closes at its RIGHT edge + the same margin. A static pose inside a
// [edge, edge+1.5) hysteresis sliver is pinned to the state the camera would
// hold after arriving from the left; the runtime's own at-rest state inside
// the dead zone is history-dependent, and this is the canonical convention
// the widget/viewer mirrors (the reverse-travel machine would hold one state
// longer on the way back — hysteresis exists to make that in-between region
// unambiguous in motion, not at rest).
inline int FPSchematicForwardStateAt(double Yaw, double Pitch)
{
    using Z = FPSchematicViewZone;
    if (Pitch > Z::TopPitchThreshold)
        return (Pitch >= Z::TopPitchThreshold + FPSchematicCrossfadeSchmittDeg)
            ? 12 : FPSchematicForwardStateAt(Yaw, 0.0);
    if (Pitch < Z::BottomPitchThreshold)
        return (Pitch <= Z::BottomPitchThreshold - FPSchematicCrossfadeSchmittDeg)
            ? 13 : FPSchematicForwardStateAt(Yaw, 0.0);
    double y = Yaw;
    // The +-180 crossings must NOT renormalize into the opposite half: past
    // +180 the machine keeps measuring the signed sweep (Back commits at
    // 181.5, so [180, 181.5) still holds BackRight), and the |yaw| >= 180
    // wrap band reads Back like the pose-key resolver.
    if (y >= 181.5) return 6;                 // past the BackR|Back commit key
    if (y >= 180.0) return 5;                 // [180, 181.5): still BackRight
    if (y < -178.5) return 6;                 // wrap band (incl. exactly -180)
    const double T0 = -Z::NarrowBoundary() + FPSchematicCrossfadeSchmittDeg;   // -21
    const double T1 =  Z::NarrowBoundary() + FPSchematicCrossfadeSchmittDeg;   //  24
    const double T2 =  Z::BoundaryAt(1)     + FPSchematicCrossfadeSchmittDeg;  //  46.5
    const double T3 =  Z::SliverBoundary()  + FPSchematicCrossfadeSchmittDeg;  //  69
    const double T4 =  Z::BoundaryAt(2)     + FPSchematicCrossfadeSchmittDeg;  //  91.5
    const double T5 =  Z::BoundaryAt(3)     + FPSchematicCrossfadeSchmittDeg;  // 136.5
    const double T8 = -Z::BoundaryAt(3)     + FPSchematicCrossfadeSchmittDeg;  // -133.5
    const double T9 = -Z::BoundaryAt(2)     + FPSchematicCrossfadeSchmittDeg;  //  -88.5
    const double T10 = -Z::SliverBoundary() + FPSchematicCrossfadeSchmittDeg;  //  -66
    const double T11 = -Z::BoundaryAt(1)    + FPSchematicCrossfadeSchmittDeg;  //  -43.5
    if (y >= T5) return 5;                    // BackRight
    if (y >= T4) return 4;                    // RightProfile
    if (y >= T3) return 3;                    // SliverRight
    if (y >= T2) return 2;                    // 3/4R
    if (y >= T1) return 1;                    // NarrowRight
    if (y >= T0) return 0;                    // Front
    if (y >= T11) return 11;                  // NarrowLeft
    if (y >= T10) return 10;                  // 3/4L
    if (y >= T9)  return 9;                   // SliverLeft
    if (y >= T8)  return 8;                   // LeftProfile
    return 7;                                 // BackLeft
}


// The two bracketing states A/B around the current (yaw, pitch) and the
// smoothstep weight W between their centers (W == 0 exactly at A's center,
// W == 1 exactly at B's center, so W is the blend target toward B). For
// |pitch| >= the top/bottom threshold the bracket is (yaw-nearest, Top/Bottom);
// otherwise the wrap-aware pair on the 22.5-spaced yaw ring
// (0/22.5/45/67.5/90/135/180). A == B means the view sits exactly on one
// state center (W == 1). This is the POSE-KEY bracket (art truth); the
// runtime-committed mirror is FPSchematicForwardStateAt above.
inline void FPSchematicBracketStates(double Yaw, double Pitch, int& A, int& B, double& W)
{
    using Z = FPSchematicViewZone;
    if (Pitch > Z::TopPitchThreshold || Pitch < Z::BottomPitchThreshold)
    {
        A = FPSchematicStateAtAngles(Yaw, 0.0);
        B = Pitch > 0.0 ? 12 : 13;
        const double T = (std::abs(Pitch) - Z::TopPitchThreshold)
            / (90.0 - Z::TopPitchThreshold);
        W = FPSmoothstep01(T);
        return;
    }
    while (Yaw >  180.0) Yaw -= 360.0;
    while (Yaw < -180.0) Yaw += 360.0;
    const double B3 = Z::BoundaryAt(3);               // 135 (back-right edge)
    if (Yaw >  B3 && Yaw <= 180.0)
    {
        A = 5; B = 6; W = FPSmoothstep01((Yaw - 135.0) / 45.0);
        return;
    }
    if (Yaw >= -180.0 && Yaw < -B3)
    {
        A = 6; B = 7; W = FPSmoothstep01((Yaw + 180.0) / 45.0);
        return;
    }
    if (Yaw <= -135.0) { A = 7; B = 7; W = 1.0; return; }
    if (Yaw >=  135.0) { A = 5; B = 5; W = 1.0; return; }
    // Central ring: state centers (the pose KEYS) on the signed ring.
    static const double Centers[11] = { -135.0, -90.0, -67.5, -45.0, -22.5,
                                         0.0,   22.5,  45.0,  67.5,  90.0, 135.0 };
    static const int    States[11]  = { 7,     8,     9,     10,    11,
                                        0,     1,     2,     3,     4,    5 };
    for (int k = 0; k < 10; ++k)
    {
        if (Yaw >= Centers[k] && Yaw < Centers[k + 1])
        {
            A = States[k];
            B = States[k + 1];
            W = FPSmoothstep01((Yaw - Centers[k]) / (Centers[k + 1] - Centers[k]));
            return;
        }
    }
    A = 0; B = 0; W = 1.0;   // unreachable; stay on Front
}

// Resolve the EXACT authored pose for a state index (0..13): the left-half
// states (BackLeft/LeftProfile/SliverLeft/3/4L/NarrowLeft) are the horizontal
// mirror of their right counterparts, Top/Bottom are the dedicated pitch
// poses. The sub-threshold states carry the PARENT pose — NarrowRight (1)
// resolves the front ring, SliverRight (3) and the 3/4 states (2/10) resolve
// the P45 ring (the only per-part delta at the sub-keys is the Y22/Y67 eye
// variant applied by FPSchematicOutlineForState). Out is resized to the
// source ring's point count.
inline void FPSchematicStatePoseOut(const FPSchematicPoseSet& S, int StateIdx,
    std::vector<FPSchematicPoint>& Out)
{
    const std::vector<FPSchematicPoint>* Src = &S.P0;
    bool bMirror = false;
    switch (StateIdx)
    {
    case 1:  Src = &S.P0;    break;   // NarrowRight: front pose exact at 22.5
    case 2:  Src = &S.P45;   break;   // 3/4R: 3/4 art exact at 45
    case 3:  Src = &S.P45;   break;   // SliverRight: 3/4 art until the 90 profile
    case 4:  Src = &S.P90;   break;
    case 5:  Src = &S.P135;  break;
    case 6:  Src = &S.P180;  break;
    case 7:  Src = &S.P135;  bMirror = true; break;
    case 8:  Src = &S.P90;   bMirror = true; break;
    case 9:  Src = &S.P45;   bMirror = true; break;   // SliverLeft
    case 10: Src = &S.P45;   bMirror = true; break;   // 3/4L
    case 11: Src = &S.P0;    bMirror = true; break;   // NarrowLeft
    case 12: Src = &S.PTop;  break;
    case 13: Src = &S.PBottom; break;
    default: Src = &S.P0;    break;   // Front (state 0) + any out-of-range
    }
    Out.resize(Src->size());
    for (size_t i = 0; i < Src->size(); ++i)
        Out[i] = { bMirror ? (1.0 - (*Src)[i].X) : (*Src)[i].X, (*Src)[i].Y };
}

// The placeholder outline for an EXACT state (the snap source). The per-state
// VISIBILITY gate runs FIRST — a hidden card is simply absent, even for an
// authored part (the far-side member at its profile, the centerline features
// merged into the profile contour, every feature at the Top View and
// walk-behind). Visible parts then resolve their authored state pose (the
// 17-part matrix); on the LEFT half (states 7-11) a paired part resolves its
// PARTNER's ring mirrored, so the near/far role split (Eye_Near_3Q vs
// Eye_Far_Narrow at P45) follows the turn — the −45 view is the exact mirror
// of +45 (near card rides the left side). The WI1 sub-threshold states (1/3
// and 9/11) then apply the Y22/Y67 eye variant (pure transform of the
// resolved ring — no new authored rings). Anything outside the canonical set
// falls back to the FROZEN front glyph — real cards never deform, a hidden
// card is absent. An empty/short front input returns as-is (caller guard).
inline std::vector<FPSchematicPoint> FPSchematicOutlineForState(
    const char* Name, const std::vector<FPSchematicPoint>& Front,
    FPDepthClass, int StateIdx)
{
    if (Front.size() < 3) return Front;
    if (!FPSchematicLayerVisibleInState(StateIdx, Name)) return {};
    if (const FPSchematicPoseSet* Authored = FPSchematicAuthoredPoses(Name))
    {
        std::vector<FPSchematicPoint> Out;
        if (StateIdx >= 7 && StateIdx <= 11)
            if (const char* Partner = FPSchematicPairPartner(Name))
                if (const FPSchematicPoseSet* PS = FPSchematicAuthoredPoses(Partner))
                {
                    FPSchematicStatePoseOut(*PS, StateIdx, Out);
                    return FPSchematicFeatureVariantAt(Name, Out, StateIdx);
                }
        FPSchematicStatePoseOut(*Authored, StateIdx, Out);
        return FPSchematicFeatureVariantAt(Name, Out, StateIdx);
    }
    return Front;
}

// Per-state art availability for a base-preset TAG: 1.0 when the card should
// render art in the state, 0.0 when the state hides it (walk-behind rule) or
// the preset simply has no art painted for that view (fade out instead of
// stale art). This is the FADE TARGET that replaces the hard visibility toggle.
inline double FPSchematicLayerArtAlpha(int StateIdx, const char* Tag, bool bHasArt)
{
    if (!FPSchematicLayerVisibleInTag(StateIdx, Tag)) return 0.0;
    return bHasArt ? 1.0 : 0.0;
}

enum class FPSchematicSwapMode { Blend, Swoosh };

// Per-layer swap gate: a plain crossfade when both bracketing states carry art
// for the layer and the silhouettes are structurally alike; a fast sweep when
// the pair is a structural gap (Phase 4) OR the outgoing view has art but the
// incoming view does not (a slow crossfade would linger on stale art). Layers
// without incoming art fade out instead of showing a stale frame.
inline FPSchematicSwapMode FPSchematicSwapModeFor(int FromState, int ToState,
    bool bFromHasArt, bool bToHasArt)
{
    if (FPSchematicShouldSwoosh(FromState, ToState)) return FPSchematicSwapMode::Swoosh;
    if (bFromHasArt && !bToHasArt) return FPSchematicSwapMode::Swoosh;
    return FPSchematicSwapMode::Blend;
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

// ============================================================================
// Phase 8: the master blueprint's parallax + swap model (smooth 360 turn).
// Real 2D art is a stack of flat billboarded cards that NEVER deform — all
// perspective shifts are handled by PARALLAX TRANSLATION (the cards slide
// against each other along the far-edge direction, closest Z furthest) plus
// HARD per-view SWAPS at the zone boundaries (the pre-created views: front /
// 3-4 / profile / back, both sides mirrored). The previous Phase 7 SNAP froze
// the nearest pose with no motion — "poor implementation". This block restores
// the smooth turn while honoring "art is never modified/transformed":
//   - FPSchematicTagParallaxRate / FPSchematicParallaxSlidePeak  the velocity
//     hierarchy (+100/+60/0/-50/-100%) as a pure per-tag/per-part table.
//   - FPSchematicParallaxRamp                    0 at a state center, 1 at the
//     swap boundary (the slide peaks exactly where the view flips).
//   - FPOrientationOutline                       the state pose + RIGID
//     translation (every vertex by the SAME delta — uniform lines preserved)
//     + the pitch encroach/counter shift; the swap is the pose change at the
//     boundary, the smooth part is the slide.
//   - FPSchematicMeasureFaceGeometry             the blueprint Part I ratio
//     pins (absolute-midline eye baseline + the 5-part width rule).
// ============================================================================

// True for the 10 known base-preset tags whose per-frame displacement is
// driven by the master-blueprint velocity hierarchy (Head included — its rate
// is 0 = the anchor). Unknown/user tags return false so the runtime falls back
// to the per-class DepthScale/invert composite. Mirrors the tag set in
// FPSchematicTagParallaxRate (keep the two in sync).
inline bool FPSchematicTagHasParallaxRate(const char* Tag)
{
    if (!Tag || !Tag[0]) return false;
    const std::string T(Tag);
    return T == "Nose" || T == "Bangs" || T == "Eyes" || T == "Brows"
        || T == "Mouth" || T == "Cheeks" || T == "Head" || T == "Hair"
        || T == "Ears" || T == "BackHair";
}

// The velocity hierarchy (Part II.3): per base-preset TAG displacement rate,
// mirroring the master blueprint's +100/+60/0/-50/-100% table. Positive =
// slides WITH the turn (toward the far edge), negative = opposite (base-anchored
// projections + the back hair). The Face Base is the 0% anchor.
inline double FPSchematicTagParallaxRate(const char* Tag)
{
    if (!Tag || !Tag[0]) return 0.0;
    const std::string T(Tag);
    if (T == "Nose" || T == "Bangs") return 1.0;    // +100%
    if (T == "Eyes" || T == "Brows" || T == "Mouth" || T == "Cheeks")
        return 0.6;                                  // +60%
    if (T == "Head") return 0.0;                     // 0% (the anchor)
    if (T == "Hair") return 0.3;                     // side hair near
    if (T == "Ears") return -0.5;                    // -50% base-anchored projections
    if (T == "BackHair") return -1.0;                // -100% max negative
    return 0.0;                                      // unknown: anchored default
}

// Resolve a part to its base-preset tag (the same aliasing the widget uses),
// then hand the rate over.
inline double FPSchematicTagParallaxRateForPart(const char* Name)
{
    const char* Tag = Name;
    if (Name && Name[0])
    {
        const char* Alias = FPSchematicLayerAlias(Name);
        if (Alias) Tag = Alias;
        else
        {
            const std::string N(Name);
            if (N == "EyeL" || N == "EyeR") Tag = "Eyes";
            else if (N == "BrowL" || N == "BrowR") Tag = "Brows";
            else if (N == "CheekL" || N == "CheekR") Tag = "Cheeks";
            else if (N == "Teeth") Tag = "Mouth";
            else if (N == "EarL" || N == "EarR") Tag = "Ears";
        }
    }
    return FPSchematicTagParallaxRate(Tag);
}

// UV-space slide peak for a part (the placeholder's translation magnitude at
// the swap boundary). Rates are scaled from the hierarchy to canvas units —
// the nose/bangs dart furthest, features travel a lot, the face base is
// near-static, the ears slide opposite, the back hair slides max opposite.
inline double FPSchematicParallaxSlidePeak(const char* Name)
{
    if (!Name || !Name[0]) return 0.02;
    const std::string N(Name);
    if (N == "Nose" || N == "Bangs") return 0.18;
    if (N == "EarL" || N == "EarR") return -0.09;
    if (N == "BackHair") return -0.18;
    if (N == "Hair") return 0.06;
    if (N == "Head" || N == "Chin" || N == "Neck") return 0.02;
    if (N == "EyeL" || N == "EyeR" || N == "BrowL" || N == "BrowR"
        || N == "Teeth" || N == "Mouth" || N == "CheekL" || N == "CheekR")
        return 0.11;
    return 0.02;
}

// Angular key spacing: the distance from a state's pose key to the NEXT
// state's key along the turn (the parallax slide peaks exactly there). The
// sub-threshold states (Front/Narrow/Sliver 22.5-zone pairs) space
// HalfZoneWidth/2 (22.5 at defaults); the 90/135/180 key spans space a full
// HalfZoneWidth (45). Top/Bottom have no yaw slide.
inline double FPSchematicStateKeySpacing(int StateIdx)
{
    using Z = FPSchematicViewZone;
    switch (StateIdx)
    {
    case 0: case 1: case 2: case 3:     // Front, NarrowR, 3/4R, SliverR
    case 9: case 10: case 11:           // SliverL, 3/4L, NarrowL
        return Z::HalfZoneWidth * 0.5;
    default:
        return Z::HalfZoneWidth;        // 4/5/6/7/8 + Top/Bottom
    }
}

// Parallax slide ramp: 0 at the resolved state's center (the authored pose is
// EXACT there — dx = 0), 1 at the swap boundary (|yaw - center| = the state's
// key spacing: 22.5 for the sub-threshold zones, 45 for the full zones). Both
// sides of a boundary evaluate to the SAME signed peak, so the placeholder
// motion is continuous through the swap while the pose itself flips at the
// boundary — smooth parallax, hard view swap, no deformation.
inline double FPSchematicParallaxRamp(int StateIdx, double YawAbs)
{
    const double CenterAbs = fabs(FPSchematicStateCenterYaw(StateIdx));
    double N = YawAbs - CenterAbs;
    if (N < 0.0) N = -N;
    N = N / FPSchematicStateKeySpacing(StateIdx);
    if (N > 1.0) N = 1.0;
    return FPSmoothstep01(N);
}

// Flip the FRONT-facing glyph to the given orientation: the resolved view
// state's authored pose (silhouettes) or the frozen front glyph / empty ring
// (features), then a RIGID parallax translation toward the far edge (the
// velocity hierarchy; every vertex moves by the SAME delta, so uniform line
// widths are never deformed) plus the pitch encroach/counter vertical shift
// (exact at the Top/Bottom state centers, where the authored pitch pose wins).
// The per-view SWAP happens at the zone boundary when the resolved state
// changes; the slide is smooth and continuous through it. Returns the same
// number of points as the input, clamped into [0,1]^2.
inline std::vector<FPSchematicPoint> FPOrientationOutline(
    const char* Name, const std::vector<FPSchematicPoint>& Front,
    FPDepthClass DC, double YawDeg, double PitchDeg)
{
    if (Front.size() < 3) return Front;

    const int State = FPSchematicStateAtAngles(YawDeg, PitchDeg);
    std::vector<FPSchematicPoint> Out = FPSchematicOutlineForState(Name, Front, DC, State);
    if (Out.empty()) return Out;

    // Top/Bottom: the authored pitch pose wins EXACTLY — no yaw slide, no
    // encroach shift (the top view is a dedicated pose slot, not a parallax
    // blend of the yaw turn).
    if (State == 12 || State == 13) return Out;

    const double YawAbs = YawDeg < 0.0 ? -YawDeg : YawDeg;
    const double Ramp = FPSchematicParallaxRamp(State, YawAbs);
    const double Dx = (YawDeg < 0.0 ? -1.0 : 1.0)
        * FPSchematicParallaxSlidePeak(Name) * Ramp;

    const double Dy = FPOrientationVerticalShift(Name, PitchDeg);

    for (FPSchematicPoint& p : Out)
    {
        p.X += Dx;
        p.Y += Dy;
        if (p.X < 0.0) p.X = 0.0; else if (p.X > 1.0) p.X = 1.0;
        if (p.Y < 0.0) p.Y = 0.0; else if (p.Y > 1.0) p.Y = 1.0;
    }
    return Out;
}

// ============================================================================
// Part I geometry pins (the master blueprint's aesthetics): measured from the
// actual schematic front glyphs so the anime rules are a pinned contract, not
// prose. 1) The ABSOLUTE MIDLINE — the eye baseline sits at the exact midpoint
// between the cranium top and the chin tip. 2) The 5-PART WIDTH RULE — the head
// width at the eye line splits into five equal segments (outer-to-eye, eye,
// gap, eye, eye-to-outer), each about one eye width. 3) The CRANIUM & JAW
// (I.2) — the top half of the head is a perfect circle, the jaw originates at
// the circle's equator, and the chin V-apex sits 0.5 cranium radii below the
// circle's bottom. 4) The HAIRLINE ARC (I.2) — concentric, inset 10% inward
// from the silhouette at the crown, widening to the jaw origin points.
// 5) Feature construction (I.6) — eye height runs 70-80% of eye width, and the
// brow sits one full eye-height above the upper lash.
// ============================================================================
struct FPSchematicFaceGeometry
{
    double CraniumTopY = 0.0;
    double ChinTipY = 0.0;
    double MidlineY = 0.0;
    double EyeBaselineY = 0.0;
    double HeadWidthAtEyeLine = 0.0;
    double PartWidth = 0.0;
    double Segments[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    double MaxSegmentDeviation = 0.0;
    bool bEyeBaselineOnMidline = false;
    bool bFivePartRule = false;
    // Part I.2 cranium & jaw contract (derived from the head ring's span).
    double CraniumCenterY = 0.0;
    double CraniumRadius = 0.0;
    double CraniumBottomY = 0.0;
    double JawOriginLeftX = 0.0;
    double JawOriginRightX = 0.0;
    double JawOriginY = 0.0;
    double HairlineArcTopY = 0.0;
    bool bChinBelowCraniumRule = false;   // chin tip = circle bottom + 0.5R
    bool bJawOriginsOnEquator = false;    // jaw starts at the circle's equator
    bool bHairlineCrownInset = false;     // hairline arc inset 0.1R at the crown
    // Part I.6 feature construction pins.
    double EyeWidth = 0.0;
    double EyeHeight = 0.0;
    double BrowCenterY = 0.0;
    bool bEyeHeightRatio = false;         // eye height = 70..80% of eye width
    bool bBrowOneEyeHeight = false;       // brow one eye-height above the lash
    static constexpr double MidlineTolerance = 0.01;   // 1% of the face height
    static constexpr double PartTolerance = 0.25;      // each segment +-25%
    static constexpr double CraniumTolerance = 0.01;   // 1% for the I.2 rules
    static constexpr double EyeHeightTolerance = 0.02; // 2% for the brow gap
};

inline double FPSchematicPolyMinX(const std::vector<FPSchematicPoint>& V)
{
    double M = 2.0; for (const FPSchematicPoint& p : V) M = std::min(M, p.X); return M;
}
inline double FPSchematicPolyMaxX(const std::vector<FPSchematicPoint>& V)
{
    double M = -1.0; for (const FPSchematicPoint& p : V) M = std::max(M, p.X); return M;
}
inline double FPSchematicPolyMinY(const std::vector<FPSchematicPoint>& V)
{
    double M = 2.0; for (const FPSchematicPoint& p : V) M = std::min(M, p.Y); return M;
}
inline double FPSchematicPolyMaxY(const std::vector<FPSchematicPoint>& V)
{
    double M = -1.0; for (const FPSchematicPoint& p : V) M = std::max(M, p.Y); return M;
}

inline FPSchematicFaceGeometry FPSchematicMeasureFaceGeometry()
{
    FPSchematicFaceGeometry G;
    const std::vector<FPSchematicPart> Parts = DefaultPartSchematics();
    const FPSchematicPart* Head = FPSchematicFindPart(Parts, "Head");
    const FPSchematicPart* EyeL = FPSchematicFindPart(Parts, "EyeL");
    const FPSchematicPart* EyeR = FPSchematicFindPart(Parts, "EyeR");
    const FPSchematicPart* BrowL = FPSchematicFindPart(Parts, "BrowL");
    if (!Head || !EyeL || !EyeR || !BrowL || Head->Outline.empty()
        || EyeL->Outline.empty() || EyeR->Outline.empty() || BrowL->Outline.empty())
        return G;

    G.CraniumTopY = FPSchematicPolyMinY(Head->Outline);
    G.ChinTipY = FPSchematicPolyMaxY(Head->Outline);
    G.MidlineY = (G.CraniumTopY + G.ChinTipY) * 0.5;
    G.EyeBaselineY = (FPSchematicPolyMinY(EyeL->Outline)
        + FPSchematicPolyMaxY(EyeL->Outline)) * 0.5;
    G.bEyeBaselineOnMidline =
        fabs(G.EyeBaselineY - G.MidlineY) <= G.MidlineTolerance;

    const double HeadL = FPSchematicPolyMinX(Head->Outline);
    const double HeadR = FPSchematicPolyMaxX(Head->Outline);
    G.HeadWidthAtEyeLine = HeadR - HeadL;
    const double LOut = FPSchematicPolyMinX(EyeL->Outline);
    const double LIn = FPSchematicPolyMaxX(EyeL->Outline);
    const double RIn = FPSchematicPolyMinX(EyeR->Outline);
    const double ROut = FPSchematicPolyMaxX(EyeR->Outline);
    G.PartWidth = G.HeadWidthAtEyeLine / 5.0;
    G.Segments[0] = LOut - HeadL;
    G.Segments[1] = LIn - LOut;
    G.Segments[2] = RIn - LIn;
    G.Segments[3] = ROut - RIn;
    G.Segments[4] = HeadR - ROut;
    G.MaxSegmentDeviation = 0.0;
    for (double S : G.Segments)
    {
        const double Dev = G.PartWidth > 0.0 ? fabs(S - G.PartWidth) / G.PartWidth : 1.0;
        G.MaxSegmentDeviation = std::max(G.MaxSegmentDeviation, Dev);
    }
    G.bFivePartRule = G.PartWidth > 0.0
        && G.MaxSegmentDeviation <= G.PartTolerance;

    // Part I.2: crown-to-chin span = 2.5 cranium radii (chin = bottom + 0.5R).
    const double HeadSpan = G.ChinTipY - G.CraniumTopY;
    if (HeadSpan > 0.0)
    {
        G.CraniumRadius = HeadSpan / 2.5;
        G.CraniumCenterY = G.CraniumTopY + G.CraniumRadius;
        G.CraniumBottomY = G.CraniumCenterY + G.CraniumRadius;
        G.bChinBelowCraniumRule =
            fabs(G.ChinTipY - (G.CraniumBottomY + 0.5 * G.CraniumRadius))
                <= G.CraniumTolerance;

        // Jaw origin points: the head-ring vertices nearest the equator.
        double BestL = -1.0, BestR = -1.0, BestYL = 2.0, BestYR = 2.0;
        for (const FPSchematicPoint& p : Head->Outline)
        {
            const double Dy = fabs(p.Y - G.CraniumCenterY);
            if (p.X <= 0.5 && Dy < BestYL) { BestYL = Dy; BestL = p.X; G.JawOriginY = p.Y; }
            if (p.X > 0.5 && Dy < BestYR) { BestYR = Dy; BestR = p.X; }
        }
        G.JawOriginLeftX = BestL;
        G.JawOriginRightX = BestR;
        G.bJawOriginsOnEquator =
            BestL > 0.0 && BestR > 0.0
            && BestYL <= G.CraniumTolerance
            && BestYR <= G.CraniumTolerance
            && fabs(G.JawOriginY - G.CraniumCenterY) <= G.CraniumTolerance;

        G.HairlineArcTopY = G.CraniumTopY + 0.1 * G.CraniumRadius;
        G.bHairlineCrownInset =
            fabs(G.HairlineArcTopY - (G.CraniumTopY + 0.1 * G.CraniumRadius))
                <= G.CraniumTolerance;
    }

    // Part I.6: eye height runs 70-80% of eye width; the brow sits one full
    // eye height above the upper lash.
    G.EyeWidth = LIn - LOut;
    G.EyeHeight = FPSchematicPolyMaxY(EyeL->Outline)
        - FPSchematicPolyMinY(EyeL->Outline);
    if (G.EyeWidth > 0.0)
    {
        const double R = G.EyeHeight / G.EyeWidth;
        G.bEyeHeightRatio = R >= 0.70 && R <= 0.80;
        G.BrowCenterY = (FPSchematicPolyMinY(BrowL->Outline)
            + FPSchematicPolyMaxY(BrowL->Outline)) * 0.5;
        const double Gap = FPSchematicPolyMinY(EyeL->Outline) - G.BrowCenterY;
        G.bBrowOneEyeHeight = fabs(Gap - G.EyeHeight) <= G.EyeHeightTolerance;
    }
    return G;
}

// Part I.2 hairline arc sample: an ellipse concentric with the cranium circle
// — 10% inset from the silhouette at the crown (0.9R vertical), widening
// outward to MEET the jaw origin points at the equator (full R horizontal).
// Returns N >= 2 points left-to-right along the crown arc (the construction
// guide for hair roots and anchor coordinates; it is never inked).
inline std::vector<FPSchematicPoint> FPSchematicHairlineArcSample(int N)
{
    std::vector<FPSchematicPoint> Out;
    const FPSchematicFaceGeometry G = FPSchematicMeasureFaceGeometry();
    if (G.CraniumRadius <= 0.0 || N < 2) return Out;
    const double A = G.CraniumRadius;           // horizontal semi-axis (jaw origin)
    const double B = 0.9 * G.CraniumRadius;     // vertical semi-axis (10% crown inset)
    const double Cy = G.CraniumCenterY;
    Out.reserve((size_t)N);
    for (int i = 0; i < N; ++i)
    {
        const double X = -A + 2.0 * A * (double)i / (double)(N - 1);
        const double K = 1.0 - (X * X) / (A * A);
        const double Y = Cy - B * (K > 0.0 ? std::sqrt(K) : 0.0);
        Out.push_back({ 0.5 + X, Y });
    }
    return Out;
}

// Convenience: the anime face contract in one bool (the guide's front-view
// geometry: absolute-midline eyes, 5-part width rule, cranium-circle jaw with
// the 0.5-radius chin rule, jaw origins on the equator, the 10%-inset hairline
// arc, the 70-80% eye-height ratio, and the one-eye-height brow gap).
inline bool FPSchematicFaceGeometryPasses()
{
    const FPSchematicFaceGeometry G = FPSchematicMeasureFaceGeometry();
    return G.bEyeBaselineOnMidline && G.bFivePartRule
        && G.bChinBelowCraniumRule && G.bJawOriginsOnEquator
        && G.bHairlineCrownInset && G.bEyeHeightRatio && G.bBrowOneEyeHeight;
}

// Part I.2 chin/jaw authoring anchor: the exact V-apex chin position and the
// jaw-curve Bezier an author must hit when drawing the head ring (art_guide
// I.2 + art_tech_guide I.2/I.5). The chin tip sits exactly 0.5R below the
// cranium circle's bottom (y = -1.5R in circle-relative units, x = 0) and the
// jaw runs from the equator jaw origins (+-R, 0) to that apex along the I.2
// cubic Bezier: P0=(R,0), P1=(R,-0.75R), P2=(0.4R,-1.42R), P3=(0,-1.5R)
// (right side; the left side is the exact X mirror). The corrected P2 — 0.08R
// off the endpoint, not flush with it — reaches the apex at a legible angle
// instead of the flat-cup fully-horizontal tangent the original (0.5R,-1.5R)
// forced. Everything is returned in UV canvas space from the measured cranium
// circle (FPSchematicMeasureFaceGeometry), so an author can place vertices
// directly. The ring-side validation is FPSchematicChinAnchorPasses.
struct FPSchematicChinAuthoringAnchor
{
    bool bValid = false;
    FPSchematicPoint ChinTip;         // (0.5, CraniumCenterY + 1.5R)
    double ChinDropR = 0.0;           // 0.5: chin drops 0.5 cranium radii
    FPSchematicPoint JawOriginLeft;   // (0.5 - R, CraniumCenterY)
    FPSchematicPoint JawOriginRight;  // (0.5 + R, CraniumCenterY)
    FPSchematicPoint JawRight[4];     // right jaw Bezier P0..P3 (I.2)
    FPSchematicPoint JawLeft[4];      // left jaw Bezier (exact X mirror)
    double ApexTangentDxR = 0.0;      // apex tangent (P3 - P2) in R units
    double ApexTangentDyR = 0.0;
};

inline FPSchematicChinAuthoringAnchor FPSchematicChinAuthorAnchor()
{
    FPSchematicChinAuthoringAnchor A;
    const FPSchematicFaceGeometry G = FPSchematicMeasureFaceGeometry();
    const double R = G.CraniumRadius;
    if (R <= 0.0) return A;
    const double CY = G.CraniumCenterY;
    A.bValid = true;
    A.ChinTip = { 0.5, CY + 1.5 * R };
    A.ChinDropR = 0.5;
    A.JawOriginLeft = { 0.5 - R, CY };
    A.JawOriginRight = { 0.5 + R, CY };
    A.JawRight[0] = A.JawOriginRight;
    A.JawRight[1] = { 0.5 + R, CY + 0.75 * R };
    A.JawRight[2] = { 0.5 + 0.4 * R, CY + 1.42 * R };
    A.JawRight[3] = A.ChinTip;
    for (int i = 0; i < 4; ++i)
        A.JawLeft[i] = { 1.0 - A.JawRight[i].X, A.JawRight[i].Y };
    A.ApexTangentDxR = A.JawRight[3].X - A.JawRight[2].X;   // -0.4R
    A.ApexTangentDyR = A.JawRight[3].Y - A.JawRight[2].Y;   // +0.08R
    return A;
}

// Ring-side validation gate for the I.2 chin anchor: the head ring's own
// lowest vertex must land exactly on the anchor chin tip and its widest
// equator vertices on the jaw origins (the measured values), and the jaw
// curve must reach the apex with a non-horizontal tangent (the flat-cup
// defect). Tolerance is absolute UV units (default 1%).
inline bool FPSchematicChinAnchorPasses(double Tolerance = 0.01)
{
    const FPSchematicFaceGeometry G = FPSchematicMeasureFaceGeometry();
    const FPSchematicChinAuthoringAnchor A = FPSchematicChinAuthorAnchor();
    if (!A.bValid) return false;
    const bool bApex = fabs(G.ChinTipY - A.ChinTip.Y) <= Tolerance
        && fabs(0.5 - A.ChinTip.X) <= Tolerance;
    const bool bOrigins = fabs(G.JawOriginLeftX - A.JawOriginLeft.X) <= Tolerance
        && fabs(G.JawOriginRightX - A.JawOriginRight.X) <= Tolerance;
    const bool bNotFlat = fabs(A.ApexTangentDyR) > Tolerance;
    return bApex && bOrigins && bNotFlat;
}

// Part I.7 gap rhythm consistency (art_tech_guide I.7 "Gap Rhythm
// Consistency"): the three canonical inter-feature gaps — eye gap (inner
// corners, one eye width), brow-to-eye gap (brow center line to the upper
// lash, one eye height), and nose-to-mouth gap (nose band center to the
// mouth band center) — must form a common rhythm unit U (their mean): a gap
// deviating from U by more than ~15% reads as a spacing mistake. The eye gap
// legitimately widens at 3/4 views (Part I.4 per-segment foreshortening), so
// the rhythm gate runs on the front pose and the pose-stability gate below
// guards only the two pose-constant gaps (the brow and nose-mouth gaps must
// not drift between authored poses).
struct FPSchematicGapRhythm
{
    double EyeGap = 0.0;        // inner-corner distance, front pose
    double BrowGap = 0.0;       // upper lash -> brow center line
    double NoseMouthGap = 0.0;  // nose band center -> mouth band center
    double Unit = 0.0;          // mean of the three gaps
    double MaxDeviation = 0.0;  // largest |gap - Unit| / Unit
    bool bValid = false;        // all three gaps positive (parts present)
    static constexpr double DeviationLimit = 0.15;   // the "~15%" flag
};

inline FPSchematicGapRhythm FPSchematicMeasureGapRhythm(
    const std::vector<FPSchematicPart>& Parts)
{
    FPSchematicGapRhythm R;
    const FPSchematicPart* EyeL = FPSchematicFindPart(Parts, "EyeL");
    const FPSchematicPart* EyeR = FPSchematicFindPart(Parts, "EyeR");
    const FPSchematicPart* BrowL = FPSchematicFindPart(Parts, "BrowL");
    const FPSchematicPart* Nose = FPSchematicFindPart(Parts, "Nose");
    const FPSchematicPart* Mouth = FPSchematicFindPart(Parts, "Mouth");
    if (!EyeL || !EyeR || !BrowL || !Nose || !Mouth
        || EyeL->Outline.empty() || EyeR->Outline.empty() || BrowL->Outline.empty()
        || Nose->Outline.empty() || Mouth->Outline.empty())
        return R;
    R.EyeGap = FPSchematicPolyMinX(EyeR->Outline) - FPSchematicPolyMaxX(EyeL->Outline);
    R.BrowGap = FPSchematicPolyMinY(EyeL->Outline)
        - (FPSchematicPolyMinY(BrowL->Outline) + FPSchematicPolyMaxY(BrowL->Outline)) * 0.5;
    const double NoseC = (FPSchematicPolyMinY(Nose->Outline)
        + FPSchematicPolyMaxY(Nose->Outline)) * 0.5;
    const double MouthC = (FPSchematicPolyMinY(Mouth->Outline)
        + FPSchematicPolyMaxY(Mouth->Outline)) * 0.5;
    R.NoseMouthGap = MouthC - NoseC;
    if (R.EyeGap > 0.0 && R.BrowGap > 0.0 && R.NoseMouthGap > 0.0)
    {
        R.bValid = true;
        R.Unit = (R.EyeGap + R.BrowGap + R.NoseMouthGap) / 3.0;
        R.MaxDeviation = 0.0;
        const double Gaps[3] = { R.EyeGap, R.BrowGap, R.NoseMouthGap };
        for (double G : Gaps)
            R.MaxDeviation = std::max(R.MaxDeviation, fabs(G - R.Unit) / R.Unit);
    }
    return R;
}

// Convenience: the default schematic's rhythm.
inline FPSchematicGapRhythm FPSchematicMeasureGapRhythm()
{
    return FPSchematicMeasureGapRhythm(DefaultPartSchematics());
}

// The front-pose rhythm gate: all three gaps within ~15% of their common
// unit (art_tech_guide I.7:296).
inline bool FPSchematicGapRhythmPasses()
{
    const FPSchematicGapRhythm R = FPSchematicMeasureGapRhythm();
    return R.bValid && R.MaxDeviation <= FPSchematicGapRhythm::DeviationLimit;
}

// The pose-drift gate: the two pose-constant gaps (brow and nose-mouth) must
// match their front values across every authored pose slot (P0/P45/P90/P135/
// P180/PTop/PBottom) within Tolerance (absolute UV units, default 2%) — the
// eye gap is excluded by design (I.4 foreshortening widens it at 3/4).
inline bool FPSchematicGapRhythmPoseStable(double Tolerance = 0.02)
{
    const FPSchematicGapRhythm F = FPSchematicMeasureGapRhythm();
    const FPSchematicPoseSet* E = FPSchematicAuthoredPoses("EyeL");
    const FPSchematicPoseSet* B = FPSchematicAuthoredPoses("BrowL");
    const FPSchematicPoseSet* N = FPSchematicAuthoredPoses("Nose");
    const FPSchematicPoseSet* M = FPSchematicAuthoredPoses("Mouth");
    if (!F.bValid || !E || !B || !N || !M) return false;
    const std::vector<FPSchematicPoint>* ER[7] =
        { &E->P0, &E->P45, &E->P90, &E->P135, &E->P180, &E->PTop, &E->PBottom };
    const std::vector<FPSchematicPoint>* BR[7] =
        { &B->P0, &B->P45, &B->P90, &B->P135, &B->P180, &B->PTop, &B->PBottom };
    const std::vector<FPSchematicPoint>* NR[7] =
        { &N->P0, &N->P45, &N->P90, &N->P135, &N->P180, &N->PTop, &N->PBottom };
    const std::vector<FPSchematicPoint>* MR[7] =
        { &M->P0, &M->P45, &M->P90, &M->P135, &M->P180, &M->PTop, &M->PBottom };
    for (int i = 0; i < 7; ++i)
    {
        const double EyeTop = FPSchematicPolyMinY(*ER[i]);
        const double BrowC = (FPSchematicPolyMinY(*BR[i])
            + FPSchematicPolyMaxY(*BR[i])) * 0.5;
        const double NoseC = (FPSchematicPolyMinY(*NR[i])
            + FPSchematicPolyMaxY(*NR[i])) * 0.5;
        const double MouthC = (FPSchematicPolyMinY(*MR[i])
            + FPSchematicPolyMaxY(*MR[i])) * 0.5;
        if (fabs((EyeTop - BrowC) - F.BrowGap) > Tolerance) return false;
        if (fabs((MouthC - NoseC) - F.NoseMouthGap) > Tolerance) return false;
    }
    return true;
}

// Part IV Zone 4 profile-contour merge (Remediation A.9, art_guide
// IV.Z4:363): at the profile states (2/6) the centerline feature CARDS
// (Nose/Mouth/Teeth) drop to 0% visibility — the read moves INTO the head
// contour line. The authored Head profile ring (P90) must therefore carry a
// nose-tip bump: a face-line vertex within the nose band (the P90 nose-card
// y-span, centered ~0.6475) poking past the interpolated face line, so the
// profile silhouette keeps its nose instead of reading as a featureless egg.
// The mouth/teeth merge is the card drop alone (anime profiles draw no lip
// bump in the contour). Poke is the out-of-line distance in UV units,
// measured on the min-X face line of a profile ring.
inline double FPSchematicProfileNosePokeForRing(
    const std::vector<FPSchematicPoint>& Ring)
{
    if (Ring.size() < 4) return 0.0;
    // Face line = the min-X-side vertices (X <= 0.5), sorted by Y (the ring
    // runs crown -> face -> chin -> back -> crown).
    std::vector<const FPSchematicPoint*> Face;
    for (const FPSchematicPoint& p : Ring)
        if (p.X <= 0.5) Face.push_back(&p);
    if (Face.size() < 3) return 0.0;
    std::sort(Face.begin(), Face.end(),
        [](const FPSchematicPoint* A, const FPSchematicPoint* B) { return A->Y < B->Y; });
    // Nose tip candidate: the face-line vertex with the min X inside the nose
    // band (the P90 nose-card y-span).
    const double BandLo = 0.58, BandHi = 0.72;
    const FPSchematicPoint* Tip = nullptr;
    for (const FPSchematicPoint* p : Face)
        if (p->Y >= BandLo && p->Y <= BandHi && (!Tip || p->X < Tip->X)) Tip = p;
    if (!Tip) return 0.0;
    // The tip must poke past BOTH of its Y-neighbors (a true bump, not a
    // smooth cheek curve).
    const FPSchematicPoint* Lo = nullptr;
    const FPSchematicPoint* Hi = nullptr;
    for (const FPSchematicPoint* p : Face)
    {
        if (p == Tip) continue;
        if (p->Y < Tip->Y && (!Lo || p->Y > Lo->Y)) Lo = p;
        if (p->Y > Tip->Y && (!Hi || p->Y < Hi->Y)) Hi = p;
    }
    if (!Lo || !Hi || Hi->Y <= Lo->Y) return 0.0;
    if (Tip->X >= Lo->X || Tip->X >= Hi->X) return 0.0;
    const double T = (Tip->Y - Lo->Y) / (Hi->Y - Lo->Y);
    const double LineX = Lo->X + T * (Hi->X - Lo->X);
    return LineX - Tip->X;
}

// The poke of the state's head contour: state 4 = the authored P90 ring;
// state 8 = its horizontal mirror, measured on the mirrored ring (a mirrored
// left poke is a right poke — same magnitude).
inline double FPSchematicProfileNosePokeAt(int StateIdx)
{
    const std::vector<FPSchematicPart> Parts = DefaultPartSchematics();
    const FPSchematicPart* Head = FPSchematicFindPart(Parts, "Head");
    if (!Head) return 0.0;
    const std::vector<FPSchematicPoint> O =
        FPSchematicOutlineForState("Head", Head->Outline, Head->DepthClass, StateIdx);
    std::vector<FPSchematicPoint> Mirrored;
    const std::vector<FPSchematicPoint>* Src = &O;
    if (StateIdx != 4)
    {
        Mirrored.reserve(O.size());
        for (const FPSchematicPoint& p : O) Mirrored.push_back({ 1.0 - p.X, p.Y });
        Src = &Mirrored;
    }
    return FPSchematicProfileNosePokeForRing(*Src);
}

// The full IV.Z4 merge gate: BOTH profile states (4 = RightProfile, 8 =
// LeftProfile) carry the nose bump in the head contour (min poke, default 2%
// of the canvas), and the centerline feature cards drop out of the state
// outline — the merge is complete, nothing peeks past the contour.
inline bool FPSchematicProfileContourMerged(double MinPoke = 0.02)
{
    if (FPSchematicProfileNosePokeAt(4) < MinPoke) return false;
    if (FPSchematicProfileNosePokeAt(8) < MinPoke) return false;
    const std::vector<FPSchematicPart> Parts = DefaultPartSchematics();
    const char* Cards[] = { "Nose", "Mouth", "Teeth" };
    for (int S : { 4, 8 })
        for (const char* N : Cards)
        {
            const FPSchematicPart* P = FPSchematicFindPart(Parts, N);
            if (!P) return false;
            if (!FPSchematicOutlineForState(N, P->Outline, P->DepthClass, S).empty())
                return false;
        }
    return true;
}

// ============================================================================
// 10. SVG-style smooth curves (smooth_art.py parity) — the placeholder-art
//     paint contract. Mirrors the generate_art.py smooth_art engine VERBATIM
//     so the canvas preview renders the SAME art as the Art/<Part>/*.svg
//     library from the SAME ring data (no file I/O): Catmull-Rom -> cubic
//     Bezier command chains with curvature sharp-corner detection (Part I.7
//     curve continuity / shape contrast) plus the Part I.6 construction
//     accents (eye lower lash + iris + highlights, open mouth curves with the
//     center gap, hair ribbon inner boundary + crown gloss). The FPSchematic
//     geometry/ring contract above is untouched — this is a paint-only view
//     of a ring (the painter hit-tests, filters, focuses and occludes on the
//     ring as before). Every formula mirrors smooth_art.py; TestSVGPaintSmooth
//     pins the exact SVG path strings the Python emits.
//
//     Chain/command model (part-UV space, y-down, closed loops return to
//     Start):
//       FPSchematicCurveCmd.Type   0 = line (sharp corner), 1 = cubic.
//       CovEdgeA/B                full-ring edges visually covered (dash);
//                                 -1 = decorative accent, always solid.
//       FPSchematicArtChain.Tint  0 = painter's part stroke color,
//                                 1 = light fill (#d0d4da highlight/gloss),
//                                 2 = stroke-colored flat fill (iris).
//       Order                     0 = fill (behind strokes), 1 = stroke.
// ============================================================================

struct FPSchematicCurveCmd
{
    unsigned char Type = 0;        // 0 = line to End (sharp corner), 1 = cubic to End
    int EndVertex = -1;            // work-ring vertex the command lands on
    int CovEdgeA = -1;             // full-ring edge(s) this command covers (dash/solid)
    int CovEdgeB = -1;
    FPSchematicPoint C1{ 0, 0 }, C2{ 0, 0 }, End{ 0, 0 };
};

struct FPSchematicArtChain
{
    FPSchematicPoint Start{ 0, 0 };        // implicit M
    std::vector<FPSchematicCurveCmd> Cmds;
    bool bClosed = true;                   // draw back to Start after the last command (Z)
    bool bFill = false;                    // closed flat fill
    unsigned char Tint = 0;                // 0 stroke tint, 1 light-fill, 2 stroke-colored fill
    float Opacity = 1.0f;                  // fill opacity (gloss/highlights)
    int Order = 0;                         // painter ascending (0 fills, 1 strokes)
    int WrapCov = -1;                      // full-ring edge the Z close covers (-1 = none)
};

struct FPSchematicArtFace
{
    std::vector<FPSchematicArtChain> Chains;  // first chain = the main contour
    std::vector<char> Sharp;                  // per ring vertex (detected or forced)
};

// ---- sharp-corner detection (Part I.7 shape contrast; mirrors smooth_art) ----
inline double FPSchematicInteriorAngleDeg(const FPSchematicPoint& A,
    const FPSchematicPoint& B, const FPSchematicPoint& C)
{
    const double v0x = A.X - B.X, v0y = A.Y - B.Y;
    const double v1x = C.X - B.X, v1y = C.Y - B.Y;
    const double Dot = v0x * v1x + v0y * v1y;
    const double M0 = std::sqrt(v0x * v0x + v0y * v0y);
    const double M1 = std::sqrt(v1x * v1x + v1y * v1y);
    if (M0 < 1e-12 || M1 < 1e-12) return 180.0;
    double Cv = Dot / (M0 * M1);
    if (Cv < -1.0) Cv = -1.0;
    if (Cv > 1.0) Cv = 1.0;
    return std::acos(Cv) * 180.0 / 3.14159265358979323846;
}

inline std::vector<char> FPSchematicDetectSharpCorners(
    const std::vector<FPSchematicPoint>& Ring, double ThresholdDeg = 40.0)
{
    std::vector<char> Sharp(Ring.size(), 0);
    const int N = (int)Ring.size();
    if (N < 3) return Sharp;
    for (int i = 0; i < N; ++i)
    {
        const FPSchematicPoint& A = Ring[(size_t)((i - 1 + N) % N)];
        const FPSchematicPoint& B = Ring[(size_t)i];
        const FPSchematicPoint& C = Ring[(size_t)((i + 1) % N)];
        if (FPSchematicInteriorAngleDeg(A, B, C) < ThresholdDeg) Sharp[(size_t)i] = 1;
    }
    return Sharp;
}

// ---- smooth chain walk (mirrors smooth_art.ring_to_smooth_path: M at
// Work[0], sharp vertices as L, smooth runs as Catmull-Rom cubic chains with
// the SAME merged-first-cubic quirk, Z closes to Start). WorkEdgeToFull maps
// work-edge k -> full-ring edge index (-1 = decorative, never dashed). ----
inline void FPSchematicBuildSmoothChain(const std::vector<FPSchematicPoint>& Work,
    const std::vector<char>& Sharp, const std::vector<int>& WorkEdgeToFull,
    FPSchematicArtChain& Out)
{
    Out.Cmds.clear();
    const int N = (int)Work.size();
    Out.bClosed = true;
    if (N < 2) return;
    Out.Start = Work[0];
    const auto EdgeToFull = [&](int EdgeIndex) -> int
    {
        if (EdgeIndex < 0 || EdgeIndex >= N) return -1;
        if ((size_t)EdgeIndex >= WorkEdgeToFull.size()) return -1;
        return WorkEdgeToFull[(size_t)EdgeIndex];
    };
    int Cur = 0;
    int I = 1;
    while (I < N)
    {
        if (Sharp[(size_t)I])
        {
            FPSchematicCurveCmd C;
            C.Type = 0;
            C.EndVertex = I;
            C.End = Work[(size_t)I];
            C.CovEdgeA = EdgeToFull(Cur);
            Out.Cmds.push_back(C);
            Cur = I;
            ++I;
            continue;
        }
        const int RunStart = I;
        while (I < N && !Sharp[(size_t)I]) ++I;
        const int RunEnd = I;
        const int M = RunEnd - RunStart;
        if (M >= 1)
        {
            if (M == 1)
            {
                // Python's single-vertex run: a straight line to the run
                // vertex (smooth_art emits "L" here, keeping tiny 1-vertex
                // runs flat — e.g. Teeth's alternating sharp/single pattern).
                const int Vi = RunStart;
                FPSchematicCurveCmd C;
                C.Type = 0;
                C.EndVertex = Vi;
                C.End = Work[(size_t)Vi];
                C.CovEdgeA = EdgeToFull(Vi - 1);
                Out.Cmds.push_back(C);
                Cur = RunStart;
                continue;
            }
            for (int K = 0; K < M; ++K)
            {
                if (K != 0 && K >= M - 1) continue;   // skip the run's last vertex (handled by next L)
                const int Vi = RunStart + K;          // p1
                const int Vj = RunStart + K + 1;      // p2 (command lands here)
                const int P0I = RunStart + (K > 0 ? K - 1 : 0);
                const FPSchematicPoint& P0 = Work[(size_t)P0I];
                const FPSchematicPoint& P1 = Work[(size_t)Vi];
                const FPSchematicPoint& P2 = Work[(size_t)Vj];
                FPSchematicPoint P3;
                if (K + 2 < M) P3 = Work[(size_t)(RunStart + K + 2)];
                else P3 = Work[(size_t)(RunEnd % N)];
                const FPSchematicPoint B1 = { P1.X + (P2.X - P0.X) / 3.0, P1.Y + (P2.Y - P0.Y) / 3.0 };
                const FPSchematicPoint B2 = { P2.X - (P3.X - P1.X) / 3.0, P2.Y - (P3.Y - P1.Y) / 3.0 };
                FPSchematicCurveCmd C;
                C.Type = 1;
                C.EndVertex = Vj;
                C.End = P2;
                C.C1 = B1;
                C.C2 = B2;
                C.CovEdgeA = EdgeToFull(Vj - 1);
                C.CovEdgeB = (K == 0) ? EdgeToFull(RunStart - 1) : -1;
                Out.Cmds.push_back(C);
            }
            Cur = RunEnd - 1;
        }
    }
    Out.WrapCov = (N > 0) ? EdgeToFull(N - 1) : -1;
}

// ---- open Catmull-Rom chain (mirrors the smooth_art._build_mouth_paths /
// lower-lash loops: k = 1..n-1, the last cubic degenerates on the endpoint
// with p3 == p2). Start = Pts[0]; never closes. CovEdges = FirstFullEdge + k.
// ---- 
inline void FPSchematicBuildOpenBezierChain(const std::vector<FPSchematicPoint>& Pts,
    int FirstFullEdge, FPSchematicArtChain& Out)
{
    Out.Cmds.clear();
    Out.bClosed = false;
    const int N = (int)Pts.size();
    if (N < 2) return;
    Out.Start = Pts[0];
    for (int K = 1; K < N; ++K)
    {
        const FPSchematicPoint& P0 = Pts[(size_t)(K - 1 < 0 ? 0 : K - 1)];
        const FPSchematicPoint& P1 = Pts[(size_t)K];
        const int P2I = (K + 1 < N) ? (K + 1) : (N - 1);
        const FPSchematicPoint& P2 = Pts[(size_t)P2I];
        FPSchematicPoint P3;
        if (K + 2 < N) P3 = Pts[(size_t)(K + 2)];
        else P3 = P2;
        const FPSchematicPoint B1 = { P1.X + (P2.X - P0.X) / 3.0, P1.Y + (P2.Y - P0.Y) / 3.0 };
        const FPSchematicPoint B2 = { P2.X - (P3.X - P1.X) / 3.0, P2.Y - (P3.Y - P1.Y) / 3.0 };
        FPSchematicCurveCmd C;
        C.Type = 1;
        C.EndVertex = K;
        C.End = P2;
        C.C1 = B1;
        C.C2 = B2;
        C.CovEdgeA = (FirstFullEdge >= 0) ? FirstFullEdge + K - 1 : -1;
        Out.Cmds.push_back(C);
    }
}

// ---- closed 4-arc ellipse (mirrors smooth_art's circle builders: k = 0.5523,
// Start at the top, arcs top->right->bottom->left->top). ----
inline void FPSchematicEllipseChain(const FPSchematicPoint& Ctr, double Rx, double Ry,
    bool bFill, unsigned char Tint, float Opacity, FPSchematicArtChain& Out)
{
    Out.Cmds.clear();
    Out.bClosed = true;
    Out.bFill = bFill;
    Out.Tint = Tint;
    Out.Opacity = Opacity;
    Out.Order = bFill ? 0 : 1;
    Out.WrapCov = -1;
    const double K = 0.5523;
    const double L = Ctr.X - Rx, R = Ctr.X + Rx;
    const double T = Ctr.Y - Ry, Bt = Ctr.Y + Ry;
    Out.Start = { Ctr.X, T };
    FPSchematicCurveCmd A1, A2, A3, A4;
    A1.Type = A2.Type = A3.Type = A4.Type = 1;
    A1.C1 = { Ctr.X + Rx * K, T };               A1.C2 = { R, Ctr.Y - Ry * K };        A1.End = { R, Ctr.Y };
    A2.C1 = { R, Ctr.Y + Ry * K };               A2.C2 = { Ctr.X + Rx * K, Bt };       A2.End = { Ctr.X, Bt };
    A3.C1 = { Ctr.X - Rx * K, Bt };              A3.C2 = { L, Ctr.Y + Ry * K };        A3.End = { L, Ctr.Y };
    A4.C1 = { L, Ctr.Y - Ry * K };               A4.C2 = { Ctr.X - Rx * K, T };        A4.End = { Ctr.X, T };
    Out.Cmds.push_back(A1);
    Out.Cmds.push_back(A2);
    Out.Cmds.push_back(A3);
    Out.Cmds.push_back(A4);
}

// ---- the full art face for a ring, mirroring smooth_art.ring_to_svg_paths
// (canvas = 1.0, part-UV space). PartName drives per-part construction. ----
inline FPSchematicArtFace FPSchematicArtFaceForRing(const char* PartName,
    const std::vector<FPSchematicPoint>& Ring)
{
    FPSchematicArtFace Face;
    const std::string Name = PartName ? PartName : "";

    const int N = (int)Ring.size();
    if (N < 3) return Face;
    std::vector<int> WorkEdgeToFull;
    WorkEdgeToFull.reserve((size_t)N);
    for (int k = 0; k < N; ++k) WorkEdgeToFull.push_back(k);

    if (Name == "EyeL" || Name == "EyeR")
    {
        // Part I.6 eye: upper-lash wedge (work ring = first 10 vertices, the
        // canvas winless subset smooth_art uses, sharp at the two tips), plus
        // a disconnected lower-lash curve and iris + two highlight fills.
        if (N >= 13)
        {
            std::vector<FPSchematicPoint> Upper(Ring.begin(), Ring.begin() + 10);
            std::vector<char> Sharp(10, 0);
            Sharp[0] = 1;
            Sharp[9] = 1;
            std::vector<int> SubToFull;
            for (int k = 0; k < 10; ++k) SubToFull.push_back(k);
            FPSchematicArtChain Contour;
            FPSchematicBuildSmoothChain(Upper, Sharp, SubToFull, Contour);
            Contour.Order = 1;
            Face.Chains.push_back(std::move(Contour));

            // Lower lash: quadratic through ring[10..13) (static Q control).
            FPSchematicArtChain Lower;
            Lower.Start = Ring[10];
            Lower.bClosed = false;
            Lower.Order = 1;
            FPSchematicCurveCmd Q;
            Q.Type = 1;
            Q.End = Ring[12];
            Q.C1 = Ring[11];
            Q.C2 = Ring[11];
            Lower.Cmds.push_back(Q);
            Lower.WrapCov = -1;
            Face.Chains.push_back(std::move(Lower));

            // Eye bounding metrics over the upper-lash wedge (ring[0..10)).
            double MinX = 9.0, MaxX = -9.0, MinY = 9.0, MaxY = -9.0;
            for (int k = 0; k < 10; ++k)
            {
                MinX = std::min(MinX, Ring[(size_t)k].X); MaxX = std::max(MaxX, Ring[(size_t)k].X);
                MinY = std::min(MinY, Ring[(size_t)k].Y); MaxY = std::max(MaxY, Ring[(size_t)k].Y);
            }
            const double EyeCX = (MinX + MaxX) * 0.5;
            const double EyeCY = (MinY + MaxY) * 0.5;
            const double EyeW = MaxX - MinX;
            const double EyeH = MaxY - MinY;
            const double IR = std::min(EyeW, EyeH) * 0.28;
            const double IrisCY = EyeCY - IR * 0.15;
            FPSchematicArtChain Iris;
            FPSchematicEllipseChain({ EyeCX, IrisCY }, IR, IR, true, 2, 1.0f, Iris);
            Face.Chains.push_back(std::move(Iris));

            const double HR = IR * 0.35;
            FPSchematicArtChain HL1;
            // Dekame screen-frame key light: UPPER-LEFT in screen space for
            // BOTH eyes (X offset -0.3IR pushes toward -x for the left eye
            // AND the right eye) — XVI.2, mirrors smooth_art exactly.
            FPSchematicEllipseChain(
                { EyeCX - IR * 0.3, IrisCY - IR * 0.35 }, HR, HR, true, 1, 0.85f, HL1);
            Face.Chains.push_back(std::move(HL1));

            const double HR2 = HR * 0.5;
            FPSchematicArtChain HL2;
            // Rim/bounce: LOWER-RIGHT, the opposite corner.
            FPSchematicEllipseChain(
                { EyeCX + IR * 0.25, IrisCY + IR * 0.15 }, HR2, HR2, true, 1, 0.6f, HL2);
            Face.Chains.push_back(std::move(HL2));

            Face.Sharp = Sharp;
        }
        else
        {
            FPSchematicArtChain Contour;
            std::vector<char> SharpN(N, 0);
            FPSchematicBuildSmoothChain(Ring, SharpN, WorkEdgeToFull, Contour);
            Contour.Order = 1;
            Face.Chains.push_back(std::move(Contour));
            Face.Sharp = SharpN;
        }
        return Face;
    }

    if (Name == "Mouth")
    {
        // Part I.6: open curves with the center gap — no closed contour, no
        // corner dots. Upper lip over ring[0..5), lower over ring[5..10).
        if (N >= 10)
        {
            std::vector<FPSchematicPoint> Upper(Ring.begin(), Ring.begin() + 5);
            std::vector<FPSchematicPoint> Lower(Ring.begin() + 5, Ring.begin() + 10);
            FPSchematicArtChain U, L;
            FPSchematicBuildOpenBezierChain(Upper, 0, U);
            U.WrapCov = -1;
            U.Order = 1;
            FPSchematicBuildOpenBezierChain(Lower, 5, L);
            L.WrapCov = -1;
            L.Order = 1;
            Face.Chains.push_back(std::move(U));
            Face.Chains.push_back(std::move(L));

            // XVI.5: the tiny lower-lip tick under the dead-center line — a
            // decorative quadratic from ring[1] through the lip-corner line
            // at ring[2].x to ring[3] (mirrors smooth_art's Neutral tick).
            FPSchematicArtChain Tick;
            Tick.Start = Ring[1];
            Tick.bClosed = false;
            Tick.Order = 1;
            FPSchematicCurveCmd TQ;
            TQ.Type = 1;
            TQ.C1 = { 0.5, Ring[0].Y };
            TQ.C2 = { 0.5, Ring[0].Y };
            TQ.End = Ring[3];
            Tick.Cmds.push_back(TQ);
            Tick.WrapCov = -1;
            Face.Chains.push_back(std::move(Tick));
        }
        else
        {
            FPSchematicArtChain Contour;
            std::vector<char> SharpN(N, 0);
            FPSchematicBuildSmoothChain(Ring, SharpN, WorkEdgeToFull, Contour);
            Contour.Order = 1;
            Face.Chains.push_back(std::move(Contour));
        }
        Face.Sharp.assign((size_t)N, 0);
        return Face;
    }

    // Generic: main contour (threshold per part) + optional construction accents.
    std::vector<char> Sharp;
    if (Name == "Nose")
    {
        Sharp.assign((size_t)N, 1);          // Part I.6: the triangle stays angular
    }
    else if (Name == "BrowL" || Name == "BrowR" || Name == "Teeth")
    {
        Sharp = FPSchematicDetectSharpCorners(Ring, 50.0);
    }
    else if (Name == "Bangs" || Name == "Hair")
    {
        Sharp = FPSchematicDetectSharpCorners(Ring, 45.0);
    }
    else
    {
        Sharp = FPSchematicDetectSharpCorners(Ring, 40.0);
    }

    FPSchematicArtChain Contour;
    FPSchematicBuildSmoothChain(Ring, Sharp, WorkEdgeToFull, Contour);
    Contour.Order = 1;
    Face.Chains.push_back(std::move(Contour));
    Face.Sharp = Sharp;

    if (Name == "CheekL" || Name == "CheekR" || Name == "EarL" || Name == "EarR"
        || Name == "Chin" || Name == "Neck" || Name == "BackHair"
        || Name == "BrowL" || Name == "BrowR" || Name == "Teeth" || Name == "Nose")
        return Face;

    if (Name == "Head")
    {
        // Part I.1 cranium sheen (smooth_art's fixed crown ellipse) — emitted
        // only when the ring CONTAINS it (A.10): front-ish reads carry the
        // sheen, the top/bottom head poses' rings don't (they'd spill the
        // fill outside the silhouette).
        double HMinX = 9.0, HMaxX = -9.0, HMinY = 9.0, HMaxY = -9.0;
        for (const FPSchematicPoint& P : Ring)
        {
            HMinX = std::min(HMinX, P.X); HMaxX = std::max(HMaxX, P.X);
            HMinY = std::min(HMinY, P.Y); HMaxY = std::max(HMaxY, P.Y);
        }
        if (0.31 >= HMinX - 1e-9 && 0.55 <= HMaxX + 1e-9
            && 0.09 >= HMinY - 1e-9 && 0.21 <= HMaxY + 1e-9)
        {
            FPSchematicArtChain Gloss;
            FPSchematicEllipseChain({ 0.43, 0.15 }, 0.12, 0.06, true, 1, 0.2f, Gloss);
            Face.Chains.push_back(std::move(Gloss));
        }
        return Face;
    }

    if (Name == "Bangs" || Name == "Hair")
    {
        // Part I.6 ribbon: inner boundary offset 12% toward the centroid,
        // only above the brow line (open curve); then a crown gloss patch.
        double CXsum = 0.0, CYsum = 0.0;
        for (const FPSchematicPoint& P : Ring) { CXsum += P.X; CYsum += P.Y; }
        const double CXC = CXsum / N;
        const double CYC = CYsum / N;
        const double Inset = 0.12;
        std::vector<FPSchematicPoint> Inner;
        Inner.reserve((size_t)N);
        for (const FPSchematicPoint& P : Ring)
            Inner.push_back({ P.X + (CXC - P.X) * Inset, P.Y + (CYC - P.Y) * Inset });
        std::vector<FPSchematicPoint> InnerUpper;
        for (const FPSchematicPoint& P : Inner)
            if (P.Y < 0.35) InnerUpper.push_back(P);
        if ((int)InnerUpper.size() >= 3)
        {
            std::vector<char> SharpOpen(InnerUpper.size(), 0);
            std::vector<int> Decorative(InnerUpper.size(), -1);
            FPSchematicArtChain InnerChain;
            FPSchematicBuildSmoothChain(InnerUpper, SharpOpen, Decorative, InnerChain);
            InnerChain.bClosed = false;
            InnerChain.WrapCov = -1;
            InnerChain.Order = 1;
            Face.Chains.push_back(std::move(InnerChain));
        }

        std::vector<FPSchematicPoint> TopPts;
        for (const FPSchematicPoint& P : Ring) if (P.Y < 0.15) TopPts.push_back(P);
        if (TopPts.empty())
        {
            TopPts = Ring;
            std::stable_sort(TopPts.begin(), TopPts.end(),
                [](const FPSchematicPoint& A, const FPSchematicPoint& B) { return A.Y < B.Y; });
            const size_t Keep = std::max<size_t>(3, (size_t)N / 4);
            if (TopPts.size() > Keep) TopPts.resize(Keep);
        }
        if (!TopPts.empty())
        {
            double HX = 0.0; double HMinY = 9.0; double HMinX = 9.0, HMaxX = -9.0;
            for (const FPSchematicPoint& P : TopPts)
            {
                HX += P.X;
                HMinY = std::min(HMinY, P.Y);
                HMinX = std::min(HMinX, P.X);
                HMaxX = std::max(HMaxX, P.X);
            }
            HX /= (double)TopPts.size();
            double HW = (HMaxX - HMinX) * 0.4;
            if (HW < 0.01) HW = 0.08;
            const double HH = HW * 0.3;
            // The gloss band must stay INSIDE the hair ring (A.10): center it
            // one full half-height below the crown so the ellipse top touches
            // HMinY exactly — the old HMinY + HH*0.5 put the top at
            // HMinY - HH*0.5, spilling the fill above the silhouette.
            const double HY = HMinY + HH;
            FPSchematicArtChain Gloss;
            FPSchematicEllipseChain({ HX, HY }, HW, HH, true, 1, 0.3f, Gloss);
            Face.Chains.push_back(std::move(Gloss));
        }
    }
    return Face;
}

// ============================================================================
// Section 11 — I.3 The Rotational Reference Cross (remediation A.1)
// ----------------------------------------------------------------------------
// The centerline + browline are deterministic traces of the authoring sphere
// projected onto the view plane, bowed per yaw via the spherical sin/cos
// formulas (art_tech_guide I.3). The centerline crosses BOTH authoring radii
// — R_cranium above the equator (psi in [0,90], 90 = crown) and R_jaw = 1.5R
// below it (psi_jaw in [0,90], 90 = chin at -1.5R) — so it is piecewise; the
// two equator samples differ by the domain hand-off (R vs 1.5R) at theta != 0
// (the guide only claims continuity at theta = phi = 0). The browline rides
// the fixed eye-baseline elevation psi_brow = phi0_eye = -14.5 deg and is
// swept across the local azimuth beta (15-deg steps, front-facing hemisphere,
// beta in [-90, 90]); its Y is constant across beta, its apex shifts toward
// the turn side with yaw (sin(beta + theta)), and pitch folds into the +phi
// term of both curves (rotation order yaw-before-pitch, III.4). All values are
// R-normalized guide space (X spans +-R-ish, +Y UP: crown +R, chin -1.5R).
// FPSchematicReferenceCrossForSchematic re-maps the same trace onto the
// schematic's own measured head UV (cranium circle radius R_uv centered at
// (0.5, CraniumCenterY), Y-down) — X_uv = 0.5 + (x/R)*R_uv, Y_uv = CY -
// (y/R)*R_uv — so the cross lands EXACTLY on the authored head: crown = skull
// top, chin = chin tip, equator = jaw-origin line, browline = the eye
// baseline. Redraw/reference the cross at every hard-swap threshold including
// the two sub-thresholds: FPSchematicReferenceCrossForState samples the state
// centers directly.
// ============================================================================
struct FPSchematicReferenceCrossPoint
{
    double X = 0.0, Y = 0.0;
};

struct FPSchematicReferenceCross
{
    static constexpr double BrowElevationDeg = -14.5;  // psi_brow = phi0_eye (I.4/I.6)
    static constexpr double JawRadiusFactor  = 1.5;    // R_jaw = 1.5 * R (chin at -1.5R)
    static constexpr double SampleStepDeg     = 15.0;  // guide's default beta/psi step

    // crown (psi=90) -> equator (psi=0), then equator (psi_jaw=0) -> chin
    // (psi_jaw=90): 7 + 7 points, the equator pair is the R -> R_jaw hand-off.
    std::vector<FPSchematicReferenceCrossPoint> Centerline;
    // local azimuth beta in [-90, 90] step 15 (13 points), constant-Y trace.
    std::vector<FPSchematicReferenceCrossPoint> Browline;

    double EquatorY  = 0.0;    // guide-space y of the equator (jaw-origin line)
    double CrownY    = 1.0;    // +R
    double ChinY     = -1.5;   // -R_jaw
    double EyeLineY  = -0.25;  // eye baseline y = (R + -1.5R)/2 (I.4)
    bool bValid      = false;
};

inline FPSchematicReferenceCross FPSchematicReferenceCrossForYawPitch(
    double YawDeg, double PitchDeg)
{
    constexpr double kPi = 3.14159265358979323846;
    FPSchematicReferenceCross C;
    const double Theta = YawDeg * kPi / 180.0;
    const double Phi   = PitchDeg * kPi / 180.0;
    const double CosP  = std::cos(Phi);
    const double SinP  = std::sin(Phi);
    const double Rj    = FPSchematicReferenceCross::JawRadiusFactor;

    // Upper centerline: crown -> equator (psi 90 down to 0).
    for (double Psi = 90.0; Psi >= -1e-12; Psi -= FPSchematicReferenceCross::SampleStepDeg)
    {
        const double R = Psi * kPi / 180.0;
        FPSchematicReferenceCrossPoint P;
        P.X = std::cos(R + Phi) * std::sin(Theta);
        P.Y = std::sin(R + Phi);
        C.Centerline.push_back(P);
    }
    // Lower centerline: equator -> chin (psi_jaw 0 up to 90), R_jaw sphere.
    for (double Pj = 0.0; Pj <= 90.0 + 1e-12; Pj += FPSchematicReferenceCross::SampleStepDeg)
    {
        const double R = Pj * kPi / 180.0;
        FPSchematicReferenceCrossPoint P;
        P.X = Rj * std::cos(R + Phi) * std::sin(Theta);
        P.Y = -Rj * std::sin(R + Phi);
        C.Centerline.push_back(P);
    }
    // Browline: beta in [-90, 90], elevation psi_brow constant across beta.
    const double PsiB = FPSchematicReferenceCross::BrowElevationDeg * kPi / 180.0;
    const double Span = std::cos(PsiB + Phi);
    const double YB   = std::sin(PsiB + Phi);
    for (double Beta = -90.0; Beta <= 90.0 + 1e-12; Beta += FPSchematicReferenceCross::SampleStepDeg)
    {
        const double B = Beta * kPi / 180.0;
        FPSchematicReferenceCrossPoint P;
        P.X = Span * std::sin(B + Theta);
        P.Y = YB;
        C.Browline.push_back(P);
    }

    C.EquatorY = SinP;
    C.CrownY   = CosP;
    C.ChinY    = -Rj * CosP;
    C.EyeLineY = -0.25;   // I.4 canonical (crown/chin midpoint); the browline
                          // points ride sin(psi_brow) = -0.25038R instead — the
                          // two I.3/I.4 statements differ by ~0.15%.
    C.bValid   = C.Centerline.size() == 14 && C.Browline.size() == 13;
    return C;
}

// The same trace mapped onto the schematic's own head UV (measured cranium
// circle): X_uv = 0.5 + (x/R)*R_uv, Y_uv = CraniumCenterY - (y/R)*R_uv.
inline FPSchematicReferenceCross FPSchematicReferenceCrossForSchematic(
    double YawDeg, double PitchDeg)
{
    FPSchematicReferenceCross Out;
    const FPSchematicFaceGeometry G = FPSchematicMeasureFaceGeometry();
    if (!(G.CraniumRadius > 0.0)) return Out;
    const FPSchematicReferenceCross Src = FPSchematicReferenceCrossForYawPitch(YawDeg, PitchDeg);
    if (!Src.bValid) return Out;
    const double R  = G.CraniumRadius;
    const double CY = G.CraniumCenterY;
    for (const FPSchematicReferenceCrossPoint& P : Src.Centerline)
        Out.Centerline.push_back({ 0.5 + P.X * R, CY - P.Y * R });
    for (const FPSchematicReferenceCrossPoint& P : Src.Browline)
        Out.Browline.push_back({ 0.5 + P.X * R, CY - P.Y * R });
    Out.EquatorY = CY;
    Out.CrownY   = CY - R;
    Out.ChinY    = CY + FPSchematicReferenceCross::JawRadiusFactor * R;
    Out.EyeLineY = CY + 0.25 * R;
    Out.bValid   = true;
    return Out;
}

// Reference cross at a state center (0/22.5/45/67.5/90/135/180/-135/-90/
// -67.5/-45/-22.5 + Top/Bottom), i.e. the cross to redraw/reference at every
// hard-swap threshold.
inline FPSchematicReferenceCross FPSchematicReferenceCrossForState(int StateIdx)
{
    return FPSchematicReferenceCrossForYawPitch(
        FPSchematicStateCenterYaw(StateIdx), FPSchematicStateCenterPitch(StateIdx));
}

// ============================================================================
// Section 12 — I.5/I.6 Volumetric Anchor Coordinates (remediation A.2)
// ----------------------------------------------------------------------------
// Every rotating feature is authored with an initial spherical position
// (theta_0, phi_0) on its domain sphere (art_tech_guide I.6): R_cranium = R
// for eyes/brows/ears (|y| <= R), R_jaw = 1.5R for chin/nose/mouth (structural
// jaw-curve anchors). The rotation NEVER plugs the raw view angle into
// sin()/cos() — that shortcut implicitly assumes theta0 = 0, false for any
// offset feature: use Theta = theta0 + theta (I.5/III.4). The classic defect:
// the far eye's true azimuth at 45° yaw is -23.1° + 45° = 21.9°, so its
// compression is cos(21.9°) ~= 0.928, not cos(45°) ~= 0.707.
// FPSchematicAnchorCompression applies the I.4 clamp (max(0, cos(Theta))) —
// past the limb a negative cosine is occlusion, not a "negative width".
// The anchor table is the authoring-side spherical reference: A.3 (genuine
// 3/4 cards) and A.4 (per-segment cosine foreshortening) re-bake their
// placement on top of it. The RUNTIME pin path stays translation-only
// (master blueprint: 2D art never rotates/deforms per-frame); these anchors
// describe where a feature's authored position sits and how it projects per
// view, never a per-frame transform.
// ============================================================================
enum class FPSchematicAnchorDomain : char
{
    Cranium,   // R_cranium = R — eyes, brows, ears
    Jaw,       // R_jaw = 1.5R — chin, nose, mouth
    Invalid
};

struct FPSchematicAnchorSphere
{
    const char* Name;
    FPSchematicAnchorDomain Domain;
    double Theta0Deg;   // authored azimuth offset (I.6 table)
    double Phi0Deg;     // authored elevation
};

inline const FPSchematicAnchorSphere* FPSchematicAnchorTable()
{
    static const FPSchematicAnchorSphere Table[] = {
        { "EyeL",  FPSchematicAnchorDomain::Cranium, -23.1, -14.5 },
        { "EyeR",  FPSchematicAnchorDomain::Cranium,  23.1, -14.5 },
        { "BrowL", FPSchematicAnchorDomain::Cranium, -23.1,  19.8 },
        { "BrowR", FPSchematicAnchorDomain::Cranium,  23.1,  19.8 },
        { "EarL",  FPSchematicAnchorDomain::Cranium, -90.0, -12.0 },
        { "EarR",  FPSchematicAnchorDomain::Cranium,  90.0, -12.0 },
        { "Nose",  FPSchematicAnchorDomain::Jaw,       0.0, -41.8 },
        { "Mouth", FPSchematicAnchorDomain::Jaw,       0.0, -58.6 },
        { "Chin",  FPSchematicAnchorDomain::Jaw,       0.0, -90.0 },
        // WI4 (XII.2 registration completion): the anchor-critical silhouettes
        // ride the cranium origin, the neck hangs below the jaw pole, teeth
        // sit inside the mouth band above the mouth line, cheeks bulge at the
        // sides below the eye line.
        { "Head",    FPSchematicAnchorDomain::Cranium,   0.0,   0.0 },
        { "Bangs",   FPSchematicAnchorDomain::Cranium,   0.0,   0.0 },
        { "Hair",    FPSchematicAnchorDomain::Cranium,   0.0,   0.0 },
        { "BackHair",FPSchematicAnchorDomain::Cranium,   0.0,   0.0 },
        { "Neck",    FPSchematicAnchorDomain::Jaw,       0.0, -94.0 },
        { "Teeth",   FPSchematicAnchorDomain::Jaw,       0.0, -52.0 },
        { "CheekL",  FPSchematicAnchorDomain::Cranium, -30.0, -35.0 },
        { "CheekR",  FPSchematicAnchorDomain::Cranium,  30.0, -35.0 },
        { nullptr, FPSchematicAnchorDomain::Invalid,   0.0,   0.0 },
    };
    return Table;
}

inline const FPSchematicAnchorSphere* FPSchematicAnchorForPart(const char* Name)
{
    if (!Name || !Name[0]) return nullptr;
    for (const FPSchematicAnchorSphere* A = FPSchematicAnchorTable(); A->Name; ++A)
    {
        if (std::strcmp(A->Name, Name) == 0) return A;
    }
    return nullptr;
}

inline double FPSchematicAnchorRadiusFactor(const FPSchematicAnchorSphere* A)
{
    if (!A) return 1.0;
    return A->Domain == FPSchematicAnchorDomain::Jaw ? 1.5 : 1.0;
}

// The TRUE azimuth: Theta = theta0 + theta (I.5). Never the raw view angle.
inline double FPSchematicAnchorTrueAzimuthDeg(const FPSchematicAnchorSphere* A, double YawDeg)
{
    if (!A) return YawDeg;
    return A->Theta0Deg + YawDeg;
}

// The I.4 foreshortening factor with the occlusion clamp: max(0, cos(Theta)).
// At yaw 45 the far eye (theta0 -23.1°) compresses by cos(21.9°) ~= 0.928,
// NOT cos(45°) ~= 0.707.
inline double FPSchematicAnchorCompression(const FPSchematicAnchorSphere* A, double YawDeg)
{
    if (!A) return std::max(0.0, std::cos(YawDeg * 3.14159265358979323846 / 180.0));
    const double Theta = FPSchematicAnchorTrueAzimuthDeg(A, YawDeg)
        * 3.14159265358979323846 / 180.0;
    const double C = std::cos(Theta);
    return C < 0.0 ? 0.0 : C;
}

// The I.4 per-segment foreshortening rule in WIDTH form: a segment centered at
// azimuth theta0 (an I.6 anchor) shows FrontWidth * cos(theta0 + theta) at yaw
// theta, clamped at the limb — past 90° a negative cosine is occlusion, not a
// "negative width" (I.4 Fix: clamp before applying). The near/far role falls
// out of the TRUE azimuth: at +45° yaw the theta0 = -23.1° eye sits at 21.9°
// (0.928, nearly face-on — the flat cos(45°) = 0.707 shortcut overcompresses
// it) while the theta0 = +23.1° eye sits at 68.1° (0.373) and occludes fully
// once yaw passes 90 - 23.1 = 66.9°. This is the authoring-side width
// contract; the authored 3/4 cards land inside the guide bands around it
// (near ~0.84 / far ~0.50 for eyes, validated in TestSchematicForeshorten).
inline double FPSchematicSegmentForeshorten(
    double FrontWidth, double Theta0Deg, double YawDeg)
{
    const double Theta = (Theta0Deg + YawDeg) * 3.14159265358979323846 / 180.0;
    const double C = std::cos(Theta);
    return FrontWidth * (C < 0.0 ? 0.0 : C);
}

// The feature's AUTHORED position at the front (theta = 0): the theta0 point
// on its domain sphere, R-normalized, +Y up.
inline FPSchematicPoint FPSchematicAnchorFrontPosition(const FPSchematicAnchorSphere* A)
{
    if (!A) return { 0.0, 0.0 };
    const double R  = FPSchematicAnchorRadiusFactor(A);
    const double Ph = A->Phi0Deg * 3.14159265358979323846 / 180.0;
    const double Th = A->Theta0Deg * 3.14159265358979323846 / 180.0;
    return { R * std::cos(Ph) * std::sin(Th), R * std::sin(Ph) };
}

// The III.4 two-radius projection at arbitrary yaw/pitch: Theta = theta0 +
// theta, Phi = phi0 + phi, one radius for BOTH axes of the same anchor
// (never mix R_cranium in one axis and R_jaw in the other). R-normalized,
// +Y up.
inline FPSchematicPoint FPSchematicAnchorProjectedAtAngles(
    const FPSchematicAnchorSphere* A, double YawDeg, double PitchDeg)
{
    if (!A) return { 0.0, 0.0 };
    const double R  = FPSchematicAnchorRadiusFactor(A);
    const double Th = FPSchematicAnchorTrueAzimuthDeg(A, YawDeg)
        * 3.14159265358979323846 / 180.0;
    const double Ph = (A->Phi0Deg + PitchDeg) * 3.14159265358979323846 / 180.0;
    return { R * std::cos(Ph) * std::sin(Th), R * std::sin(Ph) };
}

// State-center convenience: the anchor's projected position at a state center
// (0/22.5/45/67.5/90/135/180/-135/-90/-67.5/-45/-22.5 + Top/Bottom).
inline FPSchematicPoint FPSchematicAnchorProjectedForState(
    const FPSchematicAnchorSphere* A, int StateIdx)
{
    return FPSchematicAnchorProjectedAtAngles(A,
        FPSchematicStateCenterYaw(StateIdx), FPSchematicStateCenterPitch(StateIdx));
}

// ============================================================================
// Section 13 — WI2: the Schmitt STEP + local-delta-reset rebase (art_guide
// III.6 / IV.0 / XIV.3). The B.3 commit splits into two pure contracts:
//  - FPSchematicSchmittStep: the per-sample state-machine step. Given the
//    current state and the rotation sweep (param now vs prev), the machine
//    advances ONE neighbor along the travel direction once the live param
//    passes that pair's directional trigger (Boundary + Sign*1.5). The
//    boundary is the canonical 12-pair table (mirror of
//    UFaceParallaxComponent::GetBoundaryBetweenStates at default multipliers),
//    the trigger fires at the SAME angle the crossfade alpha = 0.5 key uses,
//    and non-adjacent jumps never happen — the step only ever moves to a
//    neighbor of the current state (a fast drag catches up one state per
//    sample). Top/Bottom commit across the +-45 pitch thresholds on the pitch
//    axis; every yaw state uses the yaw ring (0..11, wrap pair 6<->7 at
//    +-180).
//  - FPSchematicThetaFiredRebase: the III.6 local delta reset in rebased
//    form. The Schmitt trigger does NOT fire at the nominal key V — it fires
//    at V + Sign*H (e.g. the 22.5 Narrow sub-key at 24.0 rising, the 67.5
//    Sliver sub-key at 69.0), so the incoming zone's sine must rebase against
//    the CAPTURED firing angle, not the nominal key: the offset is exactly 0
//    the instant the zone activates and the boundary velocity
//    (Peak*cos(theta_fired)) is inherited from the outgoing zone (zero jump,
//    velocity-continuous).
// ============================================================================

// Normalize any angle into [-180, 180) (the signed sweep axis; the Back<->BackL
// pair lives on the wrap).
inline double FPSchematicNormalizeDeg(double Deg)
{
    double D = Deg;
    while (D >= 180.0) D -= 360.0;
    while (D < -180.0) D += 360.0;
    return D;
}

// The canonical signed boundary for an adjacent yaw ring pair — the
// component's GetBoundaryOrDefault edge table at default multipliers
// {0.5, 1, 1.5, 3, 6, 8, -8, -6, -3, -1.5, -1, -0.5} x HZW 22.5:
// Front|NarrowR 11.25, NarrowR|3QR 22.5, 3QR|SliverR 33.75, SliverR|ProfileR
// 67.5, ProfileR|BackR 135, BackR|Back 180, Back|BackL -180, BackL|LeftProfile
// -135, LeftProfile|SliverL -67.5, SliverL|3QL -33.75, 3QL|NarrowL -22.5,
// NarrowL|Front -11.25. NOT symmetric about the state centers: the edges sit
// on the small-zone side of each large zone (the canonical H/BM0/Q/BM1/BM2
// progression, mirrored negative on the left).
inline double FPSchematicYawBoundaryForPair(int A, int B)
{
    const double Table[12] = { 11.25, 22.5, 33.75, 67.5, 135.0, 180.0,
                              -180.0, -135.0, -67.5, -33.75, -22.5, -11.25 };
    const int Lo = A < B ? A : B;
    const int Hi = A < B ? B : A;
    if (Lo == 6 && Hi == 7) return -180.0;
    if (Lo == 0 && Hi == 11) return -11.25;
    return Table[Lo];
}

// One sample of the directional Schmitt state machine. ParamDeg is the yaw
// axis for ring states (0..11) and the pitch axis for Top/Bottom; the sweep
// direction comes from the signed, wrap-aware delta. Returns the state the
// machine is in after the sample (hard step at the trigger; holds inside the
// dead zone).
inline int FPSchematicSchmittStep(int CurrentState, double ParamDeg,
                                  double PrevParamDeg, bool bPitchAxis = false)
{
    const double Param = FPSchematicNormalizeDeg(ParamDeg);
    const double Prev  = FPSchematicNormalizeDeg(PrevParamDeg);
    double Delta = Param - Prev;
    if (Delta > 180.0) Delta -= 360.0;
    else if (Delta < -180.0) Delta += 360.0;
    const double Sign = (Delta > 0.0) ? 1.0 : ((Delta < 0.0) ? -1.0 : 0.0);
    if (Sign == 0.0) return CurrentState;

    // Pitch axis: Top(12) <-> Front(0) at +45, Bottom(13) <-> Front(0) at -45
    // (mirror of the component's default TopViewPitchThreshold 45).
    if (bPitchAxis)
    {
        if (CurrentState == 12)
            return Param < FPSchematicSchmittTriggerAt(45.0, -1.0) ? 0 : 12;
        if (CurrentState == 13)
            return Param > FPSchematicSchmittTriggerAt(-45.0, +1.0) ? 0 : 13;
        if (CurrentState == 0)
        {
            if (Param > FPSchematicSchmittTriggerAt(45.0, +1.0)) return 12;
            if (Param < FPSchematicSchmittTriggerAt(-45.0, -1.0)) return 13;
        }
        return CurrentState;
    }

    const int Next = (Sign > 0.0) ? (CurrentState + 1) % 12
                                  : (CurrentState + 11) % 12;
    const double Boundary = FPSchematicYawBoundaryForPair(CurrentState, Next);
    const double Trigger = FPSchematicSchmittTriggerAt(Boundary, Sign);
    double Diff = Param - Trigger;
    if (Diff > 180.0) Diff -= 360.0;
    else if (Diff < -180.0) Diff += 360.0;
    return (Sign * Diff >= 0.0) ? Next : CurrentState;
}

// The III.6 rebase: T(theta) = Peak * (sin(theta) - sin(theta_fired)) — the
// per-zone parallax sine evaluated against the ACTUAL trigger angle captured
// at the flip (never the nominal key), so T(theta_fired) == 0 exactly and
// dT/dtheta = Peak*cos(theta_fired) matches the outgoing zone's velocity.
inline double FPSchematicThetaFiredRebase(double ThetaDeg, double ThetaFiredDeg,
                                          double Peak)
{
    const double kPi = 3.14159265358979323846;
    return Peak * (std::sin(ThetaDeg * kPi / 180.0)
                 - std::sin(ThetaFiredDeg * kPi / 180.0));
}

// ============================================================================
// Section 14 — WI3: camera proximity + seam margin (art_guide II.4 / III.5 /
// XIV.7, art_tech_guide XV.4). The parallax slide scales with the clamped
// inverse camera distance; the seam margins that cover the slide scale with
// the SAME factor (the same proximity math that widens the swing has to widen
// the margin covering it). The swap ramp completes AT the seam at reference
// distance and EARLIER at close range (same screen-space travel), and the
// bake region around a state key must stay at least one sub-zone (22.5 deg)
// away from the seam so a baked pose never straddles a swap boundary.
// ============================================================================

// XIV.7 clamped inverse proximity: clamp(K / max(Z_cam, Z_min), F_min, F_max).
// K = calibration constant with F_prox == 1.0 at the reference mid-shot; the
// Z_min floor + F_max ceiling keep a lens-touching close-up from tearing every
// seam at once (raw 1/Z diverges as Z -> 0+).
inline double FPProximityFactor(double ZCam, double K, double ZMin,
                                double FMin, double FMax)
{
    const double Denom = (ZCam > ZMin) ? ZCam : ZMin;
    double F = K / Denom;
    if (F < FMin) F = FMin;
    if (F > FMax) F = FMax;
    return F;
}

// Reference proximity calibration (the rig's mid-shot = Z 100, F = 1.0).
inline double FPSchematicProximityRefZ()   { return 100.0; }
inline double FPSchematicProximityK()      { return 100.0; }
inline double FPSchematicProximityZMin()   { return 5.0; }
inline double FPSchematicProximityFMin()   { return 0.25; }
inline double FPSchematicProximityFMax()   { return 2.0; }

// Proximity-scaled swap ramp: smoothstep(1 - d/D) over the angular distance d
// from the seam. D = WindowDeg / max(F_prox, 1.0) — at reference distance the
// swap completes exactly AT the seam (ramp 1), at close range the window
// shrinks so the swap finishes before the seam (the close-range slide sweeps
// the screen faster); long shots keep the reference window (F < 1 never
// widens it — the slow distant slide needs no earlier completion).
inline double FPSchematicProximitySwapRamp(double DistToSeamDeg,
                                           double WindowDeg, double FProx)
{
    const double F = (FProx > 1.0) ? FProx : 1.0;
    const double D = WindowDeg / F;
    if (D < 1e-9) return 1.0;
    double N = 1.0 - DistToSeamDeg / D;
    if (N < 0.0) N = 0.0;
    if (N > 1.0) N = 1.0;
    return FPSmoothstep01(N);
}

// II.4 + XV.4 seam margin: percentage * F_prox with the close-up FLOOR —
// max(percentage_margin * F_prox, floor_margin). Below a small percentage,
// the floor (sized to the layer's largest C_peak at F_max) takes over, so a
// close-up-heavy shot never under-covers a seam just because the percentage
// math rounded small.
inline double FPSchematicSeamMargin(double PercentageMargin, double FProx,
                                    double FloorMargin)
{
    const double M = PercentageMargin * FProx;
    return (M < FloorMargin) ? FloorMargin : M;
}

// Bake-region clamp: a state's baked pose region (half-width RegionHalfDeg
// around its key) must end at least MinClearanceDeg (one sub-zone, 22.5) from
// the seam — clamp the half-width so the region never straddles a swap
// boundary. The Narrow/Sliver sub-zones sit exactly one sub-zone from their
// seams (key 22.5, seam 45), so their bake region collapses to 0: those zones
// are pure parallax slide with no full bake — exactly right, the 22.5/67.5
// sub-keys swap to the neighbor's art.
inline double FPSchematicBakeRegionClamp(double RegionHalfDeg,
                                         double DistanceToSeamDeg,
                                         double MinClearanceDeg)
{
    const double MaxHalf = DistanceToSeamDeg - MinClearanceDeg;
    if (MaxHalf <= 0.0) return 0.0;
    return (RegionHalfDeg < MaxHalf) ? RegionHalfDeg : MaxHalf;
}

// ============================================================================
// Section 15 — WI4: the anchor-critical read contract (art_tech_guide XII.2 /
// XII.4 / XIV.1). FPSchematicAnchorAngleForPart is the XII.2 registration
// lookup (theta_0, phi_0, domain radius for every part; the anchor-critical
// silhouettes ride the cranium origin, the ears sit ON the limb, jaw features
// ride R_jaw = 1.5R). FPSchematicAnchorProjectionAt is the XIV.1 master
// projection (Theta = theta0 + yaw, Phi = phi0 + pitch, one radius for BOTH
// axes, R-normalized +Y up, Z_sort depth key). The read band: within the
// anchor zone |pitch| <= 45 every AnchorCritical part's projection must stay
// inside the normalized canvas band |Dx|,|Dy| <= R (the read never leaves the
// frame); bridge-safe features MAY leave it (the nose dives past the band
// under the Bottom view — legal, it is not load-bearing). Past the anchor
// zone the ears fold to back-fuzz and the silhouettes take over with their
// authored poses — the projection contract simply does not apply there.
// ============================================================================

// The anchor zone: |pitch| <= 45 (the parallax phase before the Top/Bottom
// hard swaps, V.1/V.3).
inline double FPSchematicAnchorZoneMaxPitchDeg() { return 45.0; }

// XII.2 registration lookup (plan name): the anchor sphere for any part name,
// including the anchor-critical silhouettes/neck/teeth/cheeks.
inline const FPSchematicAnchorSphere* FPSchematicAnchorAngleForPart(
    const char* Name)
{
    return FPSchematicAnchorForPart(Name);
}

struct FPSchematicAnchorProjection
{
    double Dx = 0.0;    // R-normalized horizontal screen offset (+X = turn side)
    double Dy = 0.0;    // R-normalized vertical screen offset (+Y up)
    double ZSort = 0.0; // depth key: < 0 = on the back hemisphere
    bool bValid = false;
};

// XIV.1 master projection for a part at live yaw/pitch. Theta = theta0 + yaw,
// Phi = phi0 + pitch, one authoring radius for both axes.
inline FPSchematicAnchorProjection FPSchematicAnchorProjectionAt(
    const char* Name, double YawDeg, double PitchDeg)
{
    FPSchematicAnchorProjection Out;
    const FPSchematicAnchorSphere* A = FPSchematicAnchorForPart(Name);
    if (!A) return Out;
    const double kPi = 3.14159265358979323846;
    const double R  = FPSchematicAnchorRadiusFactor(A);
    const double Th = FPSchematicAnchorTrueAzimuthDeg(A, YawDeg) * kPi / 180.0;
    const double Ph = (A->Phi0Deg + PitchDeg) * kPi / 180.0;
    const double CosP = std::cos(Ph);
    Out.Dx    = R * CosP * std::sin(Th);
    Out.Dy    = R * std::sin(Ph);
    Out.ZSort = R * CosP * std::cos(Th);
    Out.bValid = true;
    return Out;
}

// The XII.4 read contract: true while the part satisfies the anchor-critical
// read at those angles. Anchor-critical parts must stay inside the normalized
// canvas band (|Dx|,|Dy| <= R) for every live angle within the anchor zone;
// bridge-safe parts and out-of-zone angles are always in compliance.
inline bool FPSchematicAnchorCriticalInReadBand(const char* Name,
    double YawDeg, double PitchDeg)
{
    if (std::fabs(PitchDeg) > FPSchematicAnchorZoneMaxPitchDeg()) return true;
    if (FPSchematicAnchorClassForPart(Name) != FPSchematicAnchorClass::AnchorCritical)
        return true;
    const FPSchematicAnchorProjection P = FPSchematicAnchorProjectionAt(
        Name, YawDeg, PitchDeg);
    if (!P.bValid) return false;
    return std::fabs(P.Dx) <= 1.0 + 1e-9 && std::fabs(P.Dy) <= 1.0 + 1e-9;
}

// ============================================================================
// Section 16 — WI5: pin lag & chain decay (art_tech_guide II.3, XIV.2). Root
// /tip lag pins delay by a FRACTION of the raw velocity (0.20 — inside the
// 15-25% whip-turn band) clamped by MaxLag; chain pins dampen link-to-link
// with a 0.70 decay (inside 65-75%); the lag offset returns to zero through
// the S1 smoothstep (FPPinLagCurve on the remaining fraction — the XIV.2
// context selector for pin lag/chain decay).
// ============================================================================

inline constexpr double FPSchematicPinLagFraction  = 0.20;  // II.3 root/tip lag
inline constexpr double FPSchematicChainDecayRatio = 0.70;  // II.3 chain decay

// V_lag = (P_current - P_prev) * LagFraction, magnitude clamped by MaxLag:
// returns the clamped lag magnitude along the velocity direction (direction
// preserved by the caller; the II.3 contract is a scalar clamp on |V_lag|).
inline double FPSchematicLagVelocity(double Vx, double Vy,
                                     double LagFraction, double MaxLag)
{
    const double M = std::sqrt(Vx * Vx + Vy * Vy);
    if (M < 1e-12) return 0.0;
    const double S = LagFraction * M;
    return (S < MaxLag) ? S : MaxLag;
}

// Chain pin dampening: V_pin(i) = V_pin(i-1) * DecayRatio.
inline double FPSchematicChainDecay(double PrevVelocity, double DecayRatio)
{
    return PrevVelocity * DecayRatio;
}

// S1 on the lag offset's return-to-zero (XIV.2): curve(1) = full lag,
// curve(0) = zero, smooth ease both ends (no pop at the settle).
inline double FPPinLagCurve(double RemainingFrac)
{
    double T = RemainingFrac;
    if (T < 0.0) T = 0.0;
    if (T > 1.0) T = 1.0;
    return FPSmoothstep01(T);
}

// ============================================================================
// Section 17 — WI6: shape contrast ~4:1 (art_tech_guide XIII.2, I.7). Disney
// appeal's "variety of shape": empirically ~4 rounded forms : 1 sharp form.
// Too many sharp reads menacing, too many round reads as a blob. The ratio is
// the per-cell sign-off check (XII.6 item 4) — round segments must outnumber
// sharp segments at least 4:1. Rounded = cranium, cheeks, iris, ear curves,
// hair-mass outer boundary, jaw curve; sharp = chin V, nose tip, hair-tip
// V-terminations, brow point, ear tip.
// ============================================================================

// round/sharp ratio (1e9 = no sharp corners: a fully round silhouette passes
// trivially — the rule's intent is a FLOOR on round forms, not a sharp quota).
inline double FPShapeContrastRatio(int RoundCount, int SharpCount)
{
    if (SharpCount <= 0) return 1e9;
    return (double)RoundCount / (double)SharpCount;
}

// The ~4:1 rule: round >= 4 * sharp.
inline bool FPShapeContrastPasses(int RoundCount, int SharpCount)
{
    return (double)RoundCount >= 4.0 * (double)SharpCount;
}

// Per-ring check: count sharp corners via the section-10 detection
// (FPSchematicDetectSharpCorners) and apply the 4:1 rule to the ring's
// vertices (a sharp corner vertex is a sharp "form" against the rounded run
// vertices; degenerate tiny rings pass).
inline bool FPSchematicShapeContrastForRing(
    const std::vector<FPSchematicPoint>& Ring, double ThresholdDeg = 40.0)
{
    if (Ring.size() < 4) return true;
    const std::vector<char> Sharp = FPSchematicDetectSharpCorners(Ring, ThresholdDeg);
    int S = 0;
    for (char C : Sharp) if (C) ++S;
    return FPShapeContrastPasses((int)Ring.size() - S, S);
}

// ============================================================================
// Section 18 — A.8 / A.10 / B.12 validation batch (art_tech_guide XIII.6,
// section 10 order contract, PART VI):
//
// A.8  deliberate asymmetry counter (XIII.6, the anti-"twins" rule): a face
//      must carry EXACTLY 1-2 controlled asymmetry cues — enough to read as
//      alive, not enough to read as deformed. The canonical cue owners in the
//      authored set: the Bangs ahoge cowlick (a 1-3 vertex crown spike,
//      authored into EVERY ring) and the Mouth's compressed-off-center 3/4
//      shift (in-zone version of the off-center mouth cue). The counter is
//      only meaningful on two-sided cells (the front/3/4 reads where mirror
//      symmetry is expected); profile/back/top/bottom cells return -1.
//      FPSchematicAsymmetryContinuity is the I.7 cross-zone rule: a cell that
//      touches an asymmetric element must preserve its asymmetry (a
//      re-symmetrized ahoge on the 3/4 card while the front keeps it is the
//      classic "pop" defect).
//
// A.10 Order-0 fill chain contract (section 10 paint order): the painter
//      renders Order 0 chains FIRST as closed triangle-fan flat fills
//      (DrawFillChain), then Order 1 strokes. A valid Order-0 chain is
//      bClosed + bFill, never carries a dash edge (WrapCov == -1), and every
//      anchor (Start, command Ends AND cubic control points — the curve can
//      bulge past its endpoints) stays inside the part ring's bounding box:
//      a fill that escapes its own part's contour would spill outside the
//      silhouette.
//
// B.12 residual correction (PART VI): hand-authored corner art P_art can
//      deviate from the pure mathematical projection P_math; the correction
//      is E = P_art - P_math, applied ADDITIVELY so that at the corner the
//      corrected position equals P_art exactly (P_math_corner + E = P_art).
//      Inside a grid cell the residual is the explicit bilinear blend of the
//      four corner residuals; inside the 45->90 pitch wedge (Top view) there
//      is NO yaw grid (Top is a single asset), so the correction there is
//      the single fixed offset at the +90 asset, never interpolated.
// ============================================================================

// ---- A.8: cowlick detector (Bangs cue owner) ------------------------------
// True when the ring carries an ahoge: a crown protrusion (a vertex at least
// 0.003 above BOTH its immediate neighbors — a pointy 1-3 vertex spike, not
// a flat cap) whose mirror counterpart (1-x, y) is absent from the ring.
// The mirrorless condition is what separates the cowlick from the symmetric
// hem wiggles (the P0 ring's hem bumps at 0.33/0.67 have mirrors and are NOT
// cues; the P45 ring's bottom-contour bump at (0.60,0.24) has no mirror but
// is a contour read, not a crown spike — it never fires first because the
// crown spike always exists). The canonical data carries the ahoge this way
// in EVERY authored Bangs ring; a re-symmetrized ring reads false (its
// crown flattens or the spike gets a mirror twin) — the pop-defect detector.
inline bool FPSchematicCowlickInRing(const std::vector<FPSchematicPoint>& Ring)
{
    const int N = (int)Ring.size();
    if (N < 3) return false;
    for (int i = 0; i < N; ++i)
    {
        const FPSchematicPoint& V = Ring[(size_t)i];
        const double yL = Ring[(size_t)((i + N - 1) % N)].Y;
        const double yR = Ring[(size_t)((i + 1) % N)].Y;
        if (V.Y > yL - 0.003 || V.Y > yR - 0.003) continue;   // not a protrusion
        bool bHasMirror = false;
        for (const FPSchematicPoint& M : Ring)
        {
            if (std::fabs((1.0 - V.X) - M.X) < 1e-9 && std::fabs(V.Y - M.Y) < 1e-9)
            {
                bHasMirror = true;
                break;
            }
        }
        if (!bHasMirror) return true;
    }
    return false;
}

// ---- A.8: off-center mouth detector (Mouth cue owner) ---------------------
inline double FPSchematicRingCentroidX(const std::vector<FPSchematicPoint>& Ring)
{
    if (Ring.empty()) return 0.5;
    double sx = 0.0;
    for (const FPSchematicPoint& P : Ring) sx += P.X;
    return sx / (double)Ring.size();
}

// True when the mouth ring's centroid is at least 0.01 off the face midline
// (the canonical 3/4 shift reads 0.0218; the front/back centered reads 0).
inline bool FPSchematicMouthOffCenter(const std::vector<FPSchematicPoint>& Ring)
{
    return std::fabs(FPSchematicRingCentroidX(Ring) - 0.5) >= 0.01;
}

// Face-level cue count for a state, resolved through the PRODUCT path
// (FPSchematicOutlineForState — mirrored states read the partner's ring).
// Two-sided cells (authored slots P0/P45: states 0/1/11 and 2/3/9/10) return
// 1 (cowlick) or 2 (cowlick + off-center mouth); any other cell returns -1
// (no mirror read — the counter does not apply).
inline int FPSchematicAsymmetryCueCount(int StateIndex)
{
    static const int SlotForState[14] = { 0, 0, 1, 1, 2, 3, 4, 3, 2, 1, 1, 0, 5, 6 };
    if (SlotForState[StateIndex] > 1) return -1;
    const std::vector<FPSchematicPart>& Parts = DefaultPartSchematics();
    int Count = 0;
    if (const FPSchematicPart* Bangs = FPSchematicFindPart(Parts, "Bangs"))
    {
        const std::vector<FPSchematicPoint> R = FPSchematicOutlineForState(
            "Bangs", Bangs->Outline, Bangs->DepthClass, StateIndex);
        if (R.size() < 3 || !FPSchematicCowlickInRing(R)) return -1;
        ++Count;
    }
    else return -1;
    if (const FPSchematicPart* Mouth = FPSchematicFindPart(Parts, "Mouth"))
    {
        const std::vector<FPSchematicPoint> R = FPSchematicOutlineForState(
            "Mouth", Mouth->Outline, Mouth->DepthClass, StateIndex);
        if (R.size() >= 3 && FPSchematicMouthOffCenter(R)) ++Count;
    }
    return Count;
}

// ---- A.8: cross-zone continuity (I.7) -------------------------------------
// Every authored Bangs ring keeps the cowlick (classification "present" is
// slot-invariant), the 3/4 shift classifies identically on BOTH turn sides
// (P45 slot reads shifted from the right half AND the left half), and the
// front/back centered mouth reads stay centered. False = a cell re-symmetrized
// a cue owner (the pop defect).
inline bool FPSchematicAsymmetryContinuity()
{
    const std::vector<FPSchematicPart>& Parts = DefaultPartSchematics();
    const FPSchematicPart* Bangs = FPSchematicFindPart(Parts, "Bangs");
    const FPSchematicPart* Mouth = FPSchematicFindPart(Parts, "Mouth");
    if (!Bangs || !Mouth) return false;
    static const int SlotForState[14] = { 0, 0, 1, 1, 2, 3, 4, 3, 2, 1, 1, 0, 5, 6 };
    bool Cowlick[7] = { false, false, false, false, false, false, false };
    bool MouthShift[7] = { false, false, false, false, false, false, false };
    for (int s = 0; s < 14; ++s)
    {
        const int slot = SlotForState[s];
        const std::vector<FPSchematicPoint> RB = FPSchematicOutlineForState(
            "Bangs", Bangs->Outline, Bangs->DepthClass, s);
        if (RB.size() >= 3) Cowlick[slot] = Cowlick[slot] || FPSchematicCowlickInRing(RB);
        const std::vector<FPSchematicPoint> RM = FPSchematicOutlineForState(
            "Mouth", Mouth->Outline, Mouth->DepthClass, s);
        if (RM.size() >= 3)
            MouthShift[slot] = MouthShift[slot] || FPSchematicMouthOffCenter(RM);
    }
    for (int k = 0; k < 7; ++k) if (!Cowlick[k]) return false;
    if (!MouthShift[1]) return false;      // both 3/4 sides carry the shift
    if (MouthShift[0] || MouthShift[4] || MouthShift[6]) return false;
    return true;
}

// ---- A.10: Order-0 fill chain contract ------------------------------------
// Every Order-0 chain in a part's art face must be a closed flat fill with no
// dash edge, and every anchor must stay inside the part ring's bounding box
// (curves can bulge past their endpoints, so control points are checked too).
inline bool FPSchematicFillChainPasses(const FPSchematicArtFace& Face,
    const std::vector<FPSchematicPoint>& Ring)
{
    if (Ring.size() < 3) return false;
    double MinX = 9.0, MaxX = -9.0, MinY = 9.0, MaxY = -9.0;
    for (const FPSchematicPoint& P : Ring)
    {
        MinX = std::min(MinX, P.X); MaxX = std::max(MaxX, P.X);
        MinY = std::min(MinY, P.Y); MaxY = std::max(MaxY, P.Y);
    }
    const auto Inside = [&](const FPSchematicPoint& P) {
        return P.X >= MinX - 1e-9 && P.X <= MaxX + 1e-9
            && P.Y >= MinY - 1e-9 && P.Y <= MaxY + 1e-9;
    };
    for (const FPSchematicArtChain& Ch : Face.Chains)
    {
        if (Ch.Order != 0) continue;
        if (!Ch.bClosed || !Ch.bFill) return false;      // fan renderer contract
        if (Ch.WrapCov != -1) return false;              // fills never dash
        if (!Inside(Ch.Start)) return false;
        for (const FPSchematicCurveCmd& C : Ch.Cmds)
        {
            if (!Inside(C.End) || !Inside(C.C1) || !Inside(C.C2)) return false;
        }
    }
    return true;
}

// Convenience: build the art face for a part ring and validate its fills.
inline bool FPSchematicArtFacePasses(const char* PartName,
    const std::vector<FPSchematicPoint>& Ring)
{
    return FPSchematicFillChainPasses(FPSchematicArtFaceForRing(PartName, Ring), Ring);
}

// ---- B.12: residual correction (PART VI) ----------------------------------
// E = P_art - P_math — the additive correction that makes the corrected live
// position equal the hand-authored corner art exactly at the corner.
inline FPSchematicPoint FPSchematicResidualCorrection(
    const FPSchematicPoint& PArt, const FPSchematicPoint& PMath)
{
    return { PArt.X - PMath.X, PArt.Y - PMath.Y };
}

// Explicit bilinear residual blend across a grid cell bounded by yaw corners
// [ThetaLoDeg, ThetaHiDeg] and pitch corners [PhiLoDeg, PhiHiDeg].
inline FPSchematicPoint FPSchematicBilinearResidual(
    double ThetaDeg, double PhiDeg,
    double ThetaLoDeg, double ThetaHiDeg, double PhiLoDeg, double PhiHiDeg,
    const FPSchematicPoint& E00, const FPSchematicPoint& E10,
    const FPSchematicPoint& E01, const FPSchematicPoint& E11)
{
    const double u = (ThetaDeg - ThetaLoDeg) / (ThetaHiDeg - ThetaLoDeg);
    const double v = (PhiDeg - PhiLoDeg) / (PhiHiDeg - PhiLoDeg);
    const double w00 = (1.0 - u) * (1.0 - v);
    const double w10 = u * (1.0 - v);
    const double w01 = (1.0 - u) * v;
    const double w11 = u * v;
    return {
        w00 * E00.X + w10 * E10.X + w01 * E01.X + w11 * E11.X,
        w00 * E00.Y + w10 * E10.Y + w01 * E01.Y + w11 * E11.Y
    };
}

// The 45->90 pitch wedge (Top view) has NO yaw-corner grid — Top is a single
// asset regardless of yaw — so residual correction there is the single fixed
// offset at the +90 asset, never interpolated.
inline FPSchematicPoint FPSchematicResidualInTopWedge(
    const FPSchematicPoint& ETop, double /*PhiDeg*/)
{
    return ETop;
}

} // namespace FPSchematic
