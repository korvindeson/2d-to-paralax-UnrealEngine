#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FaceParallaxTypes.h"
#include "FaceParallaxComponent.generated.h"

class UMaterialInstanceDynamic;
class UFaceParallaxPreset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFaceStateChangedSignature, EFaceAngleState, NewState, EFaceAngleState, OldState);

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
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class FACEPARALLAX_API UFaceParallaxComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFaceParallaxComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // --- SKELETAL MESH SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Skeletal Mesh")
    FName HeadBoneName = "head";

    // --- VIEW ANGLE SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|View Angles")
    float TopViewPitchThreshold = 60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|View Angles")
    float BottomViewPitchThreshold = -60.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|View Angles")
    float HalfZoneWidth = 22.5f;

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

    // --- PARALLAX SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parallax")
    float MaxParallaxOffset = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parallax")
    float MaxVerticalParallaxOffset = 3.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Parallax")
    TArray<FFaceLayerDef> LayerDefinitions;

    // --- DEPTH MAP SETTINGS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Depth Maps")
    bool bUseMaterialDrivenDepth = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Depth Maps")
    float DepthMapIntensity = 1.0f;

    // --- MATERIAL TEXTURE PARAMETERS ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params")
    FName AlbedoParamName = "AlbedoTexture";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params")
    FName NormalParamName = "NormalTexture";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Material Texture Params")
    FName DepthParamName = "DepthTexture";

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

    // --- DYNAMIC ART OFFSET ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params",
        meta = (DisplayName = "Drive ArtPosition X from yaw deviation"))
    bool bDriveArtPositionFromYaw = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params",
        meta = (DisplayName = "Max yaw-driven art offset (UV units)",
            ClampMin = "0.0", ClampMax = "1.0"))
    float MaxYawArtOffset = 0.05f;

    // --- ART TRANSFORM PARAMETERS (per-part UV manipulation) ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params")
    FName ArtPositionParamName = "ArtPosition";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params")
    FName ArtScaleParamName = "ArtScale";

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Art Transform Params")
    FName ArtRotationParamName = "ArtRotation";

    // --- PRESET ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Preset")
    class UFaceParallaxPreset* ActivePreset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Preset")
    bool bAutoApplyPreset = true;

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

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnBlinkStarted;

    UPROPERTY(BlueprintAssignable, Category = "Face Parallax|Events")
    FOnFaceStateChangedSignature OnBlinkCompleted;

    // --- EXPRESSION SYSTEM ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Expression")
    EExpression CurrentExpression = EExpression::Neutral;

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

    // --- MATERIAL DEBUG ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face Parallax|Debug")
    bool bEnableMaterialDebugMode = false;

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

    // --- ACCESSORS FOR EDITOR ---
    TMap<FName, TArray<UMaterialInstanceDynamic*>>& GetMaterialLayers() { return FaceMaterialsByLayer; }
    const TMap<FName, TArray<UMaterialInstanceDynamic*>>& GetMaterialLayers() const { return FaceMaterialsByLayer; }

private:
    UPROPERTY()
    class USkeletalMeshComponent* OwnerMesh;

    UPROPERTY()
    class APlayerCameraManager* CameraManager;

    UPROPERTY()
    TMap<FName, TArray<UMaterialInstanceDynamic*>> FaceMaterialsByLayer;

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
    float SwooshSmearAngle = 0.0f;
    int32 SwooshProceduralTick = 0;
    TArray<FFaceTextureSet> SwooshFrames;

    void UpdateBlinkTick(float DeltaTime);
    void UpdateExpressionTick(float DeltaTime);
    void UpdateVisemeTick(float DeltaTime);
    void UpdateSwooshTick(float DeltaTime);
    void ApplyExpressionTextures(EFaceAngleState State);

    void InitializeMaterials();
    void UpdateMaterialParameters();
    void ApplyCurrentStateTextures();
    void CaptureCurrentTextures();
    void SetPreviousStateTextures();

    void CalculateLookDelta(float& OutYaw, float& OutPitch);
    EFaceAngleState DetermineStateFromAngles(float Yaw, float Pitch);
    float GetZoneCenterYaw(EFaceAngleState State) const;
    float GetZoneCenterPitch(EFaceAngleState State) const;

    void UpdateStateMachine(float Yaw, float Pitch, float DeltaTime, float AngularVelocity = 0.0f);
    void UpdateParallaxOffsets(float DeltaTime);
    FVector2D ComputeOffsetForState(EFaceAngleState State, float Yaw, float Pitch, int32 LayerIndex) const;

    void StopAnimationsOnStateChange();

    void LogWarning(const FString& Message) const;
};
