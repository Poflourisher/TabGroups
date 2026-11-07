// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/SDockingTabWell.h"

/**
 * Reaches SDockingTabWell's private state. The class layout comes from the engine's own header,
 * so offsets are always right for the engine this plugin was compiled against.
 */
namespace TabWellAccess
{
	TSlotlessChildren<SDockTab>& Tabs(const SDockingTabWell& Well);
	TSharedPtr<SDockTab>& DraggedTab(const SDockingTabWell& Well);
	float& DragOffset(const SDockingTabWell& Well);
	int32& ForegroundIndex(const SDockingTabWell& Well);
	FVector2D& GrabFraction(const SDockingTabWell& Well);
	SDockTab::FExtendContextMenu& ContextMenuDelegate(SDockTab& Tab);
	void SetTabNameHidden(SDockTab& Tab, bool bHidden);

	/** Moves a tab within the well, keeping the foreground tab the same. */
	void MoveTab(const SDockingTabWell& Well, const TSharedRef<SDockTab>& Tab, int32 NewIndex);

	/** Puts the well's tabs in exactly this order. */
	void SetOrder(const SDockingTabWell& Well, const TArray<TSharedRef<SDockTab>>& NewOrder);

	/** Tab widths for name-hidden tabs (FDockingConstants members are not exported). */
	constexpr float NoNameWidth = 53.f;
	constexpr float NoNameCantCloseWidth = 32.f;
}
