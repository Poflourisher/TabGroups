// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace TabGroups
{
	/** One tab in the well, in well order. */
	struct FLayoutItem
	{
		float NaturalWidth = 0.f;
		float Overlap = 0.f;
		int32 Group = INDEX_NONE;
		/** Pinned / name-hidden tabs keep their width and never shrink. */
		bool bFixedWidth = false;
		/** Inside a collapsed group and not the foreground tab. */
		bool bHidden = false;
	};

	struct FLayoutGroup
	{
		float ChipWidth = 0.f;
	};

	struct FLayoutParams
	{
		float AvailableWidth = 0.f;
		float MinTabWidth = 90.f;
		int32 MaxRows = 1;
		float ChipGap = 4.f;

		/** A tab is being dragged through this well. */
		bool bHasDrag = false;
		float DragCenterX = 0.f;
		int32 DragRow = 0;
		float DragWidth = 0.f;
	};

	struct FPlacedRect
	{
		bool bVisible = false;
		float X = 0.f;
		float Width = 0.f;
		int32 Row = 0;
	};

	/** The coloured line under a group, one per row the group touches. */
	struct FUnderline
	{
		int32 Group = INDEX_NONE;
		float X = 0.f;
		float Width = 0.f;
		int32 Row = 0;
	};

	struct FLayoutResult
	{
		TArray<FPlacedRect> Items;
		TArray<FPlacedRect> Chips;
		TArray<FUnderline> Underlines;
		int32 NumRows = 1;
		/** Well index the dragged tab would land at; INDEX_NONE without a drag. */
		int32 DropIndex = INDEX_NONE;
		/** Group the dragged tab is over (its chip or any of its tabs) and so joins on drop; INDEX_NONE if none. */
		int32 DropGroup = INDEX_NONE;
		float DragX = 0.f;
		int32 DragRowResolved = 0;
	};

	TABGROUPSCORE_API FLayoutResult ComputeLayout(const TArray<FLayoutItem>& Items, const TArray<FLayoutGroup>& Groups, const FLayoutParams& Params);

	/** How a tab must be treated when ordering a well. */
	struct FOrderItem
	{
		/** Level Editor and Home: the engine keeps these first. */
		bool bHead = false;
		bool bPinned = false;
		int32 Group = INDEX_NONE;
	};

	/**
	 * Returns the current indices in their required order: head tabs, then pinned tabs,
	 * then the rest with each group's tabs pulled together at its first member. Otherwise stable.
	 */
	TABGROUPSCORE_API TArray<int32> ComputeNormalizedOrder(const TArray<FOrderItem>& Items);

}
