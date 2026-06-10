using UnrealBuildTool;

public class Arbor : ModuleRules
{
	public Arbor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AIModule",
			"GameplayTags",
			"GameplayTasks",
			"NavigationSystem",
			"Landscape",
			"PCG"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Json",
			"JsonUtilities",
			"EditorScriptingUtilities",
			"Kismet",
			"BlueprintGraph",
			"Slate",
			"SlateCore",
			"InputCore",
			"EditorStyle",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"ImageWrapper",
			"ImageWriteQueue",
			"RenderCore",
			"RHI",
			"Projects",
			"BehaviorTreeEditor",
			"AIGraph",
			"AnimGraph",
			"AnimGraphRuntime",
			"LiveCoding",
			"PythonScriptPlugin",
			"AdvancedPreviewScene",
			"LandscapeEditor",
			"MaterialEditor",
			"Foliage",
			"FoliageEdit",
			"MeshDescription",
			"StaticMeshDescription",
			"LevelEditor",
			"ApplicationCore",
			"DeveloperSettings",
			"Settings",
			"GraphEditor",
			"ContentBrowser",
			"UMG",
			"UMGEditor",
			"MovieScene",
			"MovieSceneTracks",
			"Niagara",
			"NiagaraCore",
			"NiagaraEditor"
		});
	}
}
