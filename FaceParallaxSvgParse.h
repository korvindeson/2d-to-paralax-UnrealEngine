#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

// The cell-pair resolver (ResolveViewCellPair) mirrors the RUNTIME's at-rest
// committed state (Phase 2 parity: FPSchematicForwardStateAt — the band state
// of FPSchematicStateAtAngles shifted right by the Schmitt margin, so the
// pair's dominant card is exactly the albedo the runtime bakes and the pair
// flips at the runtime's commit keys), with the two-card fade taken from the
// runtime's own parameter-space crossfade (FPSchematicCrossfadeAlpha at the
// pair's band edge). Both headers are pure C++17 and included by the math
// test harness, so the rotation-driven art-swap contract lives here with
// pinned tests instead of a second copy.
#include "FaceParallaxSchematic.h"

namespace FPSvg {

// ============================================================================
// SVG-subset document model (pure C++17, no UE deps).
// Parsed coordinates are normalized to 0..1 in viewBox space (y-down, so the
// canvas painter and the schematic rings share the same orientation).
// ============================================================================

struct FPoint {
    double X = 0.0;
    double Y = 0.0;
    FPoint() {}
    FPoint(double x, double y) : X(x), Y(y) {}
    bool operator==(const FPoint& o) const { return X == o.X && Y == o.Y; }
};

enum class ECmd : uint8_t {
    MoveTo,   // Pts[i] = target; starts a new contour
    LineTo,
    QuadTo,   // Pts[i] = control, Pts[i+1] = target
    CubicTo,  // Pts[i] = control1, Pts[i+1] = control2, Pts[i+2] = target
    Close     // Pts[i] = current point (duplicated); implicit line to contour start
};

struct FPath {
    std::vector<ECmd> Cmds;
    std::vector<FPoint> Pts;
    bool bClosed = false;
    bool bHasFill = false;
    double FillR = 0.0, FillG = 0.0, FillB = 0.0, FillA = 1.0;
    bool bHasStroke = false;
    double StrokeR = 0.0, StrokeG = 0.0, StrokeB = 0.0, StrokeA = 1.0;
    double StrokeWidth = 1.0;
    std::string GroupId;
};

struct FDocument {
    double Width = 1000.0;
    double Height = 1000.0;
    double VbX = 0.0, VbY = 0.0, VbW = 1000.0, VbH = 1000.0;
    std::vector<FPath> Paths;
    std::string Error;
};

// ============================================================================
// Parser.
// ============================================================================

namespace Detail {

constexpr char kParenOpen = '(';
constexpr char kParenClose = ')';

inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
inline bool IsNumStart(char c) { return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.'; }

inline void SkipSpaces(const char*& p) {
    while (*p && (IsSpace(*p) || *p == ',')) ++p;
}

inline bool ParseNumber(const char*& p, double& out) {
    SkipSpaces(p);
    if (!*p || !IsNumStart(*p)) return false;
    const char* s = p;
    double sign = 1.0;
    if (*s == '+' || *s == '-') { if (*s == '-') sign = -1.0; ++s; }
    double v = 0.0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); ++s; ++digits; }
    if (*s == '.') {
        ++s;
        double scale = 1.0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10.0 + (*s - '0');
            scale *= 10.0;
            ++s;
            ++digits;
        }
        v /= scale;
    }
    if (*s == 'e' || *s == 'E') {
        const char* e = s + 1;
        double esign = 1.0;
        if (*e == '+' || *e == '-') { if (*e == '-') esign = -1.0; ++e; }
        if (*e >= '0' && *e <= '9') {
            int exp = 0;
            while (*e >= '0' && *e <= '9') { exp = exp * 10 + (*e - '0'); ++e; }
            v *= pow(10.0, esign * exp);
            s = e;
        }
    }
    if (digits == 0) return false;
    out = sign * v;
    p = s;
    return true;
}

inline bool ParseUnitNumber(const char*& p, double& out) {
    if (!ParseNumber(p, out)) return false;
    while (*p && (isalpha((unsigned char)*p))) ++p;
    return true;
}

inline bool ParseColor(const char* s, double& r, double& g, double& b, double& a) {
    a = 1.0;
    while (*s && IsSpace(*s)) ++s;
    if (*s == '#') {
        ++s;
        int len = 0;
        const char* h = s;
        while (*h && isxdigit((unsigned char)*h) && len < 8) { ++h; ++len; }
        if (len != 3 && len != 4 && len != 6 && len != 8) return false;
        unsigned vals[4] = { 0, 0, 0, 255 };
        for (int i = 0; i < len; ++i) {
            char c = s[i];
            unsigned v = (c >= '0' && c <= '9') ? (unsigned)(c - '0') :
                         (c >= 'a' && c <= 'f') ? (unsigned)(c - 'a' + 10) :
                         (unsigned)(c - 'A' + 10);
            if (len <= 4) vals[i] = v * 16 + v;
            else vals[i / 2] = vals[i / 2] * 16 + v;
        }
        r = vals[0] / 255.0; g = vals[1] / 255.0; b = vals[2] / 255.0; a = vals[3] / 255.0;
        return true;
    }
    if (strncmp(s, "rgb(", 4) == 0 || strncmp(s, "rgba(", 5) == 0) {
        bool alpha = (s[3] == 'a');
        const char* p = s + (alpha ? 5 : 4);
        double v[4] = { 0, 0, 0, 1.0 };
        for (int i = 0; i < (alpha ? 4 : 3); ++i) {
            SkipSpaces(p);
            double num = 0.0;
            if (!ParseNumber(p, num)) return false;
            SkipSpaces(p);
            bool pct = false;
            if (*p == '%') { pct = true; ++p; }
            v[i] = pct ? num / 100.0 : num / 255.0;
            if (i < (alpha ? 3 : 2)) {
                SkipSpaces(p);
                if (*p != ',') return false;
                ++p;
            }
        }
        SkipSpaces(p);
        if (*p != kParenClose) return false;
        r = v[0]; g = v[1]; b = v[2]; a = v[3];
        return true;
    }
    struct Named { const char* Name; double R, G, B; };
    static const Named kNames[] = {
        { "black", 0, 0, 0 }, { "white", 1, 1, 1 }, { "red", 1, 0, 0 },
        { "green", 0, 0.5, 0 }, { "blue", 0, 0, 1 }, { "yellow", 1, 1, 0 },
        { "cyan", 0, 1, 1 }, { "magenta", 1, 0, 1 }, { "gray", 0.5, 0.5, 0.5 },
        { "grey", 0.5, 0.5, 0.5 }, { "orange", 1, 0.65, 0 }, { "purple", 0.5, 0, 0.5 },
        { "pink", 1, 0.75, 0.8 }, { "brown", 0.65, 0.16, 0.16 },
        { "lightgray", 0.83, 0.83, 0.83 }, { "lightgrey", 0.83, 0.83, 0.83 },
        { "darkgray", 0.66, 0.66, 0.66 }, { "darkgrey", 0.66, 0.66, 0.66 },
        { "silver", 0.75, 0.75, 0.75 }, { "maroon", 0.5, 0, 0 }, { "olive", 0.5, 0.5, 0 },
        { "lime", 0, 1, 0 }, { "teal", 0, 0.5, 0.5 }, { "navy", 0, 0, 0.5 },
        { "gold", 1, 0.84, 0 },
    };
    for (const Named& n : kNames) {
        size_t nl = strlen(n.Name);
        if (strncmp(s, n.Name, nl) == 0 && s[nl] == 0) {
            r = n.R; g = n.G; b = n.B; a = 1.0;
            return true;
        }
    }
    return false;
}

struct FStyle {
    bool bHasFill = false;
    double FR = 0.0, FG = 0.0, FB = 0.0, FA = 1.0;
    bool bHasStroke = false;
    double SR = 0.0, SG = 0.0, SB = 0.0, SA = 1.0;
    bool bHasWidth = false;
    double Width = 1.0;

    void InheritFrom(const FStyle& o) {
        if (!bHasFill && o.bHasFill) { bHasFill = true; FR = o.FR; FG = o.FG; FB = o.FB; FA = o.FA; }
        if (!bHasStroke && o.bHasStroke) { bHasStroke = true; SR = o.SR; SG = o.SG; SB = o.SB; SA = o.SA; }
        if (!bHasWidth && o.bHasWidth) { bHasWidth = true; Width = o.Width; }
    }
};

inline void ApplyStyleItem(const std::string& name, const std::string& value, FStyle& s) {
    if (name == "fill") {
        if (value == "none" || value == "transparent") { s.bHasFill = false; return; }
        double r, g, b, a;
        if (ParseColor(value.c_str(), r, g, b, a)) { s.bHasFill = true; s.FR = r; s.FG = g; s.FB = b; s.FA = a; }
    } else if (name == "stroke") {
        if (value == "none" || value == "transparent") { s.bHasStroke = false; return; }
        double r, g, b, a;
        if (ParseColor(value.c_str(), r, g, b, a)) { s.bHasStroke = true; s.SR = r; s.SG = g; s.SB = b; s.SA = a; }
    } else if (name == "stroke-width") {
        const char* p = value.c_str();
        double v = 0.0;
        if (ParseNumber(p, v) && v >= 0.0) { s.bHasWidth = true; s.Width = v; }
    } else if (name == "fill-opacity") {
        const char* p = value.c_str();
        double v = 0.0;
        if (ParseNumber(p, v) && s.bHasFill) { s.FA = v < 0 ? 0 : (v > 1 ? 1 : v); }
    } else if (name == "stroke-opacity") {
        const char* p = value.c_str();
        double v = 0.0;
        if (ParseNumber(p, v) && s.bHasStroke) { s.SA = v < 0 ? 0 : (v > 1 ? 1 : v); }
    } else if (name == "opacity") {
        const char* p = value.c_str();
        double v = 0.0;
        if (ParseNumber(p, v)) {
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            if (s.bHasFill) s.FA *= v;
            if (s.bHasStroke) s.SA *= v;
        }
    }
}

inline void ApplyStyleString(const char* style, FStyle& s) {
    const char* p = style;
    while (*p) {
        while (*p && (*p == ';' || IsSpace(*p))) ++p;
        const char* nameStart = p;
        while (*p && *p != ':' && *p != ';') ++p;
        std::string name(nameStart, (size_t)(p - nameStart));
        if (*p == ':') ++p;
        while (*p && IsSpace(*p)) ++p;
        const char* valStart = p;
        while (*p && *p != ';') ++p;
        std::string value(valStart, (size_t)(p - valStart));
        while (!value.empty() && IsSpace(value.back())) value.pop_back();
        while (!name.empty() && IsSpace(name.back())) name.pop_back();
        if (!name.empty() && !value.empty()) ApplyStyleItem(name, value, s);
    }
}

struct FAffine {
    double A = 1.0, B = 0.0, C = 0.0, D = 1.0, E = 0.0, F = 0.0;
    static FAffine Mul(const FAffine& l, const FAffine& r) {
        FAffine m;
        m.A = l.A * r.A + l.C * r.B;
        m.B = l.B * r.A + l.D * r.B;
        m.C = l.A * r.C + l.C * r.D;
        m.D = l.B * r.C + l.D * r.D;
        m.E = l.A * r.E + l.C * r.F + l.E;
        m.F = l.B * r.E + l.D * r.F + l.F;
        return m;
    }
    FPoint Apply(const FPoint& p) const {
        return FPoint(A * p.X + C * p.Y + E, B * p.X + D * p.Y + F);
    }
};

inline bool ParseTransform(const char* t, FAffine& out) {
    const char* p = t;
    while (*p) {
        while (*p && (IsSpace(*p) || *p == ',')) ++p;
        const char* fnStart = p;
        while (*p && (isalpha((unsigned char)*p))) ++p;
        std::string fn(fnStart, (size_t)(p - fnStart));
        if (fn.empty()) break;
        while (*p && (IsSpace(*p) || *p == ',')) ++p;
        if (*p != kParenOpen) return false;
        ++p;
        double args[6];
        int nargs = 0;
        while (*p && *p != kParenClose) {
            double v = 0.0;
            if (!ParseNumber(p, v) || nargs >= 6) return false;
            args[nargs++] = v;
        }
        if (*p != kParenClose) return false;
        ++p;
        FAffine m;
        const double kPi = 3.14159265358979323846;
        if (fn == "translate") {
            if (nargs < 1 || nargs > 2) return false;
            m.E = args[0];
            m.F = nargs == 2 ? args[1] : 0.0;
        } else if (fn == "scale") {
            if (nargs < 1 || nargs > 2) return false;
            m.A = args[0];
            m.D = nargs == 2 ? args[1] : args[0];
        } else if (fn == "rotate") {
            if (nargs < 1 || nargs > 3) return false;
            double rad = args[0] * kPi / 180.0;
            double c = cos(rad), sn = sin(rad);
            m.A = c; m.B = sn; m.C = -sn; m.D = c;
            if (nargs == 3) {
                FAffine t1, t2;
                t1.E = -args[1]; t1.F = -args[2];
                t2.E = args[1]; t2.F = args[2];
                out = FAffine::Mul(out, FAffine::Mul(FAffine::Mul(t1, m), t2));
                continue;
            }
        } else if (fn == "skewX") {
            if (nargs != 1) return false;
            m.C = tan(args[0] * kPi / 180.0);
        } else if (fn == "skewY") {
            if (nargs != 1) return false;
            m.B = tan(args[0] * kPi / 180.0);
        } else if (fn == "matrix") {
            if (nargs != 6) return false;
            m.A = args[0]; m.B = args[1]; m.C = args[2]; m.D = args[3]; m.E = args[4]; m.F = args[5];
        } else {
            return false;
        }
        out = FAffine::Mul(out, m);
    }
    return true;
}

inline double AngleBetween(double ux, double uy, double vx, double vy) {
    double dot = ux * vx + uy * vy;
    double len = sqrt(ux * ux + uy * uy) * sqrt(vx * vx + vy * vy);
    if (len == 0.0) return 0.0;
    double a = acos(dot / len);
    double cross = ux * vy - uy * vx;
    return cross < 0.0 ? -a : a;
}

struct FPathBuilder {
    std::vector<ECmd>& Cmds;
    std::vector<FPoint>& Pts;
    FAffine XForm = FAffine();
    const FDocument* Doc = nullptr;
    double CurX = 0.0, CurY = 0.0;
    double SubX = 0.0, SubY = 0.0;
    double LastCtrlX = 0.0, LastCtrlY = 0.0;
    char LastCmd = 0;

    FPoint Map(const FPoint& p) const {
        FPoint t = XForm.Apply(p);
        return FPoint((t.X - Doc->VbX) / Doc->VbW, (t.Y - Doc->VbY) / Doc->VbH);
    }
    void Emit(ECmd c, const FPoint& p) {
        Cmds.push_back(c);
        Pts.push_back(p);
    }
    void LineToAbs(double x, double y) {
        Emit(ECmd::LineTo, Map(FPoint(x, y)));
        CurX = x; CurY = y;
        LastCtrlX = x; LastCtrlY = y;
    }
    void MoveToAbs(double x, double y) {
        Emit(ECmd::MoveTo, Map(FPoint(x, y)));
        CurX = x; CurY = y;
        SubX = x; SubY = y;
        LastCtrlX = x; LastCtrlY = y;
    }
    void QuadToAbs(double cx, double cy, double x, double y) {
        Emit(ECmd::QuadTo, Map(FPoint(cx, cy)));
        Emit(ECmd::QuadTo, Map(FPoint(x, y)));
        LastCtrlX = cx; LastCtrlY = cy;
        CurX = x; CurY = y;
    }
    void CubicToAbs(double c1x, double c1y, double c2x, double c2y, double x, double y) {
        Emit(ECmd::CubicTo, Map(FPoint(c1x, c1y)));
        Emit(ECmd::CubicTo, Map(FPoint(c2x, c2y)));
        Emit(ECmd::CubicTo, Map(FPoint(x, y)));
        LastCtrlX = c2x; LastCtrlY = c2y;
        CurX = x; CurY = y;
    }
    void ClosePath() {
        Emit(ECmd::Close, Map(FPoint(CurX, CurY)));
        CurX = SubX; CurY = SubY;
        LastCtrlX = SubX; LastCtrlY = SubY;
    }
    void ArcToAbs(double rx, double ry, double phiDeg, bool bLarge, bool bSweep, double x, double y) {
        const double kPi = 3.14159265358979323846;
        double x1 = CurX, y1 = CurY;
        double x2 = x, y2 = y;
        if (fabs(x2 - x1) < 1e-12 && fabs(y2 - y1) < 1e-12) return;
        rx = fabs(rx); ry = fabs(ry);
        if (rx == 0.0 || ry == 0.0) {
            LineToAbs(x2, y2);
            return;
        }
        double phi = phiDeg * kPi / 180.0;
        double cp = cos(phi), sp = sin(phi);
        double dx = (x1 - x2) / 2.0, dy = (y1 - y2) / 2.0;
        double x1p = cp * dx + sp * dy;
        double y1p = -sp * dx + cp * dy;
        double rx2 = rx * rx, ry2 = ry * ry;
        double l = x1p * x1p / rx2 + y1p * y1p / ry2;
        if (l > 1.0) {
            double s = sqrt(l);
            rx *= s; ry *= s;
            rx2 = rx * rx; ry2 = ry * ry;
        }
        double num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
        double den = rx2 * y1p * y1p + ry2 * x1p * x1p;
        double coef = (den <= 0.0) ? 0.0 : sqrt(num / den);
        if (bLarge == bSweep) coef = -coef;
        double cxp = coef * (rx * y1p / ry);
        double cyp = coef * (-(ry * x1p / rx));
        double cx = cp * cxp - sp * cyp + (x1 + x2) / 2.0;
        double cy = sp * cxp + cp * cyp + (y1 + y2) / 2.0;
        double x2p = -x1p, y2p = -y1p;
        double th1 = AngleBetween(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry);
        double dth = AngleBetween((x1p - cxp) / rx, (y1p - cyp) / ry, (x2p - cxp) / rx, (y2p - cyp) / ry);
        if (!bSweep && dth > 0.0) dth -= 2.0 * kPi;
        if (bSweep && dth < 0.0) dth += 2.0 * kPi;
        int segs = (int)ceil(fabs(dth) / (kPi / 2.0));
        if (segs < 1) segs = 1;
        double th = th1;
        for (int i = 0; i < segs; ++i) {
            double thNext = th + dth / segs;
            double kappa = 4.0 / 3.0 * tan((thNext - th) / 4.0);
            double cosT = cos(th), sinT = sin(th);
            double cosTn = cos(thNext), sinTn = sin(thNext);
            double p0x = cx + rx * cosT * cp - ry * sinT * sp;
            double p0y = cy + rx * cosT * sp + ry * sinT * cp;
            double p1x = p0x + kappa * (-rx * sinT * cp - ry * cosT * sp);
            double p1y = p0y + kappa * (-rx * sinT * sp + ry * cosT * cp);
            double p2x = cx + rx * cosTn * cp - ry * sinTn * sp - kappa * (-rx * sinTn * cp - ry * cosTn * sp);
            double p2y = cy + rx * cosTn * sp + ry * sinTn * cp - kappa * (-rx * sinTn * sp + ry * cosTn * cp);
            double p3x = cx + rx * cosTn * cp - ry * sinTn * sp;
            double p3y = cy + rx * cosTn * sp + ry * sinTn * cp;
            CubicToAbs(p1x, p1y, p2x, p2y, p3x, p3y);
            th = thNext;
        }
    }
};

inline bool ParsePathData(const char* d, FPathBuilder& b, std::string& err) {
    const char* p = d;
    while (*p) {
        SkipSpaces(p);
        if (!*p) break;
        char cmd = 0;
        if (isalpha((unsigned char)*p)) {
            cmd = *p++;
            if (!strchr("MmLlHhVvCcSsQqTtAaZz", cmd)) { err = "unsupported path command"; return false; }
        } else if (b.LastCmd) {
            cmd = b.LastCmd;
        } else {
            err = "path data starts without a command";
            return false;
        }
        b.LastCmd = cmd;
        bool rel = (cmd >= 'a' && cmd <= 'z');
        char up = (char)toupper((unsigned char)cmd);
        if (up == 'Z') {
            b.ClosePath();
            b.LastCmd = 0;
            continue;
        }
        while (true) {
            SkipSpaces(p);
            if (!*p || isalpha((unsigned char)*p)) break;
            switch (up) {
                case 'M': {
                    double x = 0, y = 0;
                    if (!ParseNumber(p, x) || !ParseNumber(p, y)) { err = "bad M params"; return false; }
                    if (rel) { x += b.CurX; y += b.CurY; }
                    b.MoveToAbs(x, y);
                    up = 'L'; rel = (cmd >= 'a' && cmd <= 'z');
                    break;
                }
                case 'L': {
                    double x = 0, y = 0;
                    if (!ParseNumber(p, x) || !ParseNumber(p, y)) { err = "bad L params"; return false; }
                    if (rel) { x += b.CurX; y += b.CurY; }
                    b.LineToAbs(x, y);
                    break;
                }
                case 'H': {
                    double x = 0;
                    if (!ParseNumber(p, x)) { err = "bad H params"; return false; }
                    if (rel) x += b.CurX;
                    b.LineToAbs(x, b.CurY);
                    break;
                }
                case 'V': {
                    double y = 0;
                    if (!ParseNumber(p, y)) { err = "bad V params"; return false; }
                    if (rel) y += b.CurY;
                    b.LineToAbs(b.CurX, y);
                    break;
                }
                case 'C': {
                    double c1x = 0, c1y = 0, c2x = 0, c2y = 0, x = 0, y = 0;
                    if (!ParseNumber(p, c1x) || !ParseNumber(p, c1y) || !ParseNumber(p, c2x) ||
                        !ParseNumber(p, c2y) || !ParseNumber(p, x) || !ParseNumber(p, y)) { err = "bad C params"; return false; }
                    if (rel) { c1x += b.CurX; c1y += b.CurY; c2x += b.CurX; c2y += b.CurY; x += b.CurX; y += b.CurY; }
                    b.CubicToAbs(c1x, c1y, c2x, c2y, x, y);
                    break;
                }
                case 'S': {
                    double c2x = 0, c2y = 0, x = 0, y = 0;
                    if (!ParseNumber(p, c2x) || !ParseNumber(p, c2y) || !ParseNumber(p, x) || !ParseNumber(p, y)) { err = "bad S params"; return false; }
                    double c1x = 0, c1y = 0;
                    if (b.LastCmd == 'C' || b.LastCmd == 'S') {
                        c1x = 2.0 * b.CurX - b.LastCtrlX;
                        c1y = 2.0 * b.CurY - b.LastCtrlY;
                    } else {
                        c1x = b.CurX; c1y = b.CurY;
                    }
                    if (rel) { c2x += b.CurX; c2y += b.CurY; x += b.CurX; y += b.CurY; }
                    b.CubicToAbs(c1x, c1y, c2x, c2y, x, y);
                    break;
                }
                case 'Q': {
                    double cx = 0, cy = 0, x = 0, y = 0;
                    if (!ParseNumber(p, cx) || !ParseNumber(p, cy) || !ParseNumber(p, x) || !ParseNumber(p, y)) { err = "bad Q params"; return false; }
                    if (rel) { cx += b.CurX; cy += b.CurY; x += b.CurX; y += b.CurY; }
                    b.QuadToAbs(cx, cy, x, y);
                    break;
                }
                case 'T': {
                    double x = 0, y = 0;
                    if (!ParseNumber(p, x) || !ParseNumber(p, y)) { err = "bad T params"; return false; }
                    double cx = 0, cy = 0;
                    if (b.LastCmd == 'Q' || b.LastCmd == 'T') {
                        cx = 2.0 * b.CurX - b.LastCtrlX;
                        cy = 2.0 * b.CurY - b.LastCtrlY;
                    } else {
                        cx = b.CurX; cy = b.CurY;
                    }
                    if (rel) { x += b.CurX; y += b.CurY; }
                    b.QuadToAbs(cx, cy, x, y);
                    break;
                }
                case 'A': {
                    double rx = 0, ry = 0, phi = 0, laf = 0, sf = 0, x = 0, y = 0;
                    if (!ParseNumber(p, rx) || !ParseNumber(p, ry) || !ParseNumber(p, phi) ||
                        !ParseNumber(p, laf) || !ParseNumber(p, sf) || !ParseNumber(p, x) || !ParseNumber(p, y)) { err = "bad A params"; return false; }
                    if (rel) { x += b.CurX; y += b.CurY; }
                    b.ArcToAbs(rx, ry, phi, laf != 0.0, sf != 0.0, x, y);
                    break;
                }
                default:
                    err = "unsupported path command";
                    return false;
            }
        }
    }
    return true;
}

struct FFrame {
    FStyle Style;
    FAffine Xform;
    std::string Id;
};

} // namespace Detail

// ============================================================================

inline bool ParseDocument(const char* Src, size_t Len, FDocument& Out) {
    Out.Paths.clear();
    Out.Error.clear();
    const char* p = Src;
    const char* end = Src + Len;

    std::vector<Detail::FFrame> frames;
    bool bSeenSvg = false;
    std::string skipElem;
    int skipDepth = 0;

    while (p < end) {
        while (p < end && *p != '<') ++p;
        if (p >= end) break;
        if (p + 3 < end && strncmp(p, "<!--", 4) == 0) {
            const char* c = strstr(p, "-->");
            if (!c) break;
            p = c + 3;
            continue;
        }
        if (p + 1 < end && p[1] == '?') {
            const char* c = strstr(p, "?>");
            if (!c) break;
            p = c + 2;
            continue;
        }
        const char* tagStart = p + 1;
        bool bClose = (*tagStart == '/');
        if (bClose) ++tagStart;
        const char* nameStart = tagStart;
        while (tagStart < end && *tagStart && !Detail::IsSpace(*tagStart) && *tagStart != '>') ++tagStart;
        std::string tagName(nameStart, (size_t)(tagStart - nameStart));
        bool bSelfClose = false;

        std::string id;
        Detail::FStyle elemStyle;
        Detail::FAffine elemXform;
        std::string dAttr;
        std::vector<std::pair<std::string, std::string> > attrs;

        const char* a = tagStart;
        while (a < end && *a && *a != '>') {
            while (*a && (Detail::IsSpace(*a))) ++a;
            if (*a == '>' || *a == 0) break;
            const char* nStart = a;
            while (*a && !Detail::IsSpace(*a) && *a != '=' && *a != '>') ++a;
            std::string an(nStart, (size_t)(a - nStart));
            if (*a == '/') { bSelfClose = true; ++a; continue; }
            while (*a && (Detail::IsSpace(*a))) ++a;
            if (*a != '=') continue;
            ++a;
            while (*a && (Detail::IsSpace(*a))) ++a;
            std::string av;
            if (*a == '"' || *a == '\'') {
                char q = *a++;
                const char* vStart = a;
                while (*a && *a != q) ++a;
                av.assign(vStart, (size_t)(a - vStart));
                if (*a == q) ++a;
            } else {
                const char* vStart = a;
                while (*a && !Detail::IsSpace(*a) && *a != '>' && *a != '/') ++a;
                av.assign(vStart, (size_t)(a - vStart));
            }
            if (!an.empty()) {
                if (an == "id") id = av;
                else if (an == "d") dAttr = av;
                else if (an == "fill" || an == "stroke" || an == "stroke-width" ||
                         an == "fill-opacity" || an == "stroke-opacity" || an == "opacity") {
                    Detail::ApplyStyleItem(an, av, elemStyle);
                } else if (an == "style") {
                    Detail::ApplyStyleString(av.c_str(), elemStyle);
                } else if (an == "transform") {
                    Detail::FAffine t;
                    if (Detail::ParseTransform(av.c_str(), t)) elemXform = Detail::FAffine::Mul(elemXform, t);
                } else {
                    attrs.push_back(std::make_pair(an, av));
                }
            }
        }
        if (a < end && *a == '>') ++a;
        p = a;
        if (p >= end) break;

        if (!skipElem.empty()) {
            if (!bClose && tagName == skipElem) ++skipDepth;
            else if (bClose && tagName == skipElem) {
                --skipDepth;
                if (skipDepth <= 0) { skipElem.clear(); skipDepth = 0; }
            }
            continue;
        }

        if (bClose) {
            if (tagName == "svg" || tagName == "g") {
                if (!frames.empty()) frames.pop_back();
            }
            continue;
        }
        if (bSelfClose) continue;

        if (tagName == "svg") {
            bSeenSvg = true;
            double vb[4] = { 0.0, 0.0, 1000.0, 1000.0 };
            bool bHasVb = false;
            for (size_t i = 0; i < attrs.size(); ++i) {
                if (attrs[i].first == "viewBox") {
                    const char* vp = attrs[i].second.c_str();
                    double nums[4];
                    int n = 0;
                    while (n < 4) { if (!Detail::ParseNumber(vp, nums[n])) break; ++n; }
                    if (n == 4) { vb[0] = nums[0]; vb[1] = nums[1]; vb[2] = nums[2]; vb[3] = nums[3]; bHasVb = true; }
                } else if (attrs[i].first == "width") {
                    const char* vp = attrs[i].second.c_str();
                    Detail::ParseUnitNumber(vp, Out.Width);
                } else if (attrs[i].first == "height") {
                    const char* vp = attrs[i].second.c_str();
                    Detail::ParseUnitNumber(vp, Out.Height);
                }
            }
            if (!bHasVb) { vb[0] = 0; vb[1] = 0; vb[2] = Out.Width; vb[3] = Out.Height; }
            Out.VbX = vb[0]; Out.VbY = vb[1]; Out.VbW = vb[2]; Out.VbH = vb[3];
            Detail::FFrame f;
            f.Style = elemStyle;
            f.Xform = elemXform;
            f.Id = id;
            frames.push_back(f);
            continue;
        }

        if (tagName == "g" || tagName == "path" || tagName == "circle" || tagName == "ellipse" ||
            tagName == "rect" || tagName == "line" || tagName == "polyline" || tagName == "polygon") {
            Detail::FStyle parentStyle;
            Detail::FAffine parentXform;
            std::string parentId;
            if (!frames.empty()) {
                parentStyle = frames.back().Style;
                parentXform = frames.back().Xform;
                parentId = frames.back().Id;
            }
            if (tagName == "g") {
                Detail::FFrame f;
                f.Style = elemStyle;
                f.Style.InheritFrom(parentStyle);
                f.Xform = Detail::FAffine::Mul(parentXform, elemXform);
                f.Id = id.empty() ? parentId : id;
                frames.push_back(f);
                continue;
            }

            Detail::FStyle s = elemStyle;
            s.InheritFrom(parentStyle);
            Detail::FAffine xform = Detail::FAffine::Mul(parentXform, elemXform);
            std::string gid = id.empty() ? parentId : id;

            FPath path;
            path.GroupId = gid;
            Detail::FPathBuilder b = { path.Cmds, path.Pts };
            b.XForm = xform;
            b.Doc = &Out;

            if (tagName == "path") {
                if (dAttr.empty()) continue;
                std::string err;
                if (!Detail::ParsePathData(dAttr.c_str(), b, err)) {
                    Out.Error = "path error: " + err;
                    return false;
                }
            } else {
                double nums[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
                for (size_t i = 0; i < attrs.size(); ++i) {
                    const char* vp = attrs[i].second.c_str();
                    double v = 0.0;
                    if (!Detail::ParseNumber(vp, v)) continue;
                    const std::string& k = attrs[i].first;
                    if (k == "cx") nums[0] = v;
                    else if (k == "cy") nums[1] = v;
                    else if (k == "r") nums[2] = v;
                    else if (k == "rx") nums[3] = v;
                    else if (k == "ry") nums[4] = v;
                    else if (k == "x") nums[5] = v;
                    else if (k == "y") nums[6] = v;
                    else if (k == "width") nums[7] = v;
                }
                double h = 0.0;
                for (size_t i = 0; i < attrs.size(); ++i) {
                    if (attrs[i].first == "height") {
                        const char* vp = attrs[i].second.c_str();
                        double v = 0.0;
                        if (Detail::ParseNumber(vp, v)) h = v;
                    }
                }
                const double kKappa = 0.5522847498307936;
                if (tagName == "circle") {
                    double cx = nums[0], cy = nums[1], r = nums[2];
                    if (r <= 0.0) continue;
                    b.MoveToAbs(cx - r, cy);
                    b.CubicToAbs(cx - r, cy + kKappa * r, cx - kKappa * r, cy + r, cx, cy + r);
                    b.CubicToAbs(cx + kKappa * r, cy + r, cx + r, cy + kKappa * r, cx + r, cy);
                    b.CubicToAbs(cx + r, cy - kKappa * r, cx + kKappa * r, cy - r, cx, cy - r);
                    b.CubicToAbs(cx - kKappa * r, cy - r, cx - r, cy - kKappa * r, cx - r, cy);
                    b.ClosePath();
                } else if (tagName == "ellipse") {
                    double cx = nums[0], cy = nums[1], rx = nums[3], ry = nums[4];
                    if (rx <= 0.0 || ry <= 0.0) continue;
                    b.MoveToAbs(cx + rx, cy);
                    b.CubicToAbs(cx + rx, cy + kKappa * ry, cx + kKappa * rx, cy + ry, cx, cy + ry);
                    b.CubicToAbs(cx - kKappa * rx, cy + ry, cx - rx, cy + kKappa * ry, cx - rx, cy);
                    b.CubicToAbs(cx - rx, cy - kKappa * ry, cx - kKappa * rx, cy - ry, cx, cy - ry);
                    b.CubicToAbs(cx + kKappa * rx, cy - ry, cx + rx, cy - kKappa * ry, cx + rx, cy);
                    b.ClosePath();
                } else if (tagName == "rect") {
                    double x = nums[5], y = nums[6], w = nums[7];
                    if (w <= 0.0 || h <= 0.0) continue;
                    double rx = nums[3], ry = nums[4];
                    if (rx == 0.0 && ry == 0.0) {
                        b.MoveToAbs(x, y);
                        b.LineToAbs(x + w, y);
                        b.LineToAbs(x + w, y + h);
                        b.LineToAbs(x, y + h);
                        b.ClosePath();
                    } else {
                        if (rx == 0.0) rx = ry;
                        if (ry == 0.0) ry = rx;
                        if (rx > w / 2.0) rx = w / 2.0;
                        if (ry > h / 2.0) ry = h / 2.0;
                        b.MoveToAbs(x + rx, y);
                        b.LineToAbs(x + w - rx, y);
                        b.CubicToAbs(x + w - rx + kKappa * rx, y, x + w, y + ry - kKappa * ry, x + w, y + ry);
                        b.LineToAbs(x + w, y + h - ry);
                        b.CubicToAbs(x + w, y + h - ry + kKappa * ry, x + w - rx + kKappa * rx, y + h, x + w - rx, y + h);
                        b.LineToAbs(x + rx, y + h);
                        b.CubicToAbs(x + rx - kKappa * rx, y + h, x, y + h - ry + kKappa * ry, x, y + h - ry);
                        b.LineToAbs(x, y + ry);
                        b.CubicToAbs(x, y + ry - kKappa * ry, x + rx - kKappa * rx, y, x + rx, y);
                        b.ClosePath();
                    }
                } else if (tagName == "line") {
                    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                    for (size_t i = 0; i < attrs.size(); ++i) {
                        const char* vp = attrs[i].second.c_str();
                        double v = 0.0;
                        if (!Detail::ParseNumber(vp, v)) continue;
                        const std::string& k = attrs[i].first;
                        if (k == "x1") x1 = v; else if (k == "y1") y1 = v;
                        else if (k == "x2") x2 = v; else if (k == "y2") y2 = v;
                    }
                    b.MoveToAbs(x1, y1);
                    b.LineToAbs(x2, y2);
                } else if (tagName == "polyline" || tagName == "polygon") {
                    const char* pp = dAttr.empty() ? "" : dAttr.c_str();
                    for (size_t i = 0; i < attrs.size(); ++i) {
                        if (attrs[i].first == "points") { pp = attrs[i].second.c_str(); break; }
                    }
                    double x = 0, y = 0;
                    bool bFirst = true;
                    while (Detail::ParseNumber(pp, x) && Detail::ParseNumber(pp, y)) {
                        if (bFirst) { b.MoveToAbs(x, y); bFirst = false; }
                        else b.LineToAbs(x, y);
                    }
                    if (!bFirst && tagName == "polygon") b.ClosePath();
                }
            }

            if (path.Cmds.empty()) continue;
            path.bClosed = (path.Cmds.back() == ECmd::Close);
            if (s.bHasFill) {
                path.bHasFill = true;
                path.FillR = s.FR; path.FillG = s.FG; path.FillB = s.FB; path.FillA = s.FA;
            }
            if (s.bHasStroke) {
                path.bHasStroke = true;
                path.StrokeR = s.SR; path.StrokeG = s.SG; path.StrokeB = s.SB; path.StrokeA = s.SA;
                path.StrokeWidth = s.bHasWidth ? s.Width : 1.0;
            }
            if (!path.bHasFill && !path.bHasStroke) {
                path.bHasFill = true;
                path.FillR = 0.0; path.FillG = 0.0; path.FillB = 0.0; path.FillA = 1.0;
            }
            Out.Paths.push_back(path);
            continue;
        }

        if (tagName == "defs" || tagName == "mask" || tagName == "clipPath" ||
            tagName == "linearGradient" || tagName == "radialGradient" || tagName == "filter" ||
            tagName == "style" || tagName == "script" || tagName == "pattern" || tagName == "symbol" ||
            tagName == "metadata" || tagName == "desc" || tagName == "title") {
            skipElem = tagName;
            skipDepth = 0;
            continue;
        }

        if (tagName == "use" || tagName == "image" || tagName == "text" || tagName == "tspan" ||
            tagName == "foreignObject" || tagName == "switch") {
            continue;
        }
    }

    if (Out.Paths.empty() && Out.Error.empty() && !bSeenSvg) {
        Out.Error = "no <svg> element found";
    }
    return Out.Error.empty();
}

inline FDocument MirrorX(const FDocument& Src) {
    FDocument d = Src;
    for (size_t i = 0; i < d.Paths.size(); ++i) {
        for (size_t j = 0; j < d.Paths[i].Pts.size(); ++j) {
            d.Paths[i].Pts[j].X = 1.0 - d.Paths[i].Pts[j].X;
        }
    }
    return d;
}

// ============================================================================
// Guide-token vector grid contract (art_guide Part VIII / VI / XI).
// The 17 parts resolve to feature tokens; pairs split into Near/Far roles.
// The Nose part carries its OWN token ("Nose", art_guide I.6 — the microscopic
// triangle indicator); "Proj" is kept as an AUXILIARY token for snout/horn
// (creature presets) only — it authors from the nose ring and keeps its Y22
// sub-row, but is NOT one of the canonical 17.
// ============================================================================

struct FFeatureEntry {
    const char* Part;
    const char* Feature;
};

inline const FFeatureEntry* FeatureTable() {
    static const FFeatureEntry kTable[] = {
        { "Head",     "FaceBase" },
        { "Nose",     "Nose" },
        { "Bangs",    "HairFront" },
        { "Hair",     "HairBack" },
        { "BackHair", "BackHair" },
        { "EyeR",     "Eye_Near" },
        { "EyeL",     "Eye_Far" },
        { "BrowR",    "Brow_Near" },
        { "BrowL",    "Brow_Far" },
        { "CheekR",   "Cheek_Near" },
        { "CheekL",   "Cheek_Far" },
        { "EarR",     "Ear_Near" },
        { "EarL",     "Ear_Far" },
        { "Mouth",    "Mouth" },
        { "Teeth",    "Teeth" },
        { "Chin",     "Chin" },
        { "Neck",     "Neck" },
    };
    return kTable;
}

inline int FeatureTableCount() { return 17; }

// Non-canonical library tokens (snout/horn projections, art_guide VIII).
// These are first-class library tokens (own folder/grid/cell set) but never
// map from a canonical part; they stay resolvable for creature presets.
inline const char* AuxiliaryFeatureAt(int Index) {
    static const char* kAux[] = { "Proj", nullptr };
    return (Index >= 0 && Index < 1) ? kAux[Index] : nullptr;
}

inline int AuxiliaryFeatureCount() { return 1; }

inline bool FeatureIsKnown(const char* Feature) {
    for (int i = 0; i < FeatureTableCount(); ++i)
        if (strcmp(FeatureTable()[i].Feature, Feature) == 0) return true;
    for (int i = 0; i < AuxiliaryFeatureCount(); ++i)
        if (strcmp(AuxiliaryFeatureAt(i), Feature) == 0) return true;
    return false;
}

inline const char* FeatureTokenForPart(const char* Part) {
    const FFeatureEntry* t = FeatureTable();
    for (int i = 0; i < FeatureTableCount(); ++i) {
        if (strcmp(t[i].Part, Part) == 0) return t[i].Feature;
    }
    return nullptr;
}

inline const char* PartForFeature(const char* Feature) {
    const FFeatureEntry* t = FeatureTable();
    for (int i = 0; i < FeatureTableCount(); ++i) {
        if (strcmp(t[i].Feature, Feature) == 0) return t[i].Part;
    }
    return nullptr;
}

inline bool IsPairFeature(const char* Feature) {
    return strcmp(Feature, "Eye_Near") == 0 || strcmp(Feature, "Eye_Far") == 0 ||
           strcmp(Feature, "Brow_Near") == 0 || strcmp(Feature, "Brow_Far") == 0 ||
           strcmp(Feature, "Cheek_Near") == 0 || strcmp(Feature, "Cheek_Far") == 0 ||
           strcmp(Feature, "Ear_Near") == 0 || strcmp(Feature, "Ear_Far") == 0;
}

// Canonical state tokens, one per EFaceAngleState mirror order (0..13).
// 0..6  = right half (Front, Narrow, 3Q, Sliver, Profile, Back3Q, Back)
// 7..11 = left half  (Back3Q_L, Sliver_L, Profile_L, 3Q_L, Narrow_L)
// 12    = Top, 13 = UnderPlane (Bottom)
inline const char* StateTokenForIndex(int Idx) {
    static const char* kState[14] = {
        "Front", "Narrow", "3Q", "Sliver", "Profile", "Back3Q", "Back",
        "Back3Q_L", "Sliver_L", "Profile_L", "3Q_L", "Narrow_L",
        "Top", "UnderPlane"
    };
    return (Idx >= 0 && Idx < 14) ? kState[Idx] : nullptr;
}

inline const char* YawTokenForIndex(int Idx) {
    static const char* kYaw[14] = {
        "Y00", "Y22", "Y45", "Y67", "Y90", "Y135", "Y180",
        "Y135", "Y67", "Y90", "Y45", "Y22",
        "Y00", "Y00"
    };
    return (Idx >= 0 && Idx < 14) ? kYaw[Idx] : nullptr;
}

inline bool IsLeftIndex(int Idx) { return Idx >= 7 && Idx <= 11; }

inline int MirrorIndexForIndex(int Idx) {
    static const int kMirror[14] = { -1, 11, 10, 8, 9, 7, -1, 5, 3, 4, 2, 1, -1, -1 };
    return kMirror[Idx];
}

inline const char* RingSlotForIndex(int Idx) {
    static const char* kSlot[14] = {
        "P0", "P0", "P45", "P45", "P90", "P135", "P180",
        "P135", "P45", "P90", "P45", "P0",
        "PTop", "PBottom"
    };
    return kSlot[Idx];
}

inline const char* PitchTokenForBand(int Band) {
    static const char* kPitch[3] = { "P00", "P45", "Pn45" };
    return (Band >= 0 && Band < 3) ? kPitch[Band] : nullptr;
}

inline bool StateHasPitchBands(int Idx) { return Idx != 12 && Idx != 13; }

inline const char* NextZoneStateToken(const char* StateToken) {
    static const struct { const char* Cur; const char* Next; } kNext[] = {
        { "Front", "Narrow" }, { "Narrow", "3Q" }, { "3Q", "Sliver" },
        { "Sliver", "Profile" }, { "Profile", "Back3Q" }, { "Back3Q", "Back" },
        { "Back", "Back" },
        { "Back3Q_L", "Back" }, { "Sliver_L", "Back3Q_L" }, { "Profile_L", "Sliver_L" },
        { "3Q_L", "Profile_L" }, { "Narrow_L", "3Q_L" },
    };
    for (const auto& e : kNext) {
        if (strcmp(e.Cur, StateToken) == 0) return e.Next;
    }
    return nullptr;
}

inline bool FeatureHasYawRow(const char* Feature, const char* YawToken) {
    bool bEye = strcmp(Feature, "Eye_Near") == 0 || strcmp(Feature, "Eye_Far") == 0;
    bool bProj = strcmp(Feature, "Proj") == 0;
    if (strcmp(YawToken, "Y22") == 0) return bEye || bProj;
    if (strcmp(YawToken, "Y67") == 0) return bEye;
    return true;
}

inline int AuthoredFileCountForFeature(const char* Feature) {
    int count = 0;
    for (int idx = 0; idx < 14; ++idx) {
        if (IsLeftIndex(idx)) continue;
        const char* yaw = YawTokenForIndex(idx);
        if (!FeatureHasYawRow(Feature, yaw)) continue;
        count += StateHasPitchBands(idx) ? 3 : 1;
    }
    return count;
}

inline int CellCountForFeature(const char* Feature) {
    int count = 0;
    for (int idx = 0; idx < 14; ++idx) {
        const char* yaw = YawTokenForIndex(idx);
        if (!FeatureHasYawRow(Feature, yaw)) continue;
        count += StateHasPitchBands(idx) ? 3 : 1;
    }
    return count;
}

inline int TotalAuthoredFiles() {
    int total = 0;
    for (int i = 0; i < FeatureTableCount(); ++i) {
        total += AuthoredFileCountForFeature(FeatureTable()[i].Feature);
    }
    return total;
}

inline int TotalCells() {
    int total = 0;
    for (int i = 0; i < FeatureTableCount(); ++i) {
        total += CellCountForFeature(FeatureTable()[i].Feature);
    }
    return total;
}

inline bool IsVisemeStateToken(const char* StateToken) {
    return strcmp(StateToken, "A") == 0 || strcmp(StateToken, "I") == 0 ||
           strcmp(StateToken, "U") == 0 || strcmp(StateToken, "Closed") == 0 ||
           strcmp(StateToken, "Neutral") == 0;
}

inline bool IsBlinkStateToken(const char* StateToken) {
    return strcmp(StateToken, "Open") == 0 || strcmp(StateToken, "Half") == 0 ||
           strcmp(StateToken, "Closed") == 0;
}

inline bool FeatureHasExtraCells(const char* Feature, const char* Kind) {
    if (strcmp(Kind, "Viseme") == 0) return strcmp(Feature, "Mouth") == 0;
    if (strcmp(Kind, "Blink") == 0) {
        return strcmp(Feature, "Eye_Near") == 0 || strcmp(Feature, "Eye_Far") == 0;
    }
    return false;
}

inline int ExtraCellCount(const char* Kind) {
    if (strcmp(Kind, "Viseme") == 0) return 15;
    if (strcmp(Kind, "Blink") == 0) return 9;
    return 0;
}

inline bool IsValidStateToken(const char* StateToken) {
    for (int i = 0; i < 14; ++i) {
        if (StateTokenForIndex(i) && strcmp(StateTokenForIndex(i), StateToken) == 0) return true;
    }
    return IsVisemeStateToken(StateToken) || IsBlinkStateToken(StateToken);
}

inline bool IsValidYawToken(const char* YawToken) {
    static const char* kYaw[7] = { "Y00", "Y22", "Y45", "Y67", "Y90", "Y135", "Y180" };
    for (const char* y : kYaw) {
        if (strcmp(y, YawToken) == 0) return true;
    }
    return false;
}

inline bool IsValidPitchToken(const char* PitchToken) {
    return strcmp(PitchToken, "P00") == 0 || strcmp(PitchToken, "P45") == 0 ||
           strcmp(PitchToken, "Pn45") == 0 || strcmp(PitchToken, "P90") == 0;
}

inline std::string CellKeyForTokens(const char* Feature, const char* StateToken,
                                    const char* YawToken, const char* PitchToken) {
    return std::string(Feature) + "_" + StateToken + "_" + YawToken + "_" + PitchToken;
}

inline std::string FeatureCellKey(const char* Feature, int StateIdx, int PitchBand) {
    if (StateIdx == 12) return CellKeyForTokens(Feature, "Top", "Y00", "P90");
    if (StateIdx == 13) return CellKeyForTokens(Feature, "UnderPlane", "Y00", "Pn45");
    return CellKeyForTokens(Feature, StateTokenForIndex(StateIdx),
                            YawTokenForIndex(StateIdx), PitchTokenForBand(PitchBand));
}

inline const char* MirrorPartnerCellState(const char* StateToken) {
    for (int i = 0; i < 14; ++i) {
        const char* st = StateTokenForIndex(i);
        if (st && strcmp(st, StateToken) == 0) {
            int m = MirrorIndexForIndex(i);
            return (m >= 0) ? StateTokenForIndex(m) : nullptr;
        }
    }
    return nullptr;
}

inline std::string MirrorPartnerKey(const char* Feature, const char* StateToken,
                                    const char* YawToken, const char* PitchToken) {
    const char* partner = MirrorPartnerCellState(StateToken);
    if (!partner) return std::string();
    return CellKeyForTokens(Feature, partner, YawToken, PitchToken);
}

inline bool ParseCellKey(const char* Key, std::string& Feature, std::string& StateToken,
                         std::string& YawToken, std::string& PitchToken) {
    const FFeatureEntry* t = FeatureTable();
    for (int i = 0; i < FeatureTableCount(); ++i) {
        size_t fl = strlen(t[i].Feature);
        if (strncmp(Key, t[i].Feature, fl) != 0) continue;
        if (Key[fl] != '_') continue;
        const char* rest = Key + fl + 1;
        const char* s2 = strrchr(rest, '_');
        if (!s2) continue;
        std::string pitch(s2 + 1);
        const char* s1 = nullptr;
        for (const char* q = s2 - 1; q >= rest; --q) {
            if (*q == '_') { s1 = q; break; }
        }
        if (!s1) continue;
        std::string yaw(s1 + 1, (size_t)(s2 - s1 - 1));
        std::string state(rest, (size_t)(s1 - rest));
        if (!IsValidStateToken(state.c_str())) continue;
        if (!IsValidYawToken(yaw.c_str())) continue;
        if (!IsValidPitchToken(pitch.c_str())) continue;
        Feature = t[i].Feature;
        StateToken = state;
        YawToken = yaw;
        PitchToken = pitch;
        return true;
    }
    return false;
}

// Viewer cell resolution: map live yaw/pitch to a cell key for a feature.
struct FViewCell {
    std::string Key;
    bool bMirrorRender = false;
};

inline bool ResolveViewCell(const char* Feature, double YawDeg, double PitchDeg, FViewCell& Out) {
    bool bLeft = YawDeg < 0.0;
    double ay = fabs(YawDeg);
    if (ay > 180.0) ay = 180.0;

    std::string state = "Front";
    if (ay >= 157.5) {
        state = "Back";
    } else if (ay >= 112.5) {
        state = "Back3Q";
    } else if (ay >= 67.5) {
        state = FeatureHasYawRow(Feature, "Y67") ? "Sliver" : "Profile";
    } else if (ay >= 22.5) {
        state = FeatureHasYawRow(Feature, "Y22") ? "Narrow" : "3Q";
    }
    if (bLeft && state != "Front" && state != "Back") state += "_L";

    const char* yaw = nullptr;
    for (int i = 0; i < 14; ++i) {
        const char* st = StateTokenForIndex(i);
        if (st && strcmp(st, state.c_str()) == 0) {
            yaw = YawTokenForIndex(i);
            break;
        }
    }
    if (!yaw) return false;

    if (PitchDeg >= 67.5) {
        Out.Key = CellKeyForTokens(Feature, "Top", "Y00", "P90");
        return true;
    }
    if (PitchDeg <= -67.5) {
        Out.Key = CellKeyForTokens(Feature, "UnderPlane", "Y00", "Pn45");
        return true;
    }
    const char* pitch = "P00";
    if (PitchDeg >= 22.5) pitch = "P45";
    else if (PitchDeg <= -22.5) pitch = "Pn45";

    Out.Key = CellKeyForTokens(Feature, state.c_str(), yaw, pitch);
    return true;
}

// Viseme/blink cell resolution (extra cells, P00-only, right-half yaw rows).
inline bool ResolveExtraCell(const char* Feature, const char* Kind, const char* StateToken,
                             double YawDeg, FViewCell& Out) {
    if (strcmp(Kind, "Viseme") != 0 && strcmp(Kind, "Blink") != 0) return false;
    if (!FeatureHasExtraCells(Feature, Kind)) return false;
    double ay = fabs(YawDeg);
    const char* yaw = "Y00";
    if (ay >= 67.5) yaw = "Y90";
    else if (ay >= 22.5) yaw = "Y45";
    Out.Key = CellKeyForTokens(Feature, StateToken, yaw, "P00");
    Out.bMirrorRender = YawDeg < 0.0;
    return true;
}

// ============================================================================
// Viewer cell-PAIR resolution (rotation-driven art crossfade, pure contract).
// A view sits between TWO art cells: the state whose zone center is behind
// the current yaw and the state ahead. This resolves both cells and the blend
// weight from the angles ALONE (no frame history) by mirroring the schematic
// header's FPSchematicBracketStates: A/B are the states whose zone centers
// bracket the yaw (|pitch| past the +-45 thresholds brackets the yaw-nearest
// ground state against Top/Bottom), and BlendAlpha is the smoothstep weight
// between the centers — 0 exactly at A's center (the state's own key angle,
// where the resolved view's art is EXACT), 1 exactly at B's center. The
// widget painter renders PrevKey at (1 - BlendAlpha) and CurKey at
// BlendAlpha, so at every state center the dominant card is the resolved
// state's cell and the scrub blend is one continuous position-driven fade.
// The per-feature sub-threshold collapse mirrors ResolveViewCell: a feature
// with no Y22 row resolves the Narrow views onto the 3Q cell (and no Y67
// resolves Sliver onto Profile), so the pair never names a cell the feature
// has no art file for. Left-half keys (states 7-11) are their OWN authored
// mirrored files, so no render-time mirror flag is needed. The runtime's
// stateful prev/cur blend (B.2/B.3) is a separate contract pinned by the
// component tests; this is the directionless widget/viewer mirror.
// ============================================================================
// ============================================================================
// Phase 2 parity — the widget/viewer cell pair mirrors the RUNTIME's
// at-rest committed state (Phase 2 parity, C5). The runtime bakes art from
// the Schmitt machine's committed state, which at rest under forward travel
// is FPSchematicForwardStateAt (band state shifted right by the Schmitt
// margin: the commit fires at edge + 1.5, and the commit key coincides
// exactly with the crossfade alpha = 0.5 key). So the pair's DOMINANT card
// (alpha >= 0.5) is the runtime's cell at every static pose, the two-card
// fade runs in the runtime's OWN parameter-space window (FPSchematicCrossfadeAlpha
// at the pair's band edge), and the pair flips exactly where the runtime
// commits. The old smoothstep-between-pose-centers pair (which at yaw 40
// showed the 3Q cell while the runtime shows the Narrow cell) is retired:
// the viewer now shows the card the runtime displays, not the pose-key
// neighbor. The per-feature sub-threshold collapse still applies
// (CollapseViewStateForFeature: a feature with no Y22 row resolves the
// Narrow state onto the 3Q cell, no Y67 resolves Sliver onto Profile, on
// both halves), so the pair never names a cell the feature has no art file
// for. Left-half keys (states 7-11) are their OWN authored mirrored files,
// so no render-time mirror flag is needed.
// ============================================================================
struct FViewCellPair {
    std::string PrevKey;       // the cell we are leaving (the fade-from card)
    std::string CurKey;        // the cell we are entering (the fade-to card)
    double BlendAlpha = 0.0;   // 0..1 weight toward CurKey (runtime window)
    bool bUnderPlane = false;  // CurKey is the UnderPlane (Bottom) cell
    bool bValid = false;
};

// Mirror ResolveViewCell's FeatureHasYawRow branch: a state whose yaw row the
// feature does not author resolves onto the PARENT view's cell (Narrow/Sliver
// sub-threshold states collapse to 3Q/Profile). Right half: Narrow (1) -> 3Q
// (2), Sliver (3) -> Profile (4). Left half per the SVG token order (state 8
// IS the left Sliver band, state 9 the left Profile band): Narrow_L (11) ->
// 3Q_L (10), Sliver_L (8) -> Profile_L (9) — never the reverse, so a row-less
// feature never names a Y22/Y67 cell it has no art file for.
inline int CollapseViewStateForFeature(const char* Feature, int StateIdx) {
    if (!FeatureHasYawRow(Feature, "Y22") && (StateIdx == 1 || StateIdx == 11))
        return (StateIdx == 1) ? 2 : 10;
    if (!FeatureHasYawRow(Feature, "Y67") && (StateIdx == 3 || StateIdx == 8))
        return (StateIdx == 3) ? 4 : 9;
    return StateIdx;
}

// The pitch band (P00/P45/Pn45) for a ground-state cell, mirroring the
// ResolveViewCell pitch branch thresholds. States 12/13 ignore the band.
inline int PitchBandForDeg(double PitchDeg) {
    if (PitchDeg >= 22.5) return 1;
    if (PitchDeg <= -22.5) return 2;
    return 0;
}

// The DOMINANT cell the runtime's albedo bake shows at a static (yaw, pitch):
// the at-rest committed state (FPSchematicForwardStateAt, which applies the
// +-1.5 pitch trigger too) at the pitch band. Exactly
// TagFeatureCellKey(Feature, FPSchematicForwardStateAt(Yaw,Pitch), Band).
inline std::string ResolveDominantCellKey(const char* Feature, double YawDeg, double PitchDeg) {
    if (!Feature || !Feature[0]) return std::string();
    if (!FeatureIsKnown(Feature)) return std::string();
    const int S = FPSchematic::FPSchematicForwardStateAt(YawDeg, PitchDeg);
    if (S == 12 || S == 13) return FeatureCellKey(Feature, S, 0);
    return FeatureCellKey(Feature, CollapseViewStateForFeature(Feature, S),
                          PitchBandForDeg(PitchDeg));
}

inline bool ResolveViewCellPair(const char* Feature, double YawDeg, double PitchDeg,
                                FViewCellPair& Out) {
    Out = FViewCellPair();
    if (!Feature || !Feature[0]) return false;
    if (!FeatureIsKnown(Feature)) return false;

    const int Band = PitchBandForDeg(PitchDeg);

    // Pitch pole bracket (Top/Bottom): the pair is (ground, pole) once the
    // pitch passes the RAW threshold, faded by the runtime's OWN
    // parameter-space window (FPSchematicCrossfadeAlpha at the pitch
    // boundary with the axis direction sign) — alpha 0.5 exactly at the
    // runtime's pitch-commit key (threshold +- 1.5, so a static pitch inside
    // the Schmitt sliver still reads the ground card as dominant).
    if (PitchDeg > FPSchematic::FPSchematicViewZone::TopPitchThreshold ||
        PitchDeg < FPSchematic::FPSchematicViewZone::BottomPitchThreshold)
    {
        const bool bTop = PitchDeg > 0.0;
        const int Ground = FPSchematic::FPSchematicForwardStateAt(YawDeg, 0.0);
        const double Sign = bTop ? 1.0 : -1.0;
        const double Boundary = Sign
            * FPSchematic::FPSchematicViewZone::TopPitchThreshold;
        Out.PrevKey = FeatureCellKey(Feature, CollapseViewStateForFeature(Feature, Ground),
                                     PitchBandForDeg(PitchDeg));
        Out.CurKey = FeatureCellKey(Feature, bTop ? 12 : 13, 0);
        Out.BlendAlpha = FPSchematic::FPSchematicCrossfadeAlpha(PitchDeg, Boundary, Sign);
        Out.bUnderPlane = !bTop;
        Out.bValid = true;
        return true;
    }

    // Yaw ring: the at-rest committed state S and the two fades around it
    // (into S from SPrev at the left edge, out of S into SNext at the right
    // edge). A fade is LIVE only inside its own parameter-space window
    // (edge + 1.5 trigger, +-0.75 half-width, END-INCLUSIVE: the pair holds
    // alpha 1.0 exactly at the window end and reads as the committed card
    // alone at the window start). The windows never overlap except at the
    // +-180 back wrap, where the INTO fade (SPrev -> S) takes priority over
    // the OUT fade (S -> SNext): the pair then names the card the runtime
    // committed into, and the wrap band itself (exactly -180) is outside
    // both windows, so it degenerates to the committed Back card.
    const int S = FPSchematic::FPSchematicForwardStateAt(YawDeg, PitchDeg);
    const int SPrev = (S + 11) % 12;
    const int SNext = (S + 1) % 12;
    const double EdgePrev = FPSchematic::FPSchematicZoneEdgeForPair(SPrev, S);
    const double EdgeNext = FPSchematic::FPSchematicZoneEdgeForPair(S, SNext);
    const double HalfWin = FPSchematic::FPSchematicCrossfadeHalfWindowDeg;
    const double Schmitt = FPSchematic::FPSchematicCrossfadeSchmittDeg;
    auto DiffToTrigger = [](double Yaw, double Trigger) {
        double Diff = Yaw - Trigger;
        if (Diff > 180.0) Diff -= 360.0;
        else if (Diff < -180.0) Diff += 360.0;
        return Diff;
    };
    const double DiffInto = DiffToTrigger(YawDeg, EdgePrev + Schmitt);
    const double DiffOut = DiffToTrigger(YawDeg, EdgeNext + Schmitt);
    const bool bIntoLive = DiffInto > -HalfWin && DiffInto <= HalfWin;
    const bool bOutLive = DiffOut > -HalfWin && DiffOut <= HalfWin;
    const std::string KeyS = FeatureCellKey(
        Feature, CollapseViewStateForFeature(Feature, S), Band);
    if (bIntoLive)
    {
        // Entering S from SPrev (left-edge window): pair (SPrev, S).
        Out.PrevKey = FeatureCellKey(
            Feature, CollapseViewStateForFeature(Feature, SPrev), Band);
        Out.CurKey = KeyS;
        Out.BlendAlpha = FPSchematic::FPSchematicCrossfadeAlpha(YawDeg, EdgePrev, +1.0);
    }
    else if (bOutLive)
    {
        // Leaving S into SNext (right-edge window): pair (S, SNext).
        Out.PrevKey = KeyS;
        Out.CurKey = FeatureCellKey(
            Feature, CollapseViewStateForFeature(Feature, SNext), Band);
        Out.BlendAlpha = FPSchematic::FPSchematicCrossfadeAlpha(YawDeg, EdgeNext, +1.0);
    }
    else
    {
        // Deep in S (or exactly at a window start): the pair degenerates to
        // S's own cell (dominant = S).
        Out.PrevKey = KeyS;
        Out.CurKey = KeyS;
        Out.BlendAlpha = 0.0;
    }
    Out.bUnderPlane = false;
    Out.bValid = true;
    return true;
}

// ============================================================================
// Phase 5 — runtime albedo bake (pure contract, no UE deps).
// The runtime quads are ONE card per base-preset tag (Eyes/Brows/Mouth/Bangs/
// Nose/Cheeks/Head/Hair/BackHair/Ears), so the per-feature vector cells are
// COMPOSITED per tag into a single albedo image per view state. Composition is
// painter's order (far-side members first, near-side last; Teeth under the
// Mouth interior; the face base under the chin/neck cards). The visibility
// truth (far member dropped at the profile, Nose/Mouth/Teeth folded into the
// FaceBase contour, walk-behind/top reads) is carried by the AUTHORED CELLS
// themselves, so the composite is exactly: resolve each feature's cell through
// CollapseViewStateForFeature + FeatureCellKey, then compose in tag order.
// Rasterization is a pure supersampled even-odd painter (fills + round-capped
// strokes, stroke width normalized by the doc width) emitting straight-alpha
// RGBA8 in UV order (row 0 = top of the card, y-down — the same orientation
// as the widget canvas and the material UVs).
// ============================================================================

struct FTagEntry {
    const char* Tag;
    const char* Features[4];   // painter order; null-terminated
};

inline const FTagEntry* TagFeatureTable() {
    static const FTagEntry kTags[] = {
        { "Eyes",    { "Eye_Far", "Eye_Near", nullptr } },
        { "Brows",   { "Brow_Far", "Brow_Near", nullptr } },
        { "Mouth",   { "Teeth", "Mouth", nullptr } },
        { "Bangs",   { "HairFront", nullptr } },
        { "Nose",    { "Nose", nullptr } },
        { "Cheeks",  { "Cheek_Far", "Cheek_Near", nullptr } },
        { "Head",    { "FaceBase", "Chin", "Neck", nullptr } },
        { "Hair",    { "HairBack", nullptr } },
        { "BackHair",{ "BackHair", nullptr } },
        { "Ears",    { "Ear_Far", "Ear_Near", nullptr } },
    };
    return kTags;
}

inline int TagCount() { return 10; }

inline int TagFeatureCount(const char* Tag) {
    for (int i = 0; i < TagCount(); ++i) {
        const FTagEntry& t = TagFeatureTable()[i];
        if (strcmp(t.Tag, Tag) == 0) {
            int n = 0;
            while (n < 4 && t.Features[n]) ++n;
            return n;
        }
    }
    return 0;
}

inline const char* TagFeatureAt(const char* Tag, int Index) {
    for (int i = 0; i < TagCount(); ++i) {
        const FTagEntry& t = TagFeatureTable()[i];
        if (strcmp(t.Tag, Tag) == 0)
            return (Index >= 0 && Index < 4) ? t.Features[Index] : nullptr;
    }
    return nullptr;
}

// Tag-level albedo slot key — the SAME cell-key format as feature cells, so a
// per-tag composite texture shares the key namespace of the per-feature cells
// (states 12/13 map to Top Y00_P90 / UnderPlane Y00_Pn45). Features inside
// the tag collapse per-feature; the tag key itself never collapses.
inline std::string ResolveVectorAlbedoKey(const char* Tag, int StateIdx, int PitchBand) {
    return FeatureCellKey(Tag, StateIdx, PitchBand);
}

// Per-feature cell key INSIDE a tag composite: resolves the feature's own
// yaw-row collapse for the tag's state (a feature that does not author
// Narrow/Sliver rows renders the 3Q/Profile cell instead).
inline std::string TagFeatureCellKey(const char* Feature, int StateIdx, int PitchBand) {
    return FeatureCellKey(Feature, CollapseViewStateForFeature(Feature, StateIdx), PitchBand);
}

// Concatenate several cell documents into ONE painter-ordered document.
// Width/Height come from the first non-empty doc.
inline FDocument ComposeDocuments(const std::vector<const FDocument*>& Docs) {
    FDocument Out;
    for (const FDocument* D : Docs) {
        if (!D || D->Paths.empty()) continue;
        if (Out.Paths.empty()) {
            Out.Width = D->Width;
            Out.Height = D->Height;
            Out.VbX = D->VbX; Out.VbY = D->VbY;
            Out.VbW = D->VbW; Out.VbH = D->VbH;
        }
        Out.Paths.insert(Out.Paths.end(), D->Paths.begin(), D->Paths.end());
    }
    return Out;
}

namespace RasterDetail {

struct FPureContour {
    std::vector<FPoint> Pts;
    bool bClosed = false;
};

struct FPureLayer {
    std::vector<FPureContour> Contours;
    bool bHasFill = false;
    double FillR = 0.0, FillG = 0.0, FillB = 0.0, FillA = 0.0;
    bool bHasStroke = false;
    double StrokeR = 0.0, StrokeG = 0.0, StrokeB = 0.0, StrokeA = 0.0;
    double StrokeWidth = 0.0;   // UV units (normalized by the doc width)
    double MinX = 2.0, MaxX = -1.0, MinY = 2.0, MaxY = -1.0;  // stroke-padded bbox
};

inline void PureFlatten(const FDocument& Doc, std::vector<FPureLayer>& Out) {
    constexpr int kQuadSteps = 8;
    constexpr int kCubicSteps = 16;
    const double DocW = (Doc.Width > 0.0) ? Doc.Width : Doc.VbW;
    Out.clear();
    Out.reserve(Doc.Paths.size());
    for (const FPath& P : Doc.Paths) {
        FPureLayer L;
        L.bHasFill = P.bHasFill;
        L.FillR = P.FillR; L.FillG = P.FillG; L.FillB = P.FillB; L.FillA = P.FillA;
        L.bHasStroke = P.bHasStroke;
        L.StrokeR = P.StrokeR; L.StrokeG = P.StrokeG; L.StrokeB = P.StrokeB;
        L.StrokeA = P.StrokeA;
        L.StrokeWidth = (DocW > 0.0) ? (P.StrokeWidth / DocW) : 0.0;
        double CurX = 0.0, CurY = 0.0;
        const size_t n = P.Cmds.size();
        const size_t kPts = P.Pts.size();
        auto Push = [&](double x, double y) {
            FPureContour& C = L.Contours.back();
            C.Pts.push_back(FPoint(x, y));
            if (x < L.MinX) L.MinX = x;
            if (x > L.MaxX) L.MaxX = x;
            if (y < L.MinY) L.MinY = y;
            if (y > L.MaxY) L.MaxY = y;
            CurX = x; CurY = y;
        };
        for (size_t i = 0; i < n && i < kPts; ++i) {
            switch (P.Cmds[i]) {
            case ECmd::MoveTo: {
                L.Contours.emplace_back();
                L.Contours.back().bClosed = P.bClosed;
                Push(P.Pts[i].X, P.Pts[i].Y);
                break;
            }
            case ECmd::LineTo:
                if (L.Contours.empty()) { L.Contours.emplace_back(); L.Contours.back().bClosed = P.bClosed; }
                Push(P.Pts[i].X, P.Pts[i].Y);
                break;
            case ECmd::QuadTo: {
                if (i + 1 >= n || i + 1 >= kPts) break;
                if (L.Contours.empty()) { L.Contours.emplace_back(); L.Contours.back().bClosed = P.bClosed; }
                const FPoint Ctl = P.Pts[i];
                const FPoint End = P.Pts[i + 1];
                const FPoint P0(CurX, CurY);
                for (int s = 1; s <= kQuadSteps; ++s) {
                    const double t = (double)s / (double)kQuadSteps;
                    const double u = 1.0 - t;
                    Push(u * u * P0.X + 2.0 * u * t * Ctl.X + t * t * End.X,
                         u * u * P0.Y + 2.0 * u * t * Ctl.Y + t * t * End.Y);
                }
                ++i;
                break;
            }
            case ECmd::CubicTo: {
                if (i + 2 >= n || i + 2 >= kPts) break;
                if (L.Contours.empty()) { L.Contours.emplace_back(); L.Contours.back().bClosed = P.bClosed; }
                const FPoint C1 = P.Pts[i];
                const FPoint C2 = P.Pts[i + 1];
                const FPoint End = P.Pts[i + 2];
                const FPoint P0(CurX, CurY);
                for (int s = 1; s <= kCubicSteps; ++s) {
                    const double t = (double)s / (double)kCubicSteps;
                    const double u = 1.0 - t;
                    Push(u * u * u * P0.X + 3.0 * u * u * t * C1.X + 3.0 * u * t * t * C2.X + t * t * t * End.X,
                         u * u * u * P0.Y + 3.0 * u * u * t * C1.Y + 3.0 * u * t * t * C2.Y + t * t * t * End.Y);
                }
                i += 2;
                break;
            }
            default:
                break;   // Close: closure is implicit (contours wrap); no action
            }
        }
        if (L.bHasStroke && L.MaxX >= L.MinX) {
            const double w = L.StrokeWidth * 0.5 + 1e-9;
            L.MinX -= w; L.MaxX += w; L.MinY -= w; L.MaxY += w;
        }
        Out.push_back(L);
    }
}

inline bool PurePointInPath(double X, double Y, const FPureLayer& L) {
    if (X < L.MinX || X > L.MaxX || Y < L.MinY || Y > L.MaxY) return false;
    bool Inside = false;
    for (const FPureContour& C : L.Contours) {
        const size_t m = C.Pts.size();
        if (m < 3) continue;
        for (size_t j = 0; j < m; ++j) {
            const FPoint& A = C.Pts[j];
            const FPoint& B = C.Pts[(j + 1) % m];
            if ((A.Y > Y) != (B.Y > Y)) {
                const double Xc = A.X + (Y - A.Y) * (B.X - A.X) / (B.Y - A.Y);
                if (Xc > X) Inside = !Inside;
            }
        }
    }
    return Inside;
}

inline double PureSegDist(double X, double Y, const FPoint& A, const FPoint& B) {
    const double dx = B.X - A.X, dy = B.Y - A.Y;
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 1e-18) {
        const double ex = X - A.X, ey = Y - A.Y;
        return std::sqrt(ex * ex + ey * ey);
    }
    double t = ((X - A.X) * dx + (Y - A.Y) * dy) / len2;
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    const double px = A.X + t * dx - X, py = A.Y + t * dy - Y;
    return std::sqrt(px * px + py * py);
}

inline double PurePointToPath(double X, double Y, const FPureLayer& L) {
    double Best = 1e18;
    for (const FPureContour& C : L.Contours) {
        const size_t m = C.Pts.size();
        if (m < 2) continue;
        const size_t end = C.bClosed ? m : m - 1;
        for (size_t j = 0; j < end; ++j) {
            const double d = PureSegDist(X, Y, C.Pts[j], C.Pts[(j + 1) % m]);
            if (d < Best) Best = d;
        }
        if (!C.bClosed) {
            for (size_t j = 0; j < m; j += (m - 1)) {   // round caps on open contours
                const double dx = X - C.Pts[j].X, dy = Y - C.Pts[j].Y;
                const double d = std::sqrt(dx * dx + dy * dy);
                if (d < Best) Best = d;
            }
        }
    }
    return Best;
}

struct FPureSample {
    double R = 0.0, G = 0.0, B = 0.0, A = 0.0;
};

inline void PureOver(FPureSample& Dst, double R, double G, double B, double A) {
    if (A <= 0.0) return;
    const double oA = A + Dst.A * (1.0 - A);
    if (oA <= 0.0) { Dst.A = A; Dst.R = R; Dst.G = G; Dst.B = B; return; }
    Dst.R = (R * A + Dst.R * Dst.A * (1.0 - A)) / oA;
    Dst.G = (G * A + Dst.G * Dst.A * (1.0 - A)) / oA;
    Dst.B = (B * A + Dst.B * Dst.A * (1.0 - A)) / oA;
    Dst.A = oA;
}

} // namespace RasterDetail

// Pure supersampled painter rasterizer: RGBA8 (straight alpha), UV order
// (row 0 = top), even-odd fills + round-capped strokes per path in document
// order. Size = texel side, SS = supersample count per side.
inline void RasterizeDocument(const FDocument& Doc, int Size, int SS,
                              std::vector<uint8_t>& RGBA) {
    RGBA.assign((size_t)Size * (size_t)Size * 4, 0);
    if (Size <= 0 || SS <= 0) return;
    std::vector<RasterDetail::FPureLayer> Layers;
    RasterDetail::PureFlatten(Doc, Layers);
    const double Inv = 1.0 / (double)Size;
    const double SInv = 1.0 / (double)SS;
    const double AvgInv = 1.0 / (double)(SS * SS);
    for (int Y = 0; Y < Size; ++Y) {
        for (int X = 0; X < Size; ++X) {
            double AccR = 0.0, AccG = 0.0, AccB = 0.0, AccA = 0.0;
            for (int sy = 0; sy < SS; ++sy) {
                for (int sx = 0; sx < SS; ++sx) {
                    const double Xf = ((double)X + ((double)sx + 0.5) * SInv) * Inv;
                    const double Yf = ((double)Y + ((double)sy + 0.5) * SInv) * Inv;
                    RasterDetail::FPureSample S;
                    for (const RasterDetail::FPureLayer& L : Layers) {
                        if (L.bHasFill && RasterDetail::PurePointInPath(Xf, Yf, L)) {
                            RasterDetail::PureOver(S, L.FillR, L.FillG, L.FillB, L.FillA);
                        }
                        if (L.bHasStroke && L.StrokeWidth > 0.0 &&
                            RasterDetail::PurePointToPath(Xf, Yf, L) <= L.StrokeWidth * 0.5) {
                            RasterDetail::PureOver(S, L.StrokeR, L.StrokeG, L.StrokeB, L.StrokeA);
                        }
                    }
                    AccR += S.R; AccG += S.G; AccB += S.B; AccA += S.A;
                }
            }
            const size_t O = ((size_t)Y * (size_t)Size + (size_t)X) * 4;
            RGBA[O + 0] = (uint8_t)(AccR * AvgInv * 255.0 + 0.5);
            RGBA[O + 1] = (uint8_t)(AccG * AvgInv * 255.0 + 0.5);
            RGBA[O + 2] = (uint8_t)(AccB * AvgInv * 255.0 + 0.5);
            RGBA[O + 3] = (uint8_t)(AccA * AvgInv * 255.0 + 0.5);
        }
    }
}

// Tag composite convenience: compose the resolved feature docs in painter
// order and rasterize. Returns false when no path was present (the caller
// resolved an empty cell — e.g. a feature dropped at this state).
inline bool RasterizeAlbedoForTag(const std::vector<const FDocument*>& FeatureDocs,
                                  int Size, int SS, std::vector<uint8_t>& RGBA) {
    const FDocument Comp = ComposeDocuments(FeatureDocs);
    if (Comp.Paths.empty()) {
        RGBA.assign((size_t)Size * (size_t)Size * 4, 0);
        return false;
    }
    RasterizeDocument(Comp, Size, SS, RGBA);
    return true;
}

} // namespace FPSvg
