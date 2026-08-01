#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "FaceParallaxPreset.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInstanceConstant.h"
#include "FaceParallaxEditorSubsystem.generated.h"

struct IConsoleCommand;

UCLASS(Config = Editor)
class UFaceParallaxEditorSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Note: asset deployment (materials, preset, character BP, editor widget BP,
    // render target, preview actor) is handled entirely by the deployment script
    // deploy.py — see the repo root. There is no C++ deploy pipeline.

    // Auto-opens the editor as a docked tab once after the editor loads.
    // Default ON; set to false to disable, e.g. in DefaultEditor.ini:
    //   [/Script/FaceParallaxEditor.FaceParallaxEditorSubsystem]
    //   bAutoOpenEditorOnStartup=false
    UPROPERTY(Config, EditAnywhere, Category = "Face Parallax|Editor Subsystem")
    bool bAutoOpenEditorOnStartup = true;

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
    TSharedRef<class SDockTab> SpawnEditorTab(const class FSpawnTabArgs& Args);
    FDelegateHandle ToolbarExtenderHandle;
    bool bTabSpawnerRegistered = false;
    // Console command registered in Initialize() so typing 'FaceParallaxOpenEditor'
    // always dispatches — UFUNCTION(Exec) bindings can go stale after Live Coding.
    struct IConsoleCommand* ConsoleCommand = nullptr;
    // Auto-open on startup: binds GEditor->OnEditorLoaded() (fires when the editor
    // has finished loading), then defers the tab invoke by one ticker delay so the
    // main editor frame is guaranteed to exist before TryInvokeTab.
    void ScheduleAutoOpen();
    void AutoOpenEditorLoaded(double Duration);
    FDelegateHandle EditorLoadedHandle;
};
