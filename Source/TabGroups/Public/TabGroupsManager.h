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

	/** Contents of the Tab Groups menu in the top menu bar. */
	void FillTabGroupsMenu(FMenuBuilder& MenuBuilder);
	TSharedRef<SWidget> MakeTabGroupsMenuWidget();
	void OpenGroupEditor(const FGuid& GroupId);
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
