// Copyright 2026 Jot. All Rights Reserved.

using UnrealBuildTool;

// Pure logic: tab bar layout, tab ordering and saved state. No Slate or UObject types,
// so everything here can be tested with plain numbers.
public class TabGroupsCore : ModuleRules
{
	public TabGroupsCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = "Private/TabGroupsCorePCH.h";
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
	}
}
