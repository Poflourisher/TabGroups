// Copyright 2026 Jot. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class TabGroups : ModuleRules
{
	public TabGroups(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = "Private/TabGroupsPCH.h";

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DeveloperSettings", "TabGroupsCore" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd",
			"MainFrame",
			"AssetRegistry",
			"Settings",
			"AppFramework", // colour picker
		});

		// SDockingTabWell lives in Slate's private folder. Only its class layout is used here:
		// the class is not exported, so nothing of it can be linked against.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Runtime", "Slate", "Private"));
	}
}
