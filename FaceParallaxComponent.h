#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FaceParallaxTypes.h"
#include "FaceParallaxComponent.generated.h"

class UMaterialInstanceDynamic;
class UFaceParallaxPreset;
class UStaticMeshComponent;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFaceStateChangedSignature, EFaceAngleState, NewState, EFaceAngleState, OldState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnParamValueChangedSignature, FName, ParamName, float, OldValue, float, NewValue);

USTRUCT(BlueprintType)
struct FFaceLayerDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Layer")
    FName LayerTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Layer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DepthScale = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Layer",
        meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float DepthMapIntensity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Layer")
    bool bInvertParallax = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Layer",
        meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float DepthMin = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Layer",
        meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float DepthMax = 1.0f;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UFaceParallaxComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFaceParallaxComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // --- CAMERA SOURCE ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Camera")
    ECameraSource CameraSource = ECameraSource::PlayerCamera0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Camera",
        meta = (EditCondition = "CameraSource == ECameraSource::SpecifiedActor"))
    TObjectPtr<AActor> CustomCameraActor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Camera",
        meta = (ClampMin = "0", ClampMax = "3"))
    int32 CameraPlayerIndex = 0;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Camera")
    void SetCameraSource(ECameraSource Source) { CameraSource = Source; bSequencerCacheValid = false; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Camera")
    ECameraSource GetCameraSource() const { return CameraSource; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Camera")
    void SetCustomCameraActor(AActor* Actor) { CustomCameraActor = Actor; bSequencerCacheValid = false; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Camera")
    AActor* GetCustomCameraActor() const { return CustomCameraActor; }

    // --- SKELETAL MESH SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Skeletal Mesh")
    FName HeadBoneName = "head";

    // --- LAYER QUAD AUTO-SETUP ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Layer Quads",
        meta = (ToolTip="Spawn plane quads automatically at BeginPlay when no primitives with matching LayerTags are found on the owner."))
    bool bAutoSpawnLayerQuads = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Layer Quads",
        meta = (ToolTip="Asset path root used to resolve the material instance per layer (appends the LayerTag)."))
    FString LayerMaterialPathRoot = TEXT("/Game/FaceParallax/Materials/Instances/MI_FaceParallax_");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Layer Quads",
        meta = (ClampMin = "1.0", ToolTip="World-space width of a spawned layer quad. Height derives from the preset CanvasSize aspect."))
    float LayerQuadWorldWidth = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Layer Quads",
        meta = (ToolTip="Local offset applied to spawned quads relative to the head bone."))
    FVector LayerQuadLocalOffset = FVector(10.0f, 0.0f, 0.0f);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Layer Quads",
        meta = (ToolTip="Spawn plane quads for every layer (plus Front-state nested elements) attached to the head bone, tagged with their LayerTag. Skips layers that already have a tagged primitive."))
    int32 SpawnLayerQuads();

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Layer Quads")
    void RemoveSpawnedQuads();

    // --- VIEW ANGLE SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|View Angles")
    float TopViewPitchThreshold = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|View Angles")
    float BottomViewPitchThreshold = -60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|View Angles")
    float HalfZoneWidth = 22.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|View Angles",
        meta = (DisplayName="Zone Boundary Multipliers",
            ToolTip="4 multipliers [Front, ThreeQuarter, Profile, Back] applied to HalfZoneWidth to set zone boundaries. Default {1,3,5,7}"))
    TArray<float> ZoneBoundaryMultipliers;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|View Angles")
    void SetTopViewPitchThreshold(float Threshold) { TopViewPitchThreshold = Threshold; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|View Angles")
    float GetTopViewPitchThreshold() const { return TopViewPitchThreshold; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|View Angles")
    void SetBottomViewPitchThreshold(float Threshold) { BottomViewPitchThreshold = Threshold; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|View Angles")
    float GetBottomViewPitchThreshold() const { return BottomViewPitchThreshold; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|View Angles")
    void SetHalfZoneWidth(float Width) { HalfZoneWidth = FMath::Max(1.0f, Width); }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|View Angles")
    float GetHalfZoneWidth() const { return HalfZoneWidth; }

    static float GetBoundaryOrDefault(const TArray<float>& Multipliers, int32 Index)
    {
        static const float Defaults[4] = {1.0f, 3.0f, 5.0f, 7.0f};
        return Multipliers.IsValidIndex(Index) ? Multipliers[Index] : Defaults[Index];
    }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|View Angles")
    float GetZoneCenterYaw(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|View Angles")
    float GetZoneCenterPitch(EFaceAngleState State) const;

    // --- TRANSITION SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Transitions")
    float CrossfadeSpeed = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Transitions",
        meta = (ClampMin = "1", ClampMax = "30"))
    int32 HysteresisFrames = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Transitions")
    bool bUseContinuousBlending = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Transitions")
    float BlendWindowWidth = 5.0f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Transitions")
    void SetCrossfadeSpeed(float Speed) { CrossfadeSpeed = FMath::Max(0.1f, Speed); }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Transitions")
    float GetCrossfadeSpeed() const { return CrossfadeSpeed; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Transitions")
    void SetHysteresisFrames(int32 Frames) { HysteresisFrames = FMath::Clamp(Frames, 1, 30); }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Transitions")
    int32 GetHysteresisFrames() const { return HysteresisFrames; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Transitions")
    void SetUseContinuousBlending(bool bEnabled) { bUseContinuousBlending = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Transitions")
    bool GetUseContinuousBlending() const { return bUseContinuousBlending; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Transitions")
    void SetBlendWindowWidth(float Width) { BlendWindowWidth = FMath::Max(0.0f, Width); }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Transitions")
    float GetBlendWindowWidth() const { return BlendWindowWidth; }

    // --- SWOOSH TRANSITION SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh")
    bool bSwooshEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (ClampMin = "0.0"))
    float SwooshSpeedThreshold = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (ClampMin = "0.001", ClampMax = "1.0"))
    float SwooshFrameDuration = 0.033f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float SwooshBlendOutDuration = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SwooshBusyness = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SwooshSize = 0.5f;

    // Smoothed angular velocity (exponential moving average) for swoosh detection
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (ClampMin = "0.01", ClampMax = "1.0", DisplayName = "Swoosh Angular Velocity Smoothing"))
    float SwooshSmoothingAlpha = 0.3f;

    UPROPERTY(BlueprintReadOnly, Category = "Face Parallax|Swoosh")
    float SwooshSmoothedVelocity = 0.0f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void ForceSwoosh(EFaceAngleState TargetState);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    bool IsSwooshActive() const { return SwooshPhase != ESwooshPhase::Inactive; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshEnabled(bool bEnabled) { bSwooshEnabled = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    bool GetSwooshEnabled() const { return bSwooshEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshSpeedThreshold(float Threshold) { SwooshSpeedThreshold = FMath::Max(0.0f, Threshold); }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    float GetSwooshSpeedThreshold() const { return SwooshSpeedThreshold; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshBusyness(float Busyness) { SwooshBusyness = FMath::Clamp(Busyness, 0.0f, 1.0f); }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    float GetSwooshBusyness() const { return SwooshBusyness; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshSize(float Size) { SwooshSize = FMath::Clamp(Size, 0.0f, 1.0f); }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    float GetSwooshSize() const { return SwooshSize; }

    // --- NESTED ART + JIGGLE SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Nested Art")
    bool bNestedArtEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Nested Art",
        meta = (DisplayName = "Art Pivot Param Name"))
    FName ArtPivotParamName = "ArtPivot";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Nested Art",
        meta = (DisplayName = "Nested Anim Frame Param Name"))
    FName NestedAnimParamName = "NestedAnimFrame";

    // Fixed-timestep jiggle physics
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Nested Art",
        meta = (ClampMin = "15", ClampMax = "480", DisplayName = "Jiggle Sub-Steps Per Second"))
    int32 JiggleSubStepsPerSecond = 120;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void SetNestedArtEnabled(bool bEnabled) { bNestedArtEnabled = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    bool GetNestedArtEnabled() const { return bNestedArtEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    int32 GetNestedElementCount(EFaceAngleState State, FName LayerTag) const;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    FFaceNestedArt GetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void SetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceNestedArt& Element);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void AddNestedElement(EFaceAngleState State, FName LayerTag, const FFaceNestedArt& Element);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void RemoveNestedElement(EFaceAngleState State, FName LayerTag, int32 Index);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void SetNestedTextures(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceTextureSet& Textures);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void SetNestedTransform(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceArtTransform& Transform);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void SetNestedPivot(EFaceAngleState State, FName LayerTag, int32 Index, FVector2D Pivot);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void SetNestedVisibility(EFaceAngleState State, FName LayerTag, FName ElementName, EFaceAngleState ViewState, bool bVisible);

    // --- 3D FACE PROFILE + PIN ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Nested Art",
        meta = (DisplayName = "Face Profile (auto-detected from texture sizes)"))
    FFaceProfile3D FaceProfile;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void DetectFaceProfileFromPreset();

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void SetFaceProfile(const FFaceProfile3D& Profile) { FaceProfile = Profile; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    FFaceProfile3D GetFaceProfile() const { return FaceProfile; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    FVector2D ProjectPinToUV(FVector Pin3D, EFaceAngleState ViewState) const;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    void SetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index, const FFacePin3D& Pin);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    FFacePin3D GetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index) const;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Nested Art")
    FVector2D GetNestedEffectivePivot(EFaceAngleState State, FName LayerTag, int32 Index) const;

    // --- OUTLINE ART CONCEPT ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Outline",
        meta = (DisplayName = "Outline View States (used for 3D profile extraction)"))
    TArray<EFaceAngleState> OutlineViewStates;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Outline")
    void SetOutlineViewState(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Outline")
    void ClearOutlineViewStates();

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Outline")
    bool IsOutlineViewState(EFaceAngleState State) const;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Outline")
    TArray<EFaceAngleState> GetOutlineViewStates() const;

    // --- PARAMETER SYSTEM ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parameters")
    bool bParamsEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parameters",
        meta = (ClampMin = "0.1", ClampMax = "100.0"))
    float ParamSmoothingSpeed = 8.0f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void SetParamsEnabled(bool bEnabled) { bParamsEnabled = bEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    bool GetParamsEnabled() const { return bParamsEnabled; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void DefineParameter(FName ParamName, float DefaultValue = 0.0f, float Min = 0.0f, float Max = 1.0f, float SmoothingSpeed = 8.0f);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void SetParameterValue(FName ParamName, float Value);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    float GetParameterValue(FName ParamName) const;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    TArray<FName> GetParameterNames() const;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void ResetAllParameters();

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void SetParamSmoothingSpeed(FName ParamName, float Speed);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    float GetParamSmoothingSpeed(FName ParamName) const;

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnParamValueChangedSignature OnParamValueChanged;

    // --- PARAMETER MATERIAL PARAMS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parameters",
        meta = (DisplayName = "Param Blend Alpha Param Name"))
    FName ParamBlendParamName = "ParamBlendAlpha";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parameters",
        meta = (DisplayName = "Alt Albedo Param Name"))
    FName ParamAltAlbedoParamName = "AltAlbedoTexture";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parameters",
        meta = (DisplayName = "Alt Normal Param Name"))
    FName ParamAltNormalParamName = "AltNormalTexture";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parameters",
        meta = (DisplayName = "Alt Depth Param Name"))
    FName ParamAltDepthParamName = "AltDepthTexture";

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void SetParamBlendParamName(FName Name) { ParamBlendParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    FName GetParamBlendParamName() const { return ParamBlendParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void SetParamAltAlbedoParamName(FName Name) { ParamAltAlbedoParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    FName GetParamAltAlbedoParamName() const { return ParamAltAlbedoParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void SetParamAltNormalParamName(FName Name) { ParamAltNormalParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    FName GetParamAltNormalParamName() const { return ParamAltNormalParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    void SetParamAltDepthParamName(FName Name) { ParamAltDepthParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parameters")
    FName GetParamAltDepthParamName() const { return ParamAltDepthParamName; }

    // --- PARALLAX SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parallax")
    float MaxParallaxOffset = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parallax")
    float MaxVerticalParallaxOffset = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parallax")
    TArray<FFaceLayerDef> LayerDefinitions;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    void SetMaxParallaxOffset(float Offset) { MaxParallaxOffset = FMath::Max(0.0f, Offset); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    float GetMaxParallaxOffset() const { return MaxParallaxOffset; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    void SetMaxVerticalParallaxOffset(float Offset) { MaxVerticalParallaxOffset = FMath::Max(0.0f, Offset); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    float GetMaxVerticalParallaxOffset() const { return MaxVerticalParallaxOffset; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    int32 GetNumLayerDefinitions() const { return LayerDefinitions.Num(); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    FFaceLayerDef GetLayerDefinition(int32 Index) const;
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    void SetLayerDefinition(int32 Index, const FFaceLayerDef& Def);
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    void AddLayerDefinition(const FFaceLayerDef& Def);
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    void RemoveLayerDefinition(int32 Index);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    float GetLayerDepthMin(int32 LayerIndex) const;
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    float GetLayerDepthMax(int32 LayerIndex) const;
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    void SetLayerDepthRange(int32 LayerIndex, float Min, float Max);
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    FVector2D GetLayerParallaxOffset(int32 LayerIndex) const;
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Parallax")
    TArray<FVector2D> GetAllLayerParallaxOffsets() const { return LayerParallaxOffsets; }

    // --- DEPTH MAP SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Depth Maps")
    bool bUseMaterialDrivenDepth = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Depth Maps")
    float DepthMapIntensity = 1.0f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Depth Maps")
    void SetUseMaterialDrivenDepth(bool bEnabled) { bUseMaterialDrivenDepth = bEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Depth Maps")
    bool GetUseMaterialDrivenDepth() const { return bUseMaterialDrivenDepth; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Depth Maps")
    void SetDepthMapIntensity(float Intensity) { DepthMapIntensity = FMath::Max(0.0f, Intensity); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Depth Maps")
    float GetDepthMapIntensity() const { return DepthMapIntensity; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Depth Maps",
        meta = (DisplayName = "Depth Min Param Name"))
    FName DepthMinParamName = "DepthMin";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Depth Maps",
        meta = (DisplayName = "Depth Max Param Name"))
    FName DepthMaxParamName = "DepthMax";

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Depth Maps")
    void SetDepthMinParamName(FName Name) { DepthMinParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Depth Maps")
    FName GetDepthMinParamName() const { return DepthMinParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Depth Maps")
    void SetDepthMaxParamName(FName Name) { DepthMaxParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Depth Maps")
    FName GetDepthMaxParamName() const { return DepthMaxParamName; }

    // --- MATERIAL TEXTURE PARAMETERS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params")
    FName AlbedoParamName = "AlbedoTexture";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params")
    FName NormalParamName = "NormalTexture";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params")
    FName DepthParamName = "DepthTexture";

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    void SetAlbedoParamName(FName Name) { AlbedoParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    FName GetAlbedoParamName() const { return AlbedoParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    void SetNormalParamName(FName Name) { NormalParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    FName GetNormalParamName() const { return NormalParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    void SetDepthParamName(FName Name) { DepthParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    FName GetDepthParamName() const { return DepthParamName; }

    // --- DUAL-TEXTURE PARAMETERS for state crossfade blending ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params",
        meta = (DisplayName = "Prev Albedo Param Name"))
    FName AlbedoPrevParamName = "AlbedoTexturePrev";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params",
        meta = (DisplayName = "Prev Normal Param Name"))
    FName NormalPrevParamName = "NormalTexturePrev";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params",
        meta = (DisplayName = "Prev Depth Param Name"))
    FName DepthPrevParamName = "DepthTexturePrev";

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    void SetAlbedoPrevParamName(FName Name) { AlbedoPrevParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    FName GetAlbedoPrevParamName() const { return AlbedoPrevParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    void SetNormalPrevParamName(FName Name) { NormalPrevParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    FName GetNormalPrevParamName() const { return NormalPrevParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    void SetDepthPrevParamName(FName Name) { DepthPrevParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Material Texture Params")
    FName GetDepthPrevParamName() const { return DepthPrevParamName; }

    // --- DYNAMIC ART OFFSET ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params",
        meta = (DisplayName = "Drive ArtPosition X from yaw deviation"))
    bool bDriveArtPositionFromYaw = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params",
        meta = (DisplayName = "Max yaw-driven art offset (UV units)",
            ClampMin = "0.0", ClampMax = "1.0"))
    float MaxYawArtOffset = 0.05f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    void SetDriveArtPositionFromYaw(bool bEnabled) { bDriveArtPositionFromYaw = bEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    bool GetDriveArtPositionFromYaw() const { return bDriveArtPositionFromYaw; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    void SetMaxYawArtOffset(float Offset) { MaxYawArtOffset = FMath::Clamp(Offset, 0.0f, 1.0f); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    float GetMaxYawArtOffset() const { return MaxYawArtOffset; }

    // --- ART TRANSFORM PARAMETERS (per-part UV manipulation) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params")
    FName ArtPositionParamName = "ArtPosition";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params")
    FName ArtScaleParamName = "ArtScale";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params")
    FName ArtRotationParamName = "ArtRotation";

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    void SetArtPositionParamName(FName Name) { ArtPositionParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    FName GetArtPositionParamName() const { return ArtPositionParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    void SetArtScaleParamName(FName Name) { ArtScaleParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    FName GetArtScaleParamName() const { return ArtScaleParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    void SetArtRotationParamName(FName Name) { ArtRotationParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Art Transform Params")
    FName GetArtRotationParamName() const { return ArtRotationParamName; }

    // --- PRESET ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Preset")
    class UFaceParallaxPreset* ActivePreset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Preset")
    bool bAutoApplyPreset = true;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Preset")
    void SetAutoApplyPreset(bool bEnabled) { bAutoApplyPreset = bEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Preset")
    bool GetAutoApplyPreset() const { return bAutoApplyPreset; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Preset")
    UFaceParallaxPreset* GetActivePreset() const { return ActivePreset; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Preset")
    void ApplyPreset(UFaceParallaxPreset* Preset);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Preset")
    void SetStateTextures(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Preset")
    UTexture2D* GetCurrentDepthTexture() const;

    // --- BLINK ANIMATION ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Blink")
    bool bBlinkingEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Blink",
        meta = (ClampMin = "0.5", ClampMax = "30.0"))
    float BlinkIntervalMin = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Blink",
        meta = (ClampMin = "0.5", ClampMax = "30.0"))
    float BlinkIntervalMax = 7.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Blink",
        meta = (ClampMin = "0.001", ClampMax = "1.0"))
    float BlinkFrameDuration = 0.03f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    void ForceBlink();

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    bool IsBlinking() const { return bIsBlinking; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    void SetBlinkInterval(float Min, float Max);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    float GetBlinkIntervalMin() const { return BlinkIntervalMin; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    float GetBlinkIntervalMax() const { return BlinkIntervalMax; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    void SetBlinkingEnabled(bool bEnabled) { bBlinkingEnabled = bEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    bool GetBlinkingEnabled() const { return bBlinkingEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    void SetBlinkFrameDuration(float Duration) { BlinkFrameDuration = FMath::Clamp(Duration, 0.001f, 1.0f); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Blink")
    float GetBlinkFrameDuration() const { return BlinkFrameDuration; }

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnBlinkStarted;

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnBlinkCompleted;

    // --- EXPRESSION SYSTEM ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Expression")
    EExpression CurrentExpression = EExpression::Neutral;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Expression",
        meta = (DisplayName = "Current Named Expression (extensible)"))
    FName CurrentNamedExpression;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Expression",
        meta = (ClampMin = "0.01", ClampMax = "5.0"))
    float ExpressionCrossfadeDuration = 0.3f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    void SetExpression(EExpression NewExpression);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    EExpression GetExpression() const { return CurrentExpression; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    void SetExpressionCrossfadeDuration(float Duration) { ExpressionCrossfadeDuration = FMath::Max(0.01f, Duration); }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    float GetExpressionCrossfadeDuration() const { return ExpressionCrossfadeDuration; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    bool IsExpressionTransitioning() const { return bExpressionTransitioning; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    void SetExpressionByName(FName NewExpressionName);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    FName GetExpressionByName() const { return CurrentNamedExpression; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    bool IsNamedExpressionValid() const { return CurrentNamedExpression != NAME_None; }

    // --- VISEME SYSTEM ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Viseme")
    bool bVisemeEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Viseme",
        meta = (ClampMin = "0.001", ClampMax = "1.0"))
    float VisemeFrameDuration = 0.04f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    void PlayViseme(EViseme NewViseme);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    void StopViseme();

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    bool IsVisemePlaying() const { return bIsVisemePlaying; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    EViseme GetCurrentViseme() const { return CurrentViseme; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Viseme",
        meta = (DisplayName = "Current Named Viseme (extensible)"))
    FName CurrentNamedViseme;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    void PlayVisemeByName(FName NewVisemeName);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    FName GetVisemeByName() const { return CurrentNamedViseme; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    bool IsNamedVisemeValid() const { return CurrentNamedViseme != NAME_None; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    void SetVisemeEnabled(bool bEnabled) { bVisemeEnabled = bEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    bool GetVisemeEnabled() const { return bVisemeEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    void SetVisemeFrameDuration(float Duration) { VisemeFrameDuration = FMath::Clamp(Duration, 0.001f, 1.0f); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Viseme")
    float GetVisemeFrameDuration() const { return VisemeFrameDuration; }

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnVisemeStarted;

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnVisemeCompleted;

    // --- SWOOSH EVENTS ---
    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnSwooshStarted;

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnSwooshCompleted;

    // --- EXPRESSION MATERIAL PARAMETERS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Expression",
        meta = (DisplayName = "Expression Blend Alpha Param"))
    FName ExpressionBlendParamName = "ExpressionBlendAlpha";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Expression",
        meta = (DisplayName = "Expression Prev Albedo Param"))
    FName ExpressionAlbedoPrevParamName = "ExpressionAlbedoPrev";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Expression",
        meta = (DisplayName = "Expression Prev Normal Param"))
    FName ExpressionNormalPrevParamName = "ExpressionNormalPrev";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Expression",
        meta = (DisplayName = "Expression Prev Depth Param"))
    FName ExpressionDepthPrevParamName = "ExpressionDepthPrev";

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    void SetExpressionBlendParamName(FName Name) { ExpressionBlendParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    FName GetExpressionBlendParamName() const { return ExpressionBlendParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    void SetExpressionAlbedoPrevParamName(FName Name) { ExpressionAlbedoPrevParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    FName GetExpressionAlbedoPrevParamName() const { return ExpressionAlbedoPrevParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    void SetExpressionNormalPrevParamName(FName Name) { ExpressionNormalPrevParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    FName GetExpressionNormalPrevParamName() const { return ExpressionNormalPrevParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    void SetExpressionDepthPrevParamName(FName Name) { ExpressionDepthPrevParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Expression")
    FName GetExpressionDepthPrevParamName() const { return ExpressionDepthPrevParamName; }

    // --- SWOOSH MATERIAL PARAMETERS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (DisplayName = "Swoosh Layer Blend Param"))
    FName SwooshLayerBlendParamName = "SwooshLayerBlend";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (DisplayName = "Swoosh Intensity Param"))
    FName SwooshIntensityParamName = "SwooshIntensity";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (DisplayName = "Swoosh Angle Param"))
    FName SwooshAngleParamName = "SwooshAngle";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (DisplayName = "Swoosh Size Param"))
    FName SwooshSizeParamName = "SwooshSize";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Swoosh",
        meta = (DisplayName = "Swoosh Texture Param"))
    FName SwooshTextureParamName = "SwooshTexture";

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshLayerBlendParamName(FName Name) { SwooshLayerBlendParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    FName GetSwooshLayerBlendParamName() const { return SwooshLayerBlendParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshIntensityParamName(FName Name) { SwooshIntensityParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    FName GetSwooshIntensityParamName() const { return SwooshIntensityParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshAngleParamName(FName Name) { SwooshAngleParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    FName GetSwooshAngleParamName() const { return SwooshAngleParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshSizeParamName(FName Name) { SwooshSizeParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    FName GetSwooshSizeParamName() const { return SwooshSizeParamName; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshTextureParamName(FName Name) { SwooshTextureParamName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    FName GetSwooshTextureParamName() const { return SwooshTextureParamName; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshFrameDuration(float Duration) { SwooshFrameDuration = FMath::Clamp(Duration, 0.001f, 1.0f); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    float GetSwooshFrameDuration() const { return SwooshFrameDuration; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    void SetSwooshBlendOutDuration(float Duration) { SwooshBlendOutDuration = FMath::Clamp(Duration, 0.0f, 2.0f); }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Swoosh")
    float GetSwooshBlendOutDuration() const { return SwooshBlendOutDuration; }

    // --- MATERIAL DEBUG ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Debug")
    bool bEnableMaterialDebugMode = false;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Debug")
    void SetEnableMaterialDebugMode(bool bEnabled) { bEnableMaterialDebugMode = bEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Debug")
    bool GetEnableMaterialDebugMode() const { return bEnableMaterialDebugMode; }

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Skeletal Mesh")
    void SetHeadBoneName(FName Name) { HeadBoneName = Name; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Skeletal Mesh")
    FName GetHeadBoneName() const { return HeadBoneName; }

    // --- LAYER VISIBILITY ---
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Layer Visibility")
    void SetLayerVisibility(FName LayerTag, bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Layer Visibility")
    bool GetLayerVisibility(FName LayerTag) const;

    // --- EVENTS ---
    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnFaceStateChanged;

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnExpressionChanged;

    // --- OUTPUTS ---
    UPROPERTY(BlueprintReadOnly, Category = "Face Parallax|Outputs")
    EFaceAngleState CurrentState;

    UPROPERTY(BlueprintReadOnly, Category = "Face Parallax|Outputs")
    EFaceAngleState PreviousState;

    UPROPERTY(BlueprintReadOnly, Category = "Face Parallax|Outputs")
    float BlendAlpha;

    UPROPERTY(BlueprintReadOnly, Category = "Face Parallax|Outputs")
    float CurrentYaw;

    UPROPERTY(BlueprintReadOnly, Category = "Face Parallax|Outputs")
    float CurrentPitch;

    UPROPERTY(BlueprintReadOnly, Category = "Face Parallax|Outputs")
    TArray<FVector2D> LayerParallaxOffsets;

    UPROPERTY(BlueprintReadOnly, Category = "Face Parallax|Debug")
    bool bIsInTransition;

    // --- EDITOR BLEND PREVIEW ---
    UPROPERTY(BlueprintReadWrite, Category = "Face Parallax|Debug")
    bool bBlendPreviewOverride = false;

    UPROPERTY(BlueprintReadWrite, Category = "Face Parallax|Debug",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BlendPreviewAlpha = 0.5f;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Debug")
    void SetBlendPreview(float Alpha);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Debug")
    void ClearBlendPreview();

    // --- MPC OPTIMIZATION ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Optimization",
        meta = (DisplayName = "Use Material Parameter Collection"))
    bool bUseMPC = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Optimization",
        meta = (EditCondition = "bUseMPC"))
    TObjectPtr<class UMaterialParameterCollection> ParallaxMPC;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Optimization")
    bool bUseAsyncTextureLoading = true;

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Optimization")
    void SetUseMPC(bool bEnabled) { bUseMPC = bEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Optimization")
    bool GetUseMPC() const { return bUseMPC; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Optimization")
    void SetParallaxMPC(class UMaterialParameterCollection* MPC) { ParallaxMPC = MPC; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Optimization")
    class UMaterialParameterCollection* GetParallaxMPC() const { return ParallaxMPC; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Optimization")
    void SetUseAsyncTextureLoading(bool bEnabled) { bUseAsyncTextureLoading = bEnabled; }
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Optimization")
    bool GetUseAsyncTextureLoading() const { return bUseAsyncTextureLoading; }

    // --- ASYNC TEXTURE LOAD ---
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Optimization")
    void AsyncLoadSlotTextures(EFaceAngleState State, FName LayerTag);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Optimization")
    void AsyncUnloadSlotTextures(EFaceAngleState State, FName LayerTag);

    // --- ZONE MATH (editor-facing) ---
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Debug")
    static FName GetStateLabel(EFaceAngleState State);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Debug")
    static FLinearColor GetStateColor(EFaceAngleState State);

    // --- PARAM REFERENCE (editor-facing) ---
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Debug")
    TArray<FString> FindParamNameReferences(FName ParamName) const;

    // --- ACCESSORS FOR EDITOR ---
    TMap<FName, TArray<UMaterialInstanceDynamic*>>& GetMaterialLayers() { return FaceMaterialsByLayer; }
    const TMap<FName, TArray<UMaterialInstanceDynamic*>>& GetMaterialLayers() const { return FaceMaterialsByLayer; }

private:
    struct FFaceParamDef
    {
        float DefaultValue = 0.0f;
        float CurrentValue = 0.0f;
        float TargetValue = 0.0f;
        float Min = 0.0f;
        float Max = 1.0f;
        float SmoothingSpeed = 8.0f;
    };

    TMap<FName, FFaceParamDef> ParamDefinitions;

    void UpdateParametersTick(float DeltaTime);

    UPROPERTY()
    class USkeletalMeshComponent* OwnerMesh;

    UPROPERTY()
    class APlayerCameraManager* CameraManager;

    TMap<FName, TArray<UMaterialInstanceDynamic*>> FaceMaterialsByLayer;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> SpawnedLayerQuads;

    bool bFoundTaggedPrimitives = false;

    UStaticMeshComponent* SpawnQuadForTag(FName Tag, FVector2D QuadSize, float RotationDegrees);
    int32 SpawnNestedQuadRecursive(FName LayerTag, const FFaceNestedArt& Element, FVector2D ParentQuadSize, int32& DepthIndex);
    void SyncLayerDefinitionsFromPreset();

    int32 HysteresisFramesRemaining = 0;
    EFaceAngleState HysteresisPendingState;

    UPROPERTY()
    TMap<FName, FFaceTextureSet> PreviousTextureSets;

    // --- Blink state ---
    bool bIsBlinking = false;
    float NextBlinkCountdown = 3.0f;
    int32 BlinkFrameIndex = 0;
    float BlinkFrameTimer = 0.0f;

    // --- Expression state ---
    EExpression PreviousExpression = EExpression::Neutral;
    float ExpressionBlendAlpha = 1.0f;
    bool bExpressionTransitioning = false;

    UPROPERTY()
    TMap<FName, FFaceTextureSet> ExpressionPreviousTextureSets;

    // --- Viseme state ---
    bool bIsVisemePlaying = false;
    EViseme CurrentViseme = EViseme::Ah;
    int32 VisemeFrameIndex = 0;
    float VisemeFrameTimer = 0.0f;

    // --- Swoosh state ---
    ESwooshPhase SwooshPhase = ESwooshPhase::Inactive;
    int32 SwooshFrameIndex = 0;
    float SwooshFrameTimer = 0.0f;
    float SwooshBlendOutElapsed = 0.0f;
    float PreviousFrameYaw = 0.0f;
    float PreviousFramePitch = 0.0f;
    float FrameDyaw = 0.0f;
    float FrameDpitch = 0.0f;
    float SwooshSmearAngle = 0.0f;
    int32 SwooshProceduralTick = 0;
    UPROPERTY()
    TArray<FFaceTextureSet> SwooshFrames;

    // --- Frame delta save: MUST call BEFORE overwriting PreviousFrameYaw/Pitch ---
    // Used by jiggle impulse, swoosh smear angle, and angular velocity.
    // The overwrite of PreviousFrameYaw/Pitch happens inside this call.
    void SaveFrameDelta(float InYaw, float InPitch)
    {
        FrameDyaw = InYaw - PreviousFrameYaw;
        FrameDpitch = InPitch - PreviousFramePitch;
        PreviousFrameYaw = InYaw;
        PreviousFramePitch = InPitch;
    }

    // --- Nested art + jiggle state ---
    struct FNestedJiggleState
    {
        FVector2D Position;
        FVector2D Velocity;
    };

    struct FNestedAnimState
    {
        int32 FrameIndex = 0;
        float FrameTimer = 0.0f;
    };

    TMap<FName, FNestedJiggleState> JiggleStates;
    TMap<FName, FNestedAnimState> AnimStates;

    // Jiggle fixed-timestep accumulator
    TMap<FName, float> JiggleAccumulators;

    TMap<FName, TArray<UMaterialInstanceDynamic*>> NestedMaterialsByElement;

    void UpdateNestedArtTick(float DeltaTime);
    void PushNestedArtParams();
    void PushNestedChildArt(const FFaceNestedArt& Element, const FFaceArtTransform& ParentTransform, FName LayerTag, FName ParentElementName);
    FFaceArtTransform ComputeNestedEffectiveTransform(const FFaceNestedArt& Element, const FFaceArtTransform& ParentTransform, const FVector2D& JiggleOffset) const;

    FVector2D GetEffectivePivot(const FFaceNestedArt& Element) const;
    FVector2D ProjectPinToUVInternal(const FVector& Pin3D, EFaceAngleState ViewState) const;

    void UpdateBlinkTick(float DeltaTime);
    void UpdateExpressionTick(float DeltaTime);
    void UpdateVisemeTick(float DeltaTime);
    void UpdateSwooshTick(float DeltaTime);
    void ApplyExpressionTextures(EFaceAngleState State);
    const FFaceTextureSet* ResolveExpressionTextureSet(const FFaceArtSlot& Slot) const;
    const FFaceVisemeFrameArray* ResolveVisemeFrames(const FFaceArtSlot& Slot) const;

    void InitializeMaterials();
    void UpdateMaterialParameters();
public:
    void ApplyCurrentStateTextures();
    void CaptureCurrentTextures();
    void SetPreviousStateTextures();

    void CalculateLookDelta(float& OutYaw, float& OutPitch);
    EFaceAngleState DetermineStateFromAngles(float Yaw, float Pitch);
    void UpdateStateMachine(float Yaw, float Pitch, float DeltaTime, float AngularVelocity = 0.0f);
    void UpdateParallaxOffsets(float DeltaTime);
    FVector2D ComputeOffsetForState(EFaceAngleState State, float Yaw, float Pitch, int32 LayerIndex) const;

    void StopAnimationsOnStateChange();

    void LogWarning(const FString& Message) const;

    // --- Camera resolution ---
    bool GetCameraLocationAndRotation(FVector& OutLoc, FRotator& OutRot) const;

    // --- Sequencer camera cache ---
    void RefreshSequencerCamera();
    UPROPERTY()
    mutable TObjectPtr<AActor> SequencerCameraCache;
    mutable bool bSequencerCacheValid = false;

    // --- Async texture loading ---
    UPROPERTY()
    TMap<FSoftObjectPath, TObjectPtr<UTexture2D>> AsyncTextureCache;
    TArray<FSoftObjectPath> AsyncTextureCacheOrder;
    TMap<FSoftObjectPath, TPair<TSharedPtr<struct FStreamableHandle>, int32>> ActiveTextureLoads;
    int32 LoadGeneration = 0;
    int32 MaxAsyncTextureCacheSize = 256;
    void OnAsyncTexturesLoaded();
    void EnforceAsyncCacheSize();
    UTexture2D* ResolveTexture(const TSoftObjectPtr<UTexture2D>& SoftPtr);

    // --- Layer visibility overrides (editor eye-toggle) ---
    UPROPERTY()
    TMap<FName, bool> LayerVisibilityOverrides;

    // --- Last-applied texture cache ---
    UPROPERTY()
    TMap<FName, FFaceAppliedTextures> LastAppliedTextures;
    UPROPERTY()
    TMap<FName, FFaceAppliedTextures> LastAppliedNestedTextures;

    // --- Precomputed nested art FName keys ---
    TMap<FName, TMap<FName, FName>> NestedArtStateKeyCache;          // [LayerTag][ElementName] -> StateKey
    TMap<FName, TMap<FName, FName>> NestedArtChildTagCache;          // [ParentElementName][ElementName] -> ChildTag
    TMap<FName, TMap<FName, TMap<FName, FName>>> NestedArtChildStateKeyCache; // [LayerTag][ParentElementName][ElementName] -> StateKey
    bool bNestedArtCacheDirty = false;
    void BuildNestedArtCache();
};
