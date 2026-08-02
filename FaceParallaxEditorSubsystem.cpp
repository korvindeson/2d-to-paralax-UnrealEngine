#include "FaceParallaxEditorSubsystem.h"
#include "FaceParallaxEditorWidget.h"
#include "FaceParallaxPreviewActor.h"
#include "FaceParallaxComponent.h"
#include "FaceParallaxTypes.h"

#include "Modules/ModuleManager.h"

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
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "EditorAssetLibrary.h"
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
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FaceParallaxEditorSubsystem"

static const FName FaceParallaxToolbarName = "FaceParallax.Toolbar";
static const FName FaceParallaxEditorTabName = "FaceParallaxEditor";

// Once per process: after a Live Coding patch re-initializes the subsystem the
// tab must not be forced open again (the user may have closed it deliberately).
static bool bFaceParallaxAutoOpenedThisProcess = false;
static FTSTicker::FDelegateHandle AutoOpenTickerHandle;

void UFaceParallaxEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Apply DefaultEditor.ini overrides for Config properties (e.g. the
    // bAutoOpenEditorOnStartup toggle) before scheduling auto-open.
    LoadConfig();
    RegisterToolbar();
    ScheduleAutoOpen();

    WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(
        this, &UFaceParallaxEditorSubsystem::HandleWorldCleanup);

    // Register a real console command (in addition to the UFUNCTION(Exec)) so
    // typing 'FaceParallaxOpenEditor' always dispatches. UFUNCTION(Exec) bindings
    // can silently go stale after Live Coding patches the module layout, which
    // produces "command accepted, nothing happens".
    if (!ConsoleCommand)
    {
        ConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("FaceParallaxOpenEditor"),
            TEXT("Open the Face Parallax editor as a docked tab in the main editor window"),
            FConsoleCommandDelegate::CreateLambda([this]() { OpenEditorWidget(); }),
            ECVF_Default);
    }
    UE_LOG(LogTemp, Log, TEXT("[FaceParallax] EditorSubsystem initialized — DOCKED-TAB BUILD v3 (marker 0xV3)"));
}

void UFaceParallaxEditorSubsystem::Deinitialize()
{
    if (AutoOpenTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(AutoOpenTickerHandle);
        AutoOpenTickerHandle.Reset();
    }
    if (EditorLoadedHandle.IsValid())
    {
        FEditorDelegates::OnEditorInitialized.Remove(EditorLoadedHandle);
        EditorLoadedHandle.Reset();
    }
    if (WorldCleanupHandle.IsValid())
    {
        FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
        WorldCleanupHandle.Reset();
    }
    if (bTabSpawnerRegistered)
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FaceParallaxEditorTabName);
        bTabSpawnerRegistered = false;
    }
    if (ConsoleCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(ConsoleCommand, false);
        ConsoleCommand = nullptr;
    }
    UnregisterToolbar();
    Super::Deinitialize();
}

// =============================================================
// WORLD-BOUND TAB LIFECYCLE
// =============================================================

void UFaceParallaxEditorSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
    if (!EditorWidgetInstance) return;
    UWorld* WidgetWorld = EditorWidgetInstance->GetWorld();
    if (WidgetWorld && World != WidgetWorld) return;   // e.g. PIE worlds: keep the tab
    // The editor world hosting the widget + preview actor is being discarded
    // (level switch, compile). Drop the strong ref and close the tab so the
    // old world's package can be GC'd — otherwise the engine fatals with
    // "World Memory Leaks".
    EditorWidgetInstance = nullptr;
    if (DockedTab.IsValid())
    {
        TSharedRef<SDockTab> TabRef = DockedTab.ToSharedRef();
        DockedTab.Reset();
        TabRef->RequestCloseTab();
    }
}

// =============================================================
// TOOLBAR
// =============================================================

void UFaceParallaxEditorSubsystem::RegisterToolbar()
{
    UToolMenus* ToolMenus = UToolMenus::Get();
    if (!ToolMenus) return;

    FToolMenuOwnerScoped OwnerScope(TEXT("FaceParallax"));

    // Level editor toolbar. The section is inserted BEFORE "Play" so the button
    // stays visible instead of being appended into the toolbar's overflow chevron.
    UToolMenu* Toolbar = ToolMenus->ExtendMenu("LevelEditor.LevelEditorToolBar");
    if (Toolbar)
    {
        FToolMenuSection& Section = Toolbar->AddSection("FaceParallax",
            LOCTEXT("FaceParallaxSection", "Face Parallax"),
            FToolMenuInsert("Play", EToolMenuInsertType::Before));

        FToolMenuEntry Entry = FToolMenuEntry::InitToolBarButton(
            "OpenFaceParallaxEditor",
            FUIAction(FExecuteAction::CreateUObject(this, &UFaceParallaxEditorSubsystem::OpenEditorWidget)),
            LOCTEXT("FaceEditor", "Face Editor"),
            LOCTEXT("FaceEditorTooltip", "Open Face Parallax Editor Widget"),
            FSlateIcon()
        );
        Entry.SetCommandList(nullptr);

        Section.AddEntry(Entry);
    }

    // Window menu — guaranteed-visible path (the same menu the engine itself
    // extends), so the editor is always one click away.
    UToolMenu* WindowMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Window");
    if (WindowMenu)
    {
        FToolMenuSection& WinSection = WindowMenu->AddSection("FaceParallax",
            LOCTEXT("FaceParallaxWindowSection", "Face Parallax"));
        WinSection.AddMenuEntry(
            "OpenFaceParallaxEditorWindow",
            LOCTEXT("OpenFaceParallaxEditorWindow", "Face Parallax Editor"),
            LOCTEXT("OpenFaceParallaxEditorWindowTooltip", "Open Face Parallax Editor Widget"),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateUObject(this, &UFaceParallaxEditorSubsystem::OpenEditorWidget))
        );
    }

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
    // Headless/commandlet contexts have no Slate application — TryInvokeTab
    // would assert. In the interactive editor this is always initialized.
    if (!FSlateApplication::IsInitialized())
    {
        UE_LOG(LogTemp, Warning, TEXT("[FaceParallax] OpenEditorWidget — no Slate application (headless); skipping tab invoke"));
        return;
    }
    // Host the editor as a dockable tab inside the main editor window — standalone
    // SWindows don't reliably receive input in the editor, dock tabs always do.
    UE_LOG(LogTemp, Log, TEXT("[FaceParallax] OpenEditorWidget — invoking nomad tab 'FaceParallaxEditor'"));
    if (!bTabSpawnerRegistered)
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            FaceParallaxEditorTabName,
            FOnSpawnTab::CreateUObject(this, &UFaceParallaxEditorSubsystem::SpawnEditorTab))
            .SetDisplayName(FText::FromString(TEXT("Face Parallax Editor")))
            .SetTooltipText(FText::FromString(TEXT("Face Parallax Editor")));
        bTabSpawnerRegistered = true;
    }
    FGlobalTabmanager::Get()->TryInvokeTab(FaceParallaxEditorTabName);
}

TSharedRef<SDockTab> UFaceParallaxEditorSubsystem::SpawnEditorTab(const FSpawnTabArgs& Args)
{
    if (!EditorWidgetInstance)
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
        }

        UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!World) World = GWorld;

        if (WidgetClass && World)
        {
            EditorWidgetInstance = CreateWidget<UFaceParallaxEditorWidget>(World, WidgetClass);
            if (EditorWidgetInstance)
            {
                EditorWidgetInstance->Initialize();
                EditorWidgetInstance->bSuppressValidation = true;
                UE_LOG(LogTemp, Log, TEXT("[FaceParallax] SpawnEditorTab — widget instance created (%s)"),
                    *WidgetClass->GetName());

                if (!EditorWidgetInstance->PreviewActor.IsValid())
                {
                    for (TActorIterator<AFaceParallaxPreviewActor> It(World); It; ++It)
                    {
                        EditorWidgetInstance->SetPreviewActor(*It);
                        UE_LOG(LogTemp, Log, TEXT("[FaceParallax] SpawnEditorTab — discovered preview actor '%s'"),
                            *(*It)->GetActorLabel());
                        break;
                    }
                    if (!EditorWidgetInstance->PreviewActor.IsValid())
                    {
                        // Auto-provision: with no preview actor in the level the
                        // editor is unusable (no layers, no canvas). Spawn one so
                        // the user can start adding art immediately; the actor
                        // combo still lists it for re-selection later.
                        AFaceParallaxPreviewActor* NewActor = World->SpawnActor<AFaceParallaxPreviewActor>(
                            AFaceParallaxPreviewActor::StaticClass());
                        if (NewActor)
                        {
                            EditorWidgetInstance->SetPreviewActor(NewActor);
                            UE_LOG(LogTemp, Log, TEXT("[FaceParallax] SpawnEditorTab — auto-spawned preview actor '%s'"),
                                *NewActor->GetActorLabel());
                        }
                        else
                        {
                            UE_LOG(LogTemp, Warning, TEXT("[FaceParallax] SpawnEditorTab — could not spawn a preview actor; widget will show None"));
                        }
                    }
                }
                // Pre-populate ActivePreset from the preview actor's component
                if (EditorWidgetInstance->PreviewActor.IsValid() && EditorWidgetInstance->PreviewActor->FaceParallax)
                {
                    if (!EditorWidgetInstance->ActivePreset)
                        EditorWidgetInstance->ActivePreset = EditorWidgetInstance->PreviewActor->FaceParallax->ActivePreset;
                }

                EditorWidgetInstance->bSuppressValidation = false;
                EditorWidgetInstance->RefreshUI();
            }
        }
    }

    TSharedRef<SWidget> Content = EditorWidgetInstance
        ? EditorWidgetInstance->TakeWidget()
        : SNew(STextBlock).Text(FText::FromString(
            TEXT("Could not load WBP_FaceParallaxEditor — run the deployment script (deploy.py) from the Python console")));

    // TakeWidget() triggered RebuildWidget (which provisions the fallback
    // preset). Re-run the refresh now that the UI exists, and push the preset
    // to the actor so the preview mesh renders the default layers.
    if (EditorWidgetInstance)
    {
        if (EditorWidgetInstance->PreviewActor.IsValid() && EditorWidgetInstance->PreviewActor->FaceParallax)
        {
            if (EditorWidgetInstance->ActivePreset)
            {
                EditorWidgetInstance->PreviewActor->FaceParallax->ActivePreset = EditorWidgetInstance->ActivePreset;
                EditorWidgetInstance->PreviewActor->ApplyPreset(EditorWidgetInstance->ActivePreset);
            }
            EditorWidgetInstance->PreviewActor->RequestCapture();
        }
        EditorWidgetInstance->RefreshUI();
    }

    TSharedRef<SDockTab> Tab = SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label(FText::FromString(TEXT("Face Parallax Editor")))
        [Content];
    // Single-instance lifecycle: closing the tab releases the widget instance
    // (and its preview-actor/preset references); the next open re-creates it.
    Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(
        [this](TSharedRef<SDockTab> ClosedTab)
        {
            EditorWidgetInstance = nullptr;
            if (DockedTab == ClosedTab) DockedTab.Reset();
        }));
    // Phase F: grab keyboard focus when the tab is activated so Ctrl+Z /
    // Ctrl+Shift+Z / Ctrl+Y reach the widget's NativeOnKeyDown (the editor's
    // own global undo takes over whenever focus is elsewhere).
    // Re-entrancy guard: SetKeyboardFocus on a widget inside the dock tab can
    // itself re-trigger tab activation, which would re-enter this lambda and
    // recurse to a stack overflow; the flag breaks that loop on the first
    // re-entry (the focus change itself still completes).
    TSharedRef<bool> bInsideTabActivation = MakeShared<bool>(false);
    Tab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback::CreateLambda(
        [this, bInsideTabActivation](TSharedRef<SDockTab> ActivatedTab, ETabActivationCause InActivationCause)
        {
            if (*bInsideTabActivation) return;
            if (EditorWidgetInstance && EditorWidgetInstance->GetCachedWidget().IsValid())
            {
                *bInsideTabActivation = true;
                FSlateApplication::Get().SetKeyboardFocus(EditorWidgetInstance->GetCachedWidget());
                *bInsideTabActivation = false;
            }
        }));
    DockedTab = Tab;
    return Tab;
}

void UFaceParallaxEditorSubsystem::FaceParallaxOpenEditor()
{
    OpenEditorWidget();
}

// =============================================================
// AUTO-OPEN ON STARTUP
// =============================================================

void UFaceParallaxEditorSubsystem::ScheduleAutoOpen()
{
    if (!bAutoOpenEditorOnStartup) return;
    if (IsRunningCommandlet()) return;              // headless deploy sessions
    if (bFaceParallaxAutoOpenedThisProcess) return; // Live Coding re-init
    if (!GEditor) return;
    EditorLoadedHandle = FEditorDelegates::OnEditorInitialized.AddUObject(
        this, &UFaceParallaxEditorSubsystem::AutoOpenEditorLoaded);
}

void UFaceParallaxEditorSubsystem::AutoOpenEditorLoaded(double /*Duration*/)
{
    bFaceParallaxAutoOpenedThisProcess = true;
    if (EditorLoadedHandle.IsValid())
    {
        FEditorDelegates::OnEditorInitialized.Remove(EditorLoadedHandle);
        EditorLoadedHandle.Reset();
    }
    // Defer the invoke past the editor's loading phase so the main editor frame
    // is guaranteed to exist — TryInvokeTab before that would float the tab.
    AutoOpenTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([this](float)
        {
            AutoOpenTickerHandle.Reset();
            UE_LOG(LogTemp, Log, TEXT("[FaceParallax] Auto-open on startup — invoking tab"));
            OpenEditorWidget();
            return false;   // one-shot
        }), 1.0f);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDefaultModuleImpl, FaceParallaxEditor);
