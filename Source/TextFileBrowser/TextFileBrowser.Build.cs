// Copyright (C) 2026 Kane. All Rights Reserved.

using UnrealBuildTool;

public class TextFileBrowser : ModuleRules
{
	public TextFileBrowser(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"ApplicationCore",
			"Projects",
			"ToolMenus",
			"UnrealEd",
			"EditorFramework",
			"ContentBrowser",
			"ContentBrowserData",
			"ContentBrowserFileDataSource",
		});
	}
}
