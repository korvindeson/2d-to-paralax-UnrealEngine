#include "FaceParallaxEditorSubsystem.h"
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
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

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
    FString WidgetPath = TEXT("/Game/FaceParallax/Blueprints/WBP_FaceParallaxEditor.WBP_FaceParallaxEditor");
    UObject* WidgetAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *WidgetPath);
    if (WidgetAsset)
    {
        FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
        TArray<UObject*> Assets = { WidgetAsset };
        CBModule.Get().SyncBrowserToAssets(Assets);
    }
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

    TArray<FString> LayerNames = { TEXT("Eyes"), TEXT("Brows"), TEXT("Mouth"), TEXT("Hair") };
    for (const FString& LayerName : LayerNames)
    {
        UMaterialInstanceConstant* MI = CreateLayerMaterialInstance(ContentRoot / TEXT("Materials"), LayerName, MasterMat);
        if (!MI)
        {
            UE_LOG(LogTemp, Warning, TEXT("[FaceParallax] Failed to create MI for layer %s"), *LayerName);
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
    if (Existing) return Cast<UFaceParallaxPreset>(Existing);

    UFaceParallaxPreset* Preset = NewObject<UFaceParallaxPreset>(Package, FName(*AssetName), RF_Standalone | RF_Public);

    if (!Preset) return nullptr;

    Preset->CanvasSize = FIntPoint(2048, 2048);

    TArray<EFaceAngleState> AllStates = {
        EFaceAngleState::Front, EFaceAngleState::ThreeQuarterRight,
        EFaceAngleState::RightProfile, EFaceAngleState::BackRight,
        EFaceAngleState::BackLeft, EFaceAngleState::LeftProfile,
        EFaceAngleState::ThreeQuarterLeft, EFaceAngleState::Back,
        EFaceAngleState::Top, EFaceAngleState::Bottom
    };

    for (EFaceAngleState State : AllStates)
    {
        FFaceViewStateLayerSet& StateSet = Preset->ViewAssignments.FindOrAdd(State);
        for (const FString& LayerName : LayerNames)
        {
            FName LayerTag = FName(*LayerName);
            FFaceArtSlot Slot;
            Slot.CanonicalTransform.Position = FVector2D(0.0f, 0.0f);
            Slot.CanonicalTransform.Scale = FVector2D(1.0f, 1.0f);
            Slot.CanonicalTransform.Rotation = 0.0f;
            Slot.Textures.SyncSoftRefs();
            StateSet.Layers.Add(LayerTag, Slot);
        }
    }

    Preset->MarkPackageDirty();
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

UObject* UFaceParallaxEditorSubsystem::LoadOrCreateAsset(
    const FString& PackagePath, const FString& AssetName,
    UClass* AssetClass, UFactory* Factory)
{
    FString FullPath = PackagePath / AssetName;
    UObject* Existing = StaticFindObject(AssetClass, nullptr, *FullPath);
    if (Existing) return Existing;

    UPackage* Package = CreatePackage(*FullPath);
    if (!Package) return nullptr;

    return Factory->FactoryCreateNew(AssetClass, Package, FName(*AssetName),
        RF_Standalone | RF_Public, nullptr, GWarn);
}

UUserDefinedStruct* UFaceParallaxEditorSubsystem::EnsureLayerDefStruct(const FString& PackagePath)
{
    return nullptr;
}

void UFaceParallaxEditorSubsystem::FocusOnAsset(const FString& PackagePath)
{
    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *PackagePath);
    if (Asset)
    {
        FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
        TArray<UObject*> Assets = { Asset };
        CBModule.Get().SyncBrowserToAssets(Assets);
    }
}

#undef LOCTEXT_NAMESPACE
