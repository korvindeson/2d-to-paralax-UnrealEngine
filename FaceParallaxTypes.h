#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "FaceParallaxTypes.generated.h"

#ifndef MYPROJECT_API
#define MYPROJECT_API
#endif

UENUM(BlueprintType)
enum class ECameraSource : uint8
{
    PlayerCamera0    UMETA(DisplayName = "Player Camera 0"),
    PlayerCamera1    UMETA(DisplayName = "Player Camera 1"),
    SpecifiedActor   UMETA(DisplayName = "Specified Actor"),
    SequencerCamera  UMETA(DisplayName = "Sequencer Camera"),
    PreviewActor     UMETA(DisplayName = "Preview Actor"),
    Custom           UMETA(DisplayName = "Custom")
};

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

    // Hard references (synchronous, always loaded)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TObjectPtr<UTexture2D> Albedo = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TObjectPtr<UTexture2D> Normal = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TObjectPtr<UTexture2D> Depth = nullptr;

    // Soft references (for async loading / re-import)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TSoftObjectPtr<UTexture2D> SoftAlbedo;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TSoftObjectPtr<UTexture2D> SoftNormal;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Texture Set")
    TSoftObjectPtr<UTexture2D> SoftDepth;

    UPROPERTY()
    int32 SourceTexWidth = 0;

    UPROPERTY()
    int32 SourceTexHeight = 0;

    UPROPERTY()
    FDateTime LastImportTimestamp;

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

    // Synchronize soft refs from hard refs
    void SyncSoftRefs()
    {
        if (Albedo) SoftAlbedo = Albedo;
        if (Normal) SoftNormal = Normal;
        if (Depth) SoftDepth = Depth;
    }

    // Resolve hard refs from soft refs
    void LoadFromSoftRefs()
    {
        if (SoftAlbedo.IsValid()) Albedo = SoftAlbedo.Get();
        if (SoftNormal.IsValid()) Normal = SoftNormal.Get();
        if (SoftDepth.IsValid()) Depth = SoftDepth.Get();
    }
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

USTRUCT(BlueprintType)
struct FFaceSwooshArt
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Swoosh Art")
    TArray<FFaceTextureSet> Frames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Swoosh Art")
    FFaceArtTransform OverrideTransform;
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

UENUM(BlueprintType)
enum class EFaceParamTarget : uint8
{
    PositionX       UMETA(DisplayName = "Position X"),
    PositionY       UMETA(DisplayName = "Position Y"),
    ScaleX          UMETA(DisplayName = "Scale X"),
    ScaleY          UMETA(DisplayName = "Scale Y"),
    Rotation        UMETA(DisplayName = "Rotation"),
    TextureBlend    UMETA(DisplayName = "Texture Blend")
};

USTRUCT(BlueprintType)
struct FFaceParamBinding
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Param Binding")
    FName ParamName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Param Binding")
    EFaceParamTarget Target = EFaceParamTarget::PositionX;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Param Binding",
        meta = (ClampMin = "-10.0", ClampMax = "10.0"))
    float Scale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Param Binding")
    float Offset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Param Binding")
    bool bInvert = false;
};

// Cache of last-applied texture pointers to avoid redundant material parameter pushes.
USTRUCT(BlueprintType)
struct FFaceAppliedTextures
{
    GENERATED_BODY()
    UPROPERTY()
    UTexture2D* Albedo = nullptr;
    UPROPERTY()
    UTexture2D* Normal = nullptr;
    UPROPERTY()
    UTexture2D* Depth = nullptr;
};

USTRUCT(BlueprintType)
struct FFaceJiggleSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Jiggle",
        meta = (ClampMin = "0.0", ClampMax = "20.0"))
    float Stiffness = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Jiggle",
        meta = (ClampMin = "0.0", ClampMax = "5.0"))
    float Damping = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Jiggle",
        meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float ImpulseScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Jiggle")
    FVector2D JiggleAxis = FVector2D(1.0f, 1.0f);
};

USTRUCT(BlueprintType)
struct FFaceProfile3D
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Profile",
        meta = (ClampMin = "0.01"))
    float FaceHalfWidth = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Profile",
        meta = (ClampMin = "0.01"))
    float FaceHalfDepth = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Profile",
        meta = (ClampMin = "0.01"))
    float FaceHalfHeight = 1.0f;
};

USTRUCT(BlueprintType)
struct FFacePin3D
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Pin")
    bool bPinned = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Pin",
        meta = (EditCondition = "bPinned", ClampMin = "-1.0", ClampMax = "1.0"))
    FVector Position3D = FVector(0.0f, 0.0f, 0.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Pin",
        meta = (DisplayName = "View-Angle Rotation Enabled", EditCondition = "bPinned"))
    bool bEnableViewAngleRotation = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Pin",
        meta = (DisplayName = "Min Rotation (degrees, at zone center)",
            EditCondition = "bEnableViewAngleRotation", ClampMin = "-360.0", ClampMax = "360.0"))
    float MinRotation = -30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Pin",
        meta = (DisplayName = "Max Rotation (degrees, at zone edge)",
            EditCondition = "bEnableViewAngleRotation", ClampMin = "-360.0", ClampMax = "360.0"))
    float MaxRotation = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Pin",
        meta = (DisplayName = "Rotation Sensitivity", EditCondition = "bEnableViewAngleRotation",
            ClampMin = "-10.0", ClampMax = "10.0"))
    float RotationSensitivity = 1.0f;
};

USTRUCT(BlueprintType)
struct FFaceNestedArt
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art")
    FName ElementName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art")
    FFaceTextureSet Textures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art")
    FFaceArtTransform RelativeTransform;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    FVector2D PivotPoint = FVector2D(0.5f, 0.5f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art",
        meta = (DisplayName = "3D Pin (overrides PivotPoint when pinned)"))
    FFacePin3D Pin3D;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art",
        meta = (DisplayName = "Jiggle Enabled"))
    bool bJiggleEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art",
        meta = (EditCondition = "bJiggleEnabled"))
    FFaceJiggleSettings JiggleSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art",
        meta = (DisplayName = "Idle Animation Frames (looping)"))
    TArray<FFaceTextureSet> IdleFrames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art",
        meta = (ClampMin = "0.001", EditCondition = "IdleFrames.Num() > 0"))
    float IdleFrameDuration = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art",
        meta = (ClampMin = "0.0", EditCondition = "IdleFrames.Num() > 0"))
    float IdleSpeedMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art")
    TMap<EFaceAngleState, bool> ViewVisibility;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art")
    TArray<FFaceParamBinding> ParamBindings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Nested Art",
        meta = (DisplayName = "Alt Textures (for TextureBlend binding)"))
    FFaceTextureSet AltTextures;

    TArray<FFaceNestedArt> Children;
};

USTRUCT(BlueprintType)
struct FFaceVisemeFrameArray
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot")
    TArray<FFaceTextureSet> Frames;
};

USTRUCT(BlueprintType)
struct FFaceExpressionVisemeMap
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot")
    TMap<EViseme, FFaceVisemeFrameArray> Visemes;
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
    TMap<EFaceAngleState, FFaceArtTransform> ViewOverrides;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Expression Textures"))
    TMap<EExpression, FFaceTextureSet> ExpressionTextures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Named Expression Textures (extensible via FName)"))
    TMap<FName, FFaceTextureSet> NamedExpressionTextures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Blink Animation Frames"))
    TArray<FFaceTextureSet> BlinkFrames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Viseme Frame Sets (Expression > Viseme > Frames)"))
    TMap<EExpression, FFaceExpressionVisemeMap> VisemeFrameSets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Named Viseme Frames (extensible viseme name → frames)"))
    TMap<FName, FFaceVisemeFrameArray> NamedVisemeFrames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Swoosh Frames (per-target-state)"))
    TMap<EFaceAngleState, FFaceSwooshArt> SwooshToState;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Parameter Bindings"))
    TArray<FFaceParamBinding> ParamBindings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Alt Textures (for TextureBlend binding)"))
    FFaceTextureSet AltTextures;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Art Slot",
        meta = (DisplayName = "Nested Art Elements"))
    TArray<struct FFaceNestedArt> NestedElements;

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
