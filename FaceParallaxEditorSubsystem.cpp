#include "FaceParallaxEditorSubsystem.h"
#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxPreviewActor.h"
#include "FaceParallaxDataModel.h"
#include "FaceParallaxComponent.h"
#include "FaceParallaxTypes.h"

#include "GameFramework/Character.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "ToolMenuEntry.h"
#include "LevelEditor.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Blueprint/BlueprintSupport.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionRotator.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Blueprint/UserWidget.h"
#include "Materials/MaterialExpressionConstant4Vector.h"

#define LOCTEXT_NAMESPACE "FaceParallaxEditorSubsystem"

static const FName FaceParallaxToolbarName = "FaceParallax.Toolbar";

void UFaceParallaxEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    RegisterToolbar();
    UE_LOG(LogTemp, Log, TEXT("[FaceParallax] EditorSubsystem initialized."));
}

void UFaceParallaxEditorSubsystem::Deinitialize()
{
    UnregisterToolbar();
    Super::Deinitialize();
}

// =============================================================
// TOOLBAR
// =============================================================

void UFaceParallaxEditorSubsystem::RegisterToolbar()
{
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus) return;

    FToolMenuOwnerScoped OwnerScope(TEXT("FaceParallax"));

    UToolMenu* Toolbar = ToolMenus->ExtendMenu("LevelEditor.LevelEditorToolBar");
    if (!Toolbar) return;

    FToolMenuSection& Section = Toolbar->AddSection("FaceParallax", LOCTEXT("FaceParallaxSection", "Face Parallax"));

    FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
        "OpenFaceParallaxEditor",
        FUIAction(FExecuteAction::CreateUObject(this, &UFaceParallaxEditorSubsystem::OpenEditorWidget)),
        LOCTEXT("FaceEditor", "Face Editor"),
        LOCTEXT("FaceEditorTooltip", "Open Face Parallax Editor Widget"),
        FSlateIcon()
    );
    Entry.SetCommandList(nullptr);

    Section.AddEntry(Entry);

    ToolMenus->RefreshAllWidgets();
    UE_LOG(LogTemp, Log, TEXT("[FaceParallax] Toolbar registered."));
}

void UFaceParallaxEditorSubsystem::UnregisterToolbar()
{
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (ToolMenus)
    {
        ToolMenus->UnregisterOwnerByName(TEXT("FaceParallax"));
    }
}

void UFaceParallaxEditorSubsystem::OpenEditorWidget()
{
    // Load the widget blueprint class
    FString WidgetPath = TEXT("/Game/FaceParallax/Blueprints/WBP_FaceParallaxEditor.WBP_FaceParallaxEditor_C");
    UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, *WidgetPath);
    if (!WidgetClass)
    {
        // Fallback: try without _C suffix via StaticLoadObject
        FString AltPath = TEXT("/Game/FaceParallax/Blueprints/WBP_FaceParallaxEditor.WBP_FaceParallaxEditor");
        UObject* WidgetAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *AltPath);
        if (WidgetAsset && WidgetAsset->IsA<UBlueprint>())
            WidgetClass = Cast<UBlueprint>(WidgetAsset)->GeneratedClass;
        if (!WidgetClass)
        {
            UE_LOG(LogTemp, Error, TEXT("[FaceParallax] Could not load editor widget class"));
            return;
        }
    }

    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World) World = GWorld;
    if (!World) return;

    UUserWidget* Widget = CreateWidget<UUserWidget>(World, WidgetClass);
    if (!Widget) return;
    Widget->Initialize();

    UFaceParallaxEditorWidget* EditorWidget = Cast<UFaceParallaxEditorWidget>(Widget);
    if (EditorWidget)
    {
        EditorWidget->bSuppressValidation = true;

        if (!EditorWidget->DataModel)
        {
            UFaceParallaxDataModel* DM = NewObject<UFaceParallaxDataModel>();
            DM->InitializeDataModel();
            EditorWidget->DataModel = DM;
        }
        if (!EditorWidget->PreviewActor)
        {
            for (TActorIterator<AFaceParallaxPreviewActor> It(World); It; ++It)
            {
                EditorWidget->SetPreviewActor(*It);
                break;
            }
        }
        // Pre-populate ActivePreset from the preview actor's component
        if (EditorWidget->PreviewActor && EditorWidget->PreviewActor->FaceParallax)
        {
            if (!EditorWidget->ActivePreset)
                EditorWidget->ActivePreset = EditorWidget->PreviewActor->FaceParallax->ActivePreset;
        }
        if (EditorWidget->DataModel)
        {
            EditorWidget->DataModel->ActivePreset = EditorWidget->ActivePreset;
            EditorWidget->DataModel->PreviewActor = EditorWidget->PreviewActor;
        }

        EditorWidget->bSuppressValidation = false;
        EditorWidget->RefreshUI();
    }

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Face Parallax Editor")))
        .ClientSize(FVector2D(1200, 800))
        .SizingRule(ESizingRule::UserSized)
        .Content()
        [SNew(SBox)
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Fill)
            [Widget->TakeWidget()]];

    FSlateApplication::Get().AddWindow(Window);
}

void UFaceParallaxEditorSubsystem::FaceParallaxOpenEditor()
{
    OpenEditorWidget();
}

// =============================================================
// DEPLOY PIPELINE
// =============================================================

bool UFaceParallaxEditorSubsystem::RunDeploy(const FString& ContentRoot)
{
    UE_LOG(LogTemp, Log, TEXT("[FaceParallax] Running editor deploy pipeline..."));

    UMaterial* MasterMat = CreateMasterMaterial(ContentRoot / TEXT("Materials"));
    if (!MasterMat)
    {
        UE_LOG(LogTemp, Error, TEXT("[FaceParallax] Failed to create master material."));
        return false;
    }
    SetupMasterMaterialExpressions(MasterMat);

    TArray<FString> LayerNames = { TEXT("Eyes"), TEXT("Brows"), TEXT("Mouth"), TEXT("Hair") };
    for (const FString& LayerName : LayerNames)
    {
        UMaterialInstanceConstant* MI = CreateLayerMaterialInstance(ContentRoot / TEXT("Materials"), LayerName, MasterMat);
        if (!MI)
        {
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallax] Failed to create MI for layer %s"), *LayerName);
        }
        else
        {
            SetupMaterialInstanceParams(MI, LayerName, MasterMat);
        }
    }

    UFaceParallaxPreset* Preset = CreateDefaultPreset(ContentRoot / TEXT("Presets"), LayerNames);
    if (!Preset)
    {
        UE_LOG(LogTemp, Error, TEXT("[FaceParallax] Failed to create preset."));
        return false;
    }

    UBlueprint* BP = CreateParallaxBlueprint(ContentRoot / TEXT("Blueprints"), TEXT("/Game/FaceParallax/Meshes/SK_Face.SK_Face"));
    if (!BP)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FaceParallax] Failed to create character Blueprint."));
    }

    UE_LOG(LogTemp, Log, TEXT("[FaceParallax] Deploy pipeline complete."));
    return true;
}

// =============================================================
// MASTER MATERIAL
// =============================================================

UMaterial* UFaceParallaxEditorSubsystem::CreateMasterMaterial(const FString& PackagePath)
{
    FString FullPath = PackagePath / TEXT("M_FaceParallaxMaster");
    UPackage* Package = CreatePackage(*FullPath);
    if (!Package) return nullptr;

    UObject* Existing = StaticFindObject(UMaterial::StaticClass(), nullptr, *FullPath);
    if (Existing) return Cast<UMaterial>(Existing);

    auto Factory = NewObject<UMaterialFactoryNew>();
    UMaterial* Material = Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(), Package, FName("M_FaceParallaxMaster"),
        RF_Standalone | RF_Public, nullptr, GWarn));

    if (!Material) return nullptr;

    EMaterialShadingModel SM = MSM_Unlit;
    Material->SetShadingModel(SM);
    Material->TwoSided = true;
    Material->MarkPackageDirty();
    SaveAsset(Material);

    return Material;
}

void UFaceParallaxEditorSubsystem::SetupMasterMaterialExpressions(UMaterial* Material)
{
    if (!Material) return;

    UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);

    auto TexParam = [Material](const FString& Name, int32 X, int32 Y) -> UMaterialExpressionTextureSampleParameter2D*
    {
        auto* Node = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), X, Y);
        if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Node))
        {
            P->ParameterName = FName(*Name);
            return P;
        }
        return nullptr;
    };

    auto ScalarParam = [Material](const FString& Name, int32 X, int32 Y, float Default) -> UMaterialExpressionScalarParameter*
    {
        auto* Node = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), X, Y);
        if (auto* P = Cast<UMaterialExpressionScalarParameter>(Node))
        {
            P->ParameterName = FName(*Name);
            P->DefaultValue = Default;
            return P;
        }
        return nullptr;
    };

    auto VecParam = [Material](const FString& Name, int32 X, int32 Y) -> UMaterialExpressionVectorParameter*
    {
        auto* Node = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), X, Y);
        if (auto* P = Cast<UMaterialExpressionVectorParameter>(Node))
        {
            P->ParameterName = FName(*Name);
            return P;
        }
        return nullptr;
    };

    auto BoolParam = [Material](const FString& Name, int32 X, int32 Y, bool Default) -> UMaterialExpressionStaticBoolParameter*
    {
        auto* Node = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionStaticBoolParameter::StaticClass(), X, Y);
        if (auto* P = Cast<UMaterialExpressionStaticBoolParameter>(Node))
        {
            P->ParameterName = FName(*Name);
            P->DefaultValue = Default;
            return P;
        }
        return nullptr;
    };

    // --- Parameters ---
    UMaterialExpressionTextureSampleParameter2D* AlbedoCur   = TexParam("AlbedoTexture",     -600, -300);
    UMaterialExpressionTextureSampleParameter2D* AlbedoPrev  = TexParam("AlbedoTexturePrev",  -600, -150);
    UMaterialExpressionTextureSampleParameter2D* NormalCur   = TexParam("NormalTexture",      -600,    0);
    UMaterialExpressionTextureSampleParameter2D* NormalPrev  = TexParam("NormalTexturePrev",  -600,  150);
    UMaterialExpressionTextureSampleParameter2D* DepthCur    = TexParam("DepthTexture",       -600,  300);
    UMaterialExpressionTextureSampleParameter2D* DepthPrev   = TexParam("DepthTexturePrev",   -600,  450);
    UMaterialExpressionScalarParameter* BlendAlpha            = ScalarParam("StateBlendAlpha", -600,  600, 0.0f);
    UMaterialExpressionVectorParameter* ParallaxOff           = VecParam("ParallaxOffset",     -600,  750);
    UMaterialExpressionVectorParameter* ArtPos               = VecParam("ArtPosition",         -600,  900);
    UMaterialExpressionVectorParameter* ArtScale              = VecParam("ArtScale",            -600, 1050);
    UMaterialExpressionScalarParameter* ArtRot                = ScalarParam("ArtRotation",      -600, 1200, 0.0f);
    UMaterialExpressionScalarParameter* DepthInt              = ScalarParam("DepthIntensity",   -600, 1350, 1.0f);
    UMaterialExpressionStaticBoolParameter* DebugDepth        = BoolParam("DebugDepth",         -600, 1500, false);
    UMaterialExpressionStaticBoolParameter* IsTopDown         = BoolParam("IsTopDown",          -600, 1650, false);
    UMaterialExpressionStaticBoolParameter* IsTopView         = BoolParam("IsTopView",          -600, 1800, false);
    UMaterialExpressionVectorParameter* ArtPivot              = VecParam("ArtPivot",            -600, 1950);
    UMaterialExpressionScalarParameter* NestedFrame           = ScalarParam("NestedAnimFrame",  -600, 2100, 0.0f);
    UMaterialExpressionScalarParameter* ExprAlpha             = ScalarParam("ExpressionBlendAlpha", -600, 2250, 0.0f);
    UMaterialExpressionTextureSampleParameter2D* ExprAlbPrev  = TexParam("ExpressionAlbedoPrev",  -600, 2400);
    UMaterialExpressionTextureSampleParameter2D* ExprNrmPrev  = TexParam("ExpressionNormalPrev",  -600, 2550);
    UMaterialExpressionTextureSampleParameter2D* ExprDepPrev  = TexParam("ExpressionDepthPrev",   -600, 2700);
    // --- Swoosh & alt-texture parameters (created as material instance params; graph nodes for validation) ---
    UMaterialExpressionScalarParameter* ParamBlend            = ScalarParam("ParamBlendAlpha",     -600, 2850, 0.0f);
    UMaterialExpressionTextureSampleParameter2D* AltAlbedo    = TexParam("AltAlbedoTexture",      -600, 3000);
    UMaterialExpressionTextureSampleParameter2D* AltNormal    = TexParam("AltNormalTexture",      -600, 3150);
    UMaterialExpressionTextureSampleParameter2D* AltDepth     = TexParam("AltDepthTexture",       -600, 3300);
    UMaterialExpressionScalarParameter* SwooshBlend           = ScalarParam("SwooshLayerBlend",   -600, 3450, 0.0f);
    UMaterialExpressionScalarParameter* SwooshInt             = ScalarParam("SwooshIntensity",    -600, 3600, 0.0f);
    UMaterialExpressionScalarParameter* SwooshAngl            = ScalarParam("SwooshAngle",        -600, 3750, 0.0f);
    UMaterialExpressionScalarParameter* SwooshSiz             = ScalarParam("SwooshSize",         -600, 3900, 0.0f);
    UMaterialExpressionTextureSampleParameter2D* SwooshTex    = TexParam("SwooshTexture",        -600, 4050);

    // --- UV chain: TexCoord → Subtract(Pivot) → Add(ArtPos) → Multiply(ArtScale) → Add(Pivot) → Add(ParallaxOffset) → Rotate(ArtRot) → UVs ---
    UMaterialExpressionTextureCoordinate* TexCoord = Cast<UMaterialExpressionTextureCoordinate>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1200, -300));

    // UV - Pivot
    UMaterialExpressionSubtract* UVSubPivot = Cast<UMaterialExpressionSubtract>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionSubtract::StaticClass(), -1050, -300));
    UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), UVSubPivot, TEXT("A"));
    UMaterialEditingLibrary::ConnectMaterialExpressions(ArtPivot, TEXT(""), UVSubPivot, TEXT("B"));

    // (UV - Pivot) + ArtPos
    UMaterialExpressionAdd* UVAddPos = Cast<UMaterialExpressionAdd>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAdd::StaticClass(), -920, -300));
    UMaterialEditingLibrary::ConnectMaterialExpressions(UVSubPivot, TEXT(""), UVAddPos, TEXT("A"));
    UMaterialEditingLibrary::ConnectMaterialExpressions(ArtPos, TEXT(""), UVAddPos, TEXT("B"));

    // ((UV - Pivot) + ArtPos) * ArtScale
    UMaterialExpressionMultiply* UVMulScale = Cast<UMaterialExpressionMultiply>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -800, -300));
    UMaterialEditingLibrary::ConnectMaterialExpressions(UVAddPos, TEXT(""), UVMulScale, TEXT("A"));
    UMaterialEditingLibrary::ConnectMaterialExpressions(ArtScale, TEXT(""), UVMulScale, TEXT("B"));

    // ((UV - Pivot) + ArtPos) * ArtScale + Pivot
    UMaterialExpressionAdd* UVRePivot = Cast<UMaterialExpressionAdd>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAdd::StaticClass(), -680, -280));
    UMaterialEditingLibrary::ConnectMaterialExpressions(UVMulScale, TEXT(""), UVRePivot, TEXT("A"));
    UMaterialEditingLibrary::ConnectMaterialExpressions(ArtPivot, TEXT(""), UVRePivot, TEXT("B"));

    // (...) + ParallaxOffset
    UMaterialExpressionAdd* UVFinal = Cast<UMaterialExpressionAdd>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAdd::StaticClass(), -550, -280));
    UMaterialEditingLibrary::ConnectMaterialExpressions(UVRePivot, TEXT(""), UVFinal, TEXT("A"));
    UMaterialEditingLibrary::ConnectMaterialExpressions(ParallaxOff, TEXT(""), UVFinal, TEXT("B"));

    // Rotate UVs by ArtRotation
    UMaterialExpressionRotator* UVRotate = Cast<UMaterialExpressionRotator>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionRotator::StaticClass(), -400, -300));
    if (UVRotate)
    {
        UMaterialEditingLibrary::ConnectMaterialExpressions(UVFinal, TEXT(""), UVRotate, TEXT("Coordinate"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(ArtRot, TEXT(""), UVRotate, TEXT("Time"));
    }

    // Connect UV to all texture samplers
    UMaterialExpression* UVInputs[] = { AlbedoCur, AlbedoPrev, NormalCur, NormalPrev, DepthCur, DepthPrev,
                                        ExprAlbPrev, ExprNrmPrev, ExprDepPrev };
    UMaterialExpression* UVSource = UVRotate ? (UMaterialExpression*)UVRotate : (UMaterialExpression*)UVFinal;
    for (UMaterialExpression* Tex : UVInputs)
    {
        if (Tex) UMaterialEditingLibrary::ConnectMaterialExpressions(UVSource, TEXT(""), Tex, TEXT("UVs"));
    }

    // --- Crossfade current/prev per channel via StateBlendAlpha ---
    auto Lerp = [Material](UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* Alpha, int32 X, int32 Y) -> UMaterialExpressionLinearInterpolate*
    {
        auto* Node = Cast<UMaterialExpressionLinearInterpolate>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), X, Y));
        if (!Node) return nullptr;
        UMaterialEditingLibrary::ConnectMaterialExpressions(A, TEXT(""), Node, TEXT("A"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(B, TEXT(""), Node, TEXT("B"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(Alpha, TEXT(""), Node, TEXT("Alpha"));
        return Node;
    };

    UMaterialExpressionLinearInterpolate* AlbedoBlend = Lerp(AlbedoPrev, AlbedoCur, BlendAlpha, -300, -250);
    UMaterialExpressionLinearInterpolate* NormalBlend = Lerp(NormalPrev, NormalCur, BlendAlpha, -300, 50);
    UMaterialExpressionLinearInterpolate* DepthBlend  = Lerp(DepthPrev, DepthCur, BlendAlpha, -300, 350);

    // DepthScaled = DepthBlend * DepthIntensity
    UMaterialExpressionMultiply* DepthScaled = Cast<UMaterialExpressionMultiply>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -150, 350));
    UMaterialEditingLibrary::ConnectMaterialExpressions(DepthBlend, TEXT(""), DepthScaled, TEXT("A"));
    UMaterialEditingLibrary::ConnectMaterialExpressions(DepthInt, TEXT(""), DepthScaled, TEXT("B"));

    // --- Expression crossfade: blend toward expression prev textures via ExprAlpha ---
    UMaterialExpressionLinearInterpolate* ExprAlbedoBlend = Lerp(AlbedoBlend, ExprAlbPrev, ExprAlpha, -100, -200);
    UMaterialExpressionLinearInterpolate* ExprNormalBlend = Lerp(NormalBlend, ExprNrmPrev, ExprAlpha, -100, 100);
    UMaterialExpressionLinearInterpolate* ExprDepthBlend  = Lerp(DepthScaled, ExprDepPrev, ExprAlpha, -100, 400);

    // --- IsTopDown / IsTopView: select between standard and top-down depth output ---
    UMaterialExpressionStaticSwitch* TopDownSwitch = Cast<UMaterialExpressionStaticSwitch>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionStaticSwitch::StaticClass(), 100, 350));
    if (TopDownSwitch)
    {
        UMaterialEditingLibrary::ConnectMaterialExpressions(IsTopDown, TEXT(""), TopDownSwitch, TEXT("Value"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(ExprDepthBlend, TEXT(""), TopDownSwitch, TEXT("True"));
        // When not top-down, use depth pass-through; IsTopView can be wired similarly by callers
        UMaterialEditingLibrary::ConnectMaterialExpressions(ExprDepthBlend, TEXT(""), TopDownSwitch, TEXT("False"));
    }

    // --- NestedFrame: add frame-based UV offset (drives flipbook/animation frame select) ---
    UMaterialExpressionAdd* UVFrameOffset = Cast<UMaterialExpressionAdd>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAdd::StaticClass(), -550, -450));
    if (UVFrameOffset)
    {
        // UVFrameOffset = UVSource + float2(NestedFrame, 0) — caller can set up flipbook UV tiling in the instance
        UMaterialEditingLibrary::ConnectMaterialExpressions(UVSource, TEXT(""), UVFrameOffset, TEXT("A"));
        // Build a vector2 from NestedFrame scalar + 0
        UMaterialExpressionConstant* ZeroConst = Cast<UMaterialExpressionConstant>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -700, -450));
        UMaterialExpressionAppendVector* FrameVec = Cast<UMaterialExpressionAppendVector>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), -620, -450));
        if (ZeroConst && FrameVec)
        {
            ZeroConst->R = 0.0f;
            UMaterialEditingLibrary::ConnectMaterialExpressions(NestedFrame, TEXT(""), FrameVec, TEXT("A"));
            UMaterialEditingLibrary::ConnectMaterialExpressions(ZeroConst, TEXT(""), FrameVec, TEXT("B"));
            UMaterialEditingLibrary::ConnectMaterialExpressions(FrameVec, TEXT(""), UVFrameOffset, TEXT("B"));
        }
    }

    // DebugDepth switch: show depth in place of albedo when enabled
    UMaterialExpression* FinalAlbedo = ExprAlbedoBlend ? (UMaterialExpression*)ExprAlbedoBlend : (UMaterialExpression*)AlbedoBlend;
    UMaterialExpression* FinalDepth = TopDownSwitch ? (UMaterialExpression*)TopDownSwitch : (UMaterialExpression*)ExprDepthBlend;

    UMaterialExpressionStaticSwitch* DebugSwitch = Cast<UMaterialExpressionStaticSwitch>(
        UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionStaticSwitch::StaticClass(), 350, -100));
    UMaterialEditingLibrary::ConnectMaterialExpressions(DebugDepth, TEXT(""), DebugSwitch, TEXT("Value"));
    UMaterialEditingLibrary::ConnectMaterialExpressions(FinalDepth, TEXT(""), DebugSwitch, TEXT("True"));
    UMaterialEditingLibrary::ConnectMaterialExpressions(FinalAlbedo, TEXT(""), DebugSwitch, TEXT("False"));

    // Wire to material outputs
    UMaterialExpression* OutNormal = ExprNormalBlend ? (UMaterialExpression*)ExprNormalBlend : (UMaterialExpression*)NormalBlend;
    UMaterialEditingLibrary::ConnectMaterialProperty(DebugSwitch, TEXT(""), EMaterialProperty::MP_BaseColor);
    UMaterialEditingLibrary::ConnectMaterialProperty(OutNormal, TEXT(""), EMaterialProperty::MP_Normal);

    // Ensure material is flagged for skinned mesh use and force recompile
    Material->SetMaterialUsage(EMaterialUsage::MATUSAGE_SkeletalMesh);
    UMaterialEditingLibrary::RecompileMaterial(Material);
    Material->MarkPackageDirty();
}

// =============================================================
// MATERIAL INSTANCE
// =============================================================

UMaterialInstanceConstant* UFaceParallaxEditorSubsystem::CreateLayerMaterialInstance(
    const FString& PackagePath, const FString& LayerName, UMaterial* MasterMaterial)
{
    FString AssetName = TEXT("MI_Face_") + LayerName;
    FString FullPath = PackagePath / AssetName;

    UPackage* Package = CreatePackage(*FullPath);
    if (!Package) return nullptr;

    UObject* Existing = StaticFindObject(UMaterialInstanceConstant::StaticClass(), nullptr, *FullPath);
    if (Existing) return Cast<UMaterialInstanceConstant>(Existing);

    auto Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
    UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(
        UMaterialInstanceConstant::StaticClass(), Package, FName(*AssetName),
        RF_Standalone | RF_Public, nullptr, GWarn));

    if (!MI) return nullptr;

    MI->SetParentEditorOnly(MasterMaterial);
    MI->MarkPackageDirty();
    SaveAsset(MI);

    return MI;
}

void UFaceParallaxEditorSubsystem::SetupMaterialInstanceParams(
    UMaterialInstanceConstant* MI, const FString& LayerName, UMaterial* MasterMaterial)
{
    if (!MI) return;

    MI->SetParentEditorOnly(MasterMaterial);

    MI->SetScalarParameterValueEditorOnly(FName("StateBlendAlpha"), 1.0f);
    MI->SetScalarParameterValueEditorOnly(FName("DepthIntensity"), 1.0f);
    MI->SetScalarParameterValueEditorOnly(FName("ArtRotation"), 0.0f);
    MI->SetScalarParameterValueEditorOnly(FName("NestedAnimFrame"), 0.0f);
    MI->SetScalarParameterValueEditorOnly(FName("ExpressionBlendAlpha"), 0.0f);

    MI->SetVectorParameterValueEditorOnly(FName("ParallaxOffset"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
    MI->SetVectorParameterValueEditorOnly(FName("ArtPosition"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
    MI->SetVectorParameterValueEditorOnly(FName("ArtScale"), FLinearColor(1.0f, 1.0f, 0.0f, 0.0f));
    MI->SetVectorParameterValueEditorOnly(FName("ArtPivot"), FLinearColor(0.5f, 0.5f, 0.0f, 0.0f));

    MI->SetStaticSwitchParameterValueEditorOnly(FName("DebugDepth"), false);
    MI->SetStaticSwitchParameterValueEditorOnly(FName("IsTopDown"), false);
    MI->SetStaticSwitchParameterValueEditorOnly(FName("IsTopView"), false);

    MI->SetTextureParameterValueEditorOnly(FName("AlbedoTexture"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("AlbedoTexturePrev"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("NormalTexture"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("NormalTexturePrev"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("DepthTexture"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("DepthTexturePrev"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("ExpressionAlbedoPrev"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("ExpressionNormalPrev"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("ExpressionDepthPrev"), nullptr);
    // Swoosh & alt-texture parameters
    MI->SetScalarParameterValueEditorOnly(FName("ParamBlendAlpha"), 0.0f);
    MI->SetScalarParameterValueEditorOnly(FName("SwooshLayerBlend"), 0.0f);
    MI->SetScalarParameterValueEditorOnly(FName("SwooshIntensity"), 0.0f);
    MI->SetScalarParameterValueEditorOnly(FName("SwooshAngle"), 0.0f);
    MI->SetScalarParameterValueEditorOnly(FName("SwooshSize"), 0.0f);
    MI->SetTextureParameterValueEditorOnly(FName("AltAlbedoTexture"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("AltNormalTexture"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("AltDepthTexture"), nullptr);
    MI->SetTextureParameterValueEditorOnly(FName("SwooshTexture"), nullptr);

    MI->MarkPackageDirty();
}

// =============================================================
// PRESET DATA ASSET
// =============================================================

UFaceParallaxPreset* UFaceParallaxEditorSubsystem::CreateDefaultPreset(
    const FString& PackagePath, const TArray<FString>& LayerNames)
{
    FString AssetName = TEXT("DA_FaceParallaxPreset");
    FString FullPath = PackagePath / AssetName;

    UPackage* Package = CreatePackage(*FullPath);
    if (!Package) return nullptr;

    UObject* Existing = StaticFindObject(UFaceParallaxPreset::StaticClass(), nullptr, *FullPath);
    UFaceParallaxPreset* Preset = nullptr;
    if (Existing)
    {
        Preset = Cast<UFaceParallaxPreset>(Existing);
        // Always re-populate existing presets to ensure valid ViewAssignments
        Preset->PopulateDefaultAssignments(LayerNames);
        Preset->CanvasSize = FIntPoint(2048, 2048);
        SaveAsset(Preset);
        return Preset;
    }

    Preset = NewObject<UFaceParallaxPreset>(Package, FName(*AssetName), RF_Standalone | RF_Public);
    if (!Preset) return nullptr;

    Preset->CanvasSize = FIntPoint(2048, 2048);
    Preset->PopulateDefaultAssignments(LayerNames);
    SaveAsset(Preset);

    return Preset;
}

// =============================================================
// BLUEPRINT
// =============================================================

UBlueprint* UFaceParallaxEditorSubsystem::CreateParallaxBlueprint(
    const FString& PackagePath, const FString& SkeletalMeshPath)
{
    FString AssetName = TEXT("BP_FaceParallaxCharacter");
    FString FullPath = PackagePath / AssetName;

    UPackage* Package = CreatePackage(*FullPath);
    if (!Package) return nullptr;

    UObject* Existing = StaticFindObject(UBlueprint::StaticClass(), nullptr, *FullPath);
    if (Existing) return Cast<UBlueprint>(Existing);

    UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
        ACharacter::StaticClass(), Package, FName(*AssetName),
        BPTYPE_Normal, UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass());

    if (!BP) return nullptr;

    FKismetEditorUtilities::CompileBlueprint(BP);

    BP->MarkPackageDirty();
    SaveAsset(BP);

    return BP;
}

// =============================================================
// HELPERS
// =============================================================

FString UFaceParallaxEditorSubsystem::ProjectContentDir() const
{
    return FPaths::ProjectContentDir();
}

bool UFaceParallaxEditorSubsystem::SaveAsset(UObject* Asset)
{
    if (!Asset) return false;

    UPackage* Package = Asset->GetPackage();
    if (!Package) return false;

    FString PackageFileName = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());

    Package->FullyLoad();

    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Standalone | RF_Public;
    Args.SaveFlags = SAVE_NoError;
    Args.bWarnOfLongFilename = false;

    return UPackage::SavePackage(Package, Asset, *PackageFileName, Args);
}



#undef LOCTEXT_NAMESPACE
