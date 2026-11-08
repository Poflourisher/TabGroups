// Copyright 2026 Jot. All Rights Reserved.

#include "TabGroupsManager.h"
#include "STabGroupChipLayer.h"
#include "STabGroupEditor.h"
#include "TabGroupsColors.h"
#include "TabGroupsLog.h"
#include "TabGroupsSettings.h"
#include "TabWellAccess.h"
#include "TabWellHook.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/IInputProcessor.h"
#include "Algo/Sort.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/FileManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SOverlay.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ISettingsModule.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TabGroups"

using namespace TabGroups;

namespace
{
	TSharedPtr<FTabGroupsManager> GManager;

	constexpr float TickInterval = 0.05f;
	constexpr double SaveDelaySeconds = 1.0;
	constexpr double ReopenDelaySeconds = 3.0;
	constexpr double MenuUnlockDelaySeconds = 0.25;
	constexpr int32 MaxClosedGroups = 30;
	constexpr float NamelessChipWidth = 18.f;
	constexpr float ChipHorizontalPadding = 18.f;
	const TCHAR* const AssetPrefix = TEXT("asset:");

	UAssetEditorSubsystem* AssetEditors()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	}

	const SDockingTabWell* AsWell(const TSharedPtr<SWidget>& Widget)
	{
		return static_cast<const SDockingTabWell*>(Widget.Get());
	}

	TSharedPtr<SWidget> FindFirstWell(const TSharedRef<SWidget>& Widget)
	{
		static const FName WellType("SDockingTabWell");
		if (Widget->GetType() == WellType)
		{
			return Widget;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindFirstWell(Children->GetChildAt(Index)))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** Tabs up to and including the Level Editor are fixed in place by the engine. */
	int32 NumHeadTabs(const TSlotlessChildren<SDockTab>& Tabs)
	{
		static const FName LevelEditorType("LevelEditor");
		for (int32 Index = 0; Index < Tabs.Num(); ++Index)
		{
			if (Tabs[Index]->GetLayoutIdentifier().TabType == LevelEditorType)
			{
				return Index + 1;
			}
		}
		return 0;
	}

	bool ContainsTab(const FTabGroup& Group, const SDockTab* Tab)
	{
		return Group.Tabs.ContainsByPredicate([Tab](const TWeakPtr<SDockTab>& Weak) { return Weak.Pin().Get() == Tab; });
	}

	FGuid ParseGuid(const FString& Text)
	{
		FGuid Guid;
		return FGuid::Parse(Text, Guid) ? Guid : FGuid::NewGuid();
	}

	/** The tab under the pointer, if it sits in a tab bar. */
	TSharedPtr<SDockTab> FindTabUnderPointer(FSlateApplication& SlateApp, const FVector2D& ScreenPosition)
	{
		static const FName TabType("SDockTab");
		const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(ScreenPosition, SlateApp.GetInteractiveTopLevelWindows());
		for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
		{
			const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
			if (Widget->GetType() == TabType)
			{
				const TSharedRef<SDockTab> Tab = StaticCastSharedRef<SDockTab>(Widget);
				return Tab->GetParent().IsValid() ? TSharedPtr<SDockTab>(Tab) : nullptr;
			}
		}
		return nullptr;
	}

	/**
	 * Ctrl+click or Shift+click on a tab adds it to the selection or removes it. Runs before Slate routes the click, so a
	 * modified click selects instead of activating or dragging the tab. A plain click on a tab clears the selection.
	 */
	class FTabSelectionInputProcessor : public IInputProcessor
	{
	public:
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override
		{
			FTabGroupsManager* Manager = FTabGroupsManager::Get();
			if (!Manager || Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return false;
			}
			// Ctrl and Shift do the same thing, by request: each adds or removes one tab.
			const bool bToggle = (Event.IsControlDown() || Event.IsShiftDown()) && !Event.IsAltDown();
			if (!bToggle)
			{
				const TSharedPtr<SDockTab> Pressed = FindTabUnderPointer(SlateApp, Event.GetScreenSpacePosition());
				if (Pressed.IsValid() && Manager->GetSelectedTabs().Num() > 0)
				{
					// Pressing a selected tab may start dragging all of them, so only a release without a drag clears it.
					if (Manager->GetSelectedTabs().Num() >= 2 && Manager->IsTabSelected(Pressed.Get()))
					{
						Manager->BeginPressOnSelection();
					}
					else
					{
						Manager->ClearTabSelection();
					}
				}
				return false;
			}
			const TSharedPtr<SDockTab> Tab = FindTabUnderPointer(SlateApp, Event.GetScreenSpacePosition());
			if (!Tab.IsValid() || !Manager->CanGroupOrPin(Tab.ToSharedRef()) || Manager->IsPinned(Tab.ToSharedRef()))
			{
				return false;
			}
			if (Manager->GetSelectedTabs().Num() >= 2 && Manager->IsTabSelected(Tab.Get()))
			{
				// Still holding Ctrl while grabbing a selected tab: let the press through so it can become a drag
				// of the whole selection. Released without dragging, it deselects this one tab.
				Manager->BeginPressOnSelection(Tab);
				return false;
			}
			Manager->ToggleTabSelection(Tab.ToSharedRef());
			return true;
		}

		virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override
		{
			FTabGroupsManager* Manager = FTabGroupsManager::Get();
			if (Manager && Event.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				Manager->EndPressOnSelection(SlateApp.IsDragDropping());
			}
			return false;
		}

		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			FTabGroupsManager* Manager = FTabGroupsManager::Get();
			if (Manager && Event.GetKey() == EKeys::Escape && Manager->GetSelectedTabs().Num() > 0)
			{
				Manager->ClearTabSelection();
			}
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("TabGroupsSelection"); }
	};
}

FTabGroupsManager* FTabGroupsManager::Get()
{
	return GManager.Get();
}

void FTabGroupsManager::Startup()
{
	if (IsRunningCommandlet() || !FSlateApplication::IsInitialized() || FParse::Param(FCommandLine::Get(), TEXT("NoTabGroups")))
	{
		return;
	}
	GManager = AsShared();
	StartTime = FPlatformTime::Seconds();

	LoadState();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FTabGroupsManager::Tick), TickInterval);
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateSP(this, &FTabGroupsManager::RegisterMenus));
	SelectionInput = MakeShared<FTabSelectionInputProcessor>();
	FSlateApplication::Get().RegisterInputPreProcessor(SelectionInput);
}

void FTabGroupsManager::Shutdown()
{
	bShuttingDown = true;
	FTSTicker::RemoveTicker(TickHandle);
	if (SelectionInput.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(SelectionInput);
	}
	SelectionInput.Reset();
	if (UAssetEditorSubsystem* Editors = AssetEditors())
	{
		Editors->OnAssetOpenedInEditor().Remove(AssetOpenedHandle);
	}
	if (UObjectInitialized())
	{
		UToolMenus::UnregisterOwner(this);
	}
	TabWellHook::Uninstall();
	Wells.Empty();
	GManager.Reset();
}

bool FTabGroupsManager::Tick(float)
{
	if (IsEngineExitRequested())
	{
		// Tabs are about to be torn down; saving now would record them all as closed.
		bShuttingDown = true;
		return true;
	}

	if (!bHooked)
	{
		FindAndHookFirstWell();
	}

	if (!AssetOpenedHandle.IsValid())
	{
		if (UAssetEditorSubsystem* Editors = AssetEditors())
		{
			AssetOpenedHandle = Editors->OnAssetOpenedInEditor().AddSP(this, &FTabGroupsManager::OnAssetOpened);
		}
	}

	for (auto It = Wells.CreateIterator(); It; ++It)
	{
		if (!It.Value()->Well.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const bool bDragging = FSlateApplication::Get().IsDragDropping();
	if (!bDragging)
	{
		TArray<TSharedRef<FTabWellState>> Dirty;
		for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
		{
			if (Pair.Value->bDirty)
			{
				Dirty.Add(Pair.Value);
			}
		}
		for (const TSharedRef<FTabWellState>& State : Dirty)
		{
			RefreshWell(State);
		}
	}

	if (!bDragging && bClearSelectionAfterDrag)
	{
		bClearSelectionAfterDrag = false;
		DragBlock.Reset();
		ClearTabSelection();
	}

	if (!bDragging)
	{
		for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
		{
			if (Pair.Value->bRefocusAfterDrag)
			{
				Pair.Value->bRefocusAfterDrag = false;
				ShowVisibleTabInstead(*Pair.Value);
			}
		}
	}

	const TSet<FGuid> ToExpand = MoveTemp(PendingExpand);
	PendingExpand.Reset();
	for (const FGuid& Id : ToExpand)
	{
		if (const TSharedPtr<FTabGroup> Group = FindGroup(Id))
		{
			UE_LOG(LogTabGroups, Log, TEXT("Expanding group '%s': its hidden tab became the active tab."), *Group->Name);
		}
		SetCollapsed(Id, false);
	}

	if (PendingEditor.IsValid() && !bDragging)
	{
		const TSharedPtr<FTabWellState> State = FindStateOfGroup(PendingEditor);
		if (!State.IsValid())
		{
			PendingEditor.Invalidate();
		}
		else if (State->Layer.IsValid() && State->Layout.Chips.Num() == State->Groups.Num())
		{
			const FGuid Id = PendingEditor;
			PendingEditor.Invalidate();
			OpenGroupEditor(Id);
		}
	}

	UnlockPinnedAfterMenu();

	const double Now = FPlatformTime::Seconds();
	if (!bReopenDone && Now - StartTime > ReopenDelaySeconds)
	{
		IMainFrameModule* MainFrame = FModuleManager::GetModulePtr<IMainFrameModule>("MainFrame");
		const IAssetRegistry* Registry = IAssetRegistry::Get();
		if (MainFrame && MainFrame->IsWindowInitialized() && Registry && !Registry->IsLoadingAssets())
		{
			bReopenDone = true;
			ReopenPendingAssets();
		}
	}

	if (bSaveQueued && Now - LastChangeTime > SaveDelaySeconds && !bDragging)
	{
		SaveNow();
	}
	return true;
}

void FTabGroupsManager::FindAndHookFirstWell()
{
	for (const TSharedRef<SWindow>& Window : FSlateApplication::Get().GetInteractiveTopLevelWindows())
	{
		if (const TSharedPtr<SWidget> Well = FindFirstWell(Window))
		{
			bHooked = TabWellHook::Install(*AsWell(Well));
			if (!bHooked)
			{
				// Without the hook the plugin does nothing rather than risk the editor.
				UE_LOG(LogTabGroups, Error, TEXT("Tab Groups is disabled for this session: the tab bar could not be hooked."));
				FTSTicker::RemoveTicker(TickHandle);
			}
			return;
		}
	}
}

void FTabGroupsManager::OnAssetOpened(UObject*, IAssetEditorInstance*)
{
	MarkAllWellsDirty();
}

// ---------------------------------------------------------------------------
// Well lookup
// ---------------------------------------------------------------------------

TSharedPtr<FTabWellState> FTabGroupsManager::GetState(const SDockingTabWell& Well)
{
	if (const TSharedRef<FTabWellState>* Found = Wells.Find(&Well))
	{
		if ((*Found)->Well.IsValid())
		{
			return *Found;
		}
	}
	TSharedRef<FTabWellState> State = MakeShared<FTabWellState>();
	State->Well = const_cast<SDockingTabWell&>(Well).AsShared();
	Wells.Add(&Well, State);
	return State;
}

const FTabWellState* FTabGroupsManager::FindWellState(const SDockingTabWell* Well) const
{
	const TSharedRef<FTabWellState>* Found = Wells.Find(Well);
	return Found ? &Found->Get() : nullptr;
}

TSharedPtr<FTabWellState> FTabGroupsManager::FindStateOfGroup(const FGuid& GroupId) const
{
	for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
	{
		for (const TSharedRef<FTabGroup>& Group : Pair.Value->Groups)
		{
			if (Group->Id == GroupId)
			{
				return Pair.Value;
			}
		}
	}
	return nullptr;
}

TSharedPtr<FTabWellState> FTabGroupsManager::FindStateOfTab(const TSharedRef<SDockTab>& Tab)
{
	const TSharedPtr<SDockingTabWell> Well = Tab->GetParent();
	return Well.IsValid() ? GetState(*Well) : nullptr;
}

TSharedPtr<FTabGroup> FTabGroupsManager::FindGroup(const FGuid& GroupId) const
{
	for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
	{
		for (const TSharedRef<FTabGroup>& Group : Pair.Value->Groups)
		{
			if (Group->Id == GroupId)
			{
				return Group;
			}
		}
	}
	return nullptr;
}

TSharedPtr<FTabGroup> FTabGroupsManager::FindGroupOfTab(const TSharedRef<SDockTab>& Tab) const
{
	for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
	{
		for (const TSharedRef<FTabGroup>& Group : Pair.Value->Groups)
		{
			if (ContainsTab(*Group, &Tab.Get()))
			{
				return Group;
			}
		}
	}
	return nullptr;
}

TArray<TSharedRef<FTabGroup>> FTabGroupsManager::GetAllGroups() const
{
	TArray<TSharedRef<FTabGroup>> All;
	for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
	{
		All.Append(Pair.Value->Groups);
	}
	return All;
}

// ---------------------------------------------------------------------------
// Layout (called from the well hook during arrange and paint)
// ---------------------------------------------------------------------------

bool FTabGroupsManager::ArrangeWell(const SDockingTabWell& Well, const FGeometry& Geometry, FArrangedChildren& Out)
{
	const TSharedPtr<FTabWellState> StatePtr = GetState(Well);
	FTabWellState& State = *StatePtr;
	const UTabGroupsSettings* Settings = GetDefault<UTabGroupsSettings>();

	const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(Well);
	const TSharedPtr<SDockTab> Dragged = TabWellAccess::DraggedTab(Well);
	const int32 Foreground = TabWellAccess::ForegroundIndex(Well);

	uint32 Hash = Tabs.Num();
	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		Hash = HashCombine(Hash, PointerHash(&Tabs[Index].Get()));
		EnsureContextMenu(Tabs[Index]);
	}
	if (Hash != State.TabHash)
	{
		State.TabHash = Hash;
		State.bDirty = true;
	}

	const TSharedPtr<SDockTab> First = Tabs.Num() > 0 ? TSharedPtr<SDockTab>(Tabs[0]) : Dragged;
	const ETabRole Role = First.IsValid() ? First->GetVisualTabRole() : ETabRole::PanelTab;
	const bool bTopRow = Role == ETabRole::MajorTab;
	// A narrow panel would only trade height for tabs that are just as squeezed.
	const bool bTooNarrowToWrap = Geometry.GetLocalSize().X < Settings->MinTabWidth * 3.f;
	const int32 MaxRows = ((bTopRow && !Settings->bWrapTopRowTabs) || bTooNarrowToWrap) ? 1 : Settings->MaxRows;

	if (State.Groups.Num() == 0 && !Settings->bCompactTabs && MaxRows <= 1 && Pinned.Num() == 0)
	{
		State.Layout = FLayoutResult();
		State.NumRows = 1;
		return false; // Nothing to do: the engine's own layout runs.
	}

	const FVector2D MaxTabSize = FDockingConstants::GetMaxTabSizeFor(Role);
	const float RowHeight = FMath::Min(Geometry.GetLocalSize().Y / FMath::Max(State.NumRows, 1), MaxTabSize.Y);

	TArray<FLayoutItem> Items;
	TArray<TSharedRef<SDockTab>> ItemTabs;
	Items.Reserve(Tabs.Num());
	ItemTabs.Reserve(Tabs.Num());

	auto WidthOf = [&](const SDockTab& Tab, bool& bOutFixed) -> float
	{
		bOutFixed = Tab.IsTabNameHidden();
		if (Pinned.Contains(&Tab))
		{
			return TabWellAccess::NoNameCantCloseWidth; // Icon only. Don't call CanCloseTab here: on an asset tab it runs the editor's close request.
		}
		if (bOutFixed)
		{
			return Tab.CanCloseTab() ? TabWellAccess::NoNameWidth : TabWellAccess::NoNameCantCloseWidth;
		}
		const float Max = static_cast<float>(MaxTabSize.X);
		return Settings->bCompactTabs ? FMath::Clamp(static_cast<float>(Tab.GetDesiredSize().X), FMath::Min(Settings->MinTabWidth, Max), Max) : Max;
	};

	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		const TSharedRef<SDockTab> Tab = Tabs[Index];
		if (Tab == Dragged)
		{
			continue;
		}
		FLayoutItem Item;
		Item.NaturalWidth = WidthOf(*Tab, Item.bFixedWidth);
		Item.Overlap = Tab->GetOverlapWidth();
		if (const int32* Group = State.TabToGroup.Find(&Tab.Get()); Group && State.Groups.IsValidIndex(*Group))
		{
			Item.Group = *Group;
			if (State.Groups[*Group]->bCollapsed)
			{
				// SDockingTabWell::OnPaint dereferences the foreground tab's arranged entry,
				// so the foreground tab must always be arranged.
				if (Index == Foreground)
				{
					// Dragging a tab out makes Unreal activate a neighbour by list position, which can be a hidden
					// tab; that is not the user asking for the group. Pick a visible tab after the drag instead.
					if (Dragged.IsValid() || FSlateApplication::Get().IsDragDropping())
					{
						State.bRefocusAfterDrag = true;
					}
					else
					{
						PendingExpand.Add(State.Groups[*Group]->Id);
					}
				}
				else
				{
					Item.bHidden = true;
				}
			}
		}
		Items.Add(Item);
		ItemTabs.Add(Tab);
	}

	TArray<FLayoutGroup> Groups;
	for (int32 GroupIndex = 0; GroupIndex < State.Groups.Num(); ++GroupIndex)
	{
		FTabGroup& Group = *State.Groups[GroupIndex];
		const int32 FirstItem = Items.IndexOfByPredicate([GroupIndex](const FLayoutItem& Item) { return Item.Group == GroupIndex; });
		FLayoutGroup& Entry = Groups.Add_GetRef({ Group.ChipWidth });
		if (FirstItem != INDEX_NONE)
		{
			Group.LastFirstItem = FirstItem;
		}
		else if (Dragged.IsValid() && Group.Tabs.Num() > 0)
		{
			// Its only tab is being dragged: the chip stays put instead of jumping to the end of the bar.
			Entry.AnchorItem = Group.LastFirstItem;
		}
	}

	FLayoutParams Params;
	Params.AvailableWidth = Geometry.GetLocalSize().X;
	Params.MinTabWidth = Settings->MinTabWidth;
	Params.MaxRows = MaxRows;

	float DraggedWidth = 0.f;
	float DragX = 0.f;
	if (!Dragged.IsValid())
	{
		State.bHasDragPosition = false;
	}
	else
	{
		bool bFixed = false;
		DraggedWidth = WidthOf(*Dragged, bFixed);
		Params.bHasDrag = true;
		Params.DragWidth = DraggedWidth;
		if (const int32* OwnGroup = State.TabToGroup.Find(Dragged.Get()))
		{
			Params.DragOwnGroup = *OwnGroup;
		}
		// The well works out its drag offset for uniform tabs, so use the pointer and this tab's real width.
		DragX = State.bHasDragPosition
			? static_cast<float>(State.DragPosition.X - TabWellAccess::GrabFraction(Well).X * DraggedWidth)
			: TabWellAccess::DragOffset(Well);
		Params.DragCenterX = DragX + DraggedWidth * 0.5f;
		Params.DragRow = (State.bHasDragPosition && RowHeight > 0.f) ? FMath::FloorToInt(static_cast<float>(State.DragPosition.Y) / RowHeight) : 0;
	}

	State.Layout = ComputeLayout(Items, Groups, Params);
	State.Geometry = Geometry;
	State.RowHeight = RowHeight;
	State.DropIndex = State.Layout.DropIndex;
	State.DropGroup = State.Layout.DropGroup;

	if (State.NumRows != State.Layout.NumRows)
	{
		State.NumRows = State.Layout.NumRows;
		const_cast<SDockingTabWell&>(Well).Invalidate(EInvalidateWidgetReason::Layout);
	}

	for (int32 Index = 0; Index < ItemTabs.Num(); ++Index)
	{
		const FPlacedRect& Rect = State.Layout.Items[Index];
		if (Rect.bVisible)
		{
			Out.AddWidget(Geometry.MakeChild(ItemTabs[Index], FVector2D(Rect.X, Rect.Row * RowHeight), FVector2D(Rect.Width, RowHeight)));
		}
	}
	if (Dragged.IsValid())
	{
		const FVector2D Position(DragX, State.Layout.DragRowResolved * RowHeight);
		Out.AddWidget(Geometry.MakeChild(Dragged.ToSharedRef(), Position, FVector2D(DraggedWidth, RowHeight)));
	}
	return true;
}

int32 FTabGroupsManager::GetRowCount(const SDockingTabWell& Well) const
{
	const FTabWellState* State = FindWellState(&Well);
	return State ? FMath::Max(State->NumRows, 1) : 1;
}

void FTabGroupsManager::OnDragMoved(const SDockingTabWell& Well, const FVector2D& LocalPosition)
{
	const TSharedPtr<FTabWellState> State = GetState(Well);
	State->DragPosition = LocalPosition;
	State->bHasDragPosition = true;
}

void FTabGroupsManager::BeforeDrop(const SDockingTabWell& Well, TSharedPtr<SDockTab>& OutTab, int32& OutDropIndex)
{
	OutTab = TabWellAccess::DraggedTab(Well);
	const TSharedPtr<FTabWellState> State = GetState(Well);
	OutDropIndex = State->DropIndex;
	// Kept apart from DropGroup: the engine's own drop handling may lay the well out again before AfterDrop.
	State->DropGroupAtDrop = State->DropGroup;
}

void FTabGroupsManager::AfterDrop(SDockingTabWell& Well, const TSharedPtr<SDockTab>& Tab, int32 DropIndex)
{
	if (!Tab.IsValid() || DropIndex == INDEX_NONE)
	{
		return;
	}
	const TSharedRef<SDockTab> TabRef = Tab.ToSharedRef();
	const TSharedPtr<FTabWellState> State = GetState(Well);
	const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(Well);
	if (Tabs.Find(TabRef) == INDEX_NONE)
	{
		return;
	}

	// The well worked out the drop index for uniform tabs. Replace it with the one from our layout.
	int32 MinIndex = NumHeadTabs(Tabs);
	for (int32 Index = MinIndex; Index < Tabs.Num(); ++Index)
	{
		if (Tabs[Index] != TabRef && Pinned.Contains(&Tabs[Index].Get()))
		{
			MinIndex = Index + 1;
		}
	}
	TabWellAccess::MoveTab(Well, TabRef, FMath::Max(DropIndex, MinIndex));

	// Like Chrome: dropped over a group (its chip or any of its tabs) it joins that group; anywhere else it is loose.
	const int32 DropGroup = State->DropGroupAtDrop;
	State->DropGroupAtDrop = INDEX_NONE;
	const TSharedPtr<FTabGroup> Target = (!IsPinned(TabRef) && State->Groups.IsValidIndex(DropGroup)) ? TSharedPtr<FTabGroup>(State->Groups[DropGroup]) : nullptr;

	// Dragging one tab of a selection moves them all, like Chrome.
	TArray<TSharedRef<SDockTab>> Moved;
	for (const TWeakPtr<SDockTab>& Weak : DragBlock)
	{
		const TSharedPtr<SDockTab> Member = Weak.Pin();
		if (Member.IsValid() && Member->GetParent().Get() == &Well && Tabs.Find(Member.ToSharedRef()) != INDEX_NONE)
		{
			Moved.Add(Member.ToSharedRef());
		}
	}
	const bool bMovedBlock = Moved.Num() >= 2 && Moved.Contains(TabRef);
	if (bMovedBlock)
	{
		TArray<int32> Block;
		for (const TSharedRef<SDockTab>& Member : Moved)
		{
			Block.Add(Tabs.Find(Member));
		}
		TArray<TSharedRef<SDockTab>> NewOrder;
		for (const int32 Index : ComputeBlockDropOrder(Tabs.Num(), Block, Tabs.Find(TabRef)))
		{
			NewOrder.Add(Tabs[Index]);
		}
		TabWellAccess::SetOrder(Well, NewOrder);
	}
	else
	{
		Moved = { TabRef };
	}
	DragBlock.Reset();

	for (const TSharedRef<SDockTab>& Member : Moved)
	{
		RemoveTabFromAllGroups(Member);
	}
	if (Target.IsValid())
	{
		// Taking its last tabs out above removes the group; dropping them back on it must not lose it.
		if (!State->Groups.Contains(Target.ToSharedRef()))
		{
			State->Groups.Add(Target.ToSharedRef());
		}
		for (const TSharedRef<SDockTab>& Member : Moved)
		{
			Target->Tabs.Add(Member);
		}
	}
	MarkChanged(State.Get());
	if (bMovedBlock)
	{
		ClearTabSelection();
	}
}

// ---------------------------------------------------------------------------
// Keeping well state in step with the engine's tabs
// ---------------------------------------------------------------------------

void FTabGroupsManager::RefreshWell(const TSharedRef<FTabWellState>& State)
{
	const TSharedPtr<SWidget> Well = State->Well.Pin();
	if (!Well.IsValid())
	{
		return;
	}
	State->bDirty = false;

	PruneGroups(*State);
	RestorePendingInto(*State);
	RebuildIndex(*State);
	NormalizeOrder(*State);

	for (const TSharedRef<FTabGroup>& Group : State->Groups)
	{
		UpdateChipWidth(*Group);
	}
	const bool bHasSelection = SelectedTabs.ContainsByPredicate([Well](const TWeakPtr<SDockTab>& Weak)
	{
		const TSharedPtr<SDockTab> Tab = Weak.Pin();
		return Tab.IsValid() && Tab->GetParent().Get() == AsWell(Well);
	});
	if (State->Groups.Num() > 0 || bHasSelection)
	{
		EnsureLayer(State);
	}
	if (State->Layer.IsValid())
	{
		State->Layer->Sync();
	}
}

void FTabGroupsManager::PruneGroups(FTabWellState& State)
{
	const SDockingTabWell* Well = AsWell(State.Well.Pin());
	bool bChanged = false;

	for (int32 GroupIndex = State.Groups.Num() - 1; GroupIndex >= 0; --GroupIndex)
	{
		FTabGroup& Group = *State.Groups[GroupIndex];
		const int32 Removed = Group.Tabs.RemoveAll([Well](const TWeakPtr<SDockTab>& Weak)
		{
			const TSharedPtr<SDockTab> Tab = Weak.Pin();
			return !Tab.IsValid() || Tab->GetParent().Get() != Well;
		});
		bChanged |= Removed > 0;
		if (Group.Tabs.Num() == 0 && Group.UnopenedKeys.Num() == 0)
		{
			State.Groups.RemoveAt(GroupIndex);
			bChanged = true;
		}
	}

	for (auto It = Pinned.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid())
		{
			It.RemoveCurrent();
			bChanged = true;
		}
	}
	if (bChanged)
	{
		bSaveQueued = true;
		LastChangeTime = FPlatformTime::Seconds();
	}
}

void FTabGroupsManager::RestorePendingInto(FTabWellState& State)
{
	const bool bAnyUnopened = GetAllGroups().ContainsByPredicate([](const TSharedRef<FTabGroup>& Group) { return Group->UnopenedKeys.Num() > 0; });
	if (Pending.Groups.Num() == 0 && Pending.Pinned.Num() == 0 && !bAnyUnopened)
	{
		return;
	}

	RebuildAssetMap();
	const SDockingTabWell* Well = AsWell(State.Well.Pin());
	const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*Well);
	const int32 FirstFree = NumHeadTabs(Tabs);

	for (int32 Index = FirstFree; Index < Tabs.Num(); ++Index)
	{
		const TSharedRef<SDockTab> Tab = Tabs[Index];
		if (FindGroupOfTab(Tab).IsValid() || IsPinned(Tab))
		{
			continue;
		}
		const FString Key = GetTabKey(Tab);

		if (Pending.Pinned.Remove(Key) > 0)
		{
			ApplyPin(Tab, true);
			continue;
		}

		if (JoinWaitingGroup(State, Tab, Key))
		{
			continue;
		}

		for (int32 Saved = 0; Saved < Pending.Groups.Num(); ++Saved)
		{
			const FSavedGroup& SavedGroup = Pending.Groups[Saved];
			if (SavedGroup.Members.Contains(Key))
			{
				TSharedRef<FTabGroup> Group = MakeShared<FTabGroup>();
				Group->Id = ParseGuid(SavedGroup.Id);
				Group->Name = SavedGroup.Name;
				Group->Color = SavedGroup.Color;
				Group->CustomColor = SavedGroup.CustomColor;
				Group->bCollapsed = SavedGroup.bCollapsed;
				Group->SavedOrder = SavedGroup.Members;
				Group->UnopenedKeys = SavedGroup.Members;
				Group->UnopenedKeys.Remove(Key);
				Group->Tabs.Add(Tab);
				State.Groups.Add(Group);
				Pending.Groups.RemoveAt(Saved);
				break;
			}
		}
	}
}

bool FTabGroupsManager::JoinWaitingGroup(FTabWellState& State, const TSharedRef<SDockTab>& Tab, const FString& Key)
{
	for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
	{
		FTabWellState& Owner = *Pair.Value;
		for (int32 Index = 0; Index < Owner.Groups.Num(); ++Index)
		{
			const TSharedRef<FTabGroup> Group = Owner.Groups[Index];
			if (Group->UnopenedKeys.Remove(Key) == 0)
			{
				continue;
			}
			if (&Owner != &State)
			{
				if (Group->Tabs.ContainsByPredicate([](const TWeakPtr<SDockTab>& Weak) { return Weak.IsValid(); }))
				{
					return false; // A group lives in one tab bar; this tab opened somewhere else.
				}
				// A waiting group sits on the main bar, but assets may open in another window: follow them.
				Owner.Groups.RemoveAt(Index);
				State.Groups.Add(Group);
				RebuildIndex(Owner);
				Owner.bDirty = true;
			}
			Group->Tabs.Add(Tab);
			return true;
		}
	}
	return false;
}

void FTabGroupsManager::RebuildIndex(FTabWellState& State)
{
	State.TabToGroup.Reset();
	for (int32 GroupIndex = 0; GroupIndex < State.Groups.Num(); ++GroupIndex)
	{
		for (const TWeakPtr<SDockTab>& Weak : State.Groups[GroupIndex]->Tabs)
		{
			if (const TSharedPtr<SDockTab> Tab = Weak.Pin())
			{
				State.TabToGroup.Add(Tab.Get(), GroupIndex);
			}
		}
	}
}

void FTabGroupsManager::NormalizeOrder(FTabWellState& State)
{
	const SDockingTabWell* Well = AsWell(State.Well.Pin());
	const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*Well);
	const int32 Heads = NumHeadTabs(Tabs);

	TArray<FOrderItem> Items;
	Items.SetNum(Tabs.Num());
	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		Items[Index].bHead = Index < Heads;
		Items[Index].bPinned = Pinned.Contains(&Tabs[Index].Get());
		if (const int32* Group = State.TabToGroup.Find(&Tabs[Index].Get()))
		{
			Items[Index].Group = *Group;
		}
	}

	TArray<int32> Order = ComputeNormalizedOrder(Items);

	// Restored groups put their tabs back in their saved order.
	for (int32 GroupIndex = 0; GroupIndex < State.Groups.Num(); ++GroupIndex)
	{
		const TArray<FString>& SavedOrder = State.Groups[GroupIndex]->SavedOrder;
		if (SavedOrder.Num() == 0)
		{
			continue;
		}
		TArray<int32> Positions;
		TArray<int32> Members;
		for (int32 Position = 0; Position < Order.Num(); ++Position)
		{
			if (Items[Order[Position]].Group == GroupIndex)
			{
				Positions.Add(Position);
				Members.Add(Order[Position]);
			}
		}
		TMap<int32, int32> Rank;
		for (int32 Member : Members)
		{
			const int32 Found = SavedOrder.IndexOfByKey(GetTabKey(Tabs[Member]));
			Rank.Add(Member, Found == INDEX_NONE ? MAX_int32 : Found);
		}
		Members.StableSort([&Rank](int32 A, int32 B) { return Rank[A] < Rank[B]; });
		for (int32 Index = 0; Index < Positions.Num(); ++Index)
		{
			Order[Positions[Index]] = Members[Index];
		}
	}

	bool bIdentity = true;
	TArray<TSharedRef<SDockTab>> NewOrder;
	NewOrder.Reserve(Order.Num());
	for (int32 Index = 0; Index < Order.Num(); ++Index)
	{
		bIdentity &= Order[Index] == Index;
		NewOrder.Add(Tabs[Order[Index]]);
	}
	if (!bIdentity)
	{
		TabWellAccess::SetOrder(*Well, NewOrder);
	}
}

void FTabGroupsManager::EnsureLayer(const TSharedRef<FTabWellState>& State)
{
	if (State->Layer.IsValid())
	{
		return;
	}
	static const FName OverlayType("SOverlay");
	const TSharedPtr<SWidget> Well = State->Well.Pin();
	for (TSharedPtr<SWidget> Widget = Well->GetParentWidget(); Widget.IsValid(); Widget = Widget->GetParentWidget())
	{
		if (Widget->GetType() == OverlayType)
		{
			TSharedRef<STabGroupChipLayer> Layer = SNew(STabGroupChipLayer, AsWell(Well));
			StaticCastSharedPtr<SOverlay>(Widget)->AddSlot()[Layer];
			State->Layer = Layer;
			return;
		}
	}
	UE_LOG(LogTabGroups, Warning, TEXT("No overlay above a tab well: its group chips cannot be shown."));
}

void FTabGroupsManager::EnsureContextMenu(const TSharedRef<SDockTab>& Tab)
{
	SDockTab::FExtendContextMenu& Delegate = TabWellAccess::ContextMenuDelegate(Tab.Get());
	FDelegateHandle& Handle = MenuHandles.FindOrAdd(&Tab.Get());
	if (Handle.IsValid() && Delegate.GetHandle() == Handle)
	{
		return;
	}

	// A tab holds one menu delegate, so wrap whatever it already has.
	const SDockTab::FExtendContextMenu Original = Delegate;
	const TWeakPtr<SDockTab> WeakTab = Tab;
	Delegate = SDockTab::FExtendContextMenu::CreateLambda([Original, WeakTab](FMenuBuilder& MenuBuilder)
	{
		Original.ExecuteIfBound(MenuBuilder);
		if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
		{
			Manager->FillTabContextMenu(MenuBuilder, WeakTab);
		}
	});
	Handle = Delegate.GetHandle();
}

void FTabGroupsManager::UpdateChipWidth(FTabGroup& Group)
{
	const FText Text = STabGroupChip::GetChipText(Group);
	if (Text.IsEmpty())
	{
		Group.ChipWidth = NamelessChipWidth;
		return;
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	Group.ChipWidth = static_cast<float>(Measure->Measure(Text, FCoreStyle::GetDefaultFontStyle("Bold", 9)).X) + ChipHorizontalPadding;
}

void FTabGroupsManager::MarkChanged(FTabWellState* State)
{
	if (State)
	{
		RebuildIndex(*State);
		State->bDirty = true;
		for (const TSharedRef<FTabGroup>& Group : State->Groups)
		{
			UpdateChipWidth(*Group);
		}
	}
	bSaveQueued = true;
	LastChangeTime = FPlatformTime::Seconds();
}

void FTabGroupsManager::MarkAllWellsDirty()
{
	for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
	{
		Pair.Value->bDirty = true;
	}
}

void FTabGroupsManager::RemoveTabFromAllGroups(const TSharedRef<SDockTab>& Tab)
{
	for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
	{
		FTabWellState& State = *Pair.Value;
		bool bRemoved = false;
		for (int32 GroupIndex = State.Groups.Num() - 1; GroupIndex >= 0; --GroupIndex)
		{
			FTabGroup& Group = *State.Groups[GroupIndex];
			if (Group.Tabs.RemoveAll([&Tab](const TWeakPtr<SDockTab>& Weak) { return Weak.Pin() == Tab; }) > 0)
			{
				bRemoved = true;
				if (Group.Tabs.Num() == 0 && Group.UnopenedKeys.Num() == 0)
				{
					State.Groups.RemoveAt(GroupIndex);
				}
			}
		}
		if (bRemoved)
		{
			MarkChanged(&State);
		}
	}
}

// ---------------------------------------------------------------------------
// Group actions
// ---------------------------------------------------------------------------

bool FTabGroupsManager::CanGroupOrPin(const TSharedRef<SDockTab>& Tab) const
{
	const TSharedPtr<SDockingTabWell> Well = Tab->GetParent();
	if (!Well.IsValid())
	{
		return false;
	}
	const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*Well);
	return Tabs.Find(Tab) >= NumHeadTabs(Tabs);
}

void FTabGroupsManager::ShowVisibleTabInstead(FTabWellState& State)
{
	const TSharedPtr<SWidget> WellWidget = State.Well.Pin();
	if (!WellWidget.IsValid())
	{
		return;
	}
	const SDockingTabWell* Well = AsWell(WellWidget);
	const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*Well);
	const int32 Foreground = TabWellAccess::ForegroundIndex(*Well);
	if (Foreground < 0 || Foreground >= Tabs.Num())
	{
		return;
	}
	auto IsHidden = [&](int32 Index)
	{
		const int32* Group = State.TabToGroup.Find(&Tabs[Index].Get());
		return Group && State.Groups.IsValidIndex(*Group) && State.Groups[*Group]->bCollapsed;
	};
	if (!IsHidden(Foreground))
	{
		return;
	}
	for (int32 Distance = 1; Distance < Tabs.Num(); ++Distance)
	{
		for (const int32 Index : { Foreground + Distance, Foreground - Distance })
		{
			if (Index >= 0 && Index < Tabs.Num() && !IsHidden(Index))
			{
				Tabs[Foreground]->GetParentDockTabStack()->BringToFront(Tabs[Index]);
				return;
			}
		}
	}
	// Every tab here is hidden in a collapsed group: show the group after all.
	if (const int32* Group = State.TabToGroup.Find(&Tabs[Foreground].Get()))
	{
		PendingExpand.Add(State.Groups[*Group]->Id);
	}
}

TSharedPtr<FTabGroup> FTabGroupsManager::CreateGroup(const TSharedRef<SDockTab>& Tab, const FString& Name, int32 Color, bool bOpenEditor)
{
	const TSharedPtr<FTabWellState> State = FindStateOfTab(Tab);
	if (!State.IsValid() || !CanGroupOrPin(Tab))
	{
		return nullptr;
	}
	if (IsPinned(Tab))
	{
		SetPinned(Tab, false);
	}
	RemoveTabFromAllGroups(Tab);

	if (Color == INDEX_NONE)
	{
		// The first colour this well isn't using yet.
		Color = State->Groups.Num() % NumGroupColors;
		for (int32 Candidate = 0; Candidate < NumGroupColors; ++Candidate)
		{
			const int32 Rotated = (Candidate + 1) % NumGroupColors; // Grey comes last: blue reads better as a first group.
			if (!State->Groups.ContainsByPredicate([Rotated](const TSharedRef<FTabGroup>& Group) { return Group->Color == Rotated; }))
			{
				Color = Rotated;
				break;
			}
		}
	}

	TSharedRef<FTabGroup> Group = MakeShared<FTabGroup>();
	Group->Id = FGuid::NewGuid();
	Group->Name = Name;
	Group->Color = FMath::Clamp(Color, 0, NumGroupColors - 1);
	Group->Tabs.Add(Tab);
	State->Groups.Add(Group);
	MarkChanged(State.Get());

	if (bOpenEditor)
	{
		PendingEditor = Group->Id;
	}
	return Group;
}

void FTabGroupsManager::AddToGroup(const TSharedRef<SDockTab>& Tab, const FGuid& GroupId)
{
	const TSharedPtr<FTabWellState> State = FindStateOfTab(Tab);
	const TSharedPtr<FTabGroup> Group = FindGroup(GroupId);
	if (!State.IsValid() || !Group.IsValid() || FindStateOfGroup(GroupId) != State || !CanGroupOrPin(Tab))
	{
		return;
	}
	if (IsPinned(Tab))
	{
		SetPinned(Tab, false);
	}
	RemoveTabFromAllGroups(Tab);
	Group->Tabs.Add(Tab);
	MarkChanged(State.Get());
}

void FTabGroupsManager::RemoveFromGroup(const TSharedRef<SDockTab>& Tab)
{
	RemoveTabFromAllGroups(Tab);
}

bool FTabGroupsManager::SetCollapsed(const FGuid& GroupId, bool bCollapsed)
{
	const TSharedPtr<FTabGroup> Group = FindGroup(GroupId);
	const TSharedPtr<FTabWellState> State = FindStateOfGroup(GroupId);
	if (!Group.IsValid() || !State.IsValid())
	{
		return false;
	}
	if (Group->bCollapsed == bCollapsed)
	{
		return true;
	}

	if (bCollapsed)
	{
		// Like Chrome: the active tab can't vanish, so activate the nearest tab outside the group.
		const SDockingTabWell* Well = AsWell(State->Well.Pin());
		const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*Well);
		const int32 Foreground = TabWellAccess::ForegroundIndex(*Well);
		if (Tabs.Num() > 0 && Foreground >= 0 && Foreground < Tabs.Num() && ContainsTab(*Group, &Tabs[Foreground].Get()))
		{
			auto IsCandidate = [&](int32 Index)
			{
				const int32* Other = State->TabToGroup.Find(&Tabs[Index].Get());
				return !Other || !State->Groups.IsValidIndex(*Other) || (State->Groups[*Other] != Group.ToSharedRef() && !State->Groups[*Other]->bCollapsed);
			};
			int32 Replacement = INDEX_NONE;
			for (int32 Index = Foreground + 1; Index < Tabs.Num() && Replacement == INDEX_NONE; ++Index)
			{
				Replacement = IsCandidate(Index) ? Index : INDEX_NONE;
			}
			for (int32 Index = Foreground - 1; Index >= 0 && Replacement == INDEX_NONE; --Index)
			{
				Replacement = IsCandidate(Index) ? Index : INDEX_NONE;
			}
			if (Replacement == INDEX_NONE)
			{
				return false; // Every tab here is in this group: there is nothing left to show.
			}
			Tabs[Foreground]->GetParentDockTabStack()->BringToFront(Tabs[Replacement]);
		}
	}

	Group->bCollapsed = bCollapsed;
	MarkChanged(State.Get());

	if (!bCollapsed && Group->UnopenedKeys.Num() > 0)
	{
		OpenAssetKeys(TArray<FString>(Group->UnopenedKeys));
	}
	return true;
}

void FTabGroupsManager::SetGroupName(const FGuid& GroupId, const FString& Name)
{
	if (const TSharedPtr<FTabGroup> Group = FindGroup(GroupId))
	{
		Group->Name = Name;
		MarkChanged(FindStateOfGroup(GroupId).Get());
	}
}

void FTabGroupsManager::SetGroupColor(const FGuid& GroupId, int32 Color)
{
	if (const TSharedPtr<FTabGroup> Group = FindGroup(GroupId))
	{
		Group->Color = FMath::Clamp(Color, 0, NumGroupColors - 1);
		Group->CustomColor.Reset();
		MarkChanged(FindStateOfGroup(GroupId).Get());
	}
}

void FTabGroupsManager::SetGroupCustomColor(const FGuid& GroupId, const FLinearColor& Color)
{
	if (const TSharedPtr<FTabGroup> Group = FindGroup(GroupId))
	{
		Group->CustomColor = ToHexColor(Color);
		MarkChanged(FindStateOfGroup(GroupId).Get());
	}
}

void FTabGroupsManager::Ungroup(const FGuid& GroupId)
{
	if (const TSharedPtr<FTabWellState> State = FindStateOfGroup(GroupId))
	{
		State->Groups.RemoveAll([&GroupId](const TSharedRef<FTabGroup>& Group) { return Group->Id == GroupId; });
		MarkChanged(State.Get());
	}
}

void FTabGroupsManager::CloseGroup(const FGuid& GroupId)
{
	const TSharedPtr<FTabGroup> Group = FindGroup(GroupId);
	const TSharedPtr<FTabWellState> State = FindStateOfGroup(GroupId);
	if (!Group.IsValid() || !State.IsValid())
	{
		return;
	}

	// Like Brave: the group goes to the Tab Groups menu. Only asset tabs can be opened again,
	// so a group of panels alone is not kept (it would reopen as nothing).
	RebuildAssetMap();
	FSavedGroup Saved = MakeSavedGroup(*State, *Group);
	Saved.bCollapsed = false;
	const bool bReopenable = Saved.Members.ContainsByPredicate([](const FString& Key) { return Key.StartsWith(AssetPrefix) && !Key.Contains(TEXT(">")); });
	if (bReopenable)
	{
		ClosedGroups.RemoveAll([&Saved](const FSavedGroup& Existing) { return Existing.Id == Saved.Id; });
		ClosedGroups.Insert(MoveTemp(Saved), 0);
		if (ClosedGroups.Num() > MaxClosedGroups)
		{
			ClosedGroups.SetNum(MaxClosedGroups);
		}
	}
	DeleteGroup(GroupId);
}

void FTabGroupsManager::DeleteGroup(const FGuid& GroupId)
{
	const TSharedPtr<FTabGroup> Group = FindGroup(GroupId);
	if (!Group.IsValid())
	{
		return;
	}
	const TArray<TWeakPtr<SDockTab>> Members = Group->Tabs;
	Ungroup(GroupId);
	for (const TWeakPtr<SDockTab>& Weak : Members)
	{
		if (const TSharedPtr<SDockTab> Tab = Weak.Pin())
		{
			Tab->RequestCloseTab();
		}
	}
}

TSharedRef<SWidget> FTabGroupsManager::MakeGroupEditorWidget(const FGuid& GroupId)
{
	return SNew(STabGroupEditor, GroupId);
}

TOptional<FVector2D> FTabGroupsManager::GetChipScreenPosition(const FGuid& GroupId, const FVector2D& Fraction) const
{
	const TSharedPtr<FTabWellState> State = FindStateOfGroup(GroupId);
	const TSharedPtr<SWidget> Well = State.IsValid() ? State->Well.Pin() : nullptr;
	if (!Well.IsValid())
	{
		return {};
	}
	const int32 Index = State->Groups.IndexOfByPredicate([&GroupId](const TSharedRef<FTabGroup>& Group) { return Group->Id == GroupId; });
	if (!State->Layout.Chips.IsValidIndex(Index) || !State->Layout.Chips[Index].bVisible)
	{
		return {};
	}
	const FPlacedRect& Rect = State->Layout.Chips[Index];
	const FVector2f Local(Rect.X + Rect.Width * Fraction.X, (Rect.Row + Fraction.Y) * State->RowHeight);
	// The tick space geometry is always the on-screen one; an off-screen render (a screenshot) can arrange the bar elsewhere.
	return FVector2D(Well->GetTickSpaceGeometry().LocalToAbsolute(Local));
}

void FTabGroupsManager::OpenGroupEditor(const FGuid& GroupId)
{
	const TSharedPtr<FTabWellState> State = FindStateOfGroup(GroupId);
	if (!State.IsValid() || !State->Layer.IsValid())
	{
		return;
	}
	const TOptional<FVector2D> Location = GetChipScreenPosition(GroupId, FVector2D(0.0, 1.0));
	if (!Location.IsSet())
	{
		return;
	}

	TSharedRef<STabGroupEditor> Editor = SNew(STabGroupEditor, GroupId);
	FSlateApplication::Get().PushMenu(
		State->Layer.ToSharedRef(),
		FWidgetPath(),
		Editor,
		Location.GetValue(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	FSlateApplication::Get().SetKeyboardFocus(Editor->GetNameBox(), EFocusCause::SetDirectly);
}

// ---------------------------------------------------------------------------
// Pinned tabs
// ---------------------------------------------------------------------------

bool FTabGroupsManager::IsPinned(const TSharedRef<SDockTab>& Tab) const
{
	return Pinned.Contains(&Tab.Get());
}

void FTabGroupsManager::ApplyPin(const TSharedRef<SDockTab>& Tab, bool bPinned)
{
	// Not locked: SDockTab::RequestCloseTab refuses on a locked tab, but an asset editor closing
	// itself (Close All, deleting the asset) has already torn down by then and leaves a dangling
	// tab that crashes. Pinned tabs are only locked while a tab menu is open; see LockPinnedForMenu.
	TabWellAccess::SetTabNameHidden(Tab.Get(), bPinned);
	if (bPinned)
	{
		Pinned.Add(&Tab.Get(), Tab);
	}
	else
	{
		Pinned.Remove(&Tab.Get());
	}
}

void FTabGroupsManager::SetPinned(const TSharedRef<SDockTab>& Tab, bool bPinned)
{
	if (!CanGroupOrPin(Tab) || IsPinned(Tab) == bPinned)
	{
		return;
	}
	if (bPinned)
	{
		RemoveTabFromAllGroups(Tab);
	}
	ApplyPin(Tab, bPinned);
	const TSharedPtr<FTabWellState> State = FindStateOfTab(Tab);
	MarkChanged(State.Get());
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

void FTabGroupsManager::LockPinnedForMenu(const TSharedRef<SDockTab>& MenuTab)
{
	// The native menu's Close All and Close Others skip locked tabs. Locking only while the menu
	// is open gives pinned tabs that protection without the danger described in ApplyPin.
	const TSharedPtr<SDockingTabStack> Stack = MenuTab->GetParentDockTabStack();
	if (!Stack.IsValid())
	{
		return;
	}
	const TSlotlessChildren<SDockTab>& Tabs = Stack->GetTabs();
	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		if (IsPinned(Tabs[Index]))
		{
			Stack->SetTabLocked(Tabs[Index], true);
			LockedForMenu.AddUnique(Tabs[Index]);
		}
	}
}

void FTabGroupsManager::UnlockPinnedAfterMenu()
{
	if (LockedForMenu.Num() == 0)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (FSlateApplication::Get().AnyMenusVisible())
	{
		LastMenuVisibleTime = Now;
		return;
	}
	if (Now - LastMenuVisibleTime < MenuUnlockDelaySeconds)
	{
		return;
	}
	for (const TWeakPtr<SDockTab>& Weak : LockedForMenu)
	{
		if (const TSharedPtr<SDockTab> Tab = Weak.Pin())
		{
			if (const TSharedPtr<SDockingTabStack> Stack = Tab->GetParentDockTabStack())
			{
				Stack->SetTabLocked(Tab.ToSharedRef(), TOptional<bool>());
			}
		}
	}
	LockedForMenu.Reset();
}

void FTabGroupsManager::FillTabContextMenu(FMenuBuilder& MenuBuilder, TWeakPtr<SDockTab> WeakTab)
{
	const TSharedPtr<SDockTab> Tab = WeakTab.Pin();
	if (Tab.IsValid())
	{
		LockPinnedForMenu(Tab.ToSharedRef());
	}
	if (!Tab.IsValid() || !CanGroupOrPin(Tab.ToSharedRef()))
	{
		return;
	}
	const TSharedRef<SDockTab> TabRef = Tab.ToSharedRef();
	const TSharedPtr<FTabWellState> State = FindStateOfTab(TabRef);
	const TSharedPtr<FTabGroup> OwnGroup = FindGroupOfTab(TabRef);

	auto Self = [](TFunction<void(FTabGroupsManager&, const TSharedRef<SDockTab>&)> Action, TWeakPtr<SDockTab> Weak)
	{
		return FExecuteAction::CreateLambda([Action, Weak]()
		{
			FTabGroupsManager* Manager = FTabGroupsManager::Get();
			const TSharedPtr<SDockTab> Pinned = Weak.Pin();
			if (Manager && Pinned.IsValid())
			{
				Action(*Manager, Pinned.ToSharedRef());
			}
		});
	};

	MenuBuilder.BeginSection("TabGroups", LOCTEXT("MenuSection", "Tab groups"));

	// Right-clicking inside a Ctrl/Shift selection acts on every selected tab, like Chrome.
	const TArray<TSharedRef<SDockTab>> Selection = GetSelectedTabs();
	if (Selection.Num() >= 2 && Selection.Contains(TabRef))
	{
		TArray<TWeakPtr<SDockTab>> WeakSelection;
		for (const TSharedRef<SDockTab>& Selected : Selection)
		{
			WeakSelection.Add(Selected);
		}
		auto PinAll = [](const TArray<TWeakPtr<SDockTab>>& Weak)
		{
			TArray<TSharedRef<SDockTab>> Out;
			for (const TWeakPtr<SDockTab>& Entry : Weak)
			{
				if (const TSharedPtr<SDockTab> Alive = Entry.Pin())
				{
					Out.Add(Alive.ToSharedRef());
				}
			}
			return Out;
		};

		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("NewGroupMany", "Add {0} tabs to new group"), Selection.Num()),
			LOCTEXT("NewGroupManyTip", "Start a new group with all the selected tabs"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakSelection, PinAll]()
			{
				if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
				{
					Manager->CreateGroupFromTabs(PinAll(WeakSelection));
				}
			})));

		TArray<TPair<FGuid, FText>> Targets;
		if (State.IsValid())
		{
			for (const TSharedRef<FTabGroup>& Group : State->Groups)
			{
				Targets.Add({ Group->Id, Group->Name.IsEmpty() ? LOCTEXT("UnnamedTarget", "Unnamed group") : FText::FromString(Group->Name) });
			}
		}
		if (Targets.Num() > 0)
		{
			MenuBuilder.AddSubMenu(
				FText::Format(LOCTEXT("AddManyToGroup", "Add {0} tabs to group"), Selection.Num()),
				LOCTEXT("AddManyToGroupTip", "Move all the selected tabs into an existing group"),
				FNewMenuDelegate::CreateLambda([Targets, WeakSelection, PinAll](FMenuBuilder& SubMenu)
				{
					for (const TPair<FGuid, FText>& Target : Targets)
					{
						const FGuid Id = Target.Key;
						SubMenu.AddMenuEntry(Target.Value, FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Id, WeakSelection, PinAll]()
						{
							if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
							{
								Manager->AddTabsToGroup(PinAll(WeakSelection), Id);
							}
						})));
					}
				}));
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ClearSelection", "Clear selection"),
			LOCTEXT("ClearSelectionTip", "Deselect the tabs (or press Esc, or click any tab)"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]()
			{
				if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
				{
					Manager->ClearTabSelection();
				}
			})));
		MenuBuilder.EndSection();
		return;
	}

	MenuBuilder.AddMenuEntry(
		LOCTEXT("NewGroup", "Add tab to new group"),
		LOCTEXT("NewGroupTip", "Start a new group with this tab"),
		FSlateIcon(),
		FUIAction(Self([](FTabGroupsManager& Manager, const TSharedRef<SDockTab>& Target) { Manager.CreateGroup(Target); }, WeakTab)));

	TArray<TPair<FGuid, FText>> Others;
	if (State.IsValid())
	{
		for (const TSharedRef<FTabGroup>& Group : State->Groups)
		{
			if (Group != OwnGroup)
			{
				Others.Add({ Group->Id, Group->Name.IsEmpty() ? (Group->CustomColor.IsEmpty() ? FText::Format(LOCTEXT("UnnamedColor", "{0} group"), GetGroupColorName(Group->Color)) : LOCTEXT("UnnamedCustom", "Custom colour group")) : FText::FromString(Group->Name) });
			}
		}
	}
	if (Others.Num() > 0)
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("AddToGroup", "Add tab to group"),
			LOCTEXT("AddToGroupTip", "Move this tab into an existing group"),
			FNewMenuDelegate::CreateLambda([Others, WeakTab, Self](FMenuBuilder& SubMenu)
			{
				for (const TPair<FGuid, FText>& Other : Others)
				{
					const FGuid Id = Other.Key;
					SubMenu.AddMenuEntry(Other.Value, FText::GetEmpty(), FSlateIcon(),
						FUIAction(Self([Id](FTabGroupsManager& Manager, const TSharedRef<SDockTab>& Target) { Manager.AddToGroup(Target, Id); }, WeakTab)));
				}
			}));
	}

	if (OwnGroup.IsValid())
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("RemoveFromGroup", "Remove from group"),
			LOCTEXT("RemoveFromGroupTip", "Take this tab out of its group"),
			FSlateIcon(),
			FUIAction(Self([](FTabGroupsManager& Manager, const TSharedRef<SDockTab>& Target) { Manager.RemoveFromGroup(Target); }, WeakTab)));
	}

	const bool bPinned = IsPinned(TabRef);
	MenuBuilder.AddMenuEntry(
		bPinned ? LOCTEXT("Unpin", "Unpin tab") : LOCTEXT("Pin", "Pin tab"),
		LOCTEXT("PinTip", "Pinned tabs are small, sit at the left of the bar, and are skipped by Close All and Close Others"),
		FSlateIcon(),
		FUIAction(Self([bPinned](FTabGroupsManager& Manager, const TSharedRef<SDockTab>& Target) { Manager.SetPinned(Target, !bPinned); }, WeakTab)));

	MenuBuilder.EndSection();
}

// ---------------------------------------------------------------------------
// Saving and restoring
// ---------------------------------------------------------------------------

FString FTabGroupsManager::GetStatePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("TabGroups") / TEXT("State.json");
}

void FTabGroupsManager::RebuildAssetMap()
{
	AssetKeyByTab.Reset();
	UAssetEditorSubsystem* Editors = AssetEditors();
	if (!Editors)
	{
		return;
	}
	for (UObject* Asset : Editors->GetAllEditedAssets())
	{
		for (IAssetEditorInstance* Editor : Editors->FindEditorsForAsset(Asset))
		{
			if (const TSharedPtr<FTabManager> TabManager = Editor ? Editor->GetAssociatedTabManager() : nullptr)
			{
				if (const TSharedPtr<SDockTab> Owner = TabManager->GetOwnerTab())
				{
					AssetKeyByTab.Add(Owner.Get(), AssetPrefix + Asset->GetPathName());
				}
			}
		}
	}
}

FString FTabGroupsManager::GetTabKey(const TSharedRef<SDockTab>& Tab)
{
	if (const FString* AssetKey = AssetKeyByTab.Find(&Tab.Get()))
	{
		return *AssetKey;
	}
	const FTabId Id = Tab->GetLayoutIdentifier();
	FString Key = Id.IsTabPersistable()
		? TEXT("tab:") + Id.TabType.ToString()
		: TEXT("doc:") + Id.TabType.ToString() + TEXT(":") + Tab->GetTabLabel().ToString();

	// Panels inside an asset editor share type names across editors, so scope them to their owner.
	if (const TSharedPtr<FTabManager> TabManager = Tab->GetTabManagerPtr())
	{
		const TSharedPtr<SDockTab> Owner = TabManager->GetOwnerTab();
		if (Owner.IsValid() && Owner != Tab)
		{
			Key = GetTabKey(Owner.ToSharedRef()) + TEXT(">") + Key;
		}
	}
	return Key;
}

FSavedGroup FTabGroupsManager::MakeSavedGroup(const FTabWellState& State, const FTabGroup& Group)
{
	FSavedGroup Saved;
	Saved.Id = Group.Id.ToString();
	Saved.Name = Group.Name;
	Saved.Color = Group.Color;
	Saved.CustomColor = Group.CustomColor;
	Saved.bCollapsed = Group.bCollapsed;
	if (const TSharedPtr<SWidget> Well = State.Well.Pin())
	{
		const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*AsWell(Well));
		for (int32 Index = 0; Index < Tabs.Num(); ++Index)
		{
			if (ContainsTab(Group, &Tabs[Index].Get()))
			{
				Saved.Members.Add(GetTabKey(Tabs[Index]));
			}
		}
	}
	Saved.Members.Append(Group.UnopenedKeys);
	return Saved;
}

FSavedLayout FTabGroupsManager::CaptureLayout()
{
	RebuildAssetMap();
	FSavedLayout Layout;

	for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
	{
		const TSharedPtr<SWidget> Well = Pair.Value->Well.Pin();
		if (!Well.IsValid())
		{
			continue;
		}
		for (const TSharedRef<FTabGroup>& Group : Pair.Value->Groups)
		{
			FSavedGroup Saved = MakeSavedGroup(*Pair.Value, *Group);
			if (Saved.Members.Num() > 0)
			{
				Layout.Groups.Add(MoveTemp(Saved));
			}
		}
	}

	for (const TPair<const SDockTab*, TWeakPtr<SDockTab>>& Pair : Pinned)
	{
		if (const TSharedPtr<SDockTab> Tab = Pair.Value.Pin())
		{
			Layout.Pinned.Add(GetTabKey(Tab.ToSharedRef()));
		}
	}

	// Saved items that haven't found their tab yet must not be lost.
	Layout.Groups.Append(Pending.Groups);
	Layout.Pinned.Append(Pending.Pinned);
	return Layout;
}

TArray<FString> FTabGroupsManager::CaptureOpenAssets()
{
	RebuildAssetMap();
	TArray<FString> Keys;
	if (const TSharedPtr<FTabWellState> Root = FindRootWell())
	{
		const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*AsWell(Root->Well.Pin()));
		for (int32 Index = 0; Index < Tabs.Num(); ++Index)
		{
			if (const FString* Key = AssetKeyByTab.Find(&Tabs[Index].Get()))
			{
				Keys.Add(*Key);
			}
		}
	}
	return Keys;
}

void FTabGroupsManager::SaveNow()
{
	bSaveQueued = false;
	if (bShuttingDown || IsEngineExitRequested())
	{
		return;
	}
	FSavedState State;
	if (GetDefault<UTabGroupsSettings>()->bRememberGroups)
	{
		State.Layout = CaptureLayout();
	}
	State.Workspaces = Workspaces;
	State.ClosedGroups = ClosedGroups;
	State.ActiveWorkspace = ActiveWorkspace;

	const FString Path = GetStatePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveStringToFile(SerializeState(State), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTabGroups, Warning, TEXT("Could not save tab groups to %s."), *Path);
	}
}

void FTabGroupsManager::LoadState()
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *GetStatePath()))
	{
		return;
	}
	FSavedState State;
	if (!DeserializeState(Json, State))
	{
		UE_LOG(LogTabGroups, Warning, TEXT("Ignoring unreadable tab groups file %s."), *GetStatePath());
		return;
	}
	Workspaces = MoveTemp(State.Workspaces);
	ClosedGroups = MoveTemp(State.ClosedGroups);
	ActiveWorkspace = MoveTemp(State.ActiveWorkspace);
	if (GetDefault<UTabGroupsSettings>()->bRememberGroups)
	{
		Pending = MoveTemp(State.Layout);
	}
}

TSharedPtr<FTabWellState> FTabGroupsManager::FindRootWell()
{
	const TSharedPtr<SDockTab> LevelEditor = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId("LevelEditor"));
	return LevelEditor.IsValid() ? FindStateOfTab(LevelEditor.ToSharedRef()) : nullptr;
}

void FTabGroupsManager::OpenAssetKeys(const TArray<FString>& Keys)
{
	UAssetEditorSubsystem* Editors = AssetEditors();
	if (!Editors)
	{
		return;
	}
	for (const FString& Key : Keys)
	{
		if (!Key.StartsWith(AssetPrefix) || Key.Contains(TEXT(">")))
		{
			continue;
		}
		const FString Path = Key.RightChop(FCString::Strlen(AssetPrefix));
		if (UObject* Asset = LoadObject<UObject>(nullptr, *Path))
		{
			Editors->OpenEditorForAsset(Asset);
		}
		else
		{
			UE_LOG(LogTabGroups, Warning, TEXT("Saved tab group member no longer exists: %s"), *Path);
			for (const TPair<const void*, TSharedRef<FTabWellState>>& Pair : Wells)
			{
				for (const TSharedRef<FTabGroup>& Group : Pair.Value->Groups)
				{
					Group->UnopenedKeys.Remove(Key);
				}
				Pair.Value->bDirty = true;
			}
			for (FSavedGroup& Group : Pending.Groups)
			{
				Group.Members.Remove(Key);
			}
			Pending.Groups.RemoveAll([](const FSavedGroup& Group) { return Group.Members.Num() == 0; });
		}
	}
}

void FTabGroupsManager::ReopenPendingAssets()
{
	if (!GetDefault<UTabGroupsSettings>()->bReopenGroupedAssets)
	{
		return;
	}

	TArray<FString> ToOpen;

	// Groups already partly open: expanded ones get their remaining assets now.
	for (const TSharedRef<FTabGroup>& Group : GetAllGroups())
	{
		if (!Group->bCollapsed)
		{
			ToOpen.Append(Group->UnopenedKeys);
		}
	}

	// Groups with nothing open yet: expanded ones open, collapsed ones wait as a chip on the main tab bar.
	const TSharedPtr<FTabWellState> Root = FindRootWell();
	for (int32 Index = Pending.Groups.Num() - 1; Index >= 0; --Index)
	{
		const FSavedGroup& Saved = Pending.Groups[Index];
		const bool bHasAsset = Saved.Members.ContainsByPredicate([](const FString& Key) { return Key.StartsWith(AssetPrefix) && !Key.Contains(TEXT(">")); });
		if (!bHasAsset)
		{
			continue; // Panels of an editor that isn't open: they rejoin when it opens.
		}
		if (!Saved.bCollapsed)
		{
			ToOpen.Append(Saved.Members);
		}
		else if (Root.IsValid())
		{
			TSharedRef<FTabGroup> Group = MakeShared<FTabGroup>();
			Group->Id = ParseGuid(Saved.Id);
			Group->Name = Saved.Name;
			Group->Color = Saved.Color;
			Group->CustomColor = Saved.CustomColor;
			Group->bCollapsed = true;
			Group->SavedOrder = Saved.Members;
			Group->UnopenedKeys = Saved.Members;
			Root->Groups.Add(Group);
			Pending.Groups.RemoveAt(Index);
			MarkChanged(Root.Get());
		}
	}

	for (const FString& Key : Pending.Pinned)
	{
		if (Key.StartsWith(AssetPrefix) && !Key.Contains(TEXT(">")))
		{
			ToOpen.Add(Key);
		}
	}

	OpenAssetKeys(ToOpen);
}

// ---------------------------------------------------------------------------
// Workspaces
// ---------------------------------------------------------------------------

TArray<FString> FTabGroupsManager::GetWorkspaceNames() const
{
	TArray<FString> Names;
	for (const FSavedWorkspace& Workspace : Workspaces)
	{
		Names.Add(Workspace.Name);
	}
	return Names;
}

void FTabGroupsManager::SaveWorkspace(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return;
	}
	FSavedWorkspace* Workspace = Workspaces.FindByPredicate([&Name](const FSavedWorkspace& Candidate) { return Candidate.Name == Name; });
	if (!Workspace)
	{
		Workspace = &Workspaces.AddDefaulted_GetRef();
		Workspace->Name = Name;
	}
	Workspace->Layout = CaptureLayout();
	Workspace->Assets = CaptureOpenAssets();
	ActiveWorkspace = Name;
	bSaveQueued = true;
	LastChangeTime = FPlatformTime::Seconds();
}

void FTabGroupsManager::SwitchWorkspace(const FString& Name)
{
	const FSavedWorkspace* Target = Workspaces.FindByPredicate([&Name](const FSavedWorkspace& Candidate) { return Candidate.Name == Name; });
	UAssetEditorSubsystem* Editors = AssetEditors();
	if (!Target || !Editors || Name == ActiveWorkspace)
	{
		return;
	}
	const FSavedWorkspace TargetCopy = *Target;

	// Keep the workspace being left up to date.
	if (!ActiveWorkspace.IsEmpty())
	{
		SaveWorkspace(ActiveWorkspace);
	}

	// Workspaces cover asset tabs in the main tab bar, so clear that bar only.
	if (const TSharedPtr<FTabWellState> Root = FindRootWell())
	{
		const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*AsWell(Root->Well.Pin()));
		TArray<TSharedRef<SDockTab>> ToUnpin;
		for (int32 Index = 0; Index < Tabs.Num(); ++Index)
		{
			if (IsPinned(Tabs[Index]))
			{
				ToUnpin.Add(Tabs[Index]);
			}
		}
		for (const TSharedRef<SDockTab>& Tab : ToUnpin)
		{
			ApplyPin(Tab, false);
		}
		Root->Groups.Reset();
		MarkChanged(Root.Get());
	}
	Editors->CloseAllAssetEditors();

	Pending = TargetCopy.Layout;
	ActiveWorkspace = Name;

	TSet<FString> LazyKeys;
	for (const FSavedGroup& Group : Pending.Groups)
	{
		if (Group.bCollapsed)
		{
			LazyKeys.Append(Group.Members);
		}
	}
	TArray<FString> ToOpen;
	for (const FString& Key : TargetCopy.Assets)
	{
		if (!LazyKeys.Contains(Key))
		{
			ToOpen.Add(Key);
		}
	}
	OpenAssetKeys(ToOpen);
	MarkAllWellsDirty();
	bReopenDone = false;
	bSaveQueued = true;
	LastChangeTime = FPlatformTime::Seconds();
}

void FTabGroupsManager::DeleteWorkspace(const FString& Name)
{
	Workspaces.RemoveAll([&Name](const FSavedWorkspace& Workspace) { return Workspace.Name == Name; });
	if (ActiveWorkspace == Name)
	{
		ActiveWorkspace.Reset();
	}
	bSaveQueued = true;
	LastChangeTime = FPlatformTime::Seconds();
}

// ---------------------------------------------------------------------------
// Selecting several tabs
// ---------------------------------------------------------------------------

bool FTabGroupsManager::IsTabSelected(const SDockTab* Tab) const
{
	return SelectedTabs.ContainsByPredicate([Tab](const TWeakPtr<SDockTab>& Weak) { return Weak.Pin().Get() == Tab; });
}

TArray<TSharedRef<SDockTab>> FTabGroupsManager::GetSelectedTabs() const
{
	TArray<TSharedRef<SDockTab>> Out;
	TSharedPtr<SDockingTabWell> Well;
	for (const TWeakPtr<SDockTab>& Weak : SelectedTabs)
	{
		if (const TSharedPtr<SDockTab> Tab = Weak.Pin())
		{
			if (!Well.IsValid())
			{
				Well = Tab->GetParent();
			}
			if (Well.IsValid() && Tab->GetParent() == Well)
			{
				Out.Add(Tab.ToSharedRef());
			}
		}
	}
	if (Well.IsValid())
	{
		const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*Well);
		auto IndexOf = [&Tabs](const SDockTab& Tab)
		{
			for (int32 Index = 0; Index < Tabs.Num(); ++Index)
			{
				if (&Tabs[Index].Get() == &Tab)
				{
					return Index;
				}
			}
			return MAX_int32;
		};
		Algo::Sort(Out, [&IndexOf](const TSharedRef<SDockTab>& A, const TSharedRef<SDockTab>& B) { return IndexOf(*A) < IndexOf(*B); });
	}
	return Out;
}

void FTabGroupsManager::BeginPressOnSelection(const TSharedPtr<SDockTab>& ToggleOnRelease)
{
	bPressedOnSelection = true;
	PressToggleTab = ToggleOnRelease;
	DragBlock.Reset();
	// Taken now, while every selected tab is still in the bar and in its original order.
	for (const TSharedRef<SDockTab>& Tab : GetSelectedTabs())
	{
		DragBlock.Add(Tab);
	}
}

void FTabGroupsManager::EndPressOnSelection(bool bIsDragging)
{
	if (!bPressedOnSelection)
	{
		return;
	}
	bPressedOnSelection = false;
	const TSharedPtr<SDockTab> ToggleTab = PressToggleTab.Pin();
	PressToggleTab.Reset();
	if (bIsDragging)
	{
		bClearSelectionAfterDrag = true; // A drop on a tab bar uses the selection first; see AfterDrop.
	}
	else if (ToggleTab.IsValid())
	{
		DragBlock.Reset();
		ToggleTabSelection(ToggleTab.ToSharedRef());
	}
	else
	{
		DragBlock.Reset();
		ClearTabSelection();
	}
}

void FTabGroupsManager::ClearTabSelection()
{
	if (SelectedTabs.Num() == 0)
	{
		return;
	}
	SelectedTabs.Reset();
	MarkAllWellsDirty();
}

void FTabGroupsManager::ToggleTabSelection(const TSharedRef<SDockTab>& Tab)
{
	const TSharedPtr<SDockingTabWell> Well = Tab->GetParent();
	const TArray<TSharedRef<SDockTab>> Current = GetSelectedTabs();
	if (Current.Num() > 0 && Current[0]->GetParent() != Well)
	{
		SelectedTabs.Reset(); // A selection stays on one tab bar.
	}

	if (SelectedTabs.Num() == 0 && Well.IsValid())
	{
		// Like Chrome, the active tab is already part of the selection.
		const int32 Foreground = TabWellAccess::ForegroundIndex(*Well);
		const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*Well);
		if (Foreground >= 0 && Foreground < Tabs.Num() && Tabs[Foreground] != Tab && CanGroupOrPin(Tabs[Foreground]) && !IsPinned(Tabs[Foreground]))
		{
			SelectedTabs.Add(Tabs[Foreground]);
		}
	}

	const int32 Existing = SelectedTabs.IndexOfByPredicate([&Tab](const TWeakPtr<SDockTab>& Weak) { return Weak.Pin() == Tab; });
	if (Existing != INDEX_NONE)
	{
		SelectedTabs.RemoveAt(Existing);
	}
	else
	{
		SelectedTabs.Add(Tab);
	}

	if (const TSharedPtr<FTabWellState> State = FindStateOfTab(Tab))
	{
		EnsureLayer(State.ToSharedRef());
		State->bDirty = true;
	}
}

TSharedPtr<FTabGroup> FTabGroupsManager::CreateGroupFromTabs(const TArray<TSharedRef<SDockTab>>& Tabs)
{
	if (Tabs.Num() == 0)
	{
		return nullptr;
	}
	const TSharedPtr<FTabGroup> Group = CreateGroup(Tabs[0]);
	if (Group.IsValid())
	{
		for (int32 Index = 1; Index < Tabs.Num(); ++Index)
		{
			AddToGroup(Tabs[Index], Group->Id);
		}
	}
	ClearTabSelection();
	return Group;
}

void FTabGroupsManager::AddTabsToGroup(const TArray<TSharedRef<SDockTab>>& Tabs, const FGuid& GroupId)
{
	for (const TSharedRef<SDockTab>& Tab : Tabs)
	{
		AddToGroup(Tab, GroupId);
	}
	ClearTabSelection();
}

// ---------------------------------------------------------------------------
// Closed groups and the Tab Groups menu
// ---------------------------------------------------------------------------

void FTabGroupsManager::ReopenClosedGroup(const FString& SavedId)
{
	const int32 Index = ClosedGroups.IndexOfByPredicate([&SavedId](const FSavedGroup& Saved) { return Saved.Id == SavedId; });
	if (Index == INDEX_NONE)
	{
		return;
	}
	FSavedGroup Saved = ClosedGroups[Index];
	ClosedGroups.RemoveAt(Index);
	Saved.bCollapsed = false;

	// The same path as a restart: the group waits in Pending and forms as its assets open.
	Pending.Groups.Add(Saved);
	OpenAssetKeys(Saved.Members);
	MarkAllWellsDirty();
	bSaveQueued = true;
	LastChangeTime = FPlatformTime::Seconds();
}

void FTabGroupsManager::ForgetClosedGroup(const FString& SavedId)
{
	ClosedGroups.RemoveAll([&SavedId](const FSavedGroup& Saved) { return Saved.Id == SavedId; });
	bSaveQueued = true;
	LastChangeTime = FPlatformTime::Seconds();
}

void FTabGroupsManager::FocusGroup(const FGuid& GroupId)
{
	const TSharedPtr<FTabGroup> Group = FindGroup(GroupId);
	if (!Group.IsValid())
	{
		return;
	}
	SetCollapsed(GroupId, false);
	for (const TWeakPtr<SDockTab>& Weak : Group->Tabs)
	{
		if (const TSharedPtr<SDockTab> Tab = Weak.Pin())
		{
			Tab->ActivateInParent(ETabActivationCause::SetDirectly);
			if (const TSharedPtr<SWindow> Window = Tab->GetParentWindow())
			{
				Window->BringToFront();
			}
			return;
		}
	}
}

void FTabGroupsManager::CreateGroupFromActiveTab()
{
	const TSharedPtr<SDockTab> Active = FGlobalTabmanager::Get()->GetActiveTab();
	if (Active.IsValid() && CanGroupOrPin(Active.ToSharedRef()))
	{
		CreateGroup(Active.ToSharedRef());
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("NoActiveTab", "Click a tab first. The Home and Level Editor tabs can't be grouped."));
		Info.ExpireDuration = 4.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

namespace
{
	const FSlateBrush* DotBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 5.f);
		return &Brush;
	}

	/** A menu row with the group's colour dot, like Brave's Tab groups menu. */
	TSharedRef<SWidget> MakeGroupRow(const FLinearColor& Color, const FText& Label)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(10.f)
				.HeightOverride(10.f)
				[
					SNew(SImage)
					.Image(DotBrush())
					.ColorAndOpacity(Color)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Label)
			];
	}

	FText GroupLabel(const FString& Name, int32 Count)
	{
		const FText Shown = Name.IsEmpty() ? LOCTEXT("UnnamedGroupLabel", "Unnamed group") : FText::FromString(Name);
		return FText::Format(LOCTEXT("GroupLabelCount", "{0}  ({1})"), Shown, Count);
	}
}

void FTabGroupsManager::FillTabGroupsMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("TabGroupsCreate");
	MenuBuilder.AddMenuEntry(
		LOCTEXT("CreateGroup", "Create new tab group"),
		LOCTEXT("CreateGroupTip", "Start a group with the tab you last clicked"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
			{
				Manager->CreateGroupFromActiveTab();
			}
		})));
	MenuBuilder.EndSection();

	const TArray<TSharedRef<FTabGroup>> Open = GetAllGroups();
	if (Open.Num() > 0)
	{
		MenuBuilder.BeginSection("TabGroupsOpen", LOCTEXT("OpenGroupsSection", "Open groups"));
		for (const TSharedRef<FTabGroup>& Group : Open)
		{
			const FGuid Id = Group->Id;
			MenuBuilder.AddMenuEntry(
				FUIAction(FExecuteAction::CreateLambda([Id]()
				{
					if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
					{
						Manager->FocusGroup(Id);
					}
				})),
				MakeGroupRow(GetGroupDisplayColor(Group->Color, Group->CustomColor), GroupLabel(Group->Name, Group->Tabs.Num() + Group->UnopenedKeys.Num())),
				NAME_None,
				LOCTEXT("FocusGroupTip", "Show this group's tabs"));
		}
		MenuBuilder.EndSection();
	}

	if (ClosedGroups.Num() > 0)
	{
		MenuBuilder.BeginSection("TabGroupsClosed", LOCTEXT("ClosedGroupsSection", "Closed groups"));
		for (const FSavedGroup& Saved : ClosedGroups)
		{
			const FString Id = Saved.Id;
			MenuBuilder.AddMenuEntry(
				FUIAction(FExecuteAction::CreateLambda([Id]()
				{
					if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
					{
						Manager->ReopenClosedGroup(Id);
					}
				})),
				MakeGroupRow(GetGroupDisplayColor(Saved.Color, Saved.CustomColor), GroupLabel(Saved.Name, Saved.Members.Num())),
				NAME_None,
				LOCTEXT("ReopenGroupTip", "Reopen this group and its assets"));
		}
		TArray<TPair<FString, FText>> ToForget;
		for (const FSavedGroup& Saved : ClosedGroups)
		{
			ToForget.Add({ Saved.Id, GroupLabel(Saved.Name, Saved.Members.Num()) });
		}
		MenuBuilder.AddSubMenu(
			LOCTEXT("ForgetClosedGroup", "Delete a closed group"),
			LOCTEXT("ForgetClosedGroupTip", "Remove a closed group from this list for good"),
			FNewMenuDelegate::CreateLambda([ToForget](FMenuBuilder& SubMenu)
			{
				for (const TPair<FString, FText>& Entry : ToForget)
				{
					const FString Id = Entry.Key;
					SubMenu.AddMenuEntry(Entry.Value, FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Id]()
					{
						if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
						{
							Manager->ForgetClosedGroup(Id);
						}
					})));
				}
			}));
		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("TabGroupsMore");
	MenuBuilder.AddSubMenu(
		LOCTEXT("Workspaces", "Workspaces"),
		LOCTEXT("WorkspacesTip", "Save the open asset tabs and their groups under a name, and switch between saved sets"),
		FNewMenuDelegate::CreateStatic(&FTabGroupsManager::FillWorkspacesMenu));
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenSettings", "Settings..."),
		LOCTEXT("OpenSettingsTip", "Open Tab Groups in Editor Preferences"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "Plugins", "TabGroupsSettings");
		})));
	MenuBuilder.EndSection();
}

TSharedRef<SWidget> FTabGroupsManager::MakeTabGroupsMenuWidget()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	FillTabGroupsMenu(MenuBuilder);
	return MenuBuilder.MakeWidget();
}

void FTabGroupsManager::FillWorkspacesMenu(FMenuBuilder& MenuBuilder)
{
	FTabGroupsManager* Manager = FTabGroupsManager::Get();
	if (!Manager)
	{
		return;
	}

	MenuBuilder.BeginSection("Save", LOCTEXT("SaveSection", "Save current tabs as"));
	MenuBuilder.AddWidget(
		SNew(SEditableTextBox)
		.MinDesiredWidth(180.f)
		.HintText(LOCTEXT("WorkspaceHint", "Workspace name, then Enter"))
		.OnTextCommitted_Lambda([](const FText& Text, ETextCommit::Type Commit)
		{
			if (Commit == ETextCommit::OnEnter && !Text.IsEmptyOrWhitespace())
			{
				if (FTabGroupsManager* Current = FTabGroupsManager::Get())
				{
					Current->SaveWorkspace(Text.ToString().TrimStartAndEnd());
				}
				FSlateApplication::Get().DismissAllMenus();
			}
		}),
		FText::GetEmpty());
	MenuBuilder.EndSection();

	const TArray<FString> Names = Manager->GetWorkspaceNames();
	if (Names.Num() == 0)
	{
		return;
	}

	MenuBuilder.BeginSection("Switch", LOCTEXT("SwitchSection", "Switch to"));
	for (const FString& Name : Names)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(Name),
			LOCTEXT("SwitchTip", "Close the open asset tabs and open this workspace's tabs"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Name]()
				{
					if (FTabGroupsManager* Current = FTabGroupsManager::Get())
					{
						Current->SwitchWorkspace(Name);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Name]()
				{
					const FTabGroupsManager* Current = FTabGroupsManager::Get();
					return Current && Current->GetActiveWorkspace() == Name;
				})),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Delete", LOCTEXT("DeleteSection", "Manage"));
	MenuBuilder.AddSubMenu(
		LOCTEXT("DeleteWorkspace", "Delete workspace"),
		LOCTEXT("DeleteWorkspaceTip", "Forget a saved workspace. Open tabs are not affected"),
		FNewMenuDelegate::CreateLambda([Names](FMenuBuilder& SubMenu)
		{
			for (const FString& Name : Names)
			{
				SubMenu.AddMenuEntry(FText::FromString(Name), FText::GetEmpty(), FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Name]()
					{
						if (FTabGroupsManager* Current = FTabGroupsManager::Get())
						{
							Current->DeleteWorkspace(Name);
						}
					})));
			}
		}));
	MenuBuilder.EndSection();
}

void FTabGroupsManager::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	// MainFrame.MainMenu is the parent of every editor window's menu bar, so this shows in asset editors too.
	UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu");
	if (!MainMenu)
	{
		return;
	}
	FToolMenuSection& Section = MainMenu->FindOrAddSection(NAME_None);
	FToolMenuEntry& Entry = Section.AddSubMenu(
		"TabGroups",
		LOCTEXT("TabGroupsMenu", "Tab Groups"),
		LOCTEXT("TabGroupsMenuTip", "Open and closed tab groups, workspaces and settings"),
		FNewToolMenuChoice(FNewMenuDelegate::CreateLambda([](FMenuBuilder& MenuBuilder)
		{
			if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
			{
				Manager->FillTabGroupsMenu(MenuBuilder);
			}
		})),
		false);
	Entry.InsertPosition = FToolMenuInsert("Help", EToolMenuInsertType::Before);
}

#undef LOCTEXT_NAMESPACE
