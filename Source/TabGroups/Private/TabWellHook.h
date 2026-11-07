// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SDockingTabWell;

/**
 * Redirects a few virtual functions of SDockingTabWell to this plugin, for every well in the
 * editor, by editing that class's vtable. SDockingTabWell is not exported, so it cannot be
 * subclassed or called; virtual dispatch is the one way in.
 */
namespace TabWellHook
{
	/** Any live well will do: all wells share one vtable. Returns false and changes nothing if the slots cannot be identified. */
	bool Install(const SDockingTabWell& AnyWell);
	void Uninstall();
	bool IsInstalled();
}
