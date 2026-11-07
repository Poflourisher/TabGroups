// Copyright 2026 Jot. All Rights Reserved.

// Module precompiled header. Kept small on purpose: without it this module would share
// the engine's multi-gigabyte UnrealEd header, which is slow to build and to load.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SPanel.h"
#include "Widgets/Text/STextBlock.h"
