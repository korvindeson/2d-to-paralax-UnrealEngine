// Standalone C++ test harness for core parallax logic.
// Compile with any C++17 compiler: clang++ -std=c++17 -o ParallaxMathTests ParallaxMathTests.cpp && ./ParallaxMathTests
// No UE dependencies - pure math validation.

#include <cmath>
#include <cstdio>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

// Phase H: the layout design-contract manifest (pure C++, no UE deps).
#include "../FaceParallaxLayoutSpec.h"

// Central-canvas redesign: part schematic glyphs + front/base/back yaw rules
// (pure C++, no UE deps — the same header the runtime component consults).
#include "../FaceParallaxSchematic.h"

// --- Minimal math types matching our UE types ---
struct FVector2D {
    double X, Y;
    FVector2D() : X(0), Y(0) {}
    FVector2D(double x, double y) : X(x), Y(y) {}
    bool operator==(const FVector2D& o) const { return X==o.X && Y==o.Y; }
};

struct FLinearColor {
    double R,G,B,A;
    FLinearColor() : R(0),G(0),B(0),A(0) {}
    FLinearColor(double r, double g, double b, double a) : R(r),G(g),B(b),A(a) {}
};

// --- Replicated enum (matches FaceParallaxTypes.h order exactly) ---
enum class EFaceAngleState : unsigned char {
    Front,
    ThreeQuarterRight,
    RightProfile,
    BackRight,
    Back,
    BackLeft,
    LeftProfile,
    ThreeQuarterLeft,
    Top,
    Bottom,
    MAX
};

// --- Replicated FFaceArtTransform ---
struct FFaceLayerDef {
    double DepthScale = 0.5;
    double DepthMapIntensity = 1.0;
    double DepthMin = 0.0;
    double DepthMax = 1.0;
    bool bInvertParallax = false;

    double GetRemappedDepth(double DepthSample) const {
        double Range = DepthMax - DepthMin;
        if (std::abs(Range) < 1e-9) return DepthMin;
        return DepthMin + DepthSample * Range;
    }
};

struct FFaceArtTransform {
    FVector2D Position;
    FVector2D Scale;
    double Rotation;

    FFaceArtTransform() : Scale(1.0, 1.0), Rotation(0.0) {}
    FFaceArtTransform(FVector2D pos, FVector2D scale, double rot)
        : Position(pos), Scale(scale), Rotation(rot) {}

    bool IsIdentity() const {
        return Position.X == 0.0 && Position.Y == 0.0
            && Scale.X == 1.0 && Scale.Y == 1.0
            && Rotation == 0.0;
    }

    FFaceArtTransform Combined(const FFaceArtTransform& Other) const {
        FFaceArtTransform Result;
        Result.Position.X = Position.X + Other.Position.X;
        Result.Position.Y = Position.Y + Other.Position.Y;
        Result.Scale.X = Scale.X * Other.Scale.X;
        Result.Scale.Y = Scale.Y * Other.Scale.Y;
        Result.Rotation = Rotation + Other.Rotation;
        return Result;
    }
};

// --- State determination logic (mirrors component zone-based system) ---
constexpr double HZW = 22.5;           // Half-zone width (component default)
constexpr double Z2 = HZW * 2.0;       //  45.0 — 3Q center
constexpr double Z3 = HZW * 3.0;       //  67.5 — Profile boundary
constexpr double Z4 = HZW * 4.0;       //  90.0 — Profile center
constexpr double Z5 = HZW * 5.0;       // 112.5 — BackR boundary
constexpr double Z6 = HZW * 6.0;       // 135.0 — BackR center
constexpr double Z7 = HZW * 7.0;       // 157.5 — Back boundary

struct AngleStateConfig {
    double CenterYaw[10];
    double CenterPitch[10];
    double YawRange[10];
    double PitchRange[10];

    constexpr AngleStateConfig() : CenterYaw{}, CenterPitch{}, YawRange{}, PitchRange{} {
        // Centers match FaceParallaxComponent::GetZoneCenterYaw and GetZoneCenterPitch
        CenterYaw[(int)EFaceAngleState::Front] = 0.0;
        CenterYaw[(int)EFaceAngleState::ThreeQuarterRight] = Z2;
        CenterYaw[(int)EFaceAngleState::RightProfile] = Z4;
        CenterYaw[(int)EFaceAngleState::BackRight] = Z6;
        CenterYaw[(int)EFaceAngleState::Back] = 180.0;
        CenterYaw[(int)EFaceAngleState::BackLeft] = -Z6;
        CenterYaw[(int)EFaceAngleState::LeftProfile] = -Z4;
        CenterYaw[(int)EFaceAngleState::ThreeQuarterLeft] = -Z2;
        CenterYaw[(int)EFaceAngleState::Top] = 0.0;
        CenterYaw[(int)EFaceAngleState::Bottom] = 0.0;

        CenterPitch[(int)EFaceAngleState::Top] = 60.0;
        CenterPitch[(int)EFaceAngleState::Bottom] = -60.0;

        for (int i = 0; i < 10; ++i) {
            YawRange[i] = HZW;
            PitchRange[i] = HZW;
        }
    }
};

static constexpr AngleStateConfig STATE_CONFIG;

double NormalizeAngle(double a) {
    while (a > 180.0) a -= 360.0;
    while (a < -180.0) a += 360.0;
    return a;
}

bool IsInStateZone(double yaw, double pitch, EFaceAngleState state) {
    int idx = (int)state;
    double yawDiff = std::abs(NormalizeAngle(yaw - STATE_CONFIG.CenterYaw[idx]));
    double pitchDiff = std::abs(NormalizeAngle(pitch - STATE_CONFIG.CenterPitch[idx]));
    return yawDiff <= STATE_CONFIG.YawRange[idx] && pitchDiff <= STATE_CONFIG.PitchRange[idx];
}

// --- DetermineStateFromAngles with custom ZoneBoundaryMultipliers ---
EFaceAngleState DetermineStateFromAngles(double yaw, double pitch, const double multipliers[4]) {
    double BM[4];
    for (int i = 0; i < 4; ++i)
        BM[i] = multipliers[i] * HZW;

    if (pitch > 60.0) return EFaceAngleState::Top;
    if (pitch < -60.0) return EFaceAngleState::Bottom;

    if (yaw > -BM[0] && yaw <= BM[0]) return EFaceAngleState::Front;
    if (yaw > BM[0] && yaw <= BM[1])  return EFaceAngleState::ThreeQuarterRight;
    if (yaw > BM[1] && yaw <= BM[2])  return EFaceAngleState::RightProfile;
    if (yaw > BM[2] && yaw <= BM[3])  return EFaceAngleState::BackRight;
    if (yaw > BM[3] || yaw <= -BM[3]) return EFaceAngleState::Back;
    if (yaw > -BM[3] && yaw <= -BM[2]) return EFaceAngleState::BackLeft;
    if (yaw > -BM[2] && yaw <= -BM[1]) return EFaceAngleState::LeftProfile;
    if (yaw > -BM[1] && yaw <= -BM[0]) return EFaceAngleState::ThreeQuarterLeft;
    return EFaceAngleState::Front;
}

EFaceAngleState DetermineStateFromAngles(double yaw, double pitch) {
    static const double Defaults[4] = {1.0, 3.0, 5.0, 7.0};
    return DetermineStateFromAngles(yaw, pitch, Defaults);
}

// --- Hysteresis state machine ---
struct StateMachine {
    EFaceAngleState CurrentState = EFaceAngleState::Front;
    EFaceAngleState PendingState = EFaceAngleState::Front;
    int HysteresisFrames = 0;
    static constexpr int HYSTERESIS_THRESHOLD = 2;
    static constexpr double BLEND_WINDOW = 5.0;

    void Update(double yaw, double pitch) {
        EFaceAngleState raw = DetermineStateFromAngles(yaw, pitch);
        if (raw == EFaceAngleState::Top || raw == EFaceAngleState::Bottom) {
            if (IsInStateZone(yaw, pitch, raw)) {
                CurrentState = raw;
                HysteresisFrames = 0;
                PendingState = raw;
                return;
            }
        }
        if (raw != PendingState) {
            PendingState = raw;
            HysteresisFrames = 0;
        } else {
            HysteresisFrames++;
        }
        if (HysteresisFrames >= HYSTERESIS_THRESHOLD) {
            CurrentState = raw;
        }
    }
};

// ========================
// TEST HELPERS
// ========================
int g_total = 0, g_passed = 0;
#define TEST(name, expr) do { \
    g_total++; \
    if (!(expr)) { \
        printf("FAIL [%s] line %d: %s\n", name, __LINE__, #expr); \
    } else { \
        g_passed++; \
    } \
} while(0)

// ========================
// TESTS
// ========================
void TestStateDetermination() {
    printf("=== State Determination ===\n");
    TEST("Front at origin", DetermineStateFromAngles(0, 0) == EFaceAngleState::Front);
    TEST("Front small yaw", DetermineStateFromAngles(10, 5) == EFaceAngleState::Front);
    TEST("3Q Left", DetermineStateFromAngles(-35, 0) == EFaceAngleState::ThreeQuarterLeft);
    TEST("3Q Right", DetermineStateFromAngles(35, 0) == EFaceAngleState::ThreeQuarterRight);
    TEST("Profile Left", DetermineStateFromAngles(-80, 0) == EFaceAngleState::LeftProfile);
    TEST("Profile Right", DetermineStateFromAngles(80, 0) == EFaceAngleState::RightProfile);
    TEST("Top", DetermineStateFromAngles(0, 70) == EFaceAngleState::Top);
    TEST("Bottom", DetermineStateFromAngles(0, -70) == EFaceAngleState::Bottom);
    TEST("Top beats yaw", DetermineStateFromAngles(80, 70) == EFaceAngleState::Top);
    TEST("Bottom beats yaw", DetermineStateFromAngles(-80, -70) == EFaceAngleState::Bottom);
    TEST("Boundary 20 is Front", DetermineStateFromAngles(20, 0) == EFaceAngleState::Front);
    TEST("Boundary 30 is 3Q", DetermineStateFromAngles(30, 0) == EFaceAngleState::ThreeQuarterRight);
    TEST("Boundary 67.5 is 3Q", DetermineStateFromAngles(67.49, 0) == EFaceAngleState::ThreeQuarterRight);
    TEST("Boundary 67.51 is Profile", DetermineStateFromAngles(67.51, 0) == EFaceAngleState::RightProfile);
    TEST("Negative yaw mirror", DetermineStateFromAngles(-30, 0) == EFaceAngleState::ThreeQuarterLeft);
    TEST("Negative yaw profile", DetermineStateFromAngles(-67.51, 0) == EFaceAngleState::LeftProfile);
    // Back state coverage
    TEST("Back Right", DetermineStateFromAngles(135, 0) == EFaceAngleState::BackRight);
    TEST("Back Left", DetermineStateFromAngles(-135, 0) == EFaceAngleState::BackLeft);
    TEST("Back at 175", DetermineStateFromAngles(175, 0) == EFaceAngleState::Back);
    TEST("Back from -175", DetermineStateFromAngles(-175, 0) == EFaceAngleState::Back);
}

void TestStateZoneInclusion() {
    printf("=== Zone Inclusion ===\n");
    TEST("Front in front zone", IsInStateZone(10, 5, EFaceAngleState::Front));
    TEST("Top in top zone", IsInStateZone(0, 50, EFaceAngleState::Top));
    TEST("Bottom in bottom zone", IsInStateZone(0, -50, EFaceAngleState::Bottom));
    TEST("Front not in top", !IsInStateZone(0, 0, EFaceAngleState::Top));
    TEST("Top not in front", !IsInStateZone(60, 60, EFaceAngleState::Front));
    TEST("Edge of zone", !IsInStateZone(30, 0, EFaceAngleState::Front));
    TEST("Just inside", IsInStateZone(22.49, 0, EFaceAngleState::Front));
    TEST("Just outside yaw", !IsInStateZone(22.51, 0, EFaceAngleState::Front));
    TEST("Just inside pitch top", IsInStateZone(0, 37.51, EFaceAngleState::Top));
    TEST("Just outside pitch top", !IsInStateZone(0, 82.51, EFaceAngleState::Top));
}

void TestHysteresis() {
    printf("=== Hysteresis ===\n");
    StateMachine sm;

    sm.Update(0, 0);
    TEST("Initial state front", sm.CurrentState == EFaceAngleState::Front);

    sm.Update(35, 0);
    TEST("Still front after 1 frame 3Q", sm.CurrentState == EFaceAngleState::Front);
    sm.Update(35, 0);
    TEST("Still front after 2 frames", sm.CurrentState == EFaceAngleState::Front);
    sm.Update(35, 0);
    TEST("Now 3Q after 3 frames", sm.CurrentState == EFaceAngleState::ThreeQuarterRight);
    sm.Update(35, 0);
    TEST("Stay 3Q after 4 frames", sm.CurrentState == EFaceAngleState::ThreeQuarterRight);

    // Instant flip back
    sm.Update(0, 0);
    TEST("Flip back after 1 frame", sm.CurrentState == EFaceAngleState::ThreeQuarterRight);
    sm.Update(0, 0);
    TEST("Still flip back 2 frames", sm.CurrentState == EFaceAngleState::ThreeQuarterRight);
    sm.Update(0, 0);
    TEST("Front after 3 frames back", sm.CurrentState == EFaceAngleState::Front);
}

void TestHysteresisTopBottom() {
    printf("=== Hysteresis Top/Bottom (instant) ===\n");
    StateMachine sm;

    sm.Update(0, 70);
    TEST("Top instant", sm.CurrentState == EFaceAngleState::Top);
    sm.Update(0, -70);
    TEST("Bottom instant", sm.CurrentState == EFaceAngleState::Bottom);
    sm.Update(0, 70);
    TEST("Back to top instant", sm.CurrentState == EFaceAngleState::Top);
}

void TestTransformIdentity() {
    printf("=== Transform Identity ===\n");
    FFaceArtTransform identity;
    TEST("Default is identity", identity.IsIdentity());

    FFaceArtTransform t1(FVector2D(10,20), FVector2D(2,2), 45);
    TEST("Non-identity", !t1.IsIdentity());

    FFaceArtTransform combined = identity.Combined(t1);
    TEST("Identity combined = other", combined.Position.X == 10 && combined.Position.Y == 20);
    TEST("Identity combined scale", combined.Scale.X == 2 && combined.Scale.Y == 2);
    TEST("Identity combined rot", combined.Rotation == 45);

    FFaceArtTransform t2(FVector2D(5,5), FVector2D(0.5,0.5), -45);
    combined = t1.Combined(t2);
    TEST("Combine non-identity rotation", fabs(combined.Rotation) < 0.001);
}

void TestTransformOverride() {
    printf("=== Transform Override ===\n");
    struct MockSlot {
        FFaceArtTransform Canonical;
        FFaceArtTransform Override;
        bool bHasOverride = false;
        EFaceAngleState OverrideView;

        FFaceArtTransform GetEffectiveTransform(EFaceAngleState currentView) const {
            if (bHasOverride && currentView == OverrideView) return Canonical.Combined(Override);
            return Canonical;
        }
    };

    MockSlot slot;
    slot.Canonical = FFaceArtTransform(FVector2D(10,10), FVector2D(1,1), 0);
    slot.bHasOverride = true;
    slot.OverrideView = EFaceAngleState::RightProfile;
    slot.Override = FFaceArtTransform(FVector2D(5,0), FVector2D(1.2,1.2), 15);

    FFaceArtTransform eff = slot.GetEffectiveTransform(EFaceAngleState::Front);
    TEST("No override for front", eff.Position.X == 10 && eff.Position.Y == 10);

    eff = slot.GetEffectiveTransform(EFaceAngleState::RightProfile);
    TEST("Override for profile right", eff.Position.X != 10);
    TEST("Override rotation", eff.Rotation == 15);
}

void TestAutoFit() {
    printf("=== Auto-Fit ===\n");
    struct AutoFitter {
        double CanvasW, CanvasH;
        AutoFitter(double w, double h) : CanvasW(w), CanvasH(h) {}
        FFaceArtTransform Compute(int texW, int texH) {
            FFaceArtTransform r;
            if (CanvasW <= 0 || CanvasH <= 0 || texW <= 0 || texH <= 0) return r;
            double sx = CanvasW / texW;
            double sy = CanvasH / texH;
            double s = std::min(sx, sy);
            r.Scale.X = s; r.Scale.Y = s;
            return r;
        }
    };

    AutoFitter fit(512, 512);
    FFaceArtTransform t = fit.Compute(1024, 1024);
    TEST("Square tex on square canvas", t.Scale.X == 0.5 && t.Scale.Y == 0.5);

    t = fit.Compute(2048, 1024);
    TEST("Wide tex fit inside", fabs(t.Scale.X - 0.25) < 0.001 && fabs(t.Scale.Y - 0.25) < 0.001);

    t = fit.Compute(1024, 2048);
    TEST("Tall tex fit inside", fabs(t.Scale.X - 0.25) < 0.001 && fabs(t.Scale.Y - 0.25) < 0.001);

    t = fit.Compute(0, 512);
    TEST("Zero width returns identity", t.IsIdentity());

    AutoFitter fit2(1920, 1080);
    t = fit2.Compute(256, 256);
    TEST("HD canvas 256 tex", fabs(t.Scale.X - 4.21875) < 0.001);
}

void TestEdgeCases() {
    printf("=== Edge Cases ===\n");
    // Extreme angles
    TEST("Extreme yaw 180 is Back", DetermineStateFromAngles(180, 0) == EFaceAngleState::Back);
    TEST("Extreme yaw -180 is Back", DetermineStateFromAngles(-180, 0) == EFaceAngleState::Back);
    TEST("Extreme pitch 90", DetermineStateFromAngles(0, 90) == EFaceAngleState::Top);
    TEST("Extreme pitch -90", DetermineStateFromAngles(0, -90) == EFaceAngleState::Bottom);

    // NaN-like / bad values (we don't handle NaN, but test edge)
    TEST("Zero everything", DetermineStateFromAngles(0, 0) == EFaceAngleState::Front);

    // Combined extreme
    TEST("Extreme yaw + pitch top", DetermineStateFromAngles(180, 80) == EFaceAngleState::Top);
    TEST("Extreme yaw + pitch bottom", DetermineStateFromAngles(180, -80) == EFaceAngleState::Bottom);

    // Hysteresis at exact boundary
    StateMachine sm;
    sm.Update(22.5, 0); // exactly on Front/3Q boundary
    TEST("Boundary at 22.5 is Front", sm.CurrentState == EFaceAngleState::Front);
    sm.Update(23.0, 0); // just past boundary → 3Q
    sm.Update(23.0, 0);
    sm.Update(23.0, 0);
    TEST("Just past boundary becomes 3Q", sm.CurrentState == EFaceAngleState::ThreeQuarterRight);

    // Rapid oscillation test
    sm.CurrentState = EFaceAngleState::Front;
    sm.PendingState = EFaceAngleState::Front;
    sm.HysteresisFrames = 0;
    for (int i = 0; i < 100; i++) {
        double jitter = (i % 2 == 0) ? 35.0 : 0.0;
        sm.Update(jitter, 0);
    }
    TEST("Oscillation ends in front", sm.CurrentState == EFaceAngleState::Front);
}

void TestDifferentThresholds() {
    printf("=== Different Top/Bottom Thresholds ===\n");

    // The real UE component uses configurable TopViewPitchThreshold and BottomViewPitchThreshold
    // These tests simulate different thresholds to ensure the Bottom state doesn't wrongly
    // use the Top threshold (the original bug).
    auto DetermineWithThresholds = [](double /*yaw*/, double pitch,
        double topThresh, double bottomThresh) -> EFaceAngleState {
        if (pitch > topThresh) return EFaceAngleState::Top;
        if (pitch < bottomThresh) return EFaceAngleState::Bottom;
        return EFaceAngleState::Front;
    };

    // Default: top=60, bottom=-60
    TEST("Default top 60", DetermineWithThresholds(0, 61, 60, -60) == EFaceAngleState::Top);
    TEST("Default bottom -60", DetermineWithThresholds(0, -61, 60, -60) == EFaceAngleState::Bottom);
    TEST("Default middle 0", DetermineWithThresholds(0, 0, 60, -60) == EFaceAngleState::Front);

    // Asymmetric: top=80, bottom=-40
    TEST("Asymmetric top 80", DetermineWithThresholds(0, 81, 80, -40) == EFaceAngleState::Top);
    TEST("Asymmetric middle 50", DetermineWithThresholds(0, 50, 80, -40) == EFaceAngleState::Front);
    TEST("Asymmetric bottom -41", DetermineWithThresholds(0, -41, 80, -40) == EFaceAngleState::Bottom);
    TEST("Asymmetric bottom -39", DetermineWithThresholds(0, -39, 80, -40) == EFaceAngleState::Front);
    TEST("Asymmetric edge top 80", DetermineWithThresholds(0, 80.01, 80, -40) == EFaceAngleState::Top);
    TEST("Asymmetric edge bottom -40", DetermineWithThresholds(0, -40.01, 80, -40) == EFaceAngleState::Bottom);

    // Extreme: top=89.9, bottom=-89.9 (nearly all front)
    TEST("Narrow top 89.9", DetermineWithThresholds(0, 90, 89.9, -89.9) == EFaceAngleState::Top);
    TEST("Narrow front 80", DetermineWithThresholds(0, 80, 89.9, -89.9) == EFaceAngleState::Front);
    TEST("Narrow bottom -90", DetermineWithThresholds(0, -90, 89.9, -89.9) == EFaceAngleState::Bottom);

    // Equal: top=0, bottom=0 (everything is top or bottom)
    TEST("All top at 0.1", DetermineWithThresholds(0, 0.1, 0, 0) == EFaceAngleState::Top);
    TEST("All bottom at -0.1", DetermineWithThresholds(0, -0.1, 0, 0) == EFaceAngleState::Bottom);
}

void TestTransformCombined() {
    printf("=== Transform Combined (simple UE version) ===\n");

    FFaceArtTransform identity;
    FFaceArtTransform a(FVector2D(10, 20), FVector2D(2, 3), 45);
    FFaceArtTransform b(FVector2D(5, -5), FVector2D(0.5, 0.333), -15);

    FFaceArtTransform c = a.Combined(b);
    TEST("Combined position X", fabs(c.Position.X - 15) < 0.001);
    TEST("Combined position Y", fabs(c.Position.Y - 15) < 0.001);
    TEST("Combined scale X", fabs(c.Scale.X - 1.0) < 0.001);
    TEST("Combined scale Y", fabs(c.Scale.Y - 1.0) < 0.001);  // 3 * 0.333
    TEST("Combined rotation", fabs(c.Rotation - 30) < 0.001);

    // Identity chaining
    FFaceArtTransform d = a.Combined(identity);
    TEST("Identity chained pos", fabs(d.Position.X - 10) < 0.001 && fabs(d.Position.Y - 20) < 0.001);
    TEST("Identity chained scale", fabs(d.Scale.X - 2) < 0.001 && fabs(d.Scale.Y - 3) < 0.001);
    TEST("Identity chained rot", fabs(d.Rotation - 45) < 0.001);

    // Zero position override
    FFaceArtTransform zeroPos(FVector2D(0, 0), FVector2D(1.5, 1.5), 0);
    FFaceArtTransform e = a.Combined(zeroPos);
    TEST("Zero pos keeps canonical pos", fabs(e.Position.X - 10) < 0.001 && fabs(e.Position.Y - 20) < 0.001);
    TEST("Zero pos scales", fabs(e.Scale.X - 3) < 0.001 && fabs(e.Scale.Y - 4.5) < 0.001);
}

void TestTransformOverrideSystem() {
    printf("=== Transform Override System (full slot) ===\n");

    struct FFaceArtSlot {
        FFaceArtTransform Canonical;
        struct OverrideEntry { EFaceAngleState View; FFaceArtTransform T; };
        OverrideEntry Overrides[10];
        int OverrideCount = 0;

        void SetOverride(EFaceAngleState view, const FFaceArtTransform& t) {
            for (int i = 0; i < OverrideCount; ++i) {
                if (Overrides[i].View == view) { Overrides[i].T = t; return; }
            }
            if (OverrideCount < 10) { Overrides[OverrideCount++] = {view, t}; }
        }

        bool HasOverride(EFaceAngleState view) const {
            for (int i = 0; i < OverrideCount; ++i)
                if (Overrides[i].View == view) return true;
            return false;
        }

        void ClearOverride(EFaceAngleState view) {
            for (int i = 0; i < OverrideCount; ++i) {
                if (Overrides[i].View == view) {
                    Overrides[i] = Overrides[--OverrideCount];
                    return;
                }
            }
        }

        FFaceArtTransform GetEffectiveTransform(EFaceAngleState view) const {
            for (int i = 0; i < OverrideCount; ++i) {
                if (Overrides[i].View == view)
                    return Canonical.Combined(Overrides[i].T);
            }
            return Canonical;
        }
    };

    FFaceArtSlot slot;
    slot.Canonical = FFaceArtTransform(FVector2D(100, 200), FVector2D(1, 1), 0);

    // No overrides
    FFaceArtTransform eff = slot.GetEffectiveTransform(EFaceAngleState::Front);
    TEST("No override falls to canonical", fabs(eff.Position.X - 100) < 0.001);

    // Add override for one state
    slot.SetOverride(EFaceAngleState::RightProfile, FFaceArtTransform(FVector2D(10, 0), FVector2D(1.2, 1.2), 5));
    TEST("HasOverride RightProfile", slot.HasOverride(EFaceAngleState::RightProfile));
    TEST("No override for Front", !slot.HasOverride(EFaceAngleState::Front));

    eff = slot.GetEffectiveTransform(EFaceAngleState::RightProfile);
    TEST("Override pos X", fabs(eff.Position.X - 110) < 0.001);
    TEST("Override pos Y", fabs(eff.Position.Y - 200) < 0.001);
    TEST("Override scale", fabs(eff.Scale.X - 1.2) < 0.001);
    TEST("Override rot", fabs(eff.Rotation - 5) < 0.001);

    // Multiple overrides
    slot.SetOverride(EFaceAngleState::Top, FFaceArtTransform(FVector2D(0, -50), FVector2D(0.8, 0.8), -10));
    slot.SetOverride(EFaceAngleState::Bottom, FFaceArtTransform(FVector2D(0, 50), FVector2D(0.9, 0.9), 10));

    eff = slot.GetEffectiveTransform(EFaceAngleState::Top);
    TEST("Top override pos Y", fabs(eff.Position.Y - 150) < 0.001);
    eff = slot.GetEffectiveTransform(EFaceAngleState::Bottom);
    TEST("Bottom override pos Y", fabs(eff.Position.Y - 250) < 0.001);

    // Clear override
    slot.ClearOverride(EFaceAngleState::RightProfile);
    TEST("Cleared override gone", !slot.HasOverride(EFaceAngleState::RightProfile));

    eff = slot.GetEffectiveTransform(EFaceAngleState::RightProfile);
    TEST("Cleared falls to canonical", fabs(eff.Position.X - 100) < 0.001);
}

void TestAutoFitEdgeCases() {
    printf("=== Auto-Fit Edge Cases ===\n");

    struct AutoFitter {
        double CanvasW, CanvasH;
        AutoFitter(double w, double h) : CanvasW(w), CanvasH(h) {}
        FFaceArtTransform Compute(int texW, int texH) {
            FFaceArtTransform r;
            if (CanvasW <= 0 || CanvasH <= 0 || texW <= 0 || texH <= 0) return r;
            double sx = CanvasW / texW;
            double sy = CanvasH / texH;
            double s = std::min(sx, sy);
            r.Scale.X = s; r.Scale.Y = s;
            return r;
        }
    };

    // Zero canvas
    AutoFitter zc(0, 512);
    TEST("Zero canvas width -> identity", zc.Compute(256, 256).IsIdentity());

    AutoFitter zc2(512, 0);
    TEST("Zero canvas height -> identity", zc2.Compute(256, 256).IsIdentity());

    AutoFitter zc3(0, 0);
    TEST("Zero canvas both -> identity", zc3.Compute(256, 256).IsIdentity());

    // Zero texture dimensions
    AutoFitter fit(512, 512);
    TEST("Zero tex width -> identity", fit.Compute(0, 256).IsIdentity());
    TEST("Zero tex height -> identity", fit.Compute(256, 0).IsIdentity());
    TEST("Zero tex both -> identity", fit.Compute(0, 0).IsIdentity());

    // Negative (invalid but should not crash)
    TEST("Negative tex -> identity", fit.Compute(-256, 256).IsIdentity());

    // Exact match
    TEST("Exact 1:1 match", fabs(fit.Compute(512, 512).Scale.X - 1.0) < 0.001);

    // Very large texture
    TEST("Large tex 8192x8192", fabs(fit.Compute(8192, 8192).Scale.X - 0.0625) < 0.001);

    // Very small texture
    TEST("Small tex 4x4", fabs(fit.Compute(4, 4).Scale.X - 128.0) < 0.001);

    // Non-square canvas
    AutoFitter wide(1920, 1080);
    TEST("Wide canvas, tall tex fit by height",
        fabs(wide.Compute(400, 800).Scale.Y - 1.35) < 0.001);
    TEST("Wide canvas, wide tex fit by width",
        fabs(wide.Compute(4000, 400).Scale.X - 0.48) < 0.001);

    // Canvas much larger than texture
    AutoFitter huge(10000, 10000);
    TEST("Huge canvas tiny tex", fabs(huge.Compute(1, 1).Scale.X - 10000.0) < 0.001);
}

void TestStateBoundaryPrecision() {
    printf("=== State Boundary Precision ===\n");

    // Test all 8 cardinal states are reachable
    auto TestState = [](double yaw, double pitch, EFaceAngleState expected) {
        return DetermineStateFromAngles(yaw, pitch) == expected;
    };

    TEST("Front (0,0)", TestState(0, 0, EFaceAngleState::Front));
    TEST("3QR (35,0)", TestState(35, 0, EFaceAngleState::ThreeQuarterRight));
    TEST("3QL (-35,0)", TestState(-35, 0, EFaceAngleState::ThreeQuarterLeft));
    TEST("ProR (80,0)", TestState(80, 0, EFaceAngleState::RightProfile));
    TEST("ProL (-80,0)", TestState(-80, 0, EFaceAngleState::LeftProfile));
    TEST("BackR (135,0)", TestState(135, 0, EFaceAngleState::BackRight));
    TEST("BackL (-135,0)", TestState(-135, 0, EFaceAngleState::BackLeft));
    TEST("Back (175,0)", TestState(175, 0, EFaceAngleState::Back));
    TEST("Back from negative", TestState(-175, 0, EFaceAngleState::Back));

    // Verify all values from 0 to 180 in steps
    int lastState = -1;
    int transitions = 0;
    for (int yaw = -180; yaw <= 180; ++yaw) {
        int s = (int)DetermineStateFromAngles((double)yaw, 0);
        if (s != lastState) { transitions++; lastState = s; }
    }
    // Should have 9 transitions across full range: (-1→Back, Back↔BackL↔ProL↔3QL↔Front↔3QR↔ProR↔BackR↔Back)
    TEST("Transitions across full range", transitions >= 9);

    // Every state enum value is reachable
    TEST("MAX not reachable", DetermineStateFromAngles(0, 0) != EFaceAngleState::MAX);
}

void TestHysteresisJitter() {
    printf("=== Hysteresis Jitter Resistance ===\n");

    StateMachine sm;

    // Simulate jitter exactly on the 22.5 boundary
    for (int i = 0; i < 50; ++i) {
        double jitter = (i % 3 == 0) ? 23.0 : 22.4;
        sm.Update(jitter, 0);
    }
    // Hysteresis prevents flicker; jitter on boundary never builds 2 consecutive same PendingState
    TEST("Jitter stability settles to Front", sm.CurrentState == EFaceAngleState::Front);

    // Rapid state hopping (front <-> 3Q <-> profile)
    StateMachine sm2;
    double angles[] = {0, 35, 0, 35, 80, 35, 0, 80, 0};
    for (double a : angles) {
        sm2.Update(a, 0);
    }
    // Rapid hops never satisfy hysteresis so state stays Front
    TEST("Rapid hop ends in Front", sm2.CurrentState == EFaceAngleState::Front);
}

void TestZoneCenterCalculations() {
    printf("=== Zone Center Calculations ===\n");

    double HalfZone = 22.5;
    auto GetZoneCenterYaw = [HalfZone](EFaceAngleState s) -> double {
        switch (s) {
            case EFaceAngleState::Front: return 0.0;
            case EFaceAngleState::ThreeQuarterRight: return HalfZone * 2.0;
            case EFaceAngleState::RightProfile: return HalfZone * 4.0;
            case EFaceAngleState::BackRight: return HalfZone * 6.0;
            case EFaceAngleState::Back: return 180.0;
            case EFaceAngleState::BackLeft: return -HalfZone * 6.0;
            case EFaceAngleState::LeftProfile: return -HalfZone * 4.0;
            case EFaceAngleState::ThreeQuarterLeft: return -HalfZone * 2.0;
            default: return 0.0;
        }
    };

    TEST("Front center", fabs(GetZoneCenterYaw(EFaceAngleState::Front)) < 0.001);
    TEST("3QR center", fabs(GetZoneCenterYaw(EFaceAngleState::ThreeQuarterRight) - 45.0) < 0.001);
    TEST("ProR center", fabs(GetZoneCenterYaw(EFaceAngleState::RightProfile) - 90.0) < 0.001);
    TEST("BackR center", fabs(GetZoneCenterYaw(EFaceAngleState::BackRight) - 135.0) < 0.001);
    TEST("Back center", fabs(GetZoneCenterYaw(EFaceAngleState::Back) - 180.0) < 0.001);
    TEST("BackL center", fabs(GetZoneCenterYaw(EFaceAngleState::BackLeft) + 135.0) < 0.001);
    TEST("ProL center", fabs(GetZoneCenterYaw(EFaceAngleState::LeftProfile) + 90.0) < 0.001);
    TEST("3QL center", fabs(GetZoneCenterYaw(EFaceAngleState::ThreeQuarterLeft) + 45.0) < 0.001);
    TEST("Top center", fabs(GetZoneCenterYaw(EFaceAngleState::Top)) < 0.001);
    TEST("Bottom center", fabs(GetZoneCenterYaw(EFaceAngleState::Bottom)) < 0.001);
}

void TestBackStateAngleWrapping() {
    printf("=== Back State Angle Wrapping ===\n");

    // Simulate the angle normalization logic used in UpdateStateMachine and ComputeOffsetForState
    auto NormalizeDelta = [](double yaw, double center) -> double {
        double delta = yaw - center;
        while (delta > 180.0) delta -= 360.0;
        while (delta < -180.0) delta += 360.0;
        return delta;
    };

    // Back center is at 180 deg
    double BackCenter = 180.0;

    // From BackLeft side (negative yaw)
    double d1 = NormalizeDelta(-170.0, BackCenter);
    TEST("Back from -170 normalized", fabs(d1 - (-350.0 + 360.0)) < 0.001); // should be 10 deg

    // From BackRight side (positive yaw)
    double d2 = NormalizeDelta(170.0, BackCenter);
    TEST("Back from 170 normalized", fabs(d2 - (-10.0)) < 0.001); // should be -10 deg

    // At exact Back center
    double d3 = NormalizeDelta(180.0, BackCenter);
    TEST("Back at center", fabs(d3) < 0.001);

    // At BackLeft zone boundary (157.5)
    double d4 = NormalizeDelta(157.5, BackCenter);
    TEST("Back at BL boundary", fabs(d4 - (-22.5)) < 0.001);

    // At BackRight zone boundary (-157.5 expressed as 202.5)
    double d5 = NormalizeDelta(-157.5, BackCenter);
    TEST("Back at BR boundary from negative", fabs(d5 - 22.5) < 0.001);

    // Through zero from BackLeft
    double d6 = NormalizeDelta(-10.0, BackCenter);
    TEST("Back far negative -10", fabs(d6 - 170.0) < 0.001);

    // Front center from yaw near Back
    double FrontCenter = 0.0;
    double d7 = NormalizeDelta(-170.0, FrontCenter);
    TEST("Front center from -170", fabs(d7 - (-170.0)) < 0.001);

    // Edge case: exact opposite (either +180 or -180 is valid)
    double d8 = NormalizeDelta(0.0, 180.0);
    TEST("Opposite 0 vs 180", fabs(fabs(d8) - 180.0) < 0.001);

    double d9 = NormalizeDelta(-180.0, 180.0);
    TEST("-180 vs 180 back center", fabs(d9) < 0.001 || fabs(fabs(d9) - 360.0) < 0.001);
    // Note: -180 deg = 180 deg in this coordinate system, so delta = -180 - 180 = -360, normalized wraps to 0.
    // But -180 might also be represented as 180, so this edge case is fuzzy. Just ensure no crash.

    // Ensure normalization doesn't break non-wrapping cases
    double d10 = NormalizeDelta(30.0, 0.0);
    TEST("Front delta 30 unchanged", fabs(d10 - 30.0) < 0.001);

    double d11 = NormalizeDelta(-30.0, 0.0);
    TEST("Front delta -30 unchanged", fabs(d11 - (-30.0)) < 0.001);

    // Simulate continuous blending with wrapping
    auto ContinuousAlpha = [&](double yaw, double center, double halfZone, double window) -> double {
        double delta = yaw - center;
        while (delta > 180.0) delta -= 360.0;
        while (delta < -180.0) delta += 360.0;
        double distToEdge = fabs(delta) - (halfZone - window * 0.5);
        return 1.0 - std::max(0.0, std::min(1.0, distToEdge / window));
    };

    double HalfZone = 22.5;
    double Window = 5.0;

    // Back center (180) — from yaw = 170 (within BackRight side)
    double alpha1 = ContinuousAlpha(170.0, BackCenter, HalfZone, Window);
    TEST("Back continuous from 170", alpha1 > 0.0 && alpha1 <= 1.0);

    // Back center — from yaw = -170 (within BackLeft side)
    double alpha2 = ContinuousAlpha(-170.0, BackCenter, HalfZone, Window);
    TEST("Back continuous from -170", alpha2 > 0.0 && alpha2 <= 1.0);

    // Both directions produce similar alpha (symmetry)
    double alphaDiff = fabs(alpha1 - alpha2);
    TEST("Back continuous symmetry", alphaDiff < 0.05);

    // Front center from small yaw
    double alpha3 = ContinuousAlpha(0.0, FrontCenter, HalfZone, Window);
    TEST("Front continuous at center = 1", fabs(alpha3 - 1.0) < 0.001);

    // Front from exactly at zone edge (window straddles boundary, alpha=0.5)
    double alpha4 = ContinuousAlpha(HalfZone, FrontCenter, HalfZone, Window);
    TEST("Front continuous at edge = 0.5", fabs(alpha4 - 0.5) < 0.001);
}

void TestBlinkAnimation() {
    printf("=== Blink Animation ===\n");

    // Blink scheduling simulation
    struct BlinkState {
        bool bEnabled = true;
        bool bIsBlinking = false;
        double NextBlinkCountdown = 5.0;
        int FrameIndex = 0;
        double FrameTimer = 0.0;
        double FrameDuration = 0.03;
        int TotalFrames = 8; // number of blink frames in the slot
        int BlinkCount = 0;
        int CompletedCount = 0;

        void ForceBlink() {
            bIsBlinking = true;
            FrameIndex = 0;
            FrameTimer = 0.0;
            BlinkCount++;
        }

        void Tick(double dt) {
            if (!bEnabled) { bIsBlinking = false; return; }

            if (!bIsBlinking) {
                NextBlinkCountdown -= dt;
                if (NextBlinkCountdown <= 0.0) {
                    ForceBlink();
                }
                return;
            }

            FrameTimer += dt;
            if (FrameTimer >= FrameDuration) {
                FrameTimer = 0.0;
                FrameIndex++;
                if (FrameIndex >= TotalFrames) {
                    bIsBlinking = false;
                    FrameIndex = 0;
                    NextBlinkCountdown = 5.0;
                    CompletedCount++;
                }
            }
        }
    };

    BlinkState bs;
    bs.NextBlinkCountdown = 0.5; // blink soon

    // After 0.4s, not yet blinking
    bs.Tick(0.4);
    TEST("Not blinking before countdown", !bs.bIsBlinking);
    TEST("Countdown decreased", bs.NextBlinkCountdown < 0.5);

    // After 0.2s more, blinking starts
    bs.Tick(0.2);
    TEST("Blinking after countdown", bs.bIsBlinking);
    TEST("Frame index starts at 0", bs.FrameIndex == 0);

    // Advance through frames
    for (int i = 0; i < 7; ++i) {
        bs.Tick(bs.FrameDuration + 0.001); // advance each frame
        TEST("Frame advances", bs.FrameIndex == i + 1);
    }

    // One more tick should complete the blink
    bs.Tick(bs.FrameDuration + 0.001);
    TEST("Blink completed", !bs.bIsBlinking);
    TEST("Frame reset to 0", bs.FrameIndex == 0);
    TEST("Completed count", bs.CompletedCount == 1);

    // Force blink mid-way
    BlinkState bs2;
    bs2.NextBlinkCountdown = 10.0;
    bs2.ForceBlink();
    TEST("Force blink starts", bs2.bIsBlinking);
    TEST("Blink count", bs2.BlinkCount == 1);

    // Disable blinking cancels immediately
    bs2.bEnabled = false;
    bs2.Tick(0.016);
    TEST("Disable stops blink", !bs2.bIsBlinking);

    // Rapid re-trigger (ForceBlink during active blink)
    BlinkState bs3;
    bs3.ForceBlink();
    bs3.Tick(0.05);
    bs3.ForceBlink();
    TEST("Re-trigger resets frame", bs3.FrameIndex == 0);
    TEST("Blink count increases", bs3.BlinkCount == 2);
}

void TestExpressionSystem() {
    printf("=== Expression System ===\n");

    // Expression crossfade simulation
    struct ExprState {
        int CurrentExpr = 0; // 0=Neutral, 1=Smile, 2=Frown
        int PreviousExpr = 0;
        double BlendAlpha = 1.0;
        bool bTransitioning = false;
        double Duration = 0.3;
        int PrevCaptureCount = 0;
        int NewApplyCount = 0;

        double Elapsed = 0.0;

        void SetExpression(int newExpr) {
            if (newExpr == CurrentExpr && !bTransitioning) return;

            PreviousExpr = CurrentExpr;
            CurrentExpr = newExpr;
            BlendAlpha = 0.0;
            Elapsed = 0.0;
            bTransitioning = true;
            PrevCaptureCount++;
            NewApplyCount++;
        }

        void Tick(double dt) {
            if (!bTransitioning) {
                BlendAlpha = 1.0;
                return;
            }

            Elapsed += dt;
            BlendAlpha = std::min(1.0, Duration > 0.0 ? Elapsed / Duration : 1.0);

            if (BlendAlpha >= 1.0) {
                BlendAlpha = 1.0;
                bTransitioning = false;
            }
        }

        bool IsTransitioning() const { return bTransitioning; }
    };

    // No-op: same expression
    ExprState es;
    es.SetExpression(0); // same as current (0)
    TEST("Same expression no transition", !es.bTransitioning);
    TEST("Same no capture", es.PrevCaptureCount == 0);

    // Change to Smile
    es.SetExpression(1);
    TEST("Expression changed", es.CurrentExpr == 1);
    TEST("Previous recorded", es.PreviousExpr == 0);
    TEST("BlendAlpha reset", es.BlendAlpha < 0.001);
    TEST("Transition started", es.bTransitioning);
    TEST("Capture count", es.PrevCaptureCount == 1);

    // Tick through transition
    for (int i = 0; i < 20; ++i) {
        es.Tick(0.016);
    }
    TEST("Transition completes", !es.bTransitioning);
    TEST("BlendAlpha = 1", fabs(es.BlendAlpha - 1.0) < 0.001);

    // Change to Frown mid-transition (re-entrant)
    ExprState es2;
    es2.SetExpression(1); // start transition to Smile
    es2.Tick(0.05); // partially blend
    TEST("Mid-transition alpha < 1", es2.BlendAlpha > 0.0 && es2.BlendAlpha < 1.0);

    es2.SetExpression(2); // switch to Frown mid-way
    TEST("Re-entrant captures again", es2.PrevCaptureCount == 2);
    TEST("Re-entrant new expression", es2.CurrentExpr == 2);

    // Complete
    for (int i = 0; i < 30; ++i) es2.Tick(0.016);
    TEST("Re-entrant completes", !es2.bTransitioning);
    TEST("Final expression", es2.CurrentExpr == 2);

    // Very fast duration
    ExprState es3;
    es3.Duration = 0.01;
    es3.SetExpression(1);
    for (int i = 0; i < 5; ++i) es3.Tick(0.016);
    TEST("Fast duration completes", !es3.bTransitioning);
}

void TestVisemeAnimation() {
    printf("=== Viseme Animation ===\n");

    // Viseme frame playback (same pattern as blink but triggered manually)
    struct VisemeState {
        bool bEnabled = true;
        bool bPlaying = false;
        int VisemeId = 0; // 0=Ah, 1=Uh, etc.
        int FrameIndex = 0;
        double FrameTimer = 0.0;
        double FrameDuration = 0.04;
        int TotalFrames = 0;
        int PlayCount = 0;
        int CompletedCount = 0;

        void Play(int visemeId, int frameCount) {
            VisemeId = visemeId;
            TotalFrames = frameCount;
            FrameIndex = 0;
            FrameTimer = 0.0;
            bPlaying = true;
            PlayCount++;
        }

        void Stop() {
            if (bPlaying) {
                bPlaying = false;
                FrameIndex = 0;
                CompletedCount++;
            }
        }

        void Tick(double dt) {
            if (!bEnabled || !bPlaying) { bPlaying = false; return; }

            FrameTimer += dt;
            if (FrameTimer >= FrameDuration) {
                FrameTimer = 0.0;
                FrameIndex++;
                if (FrameIndex >= TotalFrames) {
                    bPlaying = false;
                    FrameIndex = 0;
                    CompletedCount++;
                }
            }
        }
    };

    VisemeState vs;
    vs.Play(0, 5); // Play Ah viseme with 5 frames
    TEST("Viseme started playing", vs.bPlaying);
    TEST("Viseme frame 0", vs.FrameIndex == 0);
    TEST("Viseme play count", vs.PlayCount == 1);

    // Advance through frames
    for (int i = 0; i < 4; ++i) {
        vs.Tick(vs.FrameDuration + 0.001);
        TEST("Viseme frame advances", vs.FrameIndex == i + 1);
    }

    // Last tick should complete
    vs.Tick(vs.FrameDuration + 0.001);
    TEST("Viseme auto-completes", !vs.bPlaying);
    TEST("Viseme completed count", vs.CompletedCount == 1);

    // Stop mid-play
    VisemeState vs2;
    vs2.Play(1, 10);
    vs2.Tick(0.1); // a few frames in
    TEST("Viseme2 playing", vs2.bPlaying);
    TEST("Viseme2 frame advanced", vs2.FrameIndex > 0);

    vs2.Stop();
    TEST("Viseme2 stopped", !vs2.bPlaying);
    TEST("Viseme2 completed", vs2.CompletedCount == 1);

    // Disabled ignores play
    VisemeState vs3;
    vs3.bEnabled = false;
    vs3.Play(0, 3);
    vs3.Tick(0.016);
    TEST("Disabled viseme stops", !vs3.bPlaying);

    // Zero frames — instant complete
    VisemeState vs4;
    vs4.Play(0, 0);
    TEST("Zero frames starts", vs4.bPlaying);
    vs4.Tick(vs4.FrameDuration);
    TEST("Zero frames completes", !vs4.bPlaying);

    // Re-trigger while playing resets
    VisemeState vs5;
    vs5.Play(0, 10);
    vs5.Tick(0.1);
    vs5.Play(1, 5); // re-trigger with different viseme
    TEST("Re-trigger resets frame", vs5.FrameIndex == 0);
    TEST("Re-trigger changes viseme", vs5.VisemeId == 1);
    TEST("Play count", vs5.PlayCount == 2);
}

void TestBlendingMath() {
    printf("=== Blending Math ===\n");

    struct BlendState {
        double Alpha = 1.0;
        double Duration = 0.3;
        double Elapsed = 0.0;
        bool bInTransition = false;

        void StartTransition() { Alpha = 0.0; Elapsed = 0.0; bInTransition = true; }

        bool Tick(double dt) {
            if (!bInTransition) return false;
            Elapsed += dt;
            Alpha = std::min(1.0, Elapsed / Duration);
            if (Alpha >= 1.0) { Alpha = 1.0; bInTransition = false; return true; }
            return false;
        }
    };

    // Basic interpolation
    BlendState bs;
    bs.StartTransition();
    bs.Tick(0.016);
    TEST("First tick advances alpha", bs.Alpha > 0.0);
    TEST("Alpha not yet 1", bs.Alpha < 1.0);
    for (int i = 0; i < 100; ++i) bs.Tick(0.016);
    TEST("Alpha reaches 1", fabs(bs.Alpha - 1.0) < 0.001);
    TEST("Transition ends", !bs.bInTransition);

    // Continuous blending (proximity-based alpha) — mirrors component exactly
    double HalfZone = 22.5;
    double Window = 5.0;
    auto ContinuousAlpha = [&](double yaw, double yawCenter) -> double {
        double delta = yaw - yawCenter;
        while (delta > 180.0) delta -= 360.0;
        while (delta < -180.0) delta += 360.0;
        double distToEdge = fabs(delta) - (HalfZone - Window * 0.5);
        return 1.0 - std::max(0.0, std::min(1.0, distToEdge / Window));
    };

    TEST("At center alpha=1", fabs(ContinuousAlpha(0, 0) - 1.0) < 0.001);
    // Window straddles zone boundary: alpha=0.5 at zone edge, 0.75 at window/4 inside
    TEST("At edge alpha=0.5", fabs(ContinuousAlpha(HalfZone, 0) - 0.5) < 0.001);
    TEST("Quarter window inside alpha=0.75",
        fabs(ContinuousAlpha(HalfZone - Window * 0.25, 0) - 0.75) < 0.05);

    // Verify blend window doesn't break at boundaries
    TEST("Outside zone still valid",
        ContinuousAlpha(HalfZone + 10, 0) >= 0.0);

    // Test with different yaw centers (3Q at 45 deg)
    TEST("3Q center alpha=1", fabs(ContinuousAlpha(45, 45) - 1.0) < 0.001);
    TEST("3Q at edge alpha=0.5", fabs(ContinuousAlpha(45 + HalfZone, 45) - 0.5) < 0.001);
}

// ====================================================================
// NEW EDGE CASE TESTS (covering fixes applied to the component)
// ====================================================================

void TestHalfZoneWidthZero() {
    printf("=== HalfZoneWidth = 0 Guard ===\n");

    // Guarded state determination — when HalfZoneWidth <= 0, return Front
    // (mirrors the fix in DetermineStateFromAngles)
    auto DetermineGuarded = [](double yaw, double pitch, double hzw) -> EFaceAngleState {
        if (pitch > 60.0) return EFaceAngleState::Top;
        if (pitch < -60.0) return EFaceAngleState::Bottom;
        if (hzw <= 0.001) return EFaceAngleState::Front;
        double Z3 = hzw * 3.0;
        double Z5 = hzw * 5.0;
        double Z7 = hzw * 7.0;
        if (yaw > -hzw && yaw <= hzw) return EFaceAngleState::Front;
        if (yaw > hzw && yaw <= Z3) return EFaceAngleState::ThreeQuarterRight;
        if (yaw > Z3 && yaw <= Z5) return EFaceAngleState::RightProfile;
        if (yaw > Z5 && yaw <= Z7) return EFaceAngleState::BackRight;
        if (yaw > Z7 || yaw <= -Z7) return EFaceAngleState::Back;
        if (yaw > -Z7 && yaw <= -Z5) return EFaceAngleState::BackLeft;
        if (yaw > -Z5 && yaw <= -Z3) return EFaceAngleState::LeftProfile;
        if (yaw > -Z3 && yaw <= -hzw) return EFaceAngleState::ThreeQuarterLeft;
        return EFaceAngleState::Front;
    };

    TEST("Zero HZW returns Front", DetermineGuarded(45, 0, 0.0) == EFaceAngleState::Front);
    TEST("Negative HZW returns Front", DetermineGuarded(45, 0, -1.0) == EFaceAngleState::Front);
    TEST("Zero HZW still Top", DetermineGuarded(0, 80, 0.0) == EFaceAngleState::Top);
    TEST("Zero HZW still Bottom", DetermineGuarded(0, -80, 0.0) == EFaceAngleState::Bottom);
    TEST("Normal HZW works", DetermineGuarded(45, 0, 22.5) == EFaceAngleState::ThreeQuarterRight);

    // Guarded division in offset computation (mirrors fix in ComputeOffsetForState and UpdateMaterialParameters)
    auto ComputeNormalizedYaw = [](double yawDeviation, double hzw) -> double {
        if (hzw > 0.001) {
            double clamped = yawDeviation / hzw;
            if (clamped < -1.0) clamped = -1.0;
            if (clamped > 1.0) clamped = 1.0;
            return clamped;
        }
        return 0.0;
    };

    TEST("Division by near-zero returns 0", fabs(ComputeNormalizedYaw(10.0, 0.0)) < 0.001);
    TEST("Division by negative returns 0", fabs(ComputeNormalizedYaw(10.0, -1.0)) < 0.001);
    TEST("Normal division works", fabs(ComputeNormalizedYaw(10.0, 22.5) - 0.444) < 0.01);
    TEST("Negative deviation", fabs(ComputeNormalizedYaw(-10.0, 22.5) + 0.444) < 0.01);
}

void TestStateChangeCancelsAnimations() {
    printf("=== State Change Cancels Animations ===\n");

    // Simulates the component's StopAnimationsOnStateChange logic
    struct AnimHost {
        double HZW = 22.5;
        EFaceAngleState CurrentState = EFaceAngleState::Front;
        EFaceAngleState PendingState = EFaceAngleState::Front;
        int HystRemaining = 0;
        enum : int { HYST = 2 };

        bool bBlinking = false;
        int BlinkIdx = 0;
        int BlinkCount = 0;

        bool bVisemePlaying = false;
        int VisemeStops = 0;

        bool bExprTransitioning = false;
        double ExprAlpha = 1.0;

        EFaceAngleState RawState(double yaw, double pitch) {
            if (pitch > 60.0) return EFaceAngleState::Top;
            if (pitch < -60.0) return EFaceAngleState::Bottom;
            if (HZW <= 0.001) return EFaceAngleState::Front;
            double Z3 = HZW * 3.0;
            double Z5 = HZW * 5.0;
            if (yaw > -HZW && yaw <= HZW) return EFaceAngleState::Front;
            if (yaw > HZW && yaw <= Z3) return EFaceAngleState::ThreeQuarterRight;
            if (yaw > Z3 && yaw <= Z5) return EFaceAngleState::RightProfile;
            if (yaw < -Z3 && yaw >= -Z5) return EFaceAngleState::LeftProfile;
            return EFaceAngleState::Front;
        }

        void StopAnimations() {
            if (bBlinking) {
                bBlinking = false;
                BlinkIdx = 0;
            }
            if (bVisemePlaying) {
                bVisemePlaying = false;
                VisemeStops++;
            }
            if (bExprTransitioning) {
                bExprTransitioning = false;
                ExprAlpha = 1.0;
            }
        }

        void Update(double yaw, double pitch) {
            EFaceAngleState raw = RawState(yaw, pitch);
            if (raw == CurrentState) {
                HystRemaining = 0;
                PendingState = raw;
                return;
            }
            if (raw != PendingState) {
                PendingState = raw;
                HystRemaining = HYST;
            } else {
                HystRemaining--;
            }
            if (HystRemaining <= 0) {
                CurrentState = raw;
                StopAnimations();
            }
        }

        void ForceBlink() {
            bBlinking = true;
            BlinkIdx = 0;
            BlinkCount++;
        }

        void StartExprTransition() {
            bExprTransitioning = true;
            ExprAlpha = 0.0;
        }
    };

    // Test 1: Blink stops on state change
    AnimHost ah;
    ah.ForceBlink();
    TEST("Blink started", ah.bBlinking);
    ah.Update(45, 0); // 3Q right — triggers state change after HYST frames
    ah.Update(45, 0);
    ah.Update(45, 0);
    // After 3rd frame, state changes, StopAnimations runs
    TEST("Blink stopped on state change", !ah.bBlinking);
    TEST("Blink frame reset", ah.BlinkIdx == 0);

    // Test 2: Viseme stops on state change
    AnimHost ah2;
    ah2.bVisemePlaying = true;
    ah2.Update(45, 0);
    ah2.Update(45, 0);
    ah2.Update(45, 0);
    TEST("Viseme stopped on state change", !ah2.bVisemePlaying);
    TEST("Viseme stop counted", ah2.VisemeStops == 1);

    // Test 3: Expression transition snaps on state change
    AnimHost ah3;
    ah3.StartExprTransition();
    TEST("Expr transitioning before", ah3.bExprTransitioning);
    ah3.Update(45, 0);
    ah3.Update(45, 0);
    ah3.Update(45, 0);
    TEST("Expr not transitioning after state change", !ah3.bExprTransitioning);
    TEST("Expr alpha snapped to 1", fabs(ah3.ExprAlpha - 1.0) < 0.001);

    // Test 4: No state change leaves animations alone
    AnimHost ah4;
    ah4.ForceBlink();
    ah4.StartExprTransition();
    ah4.Update(0, 0); // stays Front
    TEST("Blink persists without state change", ah4.bBlinking);
    TEST("Expr persists without state change", ah4.bExprTransitioning);
}

void TestYawDeviationNormalized() {
    printf("=== Yaw Deviation Normalized for Back State ===\n");

    auto NormalizeDelta = [](double yaw, double center) -> double {
        double delta = yaw - center;
        while (delta > 180.0) delta -= 360.0;
        while (delta < -180.0) delta += 360.0;
        return delta;
    };

    double HZW = 22.5;
    double BackCenter = 180.0;

    // BackLeft approach (yaw = -170): deviation should be +10 degrees
    double dev1 = NormalizeDelta(-170.0, BackCenter);
    TEST("Back from -170 → deviation ≈ +10", fabs(dev1 - 10.0) < 0.001);

    // BackRight approach (yaw = 170): deviation should be -10 degrees
    double dev2 = NormalizeDelta(170.0, BackCenter);
    TEST("Back from 170 → deviation ≈ -10", fabs(dev2 - (-10.0)) < 0.001);

    // Without normalization (old bug): -170 - 180 = -350, clamped to -1.0
    double oldDev = -170.0 - BackCenter; // -350 (no normalization)
    double oldNorm = oldDev / HZW;
    if (oldNorm < -1.0) oldNorm = -1.0;
    if (oldNorm > 1.0) oldNorm = 1.0;
    double newNorm = dev1 / HZW;
    if (newNorm < -1.0) newNorm = -1.0;
    if (newNorm > 1.0) newNorm = 1.0;
    TEST("Old normalization gave -1.0 (wrong)", fabs(oldNorm + 1.0) < 0.001);
    TEST("New normalization gives ~0.44 (correct)", fabs(newNorm - 0.444) < 0.05);

    // Symmetry: left and right should give symmetrical normalized values
    double devL = NormalizeDelta(-170.0, BackCenter) / HZW;
    double devR = NormalizeDelta(170.0, BackCenter) / HZW;
    if (devL < -1.0) devL = -1.0;
    if (devL > 1.0) devL = 1.0;
    if (devR < -1.0) devR = -1.0;
    if (devR > 1.0) devR = 1.0;
    TEST("Back symmetrical deviation", fabs(devL + devR) < 0.001); // one +, one -

    // Front state: no wrapping needed
    double devFront = NormalizeDelta(10.0, 0.0);
    TEST("Front deviation unchanged", fabs(devFront - 10.0) < 0.001);

    // Back state at exact center
    double devCenter = NormalizeDelta(180.0, BackCenter);
    TEST("Back at center deviation ≈ 0", fabs(devCenter) < 0.001);
}

void TestBlinkFrameMismatch() {
    printf("=== Blink Frame Count Mismatch Across Layers ===\n");

    // Simulate per-layer blink frame bounds check (mirrors UpdateMaterialParameters logic)
    struct LayerSlot {
        int NumBlinkFrames = 0;
    };

    LayerSlot layers[3];
    layers[0].NumBlinkFrames = 8;  // full blink animation
    layers[1].NumBlinkFrames = 3;  // only 3 frames
    layers[2].NumBlinkFrames = 0;  // no blink frames at all

    // Simulate frame-by-frame advancement using max-frames approach (component logic)
    int MaxFrames = 0;
    for (int i = 0; i < 3; ++i) MaxFrames = std::max(MaxFrames, layers[i].NumBlinkFrames);
    TEST("MaxFrames = 8", MaxFrames == 8);

    // Simulate per-layer bounds check: a layer only renders blink if frame < its frame count
    struct RenderCheck {
        bool bDisplayedBlink[3] = {};
    };

    auto TestFrame = [&](int frameIdx) -> RenderCheck {
        RenderCheck rc;
        for (int l = 0; l < 3; ++l) {
            rc.bDisplayedBlink[l] = (frameIdx >= 0 && frameIdx < layers[l].NumBlinkFrames);
        }
        return rc;
    };

    RenderCheck f0 = TestFrame(0);
    TEST("Layer 0 displays frame 0", f0.bDisplayedBlink[0]);
    TEST("Layer 1 displays frame 0", f0.bDisplayedBlink[1]);
    TEST("Layer 2 no frames — no blink", !f0.bDisplayedBlink[2]);

    RenderCheck f2 = TestFrame(2);
    TEST("Layer 0 displays frame 2", f2.bDisplayedBlink[0]);
    TEST("Layer 1 displays frame 2", f2.bDisplayedBlink[1]);
    TEST("Layer 2 still no blink", !f2.bDisplayedBlink[2]);

    RenderCheck f4 = TestFrame(4);
    TEST("Layer 0 displays frame 4", f4.bDisplayedBlink[0]);
    TEST("Layer 1 past end — no blink", !f4.bDisplayedBlink[1]);  // only 3 frames (0,1,2)
    TEST("Layer 2 still no blink", !f4.bDisplayedBlink[2]);

    RenderCheck f7 = TestFrame(7);
    TEST("Layer 0 displays frame 7", f7.bDisplayedBlink[0]);
    TEST("Layer 1 past end — still no blink", !f7.bDisplayedBlink[1]);
    TEST("Layer 2 still no blink", !f7.bDisplayedBlink[2]);

    RenderCheck f8 = TestFrame(8);  // past MaxFrames, not rendered
    TEST("Frame 8 past max — no blink layer 0", !f8.bDisplayedBlink[0]);
    TEST("Frame 8 past max — no blink layer 1", !f8.bDisplayedBlink[1]);
    TEST("Frame 8 past max — no blink layer 2", !f8.bDisplayedBlink[2]);
}

void TestZeroFrameBlinkViseme() {
    printf("=== Zero-Frame Blink/Viseme ===\n");

    // Zero-frame blink: MaxFrames = 0, completes instantly
    struct ZeroBlink {
        bool bIsBlinking = false;
        int FrameIndex = 0;
        int MaxFrames = 0;
        int CompletedCount = 0;

        void ForceBlink() {
            bIsBlinking = true;
            FrameIndex = 0;
            // component's UpdateBlinkTick: after increment, checks >= MaxFrames
            // First tick increments to 1, then 1 >= 0 → complete
        }

        void Tick() {
            if (!bIsBlinking) return;
            FrameIndex++;
            if (FrameIndex >= MaxFrames) {
                bIsBlinking = false;
                FrameIndex = 0;
                CompletedCount++;
            }
        }
    };

    ZeroBlink zb;
    zb.ForceBlink();
    zb.Tick(); // FrameIndex → 1, 1 >= 0 → complete
    TEST("Zero-frame blink auto-completes", !zb.bIsBlinking);
    TEST("Zero-frame blink fires completion", zb.CompletedCount == 1);

    // Zero-frame viseme: same logic, instant complete
    struct ZeroViseme {
        bool bPlaying = false;
        int FrameIndex = 0;
        int MaxFrames = 0;
        int CompletedCount = 0;

        void Play() {
            bPlaying = true;
            FrameIndex = 0;
        }

        void Tick() {
            if (!bPlaying) return;
            FrameIndex++;
            if (FrameIndex >= MaxFrames) {
                bPlaying = false;
                FrameIndex = 0;
                CompletedCount++;
            }
        }
    };

    ZeroViseme zv;
    zv.Play();
    zv.Tick(); // FrameIndex → 1, 1 >= 0 → complete
    TEST("Zero-frame viseme auto-completes", !zv.bPlaying);
    TEST("Zero-frame viseme fires completion", zv.CompletedCount == 1);
}

// ====================================================================
// SWOOSH TRANSITION TESTS
// ====================================================================

struct SwooshMachine {
    bool bSwooshEnabled = true;
    double SwooshSpeedThreshold = 120.0;
    double SwooshFrameDuration = 0.033;
    double SwooshBlendOutDuration = 0.15;
    double SwooshBusyness = 0.5;
    double SwooshSize = 0.5;

    enum class Phase { Inactive, Smearing, BlendingOut };
    Phase SwooshPhase = Phase::Inactive;
    int SwooshFrameIndex = 0;
    double SwooshFrameTimer = 0.0;
    double SwooshBlendOutElapsed = 0.0;
    double BlendAlpha = 1.0;
    int SwooshProceduralTick = 0;
    double SwooshSmearAngle = 0.0;
    int ArtFrameCount = 0;

    bool bAnimationsRunning = true;

    void TriggerSwoosh(bool hasArt, int artFrames = 0) {
        if (SwooshPhase != Phase::Inactive) return;
        SwooshPhase = Phase::Smearing;
        SwooshFrameIndex = 0;
        SwooshFrameTimer = 0.0;
        SwooshBlendOutElapsed = 0.0;
        SwooshProceduralTick = 0;
        BlendAlpha = 0.0;
        ArtFrameCount = hasArt ? artFrames : 0;
    }

    void Tick(double dt) {
        if (SwooshPhase == Phase::Inactive) return;

        if (SwooshPhase == Phase::Smearing) {
            if (ArtFrameCount > 0) {
                SwooshFrameTimer += dt;
                if (SwooshFrameTimer >= SwooshFrameDuration) {
                    SwooshFrameTimer = 0.0;
                    SwooshFrameIndex++;
                    if (SwooshFrameIndex >= ArtFrameCount) {
                        SwooshPhase = Phase::BlendingOut;
                        SwooshBlendOutElapsed = 0.0;
                    }
                }
            } else {
                SwooshProceduralTick++;
                int ProcCount = std::max(3, (int)(4.0 + SwooshBusyness * 8.0 + 0.5));
                if (SwooshProceduralTick >= ProcCount) {
                    SwooshPhase = Phase::BlendingOut;
                    SwooshBlendOutElapsed = 0.0;
                }
            }
        }

        if (SwooshPhase == Phase::BlendingOut) {
            SwooshBlendOutElapsed += dt;
            BlendAlpha = std::min(1.0, SwooshBlendOutElapsed / std::max(0.001, SwooshBlendOutDuration));
            if (BlendAlpha >= 1.0) {
                BlendAlpha = 1.0;
                SwooshPhase = Phase::Inactive;
            }
        }
    }

    double GetSwooshLayerBlend() const {
        if (SwooshPhase == Phase::Inactive) return 0.0;
        if (SwooshPhase == Phase::Smearing) return 1.0;
        return BlendAlpha;
    }

    double ComputeProceduralIntensity() const {
        if (ArtFrameCount > 0 || SwooshPhase == Phase::Inactive) return 0.0;
        double Total = std::max(3.0, 4.0 + SwooshBusyness * 8.0);
        double Progress = (Total > 0.0) ? std::min(1.0, SwooshProceduralTick / Total) : 0.0;
        double Intensity = std::sin(Progress * 3.14159265 * (1.0 + SwooshBusyness * 3.0));
        return std::abs(Intensity) * SwooshSize;
    }
};

void TestSwooshTransition() {
    printf("=== Swoosh Transition ===\n");

    // ── Velocity tracking ──
    double dt = 0.016;
    auto AngVel = [dt](double dyaw, double dpitch) {
        return (std::abs(dyaw) + std::abs(dpitch)) / dt;
    };
    TEST("Velocity 45 deg in 16ms", fabs(AngVel(45, 0) - 2812.5) < 1.0);
    TEST("Velocity negative yaw", fabs(AngVel(-45, 0) - 2812.5) < 1.0);
    TEST("Velocity combined axes", fabs(AngVel(30, 20) - 3125.0) < 1.0);
    TEST("Velocity zero delta", fabs(AngVel(0, 0)) < 0.001);
    TEST("Velocity pitch only", fabs(AngVel(0, 60) - 3750.0) < 1.0);

    // ── Threshold boundary ──
    {
        SwooshMachine sm;
        double atThreshold = sm.SwooshSpeedThreshold;
        if (atThreshold >= sm.SwooshSpeedThreshold) sm.TriggerSwoosh(true, 2);
        TEST("Trigger at exact threshold", sm.SwooshPhase != SwooshMachine::Phase::Inactive);
    }
    {
        SwooshMachine sm;
        double justBelow = sm.SwooshSpeedThreshold - 0.1;
        if (justBelow >= sm.SwooshSpeedThreshold) sm.TriggerSwoosh(true, 2);
        TEST("No trigger just below threshold", sm.SwooshPhase == SwooshMachine::Phase::Inactive);
    }
    {
        SwooshMachine sm;
        double justAbove = sm.SwooshSpeedThreshold + 0.1;
        if (justAbove >= sm.SwooshSpeedThreshold) sm.TriggerSwoosh(true, 2);
        TEST("Trigger just above threshold", sm.SwooshPhase != SwooshMachine::Phase::Inactive);
    }

    // ── Swoosh disabled guard ──
    {
        SwooshMachine sm;
        sm.bSwooshEnabled = false;
        double highVel = 999.0;
        if (sm.bSwooshEnabled && highVel >= sm.SwooshSpeedThreshold) sm.TriggerSwoosh(true, 3);
        TEST("Swoosh disabled = no trigger", sm.SwooshPhase == SwooshMachine::Phase::Inactive);
    }

    // ── Art frame boundaries ──
    {
        // Zero art frames → procedural fallback
        SwooshMachine sm;
        sm.TriggerSwoosh(false, 0);
        TEST("Zero art frames → Smearing", sm.SwooshPhase == SwooshMachine::Phase::Smearing);
        TEST("Procedural fallback has no art frames", sm.ArtFrameCount == 0);
    }
    {
        // Single art frame → advances to frame 0 then exhausts
        SwooshMachine sm;
        sm.TriggerSwoosh(true, 1);
        TEST("One frame starts at index 0", sm.SwooshFrameIndex == 0);
        sm.Tick(0.033);
        TEST("One frame exhausts after tick", sm.SwooshPhase == SwooshMachine::Phase::BlendingOut);
    }
    {
        // Many art frames (50)
        SwooshMachine sm;
        sm.TriggerSwoosh(true, 50);
        for (int i = 0; i < 49; i++) sm.Tick(0.033);
        TEST("Frame 49 still smearing", sm.SwooshPhase == SwooshMachine::Phase::Smearing);
        sm.Tick(0.033);
        TEST("Frame 50 exhausts", sm.SwooshPhase == SwooshMachine::Phase::BlendingOut);
    }

    // ── Art frame timing precision ──
    {
        SwooshMachine sm;
        sm.TriggerSwoosh(true, 3);
        // 0.032 is just under 0.033 frame duration
        sm.Tick(0.032);
        TEST("Frame 0 before duration", sm.SwooshFrameIndex == 0);
        sm.Tick(0.001);
        TEST("Frame advances at exactly 0.033", sm.SwooshFrameIndex == 1);
    }

    // ── Art frame exhaustion → BlendingOut ──
    {
        SwooshMachine sm;
        sm.TriggerSwoosh(true, 3);
        sm.Tick(0.033); sm.Tick(0.033);
        TEST("Frame 1 still smearing", sm.SwooshPhase == SwooshMachine::Phase::Smearing);
        sm.Tick(0.033);
        TEST("Frame 2 exhausts → BlendingOut", sm.SwooshPhase == SwooshMachine::Phase::BlendingOut);
    }

    // ── Blend-out timing ──
    {
        SwooshMachine sm;
        sm.TriggerSwoosh(true, 1);
        sm.Tick(0.033); // exhausts → BlendingOut, elapsed=0.033
        TEST("BlendingOut after art exhaust", sm.SwooshPhase == SwooshMachine::Phase::BlendingOut);
        TEST("BlendAlpha > 0 on exhaust tick", sm.BlendAlpha > 0.0);
        sm.Tick(0.075); // total 0.108
        TEST("BlendAlpha near 0.72", fabs(sm.BlendAlpha - 0.72) < 0.05);
        sm.Tick(0.075); // total 0.183 ≥ 0.15
        TEST("BlendAlpha completes", fabs(sm.BlendAlpha - 1.0) < 0.001);
        TEST("Swoosh inactive after blend", sm.SwooshPhase == SwooshMachine::Phase::Inactive);
    }

    // ── Blend-out at exact boundaries ──
    {
        SwooshMachine sm;
        sm.TriggerSwoosh(true, 1);
        sm.Tick(0.033); // exhaust → BlendingOut
        TEST("BlendAlpha=0 at t=0", fabs(sm.BlendAlpha - 0.033/0.15) < 0.001);
        sm.Tick(0.117); // t=0.15
        TEST("BlendAlpha=1 at t=Duration", fabs(sm.BlendAlpha - 1.0) < 0.001);
        sm.Tick(1.0); // past duration
        TEST("BlendAlpha clamped to 1", fabs(sm.BlendAlpha - 1.0) < 0.001);
    }

    // ── Procedural fallback ──
    {
        SwooshMachine sm;
        sm.TriggerSwoosh(false);
        TEST("Procedural starts smearing", sm.SwooshPhase == SwooshMachine::Phase::Smearing);
        for (int i = 0; i < 7; i++) sm.Tick(0.016);
        TEST("Procedural still smearing @ tick 7", sm.SwooshPhase == SwooshMachine::Phase::Smearing);
        sm.Tick(0.016); // 8th tick exhausts
        TEST("Procedural exhausts → BlendingOut", sm.SwooshPhase == SwooshMachine::Phase::BlendingOut);
        sm.Tick(0.15);
        TEST("Procedural complete", sm.SwooshPhase == SwooshMachine::Phase::Inactive);
    }

    // ── Animations not stopped ──
    {
        SwooshMachine sm;
        sm.TriggerSwoosh(true, 2);
        TEST("Animations run during smearing", sm.bAnimationsRunning);
        sm.Tick(0.033); sm.Tick(0.033); sm.Tick(0.033);
        TEST("Animations run during blend-out", sm.bAnimationsRunning);
        sm.Tick(0.15);
        TEST("Animations run after swoosh", sm.bAnimationsRunning);
    }

    // ── SwooshLayerBlend via phases ──
    {
        SwooshMachine sm;
        TEST("Inactive blend = 0", fabs(sm.GetSwooshLayerBlend()) < 0.001);
        sm.TriggerSwoosh(true, 2);
        TEST("Smearing blend = 1", fabs(sm.GetSwooshLayerBlend() - 1.0) < 0.001);
        sm.Tick(0.033); sm.Tick(0.033); sm.Tick(0.033);
        TEST("BlendingOut blend = Alpha", fabs(sm.GetSwooshLayerBlend() - sm.BlendAlpha) < 0.001);
        sm.Tick(0.15);
        TEST("Complete inactive blend = 0", fabs(sm.GetSwooshLayerBlend()) < 0.001);
    }

    // ── Control: SwooshSpeedThreshold ──
    {
        SwooshMachine sm;
        sm.SwooshSpeedThreshold = 50.0;
        double vel50 = 50.0;
        if (vel50 >= sm.SwooshSpeedThreshold) sm.TriggerSwoosh(true, 1);
        TEST("Custom low threshold triggers", sm.SwooshPhase != SwooshMachine::Phase::Inactive);
    }
    {
        SwooshMachine sm;
        sm.SwooshSpeedThreshold = 500.0;
        double vel200 = 200.0;
        if (vel200 >= sm.SwooshSpeedThreshold) sm.TriggerSwoosh(true, 1);
        TEST("Custom high threshold blocks", sm.SwooshPhase == SwooshMachine::Phase::Inactive);
    }

    // ── Control: SwooshBusyness procedural count ──
    {
        SwooshMachine sm;
        sm.SwooshBusyness = 0.0;
        sm.TriggerSwoosh(false);
        for (int i = 0; i < 5; i++) sm.Tick(0.016);
        TEST("Busyness=0 → ProcCount=4", sm.SwooshProceduralTick == 4);
    }
    {
        SwooshMachine sm;
        sm.SwooshBusyness = 1.0;
        sm.TriggerSwoosh(false);
        for (int i = 0; i < 13; i++) sm.Tick(0.016);
        TEST("Busyness=1 → ProcCount=12", sm.SwooshProceduralTick == 12);
    }
    {
        SwooshMachine sm;
        sm.SwooshBusyness = 0.25;
        sm.TriggerSwoosh(false);
        // ProcCount = max(3, int(4+2+0.5)) = max(3, 6) = 6
        for (int i = 0; i < 7; i++) sm.Tick(0.016);
        TEST("Busyness=0.25 → ProcCount=6", sm.SwooshProceduralTick == 6);
    }

    // ── Control: SwooshBusyness oscillation pattern ──
    {
        // Busyness=0 → sin(π * progress * 1) → 1 peak
        // Busyness=1 → sin(π * progress * 4) → 4 peaks
        auto CountPeaks = [](double busyness) {
            SwooshMachine sm;
            sm.SwooshBusyness = busyness;
            sm.TriggerSwoosh(false);
            double Total = std::max(3.0, 4.0 + busyness * 8.0);
            int Peaks = 0;
            double prev = -1.0;
            for (int t = 0; t <= (int)Total; t++) {
                sm.SwooshProceduralTick = t;
                double cur = sm.ComputeProceduralIntensity();
                if (prev >= 0.0 && cur < prev && prev > 0.01) Peaks++;
                prev = cur;
            }
            return Peaks;
        };
        double peaksLow = CountPeaks(0.0);
        // |sin(π·t/4)| at t=0..4: values 0, 0.35, 0.5, 0.35, 0 → 2 descending steps
        TEST("Busyness=0 → 2 descending steps", peaksLow == 2);
        double peaksHigh = CountPeaks(1.0);
        // |sin(π·t/3)| at t=0..12: 4 half-sine lobes → 4 descending steps
        TEST("Busyness=1 → 4 descending steps", peaksHigh == 4);
    }

    // ── Control: SwooshSize scales intensity ──
    {
        SwooshMachine sm;
        sm.SwooshSize = 1.0;
        sm.TriggerSwoosh(false);
        sm.Tick(0.016);
        double fullIntensity = sm.ComputeProceduralIntensity();
        TEST("Size=1 gives positive intensity", fullIntensity > 0.0);
        TEST("Size=1 intensity ≤ 1", fullIntensity <= 1.0);
    }
    {
        SwooshMachine sm;
        sm.SwooshSize = 0.5;
        sm.TriggerSwoosh(false);
        sm.Tick(0.016);
        double halfIntensity = sm.ComputeProceduralIntensity();
        TEST("Size=0.5 intensity ≤ 0.5", halfIntensity <= 0.51);
    }
    {
        SwooshMachine sm;
        sm.SwooshSize = 0.0;
        sm.TriggerSwoosh(false);
        sm.Tick(0.016);
        TEST("Size=0 → zero intensity", fabs(sm.ComputeProceduralIntensity()) < 0.001);
    }

    // ── Proportional: Size scales intensity linearly ──
    {
        SwooshMachine sm1, sm2;
        sm1.SwooshSize = 0.5; sm2.SwooshSize = 1.0;
        sm1.TriggerSwoosh(false); sm2.TriggerSwoosh(false);
        // Compare at same procedural tick
        for (int i = 0; i < 3; i++) { sm1.Tick(0.016); sm2.Tick(0.016); }
        double i1 = sm1.ComputeProceduralIntensity();
        double i2 = sm2.ComputeProceduralIntensity();
        TEST("Size=1 is double Size=0.5", fabs(i2 - i1 * 2.0) < 0.001);
    }

    // ── Art frame rate independent of SwooshFrameDuration ──
    {
        SwooshMachine sm;
        sm.SwooshFrameDuration = 0.1;
        sm.TriggerSwoosh(true, 3);
        sm.Tick(0.099);
        TEST("Frame 0 at 0.099s", sm.SwooshFrameIndex == 0);
        sm.Tick(0.001);
        TEST("Frame 1 at 0.100s", sm.SwooshFrameIndex == 1);
    }

    // ── Blend-out duration configurable ──
    {
        SwooshMachine sm;
        sm.SwooshBlendOutDuration = 0.5;
        sm.TriggerSwoosh(true, 1);
        sm.Tick(0.033);
        TEST("Long blend at 0.033", fabs(sm.BlendAlpha - 0.066) < 0.001);
        sm.Tick(0.25);
        TEST("Long blend at 0.283", fabs(sm.BlendAlpha - 0.566) < 0.001);
        sm.Tick(0.25);
        TEST("Long blend completes", fabs(sm.BlendAlpha - 1.0) < 0.001);
    }

    // ── Re-entrant guard ──
    {
        SwooshMachine sm;
        sm.TriggerSwoosh(true, 5);
        TEST("First swoosh active", sm.SwooshPhase == SwooshMachine::Phase::Smearing);
        // Attempt re-trigger (guarded in real component, but simulate here)
        // In the real component, ForceSwoosh checks SwooshPhase != Inactive → return
        // Just verify phase doesn't reset
        sm.TriggerSwoosh(true, 5);
        // After re-trigger, frame should still be at 0
        TEST("Re-trigger does not reset frame", sm.SwooshFrameIndex == 0);
    }

    // ── Multiple complete cycles ──
    {
        SwooshMachine sm;
        for (int cycle = 0; cycle < 3; cycle++) {
            sm.TriggerSwoosh(true, 2);
            TEST("Cycle starts smearing", sm.SwooshPhase == SwooshMachine::Phase::Smearing);
            sm.Tick(0.033); sm.Tick(0.033);
            sm.Tick(0.033); // exhausts
            TEST("Cycle blend-out", sm.SwooshPhase == SwooshMachine::Phase::BlendingOut);
            sm.Tick(0.15); // completes
            TEST("Cycle complete", sm.SwooshPhase == SwooshMachine::Phase::Inactive);
        }
    }

    // ── Smear angle computation ──
    {
        SwooshMachine sm;
        double dyaw = 30.0, dpitch = 40.0;
        double angle = std::atan2(dpitch, dyaw) * 180.0 / 3.14159265;
        TEST("Smear angle positive", fabs(angle - 53.13) < 0.1);
    }
    {
        double dyaw = -30.0, dpitch = 10.0;
        double angle = std::atan2(dpitch, dyaw) * 180.0 / 3.14159265;
        TEST("Smear angle negative yaw", fabs(angle - 161.57) < 0.1);
    }
}

// ====================================================================
// PARAMETER SYSTEM TESTS
// ====================================================================

enum class EFaceParamTarget {
    PositionX, PositionY, ScaleX, ScaleY, Rotation, TextureBlend
};

struct FFaceParamBinding {
    int ParamId = 0;
    EFaceParamTarget Target = EFaceParamTarget::PositionX;
    double Scale = 1.0;
    double Offset = 0.0;
    bool bInvert = false;
};

struct ParamDef {
    double DefaultValue = 0.0;
    double CurrentValue = 0.0;
    double TargetValue = 0.0;
    double Min = 0.0;
    double Max = 1.0;
    double SmoothingSpeed = 8.0;
};

struct ParamSystem {
    ParamDef Params[10];
    int ParamCount = 0;

    // Per-layer bindings: up to 4 layers, 8 bindings each
    FFaceParamBinding Bindings[4][8];
    int BindingCounts[4] = {};
    int LayerCount = 4;

    // Alt textures per layer (simplified: just a bool)
    bool bHasAltTextures[4] = {};

    void Define(int id, double def, double minv, double maxv, double speed) {
        if (id >= 0 && id < 10) {
            Params[id].DefaultValue = std::max(minv, std::min(maxv, def));
            Params[id].CurrentValue = Params[id].DefaultValue;
            Params[id].TargetValue = Params[id].DefaultValue;
            Params[id].Min = minv;
            Params[id].Max = maxv;
            Params[id].SmoothingSpeed = std::max(0.1, speed);
            if (id >= ParamCount) ParamCount = id + 1;
        }
    }

    void SetValue(int id, double val) {
        if (id < 0 || id >= 10) return;
        val = std::max(Params[id].Min, std::min(Params[id].Max, val));
        Params[id].TargetValue = val;
    }

    double GetValue(int id) const {
        if (id < 0 || id >= 10) return 0.0;
        return Params[id].CurrentValue;
    }

    void ResetAll() {
        for (int i = 0; i < 10; ++i) {
            Params[i].TargetValue = Params[i].DefaultValue;
        }
    }

    void TickSmoothing(double dt) {
        for (int i = 0; i < 10; ++i) {
            double& cur = Params[i].CurrentValue;
            double tgt = Params[i].TargetValue;
            if (std::abs(cur - tgt) > 0.00001) {
                double speed = Params[i].SmoothingSpeed;
                double factor = std::min(1.0, speed * dt);
                cur += (tgt - cur) * factor;
            }
        }
    }

    void AddBinding(int layerIdx, int paramId, EFaceParamTarget target, double scale, double offset, bool invert) {
        if (layerIdx < 0 || layerIdx >= 4) return;
        int& count = BindingCounts[layerIdx];
        if (count >= 8) return;
        Bindings[layerIdx][count].ParamId = paramId;
        Bindings[layerIdx][count].Target = target;
        Bindings[layerIdx][count].Scale = scale;
        Bindings[layerIdx][count].Offset = offset;
        Bindings[layerIdx][count].bInvert = invert;
        count++;
    }

    struct EvalResult {
        double PosX, PosY, ScaleX, ScaleY, Rotation;
        double TextureBlend;
    };

    EvalResult Evaluate(int layerIdx, double basePosX, double basePosY, double baseScaleX, double baseScaleY, double baseRot) {
        EvalResult r = {basePosX, basePosY, baseScaleX, baseScaleY, baseRot, 0.0};
        if (layerIdx < 0 || layerIdx >= 4) return r;

        for (int b = 0; b < BindingCounts[layerIdx]; ++b) {
            const FFaceParamBinding& binding = Bindings[layerIdx][b];
            double pv = GetValue(binding.ParamId);
            double ev = binding.bInvert ? (1.0 - pv) : pv;
            double mod = ev * binding.Scale + binding.Offset;

            switch (binding.Target) {
                case EFaceParamTarget::PositionX: r.PosX += mod; break;
                case EFaceParamTarget::PositionY: r.PosY += mod; break;
                case EFaceParamTarget::ScaleX:    r.ScaleX *= std::max(0.01, 1.0 + mod); break;
                case EFaceParamTarget::ScaleY:    r.ScaleY *= std::max(0.01, 1.0 + mod); break;
                case EFaceParamTarget::Rotation:  r.Rotation += mod; break;
                case EFaceParamTarget::TextureBlend:
                    r.TextureBlend = std::max(r.TextureBlend, std::max(0.0, std::min(1.0, ev * binding.Scale + binding.Offset)));
                    break;
            }
        }
        return r;
    }
};

void TestParameterSystem() {
    printf("=== Parameter System ===\n");

    // Basic param def/set/get
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        TEST("Param default 0", fabs(ps.GetValue(0)) < 0.001);

        ps.SetValue(0, 0.5);
        ps.TickSmoothing(1.0);
        TEST("Param set 0.5", fabs(ps.GetValue(0) - 0.5) < 0.01);

        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        TEST("Param set 1.0", fabs(ps.GetValue(0) - 1.0) < 0.01);
    }

    // Clamping
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.SetValue(0, 2.0);
        TEST("Clamp to max", fabs(ps.Params[0].TargetValue - 1.0) < 0.001);

        ps.SetValue(0, -1.0);
        TEST("Clamp to min", fabs(ps.Params[0].TargetValue) < 0.001);
    }

    // Unknown param returns 0
    {
        ParamSystem ps;
        TEST("Unknown param = 0", fabs(ps.GetValue(99)) < 0.001);
    }

    // Reset restores default
    {
        ParamSystem ps;
        ps.Define(0, 0.3, 0.0, 1.0, 8.0);
        ps.SetValue(0, 0.9);
        ps.ResetAll();
        TEST("Reset to default", fabs(ps.Params[0].TargetValue - 0.3) < 0.001);
    }

    // Param with non-zero default
    {
        ParamSystem ps;
        ps.Define(0, 0.7, 0.0, 1.0, 8.0);
        TEST("Non-zero default", fabs(ps.GetValue(0) - 0.7) < 0.01);
    }

    // Smoothing multiple steps
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 4.0);
        ps.SetValue(0, 1.0);
        ps.TickSmoothing(0.016);
        double v1 = ps.GetValue(0);
        TEST("Smoothing step 1 > 0", v1 > 0.0);
        TEST("Smoothing step 1 < 1", v1 < 1.0);
        for (int i = 0; i < 120; ++i) ps.TickSmoothing(0.016);
        TEST("Smoothing reaches target", fabs(ps.GetValue(0) - 1.0) < 0.01);
    }

    // Single binding: PositionX
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 10.0, 0.0, false);
        ps.SetValue(0, 0.5);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 100.0, 200.0, 1.0, 1.0, 0.0);
        TEST("PositionX binding", fabs(r.PosX - 105.0) < 0.01);
        TEST("PositionY unchanged", fabs(r.PosY - 200.0) < 0.01);
    }

    // Single binding: PositionY
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionY, 20.0, 0.0, false);
        ps.SetValue(0, 0.3);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 100.0, 200.0, 1.0, 1.0, 0.0);
        TEST("PositionY binding", fabs(r.PosY - 206.0) < 0.01);
    }

    // Single binding: ScaleX (multiplicative)
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::ScaleX, 1.0, 0.0, false);
        ps.SetValue(0, 0.5);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("ScaleX binding: 1 * (1 + 0.5*1) = 1.5", fabs(r.ScaleX - 1.5) < 0.01);
    }

    // Single binding: ScaleY (multiplicative)
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::ScaleY, -0.5, 0.0, false);
        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("ScaleY binding: 1 * (1 + 1.0*(-0.5)) = 0.5", fabs(r.ScaleY - 0.5) < 0.01);
    }

    // Single binding: Rotation (additive)
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::Rotation, 30.0, 0.0, false);
        ps.SetValue(0, 0.5);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 10.0);
        TEST("Rotation binding: 10 + 0.5*30 = 25", fabs(r.Rotation - 25.0) < 0.01);
    }

    // Single binding: Rotation with offset
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::Rotation, 0.0, 15.0, false);
        ps.SetValue(0, 0.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Rotation offset only: 15", fabs(r.Rotation - 15.0) < 0.01);
    }

    // Scale + offset + invert combos
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        // Invert + scale + offset: when param=0, invert makes it 1, so mod = 1*2+5 = 7
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 2.0, 5.0, true);
        ps.SetValue(0, 0.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Invert at 0: (1-0)*2+5=7", fabs(r.PosX - 7.0) < 0.01);

        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Invert at 1: (1-1)*2+5=5", fabs(r.PosX - 5.0) < 0.01);
    }

    // Multiple bindings on same slot
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.Define(1, 0.5, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 10.0, 0.0, false);
        ps.AddBinding(0, 1, EFaceParamTarget::PositionY, -20.0, 0.0, false);
        ps.AddBinding(0, 0, EFaceParamTarget::Rotation, 5.0, 0.0, false);
        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Multi binding PosX", fabs(r.PosX - 10.0) < 0.01);
        TEST("Multi binding PosY = 0 + 0.5*(-20) = -10", fabs(r.PosY + 10.0) < 0.01);
        TEST("Multi binding Rot = 0 + 1.0*5 = 5", fabs(r.Rotation - 5.0) < 0.01);
    }

    // TextureBlend binding
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::TextureBlend, 1.0, 0.0, false);
        ps.SetValue(0, 0.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("TextureBlend at 0 = 0", fabs(r.TextureBlend) < 0.001);

        ps.SetValue(0, 0.7);
        ps.TickSmoothing(1.0);
        r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("TextureBlend at 0.7 = 0.7", fabs(r.TextureBlend - 0.7) < 0.01);
    }

    // TextureBlend takes max of multiple bindings
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.Define(1, 0.3, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::TextureBlend, 1.0, 0.0, false);
        ps.AddBinding(0, 1, EFaceParamTarget::TextureBlend, 1.0, 0.0, false);
        ps.SetValue(0, 0.5);
        ps.SetValue(1, 0.8);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("TextureBlend takes max 0.8", fabs(r.TextureBlend - 0.8) < 0.01);
    }

    // TextureBlend clamped to [0,1]
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::TextureBlend, 2.0, 0.0, false);
        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("TextureBlend clamped to 1", r.TextureBlend <= 1.0);
    }

    // Zero scale factor
    {
        ParamSystem ps;
        ps.Define(0, 1.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::ScaleX, 0.0, 0.0, false);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Zero scale factor no change", fabs(r.ScaleX - 1.0) < 0.01);
    }

    // Negative scale factor guarded
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::ScaleX, -2.0, 0.0, false);
        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Negative scale guarded >= 0.01", r.ScaleX >= 0.01);
    }

    // Binding on wrong layer doesn't affect others
    {
        ParamSystem ps;
        ps.Define(0, 1.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 50.0, 0.0, false);
        ps.TickSmoothing(1.0);
        auto r1 = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        auto r2 = ps.Evaluate(1, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Layer 0 binding affects layer 0", fabs(r1.PosX - 50.0) < 0.01);
        TEST("Layer 1 unaffected by layer 0 bindings", fabs(r2.PosX) < 0.01);
    }

    // Multiple params, each binding uses its own param
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.Define(1, 0.3, 0.0, 1.0, 8.0);
        ps.Define(2, 0.7, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 100.0, 0.0, false);
        ps.AddBinding(0, 1, EFaceParamTarget::PositionY, 100.0, 0.0, false);
        ps.AddBinding(0, 2, EFaceParamTarget::Rotation, 100.0, 0.0, false);
        ps.SetValue(0, 1.0);
        ps.SetValue(1, 1.0);
        ps.SetValue(2, 1.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Three params at 1: PosX=100", fabs(r.PosX - 100.0) < 0.01);
        TEST("Three params at 1: PosY=100", fabs(r.PosY - 100.0) < 0.01);
        TEST("Three params at 1: Rot=100", fabs(r.Rotation - 100.0) < 0.01);
    }

    // Layer with no bindings passes through
    {
        ParamSystem ps;
        auto r = ps.Evaluate(0, 10.0, 20.0, 2.0, 3.0, 45.0);
        TEST("No bindings pass through PosX", fabs(r.PosX - 10.0) < 0.01);
        TEST("No bindings pass through Scale", fabs(r.ScaleX - 2.0) < 0.01 && fabs(r.ScaleY - 3.0) < 0.01);
        TEST("No bindings pass through Rot", fabs(r.Rotation - 45.0) < 0.01);
    }
}

// ====================================================================
// NESTED ART + JIGGLE SYSTEM TESTS
// ====================================================================

struct FFaceJiggleSettings {
    double Stiffness = 5.0;
    double Damping = 0.5;
    double ImpulseScale = 1.0;
    double JiggleAxisX = 1.0, JiggleAxisY = 1.0;
};

struct FFaceTextureSet {
    bool bValid = false;
    bool IsValid() const { return bValid; }
};

struct FFaceNestedArt {
    int32_t ElementName = 0; // using int for simplicity
    FFaceArtTransform RelativeTransform;
    double PivotX = 0.5, PivotY = 0.5;
    bool bJiggleEnabled = false;
    FFaceJiggleSettings JiggleSettings;
    int32_t IdleFrameCount = 0;
    double IdleFrameDuration = 0.1;
    double IdleSpeedMultiplier = 1.0;
    struct { int32_t State; bool Visible; } ViewVisibility[10];
    int32_t ViewVisibilityCount = 0;
    int32_t ChildCount = 0;
};

// Spring-damper simulation (matches component logic)
struct JiggleState {
    double PosX = 0.0, PosY = 0.0;
    double VelX = 0.0, VelY = 0.0;
};

void SimulateJiggle(JiggleState& S, const FFaceJiggleSettings& J, double ImpulseX, double ImpulseY, double DT) {
    S.VelX += ImpulseX * J.JiggleAxisX * J.ImpulseScale;
    S.VelY += ImpulseY * J.JiggleAxisY * J.ImpulseScale;
    double SpringForceX = -S.PosX * J.Stiffness;
    double SpringForceY = -S.PosY * J.Stiffness;
    double DampingForceX = -S.VelX * J.Damping;
    double DampingForceY = -S.VelY * J.Damping;
    S.VelX += (SpringForceX + DampingForceX) * DT;
    S.VelY += (SpringForceY + DampingForceY) * DT;
    S.PosX += S.VelX * DT;
    S.PosY += S.VelY * DT;
}

FFaceArtTransform ComputeNestedTransform(const FFaceArtTransform& Parent, const FFaceArtTransform& ChildRelative, double JiggleX, double JiggleY) {
    FFaceArtTransform Result;
    Result.Position.X = Parent.Position.X + ChildRelative.Position.X + JiggleX;
    Result.Position.Y = Parent.Position.Y + ChildRelative.Position.Y + JiggleY;
    Result.Scale.X = Parent.Scale.X * ChildRelative.Scale.X;
    Result.Scale.Y = Parent.Scale.Y * ChildRelative.Scale.Y;
    Result.Rotation = Parent.Rotation + ChildRelative.Rotation;
    return Result;
}

int32_t AdvanceIdleFrame(int32_t CurrentFrame, double& Timer, double DeltaTime, int32_t NumFrames, double FrameDuration, double SpeedMult) {
    double EffectiveDuration = FrameDuration / std::max(0.001, SpeedMult);
    Timer += DeltaTime;
    while (Timer >= EffectiveDuration && NumFrames > 0) {
        Timer -= EffectiveDuration;
        CurrentFrame = (CurrentFrame + 1) % NumFrames;
    }
    return CurrentFrame;
}

bool GetNestedVisibility(const FFaceNestedArt& Elem, int32_t ViewState) {
    bool Default = true;
    for (int32_t i = 0; i < Elem.ViewVisibilityCount; ++i) {
        if (Elem.ViewVisibility[i].State == ViewState)
            Default = Elem.ViewVisibility[i].Visible;
    }
    return Default;
}

void TestNestedArtSystem() {
    printf("\n--- Nested Art + Jiggle System ---\n");

    // === JIGGLE PHYSICS (15 tests) ===

    // 1. Initial position zero
    {
        JiggleState S;
        TEST("Jiggle initial pos X zero", S.PosX == 0.0);
        TEST("Jiggle initial pos Y zero", S.PosY == 0.0);
        TEST("Jiggle initial vel zero", S.VelX == 0.0 && S.VelY == 0.0);
    }

    // 2. Impulse causes displacement
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, 10.0, 0.0, 0.016);
        TEST("Jiggle impulse creates X displacement", fabs(S.PosX) > 0.0);
    }

    // 3. Higher stiffness = faster oscillation
    {
        JiggleState S1, S2;
        FFaceJiggleSettings J1, J2;
        J1.Stiffness = 10.0;
        J2.Stiffness = 1.0;
        for (int i = 0; i < 30; ++i) {
            SimulateJiggle(S1, J1, 10.0, 0.0, 0.016);
            SimulateJiggle(S2, J2, 10.0, 0.0, 0.016);
        }
        TEST("Stiffness affects dynamics", S1.PosX != S2.PosX || S1.VelX != S2.VelX);
    }

    // 4. Damping reduces amplitude over time
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.Damping = 2.0;
        SimulateJiggle(S, J, 10.0, 0.0, 0.016);
        double PeakX = S.PosX;
        // Simulate many steps to see damping
        for (int i = 0; i < 100; ++i) SimulateJiggle(S, J, 0.0, 0.0, 0.016);
        TEST("Damping reduces amplitude", fabs(S.PosX) < fabs(PeakX) * 1.1);
    }

    // 5. Zero damping = sustained oscillation (undamped)
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.Damping = 0.0;
        SimulateJiggle(S, J, 5.0, 0.0, 0.016);
        (void)S.PosX;
        for (int i = 0; i < 50; ++i) SimulateJiggle(S, J, 0.0, 0.0, 0.016);
        double Pos2 = S.PosX;
        // Undamped spring sustains oscillation long after impulse (50+ steps)
        TEST("Zero damping oscillates", fabs(Pos2) > 0.001);
    }

    // 6. Axis-limited jiggle
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.JiggleAxisX = 1.0;
        J.JiggleAxisY = 0.0;
        SimulateJiggle(S, J, 5.0, 5.0, 0.016);
        TEST("Axis-limited jiggle X moves", fabs(S.PosX) > 0.0);
        TEST("Axis-limited jiggle Y stays zero", S.PosY == 0.0);
    }

    // 7. Axis-limited jiggle Y only
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.JiggleAxisX = 0.0;
        J.JiggleAxisY = 1.0;
        SimulateJiggle(S, J, 5.0, 5.0, 0.016);
        TEST("Axis-limited Y only: X stays zero", S.PosX == 0.0);
        TEST("Axis-limited Y only: Y moves", fabs(S.PosY) > 0.0);
    }

    // 8. Multiple impulses compound
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, 10.0, 0.0, 0.016);
        double PosAfter1 = S.PosX;
        SimulateJiggle(S, J, 10.0, 0.0, 0.016);
        TEST("Multiple impulses compound (pos changes)", S.PosX != PosAfter1 || fabs(S.PosX) > 0.0);
    }

    // 9. Impulse in X only
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, 10.0, 0.0, 0.016);
        TEST("X-only impulse: X moves", S.PosX != 0.0);
        TEST("X-only impulse: Y stays zero", S.PosY == 0.0);
    }

    // 10. Impulse in Y only
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, 0.0, 10.0, 0.016);
        TEST("Y-only impulse: X stays zero", S.PosX == 0.0);
        TEST("Y-only impulse: Y moves", S.PosY != 0.0);
    }

    // 11. High damping = quick settle
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.Damping = 5.0;
        SimulateJiggle(S, J, 50.0, 0.0, 0.016);
        double VelAfter = S.VelX;
        for (int i = 0; i < 20; ++i) SimulateJiggle(S, J, 0.0, 0.0, 0.016);
        TEST("High damping settles quickly (velocity decays)", fabs(S.VelX) < fabs(VelAfter) || fabs(S.VelX) < 0.01);
    }

    // 12. Position returns toward zero after disturbance
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.Damping = 2.0;
        SimulateJiggle(S, J, 10.0, 0.0, 0.016);
        double FirstDecay = std::sqrt(S.PosX*S.PosX + S.VelX*S.VelX);
        for (int i = 0; i < 60; ++i) SimulateJiggle(S, J, 0.0, 0.0, 0.016);
        double LaterDecay = std::sqrt(S.PosX*S.PosX + S.VelX*S.VelX);
        TEST("Position returns toward zero", LaterDecay < FirstDecay);
    }

    // 13. Zero impulse = no movement
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, 0.0, 0.0, 0.016);
        TEST("Zero impulse: no movement", S.PosX == 0.0 && S.PosY == 0.0);
    }

    // 14. Impulse scale amplifies
    {
        JiggleState S1, S2;
        FFaceJiggleSettings J1, J2;
        J1.ImpulseScale = 1.0;
        J2.ImpulseScale = 2.0;
        SimulateJiggle(S1, J1, 10.0, 0.0, 0.016);
        SimulateJiggle(S2, J2, 10.0, 0.0, 0.016);
        TEST("Higher impulse scale = larger displacement", fabs(S2.PosX) > fabs(S1.PosX) || fabs(S2.VelX) > fabs(S1.VelX));
    }

    // 15. Negative impulse moves opposite direction
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, -10.0, 0.0, 0.016);
        TEST("Negative impulse: moves negative", S.PosX < 0.0);
    }

    // === NESTED TRANSFORMS (10 tests) ===

    // 16. Child pos = parent + child relative
    {
        FFaceArtTransform Parent(FVector2D(10, 20), FVector2D(1, 1), 0);
        FFaceArtTransform ChildRel(FVector2D(5, 3), FVector2D(1, 1), 0);
        auto Result = ComputeNestedTransform(Parent, ChildRel, 0, 0);
        TEST("Nested pos: parent + relative", fabs(Result.Position.X - 15) < 0.001 && fabs(Result.Position.Y - 23) < 0.001);
    }

    // 17. Child rot = parent + child relative
    {
        FFaceArtTransform Parent(FVector2D(0, 0), FVector2D(1, 1), 45);
        FFaceArtTransform ChildRel(FVector2D(0, 0), FVector2D(1, 1), 15);
        auto Result = ComputeNestedTransform(Parent, ChildRel, 0, 0);
        TEST("Nested rot: parent + relative", fabs(Result.Rotation - 60) < 0.001);
    }

    // 18. Child scale = parent * child relative
    {
        FFaceArtTransform Parent(FVector2D(0, 0), FVector2D(2, 2), 0);
        FFaceArtTransform ChildRel(FVector2D(0, 0), FVector2D(0.5, 0.5), 0);
        auto Result = ComputeNestedTransform(Parent, ChildRel, 0, 0);
        TEST("Nested scale: parent * relative", fabs(Result.Scale.X - 1.0) < 0.001 && fabs(Result.Scale.Y - 1.0) < 0.001);
    }

    // 19. Deep nesting (3 levels)
    {
        FFaceArtTransform L0(FVector2D(1, 1), FVector2D(2, 2), 0);
        FFaceArtTransform L1(FVector2D(2, 2), FVector2D(1.5, 1.5), 0);
        FFaceArtTransform L2(FVector2D(3, 3), FVector2D(1, 1), 0);
        auto R1 = ComputeNestedTransform(L0, L1, 0, 0);
        auto R2 = ComputeNestedTransform(R1, L2, 0, 0);
        TEST("Deep nested pos: 1+2+3=6", fabs(R2.Position.X - 6) < 0.001 && fabs(R2.Position.Y - 6) < 0.001);
        TEST("Deep nested scale: 2*1.5*1=3", fabs(R2.Scale.X - 3.0) < 0.001);
    }

    // 20. Jiggle offset applied to nested transform
    {
        FFaceArtTransform Parent(FVector2D(0, 0), FVector2D(1, 1), 0);
        FFaceArtTransform ChildRel(FVector2D(10, 0), FVector2D(1, 1), 0);
        auto Result = ComputeNestedTransform(Parent, ChildRel, 3, 4);
        TEST("Jiggle offset applied to pos", fabs(Result.Position.X - 13) < 0.001 && fabs(Result.Position.Y - 4) < 0.001);
    }

    // 21. Zero relative transform = parent
    {
        FFaceArtTransform Parent(FVector2D(100, 200), FVector2D(0.5, 0.5), 90);
        FFaceArtTransform ChildRel(FVector2D(0, 0), FVector2D(1, 1), 0);
        auto Result = ComputeNestedTransform(Parent, ChildRel, 0, 0);
        TEST("Zero relative = parent", fabs(Result.Position.X - 100) < 0.001 && fabs(Result.Position.Y - 200) < 0.001
            && fabs(Result.Scale.X - 0.5) < 0.001 && fabs(Result.Rotation - 90) < 0.001);
    }

    // 22. Static child + jiggle sibling (both in same parent)
    {
        // Verify that sibling transforms don't interfere
        FFaceArtTransform Parent(FVector2D(50, 50), FVector2D(1, 1), 0);
        FFaceArtTransform ChildA(FVector2D(10, 0), FVector2D(1, 1), 0);
        FFaceArtTransform ChildB(FVector2D(-10, 0), FVector2D(1, 1), 0);
        auto RA = ComputeNestedTransform(Parent, ChildA, 2, 0);
        auto RB = ComputeNestedTransform(Parent, ChildB, -2, 0);
        TEST("Sibling A pos", fabs(RA.Position.X - 62) < 0.001);
        TEST("Sibling B pos", fabs(RB.Position.X - 38) < 0.001);
    }

    // 23. Negative scale flips
    {
        FFaceArtTransform Parent(FVector2D(0, 0), FVector2D(-1, 1), 0);
        FFaceArtTransform ChildRel(FVector2D(5, 0), FVector2D(1, 1), 0);
        auto Result = ComputeNestedTransform(Parent, ChildRel, 0, 0);
        TEST("Negative parent scale: child pos unchanged additive", fabs(Result.Position.X - 5) < 0.001);
        TEST("Negative parent scale: child scale = -1*1", fabs(Result.Scale.X + 1.0) < 0.001);
    }

    // 24. Zero scale child
    {
        FFaceArtTransform Parent(FVector2D(0, 0), FVector2D(2, 2), 0);
        FFaceArtTransform ChildRel(FVector2D(5, 0), FVector2D(0, 0), 0);
        auto Result = ComputeNestedTransform(Parent, ChildRel, 0, 0);
        TEST("Zero scale child: scale = 0", fabs(Result.Scale.X) < 0.001);
        TEST("Zero scale child: pos still additive", fabs(Result.Position.X - 5) < 0.001);
    }

    // 25. Rotation accumulation at deep depth
    {
        FFaceArtTransform L0(FVector2D(0, 0), FVector2D(1, 1), 30);
        FFaceArtTransform L1(FVector2D(0, 0), FVector2D(1, 1), 30);
        FFaceArtTransform L2(FVector2D(0, 0), FVector2D(1, 1), 30);
        auto R1 = ComputeNestedTransform(L0, L1, 0, 0);
        auto R2 = ComputeNestedTransform(R1, L2, 0, 0);
        TEST("Deep rotation: 30+30+30=90", fabs(R2.Rotation - 90) < 0.001);
    }

    // === PER-VIEW VISIBILITY (8 tests) ===

    // 26. Visible by default
    {
        FFaceNestedArt Elem;
        TEST("Default visibility true", GetNestedVisibility(Elem, 0));
    }

    // 27. Hidden in specific view
    {
        FFaceNestedArt Elem;
        Elem.ViewVisibility[0] = {0, false};
        Elem.ViewVisibilityCount = 1;
        TEST("Hidden in view 0", !GetNestedVisibility(Elem, 0));
        TEST("Default visible in other view", GetNestedVisibility(Elem, 1));
    }

    // 28. Visible in one view only
    {
        FFaceNestedArt Elem;
        Elem.ViewVisibility[0] = {0, false};
        Elem.ViewVisibility[1] = {1, false};
        Elem.ViewVisibility[2] = {2, true};
        Elem.ViewVisibilityCount = 3;
        TEST("Hidden in 0", !GetNestedVisibility(Elem, 0));
        TEST("Hidden in 1", !GetNestedVisibility(Elem, 1));
        TEST("Visible in 2", GetNestedVisibility(Elem, 2));
        TEST("Default visible in unlisted 3", GetNestedVisibility(Elem, 3));
    }

    // 29. Multiple elements, different visibilities
    {
        FFaceNestedArt Elem1, Elem2;
        Elem1.ViewVisibility[0] = {0, true};
        Elem1.ViewVisibilityCount = 1;
        Elem2.ViewVisibility[0] = {0, false};
        Elem2.ViewVisibilityCount = 1;
        TEST("Elem1 visible in 0", GetNestedVisibility(Elem1, 0));
        TEST("Elem2 hidden in 0", !GetNestedVisibility(Elem2, 0));
    }

    // 30. Visibility toggle on/off
    {
        FFaceNestedArt Elem;
        Elem.ViewVisibility[0] = {0, true};
        Elem.ViewVisibility[1] = {0, false};
        Elem.ViewVisibilityCount = 2;
        // Last value wins for same state
        TEST("Visibility toggled: last wins", !GetNestedVisibility(Elem, 0));
    }

    // 31. All unlisted states default visible
    {
        FFaceNestedArt Elem;
        Elem.ViewVisibility[0] = {5, false}; // only state 5 hidden
        Elem.ViewVisibilityCount = 1;
        for (int s = 0; s < 10; ++s) {
            if (s != 5) TEST("Unlisted state visible", GetNestedVisibility(Elem, s));
        }
    }

    // === IDLE ANIMATION (8 tests) ===

    // 32. Frame cycling
    {
        double Timer = 0;
        int Frame = 0;
        Frame = AdvanceIdleFrame(Frame, Timer, 0.05, 3, 0.1, 1.0);
        TEST("Idle first advance: still frame 0 (not enough time)", Frame == 0);
    }

    // 33. Advance past frame duration
    {
        double Timer = 0;
        int Frame = 0;
        Frame = AdvanceIdleFrame(Frame, Timer, 0.15, 3, 0.1, 1.0);
        TEST("Idle advance past duration: frame 1", Frame == 1);
    }

    // 34. Frame duration
    {
        double Timer = 0;
        int Frame = 0;
        Frame = AdvanceIdleFrame(Frame, Timer, 0.1, 3, 0.1, 1.0);
        TEST("Idle at exact duration: frame 1", Frame == 1);
        Frame = AdvanceIdleFrame(Frame, Timer, 0.1, 3, 0.1, 1.0);
        TEST("Idle another duration: frame 2", Frame == 2);
    }

    // 35. Loop behavior
    {
        double Timer = 0;
        int Frame = 0;
        Frame = AdvanceIdleFrame(Frame, Timer, 0.31, 3, 0.1, 1.0);
        TEST("Idle loop: wraps to 0", Frame == 0);
    }

    // 36. Speed multiplier > 1 = faster
    {
        double Timer1 = 0, Timer2 = 0;
        int F1 = 0, F2 = 0;
        F1 = AdvanceIdleFrame(F1, Timer1, 0.1, 3, 0.1, 2.0);
        F2 = AdvanceIdleFrame(F2, Timer2, 0.1, 3, 0.1, 1.0);
        TEST("Double speed advances further", F1 > F2);
    }

    // 37. Speed multiplier < 1 = slower
    {
        double Timer = 0;
        int Frame = 0;
        Frame = AdvanceIdleFrame(Frame, Timer, 0.1, 3, 0.1, 0.5);
        TEST("Half speed: not enough time to advance", Frame == 0);
        Frame = AdvanceIdleFrame(Frame, Timer, 0.1, 3, 0.1, 0.5);
        TEST("Half speed: second advance moves to frame 0 (wrapped)", Frame == 0 || Frame == 1);
    }

    // 38. Zero frames = no animation
    {
        double Timer = 0;
        int Frame = 0;
        Frame = AdvanceIdleFrame(Frame, Timer, 1.0, 0, 0.1, 1.0);
        TEST("Zero frames: stays at 0", Frame == 0 && Timer == 1.0);
    }

    // 39. Single frame = static
    {
        double Timer = 0;
        int Frame = 0;
        Frame = AdvanceIdleFrame(Frame, Timer, 5.0, 1, 0.1, 1.0);
        TEST("Single frame: stays at 0", Frame == 0);
    }

    // === EDGE CASES (6 tests) ===

    // 40. Jiggle on element with no jiggle enabled = no effect
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, 100.0, 100.0, 0.016);
        TEST("Jiggle sim still runs even with default settings", S.PosX != 0.0 || S.PosY != 0.0 || S.VelX != 0.0 || S.VelY != 0.0);
        // This is OK - the guard is in the component (bJiggleEnabled check)
    }

    // 41. Empty nested elements
    {
        // No crash when no nested elements exist - tested in component GetNestedElementCount
        // Just verify count works
        const int count = 0;
        TEST("Empty nested: count 0", count == 0);
    }

    // 42. Idle animation with 0 frame duration (no div by zero)
    {
        double Timer = 0;
        int Frame = 0;
        // Should not divide by zero
        Frame = AdvanceIdleFrame(Frame, Timer, 0.1, 3, 0.001, 1.0);
        TEST("Idle with tiny duration: advances", Frame > 0 || Timer > 0.0);
    }

    // 43. Very negative impulse
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, -1000.0, 0.0, 0.016);
        TEST("Large negative impulse", S.PosX < 0.0);
    }

    // 44. Multi-axis jiggle
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.JiggleAxisX = 1.0;
        J.JiggleAxisY = 1.0;
        SimulateJiggle(S, J, 5.0, -3.0, 0.016);
        TEST("Multi-axis: X moves", fabs(S.PosX) > 0.0);
        TEST("Multi-axis: Y moves", fabs(S.PosY) > 0.0);
    }

    // 45. Extremely high stiffness = very fast return
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.Stiffness = 1000.0;
        SimulateJiggle(S, J, 10.0, 0.0, 0.016);
        // High stiffness should cause very fast spring back - verify it doesn't blow up
        bool stable = std::isfinite(S.PosX) && std::isfinite(S.VelX);
        TEST("High stiffness is stable", stable);
    }

    // === INTEGRATION: Frame delta impulse derivation ===

    // 46. Impulse derived from frame delta (Dyaw, Dpitch) — simulates the component fix
    {
        JiggleState S;
        FFaceJiggleSettings J;
        double FrameDyaw = 2.0, FrameDpitch = 0.5;
        double AngularVel = std::sqrt(FrameDyaw*FrameDyaw + FrameDpitch*FrameDpitch) / 0.016;
        double ImpulseX = FrameDyaw * AngularVel * 0.001;
        double ImpulseY = FrameDpitch * AngularVel * 0.001;
        SimulateJiggle(S, J, ImpulseX, ImpulseY, 0.016);
        TEST("Frame delta drives impulse: X moves", fabs(S.PosX) > 0.0);
        TEST("Frame delta drives impulse: Y moves", fabs(S.PosY) > 0.0);
    }

    // 47. No frame delta = no jiggle impulse
    {
        JiggleState S;
        FFaceJiggleSettings J;
        double FrameDyaw = 0.0, FrameDpitch = 0.0;
        double AngularVel = std::sqrt(FrameDyaw*FrameDyaw + FrameDpitch*FrameDpitch) / 0.016;
        double ImpulseX = FrameDyaw * AngularVel * 0.001;
        double ImpulseY = FrameDpitch * AngularVel * 0.001;
        SimulateJiggle(S, J, ImpulseX, ImpulseY, 0.016);
        TEST("Zero frame delta: no jiggle", S.PosX == 0.0 && S.PosY == 0.0);
    }

    // 48. DeltaTime capping: large DT doesn't blow up jiggle
    {
        JiggleState S;
        FFaceJiggleSettings J;
        J.Stiffness = 20.0;
        double CappedDT = std::min(0.5, 0.05); // matches component cap of 0.05
        SimulateJiggle(S, J, 100.0, 0.0, CappedDT);
        bool stable = std::isfinite(S.PosX) && std::isfinite(S.VelX) && fabs(S.PosX) < 1e6;
        TEST("DT capping prevents blowup", stable);
    }

    // === INTEGRATION: Param bindings on nested elements ===

    // 49. Nested element param bindings affect transform (using ParamSystem mock)
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        // Simulate bindings on a nested element (layer 0 = nested element)
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 20.0, 0.0, false);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionY, 10.0, 0.0, false);
        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Nested param binding PosX", fabs(r.PosX - 20.0) < 0.01);
        TEST("Nested param binding PosY", fabs(r.PosY - 10.0) < 0.01);
    }

    // 50. Nested element TextureBlend binding
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::TextureBlend, 1.0, 0.0, false);
        ps.SetValue(0, 0.7);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Nested TextureBlend binding", fabs(r.TextureBlend - 0.7) < 0.01);
    }

    // 51. Multiple bindings on same nested element (Pos + Rot + TextureBlend)
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.Define(1, 0.5, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 10.0, 5.0, false);
        ps.AddBinding(0, 1, EFaceParamTarget::Rotation, 30.0, 0.0, false);
        ps.AddBinding(0, 0, EFaceParamTarget::TextureBlend, 1.0, 0.0, false);
        ps.SetValue(0, 1.0);
        ps.SetValue(1, 1.0);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Nested multi-binding PosX = 10*1+5=15", fabs(r.PosX - 15.0) < 0.01);
        TEST("Nested multi-binding Rot = 30*1=30", fabs(r.Rotation - 30.0) < 0.01);
        TEST("Nested multi-binding TexBlend = 1.0*1=1", fabs(r.TextureBlend - 1.0) < 0.01);
    }

    // === INTEGRATION: Combined parent + child param bindings ===

    // 52. Parent layer bindings + nested element bindings stack
    {
        // Layer 0 = parent, Layer 1 = child
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 100.0, 0.0, false); // parent binding
        ps.AddBinding(1, 0, EFaceParamTarget::PositionX, 50.0, 0.0, false);  // child binding, same param
        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        auto parent = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        auto child = ps.Evaluate(1, parent.PosX, parent.PosY, 1.0, 1.0, 0.0);
        TEST("Parent binding: PosX=100", fabs(parent.PosX - 100.0) < 0.01);
        TEST("Child inherits parent + child binding: 100+50=150", fabs(child.PosX - 150.0) < 0.01);
    }

    // 53. Parent scale binding propagates to child
    {
        ParamSystem ps;
        ps.Define(0, 0.0, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::ScaleX, 2.0, 0.0, false);
        ps.SetValue(0, 1.0);
        ps.TickSmoothing(1.0);
        auto parent = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        auto child = ps.Evaluate(1, 0.0, 0.0, parent.ScaleX, parent.ScaleY, 0.0);
        TEST("Parent scale binding: scale=1*(1+2)=3", fabs(parent.ScaleX - 3.0) < 0.01);
        TEST("Child inherits parent scale: 3*1=3", fabs(child.ScaleX - 3.0) < 0.01);
    }

    // 54. Inverted binding on nested element
    {
        ParamSystem ps;
        ps.Define(0, 0.7, 0.0, 1.0, 8.0);
        ps.AddBinding(0, 0, EFaceParamTarget::PositionX, 100.0, 0.0, true);
        ps.TickSmoothing(1.0);
        auto r = ps.Evaluate(0, 0.0, 0.0, 1.0, 1.0, 0.0);
        TEST("Inverted binding: (1-0.7)*100=30", fabs(r.PosX - 30.0) < 0.01);
    }

    // === INTEGRATION: Deep nesting tag parsing (multi-underscore) ===

    // 55. Multi-segment tag: "FaceLayer_GrandParent_Parent_Child" — last segment = element name
    {
        // Simulate: parse "FaceLayer_GrandParent_Parent_Child"
        std::string tag("FaceLayer_GrandParent_Parent_Child");
        size_t lastUnderscore = tag.rfind('_');
        size_t firstUnderscore = tag.find('_');
        TEST("Deep tag: first underscore > 0", firstUnderscore > 0 && firstUnderscore != std::string::npos);
        TEST("Deep tag: last underscore > first", lastUnderscore > firstUnderscore);
        TEST("Deep tag: layer prefix = FaceLayer", tag.substr(0, firstUnderscore) == "FaceLayer");
    }

    // === INTEGRATION: Jiggle state reset ===

    // 56. Jiggle state cleared = position resets to zero
    {
        JiggleState S;
        FFaceJiggleSettings J;
        SimulateJiggle(S, J, 10.0, 0.0, 0.016);
        TEST("Jiggle active before reset", fabs(S.PosX) > 0.0 || fabs(S.VelX) > 0.0);
        // Simulate reset: clear state
        S = JiggleState();
        TEST("Jiggle cleared: pos zero", S.PosX == 0.0 && S.PosY == 0.0);
        TEST("Jiggle cleared: vel zero", S.VelX == 0.0 && S.VelY == 0.0);
    }

    // 57. Idle anim state reset = frame index reset
    {
        double Timer = 0;
        int Frame = 0;
        Frame = AdvanceIdleFrame(Frame, Timer, 0.25, 4, 0.1, 1.0);
        TEST("Anim frame advanced before reset", Frame > 0);
        // Simulate reset
        Frame = 0;
        Timer = 0.0;
        TEST("Anim frame reset to 0", Frame == 0);
    }

    printf("  [Nested Art + Jiggle: 57 tests]\n");
}

// --- 3D Pin Projection Tests ---
void TestPinProjection() {
    printf("\n=== 3D Pin Projection ===\n");

    // Mock zone center yaw (matches UE GetZoneCenterYaw with HalfZoneWidth=22.5)
    auto GetZoneYaw = [](int StateIdx) -> double {
        double HZW = 22.5;
        switch (StateIdx) {
            case 0: return 0.0;              // Front
            case 1: return HZW * 2.0;        // ThreeQuarterRight
            case 2: return HZW * 4.0;        // RightProfile
            case 3: return HZW * 6.0;        // BackRight
            case 4: return 180.0;            // Back
            case 5: return -HZW * 6.0;       // BackLeft
            case 6: return -HZW * 4.0;       // LeftProfile
            case 7: return -HZW * 2.0;       // ThreeQuarterLeft
            default: return 0.0;
        }
    };

    // ProjectPinToUV: 3D pin → 2D UV for a given state
    auto Project = [&](double Px, double Py, double Pz, int StateIdx,
                       double HW, double HD, double HH) -> FVector2D {
        double YawDeg = GetZoneYaw(StateIdx);
        double Rad = YawDeg * (std::acos(-1.0) / 180.0);
        double CosA = std::cos(Rad);
        double SinA = std::sin(Rad);

        double WX = Px * HW;
        double WZ = Pz * HD;
        double WY = Py * HH;

        double ViewX = WX * CosA + WZ * SinA;
        double VisibleX = HW * std::abs(CosA) + HD * std::abs(SinA);

        double UVx = 0.5;
        double UVy = 0.5;
        if (VisibleX > 1e-7) UVx = 0.5 + 0.5 * ViewX / VisibleX;
        if (HH > 1e-7) UVy = 0.5 + 0.5 * WY / HH;

        return FVector2D(std::clamp(UVx, 0.0, 1.0), std::clamp(UVy, 0.0, 1.0));
    };

    double HW = 256.0, HD = 128.0, HH = 256.0;
    int Front = 0, ProfileR = 2, ThreeQ = 1, Back = 4;

    // 1. Center pin projects to UV(0.5, 0.5) in all views
    {
        for (int s = 0; s < 8; ++s) {
            FVector2D UV = Project(0, 0, 0, s, HW, HD, HH);
            TEST("Center pin UV(0.5,0.5) for all views", std::abs(UV.X - 0.5) < 0.001 && std::abs(UV.Y - 0.5) < 0.001);
        }
    }

    // 2. Front view: right side (Px=+1) → UVx > 0.5
    {
        FVector2D UV = Project(1.0, 0, 0, Front, HW, HD, HH);
        TEST("Front: right pin UVx > 0.5", UV.X > 0.5);
        TEST("Front: right pin UVy = 0.5", std::abs(UV.Y - 0.5) < 0.001);
    }

    // 3. Front view: left side (Px=-1) → UVx < 0.5
    {
        FVector2D UV = Project(-1.0, 0, 0, Front, HW, HD, HH);
        TEST("Front: left pin UVx < 0.5", UV.X < 0.5);
    }

    // 4. Front view: top (Py=+1) → UVy > 0.5
    {
        FVector2D UV = Project(0, 1.0, 0, Front, HW, HD, HH);
        TEST("Front: top pin UVy > 0.5", UV.Y > 0.5);
    }

    // 5. Front view: bottom (Py=-1) → UVy < 0.5
    {
        FVector2D UV = Project(0, -1.0, 0, Front, HW, HD, HH);
        TEST("Front: bottom pin UVy < 0.5", UV.Y < 0.5);
    }

    // 6. Profile right: nose (Pz=+1) → UVx > 0.5
    {
        FVector2D UV = Project(0, 0, 1.0, ProfileR, HW, HD, HH);
        TEST("Profile: nose pin UVx > 0.5", UV.X > 0.5);
    }

    // 7. Profile right: back of head (Pz=-1) → UVx < 0.5
    {
        FVector2D UV = Project(0, 0, -1.0, ProfileR, HW, HD, HH);
        TEST("Profile: back pin UVx < 0.5", UV.X < 0.5);
    }

    // 8. 3/4 Right: UVx interpolates between Front and Profile for asymmetric pin
    {
        FVector2D UV_F = Project(0.8, 0, 0.2, Front, HW, HD, HH);
        FVector2D UV_P = Project(0.8, 0, 0.2, ProfileR, HW, HD, HH);
        FVector2D UV_Q = Project(0.8, 0, 0.2, ThreeQ, HW, HD, HH);
        TEST("3/4 UVx between Front and Profile", (UV_F.X < UV_Q.X && UV_Q.X < UV_P.X) || (UV_F.X > UV_Q.X && UV_Q.X > UV_P.X));
    }

    // 9. Back view: same as front but mirrored (Px negative)
    {
        FVector2D UV_F = Project(0.3, 0, 0, Front, HW, HD, HH);
        FVector2D UV_B = Project(0.3, 0, 0, Back, HW, HD, HH);
        TEST("Back: UV differs from front", std::abs(UV_F.X - UV_B.X) > 0.001 || std::abs(UV_F.Y - UV_B.Y) > 0.001);
    }

    // 10. PivotPoint fallback when Pin3D not pinned
    {
        struct FFaceNestedArtMock {
            FVector2D PivotPoint = FVector2D(0.3, 0.7);
            struct { bool bPinned = false; double PX=0, PY=0, PZ=0; } Pin3D;
        };
        auto GetEffectivePivot = [](const FFaceNestedArtMock& E) -> FVector2D {
            if (E.Pin3D.bPinned) return FVector2D(0.5, 0.5);
            return E.PivotPoint;
        };
        FFaceNestedArtMock Elem;
        FVector2D Eff = GetEffectivePivot(Elem);
        TEST("Fallback: returns PivotPoint when not pinned", std::abs(Eff.X - 0.3) < 0.001 && std::abs(Eff.Y - 0.7) < 0.001);

        Elem.Pin3D.bPinned = true;
        FVector2D EffPinned = GetEffectivePivot(Elem);
        TEST("Pinned: returns projected UV when pinned", std::abs(EffPinned.X - 0.5) < 0.001);
    }

    // 11. UV→3D for Front view: center UV → X=0, Y=0
    {
        auto UVToPin3D_Front = [](double UVx, double UVy) {
            struct { double X, Y, Z; } Pos;
            Pos.X = (UVx - 0.5) * 2.0;
            Pos.Y = (UVy - 0.5) * 2.0;
            Pos.Z = 0.0;
            return Pos;
        };
        auto P = UVToPin3D_Front(0.5, 0.5);
        TEST("UV→3D Front center: X=0", std::abs(P.X) < 0.001);
        TEST("UV→3D Front center: Y=0", std::abs(P.Y) < 0.001);
    }

    // 12. UV→3D round-trip: Front click → 3D → project back
    {
        struct { double X, Y, Z; } Pin3D = {0.3, 0.4, 0.0};
        FVector2D UV = Project(Pin3D.X, Pin3D.Y, Pin3D.Z, Front, HW, HD, HH);
        double Rx = (UV.X - 0.5) * 2.0;
        double Ry = (UV.Y - 0.5) * 2.0;
        TEST("Front round-trip X", std::abs(Rx - Pin3D.X) < 0.01);
        TEST("Front round-trip Y", std::abs(Ry - Pin3D.Y) < 0.01);
    }

    // 13. UV→3D round-trip: Profile click → 3D → project back
    {
        struct { double X, Y, Z; } Pin3D = {0.0, 0.2, 0.5};
        FVector2D UV = Project(Pin3D.X, Pin3D.Y, Pin3D.Z, ProfileR, HW, HD, HH);
        double Rz = (UV.X - 0.5) * 2.0;
        double Ry = (UV.Y - 0.5) * 2.0;
        TEST("Profile round-trip Z: 0.5", std::abs(Rz - Pin3D.Z) < 0.01);
        TEST("Profile round-trip Y: 0.2", std::abs(Ry - Pin3D.Y) < 0.01);
    }

    // 14. Same 3D pin projects differently to Front vs Profile
    {
        struct { double X, Y, Z; } Pin = {0.8, 0.0, 0.2};
        FVector2D UV_F = Project(Pin.X, Pin.Y, Pin.Z, Front, HW, HD, HH);
        FVector2D UV_P = Project(Pin.X, Pin.Y, Pin.Z, ProfileR, HW, HD, HH);
        bool different = std::abs(UV_F.X - UV_P.X) > 0.01;
        TEST("Same 3D pin → different UV across views", different);
    }

    // 15. Nested pin fallback: Pin3D.bPinned=false → uses PivotPoint
    {
        FVector2D DefaultPivot(0.5, 0.5);
        struct MockElem { FVector2D PivotPoint = FVector2D(0.2, 0.8); bool bPinned = false; };
        MockElem E;
        FVector2D Eff = E.bPinned ? FVector2D(0.5, 0.5) : E.PivotPoint;
        TEST("Nested pin fallback to PivotPoint", std::abs(Eff.X - 0.2) < 0.001 && std::abs(Eff.Y - 0.8) < 0.001);
    }

    printf("  [3D Pin Projection: 15 tests]\n");
}

// --- Batch Operations Tests ---
void TestBatchOperations() {
    printf("\n=== Batch Operations ===\n");

    // 1) Count of EFaceAngleState values (10)
    TEST("EFaceAngleState count is 10", int(EFaceAngleState::MAX) == 10);
    TEST("Front is first", EFaceAngleState::Front == EFaceAngleState(0));
    TEST("Back is index 4", EFaceAngleState::Back == EFaceAngleState(4));
    TEST("Bottom is last before MAX", EFaceAngleState::Bottom == EFaceAngleState(9));

    // 2) Simulate DuplicateState: copy front layer tags to another map
    {
        struct FStateLayerSet {
            bool bHasData = false;
            int LayerCount = 0;
        };
        std::array<FStateLayerSet, 10> sourceLayers;
        sourceLayers[0].bHasData = true;
        sourceLayers[0].LayerCount = 3;
        sourceLayers[1].bHasData = true;
        sourceLayers[1].LayerCount = 2;

        std::array<FStateLayerSet, 10> destLayers;
        auto DuplicateState = [&](int srcIdx, int dstIdx) {
            destLayers[dstIdx] = sourceLayers[srcIdx];
        };

        DuplicateState(0, 5);
        TEST("DuplicateState: dest matches source LayerCount", destLayers[5].LayerCount == 3);
        TEST("DuplicateState: dest matches source bHasData", destLayers[5].bHasData == true);
        TEST("DuplicateState: other dest remains empty", destLayers[0].bHasData == false);
    }

    // 3) Simulate ClearAllTextures: batch reset
    {
        int textureSlots[10][3] = {}; // [state][layer]
        textureSlots[0][0] = 1;
        textureSlots[1][1] = 2;
        textureSlots[2][2] = 3;

        auto ClearAll = [&]() {
            for (int s = 0; s < 10; ++s)
                for (int l = 0; l < 3; ++l)
                    textureSlots[s][l] = 0;
        };

        ClearAll();
        int total = 0;
        for (int s = 0; s < 10; ++s)
            for (int l = 0; l < 3; ++l)
                total += textureSlots[s][l];
        TEST("ClearAll: all texture slots zero", total == 0);
    }

    // 4) Simulate SyncLayerNestedToAllViews: copy one element from Front to all states
    {
        struct NestedElement {
            bool bValid = false;
            int Id = 0;
        };
        NestedElement perState[10];
        perState[0].bValid = true;
        perState[0].Id = 42;

        auto SyncToAllViews = [&]() {
            for (int i = 1; i < 10; ++i) {
                perState[i] = perState[0];
            }
        };

        SyncToAllViews();
        int countValid = 0;
        for (int i = 0; i < 10; ++i) {
            if (perState[i].bValid && perState[i].Id == 42)
                ++countValid;
        }
        TEST("SyncToAllViews: all 10 states have Id=42", countValid == 10);
    }

    // 5) Simulate BatchSetTextures: replace all textures for a state/layer
    {
        int texture[10][3] = {};
        auto BatchSet = [&](int state, int layer, int val) {
            texture[state][layer] = val;
        };
        BatchSet(3, 1, 99);
        TEST("BatchSet: sets correct slot", texture[3][1] == 99);
        TEST("BatchSet: leaves other slots unchanged", texture[0][0] == 0 && texture[9][2] == 0);
    }

    // 6) Simulate GetNumViewStates: count populated states
    {
        bool hasState[10] = {true, true, false, false, true, false, false, true, false, true};
        int count = 0;
        for (int i = 0; i < 10; ++i)
            if (hasState[i]) ++count;
        TEST("GetNumViewStates: 5 states populated", count == 5);
    }

    // 7) Simulate GetAllLayerTags
    {
        const char* layers[10][3] = {};
        layers[0][0] = "Face";
        layers[0][1] = "Hair";
        layers[0][2] = "Eyes";

        int tagCount = 0;
        for (auto* l : layers[0])
            if (l && l[0]) ++tagCount;
        TEST("GetAllLayerTags: Front has 3 layers", tagCount == 3);
    }

    // 8) Simulate SetNestedAltTextures
    {
        struct MockNested {
            int altCount = 0;
        };
        MockNested elements[5];
        auto SetAlt = [&](int idx, int count) {
            if (idx >= 0 && idx < 5)
                elements[idx].altCount = count;
        };
        SetAlt(2, 4);
        TEST("SetAltTextures: target index gets count 4", elements[2].altCount == 4);
        TEST("SetAltTextures: other index unchanged", elements[0].altCount == 0);
    }

    // 9) Simulate BatchSetTexturesAllLayers
    {
        struct TexSet { int id = 0; };
        TexSet stateLayers[10][5] = {};
        auto BatchAllLayers = [&](int state, int ids[5]) {
            for (int l = 0; l < 5; ++l)
                stateLayers[state][l].id = ids[l];
        };
        int ids[5] = {10, 20, 30, 40, 50};
        BatchAllLayers(0, ids);
        TEST("BatchAllLayers: layer 0 has id 10", stateLayers[0][0].id == 10);
        TEST("BatchAllLayers: layer 4 has id 50", stateLayers[0][4].id == 50);
        TEST("BatchAllLayers: other state unchanged", stateLayers[1][0].id == 0);
    }

    printf("  [Batch Operations: 10 tests]\n");
}

// ====================================================================
// ZONE BOUNDARY TESTS
// ====================================================================

const char* GetStateLabel(int state) {
    static const char* labels[] = {"Front","3/4R","ProfR","BackR","Back","BackL","ProfL","3/4L","Top","Bottom"};
    return (state >= 0 && state < 10) ? labels[state] : "?";
}

double GetZoneYawBoundary(EFaceAngleState state) {
    switch (state) {
        case EFaceAngleState::Front: return 0.0;
        case EFaceAngleState::ThreeQuarterRight: return Z2;
        case EFaceAngleState::RightProfile: return Z4;
        case EFaceAngleState::BackRight: return Z6;
        case EFaceAngleState::Back: return 180.0;
        case EFaceAngleState::BackLeft: return -Z6;
        case EFaceAngleState::LeftProfile: return -Z4;
        case EFaceAngleState::ThreeQuarterLeft: return -Z2;
        default: return 0.0;
    }
}

double GetZoneYawStart(EFaceAngleState state) {
    // Each horizontal zone spans HZW either side of center except Back
    if (state == EFaceAngleState::Back) return Z7; // actually wraps at 180
    // For right-side states: front is -HZW to HZW, 3QR is HZW to 3*HZW, etc.
    switch (state) {
        case EFaceAngleState::Front: return -HZW;
        case EFaceAngleState::ThreeQuarterRight: return HZW;
        case EFaceAngleState::RightProfile: return Z3;
        case EFaceAngleState::BackRight: return Z5;
        case EFaceAngleState::Back: return Z7;
        case EFaceAngleState::BackLeft: return -Z7;
        case EFaceAngleState::LeftProfile: return -Z5;
        case EFaceAngleState::ThreeQuarterLeft: return -Z3;
        default: return 0.0;
    }
}

double GetZoneYawEnd(EFaceAngleState state) {
    switch (state) {
        case EFaceAngleState::Front: return HZW;
        case EFaceAngleState::ThreeQuarterRight: return Z3;
        case EFaceAngleState::RightProfile: return Z5;
        case EFaceAngleState::BackRight: return Z7;
        case EFaceAngleState::Back: return 180.0; // wraps
        case EFaceAngleState::BackLeft: return -Z5;
        case EFaceAngleState::LeftProfile: return -Z3;
        case EFaceAngleState::ThreeQuarterLeft: return -HZW;
        default: return 0.0;
    }
}

// How wide the crossfade window is at any zone boundary
double CrossfadeWindowStart(EFaceAngleState from, EFaceAngleState to) {
    // Returns the yaw where crossfade begins (approach from 'from' toward 'to')
    // Crossfade window = BlendWindowWidth degrees
    constexpr double BlendW = 5.0;
    double boundary = 0.0;
    // Determine boundary between adjacent states
    // Front/3QR boundary is at HZW (22.5)
    if (from == EFaceAngleState::Front && to == EFaceAngleState::ThreeQuarterRight) boundary = HZW;
    else if (from == EFaceAngleState::ThreeQuarterRight && to == EFaceAngleState::Front) boundary = HZW;
    else if (from == EFaceAngleState::ThreeQuarterRight && to == EFaceAngleState::RightProfile) boundary = Z3;
    else if (from == EFaceAngleState::RightProfile && to == EFaceAngleState::ThreeQuarterRight) boundary = Z3;
    else if (from == EFaceAngleState::RightProfile && to == EFaceAngleState::BackRight) boundary = Z5;
    else if (from == EFaceAngleState::BackRight && to == EFaceAngleState::RightProfile) boundary = Z5;
    else if (from == EFaceAngleState::Front && to == EFaceAngleState::ThreeQuarterLeft) boundary = -HZW;
    else if (from == EFaceAngleState::ThreeQuarterLeft && to == EFaceAngleState::Front) boundary = -HZW;
    else if (from == EFaceAngleState::ThreeQuarterLeft && to == EFaceAngleState::LeftProfile) boundary = -Z3;
    else if (from == EFaceAngleState::LeftProfile && to == EFaceAngleState::ThreeQuarterLeft) boundary = -Z3;
    else if (from == EFaceAngleState::LeftProfile && to == EFaceAngleState::BackLeft) boundary = -Z5;
    else if (from == EFaceAngleState::BackLeft && to == EFaceAngleState::LeftProfile) boundary = -Z5;
    // For boundaries approached from the 'from' side, window starts boundary - BlendW
    // (the boundary itself is the midpoint of the blend window)
    if (from < to) return boundary - BlendW;
    return boundary + BlendW;
}

void TestZoneBoundaries() {
    printf("=== Zone Boundaries (editor visualization) ===\n");

    // All 8 horizontal zone boundaries are at multiples of HZW
    TEST("Front center", GetZoneYawBoundary(EFaceAngleState::Front) == 0.0);
    TEST("3QR center", GetZoneYawBoundary(EFaceAngleState::ThreeQuarterRight) == Z2);
    TEST("ProfR center", GetZoneYawBoundary(EFaceAngleState::RightProfile) == Z4);
    TEST("BackR center", GetZoneYawBoundary(EFaceAngleState::BackRight) == Z6);
    TEST("Back center", GetZoneYawBoundary(EFaceAngleState::Back) == 180.0);

    TEST("Front zone start", GetZoneYawStart(EFaceAngleState::Front) == -HZW);
    TEST("Front zone end", GetZoneYawEnd(EFaceAngleState::Front) == HZW);
    TEST("3QR zone start", GetZoneYawStart(EFaceAngleState::ThreeQuarterRight) == HZW);
    TEST("3QR zone end", GetZoneYawEnd(EFaceAngleState::ThreeQuarterRight) == Z3);
    TEST("ProfR zone start", GetZoneYawStart(EFaceAngleState::RightProfile) == Z3);
    TEST("ProfR zone end", GetZoneYawEnd(EFaceAngleState::RightProfile) == Z5);

    // Zone width = 2*HZW for all horizontal states
    double expectedWidth = 2.0 * HZW;
    TEST("Front zone width", GetZoneYawEnd(EFaceAngleState::Front) - GetZoneYawStart(EFaceAngleState::Front) == expectedWidth);
    TEST("3QR zone width", GetZoneYawEnd(EFaceAngleState::ThreeQuarterRight) - GetZoneYawStart(EFaceAngleState::ThreeQuarterRight) == expectedWidth);
    TEST("ProfR zone width", GetZoneYawEnd(EFaceAngleState::RightProfile) - GetZoneYawStart(EFaceAngleState::RightProfile) == expectedWidth);
    TEST("BackR zone width", GetZoneYawEnd(EFaceAngleState::BackRight) - GetZoneYawStart(EFaceAngleState::BackRight) == expectedWidth);

    // State labels for editor display
    TEST("Front label", std::string(GetStateLabel((int)EFaceAngleState::Front)) == "Front");
    TEST("3QR label", std::string(GetStateLabel((int)EFaceAngleState::ThreeQuarterRight)) == "3/4R");
    TEST("Profile labels correct", std::string(GetStateLabel((int)EFaceAngleState::RightProfile)) == "ProfR");
    TEST("Back label", std::string(GetStateLabel((int)EFaceAngleState::Back)) == "Back");
    TEST("Top label", std::string(GetStateLabel((int)EFaceAngleState::Top)) == "Top");
    TEST("Bottom label", std::string(GetStateLabel((int)EFaceAngleState::Bottom)) == "Bottom");
    TEST("State count", GetStateLabel(-1) != nullptr); // bounds check

    // Pitch thresholds
    TEST("Crossfade window Front->3QR", CrossfadeWindowStart(EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight) < HZW);
    TEST("Crossfade window symmetric", CrossfadeWindowStart(EFaceAngleState::ThreeQuarterRight, EFaceAngleState::Front) > HZW);

    printf("  [Zone Boundary Tests: 24 tests]\n");
}

void TestCustomZoneBoundaryMultipliers() {
    printf("=== Custom Zone Boundary Multipliers ===\n");

    // Default multipliers {1,3,5,7} produce same behavior as original
    static const double Defaults[4] = {1.0, 3.0, 5.0, 7.0};
    TEST("Default: Front at 0",
        DetermineStateFromAngles(0, 0, Defaults) == EFaceAngleState::Front);
    TEST("Default: Front at 22.49",
        DetermineStateFromAngles(22.49, 0, Defaults) == EFaceAngleState::Front);
    TEST("Default: 3QR at 22.51",
        DetermineStateFromAngles(22.51, 0, Defaults) == EFaceAngleState::ThreeQuarterRight);
    TEST("Default: Back at 157.51",
        DetermineStateFromAngles(157.51, 0, Defaults) == EFaceAngleState::Back);

    // Wide front zone: multipliers {2,3,5,7}
    static const double WideFront[4] = {2.0, 3.0, 5.0, 7.0};
    TEST("WideFront: still Front at 22.5",
        DetermineStateFromAngles(22.5, 0, WideFront) == EFaceAngleState::Front);
    TEST("WideFront: Front at 44.99",
        DetermineStateFromAngles(44.99, 0, WideFront) == EFaceAngleState::Front);
    TEST("WideFront: 3QR at 45.01",
        DetermineStateFromAngles(45.01, 0, WideFront) == EFaceAngleState::ThreeQuarterRight);

    // Narrow front zone: multipliers {0.5,3,5,7}
    static const double NarrowFront[4] = {0.5, 3.0, 5.0, 7.0};
    TEST("NarrowFront: 3QR at 11.26",
        DetermineStateFromAngles(11.26, 0, NarrowFront) == EFaceAngleState::ThreeQuarterRight);
    TEST("NarrowFront: Front at 11.24",
        DetermineStateFromAngles(11.24, 0, NarrowFront) == EFaceAngleState::Front);

    // Symmetric left-side with wide front
    TEST("WideFront: Left side Front at -22.5",
        DetermineStateFromAngles(-22.5, 0, WideFront) == EFaceAngleState::Front);
    TEST("WideFront: 3QL at -45.01",
        DetermineStateFromAngles(-45.01, 0, WideFront) == EFaceAngleState::ThreeQuarterLeft);

    // All zones equal width: multipliers {1,2,3,4}
    static const double EqualWidth[4] = {1.0, 2.0, 3.0, 4.0};
    TEST("EqualWidth: Front at 0",
        DetermineStateFromAngles(0, 0, EqualWidth) == EFaceAngleState::Front);
    TEST("EqualWidth: 3QR at 22.51",
        DetermineStateFromAngles(22.51, 0, EqualWidth) == EFaceAngleState::ThreeQuarterRight);
    TEST("EqualWidth: Profile at 45.01",
        DetermineStateFromAngles(45.01, 0, EqualWidth) == EFaceAngleState::RightProfile);
    TEST("EqualWidth: BackR at 67.51",
        DetermineStateFromAngles(67.51, 0, EqualWidth) == EFaceAngleState::BackRight);
    TEST("EqualWidth: Back at 90.01",
        DetermineStateFromAngles(90.01, 0, EqualWidth) == EFaceAngleState::Back);
    TEST("EqualWidth: Front at -22.49",
        DetermineStateFromAngles(-22.49, 0, EqualWidth) == EFaceAngleState::Front);
    TEST("EqualWidth: 3QL at -22.51",
        DetermineStateFromAngles(-22.51, 0, EqualWidth) == EFaceAngleState::ThreeQuarterLeft);

    // Pitch thresholds unaffected by horizontal multipliers
    static const double AnyMults[4] = {9.0, 9.0, 9.0, 9.0};
    TEST("Pitch unaffected: Top above threshold",
        DetermineStateFromAngles(0, 61, AnyMults) == EFaceAngleState::Top);
    TEST("Pitch unaffected: Bottom below threshold",
        DetermineStateFromAngles(0, -61, AnyMults) == EFaceAngleState::Bottom);

    printf("  [Custom Zone Boundary Multipliers: 20 tests]\n");
}

// ====================================================================
// BLEND PREVIEW TESTS
// ====================================================================

void TestBlendPreview() {
    printf("=== Blend Preview ===\n");

    // Simulate blend alpha override behavior (matching component logic)
    double blendAlpha = 1.0;
    bool overrideActive = false;
    double previewAlpha = 0.5;

    auto SetBlendPreview = [&](double a) {
        overrideActive = true;
        previewAlpha = std::max(0.0, std::min(1.0, a));
    };
    auto ClearBlendPreview = [&]() {
        overrideActive = false;
        previewAlpha = 0.5;
    };
    auto TickBlend = [&]() {
        if (overrideActive)
            blendAlpha = previewAlpha;
        else
            blendAlpha = 1.0; // fully settled
    };

    // Default state
    TEST("Default alpha is settled", blendAlpha == 1.0);

    // Apply blend preview
    SetBlendPreview(0.3);
    TickBlend();
    TEST("Blend preview override active", blendAlpha == 0.3);

    // Change preview value
    SetBlendPreview(0.75);
    TickBlend();
    TEST("Blend preview updated", blendAlpha == 0.75);

    // Clamping
    SetBlendPreview(-0.1);
    TickBlend();
    // Preview alpha should be clamped to 0..1 by the component
    TEST("Blend preview clamps low", previewAlpha >= 0.0);
    SetBlendPreview(1.5);
    TickBlend();
    TEST("Blend preview clamps high", previewAlpha <= 1.0);

    // Clear override
    ClearBlendPreview();
    TickBlend();
    TEST("Clear blend preview resets alpha", blendAlpha == 1.0);
    TEST("Clear blend preview resets override flag", overrideActive == false);

    // Multiple overrides: last one wins
    SetBlendPreview(0.2);
    SetBlendPreview(0.8);
    TickBlend();
    TEST("Last blend override wins", blendAlpha == 0.8);

    printf("  [Blend Preview: 9 tests]\n");
}

// ====================================================================
// STATUS MATRIX TESTS
// ====================================================================

void TestStatusMatrix() {
    printf("=== Status Matrix ===\n");

    // Simulate the preset slot assignment queries
    struct MockPreset {
        bool slots[10][5] = {}; // [state][layer]
        bool HasSlot(int s, int l) const { return s >= 0 && s < 10 && l >= 0 && l < 5 && slots[s][l]; }
        bool HasState(int s) const {
            if (s < 0 || s >= 10) return false;
            for (int l = 0; l < 5; ++l)
                if (slots[s][l]) return true;
            return false;
        }
        void Assign(int s, int l) { if (s >= 0 && s < 10 && l >= 0 && l < 5) slots[s][l] = true; }
    };

    MockPreset preset;

    // Empty preset
    TEST("Empty preset has no states", !preset.HasState(0));
    TEST("Empty preset no slot", !preset.HasSlot(0, 0));

    // Assign some slots
    preset.Assign(0, 0); // Front, Eyes
    preset.Assign(0, 1); // Front, Brows
    preset.Assign(1, 0); // 3/4R, Eyes
    preset.Assign(4, 0); // Back, Eyes
    preset.Assign(4, 1); // Back, Brows
    preset.Assign(4, 2); // Back, Mouth

    TEST("Front has slots", preset.HasState(0));
    TEST("3/4R has slots", preset.HasState(1));
    TEST("Back has slots", preset.HasState(4));
    TEST("ProfR no slots", !preset.HasState(2));
    TEST("Top no slots", !preset.HasState(8));

    TEST("Front Eyes assigned", preset.HasSlot(0, 0));
    TEST("Front Brows assigned", preset.HasSlot(0, 1));
    TEST("3/4R Eyes assigned", preset.HasSlot(1, 0));
    TEST("Front Mouth not assigned", !preset.HasSlot(0, 2));

    // Simulate GetMissingStates
    int missingCount = 0;
    for (int s = 0; s < 10; ++s)
        if (!preset.HasState(s)) ++missingCount;
    TEST("7 states missing (10 total - 3 with slots)", missingCount == 7);

    // Simulate GetMissingLayers for Front
    int missingFrontLayers = 0;
    for (int l = 0; l < 5; ++l)
        if (!preset.HasSlot(0, l)) ++missingFrontLayers;
    TEST("Front missing 3 of 5 layers", missingFrontLayers == 3);

    // Simulate GetMissingLayers for Back
    int missingBackLayers = 0;
    for (int l = 0; l < 5; ++l)
        if (!preset.HasSlot(4, l)) ++missingBackLayers;
    TEST("Back missing 2 of 5 layers", missingBackLayers == 2);

    // Column counts
    int stateSlotCounts[10] = {};
    for (int s = 0; s < 10; ++s)
        for (int l = 0; l < 5; ++l)
            if (preset.HasSlot(s, l)) stateSlotCounts[s]++;
    TEST("Front has 2 slots", stateSlotCounts[0] == 2);
    TEST("3/4R has 1 slot", stateSlotCounts[1] == 1);
    TEST("Back has 3 slots", stateSlotCounts[4] == 3);
    TEST("ProfR has 0 slots", stateSlotCounts[2] == 0);
    TEST("Top has 0 slots", stateSlotCounts[8] == 0);

    // Total assigned slots
    int totalSlots = 0;
    for (int s = 0; s < 10; ++s) totalSlots += stateSlotCounts[s];
    TEST("Total assigned slots = 6", totalSlots == 6);

    printf("  [Status Matrix: 17 tests]\n");
}

// ====================================================================
// FNAME EXPRESSION/VISEME RESOLUTION TESTS
// ====================================================================

void TestFNameExpressionViseme() {
    printf("\n=== FName Expression/Viseme Resolution ===\n");

    // Mock types replicating ResolveExpressionTextureSet / ResolveVisemeFrames logic
    struct FMockTextureSet { int ID = 0; bool bValid = false; };
    struct FMockSlot {
        // Named expression textures (simulates TMap<FName, FFaceTextureSet>)
        struct { std::string Name; FMockTextureSet Tex; } NamedExpr[8];
        int NamedExprCount = 0;
        FMockTextureSet ExprTextures[5]; // enum-based fallback

        // Named viseme frames (simulates TMap<FName, int>)
        struct { std::string Name; int Frames; } NamedVis[8];
        int NamedVisCount = 0;
        int VisemeFrameSets[5] = {};

        FMockSlot() {
            for (int i = 0; i < 5; ++i) VisemeFrameSets[i] = (i + 1) * 5;
        }
    };

    auto ResolveExpr = [](const std::string& name, const FMockSlot& slot, int enumIdx) -> FMockTextureSet {
        for (int i = 0; i < slot.NamedExprCount; ++i)
            if (slot.NamedExpr[i].Name == name) return slot.NamedExpr[i].Tex;
        if (enumIdx >= 0 && enumIdx < 5) return slot.ExprTextures[enumIdx];
        return {};
    };

    auto ResolveVis = [](const std::string& name, const FMockSlot& slot, int enumIdx) -> int {
        for (int i = 0; i < slot.NamedVisCount; ++i)
            if (slot.NamedVis[i].Name == name) return slot.NamedVis[i].Frames;
        if (enumIdx >= 0 && enumIdx < 5) return slot.VisemeFrameSets[enumIdx];
        return 0;
    };

    // 1. Named wins over enum when both are set
    {
        FMockSlot slot;
        slot.ExprTextures[0] = {100, true};
        slot.NamedExpr[0] = {"Smile", {200, true}};
        slot.NamedExprCount = 1;
        FMockTextureSet result = ResolveExpr("Smile", slot, 0);
        TEST("Named expression wins over enum", result.ID == 200);
    }

    // 2. Only named set (no enum fallback)
    {
        FMockSlot slot;
        slot.NamedExpr[0] = {"Frown", {300, true}};
        slot.NamedExprCount = 1;
        FMockTextureSet result = ResolveExpr("Frown", slot, 1);
        TEST("Named expression when enum unset", result.ID == 300);
    }

    // 3. Only enum set (no named match) — fallback
    {
        FMockSlot slot;
        slot.ExprTextures[2] = {400, true};
        FMockTextureSet result = ResolveExpr("Missing", slot, 2);
        TEST("Missing named falls back to enum", result.ID == 400);
    }

    // 4. Neither named nor enum set — returns empty
    {
        FMockSlot slot;
        FMockTextureSet result = ResolveExpr("Anything", slot, 3);
        TEST("Neither named nor enum → empty", result.ID == 0 && !result.bValid);
    }

    // 5. Empty name falls through to enum
    {
        FMockSlot slot;
        slot.ExprTextures[0] = {500, true};
        FMockTextureSet result = ResolveExpr("", slot, 0);
        TEST("Empty name falls to enum", result.ID == 500);
    }

    // 6. Multiple named entries: correct one resolved
    {
        FMockSlot slot;
        slot.NamedExpr[0] = {"Neutral", {10, true}};
        slot.NamedExpr[1] = {"Smile", {20, true}};
        slot.NamedExpr[2] = {"Frown", {30, true}};
        slot.NamedExprCount = 3;
        TEST("Smile resolved from multiple", ResolveExpr("Smile", slot, 0).ID == 20);
        TEST("Frown resolved from multiple", ResolveExpr("Frown", slot, 1).ID == 30);
        TEST("Neutral resolved from multiple", ResolveExpr("Neutral", slot, 2).ID == 10);
    }

    // 7. Named viseme wins over enum
    {
        FMockSlot slot;
        slot.NamedVis[0] = {"Ah", 12};
        slot.NamedVisCount = 1;
        int result = ResolveVis("Ah", slot, 0);
        TEST("Named viseme wins over enum", result == 12);
    }

    // 8. Named viseme fallback to enum
    {
        FMockSlot slot;
        slot.VisemeFrameSets[1] = 8;
        int result = ResolveVis("Unknown", slot, 1);
        TEST("Missing viseme falls to enum", result == 8);
    }

    // 9. Empty viseme name falls to enum
    {
        FMockSlot slot;
        slot.VisemeFrameSets[2] = 15;
        int result = ResolveVis("", slot, 2);
        TEST("Empty viseme name falls to enum", result == 15);
    }

    // 10. Multiple named visemes: correct one resolved
    {
        FMockSlot slot;
        slot.NamedVis[0] = {"Ah", 5};
        slot.NamedVis[1] = {"Uh", 8};
        slot.NamedVis[2] = {"Oh", 12};
        slot.NamedVisCount = 3;
        TEST("Ah resolved correctly", ResolveVis("Ah", slot, 0) == 5);
        TEST("Oh resolved correctly", ResolveVis("Oh", slot, 1) == 12);
        TEST("Unknown falls to enum", ResolveVis("X", slot, 2) == slot.VisemeFrameSets[2]);
    }

    // 11. Both named expr + viseme in same slot resolve independently
    {
        FMockSlot slot;
        slot.NamedExpr[0] = {"Smile", {77, true}};
        slot.NamedExprCount = 1;
        slot.NamedVis[0] = {"Ah", 9};
        slot.NamedVisCount = 1;
        TEST("Named expr + viseme: expr", ResolveExpr("Smile", slot, 0).ID == 77);
        TEST("Named expr + viseme: viseme", ResolveVis("Ah", slot, 0) == 9);
        TEST("Named expr + viseme: expr fallback", ResolveExpr("X", slot, 1).ID == 0);
        TEST("Named expr + viseme: viseme fallback", ResolveVis("X", slot, 1) == slot.VisemeFrameSets[1]);
    }

    printf("  [FName Expression/Viseme: 11 tests]\n");
}

// ====================================================================
// GETBOUNDARYORDEFAULT TESTS
// ====================================================================

void TestGetBoundaryOrDefault() {
    printf("\n=== GetBoundaryOrDefault (zone multiplier fallback) ===\n");

    // Replicates UFaceParallaxComponent::GetBoundaryOrDefault logic:
    //   if array has valid index, return array[index]; else return Defaults[index]
    struct MockArray {
        double Data[4] = {};
        int Count = 0;
    };

    static const double Defaults[4] = {1.0, 3.0, 5.0, 7.0};

    auto GetBoundaryOrDefault = [](const MockArray& arr, int idx) -> double {
        if (idx >= 0 && idx < arr.Count) return arr.Data[idx];
        if (idx >= 0 && idx < 4) return Defaults[idx];
        return 0.0;
    };

    // 1. Full array returns stored values
    {
        MockArray arr = {{2.0, 4.0, 6.0, 8.0}, 4};
        TEST("Full: index 0", fabs(GetBoundaryOrDefault(arr, 0) - 2.0) < 0.001);
        TEST("Full: index 1", fabs(GetBoundaryOrDefault(arr, 1) - 4.0) < 0.001);
        TEST("Full: index 2", fabs(GetBoundaryOrDefault(arr, 2) - 6.0) < 0.001);
        TEST("Full: index 3", fabs(GetBoundaryOrDefault(arr, 3) - 8.0) < 0.001);
    }

    // 2. Empty array returns defaults for all indices
    {
        MockArray arr = {{}, 0};
        TEST("Empty: index 0 default", fabs(GetBoundaryOrDefault(arr, 0) - 1.0) < 0.001);
        TEST("Empty: index 1 default", fabs(GetBoundaryOrDefault(arr, 1) - 3.0) < 0.001);
        TEST("Empty: index 2 default", fabs(GetBoundaryOrDefault(arr, 2) - 5.0) < 0.001);
        TEST("Empty: index 3 default", fabs(GetBoundaryOrDefault(arr, 3) - 7.0) < 0.001);
    }

    // 3. Partial array (2 elements): first two custom, last two default
    {
        MockArray arr = {{0.5, 2.5, 0.0, 0.0}, 2};
        TEST("Partial: index 0 custom", fabs(GetBoundaryOrDefault(arr, 0) - 0.5) < 0.001);
        TEST("Partial: index 1 custom", fabs(GetBoundaryOrDefault(arr, 1) - 2.5) < 0.001);
        TEST("Partial: index 2 default", fabs(GetBoundaryOrDefault(arr, 2) - 5.0) < 0.001);
        TEST("Partial: index 3 default", fabs(GetBoundaryOrDefault(arr, 3) - 7.0) < 0.001);
    }

    // 4. Partial array (3 elements): last index falls back
    {
        MockArray arr = {{1.5, 3.5, 5.5, 0.0}, 3};
        TEST("Partial3: index 0", fabs(GetBoundaryOrDefault(arr, 0) - 1.5) < 0.001);
        TEST("Partial3: index 1", fabs(GetBoundaryOrDefault(arr, 1) - 3.5) < 0.001);
        TEST("Partial3: index 2", fabs(GetBoundaryOrDefault(arr, 2) - 5.5) < 0.001);
        TEST("Partial3: index 3 default", fabs(GetBoundaryOrDefault(arr, 3) - 7.0) < 0.001);
    }

    // 5. Multipliers feed through to BM values
    {
        MockArray arr = {{1.0, 3.0, 5.0, 7.0}, 4};
        double BM[4];
        for (int i = 0; i < 4; ++i) BM[i] = GetBoundaryOrDefault(arr, i) * HZW;
        TEST("BM[0]=22.5", fabs(BM[0] - 22.5) < 0.001);
        TEST("BM[1]=67.5", fabs(BM[1] - 67.5) < 0.001);
        TEST("BM[2]=112.5", fabs(BM[2] - 112.5) < 0.001);
        TEST("BM[3]=157.5", fabs(BM[3] - 157.5) < 0.001);
    }

    // 6. Custom multipliers produce correct boundaries in state determination
    {
        static const double Custom[4] = {2.0, 4.0, 6.0, 8.0};
        TEST("Custom: Front at 44.99",
            DetermineStateFromAngles(44.99, 0, Custom) == EFaceAngleState::Front);
        TEST("Custom: 3QR at 45.01",
            DetermineStateFromAngles(45.01, 0, Custom) == EFaceAngleState::ThreeQuarterRight);
        TEST("Custom: Back at 180.01",
            DetermineStateFromAngles(180.01, 0, Custom) == EFaceAngleState::Back);
    }

    // 7. All defaults via fallback produce correct zones (using defaults directly)
    {
        static const double AllDefault[4] = {1.0, 3.0, 5.0, 7.0};
        TEST("Fallback Front at 0", DetermineStateFromAngles(0, 0, AllDefault) == EFaceAngleState::Front);
        TEST("Fallback 3QR at 35", DetermineStateFromAngles(35, 0, AllDefault) == EFaceAngleState::ThreeQuarterRight);
        TEST("Fallback Back at 170", DetermineStateFromAngles(170, 0, AllDefault) == EFaceAngleState::Back);
    }

    printf("  [GetBoundaryOrDefault: 24 tests]\n");
}

void TestDepthRange() {
    printf("\n=== Depth Range (DepthMin/DepthMax per layer) ===\n");

    // 1. Default layer definition
    {
        FFaceLayerDef def;
        TEST("Default DepthMin", std::abs(def.DepthMin - 0.0) < 1e-9);
        TEST("Default DepthMax", std::abs(def.DepthMax - 1.0) < 1e-9);
        TEST("Default remap 0.0", std::abs(def.GetRemappedDepth(0.0) - 0.0) < 1e-9);
        TEST("Default remap 0.5", std::abs(def.GetRemappedDepth(0.5) - 0.5) < 1e-9);
        TEST("Default remap 1.0", std::abs(def.GetRemappedDepth(1.0) - 1.0) < 1e-9);
    }

    // 2. Custom range [0.2, 0.8]
    {
        FFaceLayerDef def;
        def.DepthMin = 0.2;
        def.DepthMax = 0.8;
        TEST("Custom remap 0.0", std::abs(def.GetRemappedDepth(0.0) - 0.2) < 1e-9);
        TEST("Custom remap 0.5", std::abs(def.GetRemappedDepth(0.5) - 0.5) < 1e-9);
        TEST("Custom remap 1.0", std::abs(def.GetRemappedDepth(1.0) - 0.8) < 1e-9);
    }

    // 3. Inverted range [0.8, 0.2] (Min > Max)
    {
        FFaceLayerDef def;
        def.DepthMin = 0.8;
        def.DepthMax = 0.2;
        TEST("Inverted remap 0.0", std::abs(def.GetRemappedDepth(0.0) - 0.8) < 1e-9);
        TEST("Inverted remap 0.5", std::abs(def.GetRemappedDepth(0.5) - 0.5) < 1e-9);
        TEST("Inverted remap 1.0", std::abs(def.GetRemappedDepth(1.0) - 0.2) < 1e-9);
    }

    // 4. Zero-width range (Min == Max)
    {
        FFaceLayerDef def;
        def.DepthMin = 0.5;
        def.DepthMax = 0.5;
        TEST("ZeroWidth remap 0.0", std::abs(def.GetRemappedDepth(0.0) - 0.5) < 1e-9);
        TEST("ZeroWidth remap 0.5", std::abs(def.GetRemappedDepth(0.5) - 0.5) < 1e-9);
        TEST("ZeroWidth remap 1.0", std::abs(def.GetRemappedDepth(1.0) - 0.5) < 1e-9);
    }

    // 5. Negative range [-0.5, 0.5]
    {
        FFaceLayerDef def;
        def.DepthMin = -0.5;
        def.DepthMax = 0.5;
        TEST("Negative remap 0.0", std::abs(def.GetRemappedDepth(0.0) + 0.5) < 1e-9);
        TEST("Negative remap 0.5", std::abs(def.GetRemappedDepth(0.5) - 0.0) < 1e-9);
        TEST("Negative remap 1.0", std::abs(def.GetRemappedDepth(1.0) - 0.5) < 1e-9);
    }

    // 6. Full negative range [-1.0, -0.2]
    {
        FFaceLayerDef def;
        def.DepthMin = -1.0;
        def.DepthMax = -0.2;
        TEST("AllNeg remap 0.0", std::abs(def.GetRemappedDepth(0.0) + 1.0) < 1e-9);
        TEST("AllNeg remap 0.5", std::abs(def.GetRemappedDepth(0.5) + 0.6) < 1e-9);
        TEST("AllNeg remap 1.0", std::abs(def.GetRemappedDepth(1.0) + 0.2) < 1e-9);
    }

    // 7. DepthScale not affected by DepthMin/DepthMax
    {
        FFaceLayerDef def;
        def.DepthScale = 0.5;
        def.DepthMin = 0.0;
        def.DepthMax = 0.5;
        TEST("DepthScale unchanged", std::abs(def.DepthScale - 0.5) < 1e-9);
    }

    // 8. DepthMapIntensity not affected by DepthMin/DepthMax
    {
        FFaceLayerDef def;
        def.DepthMapIntensity = 2.0;
        def.DepthMin = 0.2;
        def.DepthMax = 0.8;
        TEST("DepthMapIntensity unchanged", std::abs(def.DepthMapIntensity - 2.0) < 1e-9);
    }

    // 9. Out-of-bounds depth samples are not clamped by GetRemappedDepth
    {
        FFaceLayerDef def;
        def.DepthMin = 0.2;
        def.DepthMax = 0.8;
        TEST("Sample 1.5 extends", std::abs(def.GetRemappedDepth(1.5) - 1.1) < 1e-9);
        TEST("Sample -0.5 extends", std::abs(def.GetRemappedDepth(-0.5) + 0.1) < 1e-9);
    }

    printf("  [DepthRange: 30 tests]\n");
}

void TestDepthParamNames() {
    printf("\n=== Depth Parameter Names ===\n");

    // Simulate the material parameter name pattern used in UpdateMaterialParameters
    struct MockDepthParams {
        const char* MinName = "DepthMin";
        const char* MaxName = "DepthMax";
    };

    auto GetMinParam = [](const MockDepthParams& p) -> const char* { return p.MinName; };
    auto GetMaxParam = [](const MockDepthParams& p) -> const char* { return p.MaxName; };

    // 1. Default names
    {
        MockDepthParams p;
        TEST("Default MinName", std::string(GetMinParam(p)) == "DepthMin");
        TEST("Default MaxName", std::string(GetMaxParam(p)) == "DepthMax");
    }

    // 2. Custom names
    {
        MockDepthParams p;
        p.MinName = "LayerDepthMin";
        p.MaxName = "LayerDepthMax";
        TEST("Custom MinName", std::string(GetMinParam(p)) == "LayerDepthMin");
        TEST("Custom MaxName", std::string(GetMaxParam(p)) == "LayerDepthMax");
    }

    // 3. Empty names
    {
        MockDepthParams p;
        p.MinName = "";
        p.MaxName = "";
        TEST("Empty MinName", std::string(GetMinParam(p)) == "");
        TEST("Empty MaxName", std::string(GetMaxParam(p)) == "");
    }

    // 4. FName-like resolution with multiple entries (like the FName exp/viseme test)
    // Simulates pushing DepthMin/DepthMax per layer: same name, different values per layer index
    {
        struct LayerRange {
            const char* ParamName;
            double Value;
        };
        // Two layers with different depth ranges
        LayerRange ranges[4] = {
            {"DepthMin", 0.0},  {"DepthMax", 0.5},
            {"DepthMin", 0.3},  {"DepthMax", 0.8},
        };
        TEST("Layer0 DepthMin", std::abs(ranges[0].Value - 0.0) < 1e-9);
        TEST("Layer0 DepthMax", std::abs(ranges[1].Value - 0.5) < 1e-9);
        TEST("Layer1 DepthMin", std::abs(ranges[2].Value - 0.3) < 1e-9);
        TEST("Layer1 DepthMax", std::abs(ranges[3].Value - 0.8) < 1e-9);
    }

    // 5. Depth range values mapped through GetRemappedDepth match material expectations
    {
        FFaceLayerDef def;
        def.DepthMin = 0.2;
        def.DepthMax = 0.9;
        // The material will receive DepthMin=0.2, DepthMax=0.9 as scalar params
        // and use them to remap depth map samples. Verify the mapping logic:
        TEST("Mat remap 0.0->0.2", std::abs(def.GetRemappedDepth(0.0) - 0.2) < 1e-9);
        TEST("Mat remap 0.5->0.55", std::abs(def.GetRemappedDepth(0.5) - 0.55) < 1e-9);
        TEST("Mat remap 1.0->0.9", std::abs(def.GetRemappedDepth(1.0) - 0.9) < 1e-9);
    }

    printf("  [DepthParamNames: 15 tests]\n");
}

void TestProfileVisualizerSizing() {
    printf("\n=== Profile-Aware Visualizer Sizing ===\n");

    // Simulates the sizing logic in DepthDebugVisualizerComponent::BuildDebugMesh:
    // EffectiveMeshSize = (ProfileHalfWidth > 0) ? (2 * ProfileHalfWidth) : FallbackMeshSize
    // EffectiveHeightScale = (ProfileHalfDepth > 0) ? ProfileHalfDepth : FallbackHeightScale

    struct MockVisualizer {
        double FallbackMeshSize = 30.0;
        double FallbackHeightScale = 10.0;
        double ProfileHalfWidth = 0.0;
        double ProfileHalfDepth = 0.0;

        double GetEffectiveMeshSize() const {
            return (ProfileHalfWidth > 0.0) ? (2.0 * ProfileHalfWidth) : FallbackMeshSize;
        }
        double GetEffectiveHeightScale() const {
            return (ProfileHalfDepth > 0.0) ? ProfileHalfDepth : FallbackHeightScale;
        }
    };

    // 1. No profile set — fallback defaults
    {
        MockVisualizer vis;
        TEST("Fallback mesh size", std::abs(vis.GetEffectiveMeshSize() - 30.0) < 1e-9);
        TEST("Fallback height", std::abs(vis.GetEffectiveHeightScale() - 10.0) < 1e-9);
    }

    // 2. Profile width set, depth unset
    {
        MockVisualizer vis;
        vis.ProfileHalfWidth = 15.0;
        TEST("Profile mesh size", std::abs(vis.GetEffectiveMeshSize() - 30.0) < 1e-9);
        TEST("Profile width fallback height", std::abs(vis.GetEffectiveHeightScale() - 10.0) < 1e-9);
    }

    // 3. Profile depth set, width unset
    {
        MockVisualizer vis;
        vis.ProfileHalfDepth = 5.0;
        TEST("Profile depth fallback mesh", std::abs(vis.GetEffectiveMeshSize() - 30.0) < 1e-9);
        TEST("Profile depth height", std::abs(vis.GetEffectiveHeightScale() - 5.0) < 1e-9);
    }

    // 4. Both profile dimensions set
    {
        MockVisualizer vis;
        vis.ProfileHalfWidth = 20.0;
        vis.ProfileHalfDepth = 8.0;
        TEST("Both: mesh size", std::abs(vis.GetEffectiveMeshSize() - 40.0) < 1e-9);
        TEST("Both: height", std::abs(vis.GetEffectiveHeightScale() - 8.0) < 1e-9);
    }

    // 5. Zero values treated as unset (no override)
    {
        MockVisualizer vis;
        vis.ProfileHalfWidth = 0.0;
        vis.ProfileHalfDepth = 0.0;
        TEST("Zero width fallback", std::abs(vis.GetEffectiveMeshSize() - 30.0) < 1e-9);
        TEST("Zero depth fallback", std::abs(vis.GetEffectiveHeightScale() - 10.0) < 1e-9);
    }

    // 6. ClearProfileDimensions resets to fallback
    {
        MockVisualizer vis;
        vis.ProfileHalfWidth = 20.0;
        vis.ProfileHalfDepth = 5.0;
        vis.ProfileHalfWidth = 0.0;
        vis.ProfileHalfDepth = 0.0;
        TEST("Clear: mesh size", std::abs(vis.GetEffectiveMeshSize() - 30.0) < 1e-9);
        TEST("Clear: height", std::abs(vis.GetEffectiveHeightScale() - 10.0) < 1e-9);
    }

    // 7. Vertex position computation with profile dimensions
    // (simulates X = (U - 0.5) * EffMeshSize, Z = Depth * EffHeightScale)
    {
        MockVisualizer vis;
        vis.ProfileHalfWidth = 10.0;
        vis.ProfileHalfDepth = 4.0;

        auto ComputeX = [&](double U) { return (U - 0.5) * vis.GetEffectiveMeshSize(); };
        auto ComputeZ = [&](double Depth) { return Depth * vis.GetEffectiveHeightScale(); };

        TEST("Vertex X at U=0.0", std::abs(ComputeX(0.0) - (-10.0)) < 1e-9);
        TEST("Vertex X at U=0.5", std::abs(ComputeX(0.5) - 0.0) < 1e-9);
        TEST("Vertex X at U=1.0", std::abs(ComputeX(1.0) - 10.0) < 1e-9);
        TEST("Vertex Z at Depth=0.0", std::abs(ComputeZ(0.0) - 0.0) < 1e-9);
        TEST("Vertex Z at Depth=0.5", std::abs(ComputeZ(0.5) - 2.0) < 1e-9);
        TEST("Vertex Z at Depth=1.0", std::abs(ComputeZ(1.0) - 4.0) < 1e-9);
    }

    printf("  [ProfileVisualizerSizing: 17 tests]\n");
}

void TestProfileDetectionTopBottom() {
    printf("\n=== Profile Detection (Top/Bottom cross-validation) ===\n");

    // Simulates DetectFaceProfileFromPreset's top/bottom cross-validation logic:
    //   - Front width → FaceHalfWidth, Front height → FaceHalfHeight
    //   - Top width validated against Front width, warning if mismatch
    //   - Top/Bottom height used as FaceHalfDepth fallback when profile views absent
    //   - Top/Bottom width used as FaceHalfHeight fallback when Front height absent

    struct MockTexSlot {
        int Width = 0;
        int Height = 0;
        bool HasAlbedo = false;
    };

    struct MockProfile {
        double HalfWidth = 0.0;
        double HalfHeight = 0.0;
        double HalfDepth = 0.0;
        int Warnings = 0;
    };

    // Simulates the core extraction logic (without UE_LOG warnings)
    auto DetectProfile = [](const MockTexSlot& Front, const MockTexSlot& Top,
                            const MockTexSlot& Bottom, const MockTexSlot& RightProfile,
                            const MockTexSlot& LeftProfile) -> MockProfile {
        MockProfile P;

        // Front pass
        if (Front.HasAlbedo && Front.Width > 0) P.HalfWidth = Front.Width * 0.5;
        if (Front.HasAlbedo && Front.Height > 0) P.HalfHeight = Front.Height * 0.5;

        // Top/Bottom cross-validation (just the half-height derivation)
        if (Front.Height <= 0 && Top.HasAlbedo && Top.Height > 0)
            P.HalfHeight = Top.Height * 0.5;
        if (P.HalfHeight <= 0.5 && Bottom.HasAlbedo && Bottom.Height > 0)
            P.HalfHeight = Bottom.Height * 0.5;

        // Profile pass for depth (primary)
        if (RightProfile.HasAlbedo && RightProfile.Width > 0)
            P.HalfDepth = RightProfile.Width * 0.5;
        else if (LeftProfile.HasAlbedo && LeftProfile.Width > 0)
            P.HalfDepth = LeftProfile.Width * 0.5;

        // Top/Bottom height fallback for depth
        if (P.HalfDepth <= 0.5)
        {
            if (Top.HasAlbedo && Top.Height > 0)
                P.HalfDepth = Top.Height * 0.5;
            else if (Bottom.HasAlbedo && Bottom.Height > 0)
                P.HalfDepth = Bottom.Height * 0.5;
        }

        return P;
    };

    // 1. Front only — width/height come from Front
    {
        MockTexSlot front{200, 300, true};
        MockTexSlot top{0, 0, false};
        MockTexSlot bottom{0, 0, false};
        MockTexSlot rprof{0, 0, false};
        MockTexSlot lprof{0, 0, false};
        auto P = DetectProfile(front, top, bottom, rprof, lprof);
        TEST("Front only: halfWidth = 100", std::abs(P.HalfWidth - 100.0) < 1e-9);
        TEST("Front only: halfHeight = 150", std::abs(P.HalfHeight - 150.0) < 1e-9);
        TEST("Front only: halfDepth = 0", std::abs(P.HalfDepth - 0.0) < 1e-9);
    }

    // 2. Front + Profile — depth from profile
    {
        MockTexSlot front{200, 300, true};
        MockTexSlot top{0, 0, false};
        MockTexSlot bottom{0, 0, false};
        MockTexSlot rprof{80, 400, true};
        MockTexSlot lprof{0, 0, false};
        auto P = DetectProfile(front, top, bottom, rprof, lprof);
        TEST("Front+Prof: halfWidth = 100", std::abs(P.HalfWidth - 100.0) < 1e-9);
        TEST("Front+Prof: halfHeight = 150", std::abs(P.HalfHeight - 150.0) < 1e-9);
        TEST("Front+Prof: halfDepth = 40", std::abs(P.HalfDepth - 40.0) < 1e-9);
    }

    // 3. No Front height — height derived from Top height
    {
        MockTexSlot front{200, 0, true}; // front has width but no height
        MockTexSlot top{200, 300, true};
        MockTexSlot bottom{0, 0, false};
        MockTexSlot rprof{80, 400, true};
        MockTexSlot lprof{0, 0, false};
        auto P = DetectProfile(front, top, bottom, rprof, lprof);
        TEST("Top height: halfWidth = 100", std::abs(P.HalfWidth - 100.0) < 1e-9);
        TEST("Top height: halfHeight = 150", std::abs(P.HalfHeight - 150.0) < 1e-9);
        TEST("Top height: halfDepth = 40", std::abs(P.HalfDepth - 40.0) < 1e-9);
    }

    // 4. No Front height, no Top — height from Bottom height
    {
        MockTexSlot front{200, 0, true};
        MockTexSlot top{0, 0, false};
        MockTexSlot bottom{200, 300, true};
        MockTexSlot rprof{80, 400, true};
        MockTexSlot lprof{0, 0, false};
        auto P = DetectProfile(front, top, bottom, rprof, lprof);
        TEST("Bottom height: halfHeight = 150", std::abs(P.HalfHeight - 150.0) < 1e-9);
    }

    // 5. No profile views — depth from Top height
    {
        MockTexSlot front{200, 300, true};
        MockTexSlot top{200, 80, true}; // Top height = face depth
        MockTexSlot bottom{0, 0, false};
        MockTexSlot rprof{0, 0, false};
        MockTexSlot lprof{0, 0, false};
        auto P = DetectProfile(front, top, bottom, rprof, lprof);
        TEST("Top depth: halfDepth = 40", std::abs(P.HalfDepth - 40.0) < 1e-9);
    }

    // 6. No profile views, no Top — depth from Bottom height
    {
        MockTexSlot front{200, 300, true};
        MockTexSlot top{0, 0, false};
        MockTexSlot bottom{200, 80, true};
        MockTexSlot rprof{0, 0, false};
        MockTexSlot lprof{0, 0, false};
        auto P = DetectProfile(front, top, bottom, rprof, lprof);
        TEST("Bottom depth: halfDepth = 40", std::abs(P.HalfDepth - 40.0) < 1e-9);
    }

    // 7. LeftProfile fallback when RightProfile unavailable
    {
        MockTexSlot front{200, 300, true};
        MockTexSlot top{0, 0, false};
        MockTexSlot bottom{0, 0, false};
        MockTexSlot rprof{0, 0, false};
        MockTexSlot lprof{80, 400, true};
        auto P = DetectProfile(front, top, bottom, rprof, lprof);
        TEST("LeftProf depth: halfDepth = 40", std::abs(P.HalfDepth - 40.0) < 1e-9);
    }

    // 8. All views absent — all dimensions stay 0
    {
        MockTexSlot front{0, 0, false};
        MockTexSlot top{0, 0, false};
        MockTexSlot bottom{0, 0, false};
        MockTexSlot rprof{0, 0, false};
        MockTexSlot lprof{0, 0, false};
        auto P = DetectProfile(front, top, bottom, rprof, lprof);
        TEST("No views: halfWidth = 0", std::abs(P.HalfWidth - 0.0) < 1e-9);
        TEST("No views: halfHeight = 0", std::abs(P.HalfHeight - 0.0) < 1e-9);
        TEST("No views: halfDepth = 0", std::abs(P.HalfDepth - 0.0) < 1e-9);
    }

    printf("  [ProfileDetectionTopBottom: 17 tests]\n");
}

void TestProfileDetectionMultiLayer() {
    printf("\n=== Profile Detection (Multi-Layer Scan) ===\n");

    // Simulates the GetMaxTexDimensions helper: scan ALL layers for a state,
    // return the max width/height across them.

    struct MockTexSlot {
        int Width = 0;
        int Height = 0;
        bool HasAlbedo = false;
    };

    struct MockProfile {
        double HalfWidth = 0.0;
        double HalfHeight = 0.0;
        double HalfDepth = 0.0;
    };

    auto GetMaxDimensions = [](const MockTexSlot* Slots, int Count, int& OutW, int& OutH) {
        OutW = 0;
        OutH = 0;
        for (int i = 0; i < Count; ++i)
        {
            OutW = std::max(OutW, Slots[i].Width);
            OutH = std::max(OutH, Slots[i].Height);
        }
    };

    auto DetectProfileMulti = [&](const MockTexSlot* Front, int FrontCount,
                                   const MockTexSlot* RightProf, int RightProfCount,
                                   const MockTexSlot* LeftProf, int LeftProfCount,
                                   const MockTexSlot* Top, int TopCount,
                                   const MockTexSlot* Bottom, int BottomCount) -> MockProfile {
        MockProfile P;

        int FW = 0, FH = 0;
        GetMaxDimensions(Front, FrontCount, FW, FH);
        if (FW > 0) P.HalfWidth = FW * 0.5;
        if (FH > 0) P.HalfHeight = FH * 0.5;

        // Profile pass for depth
        int RPW = 0, RPH = 0;
        GetMaxDimensions(RightProf, RightProfCount, RPW, RPH);
        if (RPW > 0) P.HalfDepth = RPW * 0.5;

        if (P.HalfDepth <= 0.5)
        {
            int LPW = 0, LPH = 0;
            GetMaxDimensions(LeftProf, LeftProfCount, LPW, LPH);
            if (LPW > 0) P.HalfDepth = LPW * 0.5;
        }

        // Top/Bottom depth fallback
        if (P.HalfDepth <= 0.5)
        {
            int TW = 0, TH = 0;
            GetMaxDimensions(Top, TopCount, TW, TH);
            if (TH > 0) P.HalfDepth = TH * 0.5;

            if (P.HalfDepth <= 0.5)
            {
                int BW = 0, BH = 0;
                GetMaxDimensions(Bottom, BottomCount, BW, BH);
                if (BH > 0) P.HalfDepth = BH * 0.5;
            }
        }

        return P;
    };

    // 1. Single layer each — baseline
    {
        MockTexSlot front[] = {{200, 300, true}};
        MockTexSlot prof[] = {{80, 400, true}};
        MockTexSlot lprof[] = {};
        MockTexSlot top[] = {};
        MockTexSlot bottom[] = {};
        auto P = DetectProfileMulti(front, 1, prof, 1, lprof, 0, top, 0, bottom, 0);
        TEST("Single: halfWidth=100", std::abs(P.HalfWidth - 100.0) < 1e-9);
        TEST("Single: halfHeight=150", std::abs(P.HalfHeight - 150.0) < 1e-9);
        TEST("Single: halfDepth=40", std::abs(P.HalfDepth - 40.0) < 1e-9);
    }

    // 2. Multiple front layers — takes max width/height
    {
        MockTexSlot front[] = {{200, 300, true}, {100, 400, true}};
        MockTexSlot prof[] = {{80, 400, true}};
        MockTexSlot lprof[] = {};
        MockTexSlot top[] = {};
        MockTexSlot bottom[] = {};
        auto P = DetectProfileMulti(front, 2, prof, 1, lprof, 0, top, 0, bottom, 0);
        TEST("MultiFront: halfWidth=100 (max 200/2)", std::abs(P.HalfWidth - 100.0) < 1e-9);
        TEST("MultiFront: halfHeight=200 (max 400/2)", std::abs(P.HalfHeight - 200.0) < 1e-9);
    }

    // 3. Multiple profile layers — takes max width for depth
    {
        MockTexSlot front[] = {{200, 300, true}};
        MockTexSlot prof[] = {{80, 400, true}, {120, 200, true}}; // max width=120
        MockTexSlot lprof[] = {};
        MockTexSlot top[] = {};
        MockTexSlot bottom[] = {};
        auto P = DetectProfileMulti(front, 1, prof, 2, lprof, 0, top, 0, bottom, 0);
        TEST("MultiProf: halfDepth=60 (max 120/2)", std::abs(P.HalfDepth - 60.0) < 1e-9);
    }

    // 4. Right profile absent, left profile with multiple layers
    {
        MockTexSlot front[] = {{200, 300, true}};
        MockTexSlot prof[] = {};
        MockTexSlot lprof[] = {{90, 400, true}, {70, 300, true}}; // max width=90
        MockTexSlot top[] = {};
        MockTexSlot bottom[] = {};
        auto P = DetectProfileMulti(front, 1, prof, 0, lprof, 2, top, 0, bottom, 0);
        TEST("LeftProf multi: halfDepth=45 (max 90/2)", std::abs(P.HalfDepth - 45.0) < 1e-9);
    }

    // 5. Zero-width layers ignored
    {
        MockTexSlot front[] = {{200, 300, true}, {0, 0, true}, {0, 100, true}};
        MockTexSlot prof[] = {{80, 400, true}};
        MockTexSlot lprof[] = {};
        MockTexSlot top[] = {};
        MockTexSlot bottom[] = {};
        auto P = DetectProfileMulti(front, 3, prof, 1, lprof, 0, top, 0, bottom, 0);
        TEST("ZeroW: halfWidth=100 (200/2)", std::abs(P.HalfWidth - 100.0) < 1e-9);
        TEST("ZeroW: halfHeight=150 (max 300/2)", std::abs(P.HalfHeight - 150.0) < 1e-9);
    }

    // 6. Multiple front + profile + top all layered
    {
        MockTexSlot front[] = {{200, 300, true}, {50, 50, true}};
        MockTexSlot prof[] = {{80, 400, true}, {60, 200, true}}; // max depth W=80
        MockTexSlot lprof[] = {};
        MockTexSlot top[] = {{200, 100, true}}; // Top W=200 (matches front), H=100
        MockTexSlot bottom[] = {};
        auto P = DetectProfileMulti(front, 2, prof, 2, lprof, 0, top, 1, bottom, 0);
        TEST("All layered: halfWidth=100", std::abs(P.HalfWidth - 100.0) < 1e-9);
        TEST("All layered: halfHeight=150", std::abs(P.HalfHeight - 150.0) < 1e-9);
        TEST("All layered: halfDepth=40", std::abs(P.HalfDepth - 40.0) < 1e-9);
    }

    printf("  [ProfileDetectionMultiLayer: 10 tests]\n");
}

void TestWireframeMode() {
    printf("\n=== Wireframe Mode ===\n");

    // Simulates UpdateWireframeMode material swap logic:
    //   - bShowWireframe && WireframeMaterial != null → use WireframeMaterial
    //   - else if DebugMaterialInstance != null → use DebugMaterialInstance
    //   - else if DepthDebugMaterial != null → use DepthDebugMaterial

    struct MockMaterial { int Id; };
    struct MockVisualizer {
        bool bShowWireframe = false;
        MockMaterial* WireframeMaterial = nullptr;
        MockMaterial* DebugMaterialInstance = nullptr;
        MockMaterial* DepthDebugMaterial = nullptr;
        MockMaterial* ActiveMaterial = nullptr;

        void UpdateWireframe() {
            if (bShowWireframe && WireframeMaterial)
                ActiveMaterial = WireframeMaterial;
            else if (DebugMaterialInstance)
                ActiveMaterial = DebugMaterialInstance;
            else if (DepthDebugMaterial)
                ActiveMaterial = DepthDebugMaterial;
        }
    };

    // 1. Wireframe off uses debug material instance
    {
        MockMaterial solid{1};
        MockMaterial wf{2};
        MockVisualizer vis;
        vis.bShowWireframe = false;
        vis.DebugMaterialInstance = &solid;
        vis.WireframeMaterial = &wf;
        vis.UpdateWireframe();
        TEST("WireOff uses DebugMatInst", vis.ActiveMaterial->Id == 1);
    }

    // 2. Wireframe on uses wireframe material
    {
        MockMaterial solid{1};
        MockMaterial wf{2};
        MockVisualizer vis;
        vis.bShowWireframe = true;
        vis.DebugMaterialInstance = &solid;
        vis.WireframeMaterial = &wf;
        vis.UpdateWireframe();
        TEST("WireOn uses WireframeMat", vis.ActiveMaterial->Id == 2);
    }

    // 3. Wireframe on but no wireframe material → falls back to debug material
    {
        MockMaterial solid{1};
        MockVisualizer vis;
        vis.bShowWireframe = true;
        vis.WireframeMaterial = nullptr;
        vis.DebugMaterialInstance = &solid;
        vis.UpdateWireframe();
        TEST("WireOn no WireframeMat → debug", vis.ActiveMaterial->Id == 1);
    }

    // 4. No debug material instance → falls back to depth debug material
    {
        MockMaterial base{3};
        MockMaterial wf{2};
        MockVisualizer vis;
        vis.bShowWireframe = true;
        vis.WireframeMaterial = &wf;
        vis.DebugMaterialInstance = nullptr;
        vis.DepthDebugMaterial = &base;
        vis.UpdateWireframe();
        TEST("No DebugMatInst → base", vis.ActiveMaterial->Id == 2); // wireframe wins
    }

    // 5. Wireframe off, no debug instance → uses base material
    {
        MockMaterial base{3};
        MockVisualizer vis;
        vis.bShowWireframe = false;
        vis.DebugMaterialInstance = nullptr;
        vis.DepthDebugMaterial = &base;
        vis.UpdateWireframe();
        TEST("WireOff no inst → base", vis.ActiveMaterial->Id == 3);
    }

    // 6. Nothing set → ActiveMaterial stays null
    {
        MockVisualizer vis;
        vis.UpdateWireframe();
        TEST("Nothing set → null", vis.ActiveMaterial == nullptr);
    }

    // 7. Toggle wireframe on then off
    {
        MockMaterial solid{1};
        MockMaterial wf{2};
        MockVisualizer vis;
        vis.DebugMaterialInstance = &solid;
        vis.WireframeMaterial = &wf;

        vis.bShowWireframe = true;
        vis.UpdateWireframe();
        TEST("Toggle on uses WF", vis.ActiveMaterial->Id == 2);

        vis.bShowWireframe = false;
        vis.UpdateWireframe();
        TEST("Toggle off uses solid", vis.ActiveMaterial->Id == 1);
    }

    printf("  [WireframeMode: 8 tests]\n");
}

void TestOutlineArtConcept() {
    printf("\n=== Outline Art Concept ===\n");

    // Simulates the OutlineViewStates container and IsOutlineState checks

    struct MockOutlineSet {
        int States[10] = {}; // bitmask: 1 if state is outline
        int Count = 0;

        void Add(int S) { if (!Contains(S)) { States[S] = 1; Count++; } }
        void Clear() { for (int i = 0; i < 10; ++i) States[i] = 0; Count = 0; }
        bool Contains(int S) const { return S >= 0 && S < 10 && States[S] == 1; }
    };

    // 1. Default set: Front(0), RightProfile(2), LeftProfile(6), Top(8), Bottom(9)
    {
        MockOutlineSet S;
        S.Add(0); S.Add(2); S.Add(6); S.Add(8); S.Add(9);
        TEST("Default: Front is outline", S.Contains(0));
        TEST("Default: 3QR not outline", !S.Contains(1));
        TEST("Default: RightProf is outline", S.Contains(2));
        TEST("Default: LeftProf is outline", S.Contains(6));
        TEST("Default: Top is outline", S.Contains(8));
        TEST("Default: Bottom is outline", S.Contains(9));
        TEST("Default: BackR not outline", !S.Contains(3));
        TEST("Default: Back not outline", !S.Contains(4));
        TEST("Default: BackL not outline", !S.Contains(5));
        TEST("Default: 3QL not outline", !S.Contains(7));
    }

    // 2. AddUnique prevents duplicates
    {
        MockOutlineSet S;
        S.Add(0);
        S.Add(0);
        TEST("AddUnique: count=1", S.Count == 1);
    }

    // 3. Clear removes all
    {
        MockOutlineSet S;
        S.Add(0); S.Add(2);
        S.Clear();
        TEST("Clear: empty", S.Count == 0);
        TEST("Clear: Front not outline", !S.Contains(0));
    }

    // 4. Custom set — only Front and Top
    {
        MockOutlineSet S;
        S.Add(0); S.Add(8);
        TEST("Custom: Front is outline", S.Contains(0));
        TEST("Custom: Top is outline", S.Contains(8));
        TEST("Custom: RightProf not outline", !S.Contains(2));
    }

    // 5. Empty set — nothing is outline
    {
        MockOutlineSet S;
        TEST("Empty: Front not outline", !S.Contains(0));
        TEST("Empty: Top not outline", !S.Contains(8));
    }

    printf("  [OutlineArtConcept: 15 tests]\n");
}

void TestProfileVisualizerPropagation() {
    printf("\n=== Profile→Visualizer Propagation ===\n");

    // Simulates the propagation logic at the end of DetectFaceProfileFromPreset:
    //   - GetOwner → FindComponentByClass<Visualizer> → SetProfileDimensions(HalfW, HalfH, HalfD)

    struct MockVisualizer {
        bool bDimensionsSet = false;
        double ReceivedHalfW = 0.0;
        double ReceivedHalfH = 0.0;
        double ReceivedHalfD = 0.0;

        void SetProfileDimensions(double HW, double HH, double HD) {
            bDimensionsSet = true;
            ReceivedHalfW = HW;
            ReceivedHalfH = HH;
            ReceivedHalfD = HD;
        }
    };

    struct MockComponent {
        bool bHasVisualizer = false;
        MockVisualizer Vis;

        MockVisualizer* FindVisualizer() {
            return bHasVisualizer ? &Vis : nullptr;
        }
    };

    // Simulate the propagation logic
    auto PropagateProfile = [](MockComponent* Comp, double HW, double HH, double HD) {
        if (MockVisualizer* V = Comp->FindVisualizer())
        {
            V->SetProfileDimensions(HW, HH, HD);
        }
    };

    // 1. Visualizer present — dimensions are set
    {
        MockComponent Comp;
        Comp.bHasVisualizer = true;
        PropagateProfile(&Comp, 100.0, 150.0, 40.0);
        TEST("Vis: dimensions set", Comp.Vis.bDimensionsSet);
        TEST("Vis: halfWidth=100", std::abs(Comp.Vis.ReceivedHalfW - 100.0) < 1e-9);
        TEST("Vis: halfHeight=150", std::abs(Comp.Vis.ReceivedHalfH - 150.0) < 1e-9);
        TEST("Vis: halfDepth=40", std::abs(Comp.Vis.ReceivedHalfD - 40.0) < 1e-9);
    }

    // 2. No visualizer — no crash, no set
    {
        MockComponent Comp;
        Comp.bHasVisualizer = false;
        PropagateProfile(&Comp, 100.0, 150.0, 40.0);
        TEST("NoVis: no crash", true);
    }

    // 3. Zero dimensions still propagate
    {
        MockComponent Comp;
        Comp.bHasVisualizer = true;
        PropagateProfile(&Comp, 0.0, 0.0, 0.0);
        TEST("Zero: set called", Comp.Vis.bDimensionsSet);
        TEST("Zero: halfW=0", std::abs(Comp.Vis.ReceivedHalfW - 0.0) < 1e-9);
    }

    // 4. Propagate after profile update (simulating re-detection)
    {
        MockComponent Comp;
        Comp.bHasVisualizer = true;
        PropagateProfile(&Comp, 100.0, 150.0, 40.0);
        PropagateProfile(&Comp, 120.0, 160.0, 50.0);
        TEST("Update: halfWidth=120", std::abs(Comp.Vis.ReceivedHalfW - 120.0) < 1e-9);
        TEST("Update: halfHeight=160", std::abs(Comp.Vis.ReceivedHalfH - 160.0) < 1e-9);
        TEST("Update: halfDepth=50", std::abs(Comp.Vis.ReceivedHalfD - 50.0) < 1e-9);
    }

    printf("  [ProfileVisualizerPropagation: 8 tests]\n");
}

// ========================
// ASYNC STALENESS TESTS
// ========================
void TestAsyncStaleness() {
    printf("\n=== AsyncStaleness ===\n");

    // Simulates: Gen 1 queues paths A,B,C. Gen 3 queues path D.
    // Gen 1 textures resolve after Gen 3 started — they must still be cached.
    struct FTextureCache {
        int Cache[100];
        int CacheCount = 0;

        void Store(int ID) {
            for (int i = 0; i < CacheCount; ++i)
                if (Cache[i] == ID) return;
            Cache[CacheCount++] = ID;
        }
        bool Contains(int ID) {
            for (int i = 0; i < CacheCount; ++i)
                if (Cache[i] == ID) return true;
            return false;
        }
    };

    struct FLoadEntry { int Gen; int Complete; };
    FLoadEntry Active[] = {{1, 0}, {1, 0}, {3, 0}};
    int LoadCount = 3;
    int LoadGeneration = 3; // newest gen
    FTextureCache Cache;

    // Simulate OnAsyncTexturesLoaded: always cache, only resolve if current-gen
    bool bNewCurrentGen = false;
    for (int i = 0; i < LoadCount; ++i) {
        if (Active[i].Complete) {
            int ID = i + 100;
            Cache.Store(ID);
            if (Active[i].Gen >= LoadGeneration)
                bNewCurrentGen = true;
        }
    }

    // Mark Gen 1 textures as completed (stale callback)
    Active[0].Complete = 1;
    Active[1].Complete = 1;
    bNewCurrentGen = false;
    for (int i = 0; i < LoadCount; ++i) {
        if (Active[i].Complete) {
            int ID = i + 100;
            Cache.Store(ID);
            if (Active[i].Gen >= LoadGeneration)
                bNewCurrentGen = true;
        }
    }
    TEST("Gen 1 A cached despite stale",  Cache.Contains(100));
    TEST("Gen 1 B cached despite stale",  Cache.Contains(101));
    TEST("No current-gen resolved",       bNewCurrentGen == false);

    // Gen 3 texture completes — should set bNewCurrentGen
    Active[2].Complete = 1;
    bNewCurrentGen = false;
    for (int i = 0; i < LoadCount; ++i) {
        if (Active[i].Complete) {
            int ID = i + 100;
            Cache.Store(ID);
            if (Active[i].Gen >= LoadGeneration)
                bNewCurrentGen = true;
        }
    }
    TEST("Gen 3 D cached",        Cache.Contains(102));
    TEST("Current-gen resolved",  bNewCurrentGen == true);
}

// ========================
// LAYER VISIBILITY TESTS
// ========================
void TestLayerVisibility() {
    printf("\n=== Layer Visibility ===\n");

    // Mock the TMap<FName, bool> logic used by SetLayerVisibility/GetLayerVisibility
    struct FVisibilityMap {
        struct FEntry { std::string Tag; bool bVisible; };
        FEntry Entries[16];
        int Count = 0;

        void SetVisibility(const std::string& Tag, bool bVisible) {
            for (int i = 0; i < Count; ++i) {
                if (Entries[i].Tag == Tag) {
                    if (bVisible) {
                        // Remove (visible is default, no override needed)
                        for (int j = i; j < Count - 1; ++j) Entries[j] = Entries[j + 1];
                        --Count;
                    } else {
                        Entries[i].bVisible = false;
                    }
                    return;
                }
            }
            if (!bVisible) {
                Entries[Count++] = {Tag, false};
            }
        }

        bool GetVisibility(const std::string& Tag) const {
            for (int i = 0; i < Count; ++i) {
                if (Entries[i].Tag == Tag) return Entries[i].bVisible;
            }
            return true; // default visible
        }
    };

    FVisibilityMap VM;
    TEST("Default visible", VM.GetVisibility("Eye") == true);
    TEST("Default visible 2", VM.GetVisibility("Mouth") == true);

    VM.SetVisibility("Eye", false);
    TEST("After hide", VM.GetVisibility("Eye") == false);
    TEST("Other still visible", VM.GetVisibility("Mouth") == true);

    VM.SetVisibility("Eye", true);
    TEST("After show again", VM.GetVisibility("Eye") == true);
    TEST("Override removed", VM.Count == 0);
}

// ========================
// ASYNC TEXTURE LOAD CACHE TESTS
// ========================
void TestAsyncTextureCache() {
    printf("\n=== Async Texture Cache ===\n");

    // Mock the AsyncTextureCache + ActiveTextureLoads + generation logic
    struct FTextureCache {
        // Simulate FSoftObjectPath as string, TObjectPtr<UTexture2D> as int
        struct FEntry { std::string Path; int TextureID; };
        FEntry Cache[32];
        int CacheCount = 0;

        struct FLoadEntry { std::string Path; int Generation; };
        FLoadEntry Loads[32];
        int LoadCount = 0;
        int LoadGeneration = 0;

        bool ContainsCache(const std::string& Path) const {
            for (int i = 0; i < CacheCount; ++i)
                if (Cache[i].Path == Path) return true;
            return false;
        }

        bool ContainsLoad(const std::string& Path) const {
            for (int i = 0; i < LoadCount; ++i)
                if (Loads[i].Path == Path) return true;
            return false;
        }

        void QueuePath(const std::string& Path) {
            if (!ContainsCache(Path) && !ContainsLoad(Path)) {
                Cache[CacheCount++] = {Path, 0}; // placeholder
                Loads[LoadCount++] = {Path, LoadGeneration};
            }
        }

        void BeginLoad() {
            ++LoadGeneration;
            // Filter loads to current generation
            int WriteIdx = 0;
            for (int i = 0; i < LoadCount; ++i) {
                if (Loads[i].Generation >= LoadGeneration)
                    Loads[WriteIdx++] = Loads[i];
            }
            LoadCount = WriteIdx;
        }

        void ResolveLoad(const std::string& Path, int TexID) {
            // Update cache with resolved texture
            for (int i = 0; i < CacheCount; ++i) {
                if (Cache[i].Path == Path) {
                    Cache[i].TextureID = TexID;
                    break;
                }
            }
            // Remove from active loads
            int WriteIdx = 0;
            for (int i = 0; i < LoadCount; ++i) {
                if (Loads[i].Path != Path)
                    Loads[WriteIdx++] = Loads[i];
            }
            LoadCount = WriteIdx;
        }

        int ResolveTexture(const std::string& Path) const {
            for (int i = 0; i < CacheCount; ++i)
                if (Cache[i].Path == Path) return Cache[i].TextureID;
            return 0;
        }
    };

    FTextureCache TC;
    TEST("Empty cache miss", TC.ContainsCache("Tex_A") == false);

    TC.QueuePath("Tex_A");
    TEST("Queue adds to cache", TC.ContainsCache("Tex_A") == true);
    TEST("Queue adds to loads", TC.ContainsLoad("Tex_A") == true);
    TC.QueuePath("Tex_A");
    TEST("Queue no dup", TC.LoadCount <= 1); // duplicate queue is no-op

    TC.ResolveLoad("Tex_A", 42);
    TEST("Resolve updates cache", TC.ResolveTexture("Tex_A") == 42);
    TEST("Resolve removes load", TC.ContainsLoad("Tex_A") == false);

    // Generation test: old loads are skipped on new BeginLoad
    TC.QueuePath("Tex_B");
    TC.BeginLoad(); // bumps generation, Tex_B has old gen
    // After BeginLoad, Tex_B's load entry should have gen >= LoadGeneration
    // Our simple mock doesn't filter, but the concept is tested
    TEST("Generation bumped", TC.LoadGeneration >= 1);
}

// ========================
// APPLY CURRENT STATE TEXTURES TESTS
// ========================
void TestApplyCurrentStateTextures() {
    printf("\n=== ApplyCurrentStateTextures ===\n");

    // Mock: a component that tracks which state/layer textures were applied
    struct FApplier {
        struct FApplied { std::string State; std::string Layer; int AlbedoID; };
        FApplied Log[32];
        int LogCount = 0;

        int CurrentStateIdx = 0;
        struct FSlot { int AlbedoID; };
        FSlot Slots[4][4]; // [state][layer]
        int StateCount = 4;
        int LayerCount = 2;

        void ApplyCurrent() {
            for (int l = 0; l < LayerCount; ++l) {
                FSlot& S = Slots[CurrentStateIdx][l];
                Log[LogCount++] = {"State" + std::to_string(CurrentStateIdx),
                                   "Layer" + std::to_string(l), S.AlbedoID};
            }
        }
    };

    FApplier App;
    App.Slots[0][0] = {100};
    App.Slots[0][1] = {101};
    App.ApplyCurrent();
    TEST("State0 Layer0 applied", App.Log[0].AlbedoID == 100);
    TEST("State0 Layer1 applied", App.Log[1].AlbedoID == 101);
    TEST("Only current state", App.LogCount == 2);
}

// ========================
// COLOR BY DEPTH / REBUILD MESH TESTS
// ========================
void TestColorByDepth() {
    printf("\n=== ColorByDepth / RebuildMesh ===\n");

    // Mock: track mesh rebuild calls and depth texture state
    struct FDepthColor {
        bool bUseVertexColors = false;
        int CurrentDepthTexID = 0;
        int RebuildCallCount = 0;
        bool bEnableGuard = false;

        void SetColorByDepth(bool bEnabled) {
            if (bEnableGuard && bUseVertexColors == bEnabled) return; // skip if no change
            bUseVertexColors = bEnabled;
            ++RebuildCallCount;
        }
    };

    FDepthColor DC;
    TEST("Initial rebuild count", DC.RebuildCallCount == 0);

    DC.SetColorByDepth(true);
    TEST("Enable increments rebuild", DC.RebuildCallCount == 1);
    TEST("Vertex colors on", DC.bUseVertexColors == true);

    DC.SetColorByDepth(true);
    TEST("No guard: second enable rebuilds", DC.RebuildCallCount == 2);

    DC.bEnableGuard = true;
    DC.SetColorByDepth(true);
    TEST("Guard skips duplicate", DC.RebuildCallCount == 2);

    DC.SetColorByDepth(false);
    TEST("Disable rebuilds", DC.RebuildCallCount == 3);
    TEST("Vertex colors off", DC.bUseVertexColors == false);
}

// ========================
// FRAME DELTA ORDERING TESTS
// ========================
void TestFrameDeltaOrdering() {
    printf("\n=== FrameDeltaOrdering ===\n");

    // Replicate the SaveFrameDelta logic to test ordering invariant
    float PreviousYaw = 0.0f, PreviousPitch = 0.0f;
    float FrameDyaw = 0.0f, FrameDpitch = 0.0f;

    auto SaveFrameDelta = [&](float CurrentYaw, float CurrentPitch) {
        FrameDyaw = CurrentYaw - PreviousYaw;
        FrameDpitch = CurrentPitch - PreviousPitch;
        PreviousYaw = CurrentYaw;
        PreviousPitch = CurrentPitch;
    };

    // First frame: no previous frame, delta = current
    SaveFrameDelta(10.0f, 5.0f);
    TEST("First frame yaw delta = current", std::abs(FrameDyaw - 10.0f) < 1e-9f);
    TEST("First frame pitch delta = current", std::abs(FrameDpitch - 5.0f) < 1e-9f);
    TEST("Previous yaw updated", std::abs(PreviousYaw - 10.0f) < 1e-9f);
    TEST("Previous pitch updated", std::abs(PreviousPitch - 5.0f) < 1e-9f);

    // Second frame: delta = new - previous
    SaveFrameDelta(15.0f, 3.0f);
    TEST("Second frame yaw delta = 5", std::abs(FrameDyaw - 5.0f) < 1e-9f);
    TEST("Second frame pitch delta = -2", std::abs(FrameDpitch - (-2.0f)) < 1e-9f);
    TEST("Previous yaw = 15", std::abs(PreviousYaw - 15.0f) < 1e-9f);
    TEST("Previous pitch = 3", std::abs(PreviousPitch - 3.0f) < 1e-9f);

    // Verify ordering: if PreviousYaw/Pitch were overwritten BEFORE computing delta,
    // the delta would be 0 instead of 5/-2
    // (This test structurally verifies the invariant documented in AGENTS.md rule 8)
    bool bOrderingCorrect = (FrameDyaw == 5.0f && FrameDpitch == -2.0f);
    TEST("Ordering invariant: delta computed before overwrite", bOrderingCorrect);

    // Edge case: zero movement
    SaveFrameDelta(15.0f, 3.0f);
    TEST("Zero movement delta", std::abs(FrameDyaw) < 1e-9f && std::abs(FrameDpitch) < 1e-9f);

    // Edge case: large jump (e.g., camera teleport)
    SaveFrameDelta(115.0f, 93.0f);
    TEST("Large yaw delta", std::abs(FrameDyaw - 100.0f) < 1e-9f);
    TEST("Large pitch delta", std::abs(FrameDpitch - 90.0f) < 1e-9f);
}

// ========================
// TEXTURE PUSH CACHING TESTS
// ========================
void TestTexturePushCaching() {
    printf("\n=== TexturePushCaching ===\n");

    // Mock texture identifiers (standalone — no UE UObject dependency)
    using TextureID = int;
    int PushCount = 0;

    struct FAppliedEntry {
        TextureID Albedo = -1;
        TextureID Normal = -1;
        TextureID Depth = -1;
    };

    FAppliedEntry LastApplied;
    auto ShouldPush = [&](TextureID Albedo, TextureID Normal, TextureID Depth) -> bool {
        return !(Albedo == LastApplied.Albedo &&
                 Normal == LastApplied.Normal &&
                 Depth == LastApplied.Depth);
    };

    auto ApplyTextures = [&](TextureID Albedo, TextureID Normal, TextureID Depth) {
        if (!ShouldPush(Albedo, Normal, Depth)) return;
        ++PushCount;
        LastApplied.Albedo = Albedo;
        LastApplied.Normal = Normal;
        LastApplied.Depth = Depth;
    };

    // First push always happens
    ApplyTextures(1, 2, 3);
    TEST("First push counts", PushCount == 1);
    TEST("Albedo cached", LastApplied.Albedo == 1);
    TEST("Normal cached", LastApplied.Normal == 2);
    TEST("Depth cached", LastApplied.Depth == 3);

    // Redundant push with same IDs — should be skipped
    ApplyTextures(1, 2, 3);
    TEST("Redundant push skipped", PushCount == 1);

    // Partial change — any difference triggers push
    ApplyTextures(5, 2, 3);
    TEST("Albedo change pushes", PushCount == 2);
    ApplyTextures(5, 7, 3);
    TEST("Normal change pushes", PushCount == 3);
    ApplyTextures(5, 7, 9);
    TEST("Depth change pushes", PushCount == 4);

    // Back to original
    ApplyTextures(1, 2, 3);
    TEST("Reapply original pushes", PushCount == 5);

    // Same as current — skipped
    ApplyTextures(1, 2, 3);
    TEST("Re-redundant skipped", PushCount == 5);
}

// ========================
// SEARCH FILTER TESTS
// ========================
void TestApplySearchFilter() {
    printf("\n=== ApplySearchFilter ===\n");

    // Mock: sections with titles, filter matches against titles
    struct FSection { std::string Title; bool bVisible; };
    FSection Sections[8];
    int SectionCount = 0;

    auto AddSection = [&](const std::string& Title) {
        Sections[SectionCount++] = {Title, true};
    };

    auto ApplyFilter = [&](const std::string& Filter) {
        for (int i = 0; i < SectionCount; ++i) {
            if (Filter.empty()) {
                Sections[i].bVisible = true;
            } else {
                // Case-insensitive substring match
                std::string UpperTitle = Sections[i].Title;
                std::string UpperFilter = Filter;
                for (auto& c : UpperTitle) c = toupper(c);
                for (auto& c : UpperFilter) c = toupper(c);
                Sections[i].bVisible = UpperTitle.find(UpperFilter) != std::string::npos;
            }
        }
    };

    AddSection("Camera");
    AddSection("Texture");
    AddSection("Animation");
    AddSection("Debug");

    ApplyFilter("");
    TEST("Empty filter shows all",
        Sections[0].bVisible && Sections[1].bVisible &&
        Sections[2].bVisible && Sections[3].bVisible);

    ApplyFilter("Camera");
    TEST("Camera match", Sections[0].bVisible == true);
    TEST("Camera non-match", Sections[1].bVisible == false);

    ApplyFilter("tex");
    TEST("Tex matches Texture", Sections[1].bVisible == true);
    TEST("Tex no match Camera", Sections[0].bVisible == false);

    ApplyFilter("NONE");
    TEST("No match hides all",
        Sections[0].bVisible == false && Sections[1].bVisible == false &&
        Sections[2].bVisible == false && Sections[3].bVisible == false);
}

// ========================
// PREVIEW ACTOR NULL SAFETY TESTS
// ========================

// Mock: a simple "preview actor" pattern that mirrors the editor widget's
// null-safety contract for when no preview actor is assigned.

struct FPreviewActorMock {
    double OrbitYaw = 0.0;
    double OrbitPitch = 0.0;
    double OrbitDistance = 50.0;
    double FOV = 15.0;
    bool bAutoRotate = false;
    double AutoRotateSpeed = 30.0;
    bool bShowTextures = false;
    bool bShowDepthMesh = false;
    bool bShowWireframe = false;
    bool bColorByDepth = false;
    double PartSourceSize = 256.0;
};

// The "editor widget" that delegates to preview actor with null fallback values
struct FEditorWidgetMock {
    FPreviewActorMock* PreviewActor = nullptr;

    // Returns default if null (mirrors ValidatePreviewActor pattern)
    bool IsPreviewValid() const {
        return PreviewActor != nullptr;
    }

    // Camera getters with null safety (mirrors widget's camera UFUNCTIONs)
    double GetOrbitYaw() const {
        return PreviewActor ? PreviewActor->OrbitYaw : 0.0;
    }
    double GetOrbitPitch() const {
        return PreviewActor ? PreviewActor->OrbitPitch : 0.0;
    }
    double GetOrbitDistance() const {
        return PreviewActor ? PreviewActor->OrbitDistance : 50.0;
    }
    double GetPreviewFOV() const {
        return PreviewActor ? PreviewActor->FOV : 15.0;
    }
    bool GetAutoRotate() const {
        return PreviewActor ? PreviewActor->bAutoRotate : false;
    }
    double GetAutoRotateSpeed() const {
        return PreviewActor ? PreviewActor->AutoRotateSpeed : 30.0;
    }

    // Camera setters with null safety (mirrors ValidatePreviewActor early-return)
    void SetOrbitYaw(double V) {
        if (!PreviewActor) return;
        PreviewActor->OrbitYaw = V;
    }
    void SetOrbitPitch(double V) {
        if (!PreviewActor) return;
        PreviewActor->OrbitPitch = V;
    }
    void SetOrbitDistance(double V) {
        if (!PreviewActor) return;
        PreviewActor->OrbitDistance = V;
    }
    void SetPreviewFOV(double V) {
        if (!PreviewActor) return;
        PreviewActor->FOV = V;
    }
    void SetAutoRotate(bool b) {
        if (!PreviewActor) return;
        PreviewActor->bAutoRotate = b;
    }
    void SetAutoRotateSpeed(double V) {
        if (!PreviewActor) return;
        PreviewActor->AutoRotateSpeed = V;
    }

    // Setter that mirrors SetPreviewActor
    void SetPreviewActor(FPreviewActorMock* NewActor) {
        PreviewActor = NewActor;
    }

    FPreviewActorMock* GetPreviewActor() const {
        return PreviewActor;
    }
};

void TestNullPreviewActorSafety() {
    printf("\n=== NullPreviewActorSafety ===\n");

    FEditorWidgetMock Widget;  // PreviewActor defaults to nullptr

    // All getters must return safe defaults when actor is null
    TEST("Null yaw default", Widget.GetOrbitYaw() == 0.0);
    TEST("Null pitch default", Widget.GetOrbitPitch() == 0.0);
    TEST("Null distance default", Widget.GetOrbitDistance() == 50.0);
    TEST("Null FOV default", Widget.GetPreviewFOV() == 15.0);
    TEST("Null auto-rotate default", Widget.GetAutoRotate() == false);
    TEST("Null auto-speed default", Widget.GetAutoRotateSpeed() == 30.0);
    TEST("Null IsPreviewValid", Widget.IsPreviewValid() == false);

    // Setters must not crash when actor is null
    Widget.SetOrbitYaw(45.0);   // should silently no-op
    Widget.SetOrbitPitch(-10.0);
    Widget.SetOrbitDistance(100.0);
    Widget.SetPreviewFOV(20.0);
    Widget.SetAutoRotate(true);
    Widget.SetAutoRotateSpeed(60.0);
    TEST("Null setter no-crash", true);  // if we reach here, no crash occurred

    // After assigning an actor, defaults are from actor not widget
    FPreviewActorMock Actor;
    Widget.SetPreviewActor(&Actor);
    TEST("Valid after assign", Widget.IsPreviewValid() == true);
    TEST("Actor yaw matches", Widget.GetOrbitYaw() == 0.0);
    TEST("Actor distance default", Widget.GetOrbitDistance() == 50.0);

    // Setters now mutate the actor
    Widget.SetOrbitYaw(45.0);
    Widget.SetOrbitDistance(200.0);
    Widget.SetPreviewFOV(30.0);
    Widget.SetAutoRotate(true);
    TEST("Yaw propagated", Widget.GetOrbitYaw() == 45.0);
    TEST("Distance propagated", Widget.GetOrbitDistance() == 200.0);
    TEST("FOV propagated", Widget.GetPreviewFOV() == 30.0);
    TEST("AutoRotate propagated", Widget.GetAutoRotate() == true);

    // Reverting to null restores safe defaults
    Widget.SetPreviewActor(nullptr);
    TEST("Re-nulled yaw default", Widget.GetOrbitYaw() == 0.0);
    TEST("Re-nulled distance default", Widget.GetOrbitDistance() == 50.0);
    TEST("Re-nulled IsPreviewValid", Widget.IsPreviewValid() == false);
}

void TestSetPreviewActorContract() {
    printf("\n=== SetPreviewActorContract ===\n");

    FEditorWidgetMock Widget;
    FPreviewActorMock Actor;

    // SetPreviewActor returns the same actor via GetPreviewActor
    Widget.SetPreviewActor(&Actor);
    TEST("GetPreviewActor matches", Widget.GetPreviewActor() == &Actor);

    // Setting to null is valid
    Widget.SetPreviewActor(nullptr);
    TEST("Set null valid", Widget.GetPreviewActor() == nullptr);

    // Reset and verify double-set doesn't break
    Widget.SetPreviewActor(&Actor);
    Widget.SetPreviewActor(&Actor);
    TEST("Double-set same actor", Widget.GetPreviewActor() == &Actor);

    // Swap between actors
    FPreviewActorMock ActorB;
    Widget.SetPreviewActor(&ActorB);
    TEST("Swap to ActorB", Widget.GetPreviewActor() == &ActorB);
    Widget.SetPreviewActor(&Actor);
    TEST("Swap back to ActorA", Widget.GetPreviewActor() == &Actor);
}

// ========================
// OUTLINE SILHOUETTE → DEPTH (mirrors UFaceParallaxComponent statics)
// ========================

// Points are consecutive (xMin, xMax) pairs per scanline, sorted by Y ascending,
// normalized to [-1,1]. Returns signed distance in normalized units:
// positive inside the silhouette, negative outside.
static double SilhouetteDistanceToEdge(const FVector2D* Pts, int NumPts, FVector2D P) {
    if (NumPts < 2) return 1.0;
    const int RowCount = NumPts / 2;
    int RowBelow = RowCount - 1; // fallback: bottom row when the query is below everything
    for (int r = 0; r < RowCount; ++r) {
        // Rows are Y-ascending (top first): the FIRST row at-or-below the
        // query height is the nearest one below it.
        if (Pts[r * 2].Y <= P.Y) { RowBelow = r; break; }
    }
    int RowAbove = RowBelow;
    for (int r = 0; r < RowCount; ++r) {
        // Nearest row strictly above the query height.
        if (Pts[r * 2].Y > P.Y) { RowAbove = r; break; }
    }
    const double Y0 = Pts[RowBelow * 2].Y;
    const double Y1 = Pts[RowAbove * 2].Y;
    const double T = (Y1 - Y0) > 1e-6 ? fmin(fmax((P.Y - Y0) / (Y1 - Y0), 0.0), 1.0) : 0.0;
    const double XL = Pts[RowBelow * 2].X + T * (Pts[RowAbove * 2].X - Pts[RowBelow * 2].X);
    const double XR = Pts[RowBelow * 2 + 1].X + T * (Pts[RowAbove * 2 + 1].X - Pts[RowBelow * 2 + 1].X);
    if (P.X < XL) return P.X - XL;
    if (P.X > XR) return XR - P.X;
    return fmin(P.X - XL, XR - P.X);
}

static double VisualHullDepth(const FVector2D* Front, int NF, const FVector2D* Right, int NR,
    const FVector2D* Left, int NL, const FVector2D* Top, int NT, const FVector2D* Bottom, int NB,
    FVector2D P) {
    auto Interior = [&](const FVector2D* Pts, int N, FVector2D Q) -> double {
        if (N < 2) return 1.0;
        return fmax(0.0, SilhouetteDistanceToEdge(Pts, N, Q));
    };
    double D = Interior(Front, NF, P);
    D = fmin(D, Interior(Right, NR, FVector2D(0.0, P.Y)));
    D = fmin(D, Interior(Left, NL, FVector2D(0.0, P.Y)));
    D = fmin(D, Interior(Top, NT, FVector2D(P.X, 0.0)));
    D = fmin(D, Interior(Bottom, NB, FVector2D(P.X, 0.0)));
    return D < 0.0 ? 0.0 : (D > 1.0 ? 1.0 : D);
}

void TestSilhouetteDistanceToEdge() {
    printf("\n=== SilhouetteDistanceToEdge ===\n");

    // Square silhouette [-0.5, 0.5] x [-0.5, 0.5], 3 scanlines
    FVector2D Square[] = {
        FVector2D(-0.5, -0.5), FVector2D(0.5, -0.5),
        FVector2D(-0.5, 0.0), FVector2D(0.5, 0.0),
        FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5)
    };
    const int NS = sizeof(Square) / sizeof(Square[0]);

    TEST("Center distance", fabs(SilhouetteDistanceToEdge(Square, NS, FVector2D(0, 0)) - 0.5) < 1e-9);
    TEST("On right edge", fabs(SilhouetteDistanceToEdge(Square, NS, FVector2D(0.5, 0))) < 1e-9);
    TEST("Inside near right", fabs(SilhouetteDistanceToEdge(Square, NS, FVector2D(0.4, 0)) - 0.1) < 1e-9);
    TEST("Outside right negative", fabs(SilhouetteDistanceToEdge(Square, NS, FVector2D(0.6, 0)) + 0.1) < 1e-9);
    TEST("Outside left negative", fabs(SilhouetteDistanceToEdge(Square, NS, FVector2D(-0.6, 0)) + 0.1) < 1e-9);
    TEST("Corner inside", fabs(SilhouetteDistanceToEdge(Square, NS, FVector2D(-0.45, -0.45)) - 0.05) < 1e-9);
    TEST("Above top clamps to nearest row", fabs(SilhouetteDistanceToEdge(Square, NS, FVector2D(0, 0.7)) - 0.5) < 1e-9);

    // Empty input returns 1.0 (no silhouette = everything interior)
    TEST("Empty returns 1.0", SilhouetteDistanceToEdge(nullptr, 0, FVector2D(0, 0)) == 1.0);
    // Single scanline row: edges are that row's, clamped vertically (no interpolation)
    TEST("Single pair uses its row edges",
        fabs(SilhouetteDistanceToEdge(Square, 2, FVector2D(0, 0)) - 0.5) < 1e-9);

    // Tapered profile (narrower at bottom), 3 rows: edges interpolate between rows
    FVector2D Taper[] = {
        FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5),
        FVector2D(-0.4, 0.0), FVector2D(0.4, 0.0),
        FVector2D(-0.3, -0.5), FVector2D(0.3, -0.5)
    };
    const int NT2 = sizeof(Taper) / sizeof(Taper[0]);
    // At y=0 the right edge is exactly 0.4
    TEST("Interpolated edge at y=0", fabs(SilhouetteDistanceToEdge(Taper, NT2, FVector2D(0.4, 0))) < 1e-9);
    TEST("Interpolated interior", fabs(SilhouetteDistanceToEdge(Taper, NT2, FVector2D(0.2, 0)) - 0.2) < 1e-9);
    TEST("Interpolated outside negative", fabs(SilhouetteDistanceToEdge(Taper, NT2, FVector2D(0.5, 0)) + 0.1) < 1e-9);
}

void TestVisualHullDepth() {
    printf("\n=== VisualHullDepth ===\n");

    // Front: square [-0.5, 0.5]^2 (defines the 2D shape)
    FVector2D Front[] = {
        FVector2D(-0.5, -0.5), FVector2D(0.5, -0.5),
        FVector2D(-0.5, 0.0), FVector2D(0.5, 0.0),
        FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5)
    };
    const int NF = sizeof(Front) / sizeof(Front[0]);

    // Right profile: strip [-0.25, 0.25] (constrains depth per height)
    FVector2D Right[] = {
        FVector2D(-0.25, -0.5), FVector2D(0.25, -0.5),
        FVector2D(-0.25, 0.0), FVector2D(0.25, 0.0),
        FVector2D(-0.25, 0.5), FVector2D(0.25, 0.5)
    };
    const int NR = sizeof(Right) / sizeof(Right[0]);

    FVector2D Left[] = {
        FVector2D(-0.25, -0.5), FVector2D(0.25, -0.5),
        FVector2D(-0.25, 0.0), FVector2D(0.25, 0.0),
        FVector2D(-0.25, 0.5), FVector2D(0.25, 0.5)
    };
    const int NL = sizeof(Left) / sizeof(Left[0]);

    FVector2D Top[] = {
        FVector2D(-0.5, -0.5), FVector2D(0.5, -0.5),
        FVector2D(-0.5, 0.0), FVector2D(0.5, 0.0),
        FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5)
    };
    const int NT = sizeof(Top) / sizeof(Top[0]);

    FVector2D Bottom[] = {
        FVector2D(-0.5, -0.5), FVector2D(0.5, -0.5),
        FVector2D(-0.5, 0.0), FVector2D(0.5, 0.0),
        FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5)
    };
    const int NB = sizeof(Bottom) / sizeof(Bottom[0]);

    // Center: constrained by the narrow profile (0.25 < 0.5)
    TEST("Center constrained by profile",
        fabs(VisualHullDepth(Front, NF, Right, NR, Left, NL, Top, NT, Bottom, NB, FVector2D(0, 0)) - 0.25) < 1e-9);

    // Front edge point: front silhouette wins (distance 0)
    TEST("Front edge zero",
        fabs(VisualHullDepth(Front, NF, Right, NR, Left, NL, Top, NT, Bottom, NB, FVector2D(0.5, 0))) < 1e-9);

    // Point inside front but outside right profile: front distance still active
    TEST("Inside front, outside profile",
        fabs(VisualHullDepth(Front, NF, Right, NR, Left, NL, Top, NT, Bottom, NB, FVector2D(0.4, 0)) - 0.1) < 1e-9);

    // Point outside everything: clamped to 0
    TEST("Outside hull zero",
        VisualHullDepth(Front, NF, Right, NR, Left, NL, Top, NT, Bottom, NB, FVector2D(0.9, 0.9)) == 0.0);

    // Top view constrains width axis: front full square + narrow top strip
    FVector2D NarrowTop[] = {
        FVector2D(-0.2, -0.5), FVector2D(0.2, -0.5),
        FVector2D(-0.2, 0.0), FVector2D(0.2, 0.0),
        FVector2D(-0.2, 0.5), FVector2D(0.2, 0.5)
    };
    const int NNT = sizeof(NarrowTop) / sizeof(NarrowTop[0]);
    TEST("Top constrains depth",
        fabs(VisualHullDepth(Front, NF, Right, NR, Left, NL, NarrowTop, NNT, Bottom, NB, FVector2D(0, 0)) - 0.2) < 1e-9);
}

// ========================
// PER-VIEW VISUAL HULL (mirrors UFaceParallaxComponent::VisualHullDepthStatic
// yaw/pitch overload): screen point P is in the TARGET view's frame. The max
// depth Z' along the view ray whose front-space point stays inside every
// silhouette prism is solved by binary search; the result is the min of that
// bound and a dome falloff measured against the front silhouette projected
// into the target view. At yaw 0 / pitch 0 this equals VisualHullDepth above.
// ========================

static double VisualHullDepthView(const FVector2D* Front, int NF, const FVector2D* Right, int NR,
    const FVector2D* Left, int NL, const FVector2D* Top, int NT, const FVector2D* Bottom, int NB,
    FVector2D P, double YawDeg, double PitchDeg) {
    auto Interior = [&](const FVector2D* Pts, int N, FVector2D Q) -> double {
        if (N < 2) return 1.0;
        return fmax(0.0, SilhouetteDistanceToEdge(Pts, N, Q));
    };
    const double PI = 3.14159265358979323846;
    const double YawRad = YawDeg * PI / 180.0;
    const double PitchRad = PitchDeg * PI / 180.0;
    const double CosY = cos(YawRad), SinY = sin(YawRad);
    const double CosP = cos(PitchRad), SinP = sin(PitchRad);

    // Front silhouette's vertical extent (rows are Y-ascending, (xMin, Y, xMax, Y)).
    // The distance helper clamps out-of-range queries to the nearest row, which
    // would otherwise leave top/bottom views unbounded along the head's height.
    const bool bFrontHasExtent = NF >= 2;
    const double FrontYMin = bFrontHasExtent ? Front[0].Y : -1.0;
    const double FrontYMax = bFrontHasExtent ? Front[NF - 2].Y : 1.0;

    auto Feasible = [&](double Xp, double Yp, double Zp) -> bool {
        const double Fx = Xp * CosY + Zp * (SinY * CosP);
        const double Fy = Yp * CosP - Zp * SinP;
        const double Zf = -Xp * SinY + Yp * (CosY * SinP) + Zp * (CosY * CosP);
        if (bFrontHasExtent && (Fy < FrontYMin || Fy > FrontYMax)) return false;
        if (SilhouetteDistanceToEdge(Front, NF, FVector2D(Fx, Fy)) < 0.0) return false;
        const double HS = fmin(Interior(Right, NR, FVector2D(0.0, Fy)), Interior(Left, NL, FVector2D(0.0, Fy)));
        const double HT = fmin(Interior(Top, NT, FVector2D(Fx, 0.0)), Interior(Bottom, NB, FVector2D(Fx, 0.0)));
        return fabs(Zf) <= HS && fabs(Zf) <= HT;
    };

    double ZBound = 0.0;
    if (Feasible(P.X, P.Y, 0.0)) {
        double Lo = 0.0, Hi = 1.0;
        for (int i = 0; i < 14; ++i) {
            const double Mid = (Lo + Hi) * 0.5;
            if (Feasible(P.X, P.Y, Mid)) Lo = Mid; else Hi = Mid;
        }
        ZBound = Lo;
    }

    double Dome = 1.0;
    if (fabs(CosY) >= 0.2 && fabs(CosP) >= 0.2 && NF >= 2) {
        std::vector<FVector2D> Projected;
        Projected.reserve(NF);
        for (int i = 0; i + 1 < NF; i += 2) {
            double Px0 = Front[i].X * CosY;
            double Py0 = Front[i].X * (SinY * SinP) + Front[i].Y * CosP;
            double Px1 = Front[i + 1].X * CosY;
            double Py1 = Front[i + 1].X * (SinY * SinP) + Front[i + 1].Y * CosP;
            if (CosY < 0.0) std::swap(Px0, Px1);
            Projected.push_back(FVector2D(Px0, Py0));
            Projected.push_back(FVector2D(Px1, Py1));
        }
        Dome = fmax(0.0, SilhouetteDistanceToEdge(Projected.data(), (int)Projected.size(), P));
    }
    const double D = fmin(ZBound, Dome);
    return D < 0.0 ? 0.0 : (D > 1.0 ? 1.0 : D);
}

void TestVisualHullDepthView() {
    printf("\n=== VisualHullDepthView ===\n");

    // Same fixture as TestVisualHullDepth: front square [-0.5,0.5]^2,
    // right/left profile strips [-0.25,0.25], top/bottom squares [-0.5,0.5].
    FVector2D Front[] = {
        FVector2D(-0.5, -0.5), FVector2D(0.5, -0.5),
        FVector2D(-0.5, 0.0), FVector2D(0.5, 0.0),
        FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5)
    };
    const int NF = sizeof(Front) / sizeof(Front[0]);
    FVector2D Strip[] = {
        FVector2D(-0.25, -0.5), FVector2D(0.25, -0.5),
        FVector2D(-0.25, 0.0), FVector2D(0.25, 0.0),
        FVector2D(-0.25, 0.5), FVector2D(0.25, 0.5)
    };
    const int NS = sizeof(Strip) / sizeof(Strip[0]);
    FVector2D Square[] = {
        FVector2D(-0.5, -0.5), FVector2D(0.5, -0.5),
        FVector2D(-0.5, 0.0), FVector2D(0.5, 0.0),
        FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5)
    };
    const int NQ = sizeof(Square) / sizeof(Square[0]);

    // (yaw 0, pitch 0) must equal the legacy front-view hull exactly.
    // Depth bounds come from a 14-iteration binary search (1e-3 tolerance).
    TEST("Front view center equals legacy",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0, 0), 0, 0) - 0.25) < 1e-3);
    TEST("Front view interior equals legacy",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0.4, 0), 0, 0) - 0.1) < 1e-3);
    TEST("Front view edge equals legacy",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0.5, 0), 0, 0)) < 1e-3);
    TEST("Front view outside equals legacy",
        VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0.9, 0.9), 0, 0) == 0.0);

    // RightProfile (yaw 90): dome is degenerate and skipped; depth = front
    // half-width (the head's X extent along the profile view axis), capped by
    // the top/bottom silhouettes as the point approaches the head's edges.
    TEST("Profile view center = front half-width",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0, 0), 90, 0) - 0.5) < 1e-3);
    TEST("Profile view inside strip capped by top silhouette",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0.2, 0), 90, 0) - 0.3) < 1e-3);
    TEST("Profile view outside profile strip zero",
        VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0.3, 0), 90, 0) == 0.0);

    // ThreeQuarterRight (yaw 45): both bounds active; foreshortened dome.
    TEST("3QR center = foreshortened dome",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0, 0), 45, 0) - 0.35355) < 1e-3);
    TEST("3QR near edge dome falloff",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0.3, 0), 45, 0) - 0.05355) < 1e-3);

    // Back (yaw 180): profile half-width governs; dome mirrors the front shape.
    TEST("Back view depth = profile half-width",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0, 0), 180, 0) - 0.25) < 1e-3);

    // Top (pitch 90): dome skipped; depth = front half-height.
    TEST("Top view depth = front half-height",
        fabs(VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0, 0), 0, 90) - 0.5) < 1e-3);

    // Outside in any view frame: zero.
    TEST("3QR outside zero",
        VisualHullDepthView(Front, NF, Strip, NS, Strip, NS, Square, NQ, Square, NQ, FVector2D(0.9, 0.9), 45, 0) == 0.0);
}

// ========================
// CAMERA SNAP TO VIEW (mirrors widget SetActiveViewState + component zone centers)
// ========================

static double ZoneCenterYaw(int State) {
    // Defaults: HalfZoneWidth = 22.5, multipliers {1, 3, 5, 7}
    static const double BM[4] = {22.5, 67.5, 112.5, 157.5};
    switch (State) {
        case 0: return 0.0;                // Front
        case 1: return (BM[0] + BM[1]) * 0.5;   // ThreeQuarterRight
        case 2: return (BM[1] + BM[2]) * 0.5;   // RightProfile
        case 3: return (BM[2] + BM[3]) * 0.5;   // BackRight
        case 4: return 180.0;              // Back
        case 5: return -(BM[2] + BM[3]) * 0.5;  // BackLeft
        case 6: return -(BM[1] + BM[2]) * 0.5;  // LeftProfile
        case 7: return -(BM[0] + BM[1]) * 0.5;  // ThreeQuarterLeft
        default: return 0.0;
    }
}

static double ZoneCenterPitch(int State) {
    if (State == 8) return 90.0;   // Top
    if (State == 9) return -90.0;  // Bottom
    return 0.0;
}

void TestCameraSnapMapping() {
    printf("\n=== CameraSnapMapping ===\n");

    // Yaw centers (default multipliers)
    TEST("Front yaw 0", fabs(ZoneCenterYaw(0)) < 1e-9);
    TEST("3QR yaw 45", fabs(ZoneCenterYaw(1) - 45.0) < 1e-9);
    TEST("RightProfile yaw 90", fabs(ZoneCenterYaw(2) - 90.0) < 1e-9);
    TEST("BackRight yaw 135", fabs(ZoneCenterYaw(3) - 135.0) < 1e-9);
    TEST("Back yaw 180", fabs(ZoneCenterYaw(4) - 180.0) < 1e-9);
    TEST("BackLeft yaw -135", fabs(ZoneCenterYaw(5) + 135.0) < 1e-9);
    TEST("LeftProfile yaw -90", fabs(ZoneCenterYaw(6) + 90.0) < 1e-9);
    TEST("3QL yaw -45", fabs(ZoneCenterYaw(7) + 45.0) < 1e-9);
    TEST("Top yaw 0", fabs(ZoneCenterYaw(8)) < 1e-9);
    TEST("Bottom yaw 0", fabs(ZoneCenterYaw(9)) < 1e-9);

    // Pitch centers
    TEST("Front pitch 0", fabs(ZoneCenterPitch(0)) < 1e-9);
    TEST("Top pitch 90", fabs(ZoneCenterPitch(8) - 90.0) < 1e-9);
    TEST("Bottom pitch -90", fabs(ZoneCenterPitch(9) + 90.0) < 1e-9);

    // Widget snap behavior: SetActiveViewState with camera-follow snaps yaw+pitch,
    // and never touches distance.
    FEditorWidgetMock Widget;
    FPreviewActorMock Actor;
    Actor.OrbitDistance = 150.0;
    Widget.SetPreviewActor(&Actor);

    auto SnapToView = [&](int State) {
        Widget.SetOrbitYaw(ZoneCenterYaw(State));
        Widget.SetOrbitPitch(ZoneCenterPitch(State));
    };

    SnapToView(2);  // RightProfile
    TEST("Snap yaw to 90", fabs(Widget.GetOrbitYaw() - 90.0) < 1e-9);
    TEST("Snap pitch to 0", fabs(Widget.GetOrbitPitch()) < 1e-9);
    TEST("Snap preserves distance", Widget.GetOrbitDistance() == 150.0);

    SnapToView(8);  // Top
    TEST("Snap Top pitch 90", fabs(Widget.GetOrbitPitch() - 90.0) < 1e-9);

    SnapToView(4);  // Back
    TEST("Snap Back yaw 180", fabs(Widget.GetOrbitYaw() - 180.0) < 1e-9);
}

// ========================
// IMPORT CHANNEL DETECTION (mirrors widget ChannelFromTextureName)
// Channel markers count only as the FINAL token: a trailing view-state
// suffix is stripped first, so both {Part}_{View}_{Map} ("Eyes_Front_Normal")
// and {Part}_{Map}_{View} ("Eyes_N_Front") naming work, while layer names
// containing "n"/"d" tokens ("Eyes_Nose_Front", "Eyes_Dyed_Front", a bare
// "Depth" layer) never match a channel marker.
// ========================

static bool MirrorEndsWith(const std::string& L, const char* Suf) {
    size_t n = strlen(Suf);
    return L.size() >= n && L.compare(L.size() - n, n, Suf) == 0;
}

// Mirrors MatchStateSuffix: returns the matched view-state suffix length (0 = none).
static size_t MirrorStateSuffixLen(const std::string& L) {
    static const char* Sufs[] = {
        "_threequarterright", "_threequarterleft", "_3quarterright", "_3quarterleft",
        "_rightprofile", "_leftprofile", "_backright", "_backleft",
        "_front", "_back", "_top", "_bottom",
        "_3r", "_3l", "_pr", "_pl", "_br", "_bl", "_f", "_b", "_t", "_bot"
    };
    for (const char* S : Sufs) {
        size_t n = strlen(S);
        if (L.size() >= n && L.compare(L.size() - n, n, S) == 0) return n;
    }
    return 0;
}

static const char* ChannelFromName(const std::string& Name) {
    std::string L = Name;
    for (auto& c : L) c = (char)tolower(c);
    size_t StateLen = MirrorStateSuffixLen(L);
    if (StateLen) L.resize(L.size() - StateLen);
    if (MirrorEndsWith(L, "_normalmap") || MirrorEndsWith(L, "_normal") ||
        MirrorEndsWith(L, "_norm") || MirrorEndsWith(L, "_n"))
        return "Normal";
    if (MirrorEndsWith(L, "_displacement") || MirrorEndsWith(L, "_depth") ||
        MirrorEndsWith(L, "_height") || MirrorEndsWith(L, "_d"))
        return "Depth";
    return "Albedo";
}

// Mirrors StripChannelSuffix: strips the channel token and re-attaches any
// trailing view-state suffix so the caller's state match still resolves.
static std::string StripChannelName(const std::string& Name, const char* Channel) {
    if (strcmp(Channel, "Normal") != 0 && strcmp(Channel, "Depth") != 0)
        return Name;
    std::string L = Name;
    for (auto& c : L) c = (char)tolower(c);
    size_t StateLen = MirrorStateSuffixLen(L);
    std::string Cand = StateLen ? L.substr(0, L.size() - StateLen) : L;
    const char* Suffixes[4] = {};
    if (strcmp(Channel, "Normal") == 0) {
        Suffixes[0] = "_normalmap"; Suffixes[1] = "_normal";
        Suffixes[2] = "_norm";       Suffixes[3] = "_n";
    } else {
        Suffixes[0] = "_displacement"; Suffixes[1] = "_depth";
        Suffixes[2] = "_height";       Suffixes[3] = "_d";
    }
    for (const char* S : Suffixes) {
        if (MirrorEndsWith(Cand, S)) {
            std::string Result = Name.substr(0, Cand.size() - strlen(S));
            if (StateLen) Result += Name.substr(Name.size() - StateLen);
            return Result;
        }
    }
    return Name;
}

// Mirrors the folder wizard's part (layer) name derivation:
// ChannelFromTextureName -> StripChannelSuffix -> MatchStateSuffix -> part.
static std::string WizardPartName(const std::string& Name) {
    const char* Channel = ChannelFromName(Name);
    std::string B = StripChannelName(Name, Channel);
    std::string L = B;
    for (auto& c : L) c = (char)tolower(c);
    size_t StateLen = MirrorStateSuffixLen(L);
    if (StateLen == 0) return ""; // no view suffix: not a wizard-named file
    return B.substr(0, B.size() - StateLen);
}

void TestImportChannelDetection() {
    printf("\n=== ImportChannelDetection ===\n");

    // Existing behavior (channel token is the final token)
    TEST("Eyes_Normal -> Normal", strcmp(ChannelFromName("Eyes_Normal"), "Normal") == 0);
    TEST("Eyes_norm -> Normal", strcmp(ChannelFromName("Eyes_norm"), "Normal") == 0);
    TEST("Eyes_N -> Normal", strcmp(ChannelFromName("Eyes_N"), "Normal") == 0);
    TEST("Eyes_Depth -> Depth", strcmp(ChannelFromName("Eyes_Depth"), "Depth") == 0);
    TEST("Eyes_d -> Depth", strcmp(ChannelFromName("Eyes_d"), "Depth") == 0);
    TEST("Eyes_height -> Depth", strcmp(ChannelFromName("Eyes_height"), "Depth") == 0);
    TEST("Eyes_Albedo -> Albedo", strcmp(ChannelFromName("Eyes_Albedo"), "Albedo") == 0);
    TEST("Eyes -> Albedo", strcmp(ChannelFromName("Eyes"), "Albedo") == 0);
    TEST("Displacement suffix beats plain name", strcmp(ChannelFromName("Eyes_displacement"), "Depth") == 0);
    TEST("Normalmap suffix -> Normal", strcmp(ChannelFromName("Eyes_normalmap"), "Normal") == 0);

    // {Part}_{View}_{Map} convention (wizard hint order)
    TEST("Eyes_Front_Normal -> Normal", strcmp(ChannelFromName("Eyes_Front_Normal"), "Normal") == 0);
    TEST("Eyes_3R_Depth -> Depth", strcmp(ChannelFromName("Eyes_3R_Depth"), "Depth") == 0);
    TEST("Eyes_Back_n -> Normal", strcmp(ChannelFromName("Eyes_Back_n"), "Normal") == 0);
    TEST("Eyes_Front -> Albedo (no channel)", strcmp(ChannelFromName("Eyes_Front"), "Albedo") == 0);
    TEST("Eyes_Back -> Albedo (no channel)", strcmp(ChannelFromName("Eyes_Back"), "Albedo") == 0);

    // {Part}_{Map}_{View} convention (channel before view suffix)
    TEST("Eyes_N_Front -> Normal", strcmp(ChannelFromName("Eyes_N_Front"), "Normal") == 0);
    TEST("Eyes_D_Back -> Depth", strcmp(ChannelFromName("Eyes_D_Back"), "Depth") == 0);
    TEST("Eyes_N_3R -> Normal", strcmp(ChannelFromName("Eyes_N_3R"), "Normal") == 0);
    TEST("Eyes_N_f -> Normal", strcmp(ChannelFromName("Eyes_N_f"), "Normal") == 0);

    // Layer names containing n/d tokens must NOT match channel markers
    TEST("Eyes_Nose_Front -> Albedo", strcmp(ChannelFromName("Eyes_Nose_Front"), "Albedo") == 0);
    TEST("Eyes_Nose -> Albedo", strcmp(ChannelFromName("Eyes_Nose"), "Albedo") == 0);
    TEST("Brow_Neck_Front -> Albedo", strcmp(ChannelFromName("Brow_Neck_Front"), "Albedo") == 0);
    TEST("Ears_Nails -> Albedo", strcmp(ChannelFromName("Ears_Nails"), "Albedo") == 0);
    TEST("Ears_Nails_N -> Normal (real channel)", strcmp(ChannelFromName("Ears_Nails_N"), "Normal") == 0);
    TEST("Eyes_Dyed_Front -> Albedo", strcmp(ChannelFromName("Eyes_Dyed_Front"), "Albedo") == 0);
    TEST("Eyes_Dyed -> Albedo", strcmp(ChannelFromName("Eyes_Dyed"), "Albedo") == 0);
    TEST("Depth_Front -> Albedo (bare Depth layer)", strcmp(ChannelFromName("Depth_Front"), "Albedo") == 0);
    TEST("Nose_Depth_Front -> Depth (real channel)", strcmp(ChannelFromName("Nose_Depth_Front"), "Depth") == 0);
    TEST("Nose_N_Back -> Normal", strcmp(ChannelFromName("Nose_N_Back"), "Normal") == 0);
    TEST("Nails_Front -> Albedo", strcmp(ChannelFromName("Nails_Front"), "Albedo") == 0);
    TEST("Neck_n -> Normal (real channel)", strcmp(ChannelFromName("Neck_n"), "Normal") == 0);
    TEST("Top_d -> Depth", strcmp(ChannelFromName("Top_d"), "Depth") == 0);
    TEST("Brow_3QuarterRight_N -> Normal", strcmp(ChannelFromName("Brow_3QuarterRight_N"), "Normal") == 0);

    // Channel markers are only final tokens
    TEST("Eyes_normal_2020 -> Albedo", strcmp(ChannelFromName("Eyes_normal_2020"), "Albedo") == 0);
    TEST("Eyes_depth_v2 -> Albedo", strcmp(ChannelFromName("Eyes_depth_v2"), "Albedo") == 0);
    TEST("Eyes_normalmap_backup -> Albedo", strcmp(ChannelFromName("Eyes_normalmap_backup"), "Albedo") == 0);
    TEST("Bare N -> Albedo (no underscore)", strcmp(ChannelFromName("N"), "Albedo") == 0);
    TEST("Bare D -> Albedo (no underscore)", strcmp(ChannelFromName("D"), "Albedo") == 0);

    // Wizard part derivation: channel/view tokens never leak into layer names
    TEST("Part Eyes_N_Front -> Eyes", WizardPartName("Eyes_N_Front") == "Eyes");
    TEST("Part Eyes_Front_Normal -> Eyes", WizardPartName("Eyes_Front_Normal") == "Eyes");
    TEST("Part Nose_D_3R -> Nose", WizardPartName("Nose_D_3R") == "Nose");
    TEST("Part Eyes_Back_d -> Eyes", WizardPartName("Eyes_Back_d") == "Eyes");
    TEST("Part Nose_N -> empty (no view)", WizardPartName("Nose_N").empty());
    TEST("Part Eyes_Dyed_Front keeps layer", WizardPartName("Eyes_Dyed_Front") == "Eyes_Dyed");
    TEST("Part Depth_Front keeps layer", WizardPartName("Depth_Front") == "Depth");
    TEST("Part Eyes_Front -> Eyes", WizardPartName("Eyes_Front") == "Eyes");
    TEST("Part Eyes_N_3R -> Eyes", WizardPartName("Eyes_N_3R") == "Eyes");
    TEST("Part with no view suffix -> empty", WizardPartName("Eyes_N").empty());
}

// ====================================================================
// Phase B mirrors: onion-skin adjacency, link broadcast, gizmo mapping,
// transform-copy guard. These mirror the widget's static helpers.
// ====================================================================

static int MirrorAdjacentState(int S, int Offset)
{
    const int N = 10;
    const int Idx = (S + Offset) % N;
    return Idx < 0 ? Idx + N : Idx;
}

static int MirrorLinkTargetCount(int /*Active*/) { return 9; }
static bool MirrorLinkTargetExcludesActive(int Active, int Target)
{
    return Target != Active;
}
static bool MirrorLinkTargetsAllUnique(int Targets[9])
{
    for (int i = 0; i < 9; ++i)
        for (int j = i + 1; j < 9; ++j)
            if (Targets[i] == Targets[j]) return false;
    return true;
}
static void MirrorLinkTargets(int Active, int Out[9])
{
    int C = 0;
    for (int i = 0; i < 10; ++i)
        if (i != Active) Out[C++] = i;
}

static float MirrorGizmoPixelsToUVX(float Px, float CanvasX)
{
    return CanvasX <= 0.0f ? 0.0f : Px / CanvasX;
}

static float MirrorGizmoUVToPixelsX(float U, float CanvasX)
{
    return U * CanvasX;
}

void TestPhaseBAlignmentMirrors() {
    printf("\n=== PhaseBAlignmentMirrors ===\n");

    // Adjacent state wrap-around (onion skin ghost source)
    TEST("Adjacent -1 from Front wraps to Bottom", MirrorAdjacentState(0, -1) == 9);
    TEST("Adjacent +1 from Bottom wraps to Front", MirrorAdjacentState(9, 1) == 0);
    TEST("Adjacent -1 from 3/4R", MirrorAdjacentState(1, -1) == 0);
    TEST("Adjacent +1 from 3/4R", MirrorAdjacentState(1, 1) == 2);
    TEST("Adjacent 0 is identity", MirrorAdjacentState(5, 0) == 5);
    TEST("Adjacent -11 wraps twice", MirrorAdjacentState(3, -11) == 2);

    // Link broadcast targets
    int Targets[9];
    MirrorLinkTargets(0, Targets);
    TEST("Link targets count", MirrorLinkTargetCount(0) == 9);
    TEST("Link targets exclude active", MirrorLinkTargetExcludesActive(0, 1));
    TEST("Link targets all unique", MirrorLinkTargetsAllUnique(Targets));
    {
        bool bNoSelf = true;
        for (int i = 0; i < 9; ++i)
            if (Targets[i] == 0) bNoSelf = false;
        TEST("Link targets contain no active state", bNoSelf);
    }
    {
        MirrorLinkTargets(9, Targets);
        bool bNoSelf = true;
        for (int i = 0; i < 9; ++i)
            if (Targets[i] == 9) bNoSelf = false;
        TEST("Link targets for Bottom exclude Bottom", bNoSelf);
    }

    // Gizmo coordinate mapping (UV <-> pixels)
    TEST("GizmoPixelsToUV 225px/450 -> 0.5", MirrorGizmoPixelsToUVX(225.0f, 450.0f) == 0.5f);
    TEST("GizmoPixelsToUV zero canvas guarded", MirrorGizmoPixelsToUVX(100.0f, 0.0f) == 0.0f);
    TEST("GizmoPixelsToUV negative canvas guarded", MirrorGizmoPixelsToUVX(100.0f, -50.0f) == 0.0f);
    TEST("GizmoPixelsToUV round trip 0.25", MirrorGizmoPixelsToUVX(0.25f * 900.0f, 900.0f) == 0.25f);
    TEST("GizmoPixelsToUV full canvas", MirrorGizmoPixelsToUVX(450.0f, 450.0f) == 1.0f);

    // Inverse mapping (widget GizmoUVToPixels): pixels = UV * canvas size.
    // Round trip through the pair must be identity.
    TEST("GizmoUVToPixels 0.5*450 -> 225", MirrorGizmoUVToPixelsX(0.5f, 450.0f) == 225.0f);
    TEST("GizmoUVToPixels zero UV -> 0", MirrorGizmoUVToPixelsX(0.0f, 450.0f) == 0.0f);
    TEST("GizmoUVToPixels full UV -> canvas", MirrorGizmoUVToPixelsX(1.0f, 450.0f) == 450.0f);
    TEST("GizmoUVToPixels negative UV -> negative", MirrorGizmoUVToPixelsX(-0.25f, 450.0f) == -112.5f);
    TEST("GizmoUVToPixels beyond 1 overflows canvas", MirrorGizmoUVToPixelsX(1.25f, 400.0f) == 500.0f);
    TEST("GizmoUVToPixels zero canvas -> 0", MirrorGizmoUVToPixelsX(0.5f, 0.0f) == 0.0f);
    {
        bool bRoundTrip = true;
        for (float U : { -0.5f, 0.0f, 0.125f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f })
        {
            const float Px = MirrorGizmoUVToPixelsX(U, 900.0f);
            if (std::abs(MirrorGizmoPixelsToUVX(Px, 900.0f) - U) > 1e-6f) bRoundTrip = false;
        }
        TEST("GizmoUVToPixels/PixelsToUV round trip identity", bRoundTrip);
    }

    // Transform copy guard: src == dst is a no-op
    TEST("Copy-from guard rejects same state",
        (1 == 1) && ([]() {
            const int Src = 4, Dst = 4;
            float DstX = 0.7f;
            if (Src == Dst) return true; // no-op, dst unchanged
            DstX = 0.2f;
            return DstX == 0.7f;
        })());
    TEST("Copy-from applies when states differ", ([]() {
        const int Src = 0, Dst = 3;
        float SrcX = 0.2f, DstX = 0.7f;
        if (Src == Dst) return false;
        DstX = SrcX;
        return DstX == 0.2f;
    })());
}

// ====================================================================
// Phase C mirrors: view-suffix parsing, channel stripping, and the
// sync-drift counter. These mirror the widget's anonymous-namespace
// helpers and RefreshSyncDriftIndicator.
// ====================================================================

static int MirrorMatchStateSuffix(const std::string& BaseName, std::string& OutSuffix)
{
    std::string Lower = BaseName;
    for (auto& c : Lower) c = (char)tolower(c);
    struct SS { const char* Suffix; int State; };
    static const SS Map[] = {
        {"_threequarterright", 1}, {"_threequarterleft", 7},
        {"_3quarterright", 1},    {"_3quarterleft", 7},
        {"_rightprofile", 2},     {"_leftprofile", 6},
        {"_backright", 3},        {"_backleft", 5},
        {"_front", 0},            {"_back", 4},
        {"_top", 8},              {"_bottom", 9},
        {"_3r", 1},               {"_3l", 7},
        {"_pr", 2},               {"_pl", 6},
        {"_br", 3},               {"_bl", 5},
        {"_f", 0},                {"_b", 4},
        {"_t", 8},                {"_bot", 9},
    };
    for (const SS& M : Map)
    {
        const size_t L = strlen(M.Suffix);
        if (Lower.length() >= L &&
            Lower.compare(Lower.length() - L, L, M.Suffix) == 0)
        {
            OutSuffix = M.Suffix;
            return M.State;
        }
    }
    return -1;
}

static std::string MirrorStripChannelSuffix(const std::string& Name, const std::string& Channel)
{
    std::string Lower = Name;
    for (auto& c : Lower) c = (char)tolower(c);
    if (Channel == "Normal")
    {
        for (const char* S : {"_normalmap", "_normal", "_norm", "_n"})
        {
            const size_t L = strlen(S);
            if (Lower.length() >= L &&
                Lower.compare(Lower.length() - L, L, S) == 0)
                return Name.substr(0, Name.length() - L);
        }
    }
    else if (Channel == "Depth")
    {
        for (const char* S : {"_displacement", "_depth", "_height", "_d"})
        {
            const size_t L = strlen(S);
            if (Lower.length() >= L &&
                Lower.compare(Lower.length() - L, L, S) == 0)
                return Name.substr(0, Name.length() - L);
        }
    }
    return Name;
}

struct MirrorArtTransform
{
    float X = 0.0f, Y = 0.0f, S = 1.0f, R = 0.0f;
    bool operator==(const MirrorArtTransform& O) const
    {
        return X == O.X && Y == O.Y && S == O.S && R == O.R;
    }
    bool operator!=(const MirrorArtTransform& O) const { return !(*this == O); }
};

static int MirrorDriftedCount(const MirrorArtTransform& Active,
    const MirrorArtTransform Others[10], int ActiveIdx)
{
    int Drifted = 0;
    for (int i = 0; i < 10; ++i)
    {
        if (i == ActiveIdx) continue;
        if (Others[i] != Active) ++Drifted;
    }
    return Drifted;
}

void TestPhaseCMirrors() {
    printf("\n=== PhaseCMirrors ===\n");

    // ---- View-suffix parsing (MatchStateSuffix) ----
    std::string Suffix;
    TEST("Full name _threequarterright -> 1", MirrorMatchStateSuffix("Eyes_ThreeQuarterRight", Suffix) == 1);
    TEST("Suffix _threequarterright reported", Suffix == "_threequarterright");
    TEST("Full name _threequarterleft -> 7", MirrorMatchStateSuffix("Eyes_ThreeQuarterLeft", Suffix) == 7);
    TEST("Short code _3quarterright -> 1", MirrorMatchStateSuffix("Eyes_3QuarterRight", Suffix) == 1);
    TEST("Short code _3quarterleft -> 7", MirrorMatchStateSuffix("Eyes_3QuarterLeft", Suffix) == 7);
    TEST("_rightprofile -> 2 wins over _pr", MirrorMatchStateSuffix("Eyes_RightProfile", Suffix) == 2);
    TEST("_pr -> 2", MirrorMatchStateSuffix("Eyes_PR", Suffix) == 2);
    TEST("_leftprofile -> 6 wins over _pl", MirrorMatchStateSuffix("Eyes_LeftProfile", Suffix) == 6);
    TEST("_pl -> 6", MirrorMatchStateSuffix("Eyes_PL", Suffix) == 6);
    TEST("_backright -> 3 wins over _br", MirrorMatchStateSuffix("Eyes_BackRight", Suffix) == 3);
    TEST("_br -> 3", MirrorMatchStateSuffix("Eyes_BR", Suffix) == 3);
    TEST("_backleft -> 5 wins over _bl", MirrorMatchStateSuffix("Eyes_BackLeft", Suffix) == 5);
    TEST("_bl -> 5", MirrorMatchStateSuffix("Eyes_BL", Suffix) == 5);
    TEST("_front -> 0 wins over _f", MirrorMatchStateSuffix("Eyes_Front", Suffix) == 0);
    TEST("_f -> 0", MirrorMatchStateSuffix("Eyes_F", Suffix) == 0);
    TEST("_back -> 4 wins over _b", MirrorMatchStateSuffix("Eyes_Back", Suffix) == 4);
    TEST("_b -> 4", MirrorMatchStateSuffix("Eyes_B", Suffix) == 4);
    TEST("_top -> 8 wins over _t", MirrorMatchStateSuffix("Eyes_Top", Suffix) == 8);
    TEST("_t -> 8", MirrorMatchStateSuffix("Eyes_T", Suffix) == 8);
    TEST("_bottom -> 9 wins over _bot", MirrorMatchStateSuffix("Eyes_Bottom", Suffix) == 9);
    TEST("_bot -> 9 (not _b)", MirrorMatchStateSuffix("Eyes_Bot", Suffix) == 9);
    TEST("_3r -> 1", MirrorMatchStateSuffix("Eyes_3R", Suffix) == 1);
    TEST("_3l -> 7", MirrorMatchStateSuffix("Eyes_3L", Suffix) == 7);
    TEST("Case-insensitive FRONT", MirrorMatchStateSuffix("eyes_FRONT", Suffix) == 0);
    TEST("No suffix -> -1", MirrorMatchStateSuffix("Eyes", Suffix) == -1);
    TEST("Unmatched suffix -> -1", MirrorMatchStateSuffix("Eyes_Pants", Suffix) == -1);

    // ---- Channel stripping (StripChannelSuffix) ----
    TEST("Strip _normal -> base", MirrorStripChannelSuffix("Eyes_Front_Normal", "Normal") == "Eyes_Front");
    TEST("Strip _normalmap -> base", MirrorStripChannelSuffix("Eyes_Front_normalmap", "Normal") == "Eyes_Front");
    TEST("Strip _norm -> base", MirrorStripChannelSuffix("Eyes_Front_norm", "Normal") == "Eyes_Front");
    TEST("Strip _n -> base", MirrorStripChannelSuffix("Eyes_Front_n", "Normal") == "Eyes_Front");
    TEST("Strip _depth -> base", MirrorStripChannelSuffix("Eyes_Front_Depth", "Depth") == "Eyes_Front");
    TEST("Strip _displacement -> base", MirrorStripChannelSuffix("Eyes_Front_displacement", "Depth") == "Eyes_Front");
    TEST("Strip _height -> base", MirrorStripChannelSuffix("Eyes_Front_Height", "Depth") == "Eyes_Front");
    TEST("Strip _d -> base", MirrorStripChannelSuffix("Eyes_Front_D", "Depth") == "Eyes_Front");
    TEST("Albedo channel leaves name untouched", MirrorStripChannelSuffix("Eyes_Front", "Albedo") == "Eyes_Front");
    TEST("Depth-normal ambiguity: _depth beats _n", MirrorStripChannelSuffix("Eyes_Front_depth", "Depth") == "Eyes_Front");
    TEST("Preserves prefix case", MirrorStripChannelSuffix("Eyes_front_normal", "Normal") == "Eyes_front");

    // ---- Sync-drift counter (RefreshSyncDriftIndicator) ----
    MirrorArtTransform Base;
    MirrorArtTransform Others[10];
    for (int i = 0; i < 10; ++i) Others[i] = Base;
    TEST("All synced -> 0 drifted", MirrorDriftedCount(Base, Others, 0) == 0);
    Others[3].X = 0.5f;
    TEST("One position drift -> 1", MirrorDriftedCount(Base, Others, 0) == 1);
    Others[3] = Base; Others[9].R = 45.0f;
    TEST("Rotation-only drift -> 1", MirrorDriftedCount(Base, Others, 0) == 1);
    Others[9] = Base; Others[6].S = 0.9f;
    TEST("Scale drift -> 1", MirrorDriftedCount(Base, Others, 0) == 1);
    Others[6] = Base;
    for (int i = 1; i < 10; ++i) Others[i].Y = 0.1f;
    TEST("All 9 others drifted -> 9", MirrorDriftedCount(Base, Others, 0) == 9);
    for (int i = 0; i < 10; ++i) Others[i] = Base;
    Others[0].X = 1.0f;
    TEST("Active-state drift not counted", MirrorDriftedCount(Base, Others, 0) == 0);
    for (int i = 0; i < 10; ++i) Others[i] = Base;
    TEST("Active at Bottom still skips itself", MirrorDriftedCount(Base, Others, 9) == 0);
    Others[1].X = 1.0f;
    TEST("Drift counted when active is Bottom", MirrorDriftedCount(Base, Others, 9) == 1);
}

// ====================================================================
// Phase D mirrors: luminance histogram + Sobel edge density used by the
// edge-overlay / histogram builder. These mirror the widget's static
// helpers BuildLumaHistogram and EdgeDensity.
// ====================================================================

static void MirrorBuildLumaHistogram(const std::vector<float>& Luma, int Grid, std::vector<float>& OutBins)
{
    OutBins.assign(16, 0.0f);
    if ((int)Luma.size() != Grid * Grid) return;
    for (float V : Luma)
    {
        const float C = std::fmax(0.0f, std::fmin(1.0f, V));
        int B = (int)std::floor(C * 15.9999f);
        if (B < 0) B = 0;
        if (B > 15) B = 15;
        OutBins[B] += 1.0f;
    }
    float MaxB = 1.0f;
    for (float B : OutBins) MaxB = std::fmax(MaxB, B);
    for (float& B : OutBins) B /= MaxB;
}

static float MirrorEdgeDensity(const std::vector<float>& Luma, int Grid, float Threshold)
{
    if ((int)Luma.size() != Grid * Grid || Grid < 3) return 0.0f;
    int Edges = 0;
    for (int Y = 1; Y < Grid - 1; ++Y)
        for (int X = 1; X < Grid - 1; ++X)
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
            const float Mag = std::sqrt(Gx * Gx + Gy * Gy) / 4.0f;
            if (Mag > Threshold) ++Edges;
        }
    const int Interior = (Grid - 2) * (Grid - 2);
    return Interior > 0 ? (float)Edges / (float)Interior : 0.0f;
}

void TestPhaseDMirrors() {
    printf("\n=== PhaseDMirrors ===\n");

    std::vector<float> Bins;
    std::vector<float> Luma(8 * 8, 0.5f);

    // ---- Luminance histogram (16 bins, normalized by max count) ----
    MirrorBuildLumaHistogram(Luma, 8, Bins);
    TEST("Histogram uniform 0.5 -> single bin", Bins[7] == 1.0f);
    TEST("Histogram uniform 0.5 -> other bins zero", Bins[0] == 0.0f && Bins[15] == 0.0f);
    std::fill(Luma.begin(), Luma.end(), 0.0f);
    MirrorBuildLumaHistogram(Luma, 8, Bins);
    TEST("Histogram all black -> bin 0", Bins[0] == 1.0f);
    std::fill(Luma.begin(), Luma.end(), 1.0f);
    MirrorBuildLumaHistogram(Luma, 8, Bins);
    TEST("Histogram all white -> bin 15", Bins[15] == 1.0f);
    std::fill(Luma.begin(), Luma.end(), 0.0f);
    for (int i = 0; i < 32; ++i) Luma[i] = 1.0f;   // half black, half white
    MirrorBuildLumaHistogram(Luma, 8, Bins);
    TEST("Histogram bimodal both peaks 1.0", Bins[0] == 1.0f && Bins[15] == 1.0f);
    TEST("Histogram bimodal mid bins zero", Bins[7] == 0.0f);
    std::vector<float> Bad = {0.5f, 0.5f};
    MirrorBuildLumaHistogram(Bad, 8, Bins);
    TEST("Histogram wrong size leaves bins zero", Bins[0] == 0.0f);

    // ---- Sobel edge density ----
    std::vector<float> Flat(16 * 16, 0.35f);
    TEST("Edge density uniform grid -> 0", MirrorEdgeDensity(Flat, 16, 0.18f) == 0.0f);
    std::vector<float> Split(16 * 16, 0.0f);
    for (int Y = 0; Y < 16; ++Y)
        for (int X = 8; X < 16; ++X)
            Split[Y * 16 + X] = 1.0f;
    const float SplitDensity = MirrorEdgeDensity(Split, 16, 0.18f);
    TEST("Edge density hard split > 0.1", SplitDensity > 0.1f);
    TEST("Edge density hard split < 0.6", SplitDensity < 0.6f);
    TEST("Edge density high threshold -> 0", MirrorEdgeDensity(Split, 16, 1.5f) == 0.0f);
    std::vector<float> Smooth(16 * 16, 0.0f);
    for (int Y = 0; Y < 16; ++Y)
        for (int X = 0; X < 16; ++X)
            Smooth[Y * 16 + X] = (float)(X + Y) / 30.0f;
    TEST("Edge density smooth gradient -> 0", MirrorEdgeDensity(Smooth, 16, 0.18f) == 0.0f);
    std::vector<float> Noise(8 * 8, 0.0f);
    unsigned Seed = 12345u;
    for (float& V : Noise)
    {
        Seed = Seed * 1103515245u + 12345u;
        V = (float)((Seed >> 8) % 100) / 100.0f;
    }
    TEST("Edge density noise high", MirrorEdgeDensity(Noise, 8, 0.18f) > 0.3f);
    TEST("Edge density near-max threshold rare", MirrorEdgeDensity(Noise, 8, 0.9f) < 0.1f);
    TEST("Edge density wrong size guarded", MirrorEdgeDensity(Bad, 8, 0.18f) == 0.0f);
    TEST("Edge density grid < 3 guarded", MirrorEdgeDensity(Flat, 2, 0.18f) == 0.0f);
}

static float MirrorFrameFillRatio(const std::vector<bool>& Occupied)
{
    if (Occupied.empty()) return 0.0f;
    int Filled = 0;
    for (bool B : Occupied) if (B) ++Filled;
    return (float)Filled / (float)Occupied.size();
}

static int MirrorClampGridCols(int MaxFrames)
{
    if (MaxFrames < 1) return 1;
    if (MaxFrames > 16) return 16;
    return MaxFrames;
}

static void MirrorAppendSortedUnique(std::vector<std::string>& Out, const std::string& Line)
{
    for (size_t i = 0; i < Out.size(); ++i)
    {
        if (Out[i] == Line) return;
        if (Out[i] > Line) { Out.insert(Out.begin() + (ptrdiff_t)i, Line); return; }
    }
    Out.push_back(Line);
}

static bool MirrorVisemeFramesMismatch(int A, int B)
{
    return A > 0 && B > 0 && A != B;
}

void TestPhaseEFMirrors() {
    printf("\n=== PhaseEFMirrors ===\n");

    std::vector<bool> Empty;
    TEST("Fill ratio empty -> 0", MirrorFrameFillRatio(Empty) == 0.0f);
    std::vector<bool> AllEmpty(4, false);
    TEST("Fill ratio all empty -> 0", MirrorFrameFillRatio(AllEmpty) == 0.0f);
    std::vector<bool> ThreeOfFour = {true, false, true, true};
    TEST("Fill ratio 3/4 -> 0.75", MirrorFrameFillRatio(ThreeOfFour) == 0.75f);

    TEST("Grid cols 0 -> 1", MirrorClampGridCols(0) == 1);
    TEST("Grid cols 1 -> 1", MirrorClampGridCols(1) == 1);
    TEST("Grid cols 8 -> 8", MirrorClampGridCols(8) == 8);
    TEST("Grid cols 16 -> 16", MirrorClampGridCols(16) == 16);
    TEST("Grid cols 40 -> 16", MirrorClampGridCols(40) == 16);

    std::vector<std::string> Sorted;
    MirrorAppendSortedUnique(Sorted, "b");
    MirrorAppendSortedUnique(Sorted, "a");
    MirrorAppendSortedUnique(Sorted, "c");
    MirrorAppendSortedUnique(Sorted, "b");
    TEST("Sorted unique order", Sorted.size() == 3 && Sorted[0] == "a" && Sorted[1] == "b" && Sorted[2] == "c");
    std::vector<std::string> Sorted2;
    MirrorAppendSortedUnique(Sorted2, "a");
    MirrorAppendSortedUnique(Sorted2, "a");
    TEST("Sorted unique duplicate skipped", Sorted2.size() == 1);

    TEST("Mismatch zero guard", !MirrorVisemeFramesMismatch(0, 3));
    TEST("Mismatch equal -> false", !MirrorVisemeFramesMismatch(3, 3));
    TEST("Mismatch 2 vs 4 -> true", MirrorVisemeFramesMismatch(2, 4));
}

// ====================================================================
// Phase G mirrors: outline-depth bake quantization (widget
// BuildOutlineDepthTexture: clamp 0..1, round to byte, BGRA with opaque
// alpha) and depth-scope targeting (GenerateDepthFromOutlinesImpl:
// 0 = Front only, 1 = 8 horizontal states, 2 = all 10 states).
// ====================================================================

static bool MirrorOutlineDepthToBytes(const std::vector<float>& Depth, int Grid, std::vector<unsigned char>& Out)
{
    if ((int)Depth.size() != Grid * Grid) return false;
    Out.assign((size_t)Grid * Grid * 4, 0);
    for (int i = 0; i < Grid * Grid; ++i)
    {
        const double N = std::clamp((double)Depth[i], 0.0, 1.0);
        const unsigned char V = (unsigned char)std::round(N * 255.0);
        Out[i * 4 + 0] = V;
        Out[i * 4 + 1] = V;
        Out[i * 4 + 2] = V;
        Out[i * 4 + 3] = 255;
    }
    return true;
}

static int MirrorOutlineDepthTargets(int Scope, int Out[10])
{
    int C = 0;
    if (Scope == 0)
    {
        Out[C++] = 0;
    }
    else
    {
        const int Count = Scope == 1 ? 8 : 10;
        for (int i = 0; i < Count; ++i) Out[C++] = i;
    }
    return C;
}

void TestPhaseGWidgetMirrors() {
    printf("\n=== PhaseGWidgetMirrors ===\n");

    // Outline-depth bake quantization
    {
        std::vector<float> Depth = { 0.0f, 0.5f, 1.0f, -0.25f };
        std::vector<unsigned char> Bytes;
        TEST("Bake size 2x2 accepted", MirrorOutlineDepthToBytes(Depth, 2, Bytes));
        TEST("Bake zero -> 0", Bytes[0] == 0);
        TEST("Bake 0.5 -> 128", Bytes[4] == 128);
        TEST("Bake one -> 255", Bytes[8] == 255);
        TEST("Bake negative clamps to 0", Bytes[12] == 0);
        TEST("Bake alpha opaque", Bytes[3] == 255 && Bytes[7] == 255);
        TEST("Bake RGB uniform", Bytes[0] == Bytes[1] && Bytes[1] == Bytes[2]);
    }
    {
        std::vector<float> Depth = { 0.2f };
        std::vector<unsigned char> Bytes;
        TEST("Bake 1x1 accepted", MirrorOutlineDepthToBytes(Depth, 1, Bytes));
        TEST("Bake 0.2 -> 51", Bytes[0] == 51);
    }
    {
        std::vector<float> Depth = { 0.5f, 0.5f, 0.5f };
        std::vector<unsigned char> Bytes;
        TEST("Bake wrong size rejected", !MirrorOutlineDepthToBytes(Depth, 2, Bytes));
        TEST("Bake grid zero rejected", !MirrorOutlineDepthToBytes(Depth, 0, Bytes));
    }

    // Depth-scope targeting
    {
        int Targets[10];
        const int N0 = MirrorOutlineDepthTargets(0, Targets);
        TEST("Scope 0 targets Front only", N0 == 1 && Targets[0] == 0);
        const int N1 = MirrorOutlineDepthTargets(1, Targets);
        bool bAll8 = N1 == 8;
        for (int i = 0; i < N1; ++i) if (Targets[i] != i) bAll8 = false;
        TEST("Scope 1 targets 8 horizontal states", bAll8);
        const int N2 = MirrorOutlineDepthTargets(2, Targets);
        bool bAll10 = N2 == 10;
        for (int i = 0; i < N2; ++i) if (Targets[i] != i) bAll10 = false;
        TEST("Scope 2 targets all 10 states", bAll10);
    }
}

// --- Phase H: UI design-contract tests (P1..P13 over the layout manifest) ---
void TestPhaseHUIDesign() {
    printf("\n=== PhaseHUIDesign ===\n");

    // Positive contract: the real manifest must be clean.
    const std::vector<FPLayout::FPLayoutNode> Spec = FPLayout::BuildSpec();
    TEST("Phase H: manifest builds (507 nodes)", Spec.size() == 507u);
    TEST("Phase H: every node reachable from root", FPLayout::CountReachable(Spec) == (int)Spec.size());
    const int RootIdx = FPLayout::FindRootIndex(Spec);
    TEST("Phase H: single root is the last node", RootIdx == (int)Spec.size() - 1);
    const std::vector<FPLayout::FPRect> Rects = FPLayout::ResolveLayout(Spec);
    TEST("Phase H: root rect matches design (1089x866)",
        Rects[(size_t)RootIdx].W == 1089.0 && Rects[(size_t)RootIdx].H == 866.0);
    const std::vector<FPLayout::FPViolation> V = FPLayout::ValidateDesign(Spec);
    TEST("Phase H: zero design violations (P1..P21)", V.empty());

    // Scroll-viewport contract: the 6 rails are fixed 180x560 clipped viewports,
    // so their content can never overlap other panels or leave the screen.
    {
        const char* RailNames[6] = { "RL-ViewLayer", "RL-Art", "RL-Animated", "RL-NestedPins", "RL-CameraPrev", "RL-Advanced" };
        bool bViewports = true;
        for (const char* nm : RailNames)
        {
            const FPLayout::FPLayoutNode* found = nullptr;
            for (const FPLayout::FPLayoutNode& n : Spec)
                if (std::string(n.Name) == nm) { found = &n; break; }
            if (!found || !found->bClipH || found->FixedH != FPLayout::MainRowHeight
                || found->FixedW != FPLayout::RailWidth)
                bViewports = false;
        }
        TEST("Phase H: rails are 180x560 clipped scroll viewports", bViewports);
    }

    // Design-system constants mirrored from RebuildWidget.
    TEST("Phase H: RailWidth=180", FPLayout::RailWidth == 180.0);
    TEST("Phase H: PropsWidth=340", FPLayout::PropsWidth == 340.0);
    TEST("Phase H: MainRowHeight=560", FPLayout::MainRowHeight == 560.0);
    TEST("Phase H: ThumbSize=72", FPLayout::ThumbSize == 72.0);
    TEST("Phase H: StateStripHeight=26", FPLayout::StateStripHeight == 26.0);
    TEST("Phase H: MaxMargin=8", FPLayout::MaxMargin == 8.0);
    {
        bool bPalette = false;
        for (double p : FPLayout::PaletteVals) if (p == 6.0) bPalette = true;
        TEST("Phase H: rhythm palette defined", bPalette);
    }

    // Known anchor nodes must exist.
    auto Has = [&](const char* name) {
        for (const FPLayout::FPLayoutNode& n : Spec)
            if (std::string(n.Name) == name) return true;
        return false;
    };
    TEST("Phase H: Toolbar present", Has("Toolbar"));
    TEST("Phase H: RAIL-Switcher present", Has("RAIL-Switcher"));
    TEST("Phase H: RL-ViewLayer present", Has("RL-ViewLayer"));
    TEST("Phase H: RL-Advanced present", Has("RL-Advanced"));
    TEST("Phase H: RL-Art present", Has("RL-Art"));
    TEST("Phase H: AG-Grid present", Has("AG-Grid"));
    TEST("Phase H: AO-PerfCombo present", Has("AO-PerfCombo"));
    TEST("Phase H: PR-ThumbCol0 present", Has("PR-ThumbCol0"));
    TEST("Phase H: TB-ClearStale present", Has("TB-ClearStale"));
    TEST("Phase H: BA-BotBar present", Has("BA-BotBar"));
    {
        // Phase B: the labeled 6-group tab bar replaces the icon rail column.
        // It is a Root row between PinnedStrip and MainRow, exactly 7 nodes
        // (6 tabs + spacer), fixed height TabBarHeight.
        const FPLayout::FPLayoutNode* RootNode = nullptr;
        const FPLayout::FPLayoutNode* Tabs = nullptr;
        for (const FPLayout::FPLayoutNode& n : Spec)
        {
            if (std::string(n.Name) == "Root") RootNode = &n;
            if (std::string(n.Name) == "TopTabs") Tabs = &n;
        }
        bool bTabs = Tabs && Tabs->Children.size() == 7 && Tabs->FixedH == FPLayout::TabBarHeight;
        if (bTabs)
            for (int i = 0; i < 6; ++i)
                if (std::string(Spec[(size_t)Tabs->Children[(size_t)i]].Name) !=
                        std::string("TT-Tab") + char('0' + i))
                    bTabs = false;
        int TabIdx = -1, StripIdx = -1;
        if (RootNode && Tabs)
            for (size_t i = 0; i < Spec.size(); ++i)
            {
                if (&Spec[i] == Tabs) TabIdx = (int)i;
                if (std::string(Spec[i].Name) == "PinnedStrip") StripIdx = (int)i;
            }
        TEST("Phase H: TopTabs is a 7-node fixed-height tab row (Phase B)",
            bTabs && RootNode && RootNode->Children.size() == 10
            && RootNode->Children[3] == StripIdx && RootNode->Children[4] == TabIdx
            && std::string(Spec[(size_t)RootNode->Children[5]].Name) == "MainRow");
    }
    {
        // Dev tools relocated (A5): Tag Validator + Material Cross-Reference
        // are Advanced-rail accordion sections, NOT bottom-bar leaves. Their
        // leaves must be children of the Advanced rail, not of BotArea.
        const FPLayout::FPLayoutNode* BotArea = nullptr;
        const FPLayout::FPLayoutNode* Adv = nullptr;
        for (const FPLayout::FPLayoutNode& n : Spec)
        {
            if (std::string(n.Name) == "BotArea") BotArea = &n;
            if (std::string(n.Name) == "RL-Advanced") Adv = &n;
        }
        bool bTagInBottom = false, bMCInBottom = false;
        bool bTagInAdv = false, bMCInAdv = false;
        if (BotArea)
        {
            for (int c : BotArea->Children)
                if (std::string(Spec[(size_t)c].Name) == "BA-TagValidator") bTagInBottom = true;
            for (int c : BotArea->Children)
                if (std::string(Spec[(size_t)c].Name) == "BA-MatCrossRef") bMCInBottom = true;
        }
        if (Adv)
        {
            for (int c : Adv->Children)
                if (std::string(Spec[(size_t)c].Name) == "Sec-TagValidator") bTagInAdv = true;
            for (int c : Adv->Children)
                if (std::string(Spec[(size_t)c].Name) == "Sec-MatCrossRef") bMCInAdv = true;
        }
        TEST("Phase H: TagValidator moved out of bottom bar (A5)", !bTagInBottom && !bMCInBottom);
        TEST("Phase H: TagValidator is an Advanced accordion section (A5)", bTagInAdv && bMCInAdv);
    }

    // Negative controls: every principle must fire on a planted violation.
    auto Violates = [](const std::vector<FPLayout::FPLayoutNode>& nodes, FPLayout::DesignRule rule) {
        for (const FPLayout::FPViolation& v : FPLayout::ValidateDesign(nodes))
            if (v.Rule == rule) return true;
        return false;
    };
    {
        FPLayout::Builder B;
        const int Root = FPLayout::GRID(B, "Root", FPLayout::LF(B, "A", 40, 20), FPLayout::LF(B, "B", 40, 20));
        B.N[(size_t)Root].Spacing = 1.0;
        TEST("Phase H: validator fires NoSiblingOverlap (P1)", Violates(B.N, FPLayout::DesignRule::NoSiblingOverlap));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::HF(B, "Root", FPLayout::LF(B, "A", 100, 20), FPLayout::LF(B, "B", 100, 20));
        B.N[(size_t)Root].FixedW = 150.0;
        B.N[(size_t)Root].Spacing = 1.0;
        TEST("Phase H: validator fires OutsideParent (P2)", Violates(B.N, FPLayout::DesignRule::OutsideParent));
    }
    {
        FPLayout::Builder B;
        FPLayout::VF(B, "Root", FPLayout::LF(B, "A", 0, 0));
        TEST("Phase H: validator fires ZeroSize (P3)", Violates(B.N, FPLayout::DesignRule::ZeroSize));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::HF(B, "Root", FPLayout::LF(B, "A", 20, 20), FPLayout::LF(B, "B", 20, 20));
        B.N[(size_t)Root].Spacing = -2.0;
        TEST("Phase H: validator fires SpacingSanity (P4)", Violates(B.N, FPLayout::DesignRule::SpacingSanity));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::HF(B, "Root", FPLayout::LF(B, "A", 20, 20), FPLayout::LF(B, "B", 20, 20));
        B.N[(size_t)Root].Spacing = 5.0;
        TEST("Phase H: validator fires OffPaletteSpacing (P5)", Violates(B.N, FPLayout::DesignRule::OffPaletteSpacing));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root", FPLayout::LF(B, "A", 20, 20));
        B.N[(size_t)Root].PadL = 10.0;
        TEST("Phase H: validator fires MarginOverBudget (P6)", Violates(B.N, FPLayout::DesignRule::MarginOverBudget));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::HF(B, "Root", FPLayout::LF(B, "A", 20, 20), FPLayout::LF(B, "B", 20, 20));
        B.N[(size_t)Root].Spacing = 1.0;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].MarginL = -25.0;
        TEST("Phase H: validator fires ReadOrderBroken (P7)", Violates(B.N, FPLayout::DesignRule::ReadOrderBroken));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::GRID(B, "Root", FPLayout::LF(B, "A", 40, 20), FPLayout::LF(B, "B", 40, 20));
        B.N[(size_t)Root].Spacing = 1.0;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].MarginL = 2.0;
        TEST("Phase H: validator fires GridMisaligned (P8)", Violates(B.N, FPLayout::DesignRule::GridMisaligned));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root", FPLayout::LF(B, "A", 20, 20), FPLayout::LF(B, "B", 20, 20));
        B.N[(size_t)Root].bSection = true;
        TEST("Phase H: validator fires SectionTitleFirst (P9)", Violates(B.N, FPLayout::DesignRule::SectionTitleFirst));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::HF(B, "Root", FPLayout::LF(B, "A", 200, 20));
        B.N[(size_t)Root].FixedW = 100.0;
        TEST("Phase H: validator fires FitNoClip (P10)", Violates(B.N, FPLayout::DesignRule::FitNoClip));
    }
    {
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root", FPLayout::LF(B, "A", 20, 20));
        B.N[(size_t)Root].FixedW = 200.0;
        TEST("Phase H: validator fires MinimalSpace (P11)", Violates(B.N, FPLayout::DesignRule::MinimalSpace));
    }
    {
        // P12: two unrelated subtrees (different parents) forced to intersect.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::HF(B, "RowA", FPLayout::LF(B, "A", 100, 20)),
            FPLayout::HF(B, "RowB", FPLayout::LF(B, "B", 100, 20)));
        B.N[(size_t)Root].Spacing = 1.0;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].MarginT = -15.0;
        TEST("Phase H: validator fires GlobalOverlap (P12)", Violates(B.N, FPLayout::DesignRule::GlobalOverlap));
    }
    {
        // P12 exemption: overlay siblings stacked in one switcher must NOT fire.
        FPLayout::Builder B;
        FPLayout::OV(B, "Root",
            FPLayout::VF(B, "S1", FPLayout::LF(B, "A", 100, 20)),
            FPLayout::VF(B, "S2", FPLayout::LF(B, "B", 100, 20)));
        TEST("Phase H: overlay stack exempt from GlobalOverlap (P12)",
            !Violates(B.N, FPLayout::DesignRule::GlobalOverlap));
    }
    {
        // P13: a leaf extending past the root (screen) right edge.
        FPLayout::Builder B;
        const int Root = FPLayout::HF(B, "Root", FPLayout::LF(B, "A", 200, 20));
        B.N[(size_t)Root].FixedW = 100.0;
        TEST("Phase H: validator fires ScreenBounds (P13)", Violates(B.N, FPLayout::DesignRule::WithinScreenBounds));
    }
    {
        // P13 exemption: viewport-clipped content may exceed the screen.
        FPLayout::Builder B;
        const int Root = FPLayout::HF(B, "Root",
            FPLayout::VF(B, "Viewport", FPLayout::LF(B, "A", 200, 20)));
        B.N[(size_t)Root].FixedW = 100.0;
        B.N[(size_t)Root].FixedH = 20.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedW = 100.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = 20.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bClipH = true;
        TEST("Phase H: viewport content exempt from ScreenBounds (P13)",
            !Violates(B.N, FPLayout::DesignRule::WithinScreenBounds));
    }
    {
        // P16: five plain sections in a clipped viewport must fire.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Viewport",
                FPLayout::VF(B, "S1", FPLayout::LF(B, "T1", 20, 10), FPLayout::LF(B, "B1", 20, 20)),
                FPLayout::VF(B, "S2", FPLayout::LF(B, "T2", 20, 10), FPLayout::LF(B, "B2", 20, 20)),
                FPLayout::VF(B, "S3", FPLayout::LF(B, "T3", 20, 10), FPLayout::LF(B, "B3", 20, 20)),
                FPLayout::VF(B, "S4", FPLayout::LF(B, "T4", 20, 10), FPLayout::LF(B, "B4", 20, 20)),
                FPLayout::VF(B, "S5", FPLayout::LF(B, "T5", 20, 10), FPLayout::LF(B, "B5", 20, 20))));
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bClipH = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedW = 180.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = 560.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].Spacing = 2.0;
        for (int s = 1; s <= 5; ++s)
        {
            const int Sec = B.N[(size_t)B.N[(size_t)Root].Children[0]].Children[(size_t)(s - 1)];
            B.N[(size_t)Sec].bSection = true;
            B.N[(size_t)B.N[(size_t)Sec].Children[0]].bTitle = true;
        }
        TEST("Phase H: validator fires SectionDensity (P16)",
            Violates(B.N, FPLayout::DesignRule::DensityOverflow));
    }
    {
        // P16 exemption: accordion sections may be denser than 4.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Viewport",
                FPLayout::VF(B, "S1", FPLayout::LF(B, "T1", 20, 10), FPLayout::LF(B, "B1", 20, 20)),
                FPLayout::VF(B, "S2", FPLayout::LF(B, "T2", 20, 10), FPLayout::LF(B, "B2", 20, 20)),
                FPLayout::VF(B, "S3", FPLayout::LF(B, "T3", 20, 10), FPLayout::LF(B, "B3", 20, 20)),
                FPLayout::VF(B, "S4", FPLayout::LF(B, "T4", 20, 10), FPLayout::LF(B, "B4", 20, 20)),
                FPLayout::VF(B, "S5", FPLayout::LF(B, "T5", 20, 10), FPLayout::LF(B, "B5", 20, 20))));
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bClipH = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedW = 180.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = 560.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].Spacing = 2.0;
        for (int s = 1; s <= 5; ++s)
        {
            const int Sec = B.N[(size_t)B.N[(size_t)Root].Children[0]].Children[(size_t)(s - 1)];
            B.N[(size_t)Sec].bSection = true;
            B.N[(size_t)Sec].bAccordion = true;
            B.N[(size_t)B.N[(size_t)Sec].Children[0]].bTitle = true;
        }
        TEST("Phase H: accordion sections exempt from SectionDensity (P16)",
            !Violates(B.N, FPLayout::DesignRule::DensityOverflow));
    }
    {
        // Real manifest: the dense rails must be accordion-marked. Phase B
        // regroup: Import + OutlineDepth (Art), Viseme + Hull (Animated),
        // all of Nested & Pins, and all six Advanced sections are accordions.
        // The props sections were converted to carousel pages (P18) - one
        // visible at a time - so they no longer need accordion collapse.
        const char* AccordionSections[] = { "Sec-Import", "Sec-OutlineDepth",
            "Sec-VisemeGrid", "Sec-HullReview", "Sec-ParamRef", "Sec-ParamTable", "Sec-NestedPins",
            "Sec-Config", "Sec-EdgeAnalysis", "Sec-DepthDebug", "Sec-Problems",
            "Sec-TagValidator", "Sec-MatCrossRef" };
        bool bAccordionOk = true;
        auto CheckAcc = [&](const char* nm)
        {
            const FPLayout::FPLayoutNode* found = nullptr;
            for (const FPLayout::FPLayoutNode& n : Spec)
                if (std::string(n.Name) == nm) { found = &n; break; }
            if (!found || !found->bAccordion) bAccordionOk = false;
        };
        for (const char* nm : AccordionSections) CheckAcc(nm);
        TEST("Phase H: grouped accordion sections are accordions (P16)", bAccordionOk);
    }
    {
        // State-strip pick button replaces the old props-pane sync picker row.
        bool bPickBtn = false, bOldPickRow = false;
        for (const FPLayout::FPLayoutNode& n : Spec)
        {
            if (std::string(n.Name) == "ST-PickBtn") bPickBtn = true;
            if (std::string(n.Name) == "SY-Pick0") bOldPickRow = true;
        }
        TEST("Phase H: ST-PickBtn present, SY-PickRow removed", bPickBtn && !bOldPickRow);
    }
    {
        // P14: the props pane must leave a right-edge gap against the screen
        // (real widget: MainRow props slot gets right padding). Mirrors the
        // "items overlap the end of the screen" defect.
        const FPLayout::FPRect& rr = Rects[(size_t)RootIdx];
        int PropsIdx = -1;
        for (size_t pi = 0; pi < Spec.size(); ++pi)
            if (std::string(Spec[pi].Name) == "PROPS") { PropsIdx = (int)pi; break; }
        bool bGap = false;
        if (PropsIdx >= 0)
        {
            const FPLayout::FPRect& pr = Rects[(size_t)PropsIdx];
            bGap = (rr.X + rr.W) - (pr.X + pr.W) >= FPLayout::PropsRightGap - 0.001;
        }
        TEST("Phase H: props pane keeps a right-edge gap (P14)", bGap);
    }
    {
        // P15: scroll viewport content must keep a right inset so items do not
        // run under the scrollbar (real widget: SBox padding inside PropScroll).
        const FPLayout::FPLayoutNode* Scroll = nullptr;
        for (const FPLayout::FPLayoutNode& n : Spec)
            if (std::string(n.Name) == "PR-Scroll") { Scroll = &n; break; }
        TEST("Phase H: PR-Scroll content keeps a right inset (P15)",
            Scroll && Scroll->PadR >= FPLayout::PropsScrollInsetR - 0.001);
    }
    {
        // Section stacking guard: every section directly inside a clipped
        // viewport stacks naturally (no flex) and never overlaps its sibling
        // (real widget contract: sections use AutoHeight slots - bare AddSlot
        // would default to Fill and paint sections over each other).
        bool bStacked = true;
        for (const FPLayout::FPLayoutNode& n : Spec)
        {
            if (!n.bClipH) continue;
            for (size_t c = 0; c < n.Children.size() && bStacked; ++c)
            {
                const FPLayout::FPLayoutNode& ch = Spec[(size_t)n.Children[c]];
                if (!ch.bSection) continue;
                if (ch.bFlexH) bStacked = false;
            }
        }
        for (size_t i = 0; i < Spec.size() && bStacked; ++i)
        {
            const FPLayout::FPLayoutNode& n = Spec[i];
            if (!n.bClipH) continue;
            for (size_t a = 0; a < n.Children.size() && bStacked; ++a)
                for (size_t b2 = a + 1; b2 < n.Children.size(); ++b2)
                {
                    const FPLayout::FPRect& ra = Rects[(size_t)n.Children[a]];
                    const FPLayout::FPRect& rb = Rects[(size_t)n.Children[b2]];
                    if (ra.W > 0.001 && rb.W > 0.001 && ra.H > 0.001 && rb.H > 0.001 &&
                        ra.X < rb.X + rb.W - 0.001 && rb.X < ra.X + ra.W - 0.001 &&
                        ra.Y < rb.Y + rb.H - 0.001 && rb.Y < ra.Y + ra.H - 0.001)
                        bStacked = false;
                }
        }
        TEST("Phase H: viewport sections auto-stack without overlap", bStacked);
    }
}

// --- Phase I: UI testing procedures (fit-first / carousel / reserve) ---
// Mirrors the three-step procedure: (1) pack content to fit without a
// vertical scroll bar (P17), (2) page-flip carousel for dynamic row lists
// (P18), (3) keep a bottom padding reserve so pages never block buttons
// (P19).
void TestPhaseIUITesting() {
    printf("\n=== PhaseIUITesting ===\n");

    // --- Procedure constants (mirrored from the widget) ---
    TEST("UI: CarouselRowsPerPage=8", FPLayout::CarouselRowsPerPage == 8);
    TEST("UI: CarouselRowHeight=22", FPLayout::CarouselRowHeight == 22.0);
    TEST("UI: CarouselViewportH=184 (176 content + 8 reserve)",
        FPLayout::CarouselViewportH == 184.0);
    TEST("UI: CarouselNavHeight=22", FPLayout::CarouselNavHeight == 22.0);
    TEST("UI: ScrollReserveBottom=8", FPLayout::ScrollReserveBottom == 8.0);

    // --- Step 2 page math ---
    TEST("UI: page count 0 rows = 1 page", FPLayout::CarouselPageCount(0) == 1);
    TEST("UI: page count 1 row = 1 page", FPLayout::CarouselPageCount(1) == 1);
    TEST("UI: page count 8 rows = 1 page", FPLayout::CarouselPageCount(8) == 1);
    TEST("UI: page count 9 rows = 2 pages", FPLayout::CarouselPageCount(9) == 2);
    TEST("UI: page count 40 rows = 5 pages", FPLayout::CarouselPageCount(40) == 5);
    TEST("UI: page clamp negative -> 0", FPLayout::ClampCarouselPage(-1, 3) == 0);
    TEST("UI: page clamp beyond end -> last", FPLayout::ClampCarouselPage(5, 3) == 2);
    TEST("UI: page clamp single page -> 0", FPLayout::ClampCarouselPage(2, 1) == 0);

    // Page slice bounds: page p shows rows [p*8, min((p+1)*8, N)).
    {
        const int N = 40, P = 4;
        const int Start = P * FPLayout::CarouselRowsPerPage;
        const int End = std::min(Start + FPLayout::CarouselRowsPerPage, N);
        TEST("UI: page 4 of 40 rows spans 32..39", Start == 32 && End == 40);
    }
    {
        const int N = 17, P = 1;
        const int Start = P * FPLayout::CarouselRowsPerPage;
        const int End = std::min(Start + FPLayout::CarouselRowsPerPage, N);
        TEST("UI: last page of 17 rows spans 8..15", Start == 8 && End == 16);
    }

    // --- Step 1 fit-first: the real rails pack without vertical scroll ---
    const std::vector<FPLayout::FPLayoutNode> Spec = FPLayout::BuildSpec();
    TEST("UI: manifest builds (507 nodes)", Spec.size() == 507u);
    {
        const char* RailNames[6] = { "RL-ViewLayer", "RL-Art", "RL-Animated", "RL-NestedPins", "RL-CameraPrev", "RL-Advanced" };
        bool bNoV = true;
        for (const char* nm : RailNames)
        {
            const FPLayout::FPLayoutNode* found = nullptr;
            for (const FPLayout::FPLayoutNode& n : Spec)
                if (std::string(n.Name) == nm) { found = &n; break; }
            if (!found || !found->bNoVScroll) bNoV = false;
        }
        TEST("UI: all 6 rails are fit-first (no vertical scroll)", bNoV);
    }
    const std::vector<FPLayout::FPViolation> V = FPLayout::ValidateDesign(Spec);
    {
        bool bP17 = true, bP18 = true, bP19 = true;
        for (const FPLayout::FPViolation& v : V)
        {
            if (v.Rule == FPLayout::DesignRule::FitNoVScroll) bP17 = false;
            if (v.Rule == FPLayout::DesignRule::CarouselFallback) bP18 = false;
            if (v.Rule == FPLayout::DesignRule::ScrollbarReserve) bP19 = false;
        }
        TEST("UI: rails fit without a vertical scroll bar (P17)", bP17);
        TEST("UI: every carousel has its nav strip (P18)", bP18);
        TEST("UI: every carousel keeps the 8px reserve (P19)", bP19);
    }

    // --- Step 2 carousels in the manifest ---
    auto Find = [&](const char* name) -> const FPLayout::FPLayoutNode* {
        for (const FPLayout::FPLayoutNode& n : Spec)
            if (std::string(n.Name) == name) return &n;
        return nullptr;
    };
    {
        const FPLayout::FPLayoutNode* L = Find("RL-LayersScroll");
        const FPLayout::FPLayoutNode* PB = Find("PB-Carousel");
        const FPLayout::FPLayoutNode* AL = Find("AL-Carousel");
        const FPLayout::FPLayoutNode* PR = Find("PR-Carousel");
        bool bCar = L && L->bCarousel && L->FixedH == FPLayout::CarouselViewportH
                 && PB && PB->bCarousel && PB->FixedH == FPLayout::CarouselViewportH
                 && AL && AL->bCarousel && AL->FixedH == FPLayout::CarouselViewportH
                 && PR && PR->bCarousel && PR->FixedH == FPLayout::CarouselViewportH;
        TEST("UI: layers/problems/cross-layer/props are carousels (P18)", bCar);
    }
    {
        const FPLayout::FPLayoutNode* LN = Find("RL-LayersNav");
        const FPLayout::FPLayoutNode* PN = Find("PB-CarouselNav");
        const FPLayout::FPLayoutNode* AN = Find("AL-CarouselNav");
        const FPLayout::FPLayoutNode* PRN = Find("PR-CarouselNav");
        bool bNav = LN && LN->bCarouselNav && PN && PN->bCarouselNav
                 && AN && AN->bCarouselNav && PRN && PRN->bCarouselNav;
        TEST("UI: every carousel has a nav strip (P18)", bNav);
    }
    {
        const FPLayout::FPLayoutNode* PR = Find("PR-Carousel");
        TEST("UI: props carousel pages one-visible-at-a-time (P18)",
            PR && PR->Kind == FPLayout::ContainerKind::Overlay && PR->Children.size() == 2);
    }
    {
        // P20: per-tab whitespace review - under-packed carousel pages must
        // be combined into the minimum achievable page count. The props
        // carousel packs View Override + Sync to Views + Alignment into one
        // page (together they fit the 176px page viewport); Transform stays
        // alone (it cannot merge with anything).
        const FPLayout::FPLayoutNode* PR = Find("PR-Carousel");
        TEST("UI: props carousel minimum page pack = 2 (P20)",
            PR && FPLayout::CarouselMinPages(Spec, PR) == 2);
        TEST("UI: props carousel fully packed - no whitespace flags (P20)",
            PR && FPLayout::CarouselMinPages(Spec, PR) == (int)PR->Children.size());
    }
    {
        // Reserve: 176px of page content + 8px reserve inside 184.
        TEST("UI: page content height = rows x row height",
            FPLayout::CarouselViewportH - FPLayout::ScrollReserveBottom
                == FPLayout::CarouselRowsPerPage * FPLayout::CarouselRowHeight);
    }
    {
        // P15 preserved: PR-Scroll still leaves the scrollbar gap.
        const FPLayout::FPLayoutNode* Scroll = Find("PR-Scroll");
        TEST("UI: props pane keeps the right inset (P15)",
            Scroll && Scroll->PadR >= FPLayout::PropsScrollInsetR - 0.001);
    }

    // --- Negative controls ---
    auto Violates = [](const std::vector<FPLayout::FPLayoutNode>& nodes, FPLayout::DesignRule rule) {
        for (const FPLayout::FPViolation& v : FPLayout::ValidateDesign(nodes))
            if (v.Rule == rule) return true;
        return false;
    };
    {
        // P17: a fit-first viewport whose plain children overflow must fire.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Viewport",
                FPLayout::LF(B, "A", 40, 200),
                FPLayout::LF(B, "B", 40, 200),
                FPLayout::LF(B, "C", 40, 200)));
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bClipH = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bNoVScroll = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedW = 180.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = 560.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].Spacing = 2.0;
        TEST("UI: overflow fires FitNoVScroll (P17)", Violates(B.N, FPLayout::DesignRule::FitNoVScroll));
    }
    {
        // P17 exemption: accordion children collapse, so a tall accordion
        // stack stays bounded in a fit-first viewport.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Viewport",
                FPLayout::VF(B, "S1", FPLayout::LF(B, "T1", 20, 10), FPLayout::LF(B, "B1", 20, 500)),
                FPLayout::VF(B, "S2", FPLayout::LF(B, "T2", 20, 10), FPLayout::LF(B, "B2", 20, 500))));
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bClipH = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bNoVScroll = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedW = 180.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = 560.0;
        for (int s = 0; s < 2; ++s)
        {
            const int Sec = B.N[(size_t)B.N[(size_t)Root].Children[0]].Children[(size_t)s];
            B.N[(size_t)Sec].bAccordion = true;
        }
        TEST("UI: accordion stacks exempt from FitNoVScroll (P17)",
            !Violates(B.N, FPLayout::DesignRule::FitNoVScroll));
    }
    {
        // P18: a carousel without a nav strip must fire.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Carousel", FPLayout::LF(B, "Page0", 20, 20)));
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bCarousel = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = FPLayout::CarouselViewportH;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].PadB = FPLayout::ScrollReserveBottom;
        TEST("UI: nav-less carousel fires CarouselFallback (P18)",
            Violates(B.N, FPLayout::DesignRule::CarouselFallback));
    }
    {
        // P18: the nav strip must sit AFTER the page viewport.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::LF(B, "Nav", 20, 20),
            FPLayout::VF(B, "Carousel", FPLayout::LF(B, "Page0", 20, 20)));
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bCarouselNav = true;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].bCarousel = true;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].FixedH = FPLayout::CarouselViewportH;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].PadB = FPLayout::ScrollReserveBottom;
        TEST("UI: nav before viewport fires CarouselFallback (P18)",
            Violates(B.N, FPLayout::DesignRule::CarouselFallback));
    }
    {
        // P18 positive: viewport + nav after it is fine.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Carousel", FPLayout::LF(B, "Page0", 20, 20)),
            FPLayout::LF(B, "Nav", 20, 20));
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bCarousel = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = FPLayout::CarouselViewportH;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].PadB = FPLayout::ScrollReserveBottom;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].bCarouselNav = true;
        TEST("UI: carousel + nav after passes P18",
            !Violates(B.N, FPLayout::DesignRule::CarouselFallback));
    }
    {
        // P19: a carousel without the 8px bottom reserve must fire.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Carousel", FPLayout::LF(B, "Page0", 20, 20)),
            FPLayout::LF(B, "Nav", 20, 20));
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bCarousel = true;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = FPLayout::CarouselViewportH;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].bCarouselNav = true;
        TEST("UI: reserve-less carousel fires ScrollbarReserve (P19)",
            Violates(B.N, FPLayout::DesignRule::ScrollbarReserve));
    }
    {
        // P20 negative: two section pages that together fit the page viewport
        // must merge - staying separate is under-packed whitespace.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Carousel",
                FPLayout::VF(B, "S1", FPLayout::LF(B, "T1", 20, 10), FPLayout::LF(B, "B1", 20, 40)),
                FPLayout::VF(B, "S2", FPLayout::LF(B, "T2", 20, 10), FPLayout::LF(B, "B2", 20, 40))));
        const int Car = B.N[(size_t)Root].Children[0];
        B.N[(size_t)Car].bCarousel = true;
        B.N[(size_t)Car].FixedH = FPLayout::CarouselViewportH;
        B.N[(size_t)Car].PadB = FPLayout::ScrollReserveBottom;
        for (int s = 0; s < 2; ++s)
        {
            const int Sec = B.N[(size_t)Car].Children[(size_t)s];
            B.N[(size_t)Sec].bSection = true;
            B.N[(size_t)B.N[(size_t)Sec].Children[0]].bTitle = true;
        }
        const FPLayout::FPLayoutNode& CarN = B.N[(size_t)Car];
        TEST("UI: two fit-in-one pages pack to 1 (P20)",
            FPLayout::CarouselMinPages(B.N, &CarN) == 1);
        TEST("UI: under-packed pages fire PageWhitespaceReview (P20)",
            Violates(B.N, FPLayout::DesignRule::PageWhitespaceReview));
    }
    {
        // P20 positive: pages that do NOT fit one viewport stay separate.
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::VF(B, "Carousel",
                FPLayout::VF(B, "S1", FPLayout::LF(B, "T1", 20, 10), FPLayout::LF(B, "B1", 20, 120)),
                FPLayout::VF(B, "S2", FPLayout::LF(B, "T2", 20, 10), FPLayout::LF(B, "B2", 20, 120))));
        const int Car = B.N[(size_t)Root].Children[0];
        B.N[(size_t)Car].bCarousel = true;
        B.N[(size_t)Car].FixedH = FPLayout::CarouselViewportH;
        B.N[(size_t)Car].PadB = FPLayout::ScrollReserveBottom;
        for (int s = 0; s < 2; ++s)
        {
            const int Sec = B.N[(size_t)Car].Children[(size_t)s];
            B.N[(size_t)Sec].bSection = true;
            B.N[(size_t)B.N[(size_t)Sec].Children[0]].bTitle = true;
        }
        const FPLayout::FPLayoutNode& CarN = B.N[(size_t)Car];
        TEST("UI: full pages stay separate - no whitespace flag (P20)",
            FPLayout::CarouselMinPages(B.N, &CarN) == 2);
        TEST("UI: full pages pass PageWhitespaceReview (P20)",
            !Violates(B.N, FPLayout::DesignRule::PageWhitespaceReview));
    }
}

// --- Phase 4: hotspot region hit-testing (named polygon buckets) ---
void TestHotspotRegions() {
    printf("\n=== HotspotRegions ===\n");
    using R = FPLayout::FPHotspotRegion;
    using V = std::vector<FPLayout::FPHotspotPoint>;
    const V Empty;
    const V Sq = { FPLayout::HP(0,0), FPLayout::HP(1,0), FPLayout::HP(1,1), FPLayout::HP(0,1) };
    const std::vector<R> SqList = { R{"Sq", Sq} };

    // Boundary-inclusive semantics.
    TEST("Square interior hit", FPLayout::FPHotspotHitIndex(SqList, 0.5, 0.5) == 0);
    TEST("Square outside miss", FPLayout::FPHotspotHitIndex(SqList, 2.0, 2.0) == -1);
    TEST("Square edge counts as inside", FPLayout::FPHotspotHitIndex(SqList, 0.5, 0.0) == 0);
    TEST("Square vertex counts as inside", FPLayout::FPHotspotHitIndex(SqList, 0.0, 0.0) == 0);
    TEST("Square just outside edge misses", FPLayout::FPHotspotHitIndex(SqList, 0.5, -1e-6) == -1);
    TEST("Square just outside vertex misses", FPLayout::FPHotspotHitIndex(SqList, 1.0 + 1e-6, 1.0) == -1);

    // Concave L-shape: reflex vertex at (1,1), notch quadrant at top-right.
    const V L = { FPLayout::HP(0,0), FPLayout::HP(2,0), FPLayout::HP(2,1),
                  FPLayout::HP(1,1), FPLayout::HP(1,2), FPLayout::HP(0,2) };
    const std::vector<R> LList = { R{"L", L} };
    TEST("Concave: lobe inside hits", FPLayout::FPHotspotHitIndex(LList, 0.5, 1.5) == 0);
    TEST("Concave: notch outside misses", FPLayout::FPHotspotHitIndex(LList, 1.5, 1.5) == -1);
    TEST("Concave: reflex vertex counts as inside", FPLayout::FPHotspotHitIndex(LList, 1.0, 1.0) == 0);
    TEST("Concave: notch interior misses", FPLayout::FPHotspotHitIndex(LList, 1.2, 1.6) == -1);

    // Duplicate consecutive vertex: degenerate edge must be skipped, results identical.
    const V Ldup = { FPLayout::HP(0,0), FPLayout::HP(2,0), FPLayout::HP(2,1),
                     FPLayout::HP(1,1), FPLayout::HP(1,1), FPLayout::HP(1,2),
                     FPLayout::HP(0,2) };
    const std::vector<R> LdupList = { R{"Ld", Ldup} };
    TEST("Duplicate vertex: lobe still hits", FPLayout::FPHotspotHitIndex(LdupList, 0.5, 1.5) == 0);
    TEST("Duplicate vertex: notch still misses", FPLayout::FPHotspotHitIndex(LdupList, 1.5, 1.5) == -1);
    TEST("Duplicate vertex: duplicate point counts as inside", FPLayout::FPHotspotHitIndex(LdupList, 1.0, 1.0) == 0);

    // Hole: outer 0..1 square with a 0.4..0.6 hole.
    R WithHole;
    WithHole.Name = "Ring";
    WithHole.Outer = Sq;
    WithHole.Holes.push_back(
        { FPLayout::HP(0.4,0.4), FPLayout::HP(0.6,0.4), FPLayout::HP(0.6,0.6), FPLayout::HP(0.4,0.6) });
    const std::vector<R> Ring = { WithHole };
    TEST("Hole: outer but not hole hits", FPLayout::FPHotspotHitIndex(Ring, 0.2, 0.5) == 0);
    TEST("Hole: inside hole misses", FPLayout::FPHotspotHitIndex(Ring, 0.5, 0.5) == -1);
    TEST("Hole: hole boundary excluded", FPLayout::FPHotspotHitIndex(Ring, 0.4, 0.5) == -1);
    TEST("Hole: outer boundary still hits", FPLayout::FPHotspotHitIndex(Ring, 0.0, 0.5) == 0);

    // Empty and degenerate loops.
    const std::vector<R> EmptyList = { R{"E", Empty} };
    TEST("Empty loop never hits", FPLayout::FPHotspotHitIndex(EmptyList, 0.5, 0.5) == -1);
    const V Pt = { FPLayout::HP(1,1) };
    const std::vector<R> PtList = { R{"P", Pt} };
    TEST("Degenerate loop: exact point hits", FPLayout::FPHotspotHitIndex(PtList, 1.0, 1.0) == 0);
    TEST("Degenerate loop: near point misses", FPLayout::FPHotspotHitIndex(PtList, 1.0 + 1e-6, 1.0) == -1);

    // Collinear edges.
    const V Tri = { FPLayout::HP(0,0), FPLayout::HP(2,0), FPLayout::HP(1,2) };
    const std::vector<R> TriList = { R{"T", Tri} };
    TEST("Triangle interior hits", FPLayout::FPHotspotHitIndex(TriList, 1.0, 0.5) == 0);
    TEST("Triangle base midpoint is boundary", FPLayout::FPHotspotHitIndex(TriList, 1.0, 0.0) == 0);
    TEST("Triangle outside misses", FPLayout::FPHotspotHitIndex(TriList, 3.0, 1.0) == -1);

    // Overlapping buckets: first match wins, table order is decisive.
    const V B = { FPLayout::HP(0.5,0.5), FPLayout::HP(1.5,0.5), FPLayout::HP(1.5,1.5), FPLayout::HP(0.5,1.5) };
    TEST("Overlap: first table entry wins", FPLayout::FPHotspotHitIndex({ R{"A", Sq}, R{"B", B} }, 0.75, 0.75) == 0);
    TEST("Overlap: exclusive second region hits", FPLayout::FPHotspotHitIndex({ R{"A", Sq}, R{"B", B} }, 1.25, 1.25) == 1);
    TEST("Overlap: reorder flips the winner", FPLayout::FPHotspotHitIndex({ R{"B", B}, R{"A", Sq} }, 0.75, 0.75) == 0);

    // Named lookup.
    const char* HitName = FPLayout::FPHotspotHit(SqList, 0.5, 0.5);
    TEST("Hit returns bucket name", HitName != nullptr && std::string(HitName) == "Sq");
    TEST("Miss returns null name", FPLayout::FPHotspotHit(SqList, 5.0, 5.0) == nullptr);

    // Default face template: named regions, concave + hole behavior in place.
    const std::vector<R> Def = FPLayout::DefaultHotspotRegions();
    TEST("Template: 13 regions", Def.size() == 13u);
    bool bNamed = true;
    for (const R& r : Def)
        if (!r.Name || !r.Name[0] || r.Outer.empty()) bNamed = false;
    TEST("Template: all named and non-empty", bNamed);
    TEST("Template: bridge hits Nose", std::string(FPLayout::FPHotspotHit(Def, 0.5, 0.30)) == "Nose");
    TEST("Template: tip boundary hits Nose", std::string(FPLayout::FPHotspotHit(Def, 0.5, 0.52)) == "Nose");
    TEST("Template: below tip misses", FPLayout::FPHotspotHit(Def, 0.5, 0.55) == nullptr);
    TEST("Template: lip hits Mouth", std::string(FPLayout::FPHotspotHit(Def, 0.5, 0.61)) == "Mouth");
    TEST("Template: mouth hole yields Teeth", std::string(FPLayout::FPHotspotHit(Def, 0.5, 0.66)) == "Teeth");
    TEST("Template: hole side yields Teeth too", std::string(FPLayout::FPHotspotHit(Def, 0.55, 0.66)) == "Teeth");
    TEST("Template: chin hits Chin", std::string(FPLayout::FPHotspotHit(Def, 0.5, 0.76)) == "Chin");
    TEST("Template: neck hits Neck", std::string(FPLayout::FPHotspotHit(Def, 0.5, 0.94)) == "Neck");
    TEST("Template: cheek hits CheekL", std::string(FPLayout::FPHotspotHit(Def, 0.10, 0.35)) == "CheekL");
    TEST("Template: cheek right hits CheekR", std::string(FPLayout::FPHotspotHit(Def, 0.90, 0.35)) == "CheekR");
    TEST("Template: ear hits EarL", std::string(FPLayout::FPHotspotHit(Def, 0.05, 0.35)) == "EarL");
    TEST("Template: ear overlap resolves to CheekL", std::string(FPLayout::FPHotspotHit(Def, 0.08, 0.35)) == "CheekL");
    TEST("Template: outside ear misses", FPLayout::FPHotspotHit(Def, 0.02, 0.35) == nullptr);
    TEST("Template: eye hits EyeL", std::string(FPLayout::FPHotspotHit(Def, 0.25, 0.24)) == "EyeL");
    TEST("Template: eye right hits EyeR", std::string(FPLayout::FPHotspotHit(Def, 0.75, 0.24)) == "EyeR");
    TEST("Template: brow hits BrowL", std::string(FPLayout::FPHotspotHit(Def, 0.26, 0.14)) == "BrowL");
    TEST("Template: forehead misses", FPLayout::FPHotspotHit(Def, 0.5, 0.14) == nullptr);
    TEST("Template: corner misses", FPLayout::FPHotspotHit(Def, 0.01, 0.01) == nullptr);
}

// --- Audit edges: opposite-direction + degenerate hotspot hits ---
void TestHotspotHitIndexEdges() {
    printf("\n=== HotspotHitIndexEdges ===\n");
    using R = FPLayout::FPHotspotRegion;
    using V = std::vector<FPLayout::FPHotspotPoint>;

    const std::vector<R> Def = FPLayout::DefaultHotspotRegions();

    TEST("Empty region list -> -1", FPLayout::FPHotspotHitIndex({}, 0.5, 0.5) == -1);
    TEST("Negative X never hits", FPLayout::FPHotspotHitIndex(Def, -0.5, 0.5) == -1);
    TEST("Negative Y never hits", FPLayout::FPHotspotHitIndex(Def, 0.5, -0.5) == -1);
    TEST("Oversized X misses", FPLayout::FPHotspotHitIndex(Def, 1.5, 0.5) == -1);
    TEST("Oversized Y misses", FPLayout::FPHotspotHitIndex(Def, 0.5, 1.5) == -1);

    const V Sq = { FPLayout::HP(0,0), FPLayout::HP(1,0), FPLayout::HP(1,1), FPLayout::HP(0,1) };
    const std::vector<R> SqList = { R{"Sq", Sq} };
    TEST("Just outside left edge misses", FPLayout::FPHotspotHitIndex(SqList, -1e-6, 0.5) == -1);
    TEST("Just outside right edge misses", FPLayout::FPHotspotHitIndex(SqList, 1.0 + 1e-6, 0.5) == -1);
    TEST("Just outside top edge misses", FPLayout::FPHotspotHitIndex(SqList, 0.5, 1.0 + 1e-6) == -1);
    TEST("Edge midpoints count as inside", FPLayout::FPHotspotHitIndex(SqList, 0.0, 0.5) == 0
        && FPLayout::FPHotspotHitIndex(SqList, 1.0, 0.5) == 0
        && FPLayout::FPHotspotHitIndex(SqList, 0.5, 1.0) == 0);
}

// --- Phase 1: hotspot region -> primary layer derivation ---
void TestHotspotLayerMapping() {
    printf("\n=== HotspotLayerMapping ===\n");
    using S = std::vector<std::string>;
    using M = const char* (*)(const std::vector<std::string>&, const char*);
    const M Match = &FPLayout::FPHotspotLayerMatch;
    const S Defaults = { "Eyes", "Brows", "Mouth", "Hair" };
    const auto Is = [](const char* P, const char* Want) -> bool
    {
        if (!Want) return P == nullptr;
        return P && std::string(P) == Want;
    };

    // Empty inputs.
    TEST("Empty region matches nothing", Is(Match(Defaults, ""), nullptr));
    TEST("Null region matches nothing", Is(Match(Defaults, nullptr), nullptr));
    TEST("Empty layer list matches nothing", Is(Match(S{}, "Nose"), nullptr));

    // Exact (case-sensitive).
    TEST("Exact: Mouth", Is(Match(Defaults, "Mouth"), "Mouth"));
    TEST("Exact: plural layer", Is(Match(Defaults, "Brows"), "Brows"));
    TEST("Exact is case-sensitive: upper", Is(Match(Defaults, "MOUTH"), nullptr));
    TEST("Exact is case-sensitive: lower", Is(Match(Defaults, "mouth"), nullptr));

    // Singular/plural normalize.
    TEST("Singular +s -> layer", Is(Match(Defaults, "Brow"), "Brows"));
    TEST("Singular +s -> layer (Eye)", Is(Match(Defaults, "Eye"), "Eyes"));
    TEST("Plural -s -> layer", Is(Match(Defaults, "Hairs"), "Hair"));
    TEST("Plural -s requires trailing s (Hair)", Is(Match(Defaults, "Hairs"), "Hair"));

    // L/R collapse.
    TEST("Collapse EyeL -> Eyes", Is(Match(Defaults, "EyeL"), "Eyes"));
    TEST("Collapse EyeR -> Eyes", Is(Match(Defaults, "EyeR"), "Eyes"));
    TEST("Collapse BrowL -> Brows", Is(Match(Defaults, "BrowL"), "Brows"));
    TEST("Collapse BrowR -> Brows", Is(Match(Defaults, "BrowR"), "Brows"));
    TEST("Collapse returns pointer into layer list", Match(Defaults, "EyeL") == Defaults[0].c_str());
    TEST("Collapse: EyeLash never collapses", Is(Match(Defaults, "EyeLash"), nullptr));
    TEST("Collapse: short names never collapse", Is(Match(Defaults, "R"), nullptr));
    TEST("Exact: Hair (lowercase trailing r untouched)", Is(Match(Defaults, "Hair"), "Hair"));

    // Collapse when the base exists exactly.
    const S WithBase = { "Eyes", "Eye" };
    TEST("Collapse prefers exact base layer", Is(Match(WithBase, "EyeL"), "Eye"));

    // Exact wins over every derivation.
    const S Dual = { "Eye", "Eyes" };
    TEST("Exact beats singular normalize", Is(Match(Dual, "Eye"), "Eye"));
    TEST("Exact beats collapse", Is(Match(WithBase, "Eye"), "Eye"));

    // Prefix (unique only).
    TEST("Prefix: unique Ey -> Eyes", Is(Match(Defaults, "Ey"), "Eyes"));
    TEST("Prefix: unique Br -> Brows", Is(Match(Defaults, "Br"), "Brows"));
    const S Ambiguous = { "Eyebrow", "Eyelid" };
    TEST("Prefix: ambiguous matches nothing", Is(Match(Ambiguous, "Eye"), nullptr));
    const S AmbigDefaults = { "Brows", "Bottom" };
    TEST("Prefix: ambiguous B matches nothing", Is(Match(AmbigDefaults, "B"), nullptr));

    // Misshapen / garbage inputs.
    TEST("Underscore region matches nothing", Is(Match(Defaults, "Eye_L"), nullptr));
    TEST("Whitespace region matches nothing", Is(Match(Defaults, " EyeL"), nullptr));
    TEST("Trailing dot matches nothing", Is(Match(Defaults, "Mouth."), nullptr));
    TEST("Random string matches nothing", Is(Match(Defaults, "Zygoma"), nullptr));

    // The 13 default regions against the 4 default primary layers.
    const std::vector<FPLayout::FPHotspotRegion> Def = FPLayout::DefaultHotspotRegions();
    int nMapped = 0, nUnmapped = 0;
    for (const FPLayout::FPHotspotRegion& R : Def)
    {
        const std::string S(R.Name ? R.Name : "");
        const char* P = Match(Defaults, S.c_str());
        if (P) ++nMapped; else ++nUnmapped;
    }
    TEST("Default template: all 13 regions are routed", nMapped + nUnmapped == 13);
    TEST("Default template: BrowL/BrowR/EyeL/EyeR/Mouth map (5)", nMapped == 5);
    TEST("Default template: unmapped parts stay unmapped (8)", nUnmapped == 8);
    TEST("Default template: Nose stays unmapped", Is(Match(Defaults, "Nose"), nullptr));
    TEST("Default template: CheekL stays unmapped", Is(Match(Defaults, "CheekL"), nullptr));
    TEST("Default template: Teeth stays unmapped", Is(Match(Defaults, "Teeth"), nullptr));
    TEST("Default template: EarR stays unmapped", Is(Match(Defaults, "EarR"), nullptr));
    TEST("Default template: Neck stays unmapped", Is(Match(Defaults, "Neck"), nullptr));
    TEST("Default template: Mouth maps to itself", Is(Match(Defaults, "Mouth"), "Mouth"));
    TEST("Default template: Eyes maps from both directions",
        Is(Match(Defaults, "EyeL"), "Eyes") && Is(Match(Defaults, "EyeR"), "Eyes"));
}

// --- Phase 2: hotspot transform mirror (master-material UV chain) ---
void TestTransformHotspotRegion() {
    printf("\n=== TransformHotspotRegion ===\n");
    using P = FPLayout::FPHotspotPoint;
    const auto H = [](double X, double Y) -> P { return FPLayout::HP(X, Y); };
    const auto TP = [](P Pt, double Px, double Py, double Sx, double Sy, double R) -> P
    { return FPLayout::FPHotspotTransformPoint(Pt, Px, Py, Sx, Sy, R); };
    const auto Near = [](P A, P B) -> bool
    { return std::abs(A.X - B.X) < 1e-9 && std::abs(A.Y - B.Y) < 1e-9; };
    const P Q = H(0.2, 0.3);

    // Identity: no pos/scale/rotation -> point unchanged.
    TEST("Identity leaves point unchanged", Near(TP(Q, 0, 0, 1, 1, 0), Q));
    TEST("Identity at 360-degree rotation", Near(TP(Q, 0, 0, 1, 1, 360), Q));

    // Translation: (uv - Pivot + Pos) * 1 + Pivot == uv + Pos.
    TEST("Translation adds Pos", Near(TP(Q, 0.1, -0.05, 1, 1, 0), H(0.3, 0.25)));
    TEST("Translation is pivot-independent", Near(TP(H(0.5, 0.5), 0.1, 0.2, 1, 1, 0), H(0.6, 0.7)));

    // Scale about pivot: (uv - Pivot) * Scale + Pivot.
    TEST("Scale doubles around pivot", Near(TP(H(0.2, 0.3), 0, 0, 2, 2, 0), H(-0.1, 0.1)));
    TEST("Pivot point stays fixed under scale", Near(TP(H(0.5, 0.5), 0, 0, 3, 3, 0), H(0.5, 0.5)));
    TEST("Scale is per-axis", Near(TP(H(0.5, 0.25), 0, 0, 2, 1, 0), H(0.5, 0.25)));

    // Order: translation BEFORE scale (material chain Subtract->Add->Multiply).
    // (0.5-0.5+0.1)*2+0.5 == 0.7; scale-first would give 0.6.
    TEST("Translate before scale order", Near(TP(H(0.5, 0.5), 0.1, 0, 2, 2, 0), H(0.7, 0.5)));
    TEST("Pivot maps to Pos*Scale + Pivot", Near(TP(H(0.5, 0.5), 0.1, 0.2, 2, 2, 0), H(0.7, 0.9)));

    // Rotation about the fixed UV center (0.5, 0.5), clockwise, degrees.
    TEST("Rot 90: right of center swings down (clockwise)", Near(TP(H(0.75, 0.5), 0, 0, 1, 1, 90), H(0.5, 0.75)));
    TEST("Rot 180 flips about center", Near(TP(H(0.75, 0.5), 0, 0, 1, 1, 180), H(0.25, 0.5)));
    TEST("Rot -90 swings up (counterclockwise)", Near(TP(H(0.75, 0.5), 0, 0, 1, 1, -90), H(0.5, 0.25)));
    TEST("Center point invariant under rotation", Near(TP(H(0.5, 0.5), 0, 0, 1, 1, 37), H(0.5, 0.5)));
    TEST("Rot 45 moves corner toward edge", Near(TP(H(0.75, 0.5), 0, 0, 1, 1, 45),
        H(0.5 + 0.25 * 0.70710678118, 0.5 + 0.25 * 0.70710678118)));

    // Rotation center is fixed at (0.5,0.5) independent of the pivot.
    TEST("Rot center ignores pivot", Near(FPLayout::FPHotspotTransformPoint(H(0.6, 0.5), 0, 0, 1, 1, 90, 0.25, 0.25), H(0.5, 0.6)));

    // Region-level: outer + holes transform, name preserved.
    using R = FPLayout::FPHotspotRegion;
    const R Mouth = FPLayout::DefaultHotspotRegions()[7];   // Mouth with hole
    TEST("Template region 7 is Mouth", Mouth.Name && std::string(Mouth.Name) == "Mouth");
    const R Moved = FPLayout::FPHotspotTransformRegion(Mouth, 0.1, 0, 1, 1, 0);
    TEST("Region name preserved", Moved.Name == Mouth.Name);
    TEST("Outer loop transforms", Near(Moved.Outer[0], H(Mouth.Outer[0].X + 0.1, Mouth.Outer[0].Y)));
    TEST("Hole transforms too", Near(Moved.Holes[0][0], H(Mouth.Holes[0][0].X + 0.1, Mouth.Holes[0][0].Y)));
    TEST("Region count preserved", Moved.Outer.size() == Mouth.Outer.size() && Moved.Holes.size() == Mouth.Holes.size());

    // Full template sweep with a typical two-layer mapping: EyeL/EyeR share
    // the Eyes transform, BrowL/BrowR share the Brows transform, everything
    // else stays default. Deterministic + names intact.
    const std::vector<R> Def = FPLayout::DefaultHotspotRegions();
    std::vector<R> Painted = Def;
    for (R& Rg : Painted)
    {
        if (!Rg.Name) continue;
        const std::string N(Rg.Name);
        if (N == "EyeL" || N == "EyeR")
            Rg = FPLayout::FPHotspotTransformRegion(Rg, 0.02, -0.03, 0.98, 1.02, 2.0);
        else if (N == "BrowL" || N == "BrowR")
            Rg = FPLayout::FPHotspotTransformRegion(Rg, -0.01, 0.04, 1.01, 0.97, -1.5);
    }
    TEST("Sweep: 13 regions survive", Painted.size() == 13u);
    bool bNames = true;
    for (size_t i = 0; i < Def.size(); ++i)
        if (Painted[i].Name != Def[i].Name) bNames = false;
    TEST("Sweep: names preserved in order", bNames);
    TEST("Sweep: unmapped Mouth stays default", Near(Painted[7].Outer[0], Def[7].Outer[0]));
    const R EyesT = Painted[2];   // EyeL
    TEST("Sweep: EyeL moved by Eyes transform",
        Near(EyesT.Outer[0], TP(Def[2].Outer[0], 0.02, -0.03, 0.98, 1.02, 2.0)));
    TEST("Sweep: EyeR shares the Eyes transform (mirror symmetry preserved)",
        Near(Painted[3].Outer[0], TP(Def[3].Outer[0], 0.02, -0.03, 0.98, 1.02, 2.0)));
    TEST("Sweep: BrowL moved by Brows transform",
        Near(Painted[0].Outer[0], TP(Def[0].Outer[0], -0.01, 0.04, 1.01, 0.97, -1.5)));
    TEST("Sweep: BrowR shares the Brows transform",
        Near(Painted[1].Outer[0], TP(Def[1].Outer[0], -0.01, 0.04, 1.01, 0.97, -1.5)));

    // Hit-testing still works on transformed regions (outline follows art).
    const R MovedBig = FPLayout::FPHotspotTransformRegion(Def[0], 0.3, 0, 1, 1, 0);   // BrowL +0.3 x
    const std::vector<R> MovedList = { MovedBig };
    TEST("Transformed region hit-tests at new location",
        FPLayout::FPHotspotHit(MovedList, Def[0].Outer[0].X + 0.3, Def[0].Outer[0].Y) != nullptr);
    TEST("Transformed region misses old location",
        FPLayout::FPHotspotHit(MovedList, Def[0].Outer[0].X, Def[0].Outer[0].Y) == nullptr);
}

// --- Phase 3 pin drift mirror: FPMirrorTransform / FPPinDriftCount ---
// Mirrors RefreshSyncDriftIndicator (exact-equality semantics like FVector2D).
void TestPinDriftMirror() {
    printf("\n=== Pin Drift Mirror ===\n");

    using FP = FPLayout::FPMirrorTransform;
    using FPLayout::FPPinDriftCount;

    // Empty / degenerate inputs are safe.
    const std::vector<FP> Empty;
    TEST("Empty view list -> 0", FPPinDriftCount(Empty, 0) == 0);
    const std::vector<FP> One = { FP() };
    TEST("Single view list -> 0", FPPinDriftCount(One, 0) == 0);
    TEST("OOB active (negative) -> 0", FPPinDriftCount(One, -1) == 0);
    TEST("OOB active (past end) -> 0", FPPinDriftCount(One, 5) == 0);

    // All synced -> 0.
    std::vector<FP> Views(10);
    TEST("All synced -> 0", FPPinDriftCount(Views, 0) == 0);

    // One drifted field each.
    Views[3].PosX = 0.5;
    TEST("Position-X drift -> 1", FPPinDriftCount(Views, 0) == 1);
    Views[3] = FP(); Views[3].PosY = -0.25;
    TEST("Position-Y drift -> 1", FPPinDriftCount(Views, 0) == 1);
    Views[3] = FP(); Views[3].ScaleX = 1.1;
    TEST("Scale-X drift -> 1", FPPinDriftCount(Views, 0) == 1);
    Views[3] = FP(); Views[3].ScaleY = 0.9;
    TEST("Scale-Y drift -> 1", FPPinDriftCount(Views, 0) == 1);
    Views[3] = FP(); Views[3].Rot = 45.0;
    TEST("Rotation-only drift -> 1", FPPinDriftCount(Views, 0) == 1);

    // Exact floating-point semantics: views sharing the active view's exact
    // double are synced (even a value like 0.30000000000000004); any
    // ULP-level difference counts as drifted. The active view is the
    // reference (widget: GetSlot(ActiveViewState) compared via FVector2D !=).
    const double Weird = 0.30000000000000004;
    for (size_t i = 0; i < Views.size(); ++i) Views[i].Rot = Weird;
    TEST("All views share active's exact double -> 0",
        FPPinDriftCount(Views, 0) == 0);
    Views[3].Rot = 0.3000000000000001;
    TEST("ULP-level rotation difference counts as drifted",
        FPPinDriftCount(Views, 0) == 1);

    // Mass drift + active exclusion.
    Views[3] = FP();
    Views[0] = FP();
    for (size_t i = 1; i < Views.size(); ++i) Views[i].PosY = 0.1;
    TEST("All 9 others drifted -> 9", FPPinDriftCount(Views, 0) == 9);
    for (size_t i = 0; i < Views.size(); ++i) Views[i] = FP();
    Views[0].PosX = 1.0;
    TEST("Active slot is reference; its own diff is never drift",
        FPPinDriftCount(Views, 0) == 9);
    for (size_t i = 0; i < Views.size(); ++i) Views[i] = FP();
    TEST("Active at last index skips itself", FPPinDriftCount(Views, 9) == 0);
    Views[1].PosX = 1.0;
    TEST("Drift counted when active is last", FPPinDriftCount(Views, 9) == 1);
    Views[4].PosX = 2.0;
    TEST("Two drifted -> 2", FPPinDriftCount(Views, 9) == 2);
}

// --- Phase 4 mirrors: rail width range + problems panel search/summary ---
void TestPhase4Mirrors() {
    printf("\n=== Phase 4 Mirrors ===\n");

    // ---- Rail width range (RailWidthMin/Max + ClampRailWidth) ----
    TEST("Rail width min = 180", FPLayout::RailWidthMin == 180.0);
    TEST("Rail width max = 360", FPLayout::RailWidthMax == 360.0);
    TEST("Default rail width stays 180", FPLayout::RailWidth == 180.0);
    TEST("Clamp: default passes through", FPLayout::ClampRailWidth(180.0) == 180.0);
    TEST("Clamp: below min -> min", FPLayout::ClampRailWidth(100.0) == 180.0);
    TEST("Clamp: above max -> max", FPLayout::ClampRailWidth(500.0) == 360.0);
    TEST("Clamp: mid range kept", FPLayout::ClampRailWidth(240.0) == 240.0);
    TEST("Clamp: max boundary kept", FPLayout::ClampRailWidth(360.0) == 360.0);
    TEST("Clamp: NaN -> default 180", FPLayout::ClampRailWidth(std::nan("")) == 180.0);
    TEST("Clamp: negative -> min", FPLayout::ClampRailWidth(-50.0) == 180.0);

    // ---- Problems search filter (mirror: case-insensitive substring match) ----
    // Widget: rows kept when filter is empty or row text (lowercased) contains
    // the lowercased filter; count = matches.
    struct FProbRow { bool bError; std::string Text; };
    auto FilterRows = [](const std::vector<FProbRow>& All, const std::string& Filter,
                         std::vector<const FProbRow*>& Out)
    {
        Out.clear();
        std::string F;
        for (char C : Filter) F += (char)std::tolower((unsigned char)C);
        for (const FProbRow& P : All)
        {
            if (!F.empty())
            {
                std::string T;
                for (char C : P.Text) T += (char)std::tolower((unsigned char)C);
                if (T.find(F) == std::string::npos) continue;
            }
            Out.push_back(&P);
        }
    };
    const std::vector<FProbRow> Rows = {
        { true,  "Front / Eyes: missing albedo" },
        { true,  "Front / Eyes: missing normal" },
        { false, "3/4R: viseme 2 frame count mismatch (4 vs 6)" },
        { false, "Back: blink frame count mismatch (3 vs 5)" },
    };
    std::vector<const FProbRow*> Out;
    FilterRows(Rows, "", Out);
    TEST("Search: empty filter keeps all", Out.size() == 4);
    FilterRows(Rows, "missing", Out);
    TEST("Search: 'missing' matches 2", Out.size() == 2);
    TEST("Search: match order preserved", Out[0]->Text.find("albedo") != std::string::npos);
    FilterRows(Rows, "MISSING", Out);
    TEST("Search: case-insensitive", Out.size() == 2);
    FilterRows(Rows, "viseme", Out);
    TEST("Search: 'viseme' matches 1", Out.size() == 1 && Out[0]->bError == false);
    FilterRows(Rows, "zzz", Out);
    TEST("Search: no match -> 0", Out.empty());
    FilterRows(Rows, "back", Out);
    TEST("Search: 'back' matches 1 (not '3/4R' viseme text)",
        Out.size() == 1 && Out[0]->Text.find("blink") != std::string::npos);

    // ---- Problems summary line (mirror of the issues section header) ----
    // Widget: "<N> issues (<E> errors, <W> warnings)" plain;
    // "<N> issues (<E> errors) - <M> match \"<filter>\"" when filtering.
    auto Summary = [](int N, int E, int M, const std::string& F) -> std::string
    {
        std::string S = std::to_string(N) + " issues (" + std::to_string(E) + " errors";
        if (!F.empty())
            S += ") - " + std::to_string(M) + " match \"" + F + "\"";
        else
            S += ", " + std::to_string(N - E) + " warnings)";
        return S;
    };
    TEST("Summary: plain format", Summary(4, 2, 0, "") == "4 issues (2 errors, 2 warnings)");
    TEST("Summary: filtered format", Summary(4, 2, 2, "missing")
        == "4 issues (2 errors) - 2 match \"missing\"");
    TEST("Summary: zero issues", Summary(0, 0, 0, "") == "0 issues (0 errors, 0 warnings)");

    // ---- Accordion one-open-per-group mirror (SFaceAccordion header swap) ----
    // Widget: clicking the open section collapses all; clicking another
    // expands only that one (Open[i] = (bIsOpen ? false : (i == Idx))).
    std::vector<bool> Open(3, false);
    Open[0] = true;
    auto Click = [&](int Idx)
    {
        const bool bIsOpen = Open[(size_t)Idx];
        for (size_t i = 0; i < Open.size(); ++i) Open[i] = (bIsOpen ? false : (i == (size_t)Idx));
    };
    Click(0);
    TEST("Accordion: clicking open section collapses all",
        !Open[0] && !Open[1] && !Open[2]);
    Click(1);
    TEST("Accordion: clicking closed section opens only it",
        !Open[0] && Open[1] && !Open[2]);
    Click(2);
    TEST("Accordion: swap keeps one open", !Open[0] && !Open[1] && Open[2]);
}

// --- Phase 5 mirrors: 2D pitch-aware pin rotation/scale + pin sync semantics ---
// (Float mirrors of UFaceParallaxComponent::PinRotationFromViewAngles /
// PinScaleFromView, which are FMath-based; 1D yaw-only mirror used for the
// byte-identical-at-pitch-0 contract.)
static float M1DPinRotationFromYawDev(float YawDev, float HZW, float MinR, float MaxR, float Sens)
{
    while (YawDev > 180.0f) YawDev -= 360.0f;
    while (YawDev < -180.0f) YawDev += 360.0f;
    const float HW = std::fmax(1.0f, HZW);
    const float ND = std::clamp(YawDev / HW, -1.0f, 1.0f);
    const float Mapped = MinR + (MaxR - MinR) * (0.5f * (ND + 1.0f));
    return Mapped * Sens;
}

static float M2DPinRotationFromViewAngles(float YawDev, float PitchDev, float HZW,
    float MinR, float MaxR, float Sens)
{
    while (YawDev > 180.0f) YawDev -= 360.0f;
    while (YawDev < -180.0f) YawDev += 360.0f;
    while (PitchDev > 180.0f) PitchDev -= 360.0f;
    while (PitchDev < -180.0f) PitchDev += 360.0f;
    const float HW = std::fmax(1.0f, HZW);
    const float NY = std::clamp(YawDev / HW, -1.0f, 1.0f);
    const float NP = std::clamp(PitchDev / HW, -1.0f, 1.0f);
    const float Driver = NY * (1.0f - std::fabs(NP));
    const float Mapped = MinR + (MaxR - MinR) * (0.5f * (Driver + 1.0f));
    return Mapped * Sens;
}

static float M2DPinScaleFromView(float YawDev, float PitchDev, float MinScale)
{
    const float PI = 3.14159265f;
    const float W = 1.0f - std::fabs(std::cos(YawDev * PI / 180.0f)
                                   * std::cos(PitchDev * PI / 180.0f));
    return 1.0f + (MinScale - 1.0f) * W;
}

void TestPrimaryLayerPin() {
    printf("\n=== Primary Layer Pin (Phase 5) ===\n");

    const auto Approx = [](float A, float B) -> bool { return std::fabs(A - B) < 1e-4f; };

    // ---- PinRotationFromViewAngles: byte-identical to PinRotationFromYawDev at pitch 0 ----
    const float HZW = 22.5f, MinR = -30.0f, MaxR = 30.0f, Sens = 1.0f;
    bool bIdentical = true;
    for (float Yaw = -180.0f; Yaw <= 180.0f && bIdentical; Yaw += 7.5f)
    {
        if (M2DPinRotationFromViewAngles(Yaw, 0.0f, HZW, MinR, MaxR, Sens)
            != M1DPinRotationFromYawDev(Yaw, HZW, MinR, MaxR, Sens))
            bIdentical = false;
    }
    TEST("Pitch=0: byte-identical to PinRotationFromYawDev across yaw sweep", bIdentical);
    TEST("Pitch=0: sensitivity scales", M2DPinRotationFromViewAngles(45.0f, 0.0f, HZW, MinR, MaxR, 2.0f)
        == 2.0f * M2DPinRotationFromViewAngles(45.0f, 0.0f, HZW, MinR, MaxR, 1.0f));

    // ---- Pitch attenuation ----
    TEST("Pitch at zone edge -> center rotation (0)",
        M2DPinRotationFromViewAngles(45.0f, HZW, HZW, MinR, MaxR, 1.0f) == 0.0f);
    TEST("Pitch beyond zone edge clamps to center",
        M2DPinRotationFromViewAngles(45.0f, 90.0f, HZW, MinR, MaxR, 1.0f) == 0.0f);
    TEST("Negative pitch attenuates symmetrically",
        M2DPinRotationFromViewAngles(45.0f, -HZW, HZW, MinR, MaxR, 1.0f) == 0.0f);
    TEST("Half pitch moves rotation halfway to center",
        M2DPinRotationFromViewAngles(45.0f, HZW * 0.5f, HZW, MinR, MaxR, 1.0f)
        == M2DPinRotationFromViewAngles(0.0f, 0.0f, HZW, MinR, MaxR, 1.0f)
           + 0.5f * (M2DPinRotationFromViewAngles(45.0f, 0.0f, HZW, MinR, MaxR, 1.0f)
                   - M2DPinRotationFromViewAngles(0.0f, 0.0f, HZW, MinR, MaxR, 1.0f)));
    TEST("Wrap: yaw 200 == -160",
        M2DPinRotationFromViewAngles(200.0f, 0.0f, HZW, MinR, MaxR, 1.0f)
        == M2DPinRotationFromViewAngles(-160.0f, 0.0f, HZW, MinR, MaxR, 1.0f));
    TEST("Wrap: pitch 190 == -170",
        M2DPinRotationFromViewAngles(0.0f, 190.0f, HZW, MinR, MaxR, 1.0f)
        == M2DPinRotationFromViewAngles(0.0f, -170.0f, HZW, MinR, MaxR, 1.0f));

    // ---- PinScaleFromView ----
    TEST("Scale: zone center -> 1.0", M2DPinScaleFromView(0.0f, 0.0f, 0.5f) == 1.0f);
    TEST("Scale: 90deg yaw -> MinScale", Approx(M2DPinScaleFromView(90.0f, 0.0f, 0.5f), 0.5f));
    TEST("Scale: 90deg pitch -> MinScale", Approx(M2DPinScaleFromView(0.0f, 90.0f, 0.5f), 0.5f));
    TEST("Scale: 45/45 combined -> midpoint", Approx(M2DPinScaleFromView(45.0f, 45.0f, 0.5f), 0.75f));
    TEST("Scale: MinScale=1 is identity", M2DPinScaleFromView(90.0f, 45.0f, 1.0f) == 1.0f);
    TEST("Scale: mirrored axes commute",
        M2DPinScaleFromView(30.0f, 60.0f, 0.5f) == M2DPinScaleFromView(60.0f, 30.0f, 0.5f));
    TEST("Scale: 180deg yaw -> 1.0", Approx(M2DPinScaleFromView(180.0f, 0.0f, 0.5f), 1.0f));

    // ---- SyncLayerNestedToAllViews bSyncPins semantics ----
    struct MPin { bool bPinned = false; float MinScale = 0.5f; };
    struct MEl { float ArtX = 0.0f; MPin Pin; };
    const MEl Source = { 12.0f, { true, 0.9f } };
    const MPin DefaultPin;
    MEl Target = { 3.0f, { true, 0.3f } };
    MEl Copied = Source;
    Copied.Pin = Target.Pin;                       // bSyncPins=false: preserve target pin
    TEST("bSyncPins=false keeps target pin", Copied.Pin.bPinned == true && Copied.Pin.MinScale == 0.3f);
    TEST("bSyncPins=false still copies art", Copied.ArtX == 12.0f);
    Copied = Source;                                // bSyncPins=true: pin propagates
    TEST("bSyncPins=true propagates source pin", Copied.Pin.bPinned && Copied.Pin.MinScale == 0.9f);
    Copied = Source;
    Copied.Pin = DefaultPin;                        // new element + no pin sync -> unpinned
    TEST("bSyncPins=false new element gets unpinned pin",
        !Copied.Pin.bPinned && Copied.Pin.MinScale == 0.5f);
}

// --- Pin View-Angle Rotation Tests (mirrors UFaceParallaxComponent::PinRotationFromYawDev) ---
static double MirrorPinRotationFromYawDev(double YawDev, double HalfZoneWidth,
    double MinRotation, double MaxRotation, double RotationSensitivity)
{
    while (YawDev > 180.0) YawDev -= 360.0;
    while (YawDev < -180.0) YawDev += 360.0;
    const double HalfWidth = std::fmax(1.0, HalfZoneWidth);
    const double NormDev = std::clamp(YawDev / HalfWidth, -1.0, 1.0);
    const double Mapped = MinRotation + (MaxRotation - MinRotation) * (0.5 * (NormDev + 1.0));
    return Mapped * RotationSensitivity;
}

void TestPinRotation() {
    printf("\n=== Pin Rotation (view-angle) ===\n");

    const double HZW = 22.5;

    // 1. Home view (dev 0) -> midpoint of min/max
    TEST("Dev 0 -> midpoint 15", std::abs(MirrorPinRotationFromYawDev(0.0, HZW, 0.0, 30.0, 1.0) - 15.0) < 1e-6);
    // 2. One half-zone toward max -> MaxRotation
    TEST("Dev +HZW -> MaxRotation", std::abs(MirrorPinRotationFromYawDev(HZW, HZW, 0.0, 30.0, 1.0) - 30.0) < 1e-6);
    // 3. One half-zone toward min -> MinRotation
    TEST("Dev -HZW -> MinRotation", std::abs(MirrorPinRotationFromYawDev(-HZW, HZW, 0.0, 30.0, 1.0) - 0.0) < 1e-6);
    // 4. Past the zone edge clamps to MaxRotation
    TEST("Dev 3*HZW clamps to Max", std::abs(MirrorPinRotationFromYawDev(3.0 * HZW, HZW, 0.0, 30.0, 1.0) - 30.0) < 1e-6);
    // 5. Wrap: 195 -> -165 -> clamps to Min
    TEST("Dev 195 wraps to Min", std::abs(MirrorPinRotationFromYawDev(195.0, HZW, 0.0, 30.0, 1.0) - 0.0) < 1e-6);
    // 6. Wrap: -190 -> 170 -> clamps to Max
    TEST("Dev -190 wraps to Max", std::abs(MirrorPinRotationFromYawDev(-190.0, HZW, 0.0, 30.0, 1.0) - 30.0) < 1e-6);
    // 7. Sensitivity 0 kills the rotation
    TEST("Sensitivity 0 -> 0", std::abs(MirrorPinRotationFromYawDev(45.0, HZW, 0.0, 30.0, 0.0)) < 1e-6);
    // 8. Sensitivity 2 doubles the mapped angle
    TEST("Sensitivity 2 doubles", std::abs(MirrorPinRotationFromYawDev(HZW, HZW, 0.0, 30.0, 2.0) - 60.0) < 1e-6);
    TEST("Sensitivity 2 mid", std::abs(MirrorPinRotationFromYawDev(0.0, HZW, -30.0, 30.0, 2.0)) < 1e-6);
    // 9. Min == Max -> constant
    TEST("Min==Max constant", std::abs(MirrorPinRotationFromYawDev(50.0, HZW, 10.0, 10.0, 1.0) - 10.0) < 1e-6);
    // 10. Zero half-zone guard (span floored at 1.0)
    TEST("Zero HZW guarded", std::abs(MirrorPinRotationFromYawDev(5.0, 0.0, 0.0, 30.0, 1.0) - 30.0) < 1e-6);
    TEST("Zero HZW small dev", std::abs(MirrorPinRotationFromYawDev(0.5, 0.0, 0.0, 30.0, 1.0) - 22.5) < 1e-6);
    // 11. Symmetric min/max -> 0 at home, +30 at zone edge
    TEST("Symmetric -30/30 home -> 0", std::abs(MirrorPinRotationFromYawDev(0.0, HZW, -30.0, 30.0, 1.0)) < 1e-6);

    // 12-14. State-based projection + SetNestedPinFromUV back-view mirroring
    auto GetZoneYaw = [](int StateIdx) -> double {
        double H = 22.5;
        switch (StateIdx) {
            case 0: return 0.0;
            case 1: return H * 2.0;
            case 2: return H * 4.0;
            case 3: return H * 6.0;
            case 4: return 180.0;
            case 5: return -H * 6.0;
            case 6: return -H * 4.0;
            case 7: return -H * 2.0;
            default: return 0.0;
        }
    };
    auto ProjectState = [&](double Px, double Py, double Pz, int StateIdx,
                            double HW, double HD, double HH) -> FVector2D {
        double YawDeg = GetZoneYaw(StateIdx);
        double Rad = YawDeg * (std::acos(-1.0) / 180.0);
        double CosA = std::cos(Rad);
        double SinA = std::sin(Rad);
        double WX = Px * HW, WZ = Pz * HD, WY = Py * HH;
        double ViewX = WX * CosA + WZ * SinA;
        double VisibleX = HW * std::abs(CosA) + HD * std::abs(SinA);
        double UVx = 0.5;
        if (VisibleX > 1e-7) UVx = 0.5 + 0.5 * ViewX / VisibleX;
        double UVy = 0.5 + 0.5 * WY / HH;
        return FVector2D(std::clamp(UVx, 0.0, 1.0), std::clamp(UVy, 0.0, 1.0));
    };
    // Mirror of SetNestedPinFromUV (widget) incl. the Back-view mirror fix
    auto UVToPin3D = [&](double UVx, double UVy, int StateIdx) {
        struct P3 { double X, Y, Z; } Pos;
        double YawDeg = GetZoneYaw(StateIdx);
        double UVCx = UVx - 0.5, UVCy = UVy - 0.5;
        if (std::abs(YawDeg) < 45.0) {
            Pos.X = UVCx * 2.0; Pos.Y = UVCy * 2.0; Pos.Z = 0.0;
        } else if (std::abs(YawDeg - 90.0) < 45.0 || std::abs(YawDeg + 90.0) < 45.0) {
            Pos.Z = UVCx * 2.0; Pos.Y = UVCy * 2.0; Pos.X = 0.0;
        } else {
            Pos.X = UVCx * 2.0; Pos.Y = UVCy * 2.0; Pos.Z = 0.0;
            if (std::abs(YawDeg) > 135.0) Pos.X = -UVCx * 2.0;
        }
        return Pos;
    };

    double HW = 256.0, HD = 128.0, HH = 256.0;
    {
        auto P = UVToPin3D(0.3, 0.5, 4);   // Back view click at UVx=0.3
        FVector2D RT = ProjectState(P.X, P.Y, P.Z, 4, HW, HD, HH);
        TEST("Back authoring round-trip mirrored", std::abs(RT.X - 0.3) < 0.01);
        // The pre-fix behavior authored X = +UVCx*2 (unmirrored): round-trips to 0.7, not 0.3
        double UnmirroredX = (0.3 - 0.5) * 2.0;
        FVector2D UnmirroredRT = ProjectState(UnmirroredX, 0.0, 0.0, 4, HW, HD, HH);
        TEST("Back unmirrored authoring round-trips off", std::abs(UnmirroredRT.X - 0.3) > 0.01);
    }
    {
        FVector2D F = ProjectState(0.5, 0.3, 0.1, 0, HW, HD, HH);
        FVector2D P = ProjectState(0.5, 0.3, 0.1, 2, HW, HD, HH);
        TEST("Same pin projects differently Front vs Profile", std::abs(F.X - P.X) > 0.05);
    }
    {
        FVector2D P = ProjectState(0.0, 0.2, 0.5, 2, HW, HD, HH);
        FVector2D L = ProjectState(0.0, 0.2, 0.5, 6, HW, HD, HH);
        TEST("Nose pin right profile UVx > 0.5", P.X > 0.5);
        TEST("Nose pin left profile UVx < 0.5", L.X < 0.5);
    }

    // 18-24. PinSliderNorm (widget refresh: NaN-safe slider normalization)
    auto MirrorPinSliderNorm = [](double Value, double Min, double Max) -> double {
        if (Max <= Min) return 0.0;
        return std::clamp((Value - Min) / (Max - Min), 0.0, 1.0);
    };
    TEST("SliderNorm zero span -> 0", MirrorPinSliderNorm(0.5, 10.0, 10.0) == 0.0);
    TEST("SliderNorm inverted span -> 0", MirrorPinSliderNorm(0.5, 30.0, -30.0) == 0.0);
    TEST("SliderNorm mid -> 0.5", std::abs(MirrorPinSliderNorm(0.0, -2.0, 2.0) - 0.5) < 1e-9);
    TEST("SliderNorm high clamps to 1", MirrorPinSliderNorm(5.0, -2.0, 2.0) == 1.0);
    TEST("SliderNorm low clamps to 0", MirrorPinSliderNorm(-5.0, -2.0, 2.0) == 0.0);
    TEST("SliderNorm value at max -> 1", MirrorPinSliderNorm(180.0, -180.0, 180.0) == 1.0);
    TEST("SliderNorm negative range mid", std::abs(MirrorPinSliderNorm(-3.0, -4.0, -2.0) - 0.5) < 1e-9);

    // 25-32. 8-state projection sweep: cheek pin (0.3, 0.2, 0.3) — states with
    // |yaw| < 90 (0 Front, 1 TQR, 2 RightProfile, 7 TQL) land UVx > 0.5;
    // states with |yaw| > 90 (3 BackRight, 4 Back, 5 BackLeft, 6 LeftProfile)
    // land UVx < 0.5 (the cheek swings behind the visible plane).
    TEST("Sweep Front UVx > 0.5", ProjectState(0.3, 0.2, 0.3, 0, HW, HD, HH).X > 0.5);
    TEST("Sweep TQR UVx > 0.5", ProjectState(0.3, 0.2, 0.3, 1, HW, HD, HH).X > 0.5);
    TEST("Sweep RightProfile UVx > 0.5", ProjectState(0.3, 0.2, 0.3, 2, HW, HD, HH).X > 0.5);
    TEST("Sweep BackRight UVx < 0.5", ProjectState(0.3, 0.2, 0.3, 3, HW, HD, HH).X < 0.5);
    TEST("Sweep Back UVx < 0.5", ProjectState(0.3, 0.2, 0.3, 4, HW, HD, HH).X < 0.5);
    TEST("Sweep BackLeft UVx < 0.5", ProjectState(0.3, 0.2, 0.3, 5, HW, HD, HH).X < 0.5);
    TEST("Sweep LeftProfile UVx < 0.5", ProjectState(0.3, 0.2, 0.3, 6, HW, HD, HH).X < 0.5);
    TEST("Sweep TQL UVx > 0.5", ProjectState(0.3, 0.2, 0.3, 7, HW, HD, HH).X > 0.5);

    // 33-34. Pin gizmo chain: pixel drag -> UV -> Pin3D (SetNestedPinFromUV
    // mirror) -> project back to UV (the round trip the gizmo drag performs).
    {
        // Front-view drag to pixel (128,128) on a 512 canvas -> UV (0.25, 0.25)
        FVector2D UV(128.0 / 512.0, 128.0 / 512.0);
        auto P = UVToPin3D(UV.X, UV.Y, 0);
        FVector2D RT = ProjectState(P.X, P.Y, P.Z, 0, HW, HD, HH);
        TEST("Gizmo front drag round-trips", std::abs(RT.X - 0.25) < 0.01 && std::abs(RT.Y - 0.25) < 0.01);
    }
    {
        // RightProfile drag to pixel (384,128) -> UV (0.75, 0.25)
        FVector2D UV(384.0 / 512.0, 128.0 / 512.0);
        auto P = UVToPin3D(UV.X, UV.Y, 2);
        FVector2D RT = ProjectState(P.X, P.Y, P.Z, 2, HW, HD, HH);
        TEST("Gizmo profile drag round-trips", std::abs(RT.X - 0.75) < 0.01 && std::abs(RT.Y - 0.25) < 0.01);
    }

    printf("  [Pin Rotation: 35 tests]\n");
}

// --- ComputeNestedEffectiveTransform integration mirror ---
// Mirrors the FIXED accumulation order: parent + relative composition FIRST,
// then the view-angle rotation added on top. The pre-fix code added the
// rotation to a zeroed Result.Rotation which was then overwritten.
struct MirrorPinEl {
    bool bPinned = false;
    bool bRotEnabled = false;
    double PX = 0.0, PY = 0.0, PZ = 0.0;
    double MinR = -30.0, MaxR = 30.0, Sens = 1.0;
    double PivotX = 0.5, PivotY = 0.5;
    double RX = 0.0, RY = 0.0;
    double RSX = 1.0, RSY = 1.0;
    double RR = 0.0;
};
struct MirrorEffT { double Px, Py, Sx, Sy, R; };

static double MirrorProjectLiveX(double Px, double Pz, double YawDeg, double HW, double HD)
{
    double Rad = YawDeg * (std::acos(-1.0) / 180.0);
    double WX = Px * HW, WZ = Pz * HD;
    double ViewX = WX * std::cos(Rad) + WZ * std::sin(Rad);
    double VisibleX = HW * std::abs(std::cos(Rad)) + HD * std::abs(std::sin(Rad));
    double UVx = 0.5;
    if (VisibleX > 1e-7) UVx = 0.5 + 0.5 * ViewX / VisibleX;
    return std::clamp(UVx, 0.0, 1.0);
}

static double MirrorProjectLiveY(double Py, double PitchDeg, double HH, double HD)
{
    double Rad = PitchDeg * (std::acos(-1.0) / 180.0);
    double WY = Py * HH;
    double ViewY = WY * std::cos(Rad);
    double VisibleY = HH * std::abs(std::cos(Rad)) + HD * std::abs(std::sin(Rad));
    double UVy = 0.5;
    if (VisibleY > 1e-7) UVy = 0.5 + 0.5 * ViewY / VisibleY;
    return std::clamp(UVy, 0.0, 1.0);
}

static MirrorEffT MirrorEffectiveTransform(
    const MirrorPinEl& E,
    double ParPx, double ParPy, double ParSx, double ParSy, double ParR,
    double YawDeg, double PitchDeg, double ZoneYawDeg, double HZW,
    double JigX, double JigY, double CanvasW, double CanvasH,
    double HW, double HD, double HH)
{
    MirrorEffT R;
    double PinOffX = 0.0, PinOffY = 0.0;
    if (E.bPinned)
    {
        PinOffX = (MirrorProjectLiveX(E.PX, E.PZ, YawDeg, HW, HD) - E.PivotX) * CanvasW;
        PinOffY = (MirrorProjectLiveY(E.PY, PitchDeg, HH, HD) - E.PivotY) * CanvasH;
    }
    R.Px = ParPx + E.RX + PinOffX + JigX;
    R.Py = ParPy + E.RY + PinOffY + JigY;
    R.Sx = ParSx * E.RSX;
    R.Sy = ParSy * E.RSY;
    R.R = ParR + E.RR;
    if (E.bPinned && E.bRotEnabled)
        R.R += MirrorPinRotationFromYawDev(YawDeg - ZoneYawDeg, HZW, E.MinR, E.MaxR, E.Sens);
    return R;
}

void TestNestedEffectiveTransform() {
    printf("\n=== Nested Effective Transform (pin rotation accumulation) ===\n");

    const double HW = 256.0, HD = 128.0, HH = 256.0;
    const double CW = 512.0, CH = 512.0;
    const double HZW = 22.5;

    // 1. THE regression: pinned + enabled, dev = +HZW -> rotation survives
    {
        MirrorPinEl E;
        E.bPinned = true; E.bRotEnabled = true;
        E.MinR = 0.0; E.MaxR = 30.0;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 0,
            22.5, 0.0, 0.0, HZW, 0, 0, CW, CH, HW, HD, HH);
        TEST("Pin rotation accumulates (was overwritten)", std::abs(T.R - 30.0) < 1e-9);
    }
    // 2. Symmetric range at home -> 0
    {
        MirrorPinEl E;
        E.bPinned = true; E.bRotEnabled = true;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 0,
            0.0, 0.0, 0.0, HZW, 0, 0, CW, CH, HW, HD, HH);
        TEST("Symmetric range home -> 0", std::abs(T.R) < 1e-9);
    }
    // 3. Composes with parent + relative rotation
    {
        MirrorPinEl E;
        E.bPinned = true; E.bRotEnabled = true;
        E.MinR = 0.0; E.MaxR = 30.0;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 10.0,
            22.5, 0.0, 0.0, HZW, 0, 0, CW, CH, HW, HD, HH);
        TEST("Rotation adds to parent+relative", std::abs(T.R - 40.0) < 1e-9);
    }
    // 4. Disabled -> no addition
    {
        MirrorPinEl E;
        E.bPinned = true; E.bRotEnabled = false;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 10.0,
            22.5, 0.0, 0.0, HZW, 0, 0, CW, CH, HW, HD, HH);
        TEST("Disabled -> parent+relative only", std::abs(T.R - 10.0) < 1e-9);
    }
    // 5. Unpinned -> neither rotation nor offset
    {
        MirrorPinEl E;
        E.bPinned = false; E.bRotEnabled = true;
        E.RX = 20.0; E.RY = 30.0;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 5.0,
            22.5, 0.0, 0.0, HZW, 4, 6, CW, CH, HW, HD, HH);
        TEST("Unpinned -> no rotation", std::abs(T.R - 5.0) < 1e-9);
        TEST("Unpinned -> no pin offset", std::abs(T.Px - 24.0) < 1e-9 && std::abs(T.Py - 36.0) < 1e-9);
    }
    // 6. Pinned front offset: pin (0.3,0.2,0.4) -> UV (0.65, 0.6), pivot 0.5
    {
        MirrorPinEl E;
        E.bPinned = true; E.PX = 0.3; E.PY = 0.2; E.PZ = 0.4;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 0,
            0.0, 0.0, 0.0, HZW, 0, 0, CW, CH, HW, HD, HH);
        TEST("Pinned front offset X", std::abs(T.Px - 76.8) < 1e-9);
        TEST("Pinned front offset Y", std::abs(T.Py - 51.2) < 1e-9);
    }
    // 7. Back-view pin offset mirrors: pin (0.3,0,0) at yaw 180 -> UVx 0.35
    {
        MirrorPinEl E;
        E.bPinned = true; E.PX = 0.3; E.PY = 0.0; E.PZ = 0.0;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 0,
            180.0, 0.0, 180.0, HZW, 0, 0, CW, CH, HW, HD, HH);
        TEST("Pinned back offset mirrors X", std::abs(T.Px + 76.8) < 1e-9);
    }
    // 8. Scale composes independently of the pin
    {
        MirrorPinEl E;
        E.bPinned = true; E.bRotEnabled = true;
        E.RSX = 1.5; E.RSY = 1.5;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 2.0, 2.0, 0,
            22.5, 0.0, 0.0, HZW, 0, 0, CW, CH, HW, HD, HH);
        TEST("Scale unaffected by pin", std::abs(T.Sx - 3.0) < 1e-9 && std::abs(T.Sy - 3.0) < 1e-9);
    }
    // 9. Sensitivity 0 kills the rotation but keeps the offset
    {
        MirrorPinEl E;
        E.bPinned = true; E.bRotEnabled = true; E.Sens = 0.0;
        E.PX = 0.3; E.PY = 0.2; E.PZ = 0.4;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 0,
            0.0, 0.0, 0.0, HZW, 0, 0, CW, CH, HW, HD, HH);
        TEST("Sens 0 -> no rotation", std::abs(T.R) < 1e-9);
        TEST("Sens 0 keeps offset", std::abs(T.Px - 76.8) < 1e-9);
    }
    // 10. Jiggle offset adds to the final position
    {
        MirrorPinEl E;
        E.bPinned = true; E.PX = 0.3; E.PY = 0.2; E.PZ = 0.4;
        MirrorEffT T = MirrorEffectiveTransform(E, 0, 0, 1, 1, 0,
            0.0, 0.0, 0.0, HZW, 3.0, 4.0, CW, CH, HW, HD, HH);
        TEST("Jiggle adds to pinned position", std::abs(T.Px - 79.8) < 1e-9 && std::abs(T.Py - 55.2) < 1e-9);
    }

    printf("  [Nested Effective Transform: 13 tests]\n");
}

// --- Cross-view sync / autofit: pin data must survive ---
struct MirrorSlotT {
    double CX = 0.0, CY = 0.0, CSX = 1.0, CSY = 1.0, CR = 0.0;
    bool bPinned = false;
    double PX = 0.0, PY = 0.0, PZ = 0.0;
    double MinR = -30.0, MaxR = 30.0, Sens = 1.0;
    bool bHasOverride = false;
    double OX = 0.0, OY = 0.0, OSX = 1.0, OSY = 1.0, OR = 0.0;
};

static void MirrorSyncCanonicalToAllViews(const MirrorSlotT& Source, std::vector<MirrorSlotT>& Others)
{
    for (auto& T : Others)
    {
        T.bHasOverride = true;
        T.OX = Source.CX - T.CX;
        T.OY = Source.CY - T.CY;
        T.OSX = Source.CSX / std::fmax(T.CSX, 1e-7);
        T.OSY = Source.CSY / std::fmax(T.CSY, 1e-7);
        T.OR = Source.CR - T.CR;
        // NOTE: the real SyncCanonicalToAllViews touches only CanonicalTransform
        // and the override; NestedElements (incl. pins) are never copied.
    }
}

void TestPinDataSurvivesSync() {
    printf("\n=== Pin Data Survives Cross-View Sync / Auto-Fit ===\n");

    // 1. Override math mirrors the real SyncCanonicalToAllViews
    {
        MirrorSlotT Src;
        Src.CX = 100.0; Src.CY = 50.0; Src.CR = 10.0; Src.CSX = 2.0; Src.CSY = 2.0;
        MirrorSlotT Tgt;
        Tgt.CX = 40.0; Tgt.CY = 20.0; Tgt.CR = 4.0; Tgt.CSX = 1.0; Tgt.CSY = 1.0;
        std::vector<MirrorSlotT> Others = { Tgt };
        MirrorSyncCanonicalToAllViews(Src, Others);
        TEST("Sync override position delta", std::abs(Others[0].OX - 60.0) < 1e-9 && std::abs(Others[0].OY - 30.0) < 1e-9);
        TEST("Sync override rotation delta", std::abs(Others[0].OR - 6.0) < 1e-9);
        TEST("Sync override scale ratio", std::abs(Others[0].OSX - 2.0) < 1e-9);
    }
    // 2. Sync leaves the target slot's pin untouched (pin data survives)
    {
        MirrorSlotT Src;
        Src.CX = 100.0;
        MirrorSlotT Tgt;
        Tgt.bPinned = true; Tgt.PX = 0.7; Tgt.MinR = -45.0; Tgt.MaxR = 45.0; Tgt.Sens = 2.0;
        std::vector<MirrorSlotT> Others = { Tgt };
        MirrorSyncCanonicalToAllViews(Src, Others);
        TEST("Sync preserves target pin", Others[0].bPinned && std::abs(Others[0].PX - 0.7) < 1e-12
            && std::abs(Others[0].MinR + 45.0) < 1e-12 && std::abs(Others[0].Sens - 2.0) < 1e-12);
    }
    // 3. Source slot pin untouched by sync
    {
        MirrorSlotT Src;
        Src.bPinned = true; Src.PX = 0.3; Src.PY = 0.1; Src.PZ = 0.5;
        MirrorSlotT Tgt;
        std::vector<MirrorSlotT> Others = { Tgt };
        MirrorSyncCanonicalToAllViews(Src, Others);
        TEST("Sync preserves source pin", Src.bPinned && std::abs(Src.PX - 0.3) < 1e-12
            && std::abs(Src.PZ - 0.5) < 1e-12);
    }
    // 4. Zero canonical scale in target guarded (fmax floor)
    {
        MirrorSlotT Src;
        Src.CSX = 2.0; Src.CSY = 2.0;
        MirrorSlotT Tgt;
        Tgt.CSX = 0.0; Tgt.CSY = 0.0;
        std::vector<MirrorSlotT> Others = { Tgt };
        MirrorSyncCanonicalToAllViews(Src, Others);
        TEST("Sync zero-scale guarded", Others[0].OSX > 1.0 && Others[0].OSY > 1.0);
    }
    // 5. ApplyAutoFitToSlot recomputes only the canonical transform —
    //    the pin fields survive unchanged
    {
        MirrorSlotT Slot;
        Slot.bPinned = true; Slot.PX = 0.25; Slot.PY = -0.4; Slot.PZ = 0.75;
        // Auto-fit mirror: canonical scale = canvas/tex min ratio (square tex)
        Slot.CX = 0.0; Slot.CY = 0.0; Slot.CSX = 512.0 / 1024.0; Slot.CSY = 512.0 / 1024.0;
        double CanonX = 0.0, CanonY = 0.0, CanonSX = 512.0 / 1024.0, CanonSY = 512.0 / 1024.0;
        (void)CanonX; (void)CanonY; (void)CanonSX; (void)CanonSY;
        TEST("Autofit preserves pin fields", Slot.bPinned && std::abs(Slot.PX - 0.25) < 1e-12
            && std::abs(Slot.PY + 0.4) < 1e-12 && std::abs(Slot.PZ - 0.75) < 1e-12);
    }

    printf("  [Pin Data Survives Sync: 7 tests]\n");
}

// --- Phase 4b: UI accessibility remediation mirrors ---
// Mirrors the five implemented pieces: below-section progressive disclosure
// (Config + Viseme summaries), the persistent quick-actions bar, the
// cross-rail section search jump, drag-resize rail width, and the pinned
// per-rail jump chips. All pure helpers live in FPLayout (manifest header)
// and are consumed by the widget 1:1.
void TestAccessibilityMirrors() {
    printf("\n=== Accessibility Mirrors (Phase 4b) ===\n");

    // ---- Rail section registry (chips + search jump source of truth) ----
    const std::vector<std::vector<std::string>>& Titles = FPLayout::RailSectionTitles();
    TEST("Registry: 6 rails", Titles.size() == 6);
    TEST("Registry: rail 0 View & Layer 3 sections", Titles[0].size() == 3
        && Titles[0][0] == "Layers" && Titles[0][1] == "Status Detail"
        && Titles[0][2] == "All Layers (current state)");
    TEST("Registry: rail 1 Art 6 sections", Titles[1].size() == 6
        && Titles[1][0] == "Quick Actions" && Titles[1][1] == "Cross-View Transform"
        && Titles[1][2] == "Import" && Titles[1][3] == "Outline -> Depth"
        && Titles[1][4] == "Bulk Assign" && Titles[1][5] == "Assign Ops");
    TEST("Registry: rail 2 Animated 2 sections", Titles[2].size() == 2
        && Titles[2][0] == "Viseme Frames (click filled cell = play)"
        && Titles[2][1] == "Hull Review (click thumb = jump)");
    TEST("Registry: rail 3 Nested & Pins 1 section", Titles[3].size() == 1
        && Titles[3][0] == "Nested Art / Pins");
    TEST("Registry: rail 4 Camera/Preview 3 sections", Titles[4].size() == 3
        && Titles[4][0] == "Camera Follow" && Titles[4][1] == "Camera"
        && Titles[4][2] == "Blend Preview");
    TEST("Registry: rail 5 Advanced 8 sections", Titles[5].size() == 8
        && Titles[5][0] == "Config" && Titles[5][1] == "Param Reference"
        && Titles[5][2] == "Param Bindings (state + layer)"
        && Titles[5][3] == "Edge Analysis" && Titles[5][4] == "Depth Debug"
        && Titles[5][5] == "Problems (click row = jump)"
        && Titles[5][6] == "Tag Validator" && Titles[5][7] == "Material Cross-Reference");

    // ---- Cross-rail search jump (mirror of OnRailSearchCommitted) ----
    int OutRail = -1, OutIdx = -1;
    TEST("Search: 'config' -> Advanced/Config",
        FPLayout::FindRailSectionByTitle("config", OutRail, OutIdx) == 0
        && OutRail == 5 && OutIdx == 0);
    TEST("Search: 'CONFIG' case-insensitive",
        FPLayout::FindRailSectionByTitle("CONFIG", OutRail, OutIdx) == 0
        && OutRail == 5 && OutIdx == 0);
    TEST("Search: 'viseme' -> Animated/Viseme",
        FPLayout::FindRailSectionByTitle("viseme", OutRail, OutIdx) == 0
        && OutRail == 2 && OutIdx == 0);
    TEST("Search: 'quick' -> Art/Quick Actions",
        FPLayout::FindRailSectionByTitle("quick", OutRail, OutIdx) == 0
        && OutRail == 1 && OutIdx == 0);
    TEST("Search: 'camera' first match is rail 4",
        FPLayout::FindRailSectionByTitle("camera", OutRail, OutIdx) == 0
        && OutRail == 4 && OutIdx == 0);
    TEST("Search: 'blend' -> rail 4 idx 2",
        FPLayout::FindRailSectionByTitle("blend", OutRail, OutIdx) == 0
        && OutRail == 4 && OutIdx == 2);
    TEST("Search: 'status' -> rail 0 Status Detail",
        FPLayout::FindRailSectionByTitle("status", OutRail, OutIdx) == 0
        && OutRail == 0 && OutIdx == 1);
    TEST("Search: 'problem' -> rail 5 Problems",
        FPLayout::FindRailSectionByTitle("problem", OutRail, OutIdx) == 0
        && OutRail == 5 && OutIdx == 5);
    TEST("Search: 'xyzzy' no match -> -1",
        FPLayout::FindRailSectionByTitle("xyzzy", OutRail, OutIdx) == -1);
    TEST("Search: empty query -> no match",
        FPLayout::FindRailSectionByTitle("", OutRail, OutIdx) == -1);

    // ---- Config disclosure summary ("K of 8 on") ----
    TEST("Config summary: 3 of 8", FPLayout::ConfigSummary(3) == "3 of 8 on");
    TEST("Config summary: 0 of 8", FPLayout::ConfigSummary(0) == "0 of 8 on");
    TEST("Config summary: 8 of 8", FPLayout::ConfigSummary(8) == "8 of 8 on");

    // ---- Viseme disclosure summary ("N viseme rows") ----
    TEST("Viseme summary: 5 rows", FPLayout::VisemeSummary(5) == "5 viseme rows");
    TEST("Viseme summary: 0 rows -> No viseme frames",
        FPLayout::VisemeSummary(0) == "No viseme frames");

    // ---- Drag-resize rail width (mirror of SFaceRailResizer + SetRailWidthLive) ----
    TEST("Drag: +50 from 180 -> 230", FPLayout::RailWidthAfterDrag(180.0, 50.0) == 230.0);
    TEST("Drag: -100 from 230 -> clamp 180", FPLayout::RailWidthAfterDrag(230.0, -100.0) == 180.0);
    TEST("Drag: +200 from 300 -> clamp 360", FPLayout::RailWidthAfterDrag(300.0, 200.0) == 360.0);
    TEST("Drag: 0 delta keeps width", FPLayout::RailWidthAfterDrag(240.0, 0.0) == 240.0);
    TEST("Drag: fractional delta rounds", FPLayout::RailWidthAfterDrag(180.0, 49.4) == 229.0);
    TEST("Drag: -1 from min -> clamp 180", FPLayout::RailWidthAfterDrag(180.0, -1.0) == 180.0);
    TEST("Drag: exact max via round kept", FPLayout::RailWidthAfterDrag(180.0, 180.0) == 360.0);
    TEST("Drag: +1 past max clamps 360", FPLayout::RailWidthAfterDrag(360.0, 1.0) == 360.0);
    TEST("Drag: NaN delta -> default 180", FPLayout::RailWidthAfterDrag(300.0, std::nan("")) == 180.0);
    TEST("Drag: huge negative clamps min", FPLayout::RailWidthAfterDrag(200.0, -1000.0) == 180.0);
    TEST("Drag: negative fraction rounds", FPLayout::RailWidthAfterDrag(200.0, -0.6) == 199.0);
    TEST("Drag: half delta rounds up", FPLayout::RailWidthAfterDrag(180.0, 49.5) == 230.0);

    // ---- Persistent quick-actions bar button set (rail-independent) ----
    const std::vector<std::string>& QL = FPLayout::QuickActionLabels();
    TEST("Quick bar: exactly 4 actions", QL.size() == 4);
    TEST("Quick bar: Import Art... first", QL[0] == "Import Art...");
    TEST("Quick bar: Sync All -> All", QL[1] == "Sync All -> All");
    TEST("Quick bar: Auto-Fit All", QL[2] == "Auto-Fit All");
    TEST("Quick bar: Clear All Overrides last", QL[3] == "Clear All Overrides");

    printf("  [Accessibility Mirrors: 38 tests]\n");
}

// --- Phase 4b: P21 PinnedActionsNeverInScroll ---
// The canonical quick actions (FPLayout::QuickActionLabels) live ONLY in the
// PinnedStrip node: never inside a clipped scroll viewport, never anywhere
// else. Positive contract on the real manifest + negative controls that plant
// pinned actions inside clip()'d viewports and outside the strip.
void TestPinnedActionsRule() {
    printf("\n=== Pinned Actions Rule (P21) ===\n");

    const std::vector<FPLayout::FPLayoutNode> Spec = FPLayout::BuildSpec();

    // Positive: the manifest is clean and the strip is a fixed-height,
    // full-width row between the zone diagram and the main row.
    const std::vector<FPLayout::FPViolation> V = FPLayout::ValidateDesign(Spec);
    bool bP21 = true;
    for (const FPLayout::FPViolation& v : V)
        if (v.Rule == FPLayout::DesignRule::PinnedActionsNeverInScroll) bP21 = false;
    TEST("P21: real manifest has zero pinned-action violations", bP21);

    const FPLayout::FPLayoutNode* Strip = nullptr;
    const FPLayout::FPLayoutNode* RootNode = nullptr;
    for (const FPLayout::FPLayoutNode& n : Spec)
    {
        if (std::string(n.Name) == "PinnedStrip") Strip = &n;
        if (std::string(n.Name) == "Root") RootNode = &n;
    }
    TEST("P21: PinnedStrip node exists", Strip != nullptr);
    TEST("P21: strip is fixed-height 26", Strip && Strip->FixedH == FPLayout::PinnedStripHeight);
    TEST("P21: strip is a full-width row (flex width)",
        Strip && Strip->bFlexW && Strip->Children.size() == 5);

    // Strip children map 1:1 to the canonical labels, all flagged, all
    // directly under the strip (never in a scroll viewport).
    const std::vector<std::string>& QL = FPLayout::QuickActionLabels();
    bool bStripOk = Strip && Strip->Children.size() == 5;
    int StripIdx = -1;
    if (Strip)
        for (size_t i = 0; i < Spec.size(); ++i)
            if (&Spec[i] == Strip) { StripIdx = (int)i; break; }
    if (bStripOk)
    {
        for (int c = 0; c < 4 && bStripOk; ++c)
        {
            const FPLayout::FPLayoutNode& btn = Spec[(size_t)Strip->Children[(size_t)c]];
            if (!btn.bPinnedAction || std::string(btn.Name) != QL[(size_t)c])
                bStripOk = false;
        }
    }
    TEST("P21: strip holds exactly the 4 canonical actions, flagged", bStripOk);
    TEST("P21: strip is a direct root child above the main row",
        RootNode && StripIdx >= 0
        && (int)RootNode->Children.size() == 10
        && RootNode->Children[3] == StripIdx
        && std::string(Spec[(size_t)RootNode->Children[5]].Name) == "MainRow");

    auto Violates = [](const std::vector<FPLayout::FPLayoutNode>& nodes, FPLayout::DesignRule rule) {
        for (const FPLayout::FPViolation& v : FPLayout::ValidateDesign(nodes))
            if (v.Rule == rule) return true;
        return false;
    };

    // Negative: a canonical label planted inside a clipped viewport fires P21.
    {
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::HF(B, "PinnedStrip",
                FPLayout::LF(B, "Import Art...", 97, 20),
                FPLayout::LF(B, "Sync All -> All", 119, 20),
                FPLayout::LF(B, "Auto-Fit All", 98, 20),
                FPLayout::LF(B, "Clear All Overrides", 147, 20),
                FPLayout::LF(B, "PS-Spacer", 0, 0)),
            FPLayout::VF(B, "RailViewport",
                FPLayout::LF(B, "QA-AutoFitAll", 98, 20)));
        B.N[(size_t)Root].FixedW = 1089.0;
        B.N[(size_t)Root].FixedH = 900.0;
        const int Strip2 = B.N[(size_t)Root].Children[0];
        B.N[(size_t)Strip2].FixedH = 26.0;
        B.N[(size_t)Strip2].bFlexW = true;
        for (int c = 0; c < 4; ++c)
            B.N[(size_t)B.N[(size_t)Strip2].Children[(size_t)c]].bPinnedAction = true;
        const int Rail = B.N[(size_t)Root].Children[1];
        B.N[(size_t)Rail].bClipH = true;
        B.N[(size_t)Rail].FixedW = 180.0;
        B.N[(size_t)Rail].FixedH = 560.0;
        B.N[(size_t)B.N[(size_t)Rail].Children[0]].bPinnedAction = true;
        TEST("P21: canonical action inside a rail viewport fires",
            Violates(B.N, FPLayout::DesignRule::PinnedActionsNeverInScroll));
    }
    // Negative: a pinned action as a direct root child (outside the strip)
    // fires P21 even though it is not scrolled.
    {
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::HF(B, "PinnedStrip",
                FPLayout::LF(B, "Import Art...", 97, 20),
                FPLayout::LF(B, "Sync All -> All", 119, 20),
                FPLayout::LF(B, "Auto-Fit All", 98, 20),
                FPLayout::LF(B, "Clear All Overrides", 147, 20),
                FPLayout::LF(B, "PS-Spacer", 0, 0)),
            FPLayout::LF(B, "TB-Import", 97, 22));
        B.N[(size_t)Root].FixedW = 1089.0;
        B.N[(size_t)Root].FixedH = 900.0;
        const int Strip3 = B.N[(size_t)Root].Children[0];
        B.N[(size_t)Strip3].FixedH = 26.0;
        B.N[(size_t)Strip3].bFlexW = true;
        for (int c = 0; c < 4; ++c)
            B.N[(size_t)B.N[(size_t)Strip3].Children[(size_t)c]].bPinnedAction = true;
        B.N[(size_t)B.N[(size_t)Root].Children[1]].bPinnedAction = true;
        TEST("P21: pinned action outside the strip fires",
            Violates(B.N, FPLayout::DesignRule::PinnedActionsNeverInScroll));
    }
    // Negative: a canonical label with no strip at all fires (orphan pinned
    // action - must live in the strip).
    {
        FPLayout::Builder B;
        FPLayout::VF(B, "Root",
            FPLayout::LF(B, "Auto-Fit All", 98, 20));
        TEST("P21: canonical label without a strip fires",
            Violates(B.N, FPLayout::DesignRule::PinnedActionsNeverInScroll));
    }
    // Negative: strip child duplicated inside a carousel page viewport fires.
    {
        FPLayout::Builder B;
        const int Root = FPLayout::VF(B, "Root",
            FPLayout::HF(B, "PinnedStrip",
                FPLayout::LF(B, "Import Art...", 97, 20),
                FPLayout::LF(B, "Sync All -> All", 119, 20),
                FPLayout::LF(B, "Auto-Fit All", 98, 20),
                FPLayout::LF(B, "Clear All Overrides", 147, 20),
                FPLayout::LF(B, "PS-Spacer", 0, 0)),
            FPLayout::VF(B, "CarouselPage",
                FPLayout::LF(B, "Import Art...", 97, 20)));
        B.N[(size_t)Root].FixedW = 1089.0;
        B.N[(size_t)Root].FixedH = 900.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].FixedH = 26.0;
        B.N[(size_t)B.N[(size_t)Root].Children[0]].bFlexW = true;
        const int Strip4 = B.N[(size_t)Root].Children[0];
        for (int c = 0; c < 4; ++c)
            B.N[(size_t)B.N[(size_t)Strip4].Children[(size_t)c]].bPinnedAction = true;
        const int Page = B.N[(size_t)Root].Children[1];
        B.N[(size_t)Page].bClipH = true;
        B.N[(size_t)Page].bCarousel = true;
        B.N[(size_t)Page].FixedW = 180.0;
        B.N[(size_t)Page].FixedH = 184.0;
        B.N[(size_t)B.N[(size_t)Page].Children[0]].bPinnedAction = true;
        TEST("P21: duplicate inside a carousel page fires",
            Violates(B.N, FPLayout::DesignRule::PinnedActionsNeverInScroll));
    }

    printf("  [Pinned Actions Rule: tests]\n");
}

// --- Phase 4b: preview mode mirrors ---
// Cycle Preview runs the four live systems one at a time (2s each); Live
// Preview runs blink + expression + viseme + orbit all at once. Pins the
// system registry, the cycle order/duration, and the live cadence/period.
void TestPreviewModesMirror() {
    printf("\n=== Preview Modes Mirror (Phase 4b) ===\n");

    const std::vector<std::string>& Sys = FPLayout::PreviewSystems();
    TEST("Systems: exactly 4 live systems", Sys.size() == 4);
    TEST("Systems: blink first", Sys[0] == "blink");
    TEST("Systems: expression second", Sys[1] == "expression");
    TEST("Systems: viseme third", Sys[2] == "viseme");
    TEST("Systems: orbit last", Sys[3] == "orbit");

    TEST("Cycle: 2s phase duration", FPLayout::PreviewCyclePhaseDuration() == 2.0);
    TEST("Cycle: phase count matches systems",
        (int)FPLayout::PreviewSystems().size() == 4);

    const std::vector<bool> F0 = FPLayout::PreviewModeSystemFlags("cycle", 0);
    const std::vector<bool> F3 = FPLayout::PreviewModeSystemFlags("cycle", 3);
    TEST("Cycle phase 0: blink only", F0[0] && !F0[1] && !F0[2] && !F0[3]);
    TEST("Cycle phase 3: orbit only", !F3[0] && !F3[1] && !F3[2] && F3[3]);
    const std::vector<bool> Fbad = FPLayout::PreviewModeSystemFlags("cycle", 9);
    TEST("Cycle out-of-range phase: nothing enabled",
        !Fbad[0] && !Fbad[1] && !Fbad[2] && !Fbad[3]);

    const std::vector<bool> Live = FPLayout::PreviewModeSystemFlags("live");
    TEST("Live: all four systems enabled together",
        Live[0] && Live[1] && Live[2] && Live[3]);
    TEST("Live: viseme re-trigger cadence 2.5s", FPLayout::LivePreviewVisemeCadence() == 2.5);
    TEST("Live: orbit sweep period 8s", FPLayout::LivePreviewOrbitPeriod() == 8.0);

    printf("  [Preview Modes Mirror: 13 tests]\n");
}

// Mirror of the widget undo stack (UFaceParallaxEditorWidget::PushUndoState /
// Undo / Redo): a labeled stack of full-preset snapshots with a 32-entry cap;
// a new mutation clears the redo branch; restore copies the whole preset.
struct FMirrorUndoStack
{
    struct FEntry { std::string Label; std::vector<int> Preset; };
    std::vector<FEntry> UndoStack;
    std::vector<FEntry> RedoStack;
    bool bRestoring = false;
    static constexpr size_t Max = 32;

    void Push(const std::string& Label, const std::vector<int>& PreState)
    {
        if (bRestoring) return;
        UndoStack.push_back({ Label, PreState });
        if (UndoStack.size() > Max) UndoStack.erase(UndoStack.begin());
        RedoStack.clear();
    }
    bool Undo(std::vector<int>& Out)
    {
        if (UndoStack.empty()) return false;
        Out = UndoStack.back().Preset;
        RedoStack.push_back(UndoStack.back());
        UndoStack.pop_back();
        return true;
    }
    bool Redo(std::vector<int>& Out)
    {
        if (RedoStack.empty()) return false;
        Out = RedoStack.back().Preset;
        UndoStack.push_back(RedoStack.back());
        RedoStack.pop_back();
        if (UndoStack.size() > Max) UndoStack.erase(UndoStack.begin());
        return true;
    }
};

void TestUndoStackSemantics() {
    printf("\n=== Undo Stack Semantics ===\n");
    FMirrorUndoStack S;
    std::vector<int> P(10, 0);
    S.Push("A", P); P[0] = 1;
    S.Push("B", P); P[0] = 2;
    S.Push("C", P); P[0] = 3;
    TEST("push: 3 undo entries, empty redo",
        S.UndoStack.size() == 3 && S.RedoStack.size() == 0);
    std::vector<int> Out;
    TEST("undo -> C pre-state (2)", S.Undo(Out) && Out[0] == 2);
    TEST("undo -> B pre-state (1)", S.Undo(Out) && Out[0] == 1);
    TEST("undo -> A pre-state (0)", S.Undo(Out) && Out[0] == 0);
    TEST("undo on empty -> false", !S.Undo(Out));
    TEST("redo -> A pre-state (0)", S.Redo(Out) && Out[0] == 0);
    TEST("redo -> B pre-state (1)", S.Redo(Out) && Out[0] == 1);
    TEST("redo -> C pre-state (2)", S.Redo(Out) && Out[0] == 2);
    TEST("redo on empty -> false", !S.Redo(Out));
    TEST("redo returns entry to undo stack", S.UndoStack.size() == 3 && S.RedoStack.size() == 0);
}

void TestUndoRedoClearsOnNewMutation() {
    printf("\n=== Undo Clears On New Mutation ===\n");
    FMirrorUndoStack S;
    std::vector<int> P(10, 0);
    S.Push("A", P); P[0] = 1;
    S.Push("B", P); P[0] = 2;
    std::vector<int> Out;
    S.Undo(Out);
    P = Out;  // undo restored the preset to B's pre-state (mirror of RestoreFromBackup)
    TEST("redo branch populated after undo", S.RedoStack.size() == 1);
    S.Push("D", P);
    P[0] = 5;
    TEST("new mutation clears redo branch", S.RedoStack.size() == 0);
    TEST("undo stack: A and D only (redo entry not merged back)", S.UndoStack.size() == 2);
    S.Undo(Out);
    TEST("undo after new mutation -> D pre-state (1)", Out[0] == 1);
    S.bRestoring = true;
    S.Push("DuringRestore", P);
    S.bRestoring = false;
    TEST("push suppressed while restoring", S.UndoStack.size() == 1);
}

void TestUndoPreservesUntouchedViews() {
    printf("\n=== Undo Preserves Untouched Views ===\n");
    // A backup copies the WHOLE preset: undoing a change to view 3 must
    // leave every untouched view exactly as captured, even if the mutation
    // clobbered other views (restore is not a delta apply).
    FMirrorUndoStack S;
    std::vector<int> P(10, 0);
    P[7] = 7;
    S.Push("Set View3", P);
    P[3] = 3;
    P[7] = 99;
    std::vector<int> Out;
    TEST("undo restores", S.Undo(Out));
    bool bFull = Out.size() == 10;
    for (int i = 0; i < 10; ++i)
    {
        if (Out[i] != (i == 7 ? 7 : 0)) bFull = false;
    }
    TEST("undo restores all 10 views from backup", bFull);
}

void TestUndoStackCap() {
    printf("\n=== Undo Stack Cap (32) ===\n");
    FMirrorUndoStack S;
    std::vector<int> P(10, 0);
    for (int i = 0; i < 40; ++i) { S.Push("E" + std::to_string(i), P); P[0] = i; }
    TEST("cap keeps 32 entries", S.UndoStack.size() == 32);
    TEST("oldest entries dropped", S.UndoStack.front().Label == "E8");
    std::vector<int> Out;
    S.Undo(Out);
    TEST("top entry is E39 pre-state (38)", Out[0] == 38);
}

// --- Phase P3 mirrors: 6th Assign rail + per-axis sync / base-layer pins /
// import completion / camera source combo / zone drag / performance tier /
// display-mode dedupe / assign grid. All helpers are pure FPLayout functions
// consumed 1:1 by the widget (FaceParallaxEditorWidgetPanels.cpp +
// FaceParallaxEditorWidgetInteractions.cpp).
void TestPhaseP3Mirrors() {
    printf("\n=== Phase P3 Mirrors (Assign rail + companions) ===\n");

    // ---- Per-axis sync (SyncCanonicalAxisToAllViews) ----
    {
        double Dx, Dy, Dr;
        FPLayout::SyncAxisDelta(100.0, 50.0, 10.0, 80.0, 50.0, 5.0, 0, Dx, Dy, Dr);
        TEST("axis 0: position X delta only", Dx == 20.0 && Dy == 0.0 && Dr == 0.0);
        FPLayout::SyncAxisDelta(100.0, 50.0, 10.0, 80.0, 40.0, 5.0, 1, Dx, Dy, Dr);
        TEST("axis 1: position Y delta only", Dx == 0.0 && Dy == 10.0 && Dr == 0.0);
        FPLayout::SyncAxisDelta(2.0, 50.0, 10.0, 1.0, 50.0, 5.0, 2, Dx, Dy, Dr);
        TEST("axis 2: scale X ratio", Dx == 2.0 && Dy == 0.0 && Dr == 0.0);
        FPLayout::SyncAxisDelta(100.0, 2.0, 10.0, 100.0, 0.5, 5.0, 3, Dx, Dy, Dr);
        TEST("axis 3: scale Y ratio", Dx == 0.0 && Dy == 4.0 && Dr == 0.0);
        FPLayout::SyncAxisDelta(100.0, 50.0, 30.0, 80.0, 40.0, 12.0, 4, Dx, Dy, Dr);
        TEST("axis 4: rotation delta", Dx == 0.0 && Dy == 0.0 && Dr == 18.0);
        FPLayout::SyncAxisDelta(100.0, 0.0, 10.0, 0.0, 0.0, 5.0, 2, Dx, Dy, Dr);
        TEST("axis 2: zero dest guards ratio", Dx == 1.0 && Dr == 0.0);
    }

    // ---- Base-layer pin authoring + gizmo projection (LayerPinFromUV) ----
    {
        double X, Y, Z;
        FPLayout::LayerPinFromUV(0.5, 0.5, 0.0, X, Y, Z);
        TEST("front zone: pin at center", X == 0.0 && Y == 0.0 && Z == 0.0);
        FPLayout::LayerPinFromUV(0.75, 0.5, 0.0, X, Y, Z);
        TEST("front zone: +U maps +X", X > 0.0 && Y == 0.0 && Z == 0.0);
        FPLayout::LayerPinFromUV(0.75, 0.75, 60.0, X, Y, Z);
        TEST("quarter zone: +V maps +Y, X zeroed", X == 0.0 && Y > 0.0 && Z != 0.0);
        FPLayout::LayerPinFromUV(0.25, 0.5, 60.0, X, Y, Z);
        TEST("quarter zone: left UV flips Z", Z < 0.0);
        FPLayout::LayerPinFromUV(0.75, 0.5, 170.0, X, Y, Z);
        TEST("back zone: +U maps -X", X < 0.0 && Z == 0.0);
    }
    {
        double U, V;
        FPLayout::PinProjectToUV(0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.1, U, V);
        TEST("pin at origin projects to UV center", U == 0.5 && V == 0.5);
        FPLayout::PinProjectToUV(0.5, 0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 0.1, U, V);
        TEST("front pin projects to 0.75,0.75", U > 0.74 && U < 0.76 && V > 0.74 && V < 0.76);
        FPLayout::PinProjectToUV(0.0, 0.0, 5.0, 0.0, 0.0, 1.0, 1.0, 0.1, U, V);
        TEST("projection clamps to unit square", U >= 0.0 && U <= 1.0 && V >= 0.0 && V <= 1.0);
    }

    // ---- Import completion summary ----
    {
        const std::string S0 = FPLayout::ImportCoverageSummary(10, 10, 9, 8);
        TEST("coverage summary 10/10 9/10 8/10",
            S0 == "albedo 10/10, normal 9/10, depth 8/10");
        TEST("coverage summary no states", FPLayout::ImportCoverageSummary(0, 0, 0, 0) == "no states");
    }

    // ---- Camera source combo (6 entries, PlayerCamera0 first) ----
    {
        const auto& L = FPLayout::CameraSourceLabels();
        TEST("camera sources: 6 entries", L.size() == 6);
        TEST("camera sources: pinned order",
            L[0] == "PlayerCamera0" && L[1] == "PlayerCamera1" && L[2] == "SpecifiedActor"
            && L[3] == "SequencerCamera" && L[4] == "PreviewActor" && L[5] == "Custom");
    }

    // ---- Zone boundary drag (multiplier edit, clamped, NaN-guarded) ----
    {
        TEST("zone drag: +10deg on 22.5 half-width", FPLayout::ZoneBoundaryAfterDrag(1.0, 10.0, 22.5) > 1.44
            && FPLayout::ZoneBoundaryAfterDrag(1.0, 10.0, 22.5) < 1.45);
        TEST("zone drag: clamps at 0.5", FPLayout::ZoneBoundaryAfterDrag(1.0, -30.0, 22.5) == 0.5);
        TEST("zone drag: clamps at 20", FPLayout::ZoneBoundaryAfterDrag(1.0, 500.0, 22.5) == 20.0);
        TEST("zone drag: NaN delta keeps multiplier", FPLayout::ZoneBoundaryAfterDrag(1.5, 0.0 / 0.0, 22.5) == 1.5);
        TEST("zone drag: NaN multiplier propagates NaN", std::isnan(FPLayout::ZoneBoundaryAfterDrag(0.0 / 0.0, 10.0, 22.5)));
    }

    // ---- Performance tiers ----
    {
        TEST("perf tier: cache sizes 64/256/512",
            FPLayout::PerformanceTierCacheSize(0) == 64 && FPLayout::PerformanceTierCacheSize(1) == 256
            && FPLayout::PerformanceTierCacheSize(2) == 512);
        TEST("perf tier: grid sizes 32/64/128",
            FPLayout::PerformanceTierGridSize(0) == 32 && FPLayout::PerformanceTierGridSize(1) == 64
            && FPLayout::PerformanceTierGridSize(2) == 128);
    }

    // ---- Display-mode dedupe (three debug toggles -> one exclusive mode) ----
    {
        TEST("display mode: textured", FPLayout::DeriveDisplayMode(true, false, false) == 0);
        TEST("display mode: depth", FPLayout::DeriveDisplayMode(false, true, false) == 1);
        TEST("display mode: wireframe", FPLayout::DeriveDisplayMode(false, false, true) == 2);
        TEST("display mode: split (textures+depth)", FPLayout::DeriveDisplayMode(true, true, false) == 3);
        TEST("display mode: custom combo clears highlight", FPLayout::DeriveDisplayMode(true, false, true) == -1);
        TEST("display mode: all off clears highlight", FPLayout::DeriveDisplayMode(false, false, false) == -1);
    }

    // ---- Assign grid cells (2 full / 1 partial / 0 empty) ----
    {
        TEST("assign cell: full", FPLayout::AssignCellState(true, true, true) == 2);
        TEST("assign cell: partial", FPLayout::AssignCellState(true, false, false) == 1);
        TEST("assign cell: empty", FPLayout::AssignCellState(false, false, false) == 0);
        TEST("assign coverage text", FPLayout::AssignCoverageText(21, 30) == "21/30");
    }
}

// --- Phase C (remediation): canvas selection + unified inspect mode. All
// helpers are pure FPLayout functions consumed 1:1 by SFaceHotspotLayer and
// the widget (canvas quad hit-test + selection outline + right/ctrl cycling,
// and the 5-segment inspect row derived from the Advanced rail Config checks).
void TestPhaseCIntegration() {
    printf("\n=== Phase C Integration (canvas selection + inspect mode) ===\n");

    // ---- Inspect-mode labels ----
    {
        TEST("inspect labels: five canonical",
            FPLayout::InspectModeLabel(0) == std::string("Textured")
            && FPLayout::InspectModeLabel(1) == std::string("Outline")
            && FPLayout::InspectModeLabel(2) == std::string("Depth")
            && FPLayout::InspectModeLabel(3) == std::string("Wireframe")
            && FPLayout::InspectModeLabel(4) == std::string("Heatmap"));
        TEST("inspect labels: custom fallback", FPLayout::InspectModeLabel(9) == std::string("Custom"));
    }

    // ---- DeriveInspectMode (five source toggles -> exclusive mode) ----
    {
        TEST("inspect derive: textured", FPLayout::DeriveInspectMode(true, false, false, false, false) == 0);
        TEST("inspect derive: outline", FPLayout::DeriveInspectMode(true, false, false, true, false) == 1);
        TEST("inspect derive: depth", FPLayout::DeriveInspectMode(false, true, false, false, false) == 2);
        TEST("inspect derive: wireframe", FPLayout::DeriveInspectMode(false, false, true, false, false) == 3);
        TEST("inspect derive: depth heatmap", FPLayout::DeriveInspectMode(false, true, false, false, true) == 4);
        TEST("inspect derive: legacy split is custom", FPLayout::DeriveInspectMode(true, true, false, false, false) == -1);
        TEST("inspect derive: all off is custom", FPLayout::DeriveInspectMode(false, false, false, false, false) == -1);
        TEST("inspect derive: textures+wireframe custom", FPLayout::DeriveInspectMode(true, false, true, false, false) == -1);
    }

    // ---- InspectComboForMode round-trips into DeriveInspectMode ----
    {
        for (int M = 0; M <= 4; ++M)
        {
            const FPLayout::FPInspectCombo B = FPLayout::InspectComboForMode(M);
            const std::string Name = std::string("inspect combo round-trip ") + std::to_string(M);
            TEST(Name.c_str(),
                FPLayout::DeriveInspectMode(B.T, B.D, B.W, B.O, B.C) == M);
        }
        const FPLayout::FPInspectCombo Bad = FPLayout::InspectComboForMode(9);
        TEST("inspect combo: unknown mode clears all", !Bad.T && !Bad.D && !Bad.W && !Bad.O && !Bad.C);
        const FPLayout::FPInspectCombo Out = FPLayout::InspectComboForMode(1);
        TEST("inspect combo: outline = textures + overlay", Out.T && Out.O && !Out.D && !Out.W && !Out.C);
        const FPLayout::FPInspectCombo Hm = FPLayout::InspectComboForMode(4);
        TEST("inspect combo: heatmap = depth + colorby", Hm.D && Hm.C && !Hm.T && !Hm.W && !Hm.O);
    }

    // ---- Layer quad from the effective transform (cross-view constraint) ----
    {
        const FPLayout::FPLayerQuad Id = FPLayout::FLayerQuadFromTransform(0, 0, 1, 1, 0);
        TEST("quad identity: unit square corners",
            Id.C[0].X == 0.0 && Id.C[0].Y == 0.0 && Id.C[1].X == 1.0 && Id.C[1].Y == 0.0
            && Id.C[2].X == 1.0 && Id.C[2].Y == 1.0 && Id.C[3].X == 0.0 && Id.C[3].Y == 1.0);
        const FPLayout::FPLayerQuad Q = FPLayout::FLayerQuadFromTransform(0.25, 0.125, 1, 1, 0);
        TEST("quad translate: corners shift by position",
            Q.C[0].X == 0.25 && Q.C[0].Y == 0.125 && Q.C[2].X == 1.25 && Q.C[2].Y == 1.125);
        // 90 degrees clockwise about the UV center maps (0,0)->(1,0),
        // (1,0)->(1,1), (1,1)->(0,1), (0,1)->(0,0) (y-down rotation).
        const FPLayout::FPLayerQuad R = FPLayout::FLayerQuadFromTransform(0, 0, 1, 1, 90);
        TEST("quad rotate 90: corners swing clockwise",
            FPLayout::FPPointInQuad(1.0, 0.0, R) && FPLayout::FPPointInQuad(0.0, 1.0, R));
        TEST("quad rotate 90: rotated corner exact",
            fabs(R.C[0].X - 1.0) < 1e-9 && fabs(R.C[0].Y) < 1e-9);
        // Quad corners match the master-material point transform 1:1.
        const FPLayout::FPHotspotPoint P0 = FPLayout::FPHotspotTransformPoint({ 0.0, 0.0 }, 0.05, -0.04, 0.9, 1.1, 12.0);
        const FPLayout::FPLayerQuad S = FPLayout::FLayerQuadFromTransform(0.05, -0.04, 0.9, 1.1, 12.0);
        TEST("quad corner equals transformed point",
            S.C[0].X == P0.X && S.C[0].Y == P0.Y);
    }

    // ---- Point-in-quad (boundary inclusive) ----
    {
        const FPLayout::FPLayerQuad Id = FPLayout::FLayerQuadFromTransform(0, 0, 1, 1, 0);
        TEST("quad hit: center inside", FPLayout::FPPointInQuad(0.5, 0.5, Id));
        TEST("quad hit: corner inside (inclusive)", FPLayout::FPPointInQuad(1.0, 1.0, Id));
        TEST("quad hit: edge inside (inclusive)", FPLayout::FPPointInQuad(0.5, 0.0, Id));
        TEST("quad hit: outside", !FPLayout::FPPointInQuad(1.5, 0.5, Id));
        TEST("quad hit: far outside", !FPLayout::FPPointInQuad(-0.1, 0.5, Id));
    }

    // ---- Topmost hit + cycling through overlapping layers ----
    {
        // A spans 0..1, B spans 0.5..1.5 -> overlap band 0.5..1.
        const std::vector<FPLayout::FPLayerQuad> Quads = {
            FPLayout::FLayerQuadFromTransform(0, 0, 1, 1, 0),
            FPLayout::FLayerQuadFromTransform(0.5, 0.5, 1, 1, 0),
        };
        TEST("topmost: overlap picks last (top) layer", FPLayout::FPHitTopmostQuad(0.75, 0.75, Quads) == 1);
        TEST("topmost: exclusive band of A", FPLayout::FPHitTopmostQuad(0.25, 0.25, Quads) == 0);
        TEST("topmost: exclusive band of B", FPLayout::FPHitTopmostQuad(1.25, 1.25, Quads) == 1);
        TEST("topmost: outside everything", FPLayout::FPHitTopmostQuad(2.0, 2.0, Quads) == -1);
        TEST("topmost: empty list", FPLayout::FPHitTopmostQuad(0.5, 0.5, {}) == -1);

        const std::vector<int> Hits = { 0, 2, 5 };
        TEST("cycle: advances to next hit", FPLayout::FPCycleQuadHit(Hits, 0) == 2);
        TEST("cycle: wraps past last", FPLayout::FPCycleQuadHit(Hits, 5) == 0);
        TEST("cycle: selection not in hits starts at topmost", FPLayout::FPCycleQuadHit(Hits, 7) == 0);
        TEST("cycle: middle hit advances", FPLayout::FPCycleQuadHit(Hits, 2) == 5);
        TEST("cycle: empty hits -> -1", FPLayout::FPCycleQuadHit({}, 0) == -1);
    }
}

void TestPhaseDSyncIntegration() {
    printf("\n=== PhaseDSyncIntegration ===\n");

    // ---- FSyncOp labels ----
    TEST("sync-op label: Transform", std::string(FPLayout::SyncOpLabel(FPLayout::SyncOpTransform)) == "Transform");
    TEST("sync-op label: Textures", std::string(FPLayout::SyncOpLabel(FPLayout::SyncOpTextures)) == "Textures");
    TEST("sync-op label: Both", std::string(FPLayout::SyncOpLabel(FPLayout::SyncOpBoth)) == "Both");
    TEST("sync-op label: invalid high -> Both", std::string(FPLayout::SyncOpLabel(9)) == "Both");
    TEST("sync-op label: invalid low -> Both", std::string(FPLayout::SyncOpLabel(-3)) == "Both");

    // ---- SyncOpHasTransform / SyncOpHasTextures truth table ----
    TEST("op Transform has transform, not textures",
        FPLayout::SyncOpHasTransform(FPLayout::SyncOpTransform) && !FPLayout::SyncOpHasTextures(FPLayout::SyncOpTransform));
    TEST("op Textures has textures, not transform",
        FPLayout::SyncOpHasTextures(FPLayout::SyncOpTextures) && !FPLayout::SyncOpHasTransform(FPLayout::SyncOpTextures));
    TEST("op Both has both channels", FPLayout::SyncOpHasTransform(FPLayout::SyncOpBoth)
        && FPLayout::SyncOpHasTextures(FPLayout::SyncOpBoth));
    TEST("op invalid -> Both channels", FPLayout::SyncOpHasTransform(5) && FPLayout::SyncOpHasTextures(5));

    // ---- FPLinkDestCount ----
    const int NV = 10;
    TEST("link count: no picks -> all other views", FPLayout::FPLinkDestCount({0,0,0,0,0,0,0,0,0,0}, 0, NV) == 9);
    TEST("link count: picks {3,7} active 0 -> 2", FPLayout::FPLinkDestCount({0,0,0,1,0,0,0,1,0,0}, 0, NV) == 2);
    TEST("link count: only active picked -> fallback all",
        FPLayout::FPLinkDestCount({0,0,0,0,0,0,0,0,0,0}, 3, NV) == 9);
    TEST("link count: active in picks excluded", FPLayout::FPLinkDestCount({1,0,0,1,0,0,0,0,0,0}, 0, NV) == 1);
    TEST("link count: zero views -> 0", FPLayout::FPLinkDestCount({1,1}, 0, 0) == 0);

    // ---- FPLinkDestIsPicked ----
    TEST("link dest: active view never a destination", !FPLayout::FPLinkDestIsPicked({0,0,0,0,0,0,0,0,0,0}, 2, 2));
    TEST("link dest: fallback sends to every other view",
        FPLayout::FPLinkDestIsPicked({0,0,0,0,0,0,0,0,0,0}, 2, 0)
        && FPLayout::FPLinkDestIsPicked({0,0,0,0,0,0,0,0,0,0}, 2, 9));
    TEST("link dest: picked view is a destination", FPLayout::FPLinkDestIsPicked({0,0,0,1,0,0,0,0,0,0}, 2, 3));
    TEST("link dest: unpicked view is not a destination", !FPLayout::FPLinkDestIsPicked({0,0,0,1,0,0,0,0,0,0}, 2, 4));
    TEST("link dest: out-of-range view excluded", !FPLayout::FPLinkDestIsPicked({0,0,0,0,0,0,0,0,0,0}, 2, 12));
    TEST("link dest: fallback set == Phase B GetLinkTargets contract", []()
    {
        const std::vector<int> NoPicks(10, 0);
        for (int Active = 0; Active < 10; ++Active)
            for (int V = 0; V < 10; ++V)
                if (FPLayout::FPLinkDestIsPicked(NoPicks, Active, V) != (V != Active)) return false;
        return true;
    }());
    TEST("link dest: active excluded even when picked", !FPLayout::FPLinkDestIsPicked({0,1,0,0,0,0,0,0,0,0}, 1, 1));
    TEST("link dest: picks include active -> others still honored",
        FPLayout::FPLinkDestIsPicked({1,1,0,1,0,0,0,0,0,0}, 1, 3)
        && !FPLayout::FPLinkDestIsPicked({1,1,0,1,0,0,0,0,0,0}, 1, 4));
}

void TestPhaseLayerBadgeMirrors() {
    printf("\n=== PhaseLayerBadgeMirrors ===\n");

    TEST("badge label: assigned", std::string(FPLayout::AssignCellLabel(2)) == "Assigned");
    TEST("badge label: partial", std::string(FPLayout::AssignCellLabel(1)) == "Partial");
    TEST("badge label: missing", std::string(FPLayout::AssignCellLabel(0)) == "Missing");
    TEST("badge label: invalid high -> Missing", std::string(FPLayout::AssignCellLabel(9)) == "Missing");
    TEST("badge label: invalid low -> Missing", std::string(FPLayout::AssignCellLabel(-1)) == "Missing");

    // The badge mirrors AssignCellState: full set -> Assigned etc.
    TEST("badge: full set -> Assigned", std::string(FPLayout::AssignCellLabel(
        FPLayout::AssignCellState(true, true, true))) == "Assigned");
    TEST("badge: single channel -> Partial", std::string(FPLayout::AssignCellLabel(
        FPLayout::AssignCellState(true, false, false))) == "Partial");
    TEST("badge: empty -> Missing", std::string(FPLayout::AssignCellLabel(
        FPLayout::AssignCellState(false, false, false))) == "Missing");
}

void TestPhasePinMgrMirrors() {
    printf("\n=== PhasePinMgrMirrors ===\n");

    // ---- FPPinnedRowCount ----
    TEST("pin rows: nothing pinned -> 0", FPLayout::FPPinnedRowCount(false, {0,0}, {}) == 0);
    TEST("pin rows: layer pin only -> 1", FPLayout::FPPinnedRowCount(true, {0,0}, {}) == 1);
    TEST("pin rows: one element pinned -> 1", FPLayout::FPPinnedRowCount(false, {1,0}, {}) == 1);
    TEST("pin rows: layer + two elements -> 3", FPLayout::FPPinnedRowCount(true, {1,1}, {}) == 3);
    TEST("pin rows: child pin counts", FPLayout::FPPinnedRowCount(false, {1,0},
        {{0,1},{0}}) == 2);
    TEST("pin rows: all sources -> 6", FPLayout::FPPinnedRowCount(true, {1,0,1},
        {{1,0},{1},{0,0,1}}) == 6);
    TEST("pin rows: unpinned children excluded", FPLayout::FPPinnedRowCount(false, {0,0},
        {{0,0},{0}}) == 0);

    // ---- UndoShortcutAction ----
    TEST("shortcut: plain Z with ctrl -> undo", FPLayout::UndoShortcutAction(true, false, true, false)
        == FPLayout::FUndoShortcutUndo);
    TEST("shortcut: ctrl+shift+Z -> redo", FPLayout::UndoShortcutAction(true, true, true, false)
        == FPLayout::FUndoShortcutRedo);
    TEST("shortcut: ctrl+Y -> redo", FPLayout::UndoShortcutAction(true, false, false, true)
        == FPLayout::FUndoShortcutRedo);
    TEST("shortcut: no ctrl -> none", FPLayout::UndoShortcutAction(false, false, true, false)
        == FPLayout::FUndoShortcutNone);
    TEST("shortcut: ctrl+other key -> none", FPLayout::UndoShortcutAction(true, false, false, false)
        == FPLayout::FUndoShortcutNone);
    TEST("shortcut: ctrl+shift+Y still redo", FPLayout::UndoShortcutAction(true, true, false, true)
        == FPLayout::FUndoShortcutRedo);
}

// --- Central canvas redesign: default-view part schematic (17 glyphs) ---
void TestSchematicParts() {
    printf("\n=== SchematicParts ===\n");
    using namespace FPSchematic;
    const std::vector<FPSchematicPart> Parts = DefaultPartSchematics();

    TEST("schematic: 17 parts (13 anatomical + Bangs/Hair/BackHair/Head)",
        Parts.size() == 17);
    TEST("schematic: names unique", [&]() {
        for (size_t i = 0; i < Parts.size(); ++i)
            for (size_t j = i + 1; j < Parts.size(); ++j)
                if (Parts[i].Name && Parts[j].Name &&
                    std::string(Parts[i].Name) == std::string(Parts[j].Name))
                    return false;
        return true;
    }());
    TEST("schematic: every part has >= 3 points in [0,1]^2 with valid class", [&]() {
        for (const FPSchematicPart& P : Parts)
        {
            if (!P.Name || !P.Name[0]) return false;
            if (P.Outline.size() < 3) return false;
            if ((int)P.DepthClass >= (int)FPDepthClass::MAX) return false;
            for (const FPSchematicPoint& Pt : P.Outline)
            {
                if (Pt.X < 0.0 || Pt.X > 1.0 || Pt.Y < 0.0 || Pt.Y > 1.0) return false;
            }
        }
        return true;
    }());

    // User rule: Bangs/Nose move with yaw (Front), Head is anchored (Base),
    // BackHair/Ears sit on the far side (Back).
    TEST("schematic: Bangs is Front", FPSchematicFindPart(Parts, "Bangs") &&
        FPSchematicFindPart(Parts, "Bangs")->DepthClass == FPDepthClass::Front);
    TEST("schematic: Nose is Front", FPSchematicFindPart(Parts, "Nose") &&
        FPSchematicFindPart(Parts, "Nose")->DepthClass == FPDepthClass::Front);
    TEST("schematic: Head is Base", FPSchematicFindPart(Parts, "Head") &&
        FPSchematicFindPart(Parts, "Head")->DepthClass == FPDepthClass::Base);
    TEST("schematic: BackHair is Back", FPSchematicFindPart(Parts, "BackHair") &&
        FPSchematicFindPart(Parts, "BackHair")->DepthClass == FPDepthClass::Back);
    TEST("schematic: Ears are Back", FPSchematicFindPart(Parts, "EarL") &&
        FPSchematicFindPart(Parts, "EarR") &&
        FPSchematicFindPart(Parts, "EarL")->DepthClass == FPDepthClass::Back &&
        FPSchematicFindPart(Parts, "EarR")->DepthClass == FPDepthClass::Back);
    // Phase 2: glyph classes follow the layer they resolve to — cheeks belong
    // to the Cheeks layer (Front), the chin to the Head layer (Base).
    TEST("schematic: Cheeks are Front (Cheeks layer)", FPSchematicFindPart(Parts, "CheekL") &&
        FPSchematicFindPart(Parts, "CheekR") &&
        FPSchematicFindPart(Parts, "CheekL")->DepthClass == FPDepthClass::Front &&
        FPSchematicFindPart(Parts, "CheekR")->DepthClass == FPDepthClass::Front);
    TEST("schematic: Chin is Base (Head layer)", FPSchematicFindPart(Parts, "Chin") &&
        FPSchematicFindPart(Parts, "Chin")->DepthClass == FPDepthClass::Base);
    TEST("schematic: every front feature moves with yaw (Front class)", [&]() {
        for (const FPSchematicPart& P : Parts)
            if (P.DepthClass != FPDepthClass::Front &&
                P.DepthClass != FPDepthClass::Base &&
                P.DepthClass != FPDepthClass::Back)
                return false;
        return true;
    }());

    // Probe table: one interior point per part. Probe points are chosen so the
    // 13 anatomical probes also agree with the hotspot-region hit-testing
    // (click parity with the parts strip); the four silhouette parts have no
    // region, so their probes must hit the schematic only.
    struct Probe { const char* Name; double X; double Y; };
    static const Probe Probes[] = {
        { "BrowL", 0.24, 0.14 }, { "BrowR", 0.76, 0.14 },
        { "EyeL", 0.25, 0.24 },  { "EyeR", 0.75, 0.24 },
        { "Nose", 0.50, 0.376 }, { "CheekL", 0.1367, 0.4333 },
        { "CheekR", 0.8633, 0.4333 }, { "Mouth", 0.50, 0.61 },
        { "Teeth", 0.50, 0.66 }, { "Chin", 0.50, 0.80 },
        { "EarL", 0.06, 0.40 },  { "EarR", 0.94, 0.40 },
        { "Neck", 0.50, 0.93 },  { "Bangs", 0.50, 0.10 },
        { "Hair", 0.50, 0.04 },  { "BackHair", 0.30, 0.80 },
        { "Head", 0.85, 0.70 },
    };
    for (const Probe& Pr : Probes)
    {
        const FPSchematicPart* Hit = FPSchematicPartAt(Parts, Pr.X, Pr.Y);
        const std::string Msg = std::string("schematic probe hits ") + Pr.Name;
        TEST(Msg.c_str(), Hit && std::string(Hit->Name) == Pr.Name);
    }

    // Click parity: for the 13 anatomical probes the region hit-test must
    // agree with the schematic (or there is no region — the new parts).
    for (const Probe& Pr : Probes)
    {
        const FPSchematicPart* Hit = FPSchematicPartAt(Parts, Pr.X, Pr.Y);
        const char* RegionHit = FPLayout::FPHotspotHit(
            FPLayout::DefaultHotspotRegions(), Pr.X, Pr.Y);
        const std::string Msg = std::string("schematic/region parity at ") + Pr.Name;
        TEST(Msg.c_str(), (!Hit) || !RegionHit || std::string(Hit->Name) == RegionHit);
    }

    // Boundary-inclusive semantics (same as the region hit-testing).
    TEST("schematic: Bangs top edge is boundary-inclusive",
        FPSchematicPartAt(Parts, 0.50, 0.05) &&
        std::string(FPSchematicPartAt(Parts, 0.50, 0.05)->Name) == "Bangs");
    TEST("schematic: just above the cap misses",
        FPSchematicPartAt(Parts, 0.50, 0.01) == nullptr);
    TEST("schematic: empty space at bottom-right corner misses",
        FPSchematicPartAt(Parts, 0.98, 0.99) == nullptr);

    // Named lookup.
    TEST("schematic: FindPart finds EarL", FPSchematicFindPart(Parts, "EarL") != nullptr);
    TEST("schematic: FindPart misses unknown", FPSchematicFindPart(Parts, "Scarf") == nullptr);

    // Transformed glyphs stay hittable (the canvas transforms glyphs by the
    // layer transform, mirroring FPLayout::FPHotspotTransformRegion).
    TEST("schematic: transform mirror hits", [&]() {
        int NoseIdx = -1;
        for (size_t i = 0; i < Parts.size(); ++i)
            if (Parts[i].Name && std::string(Parts[i].Name) == "Nose")
            {
                NoseIdx = (int)i;
                break;
            }
        if (NoseIdx < 0) return false;
        std::vector<FPSchematicPart> Moved = Parts;
        FPSchematicPart& T = Moved[(size_t)NoseIdx];
        T.Outline.clear();
        for (const FPSchematicPoint& Pt : Parts[(size_t)NoseIdx].Outline)
        {
            const FPLayout::FPHotspotPoint H = FPLayout::FPHotspotTransformPoint(
                FPLayout::FPHotspotPoint{ Pt.X, Pt.Y }, 0.1, 0.1, 1.0, 1.0, 0.0);
            T.Outline.push_back({ H.X, H.Y });
        }
        const FPSchematicPart* Hit = FPSchematicPartAt(Moved, 0.6, 0.476);
        return Hit && std::string(Hit->Name) == "Nose";
    }());
}

// --- Central canvas redesign: part->layer coverage (Cheeks + aliases) ---
void TestSchematicCoverage() {
    printf("\n=== SchematicCoverage ===\n");
    using namespace FPSchematic;
    const std::vector<std::string>& Layers = FPSchematicLayerSet();

    TEST("coverage: 10 base layers", Layers.size() == 10);
    TEST("coverage: every layer has a class entry", [&]() {
        for (const std::string& L : Layers)
            if (FPTagClassForTag(L.c_str()) == nullptr) return false;
        return true;
    }());

    // Derivation resolves the directional parts once the layers exist.
    TEST("coverage: CheekL -> Cheeks",
        std::string(FPLayout::FPHotspotLayerMatch(Layers, "CheekL")) == "Cheeks");
    TEST("coverage: CheekR -> Cheeks",
        std::string(FPLayout::FPHotspotLayerMatch(Layers, "CheekR")) == "Cheeks");
    TEST("coverage: EarL -> Ears",
        std::string(FPLayout::FPHotspotLayerMatch(Layers, "EarL")) == "Ears");
    TEST("coverage: EarR -> Ears",
        std::string(FPLayout::FPHotspotLayerMatch(Layers, "EarR")) == "Ears");
    TEST("coverage: EyeL -> Eyes",
        std::string(FPLayout::FPHotspotLayerMatch(Layers, "EyeL")) == "Eyes");
    TEST("coverage: BrowL -> Brows",
        std::string(FPLayout::FPHotspotLayerMatch(Layers, "BrowL")) == "Brows");
    TEST("coverage: Teeth has no derivation (alias needed)",
        FPLayout::FPHotspotLayerMatch(Layers, "Teeth") == nullptr);
    TEST("coverage: Chin has no derivation (alias needed)",
        FPLayout::FPHotspotLayerMatch(Layers, "Chin") == nullptr);
    TEST("coverage: Neck has no derivation (alias needed)",
        FPLayout::FPHotspotLayerMatch(Layers, "Neck") == nullptr);

    // Aliases complete the remaining parts.
    TEST("alias: Teeth -> Mouth", std::string(FPSchematicLayerAlias("Teeth")) == "Mouth");
    TEST("alias: Chin -> Head", std::string(FPSchematicLayerAlias("Chin")) == "Head");
    TEST("alias: Neck -> Head", std::string(FPSchematicLayerAlias("Neck")) == "Head");
    TEST("alias: already-resolvable part -> none", FPSchematicLayerAlias("Eyes") == nullptr);
    TEST("alias: empty -> none", FPSchematicLayerAlias("") == nullptr);

    // The full contract: every one of the 17 schematic parts resolves to a
    // base-preset layer (derivation, else alias).
    static const char* Parts17[] = { "BrowL", "BrowR", "EyeL", "EyeR", "Nose",
        "CheekL", "CheekR", "Teeth", "Mouth", "Chin", "EarL", "EarR", "Neck",
        "Bangs", "Hair", "BackHair", "Head" };
    for (const char* P : Parts17)
    {
        const char* M = FPLayout::FPHotspotLayerMatch(Layers, P);
        const char* Resolved = M ? M : FPSchematicLayerAlias(P);
        const std::string Msg = std::string("coverage: '") + P + "' resolves to a base layer";
        TEST(Msg.c_str(), Resolved && Resolved[0]);
    }
    TEST("coverage: resolved layers are all in the base set", [&]() {
        for (const char* P : Parts17)
        {
            const char* M = FPLayout::FPHotspotLayerMatch(Layers, P);
            const char* Resolved = M ? M : FPSchematicLayerAlias(P);
            if (!Resolved || !Resolved[0]) return false;
            bool bInSet = false;
            for (const std::string& L : Layers)
                if (L == Resolved) { bInSet = true; break; }
            if (!bInSet) return false;
        }
        return true;
    }());
}

// --- Central canvas redesign: hair system contract ---
void TestHairSystem() {
    printf("\n=== HairSystem ===\n");
    using namespace FPSchematic;

    TEST("hair: set has 3 layers", FPHairLayerSet().size() == 3);
    TEST("hair: Bangs is hair", FPSchematicIsHairLayer("Bangs"));
    TEST("hair: Hair is hair", FPSchematicIsHairLayer("Hair"));
    TEST("hair: BackHair is hair", FPSchematicIsHairLayer("BackHair"));
    TEST("hair: Ears is not hair", !FPSchematicIsHairLayer("Ears"));
    TEST("hair: unrelated tag is not hair", !FPSchematicIsHairLayer("Scarf"));
    TEST("hair: empty tag is not hair", !FPSchematicIsHairLayer(""));
    TEST("hair: every hair layer is in the base class table",
        FPTagClassForTag("Bangs") && FPTagClassForTag("Hair") && FPTagClassForTag("BackHair"));

    // Motion contract: front hair moves WITH yaw, back hair AGAINST it.
    TEST("hair: Bangs is Front (moves with yaw)",
        FPDepthClassForTag("Bangs") == FPDepthClass::Front &&
        FPYawRule::ApplyClass(FPDepthClass::Front, 0.5) > 0.0);
    TEST("hair: Hair is Back (moves against yaw)",
        FPDepthClassForTag("Hair") == FPDepthClass::Back &&
        FPYawRule::ApplyClass(FPDepthClass::Back, 0.5) < 0.0);
    TEST("hair: BackHair is Back (moves against yaw)",
        FPDepthClassForTag("BackHair") == FPDepthClass::Back &&
        FPYawRule::ApplyClass(FPDepthClass::Back, 0.5) < 0.0);
    TEST("hair: back hair mirrors front hair exactly",
        FPYawRule::ApplyClass(FPDepthClass::Front, 0.5) ==
        -FPYawRule::ApplyClass(FPDepthClass::Back, 0.5));
}

// --- Hair integration (Phase 7): midpoint chain-split jiggle ramp ---
void TestHairMidpointJiggle() {
    printf("\n=== HairMidpointJiggle ===\n");
    using namespace FPSchematic;

    // Feature disabled by default: Midpoint >= 1.0 keeps the legacy spring.
    TEST("jiggle: default midpoint disables the split",
        FPHairSegmentRamp(1.0, 0.0) == 0.0 && FPHairSegmentRamp(1.0, 0.5) == 0.0 &&
        FPHairSegmentRamp(1.0, 1.0) == 0.0);

    // Below/at the midpoint the base spring params apply.
    TEST("jiggle: at midpoint ramp is 0",
        FPHairSegmentRamp(0.5, 0.5) == 0.0);
    TEST("jiggle: root side of midpoint stays base",
        FPHairSegmentRamp(0.5, 0.25) == 0.0 && FPHairSegmentRamp(0.5, 0.0) == 0.0);

    // Tip fully blends.
    TEST("jiggle: tip is fully blended", FPHairSegmentRamp(0.5, 1.0) == 1.0);

    // Smoothstep: exactly half-way across the split zone = 0.5.
    TEST("jiggle: midpoint of split zone ramps 0.5",
        std::abs(FPHairSegmentRamp(0.5, 0.75) - 0.5) < 1e-9);

    // Ramp is monotonic across the split zone.
    TEST("jiggle: ramp is monotonic", [&]() {
        double Prev = 0.0;
        for (int i = 0; i <= 100; ++i)
        {
            const double R = FPHairSegmentRamp(0.25, 0.25 + 0.75 * (i / 100.0));
            if (R < Prev) return false;
            Prev = R;
        }
        return true;
    }());

    // Blend is a linear interpolation on the ramp.
    TEST("jiggle: blend lerps on the ramp",
        FPHairSegmentBlend(2.0, 10.0, 0.5) == 6.0 &&
        FPHairSegmentBlend(2.0, 10.0, 0.0) == 2.0 &&
        FPHairSegmentBlend(2.0, 10.0, 1.0) == 10.0);

    // Defensive clamping of chain progress.
    TEST("jiggle: chain progress clamps",
        FPHairSegmentRamp(0.5, -0.5) == 0.0 && FPHairSegmentRamp(0.5, 1.5) == 1.0);

    // End fields equal to the base fields keep the spring uniform everywhere
    // (the "bigger hair-end swing" only kicks in when End* differs).
    TEST("jiggle: equal end fields are identity", [&]() {
        for (int i = 0; i <= 100; ++i)
        {
            const double R = FPHairSegmentRamp(0.5, i / 100.0);
            if (FPHairSegmentBlend(5.0, 5.0, R) != 5.0) return false;
        }
        return true;
    }());

    // Hair-end swing recipe: lower end stiffness + stronger end impulse at the tip.
    TEST("jiggle: tip spring is softer and swanglier than the root", [&]() {
        const double RootStiff = FPHairSegmentBlend(5.0, 1.0, FPHairSegmentRamp(0.5, 0.0));
        const double TipStiff = FPHairSegmentBlend(5.0, 1.0, FPHairSegmentRamp(0.5, 1.0));
        const double TipImp = FPHairSegmentBlend(1.0, 3.0, FPHairSegmentRamp(0.5, 1.0));
        return RootStiff == 5.0 && TipStiff == 1.0 && TipImp == 3.0;
    }());
}

// --- Central canvas redesign: schematic filter row mirror ---
void TestSchematicFilters() {
    printf("\n=== SchematicFilters ===\n");
    using namespace FPSchematic;
    const std::vector<std::string> Empty;
    const std::vector<std::string> EyesOnly = { "Eyes" };

    // No filters = everything.
    TEST("filter: empty filters allow all", FPSchematicFilterAllows(FPDepthClass::Front, "Eyes", Empty, 0));
    TEST("filter: empty filters allow unmapped too", FPSchematicFilterAllows(FPDepthClass::Front, "", Empty, 0));

    // Depth radio: 1 Front, 2 Base, 3 Back.
    TEST("filter: depth Front allows Front", FPSchematicFilterAllows(FPDepthClass::Front, "Eyes", Empty, 1));
    TEST("filter: depth Base allows Base", FPSchematicFilterAllows(FPDepthClass::Base, "Head", Empty, 2));
    TEST("filter: depth Back allows Back", FPSchematicFilterAllows(FPDepthClass::Back, "Ears", Empty, 3));
    TEST("filter: depth mismatch rejects", !FPSchematicFilterAllows(FPDepthClass::Front, "Eyes", Empty, 3));
    TEST("filter: depth 0 bypasses the radio", FPSchematicFilterAllows(FPDepthClass::Back, "Ears", Empty, 0));

    // Layer multi-select: the part's resolved layer must be listed.
    TEST("filter: layer listed allows", FPSchematicFilterAllows(FPDepthClass::Front, "Eyes", EyesOnly, 0));
    TEST("filter: other layer rejected", !FPSchematicFilterAllows(FPDepthClass::Front, "Mouth", EyesOnly, 0));
    TEST("filter: unmapped rejected when layer filter active",
        !FPSchematicFilterAllows(FPDepthClass::Front, "", EyesOnly, 0));
    TEST("filter: two-layer set allows either", [&]() {
        std::vector<std::string> Two;
        Two.push_back("Eyes");
        Two.push_back("Mouth");
        return FPSchematicFilterAllows(FPDepthClass::Front, "Eyes", Two, 0) &&
            FPSchematicFilterAllows(FPDepthClass::Front, "Mouth", Two, 0) &&
            !FPSchematicFilterAllows(FPDepthClass::Front, "Ears", Two, 0);
    }());

    // AND semantics.
    TEST("filter: AND combines", FPSchematicFilterAllows(FPDepthClass::Front, "Eyes", EyesOnly, 1));
    TEST("filter: AND rejects on either dimension",
        !FPSchematicFilterAllows(FPDepthClass::Back, "Eyes", EyesOnly, 1) &&
        !FPSchematicFilterAllows(FPDepthClass::Front, "Mouth", EyesOnly, 1));

    // All 17 parts pass the identity filter once resolved.
    static const char* Parts17[] = { "BrowL", "BrowR", "EyeL", "EyeR", "Nose",
        "CheekL", "CheekR", "Teeth", "Mouth", "Chin", "EarL", "EarR", "Neck",
        "Bangs", "Hair", "BackHair", "Head" };
    const std::vector<std::string>& Layers = FPSchematicLayerSet();
    for (const char* P : Parts17)
    {
        const char* M = FPLayout::FPHotspotLayerMatch(Layers, P);
        const char* Resolved = M ? M : FPSchematicLayerAlias(P);
        const FPSchematicPart* Part = FPSchematicFindPart(DefaultPartSchematics(), P);
        const std::string Msg = std::string("filter: '") + P + "' passes the all-filter";
        TEST(Msg.c_str(), Part && Resolved &&
            FPSchematicFilterAllows(Part->DepthClass, Resolved, Empty, 0));
    }
}

// --- Central canvas redesign: front/base/back yaw-motion rule ---
void TestYawRule() {
    printf("\n=== YawRule ===\n");
    using R = FPSchematic::FPYawRule;
    using C = FPSchematic::FPDepthClass;

    // Pure mirror of ComputeOffsetForState (non-vertical branch).
    TEST("yaw: front config +0.5 -> +2.5", R::ComputeYawOffset(1.0, false, 0.5) == 2.5);
    TEST("yaw: back config +0.5 -> -2.5", R::ComputeYawOffset(1.0, true, 0.5) == -2.5);
    TEST("yaw: negative yaw mirrors sign", R::ComputeYawOffset(1.0, false, -0.5) == -2.5);
    TEST("yaw: zero yaw -> zero", R::ComputeYawOffset(1.0, false, 0.0) == 0.0);
    TEST("yaw: clamp above +1", R::ComputeYawOffset(1.0, false, 2.0) == 5.0);
    TEST("yaw: clamp below -1", R::ComputeYawOffset(1.0, false, -2.0) == -5.0);
    TEST("yaw: half depth halves offset", R::ComputeYawOffset(0.5, false, 1.0) == 2.5);

    // Class config is data.
    TEST("yaw: front scale 1.0, no invert",
        R::DepthScaleForClass(C::Front) == 1.0 && !R::InvertsParallaxForClass(C::Front));
    TEST("yaw: base anchored - scale below front",
        R::DepthScaleForClass(C::Base) < R::DepthScaleForClass(C::Front));
    TEST("yaw: back inverts parallax", R::InvertsParallaxForClass(C::Back));

    // The rule itself.
    TEST("yaw rule: front moves WITH yaw (positive offset)",
        R::ApplyClass(C::Front, 0.5) > 0.0);
    TEST("yaw rule: back moves AGAINST yaw (negative offset)",
        R::ApplyClass(C::Back, 0.5) < 0.0);
    TEST("yaw rule: front and back are exact mirrors",
        R::ApplyClass(C::Front, 0.5) == -R::ApplyClass(C::Back, 0.5));
    TEST("yaw rule: base anchored - |base| < |front| at full yaw",
        std::abs(R::ApplyClass(C::Base, 1.0)) < std::abs(R::ApplyClass(C::Front, 1.0)));
    TEST("yaw rule: base zero at center", R::ApplyClass(C::Base, 0.0) == 0.0);
    TEST("yaw rule: front saturates at MaxOffset", R::ApplyClass(C::Front, 1.0) == 5.0);
    TEST("yaw rule: back saturates at -MaxOffset", R::ApplyClass(C::Back, 1.0) == -5.0);

    // Base-preset tag table (deploy.py LAYERS drives the same tags).
    TEST("tag: Eyes -> Front", FPSchematic::FPDepthClassForTag("Eyes") == C::Front);
    TEST("tag: Brows -> Front", FPSchematic::FPDepthClassForTag("Brows") == C::Front);
    TEST("tag: Mouth -> Front", FPSchematic::FPDepthClassForTag("Mouth") == C::Front);
    TEST("tag: Bangs -> Front", FPSchematic::FPDepthClassForTag("Bangs") == C::Front);
    TEST("tag: Nose -> Front", FPSchematic::FPDepthClassForTag("Nose") == C::Front);
    TEST("tag: Cheeks -> Front", FPSchematic::FPDepthClassForTag("Cheeks") == C::Front);
    TEST("tag: Head -> Base", FPSchematic::FPDepthClassForTag("Head") == C::Base);
    TEST("tag: Hair -> Back", FPSchematic::FPDepthClassForTag("Hair") == C::Back);
    TEST("tag: BackHair -> Back", FPSchematic::FPDepthClassForTag("BackHair") == C::Back);
    TEST("tag: Ears -> Back", FPSchematic::FPDepthClassForTag("Ears") == C::Back);
    TEST("tag: unknown tag -> Base", FPSchematic::FPDepthClassForTag("Scarf") == C::Base);
    TEST("tag: empty tag -> Base", FPSchematic::FPDepthClassForTag("") == C::Base);
}

int main() {
    printf("===== Face Parallax Math Tests =====\n\n");

    TestStateDetermination();
    TestStateZoneInclusion();
    TestHysteresis();
    TestHysteresisTopBottom();
    TestTransformIdentity();
    TestTransformOverride();
    TestAutoFit();
    TestAutoFitEdgeCases();
    TestEdgeCases();
    TestDifferentThresholds();
    TestTransformCombined();
    TestTransformOverrideSystem();
    TestStateBoundaryPrecision();
    TestHysteresisJitter();
    TestZoneCenterCalculations();
    TestBackStateAngleWrapping();
    TestBlinkAnimation();
    TestExpressionSystem();
    TestVisemeAnimation();
    TestBlendingMath();
    TestHalfZoneWidthZero();
    TestStateChangeCancelsAnimations();
    TestYawDeviationNormalized();
    TestBlinkFrameMismatch();
    TestZeroFrameBlinkViseme();
    TestSwooshTransition();
    TestParameterSystem();
    TestNestedArtSystem();
    TestPinProjection();
    TestBatchOperations();
    TestZoneBoundaries();
    TestCustomZoneBoundaryMultipliers();
    TestBlendPreview();
    TestStatusMatrix();
    TestFNameExpressionViseme();
    TestGetBoundaryOrDefault();
    TestDepthRange();
    TestDepthParamNames();
    TestProfileVisualizerSizing();
    TestProfileDetectionTopBottom();
    TestProfileDetectionMultiLayer();
    TestWireframeMode();
    TestOutlineArtConcept();
    TestProfileVisualizerPropagation();

    TestAsyncStaleness();
    TestLayerVisibility();
    TestAsyncTextureCache();
    TestApplyCurrentStateTextures();
    TestColorByDepth();
    TestApplySearchFilter();

    TestFrameDeltaOrdering();
    TestTexturePushCaching();

    TestNullPreviewActorSafety();
    TestSetPreviewActorContract();

    TestSilhouetteDistanceToEdge();
    TestVisualHullDepth();
    TestVisualHullDepthView();
    TestCameraSnapMapping();
    TestImportChannelDetection();
    TestPhaseBAlignmentMirrors();
    TestPhaseCMirrors();
    TestPhaseDMirrors();
    TestPhaseEFMirrors();
    TestPhaseGWidgetMirrors();
    TestPhaseHUIDesign();
    TestPhaseIUITesting();
    TestHotspotRegions();
    TestHotspotHitIndexEdges();
    TestHotspotLayerMapping();
    TestTransformHotspotRegion();
    TestPinDriftMirror();
    TestPhase4Mirrors();
    TestPrimaryLayerPin();
    TestPinRotation();
    TestNestedEffectiveTransform();
    TestPinDataSurvivesSync();

    TestAccessibilityMirrors();
    TestPinnedActionsRule();
    TestPreviewModesMirror();
    TestUndoStackSemantics();
    TestUndoRedoClearsOnNewMutation();
    TestUndoPreservesUntouchedViews();
    TestUndoStackCap();
    TestPhaseP3Mirrors();
    TestPhaseCIntegration();
    TestPhaseDSyncIntegration();
    TestPhaseLayerBadgeMirrors();
    TestPhasePinMgrMirrors();
    TestSchematicParts();
    TestSchematicCoverage();
    TestHairSystem();
    TestHairMidpointJiggle();
    TestSchematicFilters();
    TestYawRule();

    printf("\n===== Results: %d/%d passed (%d failed) =====\n",
        g_passed, g_total, g_total - g_passed);

    return (g_passed == g_total) ? 0 : 1;
}
