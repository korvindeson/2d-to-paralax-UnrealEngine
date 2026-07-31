using UnrealBuildTool;

public class MyProject : ModuleRules
{
    public MyProject(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "ProceduralMeshComponent",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Slate",
            "SlateCore",
            "UMG",
            "LevelSequence",
            "CinematicCamera",
            "MaterialEditor",
            "UnrealEd",
            "ToolMenus",
            "Kismet",
            "EditorSubsystem",
            "AssetTools",
            "LevelEditor",
            "ContentBrowser",
            "DesktopPlatform",
        });
    }
}