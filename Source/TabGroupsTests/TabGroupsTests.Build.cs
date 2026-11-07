// Copyright 2026 Jot. All Rights Reserved.

using UnrealBuildTool;

public class TabGroupsTests : ModuleRules
{
	public TabGroupsTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = "Private/TabGroupsTestsPCH.h";

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore", "ImageCore", "UnrealEd",
			"TabGroupsCore", "TabGroups", "ToolMenus",
		});
	}
}
