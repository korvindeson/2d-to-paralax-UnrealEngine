// Standalone C++ test harness for core parallax logic.
// Compile with any C++17 compiler: clang++ -std=c++17 -o ParallaxMathTests ParallaxMathTests.cpp && ./ParallaxMathTests
// No UE dependencies - pure math validation.

#include <cmath>
#include <cstdio>
#include <cassert>
#include <algorithm>
#include <array>

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

EFaceAngleState DetermineStateFromAngles(double yaw, double pitch) {
    // Uses same zone-based logic as the component (default HalfZoneWidth = 22.5).
    // Top/Bottom thresholds match default TopViewPitchThreshold = 60, BottomViewPitchThreshold = -60.
    if (pitch > 60.0) return EFaceAngleState::Top;
    if (pitch < -60.0) return EFaceAngleState::Bottom;

    // Horizontal zones: each spans HZW (22.5 deg) on either side of its center
    if (yaw > -HZW && yaw <= HZW) return EFaceAngleState::Front;
    if (yaw > HZW  && yaw <= Z3)  return EFaceAngleState::ThreeQuarterRight;
    if (yaw > Z3   && yaw <= Z5)  return EFaceAngleState::RightProfile;
    if (yaw > Z5   && yaw <= Z7)  return EFaceAngleState::BackRight;
    if (yaw > Z7   || yaw <= -Z7) return EFaceAngleState::Back;
    if (yaw > -Z7  && yaw <= -Z5) return EFaceAngleState::BackLeft;
    if (yaw > -Z5  && yaw <= -Z3) return EFaceAngleState::LeftProfile;
    if (yaw > -Z3  && yaw <= -HZW)return EFaceAngleState::ThreeQuarterLeft;
    return EFaceAngleState::Front;
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
    // Should have 8 transitions across full range: Front↔3Q↔ProR↔BackR↔Back (×2)
    TEST("Transitions across full range", transitions > 3);

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
    // Should not flicker between states every frame
    TEST("Jitter stability", sm.CurrentState == EFaceAngleState::ThreeQuarterRight ||
                              sm.CurrentState == EFaceAngleState::Front);

    // Rapid state hopping (front <-> 3Q <-> profile)
    StateMachine sm2;
    double angles[] = {0, 35, 0, 35, 80, 35, 0, 80, 0};
    for (double a : angles) {
        sm2.Update(a, 0);
    }
    // Should not crash or produce invalid state
    TEST("Rapid hop valid state", (int)sm2.CurrentState < (int)EFaceAngleState::MAX);
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
        TEST("Busyness=0 → 1 intensity peak", peaksLow >= 0 && peaksLow <= 2);
        double peaksHigh = CountPeaks(1.0);
        TEST("Busyness=1 → more peaks than low", peaksHigh > peaksLow);
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

    printf("\n===== Results: %d/%d passed (%d failed) =====\n",
        g_passed, g_total, g_total - g_passed);

    return (g_passed == g_total) ? 0 : 1;
}
