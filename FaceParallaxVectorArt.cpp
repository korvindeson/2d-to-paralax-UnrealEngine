#include "FaceParallaxVectorArt.h"

namespace
{
    void ConvertPath(const FPSvg::FPath& Src, FFaceVectorPath& Dst)
    {
        Dst.Cmds.SetNum((int32)Src.Cmds.size());
        for (int32 j = 0; j < Dst.Cmds.Num(); ++j)
        {
            FFaceVectorCmdPt& Pt = Dst.Cmds[j];
            switch (Src.Cmds[(size_t)j])
            {
                case FPSvg::ECmd::MoveTo:  Pt.Cmd = EFaceVectorCmd::MoveTo;  break;
                case FPSvg::ECmd::LineTo:  Pt.Cmd = EFaceVectorCmd::LineTo;  break;
                case FPSvg::ECmd::QuadTo:  Pt.Cmd = EFaceVectorCmd::QuadTo;  break;
                case FPSvg::ECmd::CubicTo: Pt.Cmd = EFaceVectorCmd::CubicTo; break;
                default:                   Pt.Cmd = EFaceVectorCmd::Close;   break;
            }
            Pt.P = FVector2D((float)Src.Pts[(size_t)j].X, (float)Src.Pts[(size_t)j].Y);
        }
        Dst.bClosed = Src.bClosed;
        Dst.bHasFill = Src.bHasFill;
        Dst.Fill = FLinearColor((float)Src.FillR, (float)Src.FillG, (float)Src.FillB, (float)Src.FillA);
        Dst.bHasStroke = Src.bHasStroke;
        Dst.Stroke = FLinearColor((float)Src.StrokeR, (float)Src.StrokeG, (float)Src.StrokeB, (float)Src.StrokeA);
        Dst.StrokeWidth = (float)Src.StrokeWidth;
        Dst.GroupId = UTF8_TO_TCHAR(Src.GroupId.c_str());
    }
}

void FFaceVectorArtPaths::ConvertFrom(const FPSvg::FDocument& Doc)
{
    Width = (float)Doc.Width;
    Height = (float)Doc.Height;
    Paths.SetNum((int32)Doc.Paths.size());
    for (int32 i = 0; i < Paths.Num(); ++i)
    {
        ConvertPath(Doc.Paths[(size_t)i], Paths[i]);
    }
}

void FFaceVectorArtPaths::ConvertTo(FPSvg::FDocument& Doc) const
{
    Doc.Width = (double)Width;
    Doc.Height = (double)Height;
    Doc.Paths.clear();
    Doc.Paths.reserve((size_t)Paths.Num());
    for (const FFaceVectorPath& Src : Paths)
    {
        FPSvg::FPath Dst;
        Dst.Cmds.reserve((size_t)Src.Cmds.Num());
        Dst.Pts.reserve((size_t)Src.Cmds.Num());
        for (const FFaceVectorCmdPt& Pt : Src.Cmds)
        {
            switch (Pt.Cmd)
            {
                case EFaceVectorCmd::MoveTo:  Dst.Cmds.push_back(FPSvg::ECmd::MoveTo);  break;
                case EFaceVectorCmd::LineTo:  Dst.Cmds.push_back(FPSvg::ECmd::LineTo);  break;
                case EFaceVectorCmd::QuadTo:  Dst.Cmds.push_back(FPSvg::ECmd::QuadTo);  break;
                case EFaceVectorCmd::CubicTo: Dst.Cmds.push_back(FPSvg::ECmd::CubicTo); break;
                default:                      Dst.Cmds.push_back(FPSvg::ECmd::Close);   break;
            }
            Dst.Pts.emplace_back((double)Pt.P.X, (double)Pt.P.Y);
        }
        Dst.bClosed = Src.bClosed;
        Dst.bHasFill = Src.bHasFill;
        Dst.FillR = (double)Src.Fill.R; Dst.FillG = (double)Src.Fill.G;
        Dst.FillB = (double)Src.Fill.B; Dst.FillA = (double)Src.Fill.A;
        Dst.bHasStroke = Src.bHasStroke;
        Dst.StrokeR = (double)Src.Stroke.R; Dst.StrokeG = (double)Src.Stroke.G;
        Dst.StrokeB = (double)Src.Stroke.B; Dst.StrokeA = (double)Src.Stroke.A;
        Dst.StrokeWidth = (double)Src.StrokeWidth;
        Doc.Paths.push_back(Dst);
    }
}

bool UFaceVectorArt::ImportFromGridSvg(const FString& SvgText)
{
    LastImportError.Reset();
    const FTCHARToUTF8 Utf8(*SvgText);
    FPSvg::FDocument Doc;
    if (!FPSvg::ParseDocument(Utf8.Get(), (size_t)Utf8.Length(), Doc))
    {
        LastImportError = FString(UTF8_TO_TCHAR(Doc.Error.c_str()));
        return false;
    }

    TMap<FString, FFaceVectorArtPaths> NewCells;
    for (const FPSvg::FPath& Src : Doc.Paths)
    {
        const FString Group = UTF8_TO_TCHAR(Src.GroupId.c_str());
        if (Group.IsEmpty())
        {
            continue;
        }
        FFaceVectorArtPaths& Cell = NewCells.FindOrAdd(Group);
        Cell.Width = (float)Doc.Width;
        Cell.Height = (float)Doc.Height;
        FFaceVectorPath& Dst = Cell.Paths.AddDefaulted_GetRef();
        ConvertPath(Src, Dst);
    }

    if (NewCells.Num() == 0)
    {
        LastImportError = TEXT("Grid SVG contains no <g id=\"<cell key>\"> groups");
        return false;
    }

    // Derive the feature token from the first cell key (all keys in a grid
    // file share the feature prefix).
    FString FirstKey;
    for (const TPair<FString, FFaceVectorArtPaths>& Pair : NewCells)
    {
        FirstKey = Pair.Key;
        break;
    }
    std::string CellFeature, StateToken, YawToken, PitchToken;
    const std::string FirstKeyUtf8 = TCHAR_TO_UTF8(*FirstKey);
    if (FPSvg::ParseCellKey(FirstKeyUtf8.c_str(), CellFeature, StateToken, YawToken, PitchToken))
    {
        FeatureToken = FName(CellFeature.c_str());
    }

    Cells = MoveTemp(NewCells);
    ArtSchemaVersion = CurrentVectorArtSchema;
    return true;
}

void UFaceVectorArt::GetCellKeys(TArray<FString>& OutCellKeys) const
{
    OutCellKeys.Reset();
    OutCellKeys.Reserve(Cells.Num());
    for (const TPair<FString, FFaceVectorArtPaths>& Pair : Cells)
    {
        OutCellKeys.Add(Pair.Key);
    }
}

int32 UFaceVectorArt::GetCellCount() const
{
    return Cells.Num();
}

void UFaceVectorArt::GetCellStats(const FString& CellKey, int32& OutPathCount, int32& OutCubicCount) const
{
    OutPathCount = 0;
    OutCubicCount = 0;
    const FFaceVectorArtPaths* Cell = Cells.Find(CellKey);
    if (!Cell)
    {
        return;
    }
    OutPathCount = Cell->Paths.Num();
    for (const FFaceVectorPath& Path : Cell->Paths)
    {
        for (const FFaceVectorCmdPt& Pt : Path.Cmds)
        {
            if (Pt.Cmd == EFaceVectorCmd::CubicTo)
            {
                ++OutCubicCount;
            }
        }
    }
}
