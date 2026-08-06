#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FaceParallaxSvgParse.h"
#include "FaceParallaxVectorArt.generated.h"

// Vector path command types, mirroring FPSvg::ECmd (pure header).
UENUM(BlueprintType)
enum class EFaceVectorCmd : uint8
{
    MoveTo   UMETA(DisplayName = "Move To"),
    LineTo   UMETA(DisplayName = "Line To"),
    QuadTo   UMETA(DisplayName = "Quadratic To"),
    CubicTo  UMETA(DisplayName = "Cubic To"),
    Close    UMETA(DisplayName = "Close")
};

USTRUCT(BlueprintType)
struct FACEPARALLAX_API FFaceVectorCmdPt
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EFaceVectorCmd Cmd = EFaceVectorCmd::MoveTo;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D P = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct FACEPARALLAX_API FFaceVectorPath
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Path")
    TArray<FFaceVectorCmdPt> Cmds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Path")
    bool bClosed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Path")
    bool bHasFill = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Path")
    FLinearColor Fill = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Path")
    bool bHasStroke = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Path")
    FLinearColor Stroke = FLinearColor::Black;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Path")
    float StrokeWidth = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Path")
    FString GroupId;
};

// A parsed SVG document in viewBox-normalized 0..1 space (y-down).
USTRUCT(BlueprintType)
struct FACEPARALLAX_API FFaceVectorArtPaths
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art")
    float Width = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art")
    float Height = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art")
    TArray<FFaceVectorPath> Paths;

    bool IsValid() const { return Paths.Num() > 0; }

    // Convert a parsed pure SVG document into the UE-side structure.
    void ConvertFrom(const FPSvg::FDocument& Doc);

    // Inverse of ConvertFrom: rebuild the pure document from the UE-side
    // structure (used by the runtime albedo bake, which rasterizes through
    // the pinned FPSvg::RasterizeDocument contract).
    void ConvertTo(FPSvg::FDocument& Doc) const;
};

// Per-feature vector art library asset. Cells are keyed by the full guide
// token (e.g. "Eye_Near_Sliver_L_Y67_P45", "Mouth_A_Y00_P00").
UCLASS(BlueprintType, AutoExpandCategories = ("Vector Art"))
class FACEPARALLAX_API UFaceVectorArt : public UDataAsset
{
    GENERATED_BODY()

public:
    // Bumped whenever the CELL ENCODING changes (path command layout,
    // FFaceVectorCmdPt semantics, ConvertPath/ImportFromGridSvg behavior).
    // deploy.py re-imports any asset whose stamped version differs, so a
    // parser/encoding fix can never leave stale flattened cells on disk.
    static constexpr int32 CurrentVectorArtSchema = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art",
        meta = (DisplayName = "Feature Token"))
    FName FeatureToken;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art",
        meta = (DisplayName = "Cells (token → vector paths)"))
    TMap<FString, FFaceVectorArtPaths> Cells;

    // The cell-encoding schema this asset's Cells were serialized with
    // (stamped by ImportFromGridSvg). Mismatch vs CurrentVectorArtSchema
    // means the content predates the current parser/encoding and MUST be
    // re-imported before it is consumed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vector Art|Import",
        meta = (DisplayName = "Art Schema Version"))
    int32 ArtSchemaVersion = 0;

    // Last parse/import failure detail (empty when the last import succeeded).
    UPROPERTY(Transient, EditAnywhere, Category = "Vector Art|Import",
        meta = (DisplayName = "Last Import Error"))
    FString LastImportError;

    // Rebuild Cells from a per-feature GRID SVG (one <g id="<cell key>"> per
    // cell, as emitted by generate_art.py into Art/_grids/). Derives
    // FeatureToken from the first cell key. Returns false with LastImportError
    // set when the SVG fails to parse or contains no cell groups.
    UFUNCTION(BlueprintCallable, Category = "Vector Art")
    bool ImportFromGridSvg(const FString& SvgText);

    UFUNCTION(BlueprintCallable, Category = "Vector Art")
    void GetCellKeys(TArray<FString>& OutCellKeys) const;

    UFUNCTION(BlueprintCallable, Category = "Vector Art")
    int32 GetCellCount() const;

    // Content probe for the deploy zero-gap pass: path count and CubicTo
    // command count for one cell (0/0 when the key is missing or empty).
    // A cubic count of 0 on a curve-bearing cell means the cell was
    // serialized with a flattened encoding — the stale-data signal.
    UFUNCTION(BlueprintCallable, Category = "Vector Art")
    void GetCellStats(const FString& CellKey, int32& OutPathCount, int32& OutCubicCount) const;

    const FFaceVectorArtPaths* FindCell(const FString& CellKey) const
    {
        return Cells.Find(CellKey);
    }
};
