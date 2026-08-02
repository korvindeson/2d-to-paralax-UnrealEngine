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
//   8. FPYawRule — the front/base/back yaw-motion rule as a pure mirror of
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
        { "BrowL",  { SPT(0.14, 0.14), SPT(0.20, 0.10), SPT(0.30, 0.10), SPT(0.34, 0.15),
                      SPT(0.28, 0.18), SPT(0.19, 0.18) }, FPDepthClass::Front },
        { "BrowR",  { SPT(0.66, 0.10), SPT(0.80, 0.10), SPT(0.86, 0.14), SPT(0.81, 0.18),
                      SPT(0.72, 0.18) }, FPDepthClass::Front },
        { "EyeL",   { SPT(0.15, 0.22), SPT(0.20, 0.19), SPT(0.31, 0.19), SPT(0.35, 0.23),
                      SPT(0.30, 0.29), SPT(0.21, 0.29) }, FPDepthClass::Front },
        { "EyeR",   { SPT(0.65, 0.19), SPT(0.80, 0.19), SPT(0.85, 0.22), SPT(0.79, 0.29),
                      SPT(0.70, 0.29) }, FPDepthClass::Front },
        { "Nose",   { SPT(0.42, 0.24), SPT(0.58, 0.24), SPT(0.60, 0.33), SPT(0.55, 0.35),
                      SPT(0.57, 0.44), SPT(0.55, 0.52), SPT(0.45, 0.52), SPT(0.43, 0.44),
                      SPT(0.45, 0.35), SPT(0.40, 0.33) }, FPDepthClass::Front },
        { "CheekL", { SPT(0.06, 0.30), SPT(0.13, 0.22), SPT(0.20, 0.34), SPT(0.21, 0.52),
                      SPT(0.15, 0.64), SPT(0.07, 0.58) }, FPDepthClass::Front },
        { "CheekR", { SPT(0.80, 0.22), SPT(0.94, 0.30), SPT(0.93, 0.58), SPT(0.85, 0.64),
                      SPT(0.79, 0.52) }, FPDepthClass::Front },
        { "Teeth",  { SPT(0.43, 0.64), SPT(0.57, 0.64), SPT(0.59, 0.68), SPT(0.41, 0.68) },
                      FPDepthClass::Front },
        { "Mouth",  { SPT(0.36, 0.62), SPT(0.42, 0.585), SPT(0.47, 0.605), SPT(0.50, 0.585),
                      SPT(0.53, 0.605), SPT(0.58, 0.585), SPT(0.64, 0.62), SPT(0.62, 0.68),
                      SPT(0.56, 0.73), SPT(0.44, 0.73), SPT(0.38, 0.68) }, FPDepthClass::Front },
        { "Chin",   { SPT(0.40, 0.74), SPT(0.60, 0.74), SPT(0.58, 0.84), SPT(0.50, 0.88),
                      SPT(0.42, 0.84) }, FPDepthClass::Base },
        { "EarL",   { SPT(0.03, 0.22), SPT(0.07, 0.13), SPT(0.11, 0.26), SPT(0.10, 0.44),
                      SPT(0.06, 0.48) }, FPDepthClass::Back },
        { "EarR",   { SPT(0.89, 0.13), SPT(0.97, 0.22), SPT(0.94, 0.48), SPT(0.90, 0.44),
                      SPT(0.89, 0.26) }, FPDepthClass::Back },
        { "Neck",   { SPT(0.42, 0.88), SPT(0.58, 0.88), SPT(0.70, 0.98), SPT(0.30, 0.98) },
                      FPDepthClass::Base },
        { "Bangs",  { SPT(0.28, 0.13), SPT(0.33, 0.05), SPT(0.38, 0.06), SPT(0.44, 0.05),
                      SPT(0.50, 0.05), SPT(0.56, 0.05), SPT(0.62, 0.06), SPT(0.67, 0.05),
                      SPT(0.72, 0.13), SPT(0.66, 0.16), SPT(0.59, 0.12), SPT(0.50, 0.15),
                      SPT(0.41, 0.12), SPT(0.34, 0.16) }, FPDepthClass::Front },
        { "Hair",   { SPT(0.05, 0.52), SPT(0.07, 0.30), SPT(0.12, 0.15), SPT(0.22, 0.05),
                      SPT(0.30, 0.03), SPT(0.70, 0.03), SPT(0.78, 0.05), SPT(0.88, 0.15),
                      SPT(0.93, 0.30), SPT(0.95, 0.52), SPT(0.87, 0.40), SPT(0.88, 0.26),
                      SPT(0.78, 0.12), SPT(0.66, 0.06), SPT(0.34, 0.06), SPT(0.22, 0.12),
                      SPT(0.12, 0.26), SPT(0.13, 0.40) }, FPDepthClass::Back },
        { "BackHair", { SPT(0.20, 0.92), SPT(0.26, 0.80), SPT(0.38, 0.74), SPT(0.50, 0.72),
                      SPT(0.62, 0.74), SPT(0.74, 0.80), SPT(0.80, 0.92), SPT(0.72, 0.97),
                      SPT(0.28, 0.97) }, FPDepthClass::Back },
        { "Head",   { SPT(0.50, 0.02), SPT(0.70, 0.06), SPT(0.86, 0.18), SPT(0.94, 0.40),
                      SPT(0.92, 0.62), SPT(0.81, 0.81), SPT(0.62, 0.93), SPT(0.38, 0.93),
                      SPT(0.19, 0.81), SPT(0.08, 0.62), SPT(0.06, 0.40), SPT(0.14, 0.18),
                      SPT(0.30, 0.06) }, FPDepthClass::Base },
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
// FPYawRule — front/base/back yaw-motion rule. Pure mirror of the component's
// non-vertical offset formula; the classes are data, the mirror is math.
// ============================================================================
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

} // namespace FPSchematic
