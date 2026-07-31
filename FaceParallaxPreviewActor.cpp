#include "FaceParallaxPreviewActor.h"

#if WITH_EDITOR
#include "FaceParallaxComponent.h"
#include "FaceParallaxPreset.h"
#include "DepthDebugVisualizerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMathLibrary.h"

AFaceParallaxPreviewActor::AFaceParallaxPreviewActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    PreviewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
    SetRootComponent(PreviewRoot);

    PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
    PreviewMesh->SetupAttachment(PreviewRoot);

    FaceParallax = CreateDefaultSubobject<UFaceParallaxComponent>(TEXT("FaceParallax"));
    FaceParallax->bUseMaterialDrivenDepth = true;
    FaceParallax->bAutoApplyPreset = true;

    DepthDebug = CreateDefaultSubobject<UDepthDebugVisualizerComponent>(TEXT("DepthDebug"));
    DepthDebug->bStartEnabled = false;

    SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
    SceneCapture->SetupAttachment(PreviewRoot);
    SceneCapture->bCaptureEveryFrame = false;
    SceneCapture->bCaptureOnMovement = false;
    SceneCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_LegacySceneCapture;
    SceneCapture->ShowFlags.SetAtmosphere(false);
    SceneCapture->ShowFlags.SetFog(false);
    SceneCapture->ShowFlags.SetSkyLighting(false);
    SceneCapture->ShowFlags.SetLighting(false);
    SceneCapture->ShowFlags.SetPostProcessing(false);
}

void AFaceParallaxPreviewActor::BeginPlay()
{
    Super::BeginPlay();
    bOrbitDirty = true;
    bCaptureDirty = true;
    UpdateCaptureTransform();
}

void AFaceParallaxPreviewActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bAutoRotate)
    {
        OrbitYaw += AutoRotateSpeed * DeltaTime;
        if (OrbitYaw > 360.0f) OrbitYaw -= 360.0f;
        bOrbitDirty = true;
    }

    if (bOrbitDirty)
    {
        UpdateCaptureTransform();
        bOrbitDirty = false;
    }
    if (bCaptureDirty)
    {
        SceneCapture->CaptureScene();
        bCaptureDirty = false;
    }
}

void AFaceParallaxPreviewActor::AssignSkeletalMesh(USkeletalMesh* Mesh)
{
    if (PreviewMesh)
    {
        PreviewMesh->SetSkeletalMesh(Mesh);
    }
}

void AFaceParallaxPreviewActor::SetRenderTarget(UTextureRenderTarget2D* RenderTarget)
{
    if (SceneCapture && RenderTarget)
    {
        SceneCapture->TextureTarget = RenderTarget;
    }
}

void AFaceParallaxPreviewActor::SetOrbitYaw(float Degrees)
{
    OrbitYaw = FMath::Fmod(Degrees, 360.0f);
    MarkOrbitDirty();
}

void AFaceParallaxPreviewActor::SetOrbitPitch(float Degrees)
{
    OrbitPitch = FMath::Clamp(Degrees, -89.0f, 89.0f);
    MarkOrbitDirty();
}

void AFaceParallaxPreviewActor::SetOrbitDistance(float Distance)
{
    OrbitDistance = FMath::Max(10.0f, Distance);
    MarkOrbitDirty();
}

void AFaceParallaxPreviewActor::SetPreviewFOV(float FOV)
{
    PreviewFOV = FMath::Clamp(FOV, 1.0f, 160.0f);
    if (SceneCapture)
    {
        SceneCapture->FOVAngle = PreviewFOV;
    }
    MarkOrbitDirty();
}

void AFaceParallaxPreviewActor::ResetCamera()
{
    OrbitYaw = 0.0f;
    OrbitPitch = -10.0f;
    OrbitDistance = 220.0f;
    PreviewFOV = 30.0f;
    MarkOrbitDirty();
}

void AFaceParallaxPreviewActor::UpdateCaptureTransform()
{
    if (!SceneCapture) return;

    FRotator OrbitRotation(OrbitPitch, OrbitYaw, 0.0f);
    FVector OrbitDir = OrbitRotation.Vector();

    // Frame the head, not the actor origin — the mannequin root bone sits at the
    // feet, so orbiting the origin puts the legs in the middle of the preview.
    FVector Target = GetActorLocation() + FVector(0.0f, 0.0f, 160.0f);
    if (PreviewMesh)
    {
        FVector HeadLoc = PreviewMesh->GetSocketLocation(TEXT("head"));
        if (!HeadLoc.IsZero())
        {
            Target = HeadLoc;
        }
    }

    FVector CaptureLocation = Target - OrbitDir * OrbitDistance;
    FRotator CaptureRotation = UKismetMathLibrary::FindLookAtRotation(CaptureLocation, Target);

    SceneCapture->SetWorldLocation(CaptureLocation);
    SceneCapture->SetWorldRotation(CaptureRotation);
    SceneCapture->FOVAngle = PreviewFOV;
    bCaptureDirty = true;
}

void AFaceParallaxPreviewActor::ApplyPreset(UFaceParallaxPreset* Preset)
{
    if (FaceParallax && Preset)
    {
        FaceParallax->ApplyPreset(Preset);
    }
}

void AFaceParallaxPreviewActor::ShowDepthMesh(bool bVisible)
{
    if (DepthDebug)
    {
        DepthDebug->SetVisualizerEnabled(bVisible);
    }
}

void AFaceParallaxPreviewActor::ShowWireframe(bool bVisible)
{
    if (DepthDebug)
    {
        DepthDebug->SetWireframeEnabled(bVisible);
    }
}

void AFaceParallaxPreviewActor::ColorByDepth(bool bEnabled)
{
    if (!DepthDebug) return;
    if (bEnabled == bLastColorByDepth) return;
    bLastColorByDepth = bEnabled;
    DepthDebug->bUseVertexColors = bEnabled;
    UTexture2D* DepthTex = FaceParallax ? FaceParallax->GetCurrentDepthTexture() : nullptr;
    DepthDebug->RebuildMeshFromDepthMap(DepthTex);
}

void AFaceParallaxPreviewActor::ShowTextures(bool bVisible)
{
    if (PreviewMesh)
    {
        PreviewMesh->SetVisibility(bVisible, true);
    }
    if (DepthDebug)
    {
        DepthDebug->SetVisualizerEnabled(!bVisible);
    }
}

FFaceArtTransform AFaceParallaxPreviewActor::GetEffectivePartTransform(EFaceAngleState State, FName LayerTag) const
{
    if (!FaceParallax || !FaceParallax->ActivePreset) return FFaceArtTransform();
    const FFaceArtSlot& Slot = FaceParallax->ActivePreset->GetSlot(State, LayerTag);
    return Slot.GetEffectiveTransform(State);
}

FVector2D AFaceParallaxPreviewActor::GetPartSourceSize(EFaceAngleState State, FName LayerTag) const
{
    if (!FaceParallax || !FaceParallax->ActivePreset) return FVector2D::ZeroVector;
    const FFaceArtSlot& Slot = FaceParallax->ActivePreset->GetSlot(State, LayerTag);
    if (!Slot.Textures.IsValid() || !Slot.Textures.Albedo) return FVector2D::ZeroVector;
    int32 W = Slot.Textures.SourceTexWidth > 0 ? Slot.Textures.SourceTexWidth : Slot.Textures.Albedo->GetSizeX();
    int32 H = Slot.Textures.SourceTexHeight > 0 ? Slot.Textures.SourceTexHeight : Slot.Textures.Albedo->GetSizeY();
    return FVector2D((float)W, (float)H);
}

void AFaceParallaxPreviewActor::RefreshPreview()
{
    if (FaceParallax)
    {
        FaceParallax->ApplyCurrentStateTextures();
    }
}

#endif // WITH_EDITOR
