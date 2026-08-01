#include "FaceParallaxComponent.h"
#include "FaceParallaxPreset.h"
#include "FaceParallaxPreviewActor.h"
#include "DepthDebugVisualizerComponent.h"
#include "GameFramework/Actor.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include <functional>

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

    SwooshPhase = ESwooshPhase::Inactive;
    SwooshFrameIndex = 0;
    SwooshFrameTimer = 0.0f;
    SwooshBlendOutElapsed = 0.0f;
    PreviousFrameYaw = 0.0f;
    PreviousFramePitch = 0.0f;
    SwooshSmearAngle = 0.0f;
    SwooshProceduralTick = 0;

    FFaceLayerDef DefaultLayer;
    DefaultLayer.LayerTag = "FaceLayer";
    DefaultLayer.DepthScale = 1.0f;
    DefaultLayer.bInvertParallax = false;
    LayerDefinitions.Add(DefaultLayer);

    OutlineViewStates.Add(EFaceAngleState::Front);
    OutlineViewStates.Add(EFaceAngleState::RightProfile);
    OutlineViewStates.Add(EFaceAngleState::LeftProfile);
    OutlineViewStates.Add(EFaceAngleState::Top);
    OutlineViewStates.Add(EFaceAngleState::Bottom);
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

    // Camera is resolved per-tick via GetCameraLocationAndRotation() — no longer requires CameraManager.
    // Legacy CameraManager var kept for backward compat; runtime uses CameraSource-based resolution.
    CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);

    RefreshSequencerCamera();

    InitializeMaterials();
    LayerParallaxOffsets.SetNum(LayerDefinitions.Num());

    if (bAutoSpawnLayerQuads && !bFoundTaggedPrimitives && OwnerMesh)
    {
        SpawnLayerQuads();
    }
}

void UFaceParallaxComponent::InitializeMaterials()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    TArray<UPrimitiveComponent*> PrimitiveComponents;
    Owner->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

    // Layer material discovery (only if depth-driven is enabled)
    int32 TotalTagged = 0;
    if (bUseMaterialDrivenDepth)
    {
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
    }

    // Discover nested art element primitives tagged LayerTag_ElementName (or LayerTag_Parent_Child for recursion)
    // Always runs, independent of bUseMaterialDrivenDepth
    NestedMaterialsByElement.Empty();
    for (UPrimitiveComponent* PrimComp : PrimitiveComponents)
    {
        if (!PrimComp) continue;

        TArray<FName> Tags = PrimComp->ComponentTags;
        for (const FName& Tag : Tags)
        {
            FString TagStr = Tag.ToString();
            // Use last underscore to find the layer prefix — supports multi-segment names like "FaceLayer_Parent_Child"
            int32 UnderscoreIdx = -1;
            TagStr.FindLastChar('_', UnderscoreIdx);
            if (UnderscoreIdx > 0 && UnderscoreIdx < TagStr.Len() - 1)
            {
                FName LayerPrefix = FName(TagStr.Left(UnderscoreIdx));
                // Check if the prefix (the part before the last underscore) is itself a known nested element or layer
                // The first segment before the first underscore is the layer tag
                int32 FirstUnderscore = INDEX_NONE;
                TagStr.FindChar('_', FirstUnderscore);
                if (FirstUnderscore > 0)
                {
                    FName RootLayer = FName(TagStr.Left(FirstUnderscore));
                    if (FaceMaterialsByLayer.Contains(RootLayer))
                    {
                        FName ElementFullTag = Tag;
                        TArray<UMaterialInstanceDynamic*>& ElemMats = NestedMaterialsByElement.FindOrAdd(ElementFullTag);
                        for (int32 i = 0; i < PrimComp->GetNumMaterials(); ++i)
                        {
                            UMaterialInstanceDynamic* DynMat = PrimComp->CreateAndSetMaterialInstanceDynamic(i);
                            if (DynMat)
                            {
                                ElemMats.Add(DynMat);
                            }
                        }
                    }
                }
            }
        }
    }

    if (TotalTagged == 0 && bUseMaterialDrivenDepth)
    {
        LogWarning("No primitive components found with any LayerTag. Depth map material parameters will not be driven.");
    }
    bFoundTaggedPrimitives = (TotalTagged > 0);
}

UStaticMeshComponent* UFaceParallaxComponent::SpawnQuadForTag(FName Tag, FVector2D QuadSize, float RotationDegrees)
{
    AActor* Owner = GetOwner();
    if (!Owner) return nullptr;

    UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!PlaneMesh)
    {
        LogWarning(FString::Printf(TEXT("SpawnQuadForTag: Could not load /Engine/BasicShapes/Plane. Quad '%s' will be invisible."), *Tag.ToString()));
    }

    UStaticMeshComponent* Quad = NewObject<UStaticMeshComponent>(
        Owner,
        MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(),
            *FString::Printf(TEXT("FaceQuad_%s"), *Tag.ToString())));

    if (PlaneMesh) Quad->SetStaticMesh(PlaneMesh);
    Quad->ComponentTags.AddUnique(Tag);
    Quad->SetMobility(EComponentMobility::Movable);
    Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Quad->SetCastShadow(false);
    Quad->bCastStaticShadow = false;
    Quad->SetVisibility(true);
    Quad->SetHiddenInGame(false);
    Quad->RegisterComponent();

    bool bAttachedToBone = false;
    if (OwnerMesh)
    {
        bAttachedToBone = Quad->AttachToComponent(OwnerMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, HeadBoneName);
        if (!bAttachedToBone)
        {
            LogWarning(FString::Printf(TEXT("SpawnQuadForTag: Bone '%s' not found on skeletal mesh — attaching to mesh root."), *HeadBoneName.ToString()));
            Quad->AttachToComponent(OwnerMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        }
        Quad->SetRelativeLocation(LayerQuadLocalOffset);
    }
    else if (USceneComponent* Root = Owner->GetRootComponent())
    {
        Quad->AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        Quad->SetRelativeLocation(LayerQuadLocalOffset);
    }
    else
    {
        Quad->SetWorldLocation(Owner->GetActorLocation());
    }

    // /Engine/BasicShapes/Plane is a 100x100 unit quad with UVs spanning 0..1 (the canvas).
    Quad->SetRelativeScale3D(FVector(QuadSize.X / 100.0f, QuadSize.Y / 100.0f, 1.0f));
    Quad->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f)); // Plane faces +Z — rotate to face forward (+X)
    if (!FMath::IsNearlyZero(RotationDegrees))
    {
        Quad->AddLocalRotation(FRotator(0.0f, 0.0f, RotationDegrees));
    }

    SpawnedLayerQuads.Add(Quad);
    return Quad;
}

int32 UFaceParallaxComponent::SpawnLayerQuads()
{
    AActor* Owner = GetOwner();
    if (!Owner)
    {
        LogWarning("SpawnLayerQuads: No owner actor.");
        return 0;
    }

    OwnerMesh = Owner->FindComponentByClass<USkeletalMeshComponent>();

    // The quad represents the preset canvas in UV space — match the canvas aspect.
    FVector2D Canvas = ActivePreset ? ActivePreset->CanvasSize : FVector2D(512.0f, 512.0f);
    if (Canvas.X <= 0.0f || Canvas.Y <= 0.0f) Canvas = FVector2D(512.0f, 512.0f);
    const float Aspect = Canvas.Y / Canvas.X;

    int32 Spawned = 0;

    TArray<UPrimitiveComponent*> OwnerPrims;
    Owner->GetComponents<UPrimitiveComponent>(OwnerPrims);

    for (const FFaceLayerDef& LayerDef : LayerDefinitions)
    {
        FName Tag = LayerDef.LayerTag;
        if (Tag.IsNone()) continue;

        // A preset is the source of truth for which layers have art — skip
        // layers it has no slot for (e.g. the constructor-default FaceLayer).
        if (ActivePreset)
        {
            bool bHasSlot = false;
            for (int32 S = 0; S <= (int32)EFaceAngleState::Bottom; ++S)
            {
                if (ActivePreset->GetAllLayerTags((EFaceAngleState)S).Contains(Tag))
                {
                    bHasSlot = true;
                    break;
                }
            }
            if (!bHasSlot) continue;
        }

        bool bHasTagged = false;
        for (UPrimitiveComponent* Prim : OwnerPrims)
        {
            if (Prim && Prim->ComponentHasTag(Tag)) { bHasTagged = true; break; }
        }
        if (bHasTagged) continue;

        const FVector2D QuadSize = FVector2D(LayerQuadWorldWidth, LayerQuadWorldWidth * Aspect);
        UStaticMeshComponent* Quad = SpawnQuadForTag(Tag, QuadSize, 0.0f);
        if (!Quad) continue;

        // Layer material instance — falls back to the master material if missing.
        UMaterialInterface* Mat = nullptr;
        FString MiPath = LayerMaterialPathRoot + Tag.ToString();
        if (!MiPath.Contains(TEXT(".")))
        {
            FString ObjectName = MiPath;
            const int32 LastSlash = MiPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
            if (LastSlash != INDEX_NONE) ObjectName = MiPath.RightChop(LastSlash + 1);
            MiPath += TEXT(".") + ObjectName;
        }
        Mat = LoadObject<UMaterialInterface>(nullptr, *MiPath);
        if (!Mat)
        {
            Mat = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Game/FaceParallax/Materials/M_FaceParallax_Master.M_FaceParallax_Master"));
            if (Mat)
            {
                LogWarning(FString::Printf(TEXT("SpawnLayerQuads: Material instance for layer '%s' not found at '%s' — using master material."),
                    *Tag.ToString(), *MiPath));
            }
        }
        if (Mat) Quad->SetMaterial(0, Mat);
        ++Spawned;

        // Front-state nested elements for this layer (tagged LayerTag_ElementName, recursive).
        if (ActivePreset)
        {
            const FFaceArtSlot& Slot = ActivePreset->GetSlot(EFaceAngleState::Front, Tag);
            int32 DepthIndex = 0;
            for (const FFaceNestedArt& Element : Slot.NestedElements)
            {
                Spawned += SpawnNestedQuadRecursive(Tag, Element, QuadSize, DepthIndex);
                ++DepthIndex;
            }
        }
    }

    if (Spawned > 0)
    {
        InitializeMaterials();
        if (ActivePreset)
        {
            ApplyCurrentStateTextures();
        }
    }

    return Spawned;
}

int32 UFaceParallaxComponent::SpawnNestedQuadRecursive(FName LayerTag, const FFaceNestedArt& Element,
    FVector2D ParentQuadSize, int32& DepthIndex)
{
    int32 Spawned = 0;

    AActor* Owner = GetOwner();
    if (!Owner || Element.ElementName.IsNone()) return 0;

    FName ElementTag = FName(*(LayerTag.ToString() + TEXT("_") + Element.ElementName.ToString()));

    bool bHasTagged = false;
    TArray<UPrimitiveComponent*> OwnerPrims;
    Owner->GetComponents<UPrimitiveComponent>(OwnerPrims);
    for (UPrimitiveComponent* Prim : OwnerPrims)
    {
        if (Prim && Prim->ComponentHasTag(ElementTag)) { bHasTagged = true; break; }
    }

    if (!bHasTagged)
    {
        UStaticMeshComponent* Quad = SpawnQuadForTag(ElementTag, ParentQuadSize, Element.RelativeTransform.Rotation);
        if (Quad)
        {
            // Stack nested quads slightly forward of the layer quad to avoid z-fighting.
            FVector Offset = LayerQuadLocalOffset;
            Offset.X += 2.0f + DepthIndex * 2.0f;
            Quad->SetRelativeLocation(Offset);
            ++Spawned;
        }
    }

    int32 ChildIndex = 0;
    for (const FFaceNestedArt& Child : Element.Children)
    {
        Spawned += SpawnNestedQuadRecursive(LayerTag, Child, ParentQuadSize, ChildIndex);
        ++ChildIndex;
    }

    return Spawned;
}

void UFaceParallaxComponent::RemoveSpawnedQuads()
{
    for (UStaticMeshComponent* Quad : SpawnedLayerQuads)
    {
        if (Quad)
        {
            Quad->DestroyComponent();
        }
    }
    SpawnedLayerQuads.Empty();
    FaceMaterialsByLayer.Empty();
    NestedMaterialsByElement.Empty();
    if (GetOwner())
    {
        InitializeMaterials();
    }
}

void UFaceParallaxComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!OwnerMesh) return;

    FVector CamLoc;
    FRotator CamRot;
    if (!GetCameraLocationAndRotation(CamLoc, CamRot))
    {
        CameraManager = nullptr;
        return;
    }

    float DeltaYaw, DeltaPitch;
    CalculateLookDelta(DeltaYaw, DeltaPitch);

    CurrentYaw = DeltaYaw;
    CurrentPitch = DeltaPitch;

    // Smoothed angular velocity for swoosh detection (EMA filter)
    float RawAngularVelocity = (FMath::Abs(DeltaYaw - PreviousFrameYaw) + FMath::Abs(DeltaPitch - PreviousFramePitch))
        / FMath::Max(DeltaTime, 0.0001f);
    float SmoothFactor = FMath::Clamp(SwooshSmoothingAlpha, 0.01f, 1.0f);
    SwooshSmoothedVelocity = FMath::Lerp(SwooshSmoothedVelocity, RawAngularVelocity, SmoothFactor);

    SaveFrameDelta(DeltaYaw, DeltaPitch);

    UpdateStateMachine(DeltaYaw, DeltaPitch, DeltaTime, SwooshSmoothedVelocity);

    // Blend preview override: force BlendAlpha when editor preview is active
    if (bBlendPreviewOverride)
    {
        BlendAlpha = BlendPreviewAlpha;
        bIsInTransition = true;
    }

    UpdateParallaxOffsets(DeltaTime);
    UpdateBlinkTick(DeltaTime);
    UpdateExpressionTick(DeltaTime);
    UpdateVisemeTick(DeltaTime);
    UpdateParametersTick(DeltaTime);
    UpdateSwooshTick(DeltaTime);

    if (bNestedArtEnabled)
    {
        UpdateNestedArtTick(DeltaTime);
        PushNestedArtParams();
    }

    UpdateMaterialParameters();
}

// ====================================================================
// PARAMETER SYSTEM
// ====================================================================

void UFaceParallaxComponent::DefineParameter(FName ParamName, float DefaultValue, float Min, float Max, float SmoothingSpeed)
{
    FFaceParamDef& Def = ParamDefinitions.FindOrAdd(ParamName);
    Def.DefaultValue = FMath::Clamp(DefaultValue, Min, Max);
    Def.CurrentValue = Def.DefaultValue;
    Def.TargetValue = Def.DefaultValue;
    Def.Min = Min;
    Def.Max = Max;
    Def.SmoothingSpeed = FMath::Max(0.1f, SmoothingSpeed);
}

void UFaceParallaxComponent::SetParameterValue(FName ParamName, float Value)
{
    FFaceParamDef* Def = ParamDefinitions.Find(ParamName);
    if (!Def) return;

    float Clamped = FMath::Clamp(Value, Def->Min, Def->Max);
    float OldValue = Def->TargetValue;
    Def->TargetValue = Clamped;

    if (!FMath::IsNearlyEqual(OldValue, Clamped))
    {
        OnParamValueChanged.Broadcast(ParamName, OldValue, Clamped);
    }
}

float UFaceParallaxComponent::GetParameterValue(FName ParamName) const
{
    const FFaceParamDef* Def = ParamDefinitions.Find(ParamName);
    return Def ? Def->CurrentValue : 0.0f;
}

TArray<FName> UFaceParallaxComponent::GetParameterNames() const
{
    TArray<FName> Result;
    ParamDefinitions.GetKeys(Result);
    return Result;
}

void UFaceParallaxComponent::ResetAllParameters()
{
    for (auto& Pair : ParamDefinitions)
    {
        FFaceParamDef& Def = Pair.Value;
        float OldValue = Def.TargetValue;
        Def.TargetValue = Def.DefaultValue;
        if (!FMath::IsNearlyEqual(OldValue, Def.DefaultValue))
        {
            OnParamValueChanged.Broadcast(Pair.Key, OldValue, Def.DefaultValue);
        }
    }
}

void UFaceParallaxComponent::SetParamSmoothingSpeed(FName ParamName, float Speed)
{
    FFaceParamDef* Def = ParamDefinitions.Find(ParamName);
    if (Def) Def->SmoothingSpeed = FMath::Max(0.1f, Speed);
}

float UFaceParallaxComponent::GetParamSmoothingSpeed(FName ParamName) const
{
    const FFaceParamDef* Def = ParamDefinitions.Find(ParamName);
    return Def ? Def->SmoothingSpeed : ParamSmoothingSpeed;
}

void UFaceParallaxComponent::UpdateParametersTick(float DeltaTime)
{
    if (!bParamsEnabled) return;

    for (auto& Pair : ParamDefinitions)
    {
        FFaceParamDef& Def = Pair.Value;
        if (!FMath::IsNearlyEqual(Def.CurrentValue, Def.TargetValue))
        {
            float Speed = Def.SmoothingSpeed;
            float InterpFactor = FMath::Clamp(Speed * DeltaTime, 0.0f, 1.0f);
            float OldCurrent = Def.CurrentValue;
            Def.CurrentValue = FMath::Lerp(Def.CurrentValue, Def.TargetValue, InterpFactor);

            if (!FMath::IsNearlyEqual(OldCurrent, Def.CurrentValue))
            {
                OnParamValueChanged.Broadcast(Pair.Key, OldCurrent, Def.CurrentValue);
            }
        }
    }
}

bool UFaceParallaxComponent::GetCameraLocationAndRotation(FVector& OutLoc, FRotator& OutRot) const
{
    AActor* Owner = GetOwner();
    if (!Owner) return false;

    UWorld* World = Owner->GetWorld();
    if (!World) return false;

    switch (CameraSource)
    {
        case ECameraSource::PlayerCamera0:
        case ECameraSource::PlayerCamera1:
        {
            int32 Idx = (CameraSource == ECameraSource::PlayerCamera1) ? 1 : 0;
            APlayerCameraManager* PCM = UGameplayStatics::GetPlayerCameraManager(Owner, Idx);
            if (PCM)
            {
                OutLoc = PCM->GetCameraLocation();
                OutRot = PCM->GetCameraRotation();
                return true;
            }
            break;
        }
        case ECameraSource::SpecifiedActor:
        {
            if (CustomCameraActor)
            {
                OutLoc = CustomCameraActor->GetActorLocation();
                OutRot = CustomCameraActor->GetActorRotation();
                return true;
            }
            break;
        }
        case ECameraSource::SequencerCamera:
        {
            if (!bSequencerCacheValid || !IsValid(SequencerCameraCache))
            {
                const_cast<UFaceParallaxComponent*>(this)->RefreshSequencerCamera();
            }
            if (SequencerCameraCache)
            {
                OutLoc = SequencerCameraCache->GetActorLocation();
                OutRot = SequencerCameraCache->GetActorRotation();
                return true;
            }
            break;
        }
        case ECameraSource::PreviewActor:
        {
            AActor* PreviewOwner = GetOwner();
            if (PreviewOwner)
            {
                AFaceParallaxPreviewActor* Preview = Cast<AFaceParallaxPreviewActor>(PreviewOwner);
                if (Preview && Preview->SceneCapture)
                {
                    OutLoc = Preview->SceneCapture->GetComponentLocation();
                    OutRot = Preview->SceneCapture->GetComponentRotation();
                    return true;
                }
            }
            break;
        }
        default:
            break;
    }

    // Last resort: fall back to player camera 0
    APlayerCameraManager* PCM = UGameplayStatics::GetPlayerCameraManager(Owner, 0);
    if (PCM)
    {
        OutLoc = PCM->GetCameraLocation();
        OutRot = PCM->GetCameraRotation();
        return true;
    }

    return false;
}

void UFaceParallaxComponent::RefreshSequencerCamera()
{
    SequencerCameraCache = nullptr;
    bSequencerCacheValid = true;
    AActor* Owner = GetOwner();
    if (!Owner) return;
    UWorld* World = Owner->GetWorld();
    if (!World) return;

    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(Owner, ACameraActor::StaticClass(), Found);
    for (AActor* A : Found)
    {
        SequencerCameraCache = A;
        return;
    }
    Found.Reset();
    UGameplayStatics::GetAllActorsOfClass(Owner, ACameraActor::StaticClass(), Found);
    for (AActor* A : Found)
    {
        SequencerCameraCache = A;
        return;
    }
}

void UFaceParallaxComponent::CalculateLookDelta(float& OutYaw, float& OutPitch)
{
    FVector HeadLoc = OwnerMesh->GetSocketLocation(HeadBoneName);
    FVector CamLoc;
    FRotator CamRot;
    if (!GetCameraLocationAndRotation(CamLoc, CamRot))
    {
        OutYaw = 0.0f;
        OutPitch = 0.0f;
        return;
    }
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

    float BM[4];
    for (int32 i = 0; i < 4; ++i)
        BM[i] = GetBoundaryOrDefault(ZoneBoundaryMultipliers, i) * HalfZoneWidth;

    if (Yaw > -BM[0] && Yaw <= BM[0]) return EFaceAngleState::Front;

    if (Yaw > BM[0] && Yaw <= BM[1])        return EFaceAngleState::ThreeQuarterRight;
    if (Yaw > BM[1] && Yaw <= BM[2])        return EFaceAngleState::RightProfile;
    if (Yaw > BM[2] && Yaw <= BM[3])        return EFaceAngleState::BackRight;
    if (Yaw > BM[3] || Yaw <= -BM[3])        return EFaceAngleState::Back;
    if (Yaw > -BM[3] && Yaw <= -BM[2])       return EFaceAngleState::BackLeft;
    if (Yaw > -BM[2] && Yaw <= -BM[1])       return EFaceAngleState::LeftProfile;
    if (Yaw > -BM[1] && Yaw <= -BM[0])       return EFaceAngleState::ThreeQuarterLeft;

    return EFaceAngleState::Front;
}

float UFaceParallaxComponent::GetZoneCenterYaw(EFaceAngleState State) const
{
    float BM[4];
    for (int32 i = 0; i < 4; ++i)
        BM[i] = GetBoundaryOrDefault(ZoneBoundaryMultipliers, i) * HalfZoneWidth;
    switch (State)
    {
        case EFaceAngleState::Front:              return 0.0f;
        case EFaceAngleState::ThreeQuarterRight:  return (BM[0] + BM[1]) * 0.5f;
        case EFaceAngleState::RightProfile:       return (BM[1] + BM[2]) * 0.5f;
        case EFaceAngleState::BackRight:          return (BM[2] + BM[3]) * 0.5f;
        case EFaceAngleState::Back:               return 180.0f;
        case EFaceAngleState::BackLeft:           return -(BM[2] + BM[3]) * 0.5f;
        case EFaceAngleState::LeftProfile:        return -(BM[1] + BM[2]) * 0.5f;
        case EFaceAngleState::ThreeQuarterLeft:   return -(BM[0] + BM[1]) * 0.5f;
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

void UFaceParallaxComponent::UpdateStateMachine(float Yaw, float Pitch, float DeltaTime, float AngularVelocity)
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

            if (bAutoApplyPreset)
            {
                CaptureCurrentTextures();
                ApplyCurrentStateTextures();
                SetPreviousStateTextures();
            }

            // Swoosh trigger: fast camera movement overrides smooth transition
            if (bSwooshEnabled && AngularVelocity >= SwooshSpeedThreshold)
            {
                BlendAlpha = 0.0f;
                bIsInTransition = true;
                SwooshPhase = ESwooshPhase::Smearing;
                SwooshFrameIndex = 0;
                SwooshFrameTimer = 0.0f;
                SwooshBlendOutElapsed = 0.0f;
                SwooshProceduralTick = 0;

                // Capture smear angle from camera movement direction
                // Uses saved frame delta (FrameDyaw/FrameDpitch) because
                // PreviousFrameYaw/Pitch are already updated to current values
                SwooshSmearAngle = FMath::RadiansToDegrees(FMath::Atan2(FrameDpitch, FrameDyaw));

                // Load art frames from preset if available (stored by target state)
                SwooshFrames.Reset();
                if (ActivePreset)
                {
                    for (const auto& LayerPair : FaceMaterialsByLayer)
                    {
                        const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerPair.Key);
                        const FFaceSwooshArt* Art = Slot.SwooshToState.Find(CurrentState);
                        if (Art && Art->Frames.Num() > 0)
                        {
                            SwooshFrames.Append(Art->Frames);
                        }
                    }
                }

                OnFaceStateChanged.Broadcast(CurrentState, PreviousState);
                OnSwooshStarted.Broadcast(CurrentState, PreviousState);
            }
            else
            {
                // Normal crossfade
                BlendAlpha = 0.0f;
                bIsInTransition = true;
                StopAnimationsOnStateChange();
                OnFaceStateChanged.Broadcast(CurrentState, PreviousState);
            }
        }
    }
    else
    {
        HysteresisFramesRemaining = 0;
    }

    // Normal transition blending (only used when swoosh is not active)
    if (bIsInTransition && SwooshPhase == ESwooshPhase::Inactive)
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
    else if (SwooshPhase != ESwooshPhase::Inactive)
    {
        // Swoosh manages its own blending; don't touch BlendAlpha here
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

    // MPC optimization: push shared scalar params once instead of per-MID
    UMaterialParameterCollectionInstance* MPCInstance = nullptr;
    if (bUseMPC && ParallaxMPC)
    {
        AActor* Owner = GetOwner();
        UWorld* World = Owner ? Owner->GetWorld() : nullptr;
        if (World) MPCInstance = World->GetParameterCollectionInstance(ParallaxMPC);
    }

    if (MPCInstance)
    {
        MPCInstance->SetScalarParameterValue(FName("StateBlendAlpha"), BlendAlpha);
        MPCInstance->SetScalarParameterValue(FName("DepthIntensity"), DepthMapIntensity);
        MPCInstance->SetScalarParameterValue(FName("DebugDepth"), bEnableMaterialDebugMode ? 1.0f : 0.0f);
        MPCInstance->SetScalarParameterValue(FName("IsTopDown"), bAnyTopOrBottom ? 1.0f : 0.0f);
        MPCInstance->SetScalarParameterValue(FName("IsTopView"), bIsTop ? 1.0f : 0.0f);
        // DepthMin/DepthMax pushed per-MID below (per-layer values don't apply to shared MPC)
    }

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

        // Blended parallax offset
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

            // When MPC is active, skip scalar params that are in the MPC
            if (!MPCInstance)
            {
                Mat->SetScalarParameterValue(FName("StateBlendAlpha"), BlendAlpha);
                Mat->SetScalarParameterValue(FName("DepthIntensity"), LayerDepthIntensity);
                Mat->SetScalarParameterValue(FName("DebugDepth"), bEnableMaterialDebugMode ? 1.0f : 0.0f);
                Mat->SetScalarParameterValue(FName("IsTopDown"), bAnyTopOrBottom ? 1.0f : 0.0f);
                Mat->SetScalarParameterValue(FName("IsTopView"), bIsTop ? 1.0f : 0.0f);
            }

            Mat->SetVectorParameterValue(FName("ParallaxOffset"),
                FLinearColor(BlendedOffset.X, BlendedOffset.Y, 0.0f, 0.0f));

            // Per-layer depth range bounds for material-side depth map interpretation
            float LayerDepthMin = 0.0f;
            float LayerDepthMax = 1.0f;
            if (GlobalLayerIdx < LayerDefinitions.Num())
            {
                LayerDepthMin = LayerDefinitions[GlobalLayerIdx].DepthMin;
                LayerDepthMax = LayerDefinitions[GlobalLayerIdx].DepthMax;
            }
            Mat->SetScalarParameterValue(DepthMinParamName, LayerDepthMin);
            Mat->SetScalarParameterValue(DepthMaxParamName, LayerDepthMax);

            // Previous state textures for crossfade blending
            if (PrevTexSet && PrevTexSet->IsValid())
            {
                if (PrevTexSet->Albedo) Mat->SetTextureParameterValue(AlbedoPrevParamName, PrevTexSet->Albedo);
                if (PrevTexSet->Normal) Mat->SetTextureParameterValue(NormalPrevParamName, PrevTexSet->Normal);
                if (PrevTexSet->Depth)  Mat->SetTextureParameterValue(DepthPrevParamName, PrevTexSet->Depth);
            }

            // Swoosh transition — overrides view textures during fast camera moves
            if (SwooshPhase != ESwooshPhase::Inactive)
            {
                float SwooshBlend = (SwooshPhase == ESwooshPhase::BlendingOut) ? BlendAlpha : 1.0f;
                Mat->SetScalarParameterValue(SwooshLayerBlendParamName, SwooshBlend);

                if (SwooshPhase == ESwooshPhase::Smearing && SwooshFrames.Num() > 0)
                {
                    int32 SafeIdx = FMath::Clamp(SwooshFrameIndex, 0, SwooshFrames.Num() - 1);
                    const FFaceTextureSet& Frame = SwooshFrames[SafeIdx];
                    if (Frame.Albedo) Mat->SetTextureParameterValue(AlbedoParamName, Frame.Albedo);
                    if (Frame.Normal) Mat->SetTextureParameterValue(NormalParamName, Frame.Normal);
                    if (Frame.Depth)  Mat->SetTextureParameterValue(DepthParamName, Frame.Depth);
                    if (Frame.Albedo) Mat->SetTextureParameterValue(SwooshTextureParamName, Frame.Albedo);
                    Mat->SetScalarParameterValue(SwooshIntensityParamName, 0.0f);
                }
                else
                {
                    // Procedural smear: drive material parameters
                    float TotalProcedural = FMath::Max(3.0f, 4.0f + SwooshBusyness * 8.0f);
                    float Progress = (TotalProcedural > 0.0f)
                        ? FMath::Min(1.0f, (float)SwooshProceduralTick / TotalProcedural)
                        : 0.0f;
                    float Intensity = FMath::Sin(Progress * PI * (1.0f + SwooshBusyness * 3.0f));
                    Intensity = FMath::Abs(Intensity) * SwooshSize;
                    Mat->SetScalarParameterValue(SwooshIntensityParamName, Intensity);
                    Mat->SetScalarParameterValue(SwooshAngleParamName, SwooshSmearAngle);
                    Mat->SetScalarParameterValue(SwooshSizeParamName, SwooshSize);
                }
            }
            else
            {
                Mat->SetScalarParameterValue(SwooshLayerBlendParamName, 0.0f);
                Mat->SetScalarParameterValue(SwooshIntensityParamName, 0.0f);
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
                const FFaceVisemeFrameArray* VisemeFrames = ResolveVisemeFrames(VisemeSlot);
                if (VisemeFrames && VisemeFrameIndex >= 0 && VisemeFrameIndex < VisemeFrames->Frames.Num())
                {
                    const FFaceTextureSet& VisemeFrame = VisemeFrames->Frames[VisemeFrameIndex];
                    if (VisemeFrame.Albedo) Mat->SetTextureParameterValue(AlbedoParamName, VisemeFrame.Albedo);
                    if (VisemeFrame.Normal) Mat->SetTextureParameterValue(NormalParamName, VisemeFrame.Normal);
                    if (VisemeFrame.Depth)  Mat->SetTextureParameterValue(DepthParamName, VisemeFrame.Depth);
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
                FFaceArtTransform ParamDrivenTransform = EffectiveTransform;
                float TextureBlendValue = 0.0f;

                // Apply parameter bindings from the slot
                const FFaceArtSlot& ParamSlot = ActivePreset ? ActivePreset->GetSlot(CurrentState, LayerTag) : FFaceArtSlot();
                if (bParamsEnabled && ActivePreset)
                {
                    for (const FFaceParamBinding& Binding : ParamSlot.ParamBindings)
                    {
                        float ParamVal = GetParameterValue(Binding.ParamName);
                        float EffectiveVal = Binding.bInvert ? (1.0f - ParamVal) : ParamVal;
                        float Modifier = EffectiveVal * Binding.Scale + Binding.Offset;

                        switch (Binding.Target)
                        {
                        case EFaceParamTarget::PositionX:
                            ParamDrivenTransform.Position.X += Modifier;
                            break;
                        case EFaceParamTarget::PositionY:
                            ParamDrivenTransform.Position.Y += Modifier;
                            break;
                        case EFaceParamTarget::ScaleX:
                            ParamDrivenTransform.Scale.X *= FMath::Max(0.01f, 1.0f + Modifier);
                            break;
                        case EFaceParamTarget::ScaleY:
                            ParamDrivenTransform.Scale.Y *= FMath::Max(0.01f, 1.0f + Modifier);
                            break;
                        case EFaceParamTarget::Rotation:
                            ParamDrivenTransform.Rotation += Modifier;
                            break;
                        case EFaceParamTarget::TextureBlend:
                            TextureBlendValue = FMath::Max(TextureBlendValue, FMath::Clamp(EffectiveVal * Binding.Scale + Binding.Offset, 0.0f, 1.0f));
                            break;
                        }
                    }
                }

                FVector2D FinalArtPos = ParamDrivenTransform.Position;

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

            // Phase 5: primary-layer 3D pin (LayerPin3D) — the whole layer's
            // art stays attached to a projected 3D point, with optional
            // view-angle rotation/scale exactly like a nested pin.
            if (ActivePreset && ParamSlot.LayerPin3D.bPinned)
            {
                const FVector2D PinnedUV = ProjectPinToUVInternal(ParamSlot.LayerPin3D.Position3D);
                const FVector2D Canvas = ActivePreset->CanvasSize;
                FinalArtPos += FVector2D((PinnedUV.X - 0.5f) * Canvas.X,
                                         (PinnedUV.Y - 0.5f) * Canvas.Y);
                if (ParamSlot.LayerPin3D.bEnableViewAngleRotation)
                {
                    // YawCenter is already in scope (dynamic art offset above).
                    const float PitchCenter = GetZoneCenterPitch(CurrentState);
                    ParamDrivenTransform.Rotation += PinRotationFromViewAngles(
                        CurrentYaw - YawCenter, CurrentPitch - PitchCenter, HalfZoneWidth,
                        ParamSlot.LayerPin3D.MinRotation, ParamSlot.LayerPin3D.MaxRotation,
                        ParamSlot.LayerPin3D.RotationSensitivity);
                    ParamDrivenTransform.Scale *= PinScaleFromView(
                        CurrentYaw - YawCenter, CurrentPitch - PitchCenter,
                        ParamSlot.LayerPin3D.MinScale);
                }
            }

                Mat->SetVectorParameterValue(ArtPositionParamName,
                    FLinearColor(FinalArtPos.X, FinalArtPos.Y, 0.0f, 0.0f));
                Mat->SetVectorParameterValue(ArtScaleParamName,
                    FLinearColor(ParamDrivenTransform.Scale.X, ParamDrivenTransform.Scale.Y, 0.0f, 0.0f));
                Mat->SetScalarParameterValue(ArtRotationParamName, ParamDrivenTransform.Rotation);

                // TextureBlend — push alt textures + blend alpha
                if (TextureBlendValue > 0.0f && ActivePreset)
                {
                    const FFaceTextureSet& AltTex = ParamSlot.AltTextures;
                    if (AltTex.Albedo) Mat->SetTextureParameterValue(ParamAltAlbedoParamName, AltTex.Albedo);
                    if (AltTex.Normal) Mat->SetTextureParameterValue(ParamAltNormalParamName, AltTex.Normal);
                    if (AltTex.Depth)  Mat->SetTextureParameterValue(ParamAltDepthParamName, AltTex.Depth);
                    Mat->SetScalarParameterValue(ParamBlendParamName, TextureBlendValue);
                }
                else
                {
                    Mat->SetScalarParameterValue(ParamBlendParamName, 0.0f);
                }
            }
        }
        ++GlobalLayerIdx;
    }
}

void UFaceParallaxComponent::ApplyPreset(UFaceParallaxPreset* Preset)
{
    ActivePreset = Preset;
    bNestedArtCacheDirty = true;
    // Invalidate texture cache so ApplyCurrentStateTextures re-pushes everything
    LastAppliedTextures.Empty();
    LastAppliedNestedTextures.Empty();
    if (ActivePreset)
    {
        SyncLayerDefinitionsFromPreset();
        DetectFaceProfileFromPreset();
        ApplyCurrentStateTextures();
    }
}

void UFaceParallaxComponent::SyncLayerDefinitionsFromPreset()
{
    if (!ActivePreset) return;

    // Merge preset layer tags into LayerDefinitions so spawned quads (and material
    // discovery) cover every layer the preset actually has art for.
    TArray<FName> Tags = ActivePreset->GetAllLayerTags(EFaceAngleState::Front);
    bool bChanged = false;
    for (const FName& Tag : Tags)
    {
        if (Tag.IsNone()) continue;
        FFaceLayerDef* Existing = LayerDefinitions.FindByPredicate(
            [&Tag](const FFaceLayerDef& Def) { return Def.LayerTag == Tag; });
        if (!Existing)
        {
            FFaceLayerDef NewDef;
            NewDef.LayerTag = Tag;
            NewDef.DepthScale = 1.0f;
            NewDef.DepthMapIntensity = 1.0f;
            NewDef.bInvertParallax = false;
            LayerDefinitions.Add(NewDef);
            bChanged = true;
        }
    }

    // The constructor seeds a bare placeholder layer ("FaceLayer") so the
    // component works before a preset exists. Now that the preset is the
    // source of truth for layers, drop the placeholder if it has no slot in
    // the preset and still has default values (a user layer that happens to
    // be named "FaceLayer" has non-default values or preset slots and survives).
    auto PresetHasLayerAnyState = [this](const FName& Tag)
    {
        for (int32 S = 0; S <= (int32)EFaceAngleState::Bottom; ++S)
        {
            if (ActivePreset->GetAllLayerTags((EFaceAngleState)S).Contains(Tag))
            {
                return true;
            }
        }
        return false;
    };
    for (int32 i = LayerDefinitions.Num() - 1; i >= 0; --i)
    {
        const FFaceLayerDef& Def = LayerDefinitions[i];
        if (!IsSeedPlaceholderLayerDef(Def)) continue;
        if (PresetHasLayerAnyState(Def.LayerTag)) continue;
        if (LayerDefinitions.Num() <= 1) break;
        LayerDefinitions.RemoveAt(i);
        bChanged = true;
    }

    if (bChanged)
    {
        LayerParallaxOffsets.SetNum(LayerDefinitions.Num());
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

void UFaceParallaxComponent::ForceSwoosh(EFaceAngleState TargetState)
{
    if (!ActivePreset || SwooshPhase != ESwooshPhase::Inactive) return;

    PreviousState = CurrentState;
    CurrentState = TargetState;

    if (bAutoApplyPreset)
    {
        CaptureCurrentTextures();
        ApplyCurrentStateTextures();
        SetPreviousStateTextures();
    }

    BlendAlpha = 0.0f;
    bIsInTransition = true;
    SwooshPhase = ESwooshPhase::Smearing;
    SwooshFrameIndex = 0;
    SwooshFrameTimer = 0.0f;
    SwooshBlendOutElapsed = 0.0f;
    SwooshProceduralTick = 0;

    // Use last-known frame delta for smear angle; may be stale if called outside TickComponent
    SwooshSmearAngle = FMath::RadiansToDegrees(FMath::Atan2(FrameDpitch, FrameDyaw));

    SwooshFrames.Reset();
    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(TargetState, LayerPair.Key);
        const FFaceSwooshArt* Art = Slot.SwooshToState.Find(TargetState);
        if (Art && Art->Frames.Num() > 0)
        {
            SwooshFrames.Append(Art->Frames);
        }
    }

    OnFaceStateChanged.Broadcast(CurrentState, PreviousState);
    OnSwooshStarted.Broadcast(CurrentState, PreviousState);
}

void UFaceParallaxComponent::SetStateTextures(EFaceAngleState State)
{
    if (!ActivePreset) return;

    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);

        // Use expression texture variant if available for current expression
        const FFaceTextureSet* ExprTex = ResolveExpressionTextureSet(Slot);
        const FFaceTextureSet& TexSet = ExprTex ? *ExprTex : Slot.Textures;
        if (!TexSet.IsValid()) continue;

        for (UMaterialInstanceDynamic* Mat : LayerPair.Value)
        {
            FFaceAppliedTextures& Cached = LastAppliedTextures.FindOrAdd(LayerTag);
            if (Cached.Albedo != TexSet.Albedo)
            {
                if (TexSet.Albedo) Mat->SetTextureParameterValue(AlbedoParamName, TexSet.Albedo);
                Cached.Albedo = TexSet.Albedo;
            }
            if (Cached.Normal != TexSet.Normal)
            {
                if (TexSet.Normal) Mat->SetTextureParameterValue(NormalParamName, TexSet.Normal);
                Cached.Normal = TexSet.Normal;
            }
            if (Cached.Depth != TexSet.Depth)
            {
                if (TexSet.Depth) Mat->SetTextureParameterValue(DepthParamName, TexSet.Depth);
                Cached.Depth = TexSet.Depth;
            }
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
    CurrentNamedViseme = NAME_None;

    OnVisemeCompleted.Broadcast(CurrentState, PreviousState);
}

void UFaceParallaxComponent::PlayVisemeByName(FName NewVisemeName)
{
    if (!ActivePreset || !bVisemeEnabled) return;

    CurrentNamedViseme = NewVisemeName;
    VisemeFrameIndex = 0;
    VisemeFrameTimer = 0.0f;
    bIsVisemePlaying = true;

    OnVisemeStarted.Broadcast(CurrentState, PreviousState);
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
        const FFaceVisemeFrameArray* VisemeFrames = ResolveVisemeFrames(Slot);
        if (VisemeFrames)
        {
            MaxFrames = FMath::Max(MaxFrames, VisemeFrames->Frames.Num());
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

void UFaceParallaxComponent::UpdateSwooshTick(float DeltaTime)
{
    if (SwooshPhase == ESwooshPhase::Inactive) return;

    if (SwooshPhase == ESwooshPhase::Smearing)
    {
        bool bHasArt = (SwooshFrames.Num() > 0);
        if (bHasArt)
        {
            SwooshFrameTimer += DeltaTime;
            if (SwooshFrameTimer >= SwooshFrameDuration)
            {
                SwooshFrameTimer = 0.0f;
                SwooshFrameIndex++;
                if (SwooshFrameIndex >= SwooshFrames.Num())
                {
                    SwooshPhase = ESwooshPhase::BlendingOut;
                    SwooshBlendOutElapsed = 0.0f;
                }
            }
        }
        else
        {
            // Procedural smear: run through fake frames based on Busyness
            SwooshProceduralTick++;
            int32 ProceduralFrameCount = FMath::Max(3, FMath::RoundToInt(4.0f + SwooshBusyness * 8.0f));
            if (SwooshProceduralTick >= ProceduralFrameCount)
            {
                SwooshPhase = ESwooshPhase::BlendingOut;
                SwooshBlendOutElapsed = 0.0f;
            }
        }
    }

    if (SwooshPhase == ESwooshPhase::BlendingOut)
    {
        SwooshBlendOutElapsed += DeltaTime;
        BlendAlpha = FMath::Min(1.0f, SwooshBlendOutElapsed / FMath::Max(0.001f, SwooshBlendOutDuration));
        if (BlendAlpha >= 1.0f)
        {
            BlendAlpha = 1.0f;
            SwooshPhase = ESwooshPhase::Inactive;
            bIsInTransition = false;
            SwooshFrames.Reset();
            OnSwooshCompleted.Broadcast(CurrentState, PreviousState);
        }
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
        ExpressionPreviousTextureSets.Reset();
    }

    // Reset jiggle + idle animation states on state change
    JiggleStates.Empty();
    AnimStates.Empty();
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
    ExpressionPreviousTextureSets.Reset();
    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerTag);
        const FFaceTextureSet* ExprTex = ResolveExpressionTextureSet(Slot);
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
    CurrentNamedExpression = NAME_None;
    ExpressionBlendAlpha = 0.0f;
    bExpressionTransitioning = true;

    // Push new expression textures
    ApplyExpressionTextures(CurrentState);

    OnExpressionChanged.Broadcast(CurrentState, PreviousState);
}

void UFaceParallaxComponent::SetExpressionByName(FName NewExpressionName)
{
    if (NewExpressionName == CurrentNamedExpression && !bExpressionTransitioning) return;

    if (!ActivePreset) return;

    ExpressionPreviousTextureSets.Reset();
    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerTag);
        const FFaceTextureSet* ExprTex = ResolveExpressionTextureSet(Slot);
        if (ExprTex && ExprTex->IsValid())
        {
            ExpressionPreviousTextureSets.Add(LayerTag, *ExprTex);
        }
        else if (Slot.Textures.IsValid())
        {
            ExpressionPreviousTextureSets.Add(LayerTag, Slot.Textures);
        }
    }

    CurrentNamedExpression = NewExpressionName;
    ExpressionBlendAlpha = 0.0f;
    bExpressionTransitioning = true;

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
        ExpressionPreviousTextureSets.Reset();
    }
}

void UFaceParallaxComponent::ApplyExpressionTextures(EFaceAngleState State)
{
    if (!ActivePreset) return;

    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);

        const FFaceTextureSet* ExprTex = ResolveExpressionTextureSet(Slot);
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

const FFaceTextureSet* UFaceParallaxComponent::ResolveExpressionTextureSet(const FFaceArtSlot& Slot) const
{
    if (CurrentNamedExpression != NAME_None)
    {
        const FFaceTextureSet* Found = Slot.NamedExpressionTextures.Find(CurrentNamedExpression);
        if (Found) return Found;
    }
    return Slot.ExpressionTextures.Find(CurrentExpression);
}

const FFaceVisemeFrameArray* UFaceParallaxComponent::ResolveVisemeFrames(const FFaceArtSlot& Slot) const
{
    if (CurrentNamedViseme != NAME_None)
    {
        return Slot.NamedVisemeFrames.Find(CurrentNamedViseme);
    }
    const FFaceExpressionVisemeMap* ExprVisemes = Slot.VisemeFrameSets.Find(CurrentExpression);
    if (ExprVisemes)
    {
        return ExprVisemes->Visemes.Find(CurrentViseme);
    }
    return nullptr;
}

// ====================================================================
// NESTED ART + JIGGLE
// ====================================================================

void UFaceParallaxComponent::UpdateNestedArtTick(float DeltaTime)
{
    if (!ActivePreset) return;

    if (bNestedArtCacheDirty)
        BuildNestedArtCache();

    // Cap delta-time to prevent numerical blowup on hitches
    DeltaTime = FMath::Min(DeltaTime, 0.05f);

    // Jiggle simulation: spring-damper driven by camera angular velocity
    // Use saved frame delta (FrameDyaw/FrameDpitch capture pre-overwrite values)
    float AngularVel = FMath::Sqrt(FrameDyaw * FrameDyaw + FrameDpitch * FrameDpitch) / FMath::Max(DeltaTime, 0.0001f);

    FVector2D Impulse(FrameDyaw, FrameDpitch);
    Impulse *= AngularVel * 0.001f; // Normalize to reasonable scale

    // Fixed-timestep sub-stepping for jiggle physics
    float SubStepDt = 1.0f / FMath::Max(JiggleSubStepsPerSecond, 15);
    int32 MaxSubSteps = FMath::Min((int32)(DeltaTime / SubStepDt) + 1, 10); // cap to prevent spiral of death

    // Process all nested elements from all layers (including children recursively)
    std::function<void(const TArray<FFaceNestedArt>&, FName)> ProcessElements =
        [&](const TArray<FFaceNestedArt>& Elements, FName LayerTag)
    {
        auto BuildElemKey = [&](const FFaceNestedArt& Elem) -> FName
        {
            FName OutKey;
            if (NestedArtStateKeyCache.Contains(LayerTag) && NestedArtStateKeyCache[LayerTag].Contains(Elem.ElementName))
                OutKey = NestedArtStateKeyCache[LayerTag][Elem.ElementName];
            else
                OutKey = FName(*(LayerTag.ToString() + TEXT("_") + Elem.ElementName.ToString()));
            return OutKey;
        };

        for (const FFaceNestedArt& Element : Elements)
        {
            // Jiggle physics
            if (Element.bJiggleEnabled)
            {
                FName StateKey = BuildElemKey(Element);
                FNestedJiggleState& JiggleState = JiggleStates.FindOrAdd(StateKey);
                float& Accumulator = JiggleAccumulators.FindOrAdd(StateKey);
                Accumulator += DeltaTime;

                JiggleState.Velocity += Impulse * Element.JiggleSettings.JiggleAxis * Element.JiggleSettings.ImpulseScale;

                const float Stiffness = Element.JiggleSettings.Stiffness;
                const float Damping = Element.JiggleSettings.Damping;
                int32 Steps = 0;
                while (Accumulator >= SubStepDt && Steps < MaxSubSteps)
                {
                    Accumulator -= SubStepDt;
                    FVector2D SpringForce = -JiggleState.Position * Stiffness;
                    FVector2D DampingForce = -JiggleState.Velocity * Damping;
                    FVector2D Acceleration = SpringForce + DampingForce;

                    JiggleState.Velocity += Acceleration * SubStepDt;
                    JiggleState.Position += JiggleState.Velocity * SubStepDt;
                    ++Steps;
                }

                if (Steps >= MaxSubSteps)
                    Accumulator = 0.0f;
            }

            // Idle animation
            if (Element.IdleFrames.Num() > 0)
            {
                FName StateKey = BuildElemKey(Element);
                FNestedAnimState& Anim = AnimStates.FindOrAdd(StateKey);

                float EffectiveDuration = Element.IdleFrameDuration / FMath::Max(0.001f, Element.IdleSpeedMultiplier);
                Anim.FrameTimer += DeltaTime;

                while (Anim.FrameTimer >= EffectiveDuration && Element.IdleFrames.Num() > 0)
                {
                    Anim.FrameTimer -= EffectiveDuration;
                    Anim.FrameIndex = (Anim.FrameIndex + 1) % Element.IdleFrames.Num();
                }
            }

            // Recurse into children
            if (Element.Children.Num() > 0)
            {
                ProcessElements(Element.Children, LayerTag);
            }
        }
    };

    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerTag);
        ProcessElements(Slot.NestedElements, LayerTag);
    }

}

void UFaceParallaxComponent::PushNestedArtParams()
{
    if (!ActivePreset || NestedMaterialsByElement.Num() == 0) return;

    for (const auto& LayerPair : FaceMaterialsByLayer)
    {
        FName LayerTag = LayerPair.Key;
        const FFaceArtSlot& Slot = ActivePreset->GetSlot(CurrentState, LayerTag);

        // Compute parent effective transform (canonical + param bindings + parallax)
        FFaceArtTransform ParentTransform = Slot.GetEffectiveTransform(CurrentState);

        // Apply param bindings from the slot to parent transform
        if (bParamsEnabled)
        {
            for (const FFaceParamBinding& Binding : Slot.ParamBindings)
            {
                float ParamVal = GetParameterValue(Binding.ParamName);
                float EffectiveVal = Binding.bInvert ? (1.0f - ParamVal) : ParamVal;
                float Modifier = EffectiveVal * Binding.Scale + Binding.Offset;

                switch (Binding.Target)
                {
                case EFaceParamTarget::PositionX: ParentTransform.Position.X += Modifier; break;
                case EFaceParamTarget::PositionY: ParentTransform.Position.Y += Modifier; break;
                case EFaceParamTarget::ScaleX:    ParentTransform.Scale.X *= FMath::Max(0.01f, 1.0f + Modifier); break;
                case EFaceParamTarget::ScaleY:    ParentTransform.Scale.Y *= FMath::Max(0.01f, 1.0f + Modifier); break;
                case EFaceParamTarget::Rotation:  ParentTransform.Rotation += Modifier; break;
                default: break;
                }
            }
        }

        for (const FFaceNestedArt& Element : Slot.NestedElements)
        {
            const TMap<FName, FName>* LayerCache = NestedArtStateKeyCache.Find(LayerTag);
            if (!LayerCache) continue;
            const FName* StateKeyPtr = LayerCache->Find(Element.ElementName);
            if (!StateKeyPtr) continue;
            FName StateKey = *StateKeyPtr;
            TArray<UMaterialInstanceDynamic*>* ElemMats = NestedMaterialsByElement.Find(StateKey);
            if (!ElemMats || ElemMats->Num() == 0) continue;

            // Check per-view visibility
            const bool* bVisibleOverride = Element.ViewVisibility.Find(CurrentState);
            if (bVisibleOverride && !(*bVisibleOverride)) continue;

            // Get jiggle offset
            FVector2D JiggleOffset = FVector2D::ZeroVector;
            if (Element.bJiggleEnabled)
            {
                const FNestedJiggleState* JiggleState = JiggleStates.Find(StateKey);
                if (JiggleState)
                {
                    JiggleOffset = JiggleState->Position;
                }
            }

            // Compute effective transform for this nested element
            FFaceArtTransform NestedTransform = ComputeNestedEffectiveTransform(Element, ParentTransform, JiggleOffset);

            // Determine which texture set to use (idle animation or static)
            FFaceTextureSet FinalTex = Element.Textures;
            if (Element.IdleFrames.Num() > 0)
            {
                const FNestedAnimState* Anim = AnimStates.Find(StateKey);
                if (Anim && Anim->FrameIndex >= 0 && Anim->FrameIndex < Element.IdleFrames.Num())
                {
                    FinalTex = Element.IdleFrames[Anim->FrameIndex];
                }
            }

            // Apply param bindings on nested element itself
            float NestedTextureBlend = 0.0f;
            if (bParamsEnabled)
            {
                for (const FFaceParamBinding& Binding : Element.ParamBindings)
                {
                    float ParamVal = GetParameterValue(Binding.ParamName);
                    float EffectiveVal = Binding.bInvert ? (1.0f - ParamVal) : ParamVal;
                    float Modifier = EffectiveVal * Binding.Scale + Binding.Offset;

                    switch (Binding.Target)
                    {
                    case EFaceParamTarget::PositionX: NestedTransform.Position.X += Modifier; break;
                    case EFaceParamTarget::PositionY: NestedTransform.Position.Y += Modifier; break;
                    case EFaceParamTarget::ScaleX:    NestedTransform.Scale.X *= FMath::Max(0.01f, 1.0f + Modifier); break;
                    case EFaceParamTarget::ScaleY:    NestedTransform.Scale.Y *= FMath::Max(0.01f, 1.0f + Modifier); break;
                    case EFaceParamTarget::Rotation:  NestedTransform.Rotation += Modifier; break;
                    case EFaceParamTarget::TextureBlend: NestedTextureBlend = FMath::Max(NestedTextureBlend, FMath::Clamp(EffectiveVal * Binding.Scale + Binding.Offset, 0.0f, 1.0f)); break;
                    }
                }
            }

            // Push to all materials on this nested primitive
            for (UMaterialInstanceDynamic* Mat : *ElemMats)
            {
                if (!Mat) continue;

                // Textures
                if (FinalTex.Albedo) Mat->SetTextureParameterValue(AlbedoParamName, FinalTex.Albedo);
                if (FinalTex.Normal) Mat->SetTextureParameterValue(NormalParamName, FinalTex.Normal);
                if (FinalTex.Depth)  Mat->SetTextureParameterValue(DepthParamName, FinalTex.Depth);

                // Transform
                Mat->SetVectorParameterValue(ArtPositionParamName,
                    FLinearColor(NestedTransform.Position.X, NestedTransform.Position.Y, 0.0f, 0.0f));
                Mat->SetVectorParameterValue(ArtScaleParamName,
                    FLinearColor(NestedTransform.Scale.X, NestedTransform.Scale.Y, 0.0f, 0.0f));
                Mat->SetScalarParameterValue(ArtRotationParamName, NestedTransform.Rotation);

                // Pivot point
                FVector2D EffectivePivot = GetEffectivePivot(Element);
                Mat->SetVectorParameterValue(ArtPivotParamName,
                    FLinearColor(EffectivePivot.X, EffectivePivot.Y, 0.0f, 0.0f));

                // TextureBlend — push alt textures + blend alpha
                if (NestedTextureBlend > 0.0f)
                {
                    const FFaceTextureSet& AltTex = Element.AltTextures;
                    if (AltTex.Albedo) Mat->SetTextureParameterValue(ParamAltAlbedoParamName, AltTex.Albedo);
                    if (AltTex.Normal) Mat->SetTextureParameterValue(ParamAltNormalParamName, AltTex.Normal);
                    if (AltTex.Depth)  Mat->SetTextureParameterValue(ParamAltDepthParamName, AltTex.Depth);
                    Mat->SetScalarParameterValue(ParamBlendParamName, NestedTextureBlend);
                }
                else
                {
                    Mat->SetScalarParameterValue(ParamBlendParamName, 0.0f);
                }

                // Idle animation frame (for shader-based flipbook, optional)
                if (Element.IdleFrames.Num() > 0)
                {
                    const FNestedAnimState* Anim = AnimStates.Find(StateKey);
                    float FrameF = Anim ? (float)Anim->FrameIndex : 0.0f;
                    Mat->SetScalarParameterValue(NestedAnimParamName, FrameF);
                }
            }

            // Push state blend on nested materials for crossfade, swoosh, etc.
            // Use same material params as parent layers
            for (UMaterialInstanceDynamic* Mat : *ElemMats)
            {
                if (!Mat) continue;
                Mat->SetScalarParameterValue(FName("StateBlendAlpha"), BlendAlpha);
                Mat->SetScalarParameterValue(FName("IsTopDown"), (CurrentState == EFaceAngleState::Top || CurrentState == EFaceAngleState::Bottom) ? 1.0f : 0.0f);
                Mat->SetScalarParameterValue(FName("IsTopView"), (CurrentState == EFaceAngleState::Top) ? 1.0f : 0.0f);
            }

            // Recurse into static children (non-jiggle)
            if (!Element.bJiggleEnabled && Element.Children.Num() > 0)
            {
                for (const FFaceNestedArt& Child : Element.Children)
                {
                    PushNestedChildArt(Child, NestedTransform, LayerTag, Element.ElementName);
                }
            }
        }
    }
}

void UFaceParallaxComponent::PushNestedChildArt(const FFaceNestedArt& Element, const FFaceArtTransform& ParentTransform, FName LayerTag, FName ParentElementName)
{
    const TMap<FName, TMap<FName, FName>>* LayerCache = NestedArtChildStateKeyCache.Find(LayerTag);
    if (!LayerCache) return;
    const TMap<FName, FName>* ParentCache = LayerCache->Find(ParentElementName);
    if (!ParentCache) return;
    const FName* StateKeyPtr = ParentCache->Find(Element.ElementName);
    if (!StateKeyPtr) return;
    FName StateKey = *StateKeyPtr;
    TArray<UMaterialInstanceDynamic*>* ElemMats = NestedMaterialsByElement.Find(StateKey);
    if (!ElemMats || ElemMats->Num() == 0) return;

    const bool* bVisibleOverride = Element.ViewVisibility.Find(CurrentState);
    if (bVisibleOverride && !(*bVisibleOverride)) return;

    FVector2D JiggleOffset = FVector2D::ZeroVector;
    if (Element.bJiggleEnabled)
    {
        const FNestedJiggleState* JiggleState = JiggleStates.Find(StateKey);
        if (JiggleState) JiggleOffset = JiggleState->Position;
    }

    FFaceArtTransform NestedTransform = ComputeNestedEffectiveTransform(Element, ParentTransform, JiggleOffset);

    // Determine texture set
    FFaceTextureSet FinalTex = Element.Textures;
    if (Element.IdleFrames.Num() > 0)
    {
        const FNestedAnimState* Anim = AnimStates.Find(StateKey);
        if (Anim && Anim->FrameIndex >= 0 && Anim->FrameIndex < Element.IdleFrames.Num())
        {
            FinalTex = Element.IdleFrames[Anim->FrameIndex];
        }
    }

    // Apply param bindings on this child element
    float ChildTextureBlend = 0.0f;
    if (bParamsEnabled)
    {
        for (const FFaceParamBinding& Binding : Element.ParamBindings)
        {
            float ParamVal = GetParameterValue(Binding.ParamName);
            float EffectiveVal = Binding.bInvert ? (1.0f - ParamVal) : ParamVal;
            float Modifier = EffectiveVal * Binding.Scale + Binding.Offset;

            switch (Binding.Target)
            {
            case EFaceParamTarget::PositionX: NestedTransform.Position.X += Modifier; break;
            case EFaceParamTarget::PositionY: NestedTransform.Position.Y += Modifier; break;
            case EFaceParamTarget::ScaleX:    NestedTransform.Scale.X *= FMath::Max(0.01f, 1.0f + Modifier); break;
            case EFaceParamTarget::ScaleY:    NestedTransform.Scale.Y *= FMath::Max(0.01f, 1.0f + Modifier); break;
            case EFaceParamTarget::Rotation:  NestedTransform.Rotation += Modifier; break;
            case EFaceParamTarget::TextureBlend: ChildTextureBlend = FMath::Max(ChildTextureBlend, FMath::Clamp(EffectiveVal * Binding.Scale + Binding.Offset, 0.0f, 1.0f)); break;
            }
        }
    }

    // Compute effective pivot for this child element
    FVector2D ChildEffectivePivot = GetEffectivePivot(Element);

    for (UMaterialInstanceDynamic* Mat : *ElemMats)
    {
        if (!Mat) continue;
        FFaceAppliedTextures& NestedCached = LastAppliedNestedTextures.FindOrAdd(StateKey);
        if (NestedCached.Albedo != FinalTex.Albedo)
        {
            if (FinalTex.Albedo) Mat->SetTextureParameterValue(AlbedoParamName, FinalTex.Albedo);
            NestedCached.Albedo = FinalTex.Albedo;
        }
        if (NestedCached.Normal != FinalTex.Normal)
        {
            if (FinalTex.Normal) Mat->SetTextureParameterValue(NormalParamName, FinalTex.Normal);
            NestedCached.Normal = FinalTex.Normal;
        }
        if (NestedCached.Depth != FinalTex.Depth)
        {
            if (FinalTex.Depth) Mat->SetTextureParameterValue(DepthParamName, FinalTex.Depth);
            NestedCached.Depth = FinalTex.Depth;
        }
        Mat->SetVectorParameterValue(ArtPositionParamName, FLinearColor(NestedTransform.Position.X, NestedTransform.Position.Y, 0.0f, 0.0f));
        Mat->SetVectorParameterValue(ArtScaleParamName, FLinearColor(NestedTransform.Scale.X, NestedTransform.Scale.Y, 0.0f, 0.0f));
        Mat->SetScalarParameterValue(ArtRotationParamName, NestedTransform.Rotation);
        Mat->SetVectorParameterValue(ArtPivotParamName, FLinearColor(ChildEffectivePivot.X, ChildEffectivePivot.Y, 0.0f, 0.0f));

        // TextureBlend
        if (ChildTextureBlend > 0.0f)
        {
            const FFaceTextureSet& AltTex = Element.AltTextures;
            if (AltTex.Albedo) Mat->SetTextureParameterValue(ParamAltAlbedoParamName, AltTex.Albedo);
            if (AltTex.Normal) Mat->SetTextureParameterValue(ParamAltNormalParamName, AltTex.Normal);
            if (AltTex.Depth)  Mat->SetTextureParameterValue(ParamAltDepthParamName, AltTex.Depth);
            Mat->SetScalarParameterValue(ParamBlendParamName, ChildTextureBlend);
        }

        if (Element.IdleFrames.Num() > 0)
        {
            const FNestedAnimState* Anim = AnimStates.Find(StateKey);
            Mat->SetScalarParameterValue(NestedAnimParamName, Anim ? (float)Anim->FrameIndex : 0.0f);
        }
        Mat->SetScalarParameterValue(FName("StateBlendAlpha"), BlendAlpha);
        Mat->SetScalarParameterValue(FName("IsTopDown"), (CurrentState == EFaceAngleState::Top || CurrentState == EFaceAngleState::Bottom) ? 1.0f : 0.0f);
        Mat->SetScalarParameterValue(FName("IsTopView"), (CurrentState == EFaceAngleState::Top) ? 1.0f : 0.0f);
    }

    if (!Element.bJiggleEnabled && Element.Children.Num() > 0)
    {
        FName ChildTag = NestedArtChildTagCache[ParentElementName][Element.ElementName];
        for (const FFaceNestedArt& Child : Element.Children)
        {
            PushNestedChildArt(Child, NestedTransform, LayerTag, ChildTag);
        }
    }
}

FFaceArtTransform UFaceParallaxComponent::ComputeNestedEffectiveTransform(
    const FFaceNestedArt& Element, const FFaceArtTransform& ParentTransform, const FVector2D& JiggleOffset) const
{
    FFaceArtTransform Result;
    FVector2D BasePos = ParentTransform.Position + Element.RelativeTransform.Position;
    FVector2D PinOffset = FVector2D::ZeroVector;

    // When pinned, project the 3D pin at the live camera angle and compute
    // a position offset that keeps the element attached to the 3D point.
    if (Element.Pin3D.bPinned)
    {
        FVector2D PinnedUV = ProjectPinToUVInternal(Element.Pin3D.Position3D);
        // Reference: the unpinned pivot is the element's PivotPoint (0..1).
        // Map UV delta to a position offset relative to canvas size.
        FVector2D UVDelta = PinnedUV - Element.PivotPoint;
        float CanvasW = ActivePreset ? ActivePreset->CanvasSize.X : 512.0f;
        float CanvasH = ActivePreset ? ActivePreset->CanvasSize.Y : 512.0f;
        PinOffset = FVector2D(UVDelta.X * CanvasW, UVDelta.Y * CanvasH);
    }

    Result.Position = BasePos + PinOffset + JiggleOffset;
    Result.Scale = ParentTransform.Scale * Element.RelativeTransform.Scale;
    Result.Rotation = ParentTransform.Rotation + Element.RelativeTransform.Rotation;

    // View-angle rotation: as the camera turns away from the rendered
    // state's zone center, rotate the element (around its pin pivot,
    // since GetEffectivePivot returns the projected pin UV when pinned).
    // NOTE: accumulated AFTER the parent/relative composition — the
    // previous placement of this block before Result.Rotation was
    // assigned meant the += landed on a zeroed rotation and was
    // immediately overwritten.
    if (Element.Pin3D.bPinned && Element.Pin3D.bEnableViewAngleRotation)
    {
        const float YawCenter = GetZoneCenterYaw(CurrentState);
        const float PitchCenter = GetZoneCenterPitch(CurrentState);
        const float YawDev = CurrentYaw - YawCenter;
        const float PitchDev = CurrentPitch - PitchCenter;
        // Phase 5: pitch-aware 2D rotation (byte-identical to the old
        // yaw-only path at zero pitch) plus view-angle scale easing.
        Result.Rotation += PinRotationFromViewAngles(YawDev, PitchDev, HalfZoneWidth,
            Element.Pin3D.MinRotation, Element.Pin3D.MaxRotation,
            Element.Pin3D.RotationSensitivity);
        Result.Scale *= PinScaleFromView(YawDev, PitchDev, Element.Pin3D.MinScale);
    }
    return Result;
}

// --- NESTED ART BP FUNCTIONS ---

int32 UFaceParallaxComponent::GetNestedElementCount(EFaceAngleState State, FName LayerTag) const
{
    if (!ActivePreset) return 0;
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    return Slot.NestedElements.Num();
}

FFaceNestedArt UFaceParallaxComponent::GetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ActivePreset) return FFaceNestedArt();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return FFaceNestedArt();
    return Slot.NestedElements[Index];
}

void UFaceParallaxComponent::SetNestedElement(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceNestedArt& Element)
{
    if (!ActivePreset) return;
    FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return;
    Slot.NestedElements[Index] = Element;
}

void UFaceParallaxComponent::AddNestedElement(EFaceAngleState State, FName LayerTag, const FFaceNestedArt& Element)
{
    if (!ActivePreset) return;
    FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, LayerTag);
    Slot.NestedElements.Add(Element);
}

void UFaceParallaxComponent::RemoveNestedElement(EFaceAngleState State, FName LayerTag, int32 Index)
{
    if (!ActivePreset) return;
    FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return;
    Slot.NestedElements.RemoveAt(Index);
}

void UFaceParallaxComponent::SetNestedTextures(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceTextureSet& Textures)
{
    if (!ActivePreset) return;
    FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return;
    Slot.NestedElements[Index].Textures = Textures;
}

void UFaceParallaxComponent::SetNestedTransform(EFaceAngleState State, FName LayerTag, int32 Index, const FFaceArtTransform& Transform)
{
    if (!ActivePreset) return;
    FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return;
    Slot.NestedElements[Index].RelativeTransform = Transform;
}

void UFaceParallaxComponent::SetNestedPivot(EFaceAngleState State, FName LayerTag, int32 Index, FVector2D Pivot)
{
    if (!ActivePreset) return;
    FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return;
    Slot.NestedElements[Index].PivotPoint = Pivot;
}

void UFaceParallaxComponent::SetNestedVisibility(EFaceAngleState State, FName LayerTag, FName ElementName, EFaceAngleState ViewState, bool bVisible)
{
    if (!ActivePreset) return;
    FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, LayerTag);
    for (FFaceNestedArt& Element : Slot.NestedElements)
    {
        if (Element.ElementName == ElementName)
        {
            Element.ViewVisibility.Add(ViewState, bVisible);
            return;
        }
    }
}

FFaceLayerDef UFaceParallaxComponent::GetLayerDefinition(int32 Index) const
{
    if (Index < 0 || Index >= LayerDefinitions.Num()) return FFaceLayerDef();
    return LayerDefinitions[Index];
}

void UFaceParallaxComponent::SetLayerDefinition(int32 Index, const FFaceLayerDef& Def)
{
    if (Index < 0 || Index >= LayerDefinitions.Num()) return;
    LayerDefinitions[Index] = Def;
}

void UFaceParallaxComponent::AddLayerDefinition(const FFaceLayerDef& Def)
{
    LayerDefinitions.Add(Def);
}

void UFaceParallaxComponent::RemoveLayerDefinition(int32 Index)
{
    if (Index >= 0 && Index < LayerDefinitions.Num())
        LayerDefinitions.RemoveAt(Index);
}

void UFaceParallaxComponent::SetLayerVisibility(FName LayerTag, bool bVisible)
{
    if (bVisible)
        LayerVisibilityOverrides.Remove(LayerTag);
    else
        LayerVisibilityOverrides.Add(LayerTag, false);

    AActor* Owner = GetOwner();
    if (!Owner) return;
    TArray<UPrimitiveComponent*> PrimComps;
    Owner->GetComponents<UPrimitiveComponent>(PrimComps);
    for (UPrimitiveComponent* Prim : PrimComps)
    {
        if (Prim && Prim->ComponentHasTag(LayerTag))
        {
            Prim->SetVisibility(bVisible, true);
        }
    }
}

bool UFaceParallaxComponent::GetLayerVisibility(FName LayerTag) const
{
    const bool* Found = LayerVisibilityOverrides.Find(LayerTag);
    return Found ? *Found : true;
}

float UFaceParallaxComponent::GetLayerDepthMin(int32 LayerIndex) const
{
    if (LayerIndex < 0 || LayerIndex >= LayerDefinitions.Num()) return 0.0f;
    return LayerDefinitions[LayerIndex].DepthMin;
}

float UFaceParallaxComponent::GetLayerDepthMax(int32 LayerIndex) const
{
    if (LayerIndex < 0 || LayerIndex >= LayerDefinitions.Num()) return 1.0f;
    return LayerDefinitions[LayerIndex].DepthMax;
}

void UFaceParallaxComponent::SetLayerDepthRange(int32 LayerIndex, float Min, float Max)
{
    if (LayerIndex < 0 || LayerIndex >= LayerDefinitions.Num()) return;
    LayerDefinitions[LayerIndex].DepthMin = FMath::Clamp(Min, -1.0f, 1.0f);
    LayerDefinitions[LayerIndex].DepthMax = FMath::Clamp(Max, -1.0f, 1.0f);
}

FVector2D UFaceParallaxComponent::GetLayerParallaxOffset(int32 LayerIndex) const
{
    if (LayerIndex < 0 || LayerIndex >= LayerParallaxOffsets.Num()) return FVector2D::ZeroVector;
    return LayerParallaxOffsets[LayerIndex];
}

// --- 3D PIN SYSTEM ---

FVector2D UFaceParallaxComponent::GetEffectivePivot(const FFaceNestedArt& Element) const
{
    if (Element.Pin3D.bPinned)
    {
        return ProjectPinToUVInternal(Element.Pin3D.Position3D);
    }
    return Element.PivotPoint;
}

float UFaceParallaxComponent::PinRotationFromYawDev(float YawDev, float HalfZoneWidth,
    float MinRotation, float MaxRotation, float RotationSensitivity)
{
    // Wrap to [-180, 180]
    while (YawDev > 180.0f) YawDev -= 360.0f;
    while (YawDev < -180.0f) YawDev += 360.0f;

    const float HalfWidth = FMath::Max(1.0f, HalfZoneWidth);
    const float NormDev = FMath::Clamp(YawDev / HalfWidth, -1.0f, 1.0f);
    const float Mapped = FMath::Lerp(MinRotation, MaxRotation, 0.5f * (NormDev + 1.0f));
    return Mapped * RotationSensitivity;
}

float UFaceParallaxComponent::PinRotationFromViewAngles(float YawDev, float PitchDev,
    float HalfZoneWidth, float MinRotation, float MaxRotation, float RotationSensitivity)
{
    // Wrap both axes to [-180, 180]
    while (YawDev > 180.0f) YawDev -= 360.0f;
    while (YawDev < -180.0f) YawDev += 360.0f;
    while (PitchDev > 180.0f) PitchDev -= 360.0f;
    while (PitchDev < -180.0f) PitchDev += 360.0f;

    const float HalfWidth = FMath::Max(1.0f, HalfZoneWidth);
    const float NormYaw = FMath::Clamp(YawDev / HalfWidth, -1.0f, 1.0f);
    const float NormPitch = FMath::Clamp(PitchDev / HalfWidth, -1.0f, 1.0f);
    // Pitch attenuates the yaw driver. At PitchDev = 0 the driver is exactly
    // NormYaw, so the result is byte-identical to PinRotationFromYawDev;
    // at the pitch zone edge the rotation eases to the center (MinRotation).
    const float Driver = NormYaw * (1.0f - FMath::Abs(NormPitch));
    const float Mapped = FMath::Lerp(MinRotation, MaxRotation, 0.5f * (Driver + 1.0f));
    return Mapped * RotationSensitivity;
}

float UFaceParallaxComponent::PinScaleFromView(float YawDev, float PitchDev, float MinScale)
{
    const float YawRad = FMath::DegreesToRadians(YawDev);
    const float PitchRad = FMath::DegreesToRadians(PitchDev);
    const float W = 1.0f - FMath::Abs(FMath::Cos(YawRad) * FMath::Cos(PitchRad));
    return FMath::Lerp(1.0f, MinScale, W);
}

FVector2D UFaceParallaxComponent::ProjectPinToUVAtAngles(const FVector& Pin3D, float YawDeg, float PitchDeg, const FFaceProfile3D& Profile)
{
    float YawRad = FMath::DegreesToRadians(YawDeg);
    float CosY = FMath::Cos(YawRad);
    float SinY = FMath::Sin(YawRad);

    float PitchRad = FMath::DegreesToRadians(PitchDeg);
    float CosP = FMath::Cos(PitchRad);
    float SinP = FMath::Sin(PitchRad);

    // 3D pin in face-local space
    float WX = Pin3D.X * Profile.FaceHalfWidth;
    float WY = Pin3D.Y * Profile.FaceHalfHeight;
    float WZ = Pin3D.Z * Profile.FaceHalfDepth;

    // Yaw: project XZ onto view plane
    float ViewX = WX * CosY + WZ * SinY;
    float VisibleX = Profile.FaceHalfWidth * FMath::Abs(CosY) + Profile.FaceHalfDepth * FMath::Abs(SinY);

    // Pitch: project Y and Z*SinP
    float ViewY = WY * CosP;
    float VisibleY = Profile.FaceHalfHeight * FMath::Abs(CosP) + Profile.FaceHalfDepth * FMath::Abs(SinP);

    float UVx = 0.5f;
    float UVy = 0.5f;
    if (VisibleX > KINDA_SMALL_NUMBER) UVx = 0.5f + 0.5f * ViewX / VisibleX;
    if (VisibleY > KINDA_SMALL_NUMBER) UVy = 0.5f + 0.5f * ViewY / VisibleY;

    return FVector2D(FMath::Clamp(UVx, 0.0f, 1.0f), FMath::Clamp(UVy, 0.0f, 1.0f));
}

FVector2D UFaceParallaxComponent::ProjectPinToUVInternal(const FVector& Pin3D) const
{
    // Use live camera angle for continuous tracking of the rendered state.
    return ProjectPinToUVAtAngles(Pin3D, CurrentYaw, CurrentPitch, FaceProfile);
}

FVector2D UFaceParallaxComponent::ProjectPinToUVForStateInternal(const FVector& Pin3D, EFaceAngleState ViewState) const
{
    // Use the zone-center camera angle for a specific view state (authoring:
    // "what UV does this pin map to in the Right Profile view?").
    return ProjectPinToUVAtAngles(Pin3D, GetZoneCenterYaw(ViewState), GetZoneCenterPitch(ViewState), FaceProfile);
}

FVector2D UFaceParallaxComponent::ProjectPinToUV(FVector Pin3D, EFaceAngleState ViewState) const
{
    return ProjectPinToUVForStateInternal(Pin3D, ViewState);
}

FVector2D UFaceParallaxComponent::ProjectPinToUVForState(FVector Pin3D, EFaceAngleState ViewState) const
{
    return ProjectPinToUVForStateInternal(Pin3D, ViewState);
}

void UFaceParallaxComponent::DetectFaceProfileFromPreset()
{
    if (!ActivePreset) return;
    OutlinePointCache.Reset();

    // Helper: scan all layers for a state, return max width/height
    auto GetMaxTexDimensions = [this](EFaceAngleState State, int32& OutMaxW, int32& OutMaxH) {
        OutMaxW = 0;
        OutMaxH = 0;
        if (!ActivePreset->HasState(State)) return;
        TArray<FName> Tags = ActivePreset->GetAllLayerTags(State);
        for (FName Tag : Tags)
        {
            const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, Tag);
            OutMaxW = FMath::Max(OutMaxW, Slot.Textures.SourceTexWidth);
            OutMaxH = FMath::Max(OutMaxH, Slot.Textures.SourceTexHeight);
        }
    };

    // Helper: check if a state should be considered for profile extraction
    auto IsOutlineState = [this](EFaceAngleState S) -> bool {
        return OutlineViewStates.Contains(S);
    };

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::Back, EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    // First pass: use Front state for width/height, RightProfile/LeftProfile for depth
    int32 FrontWidth = 0, FrontHeight = 0;
    if (IsOutlineState(EFaceAngleState::Front))
    {
        GetMaxTexDimensions(EFaceAngleState::Front, FrontWidth, FrontHeight);
        if (FrontWidth > 0) FaceProfile.FaceHalfWidth = (float)FrontWidth * 0.5f;
        if (FrontHeight > 0) FaceProfile.FaceHalfHeight = (float)FrontHeight * 0.5f;
    }

    // Cross-reference Top/Bottom views for height and depth validation
    int32 TopW = 0, TopH = 0;
    if (IsOutlineState(EFaceAngleState::Top))
    {
        GetMaxTexDimensions(EFaceAngleState::Top, TopW, TopH);
        if (TopW > 0 && FrontWidth > 0 && FMath::Abs(FrontWidth - TopW) > 1)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DetectFaceProfile] Front width %d differs from Top width %d — art may be misaligned."),
                FrontWidth, TopW);
        }
        if (FrontHeight <= 0 && TopH > 0)
        {
            FaceProfile.FaceHalfHeight = (float)TopH * 0.5f;
        }
    }

    int32 BotW = 0, BotH = 0;
    if (IsOutlineState(EFaceAngleState::Bottom))
    {
        GetMaxTexDimensions(EFaceAngleState::Bottom, BotW, BotH);
        if (BotW > 0 && FrontWidth > 0 && FMath::Abs(FrontWidth - BotW) > 1)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DetectFaceProfile] Front width %d differs from Bottom width %d — art may be misaligned."),
                FrontWidth, BotW);
        }
        if (BotH > 0 && FrontHeight > 0 && FMath::Abs(FrontHeight - BotH) > 1)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[DetectFaceProfile] Front height %d differs from Bottom height %d — art may be misaligned."),
                FrontHeight, BotH);
        }
        if (FrontHeight <= 0 && BotH > 0 && FaceProfile.FaceHalfHeight <= 0.5f)
        {
            FaceProfile.FaceHalfHeight = (float)BotH * 0.5f;
        }
    }

    // Depth from profile views (scan all layers)
    if (IsOutlineState(EFaceAngleState::RightProfile))
    {
        int32 RightProfW = 0, RightProfH = 0;
        GetMaxTexDimensions(EFaceAngleState::RightProfile, RightProfW, RightProfH);
        if (RightProfW > 0) FaceProfile.FaceHalfDepth = (float)RightProfW * 0.5f;
    }

    if (FaceProfile.FaceHalfDepth <= 0.5f && IsOutlineState(EFaceAngleState::LeftProfile))
    {
        int32 LeftProfW = 0, LeftProfH = 0;
        GetMaxTexDimensions(EFaceAngleState::LeftProfile, LeftProfW, LeftProfH);
        if (LeftProfW > 0) FaceProfile.FaceHalfDepth = (float)LeftProfW * 0.5f;
    }

    // Depth fallback from Top/Bottom height
    if (FaceProfile.FaceHalfDepth <= 0.5f)
    {
        if (TopH > 0) FaceProfile.FaceHalfDepth = (float)TopH * 0.5f;
        else if (BotH > 0) FaceProfile.FaceHalfDepth = (float)BotH * 0.5f;
    }

    // Fallback: scan outline states for max dimensions
    if (FaceProfile.FaceHalfWidth <= 0.5f || FaceProfile.FaceHalfHeight <= 0.5f)
    {
        float MaxWidth = 0.0f, MaxHeight = 0.0f;
        for (EFaceAngleState S : AllStates)
        {
            if (!IsOutlineState(S)) continue;
            int32 S_W = 0, S_H = 0;
            GetMaxTexDimensions(S, S_W, S_H);
            if (S_W > 0) MaxWidth = FMath::Max(MaxWidth, (float)S_W);
            if (S_H > 0) MaxHeight = FMath::Max(MaxHeight, (float)S_H);
        }
        if (MaxWidth > 0.0f && FaceProfile.FaceHalfWidth <= 0.5f)
            FaceProfile.FaceHalfWidth = MaxWidth * 0.5f;
        if (MaxHeight > 0.0f && FaceProfile.FaceHalfHeight <= 0.5f)
            FaceProfile.FaceHalfHeight = MaxHeight * 0.5f;
        if (MaxWidth > 0.0f && FaceProfile.FaceHalfDepth <= 0.5f)
            FaceProfile.FaceHalfDepth = MaxWidth * 0.5f;
    }

    // Silhouette refinement: the edges of the rotation views carry real shape
    // info — the visible face silhouette is narrower than the full canvas.
    {
        TArray<FVector2D> Sil = ExtractSilhouettePoints(EFaceAngleState::Front);
        if (Sil.Num() >= 2)
        {
            float MinX = 1.0f, MaxX = -1.0f, MinY = 1.0f, MaxY = -1.0f;
            for (const FVector2D& Pt : Sil)
            {
                MinX = FMath::Min(MinX, Pt.X); MaxX = FMath::Max(MaxX, Pt.X);
                MinY = FMath::Min(MinY, Pt.Y); MaxY = FMath::Max(MaxY, Pt.Y);
            }
            if (MaxX > MinX && MaxY > MinY)
            {
                FaceProfile.FaceHalfWidth = FMath::Max(0.5f, (MaxX - MinX) * FaceProfile.FaceHalfWidth);
                FaceProfile.FaceHalfHeight = FMath::Max(0.5f, (MaxY - MinY) * FaceProfile.FaceHalfHeight);
            }
        }
        TArray<FVector2D> SilR = ExtractSilhouettePoints(EFaceAngleState::RightProfile);
        if (SilR.Num() >= 2)
        {
            float MinX = 1.0f, MaxX = -1.0f;
            for (const FVector2D& Pt : SilR)
            {
                MinX = FMath::Min(MinX, Pt.X); MaxX = FMath::Max(MaxX, Pt.X);
            }
            if (MaxX > MinX)
                FaceProfile.FaceHalfDepth = FMath::Max(0.5f, (MaxX - MinX) * FaceProfile.FaceHalfDepth);
        }
    }

    // Propagate profile to depth debug visualizer if present
    if (AActor* Owner = GetOwner())
    {
        UDepthDebugVisualizerComponent* Vis = Owner->FindComponentByClass<UDepthDebugVisualizerComponent>();
        if (Vis)
        {
            Vis->SetProfileDimensions(FaceProfile.FaceHalfWidth, FaceProfile.FaceHalfHeight, FaceProfile.FaceHalfDepth);
        }
    }
}

// --- OUTLINE → DEPTH MAP ---

void UFaceParallaxComponent::ClearOutlinePointCache()
{
    OutlinePointCache.Reset();
}

TArray<FVector2D> UFaceParallaxComponent::ExtractSilhouettePoints(EFaceAngleState State, int32 MaxPoints) const
{
    TArray<FVector2D>* Cached = OutlinePointCache.Find(State);
    if (Cached && Cached->Num() > 0)
    {
        return *Cached;
    }

    TArray<FVector2D> Result;
    if (!ActivePreset || !ActivePreset->HasState(State))
    {
        return Result;
    }
    MaxPoints = FMath::Clamp(MaxPoints, 4, 256);

    // First assigned layer with a source-readable albedo wins
    UTexture2D* Tex = nullptr;
    TArray<FName> Tags = ActivePreset->GetAllLayerTags(State);
    for (FName Tag : Tags)
    {
        UTexture2D* T = ActivePreset->GetSlot(State, Tag).Textures.Albedo;
        if (T && T->GetPlatformData() && T->GetPlatformData()->Mips.Num() > 0)
        {
            Tex = T;
            break;
        }
    }
    if (!Tex)
    {
        return Result;
    }

    FTextureSource& Src = Tex->Source;
    if (Src.GetNumMips() == 0)
    {
        return Result;
    }
    const int32 W = Src.GetSizeX();
    const int32 H = Src.GetSizeY();
    if (W < 4 || H < 4)
    {
        return Result;
    }

    const uint8* Pixels = Src.LockMip(0);
    if (!Pixels)
    {
        return Result;
    }

    const int32 BytesPerPixel = Src.GetBytesPerPixel();
    const ETextureSourceFormat Fmt = Src.GetFormat();
    const bool bHasAlpha = (Fmt == TSF_BGRA8) && BytesPerPixel >= 4;
    const int32 AlphaOffset = (Fmt == TSF_BGRA8) ? 3 : 0;
    const bool bGrayscale = (Fmt == TSF_G8) && BytesPerPixel == 1;

    auto SampleOpaque = [&](int32 X, int32 Y) -> bool
    {
        const uint8* P = Pixels + (size_t)Y * (size_t)W * BytesPerPixel + X * BytesPerPixel;
        if (bHasAlpha) return P[AlphaOffset] > 40;
        if (bGrayscale) return P[0] < 250;
        return P[0] < 250 && P[1] < 250 && P[2] < 250;
    };

    // Scan every N-th row; emit (xMin, xMax) pairs per row, normalized to [-1,1]
    const int32 Rows = FMath::Max(2, MaxPoints / 2);
    const int32 Step = FMath::Max(1, H / Rows);
    for (int32 Y = Step / 2; Y < H; Y += Step)
    {
        int32 MinX = -1, MaxX = -1;
        for (int32 X = 0; X < W; ++X)
        {
            if (SampleOpaque(X, Y)) { MinX = X; break; }
        }
        for (int32 X = W - 1; X >= 0; --X)
        {
            if (SampleOpaque(X, Y)) { MaxX = X; break; }
        }
        if (MinX >= 0 && MaxX >= MinX)
        {
            const float YN = 1.0f - ((float)Y + 0.5f) * 2.0f / (float)H;
            Result.Add(FVector2D(((float)MinX + 0.5f) * 2.0f / (float)W - 1.0f, YN));
            Result.Add(FVector2D(((float)MaxX + 0.5f) * 2.0f / (float)W - 1.0f, YN));
        }
    }
    Src.UnlockMip(0);

    if (Result.Num() > 0)
    {
        OutlinePointCache.Add(State, Result);
    }
    return Result;
}

float UFaceParallaxComponent::SilhouetteDistanceToEdge(EFaceAngleState State, FVector2D LocalPoint) const
{
    TArray<FVector2D> Pts = ExtractSilhouettePoints(State);
    if (Pts.Num() < 2)
    {
        return 1.0f;
    }
    return SilhouetteDistanceToEdgeStatic(Pts, LocalPoint);
}

float UFaceParallaxComponent::SilhouetteDistanceToEdgeStatic(const TArray<FVector2D>& Pts, FVector2D P)
{
    if (Pts.Num() < 2)
    {
        return 1.0f;
    }
    const int32 RowCount = Pts.Num() / 2;
    int32 RowBelow = RowCount - 1; // fallback: bottom row when the query is below everything
    for (int32 r = 0; r < RowCount; ++r)
    {
        // Rows are Y-ascending (top first): the FIRST row at-or-below the
        // query height is the nearest one below it.
        if (Pts[r * 2].Y <= P.Y)
        {
            RowBelow = r;
            break;
        }
    }
    int32 RowAbove = RowBelow;
    for (int32 r = 0; r < RowCount; ++r)
    {
        // Nearest row strictly above the query height.
        if (Pts[r * 2].Y > P.Y)
        {
            RowAbove = r;
            break;
        }
    }
    const float Y0 = Pts[RowBelow * 2].Y;
    const float Y1 = Pts[RowAbove * 2].Y;
    const float T = (Y1 - Y0) > KINDA_SMALL_NUMBER ? FMath::Clamp((P.Y - Y0) / (Y1 - Y0), 0.0f, 1.0f) : 0.0f;
    const float XL = FMath::Lerp(Pts[RowBelow * 2].X, Pts[RowAbove * 2].X, T);
    const float XR = FMath::Lerp(Pts[RowBelow * 2 + 1].X, Pts[RowAbove * 2 + 1].X, T);
    if (P.X < XL)
    {
        return P.X - XL;
    }
    if (P.X > XR)
    {
        return XR - P.X;
    }
    return FMath::Min(P.X - XL, XR - P.X);
}

float UFaceParallaxComponent::VisualHullDepthStatic(
    const TArray<FVector2D>& Front, const TArray<FVector2D>& Right,
    const TArray<FVector2D>& Left, const TArray<FVector2D>& Top,
    const TArray<FVector2D>& Bottom, FVector2D P)
{
    auto Interior = [](const TArray<FVector2D>& Pts, FVector2D Q) -> float
    {
        if (Pts.Num() < 2)
        {
            return 1.0f;
        }
        return FMath::Max(0.0f, SilhouetteDistanceToEdgeStatic(Pts, Q));
    };

    // Front defines the 2D shape; the rotation views constrain depth:
    // profile edges act on the horizontal axis, top/bottom on the vertical.
    float D = Interior(Front, P);
    D = FMath::Min(D, Interior(Right, FVector2D(0.0f, P.Y)));
    D = FMath::Min(D, Interior(Left, FVector2D(0.0f, P.Y)));
    D = FMath::Min(D, Interior(Top, FVector2D(P.X, 0.0f)));
    D = FMath::Min(D, Interior(Bottom, FVector2D(P.X, 0.0f)));
    return FMath::Clamp(D, 0.0f, 1.0f);
}

bool UFaceParallaxComponent::GenerateDepthBufferFromOutlines(int32 GridSize, TArray<float>& OutDepth, float& OutCellSize) const
{
    OutDepth.Reset();
    GridSize = FMath::Clamp(GridSize, 8, 256);
    OutCellSize = 2.0f / (float)GridSize;

    TArray<FVector2D> Front = ExtractSilhouettePoints(EFaceAngleState::Front);
    TArray<FVector2D> Right = ExtractSilhouettePoints(EFaceAngleState::RightProfile);
    TArray<FVector2D> Left = ExtractSilhouettePoints(EFaceAngleState::LeftProfile);
    TArray<FVector2D> Top = ExtractSilhouettePoints(EFaceAngleState::Top);
    TArray<FVector2D> Bottom = ExtractSilhouettePoints(EFaceAngleState::Bottom);

    const bool bAny = Front.Num() >= 2;
    OutDepth.Reserve(GridSize * GridSize);
    for (int32 gy = 0; gy < GridSize; ++gy)
    {
        const float Y = -1.0f + OutCellSize * ((float)gy + 0.5f);
        for (int32 gx = 0; gx < GridSize; ++gx)
        {
            const float X = -1.0f + OutCellSize * ((float)gx + 0.5f);
            OutDepth.Add(VisualHullDepthStatic(Front, Right, Left, Top, Bottom, FVector2D(X, Y)));
        }
    }
    return bAny;
}

float UFaceParallaxComponent::VisualHullYawForState(EFaceAngleState State)
{
    // Default zone centers: HalfZoneWidth 22.5, multipliers {1, 3, 5, 7}
    static const float BM[4] = { 22.5f, 67.5f, 112.5f, 157.5f };
    switch (State)
    {
        case EFaceAngleState::Front:               return 0.0f;
        case EFaceAngleState::ThreeQuarterRight:   return (BM[0] + BM[1]) * 0.5f;
        case EFaceAngleState::RightProfile:        return (BM[1] + BM[2]) * 0.5f;
        case EFaceAngleState::BackRight:           return (BM[2] + BM[3]) * 0.5f;
        case EFaceAngleState::Back:                return 180.0f;
        case EFaceAngleState::BackLeft:            return -(BM[2] + BM[3]) * 0.5f;
        case EFaceAngleState::LeftProfile:         return -(BM[1] + BM[2]) * 0.5f;
        case EFaceAngleState::ThreeQuarterLeft:    return -(BM[0] + BM[1]) * 0.5f;
        default:                                   return 0.0f;
    }
}

float UFaceParallaxComponent::VisualHullPitchForState(EFaceAngleState State)
{
    if (State == EFaceAngleState::Top) return 90.0f;
    if (State == EFaceAngleState::Bottom) return -90.0f;
    return 0.0f;
}

bool UFaceParallaxComponent::GenerateDepthBufferFromOutlinesForView(int32 GridSize, float YawDegrees,
    float PitchDegrees, TArray<float>& OutDepth, float& OutCellSize) const
{
    OutDepth.Reset();
    GridSize = FMath::Clamp(GridSize, 8, 256);
    OutCellSize = 2.0f / (float)GridSize;

    TArray<FVector2D> Front = ExtractSilhouettePoints(EFaceAngleState::Front);
    TArray<FVector2D> Right = ExtractSilhouettePoints(EFaceAngleState::RightProfile);
    TArray<FVector2D> Left = ExtractSilhouettePoints(EFaceAngleState::LeftProfile);
    TArray<FVector2D> Top = ExtractSilhouettePoints(EFaceAngleState::Top);
    TArray<FVector2D> Bottom = ExtractSilhouettePoints(EFaceAngleState::Bottom);

    const bool bAny = Front.Num() >= 2;
    OutDepth.Reserve(GridSize * GridSize);
    for (int32 gy = 0; gy < GridSize; ++gy)
    {
        const float Y = -1.0f + OutCellSize * ((float)gy + 0.5f);
        for (int32 gx = 0; gx < GridSize; ++gx)
        {
            const float X = -1.0f + OutCellSize * ((float)gx + 0.5f);
            OutDepth.Add(VisualHullDepthStatic(Front, Right, Left, Top, Bottom,
                FVector2D(X, Y), YawDegrees, PitchDegrees));
        }
    }
    return bAny;
}

float UFaceParallaxComponent::VisualHullDepthStatic(
    const TArray<FVector2D>& Front, const TArray<FVector2D>& Right,
    const TArray<FVector2D>& Left, const TArray<FVector2D>& Top,
    const TArray<FVector2D>& Bottom, FVector2D P, float YawDegrees, float PitchDegrees)
{
    auto Interior = [](const TArray<FVector2D>& Pts, FVector2D Q) -> float
    {
        if (Pts.Num() < 2)
        {
            return 1.0f;
        }
        return FMath::Max(0.0f, SilhouetteDistanceToEdgeStatic(Pts, Q));
    };

    const float YawRad = FMath::DegreesToRadians(YawDegrees);
    const float PitchRad = FMath::DegreesToRadians(PitchDegrees);
    const float CosY = FMath::Cos(YawRad), SinY = FMath::Sin(YawRad);
    const float CosP = FMath::Cos(PitchRad), SinP = FMath::Sin(PitchRad);

    // Camera basis in front space: R = screen right, U = screen up, depth axis
    // is -V (V = view direction). Screen point P = (X', Y') in the target view.
    auto FrontSpace = [&](float Xp, float Yp, float Zp, float& OutFx, float& OutFy, float& OutZf)
    {
        OutFx = Xp * CosY + Zp * (SinY * CosP);
        OutFy = Yp * CosP - Zp * SinP;
        OutZf = -Xp * SinY + Yp * (CosY * SinP) + Zp * (CosY * CosP);
    };

    // Front silhouette's vertical extent (rows are Y-ascending, (xMin, Y, xMax, Y)).
    // The distance helper clamps out-of-range queries to the nearest row, which
    // would otherwise leave top/bottom views unbounded along the head's height.
    const bool bFrontHasExtent = Front.Num() >= 2;
    const float FrontYMin = bFrontHasExtent ? Front[0].Y : -1.0f;
    const float FrontYMax = bFrontHasExtent ? Front[Front.Num() - 2].Y : 1.0f;

    // Feasibility: is the hull point at depth Z' (screen X', Y') inside every prism?
    auto Feasible = [&](float Xp, float Yp, float Zp) -> bool
    {
        float Fx, Fy, Zf;
        FrontSpace(Xp, Yp, Zp, Fx, Fy, Zf);
        if (bFrontHasExtent && (Fy < FrontYMin || Fy > FrontYMax)) return false;
        if (SilhouetteDistanceToEdgeStatic(Front, FVector2D(Fx, Fy)) < 0.0f) return false;
        const float HS = FMath::Min(Interior(Right, FVector2D(0.0f, Fy)), Interior(Left, FVector2D(0.0f, Fy)));
        const float HT = FMath::Min(Interior(Top, FVector2D(Fx, 0.0f)), Interior(Bottom, FVector2D(Fx, 0.0f)));
        return FMath::Abs(Zf) <= HS && FMath::Abs(Zf) <= HT;
    };

    // Max depth along the view ray (binary search; membership is monotone in Z'
    // for camera angles in the parallax range).
    float ZBound = 0.0f;
    if (Feasible(P.X, P.Y, 0.0f))
    {
        float Lo = 0.0f, Hi = 1.0f;
        for (int32 Iter = 0; Iter < 14; ++Iter)
        {
            const float Mid = (Lo + Hi) * 0.5f;
            if (Feasible(P.X, P.Y, Mid)) Lo = Mid;
            else Hi = Mid;
        }
        ZBound = Lo;
    }

    // Dome falloff: distance to the front silhouette foreshortened into the
    // target view. Skipped when the projection is degenerate (profile/back-ish
    // yaw or top/bottom pitch), where membership alone defines the surface.
    float Dome = 1.0f;
    if (FMath::Abs(CosY) >= 0.2f && FMath::Abs(CosP) >= 0.2f && Front.Num() >= 2)
    {
        TArray<FVector2D> Projected;
        Projected.Reserve(Front.Num());
        for (int32 i = 0; i + 1 < Front.Num(); i += 2)
        {
            // Flat front point (Fx, Fy, 0) into the target view's screen space.
            // Views past 90 degrees mirror the front shape; keep (xMin, xMax)
            // row ordering by swapping the pair when yaw flips horizontally.
            float Px0 = Front[i].X * CosY;
            float Py0 = Front[i].X * (SinY * SinP) + Front[i].Y * CosP;
            float Px1 = Front[i + 1].X * CosY;
            float Py1 = Front[i + 1].X * (SinY * SinP) + Front[i + 1].Y * CosP;
            if (CosY < 0.0f)
            {
                Swap(Px0, Px1);
            }
            Projected.Add(FVector2D(Px0, Py0));
            Projected.Add(FVector2D(Px1, Py1));
        }
        Dome = FMath::Max(0.0f, SilhouetteDistanceToEdgeStatic(Projected, P));
    }

    return FMath::Clamp(FMath::Min(ZBound, Dome), 0.0f, 1.0f);
}

void UFaceParallaxComponent::SetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index, const FFacePin3D& Pin)
{
    if (!ActivePreset) return;
    FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return;
    Slot.NestedElements[Index].Pin3D = Pin;
}

FFacePin3D UFaceParallaxComponent::GetNestedPin3D(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ActivePreset) return FFacePin3D();
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return FFacePin3D();
    return Slot.NestedElements[Index].Pin3D;
}

FVector2D UFaceParallaxComponent::GetNestedEffectivePivot(EFaceAngleState State, FName LayerTag, int32 Index) const
{
    if (!ActivePreset) return FVector2D(0.5f, 0.5f);
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    if (Index < 0 || Index >= Slot.NestedElements.Num()) return FVector2D(0.5f, 0.5f);
    return GetEffectivePivot(Slot.NestedElements[Index]);
}

// --- OUTLINE ART CONCEPT ---

void UFaceParallaxComponent::SetOutlineViewState(EFaceAngleState State)
{
    OutlineViewStates.AddUnique(State);
}

void UFaceParallaxComponent::SetOutlineViewEnabled(EFaceAngleState State, bool bEnabled)
{
    if (bEnabled)
    {
        OutlineViewStates.AddUnique(State);
    }
    else
    {
        OutlineViewStates.Remove(State);
    }
}

void UFaceParallaxComponent::ClearOutlineViewStates()
{
    OutlineViewStates.Empty();
}

bool UFaceParallaxComponent::IsOutlineViewState(EFaceAngleState State) const
{
    return OutlineViewStates.Contains(State);
}

TArray<EFaceAngleState> UFaceParallaxComponent::GetOutlineViewStates() const
{
    TArray<EFaceAngleState> Result;
    for (const auto& E : OutlineViewStates)
        Result.Add(E);
    return Result;
}

// ====================================================================
// EDITOR BLEND PREVIEW
// ====================================================================

void UFaceParallaxComponent::SetBlendPreview(float Alpha)
{
    bBlendPreviewOverride = true;
    BlendPreviewAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
}

void UFaceParallaxComponent::ClearBlendPreview()
{
    bBlendPreviewOverride = false;
    BlendPreviewAlpha = 0.5f;
}

// ====================================================================
// ZONE MATH — editor-facing helpers
// ====================================================================

FName UFaceParallaxComponent::GetStateLabel(EFaceAngleState State)
{
    switch (State)
    {
        case EFaceAngleState::Front: return TEXT("Front");
        case EFaceAngleState::ThreeQuarterRight: return TEXT("3/4 R");
        case EFaceAngleState::RightProfile: return TEXT("Profile R");
        case EFaceAngleState::BackRight: return TEXT("Back R");
        case EFaceAngleState::Back: return TEXT("Back");
        case EFaceAngleState::BackLeft: return TEXT("Back L");
        case EFaceAngleState::LeftProfile: return TEXT("Profile L");
        case EFaceAngleState::ThreeQuarterLeft: return TEXT("3/4 L");
        case EFaceAngleState::Top: return TEXT("Top");
        case EFaceAngleState::Bottom: return TEXT("Bottom");
    }
    return TEXT("?");
}

FLinearColor UFaceParallaxComponent::GetStateColor(EFaceAngleState State)
{
    switch (State)
    {
        case EFaceAngleState::Front: return FLinearColor(0.6f, 0.8f, 1.0f);
        case EFaceAngleState::ThreeQuarterRight: return FLinearColor(0.5f, 0.7f, 1.0f);
        case EFaceAngleState::RightProfile: return FLinearColor(0.4f, 0.6f, 1.0f);
        case EFaceAngleState::BackRight: return FLinearColor(0.5f, 0.5f, 0.7f);
        case EFaceAngleState::Back: return FLinearColor(0.4f, 0.4f, 0.6f);
        case EFaceAngleState::BackLeft: return FLinearColor(0.5f, 0.5f, 0.7f);
        case EFaceAngleState::LeftProfile: return FLinearColor(0.4f, 0.6f, 1.0f);
        case EFaceAngleState::ThreeQuarterLeft: return FLinearColor(0.5f, 0.7f, 1.0f);
        case EFaceAngleState::Top: return FLinearColor(0.6f, 1.0f, 0.6f);
        case EFaceAngleState::Bottom: return FLinearColor(1.0f, 0.6f, 0.6f);
    }
    return FLinearColor(0.5f, 0.5f, 0.5f);
}

// ====================================================================
// PARAM REFERENCE — find which slots use a given material param name
// ====================================================================

TArray<FString> UFaceParallaxComponent::FindParamNameReferences(FName ParamName) const
{
    TArray<FString> Results;

    // Check component-level param names
    struct FNamed { const FName& Name; const TCHAR* Desc; };
    FNamed ToCheck[] = {
        { AlbedoParamName, TEXT("AlbedoTexture") },
        { NormalParamName, TEXT("NormalTexture") },
        { DepthParamName, TEXT("DepthTexture") },
        { AlbedoPrevParamName, TEXT("AlbedoPrev") },
        { NormalPrevParamName, TEXT("NormalPrev") },
        { DepthPrevParamName, TEXT("DepthPrev") },
        { ArtPositionParamName, TEXT("ArtPosition") },
        { ArtScaleParamName, TEXT("ArtScale") },
        { ArtRotationParamName, TEXT("ArtRotation") },
        { ExpressionBlendParamName, TEXT("ExpressionBlendAlpha") },
        { ExpressionAlbedoPrevParamName, TEXT("ExpressionAlbedoPrev") },
        { ExpressionNormalPrevParamName, TEXT("ExpressionNormalPrev") },
        { ExpressionDepthPrevParamName, TEXT("ExpressionDepthPrev") },
        { ParamBlendParamName, TEXT("ParamBlendAlpha") },
        { ParamAltAlbedoParamName, TEXT("AltAlbedo") },
        { ParamAltNormalParamName, TEXT("AltNormal") },
        { ParamAltDepthParamName, TEXT("AltDepth") },
        { ArtPivotParamName, TEXT("ArtPivot") },
        { NestedAnimParamName, TEXT("NestedAnimFrame") },
        { SwooshLayerBlendParamName, TEXT("SwooshLayerBlend") },
        { SwooshIntensityParamName, TEXT("SwooshIntensity") },
        { SwooshAngleParamName, TEXT("SwooshAngle") },
        { SwooshSizeParamName, TEXT("SwooshSize") },
        { SwooshTextureParamName, TEXT("SwooshTexture") },
    };

    for (const auto& Item : ToCheck)
    {
        if (Item.Name == ParamName)
        {
            Results.Add(FString::Printf(TEXT("[Component] %s"), Item.Desc));
        }
    }

    // Check each slot in the preset
    if (ActivePreset)
    {
        TArray<EFaceAngleState> States = {
            EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
            EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
            EFaceAngleState::Back, EFaceAngleState::BackLeft,
            EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
            EFaceAngleState::Top, EFaceAngleState::Bottom
        };
        for (EFaceAngleState St : States)
        {
            TArray<FName> Tags = ActivePreset->GetAllLayerTags(St);
            for (FName Tag : Tags)
            {
                const FFaceArtSlot& Slot = ActivePreset->GetSlot(St, Tag);
                for (const FFaceParamBinding& B : Slot.ParamBindings)
                {
                    if (B.ParamName == ParamName)
                    {
                        Results.Add(FString::Printf(TEXT("[Slot %s / %s] ParamBinding"),
                            *UFaceParallaxComponent::GetStateLabel(St).ToString(), *Tag.ToString()));
                    }
                }
                for (const FFaceNestedArt& N : Slot.NestedElements)
                {
                    for (const FFaceParamBinding& B : N.ParamBindings)
                    {
                        if (B.ParamName == ParamName)
                        {
                            Results.Add(FString::Printf(TEXT("[Nested %s / %s / %s] ParamBinding"),
                                *UFaceParallaxComponent::GetStateLabel(St).ToString(),
                                *Tag.ToString(), *N.ElementName.ToString()));
                        }
                    }
                }
            }
        }
    }

    return Results;
}

// ====================================================================
// ASYNC TEXTURE LOADING
// ====================================================================

void UFaceParallaxComponent::AsyncLoadSlotTextures(EFaceAngleState State, FName LayerTag)
{
    if (!ActivePreset || !bUseAsyncTextureLoading) return;
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
    if (!Slot.Textures.IsValid()) return;

    TArray<FSoftObjectPath> PathsToLoad;
    auto QueuePath = [&](const TSoftObjectPtr<UTexture2D>& SoftPtr)
    {
        if (!SoftPtr.IsNull() && !SoftPtr.IsValid())
        {
            FSoftObjectPath Path = SoftPtr.ToSoftObjectPath();
            if (!AsyncTextureCache.Contains(Path) && !ActiveTextureLoads.Contains(Path))
            {
                PathsToLoad.Add(Path);
                AsyncTextureCache.Add(Path, nullptr);
                AsyncTextureCacheOrder.Add(Path);
                EnforceAsyncCacheSize();
            }
        }
    };

    QueuePath(Slot.Textures.SoftAlbedo);
    QueuePath(Slot.Textures.SoftNormal);
    QueuePath(Slot.Textures.SoftDepth);

    for (const FFaceTextureSet& FT : Slot.BlinkFrames)
    {
        QueuePath(FT.SoftAlbedo);
        QueuePath(FT.SoftNormal);
        QueuePath(FT.SoftDepth);
    }

    for (const auto& ExprPair : Slot.ExpressionTextures)
    {
        QueuePath(ExprPair.Value.SoftAlbedo);
        QueuePath(ExprPair.Value.SoftNormal);
        QueuePath(ExprPair.Value.SoftDepth);
    }

    for (const auto& NamedExprPair : Slot.NamedExpressionTextures)
    {
        QueuePath(NamedExprPair.Value.SoftAlbedo);
        QueuePath(NamedExprPair.Value.SoftNormal);
        QueuePath(NamedExprPair.Value.SoftDepth);
    }

    for (const auto& VisemePair : Slot.NamedVisemeFrames)
    {
        for (const FFaceTextureSet& FT : VisemePair.Value.Frames)
        {
            QueuePath(FT.SoftAlbedo);
            QueuePath(FT.SoftNormal);
            QueuePath(FT.SoftDepth);
        }
    }

    if (PathsToLoad.Num() > 0)
    {
        ++LoadGeneration;
        int32 Gen = LoadGeneration;
        TSharedPtr<FStreamableHandle> Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
            PathsToLoad,
            FStreamableDelegate::CreateUObject(this, &UFaceParallaxComponent::OnAsyncTexturesLoaded)
        );
        for (const FSoftObjectPath& P : PathsToLoad)
        {
            ActiveTextureLoads.Add(P, TPair<TSharedPtr<FStreamableHandle>, int32>(Handle, Gen));
        }
    }
}

void UFaceParallaxComponent::AsyncUnloadSlotTextures(EFaceAngleState State, FName LayerTag)
{
    if (!ActivePreset || !bUseAsyncTextureLoading) return;
    const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);

    auto QueueUnload = [&](const TSoftObjectPtr<UTexture2D>& SoftPtr)
    {
        if (!SoftPtr.IsNull())
        {
            FSoftObjectPath Path = SoftPtr.ToSoftObjectPath();
            AsyncTextureCache.Remove(Path);
            AsyncTextureCacheOrder.Remove(Path);
            if (TPair<TSharedPtr<FStreamableHandle>, int32>* Entry = ActiveTextureLoads.Find(Path))
            {
                if (Entry->Key.IsValid()) Entry->Key->CancelHandle();
                ActiveTextureLoads.Remove(Path);
            }
        }
    };

    QueueUnload(Slot.Textures.SoftAlbedo);
    QueueUnload(Slot.Textures.SoftNormal);
    QueueUnload(Slot.Textures.SoftDepth);
}

void UFaceParallaxComponent::OnAsyncTexturesLoaded()
{
    if (!bUseAsyncTextureLoading) return;

    TArray<FSoftObjectPath> LoadedPaths;
    bool bNewCurrentGen = false;
    for (const auto& KV : ActiveTextureLoads)
    {
        UObject* Obj = KV.Key.ResolveObject();
        if (Obj)
        {
            AsyncTextureCache[KV.Key] = Cast<UTexture2D>(Obj);
            if (KV.Value.Value >= LoadGeneration)
                bNewCurrentGen = true;
        }
        LoadedPaths.Add(KV.Key);
    }
    for (const FSoftObjectPath& P : LoadedPaths)
        ActiveTextureLoads.Remove(P);

    if (!ActivePreset || !bNewCurrentGen) return;

    // Resolve CurrentState slot hard refs from cache
    auto ResolveStateTextures = [&](EFaceAngleState State)
    {
        TArray<FName> Tags = ActivePreset->GetAllLayerTags(State);
        for (FName Tag : Tags)
        {
            FFaceArtSlot& Slot = ActivePreset->GetSlotMutable(State, Tag);
            Slot.Textures.Albedo = ResolveTexture(Slot.Textures.SoftAlbedo);
            Slot.Textures.Normal = ResolveTexture(Slot.Textures.SoftNormal);
            Slot.Textures.Depth = ResolveTexture(Slot.Textures.SoftDepth);
        }
    };

    ResolveStateTextures(CurrentState);
    if (bIsInTransition)
        ResolveStateTextures(PreviousState);

    ApplyCurrentStateTextures();
}

UTexture2D* UFaceParallaxComponent::ResolveTexture(const TSoftObjectPtr<UTexture2D>& SoftPtr)
{
    if (SoftPtr.IsNull()) return nullptr;
    FSoftObjectPath Path = SoftPtr.ToSoftObjectPath();
    TObjectPtr<UTexture2D>* Found = AsyncTextureCache.Find(Path);
    return Found ? Found->Get() : nullptr;
}

void UFaceParallaxComponent::EnforceAsyncCacheSize()
{
    while (AsyncTextureCacheOrder.Num() > MaxAsyncTextureCacheSize)
    {
        FSoftObjectPath Oldest = AsyncTextureCacheOrder[0];
        AsyncTextureCacheOrder.RemoveAt(0);
        AsyncTextureCache.Remove(Oldest);
    }
}

void UFaceParallaxComponent::BuildNestedArtCache()
{
    NestedArtStateKeyCache.Reset();
    NestedArtChildTagCache.Reset();
    NestedArtChildStateKeyCache.Reset();
    bNestedArtCacheDirty = false;

    if (!ActivePreset) return;

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::Back, EFaceAngleState::BackLeft,
        EFaceAngleState::LeftProfile, EFaceAngleState::ThreeQuarterLeft,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    auto BuildForState = [&](EFaceAngleState State)
    {
        if (!ActivePreset->HasState(State)) return;
        TArray<FName> Tags = ActivePreset->GetAllLayerTags(State);
        for (FName LayerTag : Tags)
        {
            const FFaceArtSlot& Slot = ActivePreset->GetSlot(State, LayerTag);
            // Parent-level state keys
            TMap<FName, FName>& LayerCache = NestedArtStateKeyCache.FindOrAdd(LayerTag);
            for (const FFaceNestedArt& Elem : Slot.NestedElements)
            {
                FName Key = FName(*FString::Printf(TEXT("%s_%s"), *LayerTag.ToString(), *Elem.ElementName.ToString()));
                LayerCache.FindOrAdd(Elem.ElementName) = Key;

                // Child state keys: LayerTag_ParentElementName_ElementName
                TMap<FName, TMap<FName, FName>>& ParentLayer = NestedArtChildStateKeyCache.FindOrAdd(LayerTag);
                TMap<FName, FName>& ChildCache = ParentLayer.FindOrAdd(Elem.ElementName);
                for (const FFaceNestedArt& Child : Elem.Children)
                {
                    FName ChildKey = FName(*FString::Printf(TEXT("%s_%s_%s"),
                        *LayerTag.ToString(), *Elem.ElementName.ToString(), *Child.ElementName.ToString()));
                    ChildCache.FindOrAdd(Child.ElementName) = ChildKey;
                }
            }
            // Child tag cache: ParentElementName_ElementName
            for (const FFaceNestedArt& Elem : Slot.NestedElements)
            {
                TMap<FName, FName>& ParentCache = NestedArtChildTagCache.FindOrAdd(Elem.ElementName);
                for (const FFaceNestedArt& Child : Elem.Children)
                {
                    FName ChildTag = FName(*FString::Printf(TEXT("%s_%s"),
                        *Elem.ElementName.ToString(), *Child.ElementName.ToString()));
                    ParentCache.FindOrAdd(Child.ElementName) = ChildTag;
                }
            }
        }
    };

    for (EFaceAngleState S : AllStates)
        BuildForState(S);
}
