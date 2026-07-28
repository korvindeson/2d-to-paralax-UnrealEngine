#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "FaceParallaxTypes.generated.h"

UENUM(BlueprintType)
enum class EFaceAngleState : uint8
{
    Front               UMETA(DisplayName = "Front View"),
    ThreeQuarterRight   UMETA(DisplayName = "3/4 Right"),
    RightProfile        UMETA(DisplayName = "Right Profile"),
    BackRight           UMETA(DisplayName = "Back Right"),
    Back                UMETA(DisplayName = "Back View"),
    BackLeft            UMETA(DisplayName = "Back Left"),
    LeftProfile         UMETA(DisplayName = "Left Profile"),
    ThreeQuarterLeft    UMETA(DisplayName = "3/4 Left"),
    Top                 UMETA(DisplayName = "Top View"),
    Bottom              UMETA(DisplayName = "Bottom View")
};

USTRUCT(BlueprintType)
struct FFaceTextureSet
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TObjectPtr<UTexture2D> Albedo = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TObjectPtr<UTexture2D> Normal = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TObjectPtr<UTexture2D> Depth = nullptr;

    UPROPERTY()
    int32 SourceTexWidth = 0;

    UPROPERTY()
    int32 SourceTexHeight = 0;

    bool IsValid() const
    {
        return Albedo != nullptr;
    }

    bool IsFullyAssigned() const
    {
        return Albedo != nullptr && Normal != nullptr && Depth != nullptr;
    }

    void CaptureSourceSize()
    {
        if (Albedo && Albedo->GetResource())
        {
            SourceTexWidth = Albedo->GetSizeX();
            SourceTexHeight = Albedo->GetSizeY();
        }
    }
};

USTRUCT(BlueprintType)
struct FFaceSwooshArt
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Swoosh Art")
    TArray<FFaceTextureSet> Frames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Swoosh Art")
    FFaceArtTransform OverrideTransform;
};

USTRUCT(BlueprintType)
struct FFaceArtTransform
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Transform")
    FVector2D Position = FVector2D(0.0f, 0.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Transform",
        meta = (ClampMin = "0.01", ClampMax = "100.0"))
    FVector2D Scale = FVector2D(1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art Transform",
        meta = (ClampMin = "-360.0", ClampMax = "360.0"))
    float Rotation = 0.0f;

    FFaceArtTransform GetCombined(const FFaceArtTransform& Override) const
    {
        FFaceArtTransform Result;
        Result.Position = Position + Override.Position;
        Result.Scale = Scale * Override.Scale;
        Result.Rotation = Rotation + Override.Rotation;
        return Result;
    }

    bool IsIdentity() const
    {
        return Position.IsNearlyZero()
            && FMath::IsNearlyEqual(Scale.X, 1.0f)
            && FMath::IsNearlyEqual(Scale.Y, 1.0f)
            && FMath::IsNearlyEqual(Rotation, 0.0f);
    }
};

UENUM(BlueprintType)
enum class ESwooshPhase : uint8
{
    Inactive        UMETA(DisplayName = "Inactive"),
    Smearing        UMETA(DisplayName = "Smearing"),
    BlendingOut     UMETA(DisplayName = "Blending Out")
};

UENUM(BlueprintType)
enum class EExpression : uint8
{
    Neutral     UMETA(DisplayName = "Neutral"),
    Smile       UMETA(DisplayName = "Smile"),
    Frown       UMETA(DisplayName = "Frown")
};

UENUM(BlueprintType)
enum class EViseme : uint8
{
    Uh      UMETA(DisplayName = "Uh"),
    Ah      UMETA(DisplayName = "Ah"),
    Ee      UMETA(DisplayName = "Ee"),
    D       UMETA(DisplayName = "D"),
    S       UMETA(DisplayName = "S"),
    F       UMETA(DisplayName = "F"),
    M       UMETA(DisplayName = "M"),
    L       UMETA(DisplayName = "L"),
    WOO     UMETA(DisplayName = "WO-o"),
    Oh      UMETA(DisplayName = "Oh"),
    R       UMETA(DisplayName = "R")
};

USTRUCT(BlueprintType)
struct FFaceArtSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot")
    FFaceTextureSet Textures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot")
    FFaceArtTransform CanonicalTransform;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot")
    TMap<TEnumAsByte<EFaceAngleState>, FFaceArtTransform> ViewOverrides;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Expression Textures"))
    TMap<TEnumAsByte<EExpression>, FFaceTextureSet> ExpressionTextures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Blink Animation Frames"))
    TArray<FFaceTextureSet> BlinkFrames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Viseme Frame Sets (Expression > Viseme > Frames)"))
    TMap<TEnumAsByte<EExpression>, TMap<TEnumAsByte<EViseme>, TArray<FFaceTextureSet>>> VisemeFrameSets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Swoosh Frames (per-target-state)"))
    TMap<TEnumAsByte<EFaceAngleState>, FFaceSwooshArt> SwooshToState;

    FFaceArtTransform GetEffectiveTransform(EFaceAngleState ForState) const
    {
        const FFaceArtTransform* Override = ViewOverrides.Find(ForState);
        if (Override)
        {
            return CanonicalTransform.GetCombined(*Override);
        }
        return CanonicalTransform;
    }

    bool HasOverride(EFaceAngleState ForState) const
    {
        return ViewOverrides.Contains(ForState);
    }

    void SetOverride(EFaceAngleState ForState, const FFaceArtTransform& Override)
    {
        ViewOverrides.Add(ForState, Override);
    }

    void ClearOverride(EFaceAngleState ForState)
    {
        ViewOverrides.Remove(ForState);
    }

    void ClearAllOverrides()
    {
        ViewOverrides.Empty();
    }
};

USTRUCT(BlueprintType)
struct FFaceViewStateLayerSet
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face View State")
    TMap<FName, FFaceArtSlot> Layers;
};
