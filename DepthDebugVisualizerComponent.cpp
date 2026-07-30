#include "DepthDebugVisualizerComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDepthDebugVisualizerComponent::UDepthDebugVisualizerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bIsEnabled = false;
    CurrentVertexCount = 0;
    bNeedsRebuild = false;
    DebugMesh = nullptr;
    DebugMaterialInstance = nullptr;
    LastDepthTexture = nullptr;
    bAddressModeSaved = false;
}

void UDepthDebugVisualizerComponent::BeginPlay()
{
    Super::BeginPlay();

    CreateProceduralMeshComponent();

    if (bStartEnabled)
    {
        SetVisualizerEnabled(true);
    }
}

void UDepthDebugVisualizerComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bNeedsRebuild && DebugMesh && bIsEnabled)
    {
        BuildDebugMesh();
        bNeedsRebuild = false;
    }
}

void UDepthDebugVisualizerComponent::CreateProceduralMeshComponent()
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    DebugMesh = NewObject<UProceduralMeshComponent>(Owner, TEXT("DepthDebugMesh"));
    if (DebugMesh)
    {
        DebugMesh->RegisterComponent();
        Owner->AddInstanceComponent(DebugMesh);
        DebugMesh->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        DebugMesh->SetRelativeLocation(LocalOffset);
        DebugMesh->SetVisibility(bIsEnabled);
    }
}

void UDepthDebugVisualizerComponent::ToggleVisualizer()
{
    SetVisualizerEnabled(!bIsEnabled);
}

void UDepthDebugVisualizerComponent::SetVisualizerEnabled(bool bEnabled)
{
    bIsEnabled = bEnabled;

    if (DebugMesh)
    {
        DebugMesh->SetVisibility(bIsEnabled);
        DebugMesh->SetHiddenInGame(!bIsEnabled);

        if (bIsEnabled)
        {
            bNeedsRebuild = true;
        }
    }

    OnStateChanged();
}

void UDepthDebugVisualizerComponent::RebuildMeshFromDepthMap(UTexture2D* DepthMapTexture)
{
    LastDepthTexture = DepthMapTexture;

    if (!DepthMapTexture)
    {
        UE_LOG(LogTemp, Warning, TEXT("[UDepthDebugVisualizerComponent] RebuildMeshFromDepthMap called with null texture."));
        return;
    }

    TArray<float> Samples;
    int32 SampleWidth = 0;
    int32 SampleHeight = 0;

    SampleDepthMap(DepthMapTexture, Samples, SampleWidth, SampleHeight);

    if (Samples.Num() == 0 || SampleWidth == 0 || SampleHeight == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[UDepthDebugVisualizerComponent] Failed to sample depth map texture."));
        return;
    }

    CachedDepthSamples = Samples;

    if (bIsEnabled && DebugMesh)
    {
        BuildDebugMesh();
        UpdateWireframeMode();
    }
    else
    {
        bNeedsRebuild = true;
    }
}

void UDepthDebugVisualizerComponent::SampleDepthMap(UTexture2D* Texture,
    TArray<float>& OutSamples, int32& OutSampleWidth, int32& OutSampleHeight)
{
    OutSamples.Empty();
    OutSampleWidth = 0;
    OutSampleHeight = 0;

    if (!Texture || !Texture->GetResource())
    {
        UE_LOG(LogTemp, Warning, TEXT("[UDepthDebugVisualizerComponent] Texture is null or has no resource."));
        return;
    }

    // Validate texture compression — raw pixel reads require uncompressed formats
    TextureCompressionSettings Compression = Texture->CompressionSettings;
    if (Compression != TC_EditorIcon && Compression != TC_VectorDisplacementmap && Compression != TC_HDR)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[UDepthDebugVisualizerComponent] Texture '%s' has compression %d which may not support raw pixel reads. Use TC_EditorIcon, TC_VectorDisplacementmap, TC_HDR, or TC_HDR_Float."),
            *Texture->GetName(), (int32)Compression);
    }

    // Save original address modes and set to Clamp for safe sampling
    bAddressModeSaved = true;
    OriginalAddressX = (uint8)Texture->AddressX;
    OriginalAddressY = (uint8)Texture->AddressY;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;

    int32 TexWidth = Texture->GetSizeX();
    int32 TexHeight = Texture->GetSizeY();

    if (TexWidth == 0 || TexHeight == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[UDepthDebugVisualizerComponent] Texture has zero dimensions."));
        return;
    }

    int32 Res = FMath::Clamp(GridResolution, 8, 256);
    OutSampleWidth = Res;
    OutSampleHeight = Res;
    OutSamples.Reserve(Res * Res);

    int32 SourceWidth = Texture->Source.GetSizeX();
    int32 SourceHeight = Texture->Source.GetSizeY();

    TArray64<uint8> SourceData;
    if (!Texture->Source.GetMipData(SourceData, 0))
    {
        return;
    }

    int32 BytesPerPixel = Texture->Source.GetBytesPerPixel();
    if (BytesPerPixel == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[UDepthDebugVisualizerComponent] Compressed texture format — cannot read source data directly. Reimport with uncompressed format."));
        return;
    }

    for (int32 Gy = 0; Gy < Res; ++Gy)
    {
        for (int32 Gx = 0; Gx < Res; ++Gx)
        {
            int32 TexX = FMath::Clamp((Gx * SourceWidth) / Res, 0, SourceWidth - 1);
            int32 TexY = FMath::Clamp((Gy * SourceHeight) / Res, 0, SourceHeight - 1);

            int32 SrcIdx = (TexY * SourceWidth + TexX) * BytesPerPixel;
            float DepthValue = 0.0f;

            if (BytesPerPixel >= 4)
            {
                DepthValue = *(const float*)&SourceData[SrcIdx];
            }
            else if (BytesPerPixel == 2)
            {
                DepthValue = *(const uint16*)&SourceData[SrcIdx] / 65535.0f;
            }
            else
            {
                DepthValue = SourceData[SrcIdx] / 255.0f;
            }

            OutSamples.Add(FMath::Clamp(DepthValue, 0.0f, 1.0f));
        }
    }

    // Restore original address modes
    if (bAddressModeSaved)
    {
        Texture->AddressX = (TextureAddress)OriginalAddressX;
        Texture->AddressY = (TextureAddress)OriginalAddressY;
        bAddressModeSaved = false;
    }
}

void UDepthDebugVisualizerComponent::BuildDebugMesh()
{
    if (!DebugMesh || CachedDepthSamples.Num() == 0) return;

    int32 Res = FMath::Clamp(GridResolution, 8, 256);
    if (CachedDepthSamples.Num() < Res * Res) return;

    UProceduralMeshComponent* PMC = Cast<UProceduralMeshComponent>(DebugMesh);
    if (!PMC) return;

    PMC->ClearAllMeshSections();

    int32 VertexCount = Res * Res;
    int32 TriangleCount = (Res - 1) * (Res - 1) * 2;

    TArray<FVector> Vertices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> VertexColors;
    TArray<int32> Triangles;
    TArray<FProcMeshTangent> Tangents;

    Vertices.Reserve(VertexCount);
    Normals.Reserve(VertexCount);
    UVs.Reserve(VertexCount);
    VertexColors.Reserve(VertexCount);
    Triangles.Reserve(TriangleCount * 3);

    float EffectiveMeshSize = (ProfileHalfWidth > 0.0f) ? (2.0f * ProfileHalfWidth) : MeshSize;
    float EffectiveHeightScale = (ProfileHalfDepth > 0.0f) ? ProfileHalfDepth : HeightScale;

    for (int32 Gy = 0; Gy < Res; ++Gy)
    {
        for (int32 Gx = 0; Gx < Res; ++Gx)
        {
            float U = (float)Gx / (float)(Res - 1);
            float V = (float)Gy / (float)(Res - 1);

            float X = (U - 0.5f) * EffectiveMeshSize;
            float Y = (V - 0.5f) * EffectiveMeshSize;

            int32 SampleIdx = Gy * Res + Gx;
            float Depth = CachedDepthSamples[SampleIdx];
            float Z = Depth * EffectiveHeightScale;

            Vertices.Add(FVector(X, Y, Z));
            UVs.Add(FVector2D(U, V));

            if (bUseVertexColors)
            {
                VertexColors.Add(DepthToColor(Depth).ToFColor(true));
            }
            else
            {
                VertexColors.Add(FColor::White);
            }
        }
    }

    for (int32 Gy = 0; Gy < Res - 1; ++Gy)
    {
        for (int32 Gx = 0; Gx < Res - 1; ++Gx)
        {
            int32 Idx00 = Gy * Res + Gx;
            int32 Idx10 = Gy * Res + (Gx + 1);
            int32 Idx01 = (Gy + 1) * Res + Gx;
            int32 Idx11 = (Gy + 1) * Res + (Gx + 1);

            Triangles.Add(Idx00);
            Triangles.Add(Idx10);
            Triangles.Add(Idx01);

            Triangles.Add(Idx10);
            Triangles.Add(Idx11);
            Triangles.Add(Idx01);
        }
    }

    Normals.Init(FVector(0.0f, 0.0f, 1.0f), VertexCount);
    Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), VertexCount);

    PMC->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false);

    if (DepthDebugMaterial)
    {
        if (!DebugMaterialInstance)
        {
            DebugMaterialInstance = UMaterialInstanceDynamic::Create(DepthDebugMaterial, this);
        }
        DebugMesh->SetMaterial(0, DebugMaterialInstance);
    }

    CurrentVertexCount = VertexCount;
    UpdateWireframeMode();
    UE_LOG(LogTemp, Verbose, TEXT("[UDepthDebugVisualizerComponent] Mesh rebuilt: %d vertices, %d triangles."),
        VertexCount, Triangles.Num() / 3);
}

FLinearColor UDepthDebugVisualizerComponent::DepthToColor(float Depth) const
{
    float T = FMath::Clamp(Depth, 0.0f, 1.0f);
    FLinearColor Color;
    Color.R = FMath::Lerp(LowColor.R, HighColor.R, T);
    Color.G = FMath::Lerp(LowColor.G, HighColor.G, T);
    Color.B = FMath::Lerp(LowColor.B, HighColor.B, T);
    Color.A = 1.0f;
    return Color;
}

void UDepthDebugVisualizerComponent::UpdateWireframeMode()
{
    if (!DebugMesh) return;

    if (bShowWireframe && WireframeMaterial)
    {
        DebugMesh->SetMaterial(0, WireframeMaterial);
    }
    else if (DebugMaterialInstance)
    {
        DebugMesh->SetMaterial(0, DebugMaterialInstance);
    }
    else if (DepthDebugMaterial)
    {
        DebugMesh->SetMaterial(0, DepthDebugMaterial);
    }
}

void UDepthDebugVisualizerComponent::OnStateChanged()
{
    if (bIsEnabled && LastDepthTexture)
    {
        RebuildMeshFromDepthMap(LastDepthTexture);
    }
}
