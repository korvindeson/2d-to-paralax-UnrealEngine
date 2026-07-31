#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "FaceParallaxPreset.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInstanceConstant.h"
#include "FaceParallaxEditorSubsystem.generated.h"

struct IConsoleCommand;

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

    // (removed: EnsureLayerDefStruct was a stub that always returned nullptr)

    // --- Material Graph (single source of truth for material creation) ---
    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    void SetupMasterMaterialExpressions(class UMaterial* Material);

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    void SetupMaterialInstanceParams(class UMaterialInstanceConstant* MI, const FString& LayerName, class UMaterial* MasterMaterial);

    // --- Toolbar ---
    void RegisterToolbar();
    void UnregisterToolbar();

    UFUNCTION(BlueprintCallable, Category = "Face Parallax|Editor Subsystem")
    void OpenEditorWidget();

    UFUNCTION(Exec, Category = "Face Parallax|Editor Subsystem")
    void FaceParallaxOpenEditor();

    // Keeps the editor widget instance alive for the subsystem's lifetime —
    // without this, GC can collect it while the tab still renders (dead input).
    UPROPERTY(Transient)
    TObjectPtr<class UFaceParallaxEditorWidget> EditorWidgetInstance;

private:
    FString ProjectContentDir() const;
    bool SaveAsset(class UObject* Asset);
    TSharedRef<class SDockTab> SpawnEditorTab(const class FSpawnTabArgs& Args);
    FDelegateHandle ToolbarExtenderHandle;
    bool bTabSpawnerRegistered = false;
    // Console command registered in Initialize() so typing 'FaceParallaxOpenEditor'
    // always dispatches — UFUNCTION(Exec) bindings can go stale after Live Coding.
    struct IConsoleCommand* ConsoleCommand = nullptr;
};
