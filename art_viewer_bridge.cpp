// ============================================================================
// art_viewer_bridge.cpp - FaceParallax art-viewer BRIDGE (the system's truth).
//
// The viewer never re-implements a single rule. This tiny pure C++17 program
// #includes the EXACT canonical contract headers the runtime and the math
// test harness use (FaceParallaxSchematic.h + FaceParallaxSvgParse.h) and
// prints, as one JSON document on stdout, every value the art viewer needs:
//
//   states     - token / yaw token / center yaw+pitch / walk-behind flag for
//                every EFaceAngleState-mirror state (via StateTokenForIndex,
//                YawTokenForIndex, FPSchematicStateCenterYaw/Pitch,
//                FPSchematicStateIsWalkBehind).
//   features   - the canonical 17 part<->feature-token pairs (FeatureTable).
//   visibility - per (state x part): FPSchematicLayerVisibleInState +
//                FPSchematicLayerOrderInState (the exact per-part Z-order the
//                editor canvas paints, incl. the BackHair state-6 promote).
//   cells      - per (state x feature): the RESOLVED cell key the runtime
//                bake and the widget's dominant card use =
//                FeatureCellKey(Feature, CollapseViewStateForFeature(F,S), 0)
//                (band 0 = pitch 0; states 12/13 are pitch-explicit Top /
//                UnderPlane keys). Left-half states resolve the system's own
//                authored mirrored cells (e.g. Sliver_L_Y67 for state 8).
//
// When the system is updated (headers change) the viewer recompiles this
// bridge and every value comes from the NEW system - nothing is mirrored.
//
// Compile (same compiler + flags as the math test harness):
//   g++ -std=c++17 -O2 -o art_viewer_bridge.exe art_viewer_bridge.cpp
// Run:  art_viewer_bridge.exe  -> JSON on stdout
// ============================================================================

#include <cstdio>
#include <cstring>
#include <string>

#include "FaceParallaxSchematic.h"
#include "FaceParallaxSvgParse.h"

namespace {

// Minimal JSON string escaping (the emitted values are plain alnum tokens,
// kept for safety).
void EmitJsonString(const char* S)
{
    fputc('"', stdout);
    for (const char* p = S; p && *p; ++p)
    {
        const unsigned char c = (unsigned char)*p;
        switch (c)
        {
        case 34: fputc(34, stdout); break; /* '"' */
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout);  break;
        case '\r': fputs("\\r", stdout);  break;
        case '\t': fputs("\\t", stdout);  break;
        default:
            if (c < 0x20) fprintf(stdout, "\\u%04x", c);
            else fputc(c, stdout);
            break;
        }
    }
    fputc('"', stdout);
}

} // namespace

int main()
{
    using namespace FPSchematic;
    using namespace FPSvg;

    const FFeatureEntry* Table = FeatureTable();
    const int NumFeatures = FeatureTableCount();

    // --- states -----------------------------------------------------------
    fputs("{\n\"states\":[", stdout);
    bool first = true;
    for (int S = 0; const char* Token = StateTokenForIndex(S); ++S)
    {
        if (!first) fputs(",", stdout);
        first = false;
        fprintf(stdout,
                "{\"idx\":%d,\"token\":", S);
        EmitJsonString(Token);
        fprintf(stdout, ",\"yaw\":");
        EmitJsonString(YawTokenForIndex(S));
        fprintf(stdout, ",\"centerYaw\":%.6f,\"centerPitch\":%.6f,"
                        "\"walkBehind\":%s}",
                FPSchematicStateCenterYaw(S), FPSchematicStateCenterPitch(S),
                FPSchematicStateIsWalkBehind(S) ? "true" : "false");
    }
    fputs("],\n\"features\":[", stdout);
    first = true;
    for (int i = 0; i < NumFeatures; ++i)
    {
        if (!first) fputs(",", stdout);
        first = false;
        fputs("{\"part\":", stdout);
        EmitJsonString(Table[i].Part);
        fputs(",\"feature\":", stdout);
        EmitJsonString(Table[i].Feature);
        fputc(125, stdout); /* close-object */
    }

    // --- per-state x per-part visibility + Z-order -------------------------
    fputs("],\n\"visibility\":[", stdout);
    first = true;
    for (int S = 0; StateTokenForIndex(S); ++S)
    {
        for (int i = 0; i < NumFeatures; ++i)
        {
            if (!first) fputs(",", stdout);
            first = false;
            fprintf(stdout,
                    "{\"state\":%d,\"part\":", S);
            EmitJsonString(Table[i].Part);
            fprintf(stdout, ",\"visible\":%s,\"order\":%d}",
                    FPSchematicLayerVisibleInState(S, Table[i].Part)
                        ? "true" : "false",
                    FPSchematicLayerOrderInState(S, Table[i].Part));
        }
    }

    // --- per-state x per-feature resolved cell keys ------------------------
    fputs("],\n\"cells\":[", stdout);
    first = true;
    for (int S = 0; StateTokenForIndex(S); ++S)
    {
        for (int i = 0; i < NumFeatures; ++i)
        {
            if (!first) fputs(",", stdout);
            first = false;
            const int Collapsed = CollapseViewStateForFeature(Table[i].Feature, S);
            const std::string Key = FeatureCellKey(Table[i].Feature, Collapsed, 0);
            fprintf(stdout,
                    "{\"state\":%d,\"feature\":", S);
            EmitJsonString(Table[i].Feature);
            fputs(",\"key\":", stdout);
            EmitJsonString(Key.c_str());
    fputc(125, stdout); /* close-object */
        }
    }

    fputs("]\n}\n", stdout);
    return 0;
}
