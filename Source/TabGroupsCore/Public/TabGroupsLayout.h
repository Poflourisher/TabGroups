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
		/** For a group with no items right now: the item its chip stays in front of (else it goes to the end of the bar). */
		int32 AnchorItem = INDEX_NONE;
		/** Being moved as a whole: its chip is left out of the layout. */
		bool bLifted = false;
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
		/** The group the dragged tab belongs to, if any: its area keeps room for the tab while it is being dragged. */
		int32 DragOwnGroup = INDEX_NONE;
		/**
		 * Set when several tabs are dragged together: DragCenterX and DragWidth then describe the whole block,
		 * and these describe the tab under the pointer, which alone decides the group the block joins.
		 */
		float DragHeldWidth = 0.f;
		float DragHeldCenterX = 0.f;
		/** A whole group is being dragged: it can't land inside another group, or before the first DragMinItem items. */
		bool bDragIsGroup = false;
		int32 DragMinItem = 0;
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
		/** Group the dragged tab would land inside of, and so joins on drop; INDEX_NONE if none. */
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

	/**
	 * Order after dropping several selected tabs together. Block holds their current indices in the order they
	 * should end up in; Pivot (one of them) is the tab that was dragged and already sits where the user dropped it.
	 * The block gathers at the pivot's place and every other tab keeps its order.
	 */
	TABGROUPSCORE_API TArray<int32> ComputeBlockDropOrder(int32 NumTabs, const TArray<int32>& Block, int32 Pivot);

	/** One animation step: moves Current most of the way to Target within about a fifth of a second, then lands on it. */
	TABGROUPSCORE_API float EaseToward(float Current, float Target, float DeltaSeconds);

}
