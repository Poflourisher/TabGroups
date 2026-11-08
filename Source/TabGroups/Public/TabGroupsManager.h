// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Layout/Geometry.h"
#include "TabGroupsLayout.h"
#include "TabGroupsState.h"

class FArrangedChildren;
class FMenuBuilder;
class SDockTab;
class SDockingTabWell;
class STabGroupChipLayer;
class SWidget;

/** A group as it exists in a live tab well. */
struct FTabGroup
{
	FGuid Id;
	FString Name;
	int32 Color = 0;
	/** Colour from the colour wheel as hex RRGGBB; empty means the palette Color is used. */
	FString CustomColor;
	bool bCollapsed = false;
	TArray<TWeakPtr<SDockTab>> Tabs;
	/** Saved members with no open tab yet; opened when the group expands. */
	TArray<FString> UnopenedKeys;
	/** Member order from the saved file, for restored groups. */
	TArray<FString> SavedOrder;
	float ChipWidth = 0.f;
	/** Where the group's first tab was in the latest layout, so its chip can hold its place during a drag. */
	int32 LastFirstItem = INDEX_NONE;
};

/** Everything known about one tab well. */
struct FTabWellState
{
	TWeakPtr<SWidget> Well;
	TArray<TSharedRef<FTabGroup>> Groups;
	TMap<const SDockTab*, int32> TabToGroup;
	TSharedPtr<STabGroupChipLayer> Layer;

	TabGroups::FLayoutResult Layout;
	FGeometry Geometry;
	float RowHeight = 25.f;
	int32 NumRows = 1;
	/** Index the dragged tab would land at, counted without the dragged tab. */
	int32 DropIndex = INDEX_NONE;
	/** Group the dragged tab is over, from the latest layout, and the value captured when the drop began. */
	int32 DropGroup = INDEX_NONE;
	int32 DropGroupAtDrop = INDEX_NONE;
	/** A drag left a hidden (collapsed) tab active; switch to a visible tab once the drag ends. */
	bool bRefocusAfterDrag = false;

	/** Where things were last drawn, so they can slide to new places during and just after a drag. */
	struct FSlide
	{
		float X = 0.f;
		float Right = 0.f;
		int32 Row = 0;
	};
	TMap<const void*, FSlide> Slides;
	TMap<TPair<const void*, int32>, FSlide> LineSlides;
	/** How far each selected tab still is from its place beside the held tab. */
	TMap<const void*, float> BlockOffsets;
	double SlideUntil = 0.0;
	/** A group being moved by its chip, and where the mouse is in the well. */
	FGuid DraggingGroup;
	FVector2D GroupDragPointer = FVector2D::ZeroVector;
	float GroupDragGrab = 0.f;
	int32 GroupDropIndex = INDEX_NONE;
	/** Index of DraggingGroup in Groups as of the latest layout. */
	int32 LiftedGroup = INDEX_NONE;
	/** The span of the bar the dragged tabs cover; chips and lines are drawn behind it. */
	bool bHasDragCover = false;
	float DragCoverLeft = 0.f;
	float DragCoverRight = 0.f;
	int32 DragCoverRow = 0;
	double LastSlideTime = 0.0;
	float SlideDeltaSeconds = 0.f;
	/** Pointer position during a drag, in well space. */
	FVector2D DragPosition = FVector2D::ZeroVector;
	bool bHasDragPosition = false;
	uint32 TabHash = 0;
	bool bDirty = true;
};

/**
 * Owns tab groups, pinned tabs and workspaces, and supplies the layout that the patched
 * SDockingTabWell uses. Tabs always stay in the engine's own tab list: grouping only changes
 * how they are arranged, so engine behaviour (finding, closing, saving tabs) is untouched.
 */
class TABGROUPS_API FTabGroupsManager : public TSharedFromThis<FTabGroupsManager>
{
public:
	static FTabGroupsManager* Get();

	void Startup();
	void Shutdown();

	// Groups
	TSharedPtr<FTabGroup> CreateGroup(const TSharedRef<SDockTab>& Tab, const FString& Name = FString(), int32 Color = INDEX_NONE, bool bOpenEditor = true);
	void AddToGroup(const TSharedRef<SDockTab>& Tab, const FGuid& GroupId);
	void RemoveFromGroup(const TSharedRef<SDockTab>& Tab);
	bool SetCollapsed(const FGuid& GroupId, bool bCollapsed);
	void SetGroupName(const FGuid& GroupId, const FString& Name);
	void SetGroupColor(const FGuid& GroupId, int32 Color);
	void SetGroupCustomColor(const FGuid& GroupId, const FLinearColor& Color);
	void Ungroup(const FGuid& GroupId);
	/** Closes the tabs but keeps the group, so it can be reopened from the Tab Groups menu. */
	void CloseGroup(const FGuid& GroupId);
	/** Closes the tabs and forgets the group. */
	void DeleteGroup(const FGuid& GroupId);
	/** Expands the group and brings its first tab forward. */
	void FocusGroup(const FGuid& GroupId);
	/** Starts a group with the active tab, as the Tab Groups menu does. */
	void CreateGroupFromActiveTab();

	// Closed groups
	const TArray<TabGroups::FSavedGroup>& GetClosedGroups() const { return ClosedGroups; }
	void ReopenClosedGroup(const FString& SavedId);
	void ForgetClosedGroup(const FString& SavedId);

	// Selecting several tabs (Ctrl+click or Shift+click)
	void ToggleTabSelection(const TSharedRef<SDockTab>& Tab);
	void ClearTabSelection();
	/** A plain press landed on a selected tab: keep the selection in case this becomes a drag of all of them. */
	void BeginPressOnSelection(const TSharedPtr<SDockTab>& ToggleOnRelease = nullptr);
	void EndPressOnSelection(bool bIsDragging);
	bool IsTabSelected(const SDockTab* Tab) const;
	/** Selected tabs that still exist, in tab bar order. A selection never spans two tab bars. */
	TArray<TSharedRef<SDockTab>> GetSelectedTabs() const;
	TSharedPtr<FTabGroup> CreateGroupFromTabs(const TArray<TSharedRef<SDockTab>>& Tabs);
	void AddTabsToGroup(const TArray<TSharedRef<SDockTab>>& Tabs, const FGuid& GroupId);

	/** Contents of the Tab Groups menu in the top menu bar. */
	void FillTabGroupsMenu(FMenuBuilder& MenuBuilder);
	TSharedRef<SWidget> MakeTabGroupsMenuWidget();
	void OpenGroupEditor(const FGuid& GroupId);

	/** Moving a whole group by its chip, within its own tab bar. */
	bool BeginGroupDrag(const FGuid& GroupId, const FVector2D& ScreenPosition);
	void UpdateGroupDrag(const FGuid& GroupId, const FVector2D& ScreenPosition);
	void EndGroupDrag(const FGuid& GroupId, bool bCommit);
	void CancelGroupDrags();
	TSharedRef<SWidget> MakeGroupEditorWidget(const FGuid& GroupId);
	/** Screen position of a point on a group chip; Fraction runs 0-1 across the chip and its row. */
	TOptional<FVector2D> GetChipScreenPosition(const FGuid& GroupId, const FVector2D& Fraction) const;
	TSharedPtr<FTabGroup> FindGroup(const FGuid& GroupId) const;
	TSharedPtr<FTabGroup> FindGroupOfTab(const TSharedRef<SDockTab>& Tab) const;
	TArray<TSharedRef<FTabGroup>> GetAllGroups() const;
	bool CanGroupOrPin(const TSharedRef<SDockTab>& Tab) const;

	// Pinned tabs
	void SetPinned(const TSharedRef<SDockTab>& Tab, bool bPinned);
	bool IsPinned(const TSharedRef<SDockTab>& Tab) const;

	// Workspaces
	void SaveWorkspace(const FString& Name);
	void SwitchWorkspace(const FString& Name);
	void DeleteWorkspace(const FString& Name);
	TArray<FString> GetWorkspaceNames() const;
	FString GetActiveWorkspace() const { return ActiveWorkspace; }

	// Saved state
	void SaveNow();
	FString GetStatePath() const;

	/** Stable identity of a tab across editor sessions. */
	FString GetTabKey(const TSharedRef<SDockTab>& Tab);

	// Called by the well hook
	bool ArrangeWell(const SDockingTabWell& Well, const FGeometry& Geometry, FArrangedChildren& Out);
	int32 GetRowCount(const SDockingTabWell& Well) const;
	void OnDragMoved(const SDockingTabWell& Well, const FVector2D& LocalPosition);
	void BeforeDrop(const SDockingTabWell& Well, TSharedPtr<SDockTab>& OutTab, int32& OutDropIndex);
	void AfterDrop(SDockingTabWell& Well, const TSharedPtr<SDockTab>& Tab, int32 DropIndex);

	// Called by the chip layer
	const FTabWellState* FindWellState(const SDockingTabWell* Well) const;

	void FillTabContextMenu(FMenuBuilder& MenuBuilder, TWeakPtr<SDockTab> WeakTab);

private:
	bool Tick(float DeltaTime);
	void FindAndHookFirstWell();
	void OnAssetOpened(UObject* Asset, class IAssetEditorInstance* Editor);

	TSharedPtr<FTabWellState> GetState(const SDockingTabWell& Well);
	TSharedPtr<FTabWellState> FindStateOfGroup(const FGuid& GroupId) const;
	TSharedPtr<FTabWellState> FindStateOfTab(const TSharedRef<SDockTab>& Tab);

	void RefreshWell(const TSharedRef<FTabWellState>& State);
	void RebuildIndex(FTabWellState& State);
	void NormalizeOrder(FTabWellState& State);
	void PruneGroups(FTabWellState& State);
	void RestorePendingInto(FTabWellState& State);
	bool JoinWaitingGroup(FTabWellState& State, const TSharedRef<SDockTab>& Tab, const FString& Key);
	void EnsureLayer(const TSharedRef<FTabWellState>& State);
	void EnsureContextMenu(const TSharedRef<SDockTab>& Tab);
	void UpdateChipWidth(FTabGroup& Group);
	void RemoveTabFromAllGroups(const TSharedRef<SDockTab>& Tab);
	void MarkChanged(FTabWellState* State);
	void MarkAllWellsDirty();
	void ApplyPin(const TSharedRef<SDockTab>& Tab, bool bPinned);
	void ShowVisibleTabInstead(FTabWellState& State);
	void LockPinnedForMenu(const TSharedRef<SDockTab>& MenuTab);
	void UnlockPinnedAfterMenu();

	void RebuildAssetMap();
	void LoadState();
	TabGroups::FSavedLayout CaptureLayout();
	TArray<FString> CaptureOpenAssets();
	void ApplyLayout(const TabGroups::FSavedLayout& Layout, const TArray<FString>& AssetsToOpen);
	void OpenAssetKeys(const TArray<FString>& Keys);
	void ReopenPendingAssets();
	TSharedPtr<FTabWellState> FindRootWell();
	void RegisterMenus();
	static void FillWorkspacesMenu(FMenuBuilder& MenuBuilder);
	TabGroups::FSavedGroup MakeSavedGroup(const FTabWellState& State, const FTabGroup& Group);

	TMap<const void*, TSharedRef<FTabWellState>> Wells;
	TMap<const SDockTab*, TWeakPtr<SDockTab>> Pinned;
	TMap<const SDockTab*, FDelegateHandle> MenuHandles;
	TMap<const SDockTab*, FString> AssetKeyByTab;

	/** Saved groups and pins waiting for their tabs to appear. */
	TabGroups::FSavedLayout Pending;
	TArray<TabGroups::FSavedWorkspace> Workspaces;
	/** Most recently closed first. */
	TArray<TabGroups::FSavedGroup> ClosedGroups;
	FString ActiveWorkspace;

	TSet<FGuid> PendingExpand;
	TArray<TWeakPtr<SDockTab>> LockedForMenu;
	TArray<TWeakPtr<SDockTab>> SelectedTabs;
	/** The selection as it was when a drag of it began, in tab bar order. */
	TArray<TWeakPtr<SDockTab>> DragBlock;
	TWeakPtr<SDockTab> PressToggleTab;
	/** The selected tabs riding with the one under the pointer, in bar order; empty unless two or more are. */
	void UpdateSlides(FTabWellState& State, const TArray<TSharedRef<SDockTab>>& ItemTabs, bool bDragging);
	TArray<TSharedRef<SDockTab>> GatherDragBlock(const FTabWellState& State, const TSlotlessChildren<SDockTab>& Tabs, const TSharedPtr<SDockTab>& Dragged) const;
	bool bPressedOnSelection = false;
	bool bClearSelectionAfterDrag = false;
	TSharedPtr<class IInputProcessor> SelectionInput;
	double LastMenuVisibleTime = 0.0;
	FGuid PendingEditor;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle AssetOpenedHandle;
	double LastChangeTime = 0.0;
	double StartTime = 0.0;
	bool bHooked = false;
	bool bSaveQueued = false;
	bool bReopenDone = false;
	bool bShuttingDown = false;
};
