#include "FaceParallaxComponent.h"
#include "FaceParallaxPreset.h"
#include "DepthDebugVisualizerComponent.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"

UFaceParallaxComponent::UFaceParallaxComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    CurrentState = EFaceAngleState::Front;
    PreviousState = EFaceAngleState::Front;
    BlendAlpha = 1.0f;
    CurrentYaw = 0.0f;
    CurrentPitch = 0.0f;
    bIsInTransition = false;
    ActivePreset = nullptr;

    NextBlinkCountdown = FMath::RandRange(BlinkIntervalMin, BlinkIntervalMax);
    PreviousExpression = EExpression::Neutral;
    ExpressionBlendAlpha = 1.0f;
    bExpressionTransitioning = false;
    bIsVisemePlaying = false;
    CurrentViseme = EViseme::Ah;
    VisemeFrameIndex = 0;
    VisemeFrameTimer = 0.0f;

    FFaceLayerDef DefaultLayer;
    DefaultLayer.LayerTag = "FaceLayer";
    DefaultLayer.DepthScale = 1.0f;
    DefaultLayer.bInvertParallax = false;
    LayerDefinitions.Add(DefaultLayer);
}

void UFaceParallaxComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* Owner = GetOwner();
    if (!Owner)
    {
        LogWarning("No owner actor assigned.");
        return;
    }

    OwnerMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
    if (!OwnerMesh)
    {
        LogWarning("No USkeletalMeshComponent found on owner.");
    }

    CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
    if (!CameraManager)
    {
        LogWarning("No APlayerCameraManager found.");
    }

    InitializeMaterials();
    LayerParallaxOffsets.SetNum(LayerDefinitions.Num());
}

void UFaceParallaxComponent::InitializeMaterials()
{
    if (!bUseMaterialDrivenDepth) return;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    TArray<UPrimitiveComponent*> PrimitiveComponents;
    Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

    int32 TotalTagged = 0;
    for (const FFaceLayerDef& LayerDef : LayerDefinitions)
    {
        FName Tag = LayerDef.LayerTag;
        for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
        {
            if (PrimComp && PrimComp->ComponentHasTag(Tag))
            {
                TArray<UMaterialInstanceDynamic*>& LayerMats = FaceMaterialsByLayer.FindOrAdd(Tag);
                for (int32 i = 0; i < PrimComp->GetNumMaterials(); ++i)
                {
                    UMaterialInstanceDynamic* DynMat = PrimComp->CreateAndSetMaterialInstanceDynamic(i);
                    if (DynMat)
                    {
                        LayerMats.Add(DynMat);
                        ++TotalTagged;
                    }
                }
            }
        }
    }

    if (TotalTagged == 0)
    {
        LogWarning("No primitive components found with any LayerTag. Depth map material parameters will not be driven.");
    }
}

void UFaceParallaxComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!OwnerMesh || !CameraManager) return;

    float DeltaYaw, DeltaPitch;
    CalculateLookDelta(DeltaYaw, DeltaPitch);

    CurrentYaw = DeltaYaw;
    CurrentPitch = DeltaPitch;

    UpdateStateMachine(DeltaYaw, DeltaPitch, DeltaTime);
    UpdateParallaxOffsets(DeltaTime);
    UpdateBlinkTick(DeltaTime);
    UpdateExpressionTick(DeltaTime);
    UpdateVisemeTick(DeltaTime);
    UpdateMaterialParameters();
}

void UFaceParallaxComponent::CalculateLookDelta(float& OutYaw, float& OutPitch)
{
    FVector HeadLoc = OwnerMesh->GetSocketLocation(HeadBoneName);
    FVector CamLoc = CameraManager->GetCameraLocation();
    FVector ToCamera = (CamLoc - HeadLoc).GetSafeNormal();

    if (ToCamera.IsNearlyZero())
    {
        OutYaw = 0.0f;
        OutPitch = 0.0f;
        return;
    }

    FRotator HeadRot = OwnerMesh->GetSocketRotation(HeadBoneName);
    FVector LocalToCamera = HeadRot.UnrotateVector(ToCamera);

    OutYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalToCamera.Y, LocalToCamera.X));
    OutPitch = FMath::RadiansToDegrees(FMath::Atan2(LocalToCamera.Z, FMath::Sqrt(
        LocalToCamera.X * LocalToCamera.X + LocalToCamera.Y * LocalToCamera.Y)));
}

EFaceAngleState UFaceParallaxComponent::DetermineStateFromAngles(float Yaw, float Pitch)
{
    if (Pitch > TopViewPitchThreshold)    return EFaceAngleState::Top;
    if (Pitch < BottomViewPitchThreshold) return EFaceAngleState::Bottom;

    if (HalfZoneWidth <= 0.0f) return EFaceAngleState::Front;

    if (Yaw > -HalfZoneWidth && Yaw <= HalfZoneWidth) return EFaceAngleState::Front;

    float Z2 = HalfZoneWidth * 2.0f;
    float Z3 = HalfZoneWidth * 3.0f;
    float Z4 = HalfZoneWidth * 4.0f;
    float Z5 = HalfZoneWidth * 5.0f;
    float Z6 = HalfZoneWidth * 6.0f;
    float Z7 = HalfZoneWidth * 7.0f;

    if (Yaw > HalfZoneWidth && Yaw <= Z3)        return EFaceAngleState::ThreeQuarterRight;
    if (Yaw > Z3 && Yaw <= Z5)                    return EFaceAngleState::RightProfile;
    if (Yaw > Z5 && Yaw <= Z7)                    return EFaceAngleState::BackRight;
    if (Yaw > Z7 || Yaw <= -Z7)                   return EFaceAngleState::Back;
    if (Yaw > -Z7 && Yaw <= -Z5)                  return EFaceAngleState::BackLeft;
    if (Yaw > -Z5 && Yaw <= -Z3)                  return EFaceAngleState::LeftProfile;
    if (Yaw > -Z3 && Yaw <= -HalfZoneWidth)       return EFaceAngleState::ThreeQuarterLeft;

    return EFaceAngleState::Front;
}

float UFaceParallaxComponent::GetZoneCenterYaw(EFaceAngleState State) const
{
    switch (State)
    {
        case EFaceAngleState::Front:              return 0.0f;
        case EFaceAngleState::ThreeQuarterRight:  return HalfZoneWidth * 2.0f;
        case EFaceAngleState::RightProfile:       return HalfZoneWidth * 4.0f;
        case EFaceAngleState::BackRight:          return HalfZoneWidth * 6.0f;
        case EFaceAngleState::Back:               return 180.0f;
        case EFaceAngleState::BackLeft:           return -HalfZoneWidth * 6.0f;
        case EFaceAngleState::LeftProfile:        return -HalfZoneWidth * 4.0f;
        case EFaceAngleState::ThreeQuarterLeft:   return -HalfZoneWidth * 2.0f;
        default:                                  return 0.0f;
    }
}

float UFaceParallaxComponent::GetZoneCenterPitch(EFaceAngleState State) const
{
    switch (State)
    {
        case EFaceAngleState::Top:    return 90.0f;
        case EFaceAngleState::Bottom: return -90.0f;
        default:                      return 0.0f;
    }
}

void UFaceParallaxComponent::UpdateStateMachine(float Yaw, float Pitch, float DeltaTime)
{
    EFaceAngleState RawState = DetermineStateFromAngles(Yaw, Pitch);

    if (RawState != CurrentState && RawState != PreviousState)
    {
        if (HysteresisFramesRemaining > 0 && RawState == HysteresisPendingState)
        {
            --HysteresisFramesRemaining;
        }
        else
        {
            HysteresisFramesRemaining = FMath::Max(1, HysteresisFrames);
            HysteresisPendingState = RawState;
        }

        if (HysteresisFramesRemaining <= 0)
        {
            PreviousState = CurrentState;
            CurrentState = RawState;
            BlendAlpha = 0.0f;
            bIsInTransition = true;

            if (bAutoApplyPreset)
            {
                CaptureCurrentTextures();
                ApplyCurrentStateTextures();
                SetPreviousStateTextures();
            }

            StopAnimationsOnStateChange();
            OnFaceStateChanged.Broadcast(CurrentState, PreviousState);
        }
    }
    else
    {
        HysteresisFramesRemaining = 0;
    }

    if (bIsInTransition)
    {
        if (bUseContinuousBlending)
        {
            float TargetAlpha = 1.0f;
            float YawCenter = GetZoneCenterYaw(CurrentState);
            bool bIsVerticalState = (CurrentState == EFaceAngleState::Top || CurrentState == EFaceAngleState::Bottom);

            if (!bIsVerticalState && BlendWindowWidth > 0.0f)
            {
                float YawDelta = Yaw - YawCenter;
                while (YawDelta > 180.0f) YawDelta -= 360.0f;
                while (YawDelta < -180.0f) YawDelta += 360.0f;
                float DistToEdge = FMath::Abs(YawDelta) - (HalfZoneWidth - BlendWindowWidth * 0.5f);
                TargetAlpha = 1.0f - FMath::Clamp(DistToEdge / BlendWindowWidth, 0.0f, 1.0f);
            }

            BlendAlpha = FMath::FInterpTo(BlendAlpha, TargetAlpha, DeltaTime, CrossfadeSpeed);

            if (TargetAlpha >= 1.0f && BlendAlpha >= 0.995f)
            {
                BlendAlpha = 1.0f;
                bIsInTransition = false;
            }
        }
        else
        {
            BlendAlpha = FMath::FInterpTo(BlendAlpha, 1.0f, DeltaTime, CrossfadeSpeed);
            if (BlendAlpha >= 1.0f)
            {
                BlendAlpha = 1.0f;
                bIsInTransition = false;
            }
        }
    }
    else
    {
        BlendAlpha = 1.0f;
    }
}

void UFaceParallaxComponent::UpdateParallaxOffsets(float DeltaTime)
{
    LayerParallaxOffsets.SetNum(LayerDefinitions.Num());

    for (int32 i = 0; i < LayerDefinitions.Num(); ++i)
    {
        LayerParallaxOffsets[i] = ComputeOffsetForState(CurrentState, CurrentYaw, CurrentPitch, i);
    }
}

FVector2D UFaceParallaxComponent::ComputeOffsetForState(EFaceAngleState State, float Yaw, float Pitch,
    int32 LayerIndex) const
{
    if (LayerIndex < 0 || LayerIndex >= LayerDefinitions.Num())
    {
        return FVector2D::ZeroVector;
    }

    const FFaceLayerDef& Layer = LayerDefinitions[LayerIndex];
    float DepthFactor = Layer.DepthScale * (Layer.bInvertParallax ? -1.0f : 1.0f);

    bool bIsVerticalState = (State == EFaceAngleState::Top || State == EFaceAngleState::Bottom);
    float YawCenter = GetZoneCenterYaw(State);
    float PitchCenter = GetZoneCenterPitch(State);

    float YawDeviation = Yaw - YawCenter;
    while (YawDeviation > 180.0f) YawDeviation -= 360.0f;
    while (YawDeviation < -180.0f) YawDeviation += 360.0f;
    float PitchDeviation = Pitch - PitchCenter;

    if (bIsVerticalState)
    {
        float PitchThreshold = (State == EFaceAngleState::Top)
            ? TopViewPitchThreshold
            : FMath::Abs(BottomViewPitchThreshold);
        float PitchRange = 90.0f - PitchThreshold;
        float NormalizedPitch = (PitchRange > 0.0f)
            ? FMath::Clamp(PitchDeviation / PitchRange, -1.0f, 1.0f)
            : 0.0f;

        float YawRange = 180.0f;
        float YawNormalizedFull = FMath::Clamp(YawDeviation / YawRange, -1.0f, 1.0f);

        return FVector2D(
            YawNormalizedFull * DepthFactor * MaxParallaxOffset,
            NormalizedPitch * DepthFactor * MaxVerticalParallaxOffset
        );
    }
    else
    {
        float NormalizedYaw = (HalfZoneWidth > 0.0f)
            ? FMath::Clamp(YawDeviation / HalfZoneWidth, -1.0f, 1.0f)
            : 0.0f;
        float NormalizedPitch = (HalfZoneWidth > 0.0f)
            ? FMath::Clamp(PitchDeviation / HalfZoneWidth, -1.0f, 1.0f)
            : 0.0f;

        return FVector2D(
            NormalizedYaw * DepthFactor * MaxParallaxOffset,
            NormalizedPitch * DepthFactor * MaxVerticalParallaxOffset
        );
    }
}

void UFaceParallaxComponent::UpdateMaterialParameters()
{
    if (!bUseMaterialDrivenDepth || FaceMaterialsByLayer.Num() == 0) return;

    bool bAnyTopOrBottom = (CurrentState == EFaceAngleState::Top || CurrentState == EFaceAngleState::Bottom);
    bool bIsTop = (CurrentState == EFaceAngleState::Top);

    // Compute yaw deviation for dynamic art offset
    float YawCenter = GetZoneCenterYaw(CurrentState);
    float YawDeviation = CurrentYaw - YawCenter;
    while (YawDeviation > 180.0f) YawDeviation -= 360.0f;
    while (YawDeviation < -180.0f) YawDeviation += 360.0f;
    float NormalizedYawDev = (HalfZoneWidth > 0.0f)
        ? FMath::Clamp(YawDeviation / HalfZoneWidth, -1.0f, 1.0f)
        : 0.0f;
    int32 GlobalLayerIdx = 0;
    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;

        float LayerDepthIntensity = DepthMapIntensity;
        if (GlobalLayerIdx < LayerDefinitions.Num())
        {
            LayerDepthIntensity *= LayerDefinitions[GlobalLayerIdx].DepthMapIntensity;
        }

        FFaceArtTransform EffectiveTransform;

        bool bHasTransform = false;
        if (ActivePreset)
        {
            const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerTag);
            if (Slot.Textures.IsValid())
            {
                EffectiveTransform = Slot.GetEffectiveTransform(CurrentState);
                bHasTransform = true;
            }
        }

        // Previous state textures for crossfade
        const FFaceTextureSet* PrevTexSet = PreviousTextureSets.Find(LayerTag);

        // Blended parallax offset — compute both states' offsets and lerp during transitions
        FVector2D BlendedOffset = FVector2D::ZeroVector;
        if (GlobalLayerIdx < LayerParallaxOffsets.Num())
        {
            if (bIsInTransition && PreviousState != CurrentState)
            {
                FVector2D PrevOffset = ComputeOffsetForState(PreviousState, CurrentYaw, CurrentPitch, GlobalLayerIdx);
                FVector2D CurOffset = LayerParallaxOffsets[GlobalLayerIdx];
                BlendedOffset = FMath::Lerp(PrevOffset, CurOffset, BlendAlpha);
            }
            else
            {
                BlendedOffset = LayerParallaxOffsets[GlobalLayerIdx];
            }
        }

        for (UMaterialInstanceDynamic* Mat : LayerPair.Value)
        {
            if (!Mat) continue;

            Mat->SetScalarParameterValue(FName("StateBlendAlpha"), BlendAlpha);
            Mat->SetVectorParameterValue(FName("ParallaxOffset"),
                FLinearColor(BlendedOffset.X, BlendedOffset.Y, 0.0f, 0.0f));
            Mat->SetScalarParameterValue(FName("DepthIntensity"), LayerDepthIntensity);
            Mat->SetScalarParameterValue(FName("DebugDepth"), bEnableMaterialDebugMode ? 1.0f : 0.0f);
            Mat->SetScalarParameterValue(FName("IsTopDown"), bAnyTopOrBottom ? 1.0f : 0.0f);
            Mat->SetScalarParameterValue(FName("IsTopView"), bIsTop ? 1.0f : 0.0f);

            // Previous state textures for crossfade blending
            if (PrevTexSet && PrevTexSet->IsValid())
            {
                if (PrevTexSet->Albedo) Mat->SetTextureParameterValue(AlbedoPrevParamName, PrevTexSet->Albedo);
                if (PrevTexSet->Normal) Mat->SetTextureParameterValue(NormalPrevParamName, PrevTexSet->Normal);
                if (PrevTexSet->Depth)  Mat->SetTextureParameterValue(DepthPrevParamName, PrevTexSet->Depth);
            }

            // Blink animation — override current textures with blink frame if blinking.
            // Runs before viseme; viseme overrides blink on layers that have both.
            if (bIsBlinking && ActivePreset)
            {
                const FFaceArtSlot& BlinkSlot = ActivePreset->GetSlot(CurrentState, LayerTag);
                if (BlinkFrameIndex >= 0 && BlinkFrameIndex < BlinkSlot.BlinkFrames.Num())
                {
                    const FFaceTextureSet& BlinkFrame = BlinkSlot.BlinkFrames[BlinkFrameIndex];
                    if (BlinkFrame.Albedo) Mat->SetTextureParameterValue(AlbedoParamName, BlinkFrame.Albedo);
                    if (BlinkFrame.Normal) Mat->SetTextureParameterValue(NormalParamName, BlinkFrame.Normal);
                    if (BlinkFrame.Depth)  Mat->SetTextureParameterValue(DepthParamName, BlinkFrame.Depth);
                }
            }

            // Viseme animation — override textures with current viseme frame if playing.
            // Runs after blink, so viseme takes priority over blink for any layer with both.
            if (bIsVisemePlaying && ActivePreset)
            {
                const FFaceArtSlot& VisemeSlot = ActivePreset->GetSlot(CurrentState, LayerTag);
                const TMap<TEnumAsByte<EViseme>, TArray<FFaceTextureSet>>* ExprVisemes =
                    VisemeSlot.VisemeFrameSets.Find(CurrentExpression);
                if (ExprVisemes)
                {
                    const TArray<FFaceTextureSet>* VisemeFrames = ExprVisemes->Find(CurrentViseme);
                    if (VisemeFrames && VisemeFrameIndex >= 0 && VisemeFrameIndex < VisemeFrames->Num())
                    {
                        const FFaceTextureSet& VisemeFrame = (*VisemeFrames)[VisemeFrameIndex];
                        if (VisemeFrame.Albedo) Mat->SetTextureParameterValue(AlbedoParamName, VisemeFrame.Albedo);
                        if (VisemeFrame.Normal) Mat->SetTextureParameterValue(NormalParamName, VisemeFrame.Normal);
                        if (VisemeFrame.Depth)  Mat->SetTextureParameterValue(DepthParamName, VisemeFrame.Depth);
                    }
                }
            }

            // Expression crossfade
            Mat->SetScalarParameterValue(FName("ExpressionBlendAlpha"), ExpressionBlendAlpha);
            if (bExpressionTransitioning)
            {
                const FFaceTextureSet* ExprPrevTex = ExpressionPreviousTextureSets.Find(LayerTag);
                if (ExprPrevTex && ExprPrevTex->IsValid())
                {
                    if (ExprPrevTex->Albedo) Mat->SetTextureParameterValue(ExpressionAlbedoPrevParamName, ExprPrevTex->Albedo);
                    if (ExprPrevTex->Normal) Mat->SetTextureParameterValue(ExpressionNormalPrevParamName, ExprPrevTex->Normal);
                    if (ExprPrevTex->Depth)  Mat->SetTextureParameterValue(ExpressionDepthPrevParamName, ExprPrevTex->Depth);
                }
            }

            if (bHasTransform)
            {
                FVector2D FinalArtPos = EffectiveTransform.Position;

            // Dynamic art offset — drives ArtPosition from yaw/pitch deviation
            // so eye textures appear to track the camera within the view zone
            if (bDriveArtPositionFromYaw)
            {
                bool bVertical = (CurrentState == EFaceAngleState::Top || CurrentState == EFaceAngleState::Bottom);
                if (bVertical)
                {
                    float PitchCenter = GetZoneCenterPitch(CurrentState);
                    float PitchDeviation = CurrentPitch - PitchCenter;
                    float NormPitchDev = FMath::Clamp(PitchDeviation / FMath::Max(1.0f, HalfZoneWidth), -1.0f, 1.0f);
                    FinalArtPos.Y += NormPitchDev * MaxYawArtOffset;
                }
                else
                {
                    FinalArtPos.X += NormalizedYawDev * MaxYawArtOffset;
                }
            }

                Mat->SetVectorParameterValue(ArtPositionParamName,
                    FLinearColor(FinalArtPos.X, FinalArtPos.Y, 0.0f, 0.0f));
                Mat->SetVectorParameterValue(ArtScaleParamName,
                    FLinearColor(EffectiveTransform.Scale.X, EffectiveTransform.Scale.Y, 0.0f, 0.0f));
                Mat->SetScalarParameterValue(ArtRotationParamName, EffectiveTransform.Rotation);
            }
        }
        ++GlobalLayerIdx;
    }
}

void UFaceParallaxComponent::ApplyPreset(UFaceParallaxPreset* Preset)
{
    ActivePreset = Preset;
    if (ActivePreset)
    {
        ApplyCurrentStateTextures();
    }
}

UTexture2D* UFaceParallaxComponent::GetCurrentDepthTexture() const
{
    if (!ActivePreset) return nullptr;
    const FFaceViewStateLayerSet* StateSet = ActivePreset->ViewAssignments.Find(CurrentState);
    if (!StateSet) return nullptr;

    // Return the depth texture from the first layer that has one
    for (const auto& LayerPair : StateSet->Layers)
    {
        if (LayerPair.Value.Textures.Depth)
        {
            return LayerPair.Value.Textures.Depth;
        }
    }
    return nullptr;
}

void UFaceParallaxComponent::SetStateTextures(EFaceAngleState State)
{
    if (!ActivePreset) return;

    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);

        // Use expression texture variant if available for current expression
        const FFaceTextureSet* ExprTex = Slot.ExpressionTextures.Find(CurrentExpression);
        const FFaceTextureSet& TexSet = ExprTex ? *ExprTex : Slot.Textures;
        if (!TexSet.IsValid()) continue;

        for (UMaterialInstanceDynamic* Mat : LayerPair.Value)
        {
            if (TexSet.Albedo)
                Mat->SetTextureParameterValue(AlbedoParamName, TexSet.Albedo);
            if (TexSet.Normal)
                Mat->SetTextureParameterValue(NormalParamName, TexSet.Normal);
            if (TexSet.Depth)
                Mat->SetTextureParameterValue(DepthParamName, TexSet.Depth);
        }
    }

    // Notify depth debug visualizer on the same actor
    AActor* Owner = GetOwner();
    if (Owner)
    {
        UDepthDebugVisualizerComponent* Vis = Owner->FindComponentByClass<UDepthDebugVisualizerComponent>();
        if (Vis)
        {
            UTexture2D* DepthTex = GetCurrentDepthTexture();
            if (DepthTex)
            {
                Vis->RebuildMeshFromDepthMap(DepthTex);
            }
        }
    }
}

void UFaceParallaxComponent::ApplyCurrentStateTextures()
{
    SetStateTextures(CurrentState);
}

void UFaceParallaxComponent::CaptureCurrentTextures()
{
    if (!ActivePreset) return;

    PreviousTextureSets.Empty();
    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        // Capture textures from PreviousState (the state we are leaving)
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(PreviousState, LayerTag);
        if (Slot.Textures.IsValid())
        {
            PreviousTextureSets.Add(LayerTag, Slot.Textures);
        }
    }
}

void UFaceParallaxComponent::SetPreviousStateTextures()
{
    if (!ActivePreset) return;

    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceTextureSet* PrevTex = PreviousTextureSets.Find(LayerTag);
        if (!PrevTex || !PrevTex->IsValid()) continue;

        for (UMaterialInstanceDynamic* Mat : LayerPair.Value)
        {
            if (PrevTex->Albedo)
                Mat->SetTextureParameterValue(AlbedoPrevParamName, PrevTex->Albedo);
            if (PrevTex->Normal)
                Mat->SetTextureParameterValue(NormalPrevParamName, PrevTex->Normal);
            if (PrevTex->Depth)
                Mat->SetTextureParameterValue(DepthPrevParamName, PrevTex->Depth);
        }
    }
}

void UFaceParallaxComponent::PlayViseme(EViseme NewViseme)
{
    if (!ActivePreset || !bVisemeEnabled) return;

    CurrentViseme = NewViseme;
    VisemeFrameIndex = 0;
    VisemeFrameTimer = 0.0f;
    bIsVisemePlaying = true;

    OnVisemeStarted.Broadcast(CurrentState, PreviousState);
}

void UFaceParallaxComponent::StopViseme()
{
    if (!bIsVisemePlaying) return;

    bIsVisemePlaying = false;
    VisemeFrameIndex = 0;
    VisemeFrameTimer = 0.0f;

    OnVisemeCompleted.Broadcast(CurrentState, PreviousState);
}

void UFaceParallaxComponent::UpdateVisemeTick(float DeltaTime)
{
    if (!bIsVisemePlaying || !ActivePreset)
    {
        bIsVisemePlaying = false;
        return;
    }

    VisemeFrameTimer += DeltaTime;
    if (VisemeFrameTimer < VisemeFrameDuration) return;

    VisemeFrameTimer = 0.0f;
    ++VisemeFrameIndex;

    // Find max frame count across layers for current expression + viseme
    int32 MaxFrames = 0;
    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerPair.Key);
        const TMap<TEnumAsByte<EViseme>, TArray<FFaceTextureSet>>* ExprVisemes =
            Slot.VisemeFrameSets.Find(CurrentExpression);
        if (ExprVisemes)
        {
            const TArray<FFaceTextureSet>* VisemeFrames = ExprVisemes->Find(CurrentViseme);
            if (VisemeFrames)
            {
                MaxFrames = FMath::Max(MaxFrames, VisemeFrames->Num());
            }
        }
    }

    if (VisemeFrameIndex >= MaxFrames)
    {
        bIsVisemePlaying = false;
        VisemeFrameIndex = 0;
        VisemeFrameTimer = 0.0f;

        OnVisemeCompleted.Broadcast(CurrentState, PreviousState);
    }
}

void UFaceParallaxComponent::StopAnimationsOnStateChange()
{
    if (bIsBlinking)
    {
        bIsBlinking = false;
        BlinkFrameIndex = 0;
        NextBlinkCountdown = FMath::RandRange(BlinkIntervalMin, BlinkIntervalMax);
        OnBlinkCompleted.Broadcast(CurrentState, PreviousState);
    }

    if (bIsVisemePlaying)
    {
        StopViseme();
    }

    if (bExpressionTransitioning)
    {
        bExpressionTransitioning = false;
        ExpressionBlendAlpha = 1.0f;
        ExpressionPreviousTextureSets.Empty();
    }
}

void UFaceParallaxComponent::LogWarning(const FString& Message) const
{
    AActor* Owner = GetOwner();
    FString OwnerName = Owner ? Owner->GetName() : TEXT("NULL Owner");
    UE_LOG(LogTemp, Warning, TEXT("[FaceParallaxComponent on %s] %s"), *OwnerName, *Message);
}

void UFaceParallaxComponent::ForceBlink()
{
    if (!ActivePreset) return;

    bIsBlinking = true;
    BlinkFrameIndex = 0;
    BlinkFrameTimer = 0.0f;

    OnBlinkStarted.Broadcast(CurrentState, PreviousState);
}

void UFaceParallaxComponent::SetBlinkInterval(float Min, float Max)
{
    BlinkIntervalMin = FMath::Max(0.5f, Min);
    BlinkIntervalMax = FMath::Max(BlinkIntervalMin, Max);
    NextBlinkCountdown = FMath::RandRange(BlinkIntervalMin, BlinkIntervalMax);
}

void UFaceParallaxComponent::UpdateBlinkTick(float DeltaTime)
{
    if (!bBlinkingEnabled || !ActivePreset)
    {
        bIsBlinking = false;
        return;
    }

    // Countdown to next blink
    if (!bIsBlinking)
    {
        NextBlinkCountdown -= DeltaTime;
        if (NextBlinkCountdown <= 0.0f)
        {
            ForceBlink();
        }
        return;
    }

    // Advance blink frame
    BlinkFrameTimer += DeltaTime;
    if (BlinkFrameTimer >= BlinkFrameDuration)
    {
        BlinkFrameTimer = 0.0f;
        ++BlinkFrameIndex;

        // Find the max frame count across all current-state slots
        int32 MaxFrames = 0;
        for (const auto& LayerPair : FaceMaterialsByLayer)
        {
            const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerPair.Key);
            MaxFrames = FMath::Max(MaxFrames, Slot.BlinkFrames.Num());
        }

        if (BlinkFrameIndex >= MaxFrames)
        {
            bIsBlinking = false;
            BlinkFrameIndex = 0;
            NextBlinkCountdown = FMath::RandRange(BlinkIntervalMin, BlinkIntervalMax);
            OnBlinkCompleted.Broadcast(CurrentState, PreviousState);
        }
    }
}

void UFaceParallaxComponent::SetExpression(EExpression NewExpression)
{
    if (NewExpression == CurrentExpression && !bExpressionTransitioning) return;

    if (!ActivePreset) return;

    // Capture current expression textures as previous
    ExpressionPreviousTextureSets.Empty();
    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerTag);
        const FFaceTextureSet* ExprTex = Slot.ExpressionTextures.Find(CurrentExpression);
        if (ExprTex && ExprTex->IsValid())
        {
            ExpressionPreviousTextureSets.Add(LayerTag, *ExprTex);
        }
        else if (Slot.Textures.IsValid())
        {
            ExpressionPreviousTextureSets.Add(LayerTag, Slot.Textures);
        }
    }

    PreviousExpression = CurrentExpression;
    CurrentExpression = NewExpression;
    ExpressionBlendAlpha = 0.0f;
    bExpressionTransitioning = true;

    // Push new expression textures
    ApplyExpressionTextures(CurrentState);

    OnExpressionChanged.Broadcast(CurrentState, PreviousState);
}

void UFaceParallaxComponent::UpdateExpressionTick(float DeltaTime)
{
    if (!bExpressionTransitioning || !ActivePreset)
    {
        ExpressionBlendAlpha = 1.0f;
        bExpressionTransitioning = false;
        return;
    }

    float Speed = (ExpressionCrossfadeDuration > 0.0f) ? (1.0f / ExpressionCrossfadeDuration) : 10.0f;
    ExpressionBlendAlpha = FMath::FInterpTo(ExpressionBlendAlpha, 1.0f, DeltaTime, Speed);

    if (ExpressionBlendAlpha >= 1.0f)
    {
        ExpressionBlendAlpha = 1.0f;
        bExpressionTransitioning = false;
        ExpressionPreviousTextureSets.Empty();
    }
}

void UFaceParallaxComponent::ApplyExpressionTextures(EFaceAngleState State)
{
    if (!ActivePreset) return;

    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);

        const FFaceTextureSet* ExprTex = Slot.ExpressionTextures.Find(CurrentExpression);
        const FFaceTextureSet& TexSet = ExprTex ? *ExprTex : Slot.Textures;

        if (!TexSet.IsValid()) continue;

        for (UMaterialInstanceDynamic* Mat : LayerPair.Value)
        {
            if (TexSet.Albedo)
                Mat->SetTextureParameterValue(AlbedoParamName, TexSet.Albedo);
            if (TexSet.Normal)
                Mat->SetTextureParameterValue(NormalParamName, TexSet.Normal);
            if (TexSet.Depth)
                Mat->SetTextureParameterValue(DepthParamName, TexSet.Depth);
        }
    }
}
