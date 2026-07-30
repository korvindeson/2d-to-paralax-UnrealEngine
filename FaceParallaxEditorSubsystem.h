#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "FaceParallaxPreset.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInstanceConstant.h"
#include "FaceParallaxEditorSubsystem.generated.h"

UCLASS()
class UFaceParallaxEditorSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --- Deploy pipeline ---
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    bool RunDeploy(const FString& ContentRoot = TEXT("/Game/FaceParallax"));

    // --- Individual asset creators ---
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    class UMaterial* CreateMasterMaterial(const FString& PackagePath);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    class UMaterialInstanceConstant* CreateLayerMaterialInstance(const FString& PackagePath, const FString& LayerName, class UMaterial* MasterMaterial);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    class UFaceParallaxPreset* CreateDefaultPreset(const FString& PackagePath, const TArray<FString>& LayerNames);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    class UBlueprint* CreateParallaxBlueprint(const FString& PackagePath, const FString& SkeletalMeshPath);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    class UUserDefinedStruct* EnsureLayerDefStruct(const FString& PackagePath);

    // --- Material Instance editing ---
    void SetupMaterialInstanceParams(class UMaterialInstanceConstant* MI, const FString& LayerName, class UMaterial* MasterMaterial);

    // --- Toolbar ---
    void RegisterToolbar();
    void UnregisterToolbar();

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    void OpenEditorWidget();

private:
    void SetupMasterMaterialExpressions(class UMaterial* Material);
    FString ProjectContentDir() const;
    bool SaveAsset(class UObject* Asset);
    class UObject* LoadOrCreateAsset(const FString& PackagePath, const FString& AssetName, UClass* AssetClass, UFactory* Factory);
    void FocusOnAsset(const FString& PackagePath);

    FDelegateHandle ToolbarExtenderHandle;
};
