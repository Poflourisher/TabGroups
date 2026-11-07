// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace TabGroups
{
	/** Chrome's nine group colours, in menu order. */
	constexpr int32 NumGroupColors = 9;

	struct FSavedGroup
	{
		FString Id;
		FString Name;
		int32 Color = 0;
		/** Colour picked on the colour wheel as hex RRGGBB; empty means Color (a Chrome palette index) is used. */
		FString CustomColor;
		bool bCollapsed = false;
		/** Stable tab keys in tab order. */
		TArray<FString> Members;
	};

	struct FSavedLayout
	{
		TArray<FSavedGroup> Groups;
		TArray<FString> Pinned;
	};

	struct FSavedWorkspace
	{
		FString Name;
		FSavedLayout Layout;
		/** Asset object paths open in this workspace, in tab order. */
		TArray<FString> Assets;
	};

	struct FSavedState
	{
		FSavedLayout Layout;
		/** Groups closed with Close group, kept so they can be reopened from the Tab Groups menu. */
		TArray<FSavedGroup> ClosedGroups;
		TArray<FSavedWorkspace> Workspaces;
		FString ActiveWorkspace;
	};

	TABGROUPSCORE_API FString SerializeState(const FSavedState& State);

	/** False on unreadable input, leaving OutState empty. Out-of-range values are repaired, not rejected. */
	TABGROUPSCORE_API bool DeserializeState(const FString& Json, FSavedState& OutState);
}
