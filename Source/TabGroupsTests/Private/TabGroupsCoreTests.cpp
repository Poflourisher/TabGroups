// Copyright 2026 Jot. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "TabGroupsLayout.h"
#include "TabGroupsState.h"

using namespace TabGroups;


namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	TArray<FLayoutItem> MakeTabs(int32 Count, float Width)
	{
		TArray<FLayoutItem> Items;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FLayoutItem Item;
			Item.NaturalWidth = Width;
			Items.Add(Item);
		}
		return Items;
	}

	FLayoutParams MakeParams(float Available, int32 MaxRows = 1)
	{
		FLayoutParams Params;
		Params.AvailableWidth = Available;
		Params.MaxRows = MaxRows;
		return Params;
	}

	float RowEnd(const FLayoutResult& Result, int32 Row)
	{
		float End = 0.f;
		for (const FPlacedRect& Rect : Result.Items)
		{
			if (Rect.bVisible && Rect.Row == Row)
			{
				End = FMath::Max(End, Rect.X + Rect.Width);
			}
		}
		return End;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutKeepsNaturalWidthsWhenTheyFit, "TabGroups.Layout.KeepsNaturalWidthsWhenTheyFit", TestFlags)
bool FLayoutKeepsNaturalWidthsWhenTheyFit::RunTest(const FString&)
{
	TArray<FLayoutItem> Items = MakeTabs(3, 100.f);
	Items[1].NaturalWidth = 150.f;
	const FLayoutResult Result = ComputeLayout(Items, {}, MakeParams(1000.f));
	TestEqual(TEXT("first"), Result.Items[0].X, 0.f);
	TestEqual(TEXT("second x"), Result.Items[1].X, 100.f);
	TestEqual(TEXT("second width"), Result.Items[1].Width, 150.f);
	TestEqual(TEXT("third x"), Result.Items[2].X, 250.f);
	TestEqual(TEXT("rows"), Result.NumRows, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutShrinksToFitOneRow, "TabGroups.Layout.ShrinksToFitOneRow", TestFlags)
bool FLayoutShrinksToFitOneRow::RunTest(const FString&)
{
	const FLayoutResult Result = ComputeLayout(MakeTabs(10, 100.f), {}, MakeParams(500.f));
	TestEqual(TEXT("rows"), Result.NumRows, 1);
	TestTrue(TEXT("fits"), RowEnd(Result, 0) <= 500.5f);
	TestEqual(TEXT("uniform"), Result.Items[0].Width, Result.Items[9].Width);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutCollapsedGroupFreesSpace, "TabGroups.Layout.CollapsedGroupFreesSpace", TestFlags)
bool FLayoutCollapsedGroupFreesSpace::RunTest(const FString&)
{
	TArray<FLayoutItem> Items = MakeTabs(10, 100.f);
	for (int32 Index = 2; Index < 8; ++Index)
	{
		Items[Index].Group = 0;
		Items[Index].bHidden = true;
	}
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });

	const FLayoutResult Result = ComputeLayout(Items, Groups, MakeParams(500.f));
	TestFalse(TEXT("hidden"), Result.Items[3].bVisible);
	TestEqual(TEXT("visible tabs keep width"), Result.Items[0].Width, 100.f);
	TestEqual(TEXT("chip after two tabs"), Result.Chips[0].X, 200.f);
	TestEqual(TEXT("tab after chip and gap"), Result.Items[8].X, 264.f);
	TestEqual(TEXT("no underline when nothing is visible"), Result.Underlines.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutExpandedGroupHasUnderline, "TabGroups.Layout.ExpandedGroupHasUnderline", TestFlags)
bool FLayoutExpandedGroupHasUnderline::RunTest(const FString&)
{
	TArray<FLayoutItem> Items = MakeTabs(4, 100.f);
	Items[1].Group = 0;
	Items[2].Group = 0;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 50.f });

	const FLayoutResult Result = ComputeLayout(Items, Groups, MakeParams(1000.f));
	TestEqual(TEXT("one line"), Result.Underlines.Num(), 1);
	TestEqual(TEXT("starts at chip"), Result.Underlines[0].X, Result.Chips[0].X);
	TestEqual(TEXT("ends at last tab"), Result.Underlines[0].X + Result.Underlines[0].Width, Result.Items[2].X + Result.Items[2].Width);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutPinnedNeverShrinks, "TabGroups.Layout.PinnedNeverShrinks", TestFlags)
bool FLayoutPinnedNeverShrinks::RunTest(const FString&)
{
	TArray<FLayoutItem> Items = MakeTabs(10, 100.f);
	Items[0].bFixedWidth = true;
	Items[0].NaturalWidth = 32.f;
	const FLayoutResult Result = ComputeLayout(Items, {}, MakeParams(500.f));
	TestEqual(TEXT("pinned width"), Result.Items[0].Width, 32.f);
	TestTrue(TEXT("others shrink"), Result.Items[1].Width < 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutWrapsBeforeTabsGetTooSmall, "TabGroups.Layout.WrapsBeforeTabsGetTooSmall", TestFlags)
bool FLayoutWrapsBeforeTabsGetTooSmall::RunTest(const FString&)
{
	const FLayoutResult Result = ComputeLayout(MakeTabs(12, 150.f), {}, MakeParams(1000.f, 2));
	TestEqual(TEXT("two rows"), Result.NumRows, 2);
	TestEqual(TEXT("natural width kept"), Result.Items[0].Width, 150.f);
	TestTrue(TEXT("row one fits"), RowEnd(Result, 0) <= 1000.5f);
	TestTrue(TEXT("row two fits"), RowEnd(Result, 1) <= 1000.5f);
	TestEqual(TEXT("second row starts at zero"), Result.Items[6].X, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutStaysOneRowWhileComfortable, "TabGroups.Layout.StaysOneRowWhileComfortable", TestFlags)
bool FLayoutStaysOneRowWhileComfortable::RunTest(const FString&)
{
	const FLayoutResult Result = ComputeLayout(MakeTabs(8, 150.f), {}, MakeParams(1000.f, 2));
	TestEqual(TEXT("one row, shrunk a little"), Result.NumRows, 1);
	TestTrue(TEXT("above minimum"), Result.Items[0].Width >= 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRowsAreCapped, "TabGroups.Layout.RowsAreCapped", TestFlags)
bool FLayoutRowsAreCapped::RunTest(const FString&)
{
	const FLayoutResult Result = ComputeLayout(MakeTabs(60, 150.f), {}, MakeParams(1000.f, 2));
	TestEqual(TEXT("capped"), Result.NumRows, 2);
	TestTrue(TEXT("row one fits"), RowEnd(Result, 0) <= 1000.5f);
	TestTrue(TEXT("row two fits"), RowEnd(Result, 1) <= 1000.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutDragFindsDropIndexAndGap, "TabGroups.Layout.DragFindsDropIndexAndGap", TestFlags)
bool FLayoutDragFindsDropIndexAndGap::RunTest(const FString&)
{
	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragCenterX = 120.f;
	Params.DragWidth = 100.f;
	const FLayoutResult Result = ComputeLayout(MakeTabs(3, 100.f), {}, Params);
	TestEqual(TEXT("before the second tab"), Result.DropIndex, 1);
	TestEqual(TEXT("gap x"), Result.DragX, 100.f);
	TestEqual(TEXT("second tab moved right"), Result.Items[1].X, 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutDragPastCollapsedGroupSkipsHiddenTabs, "TabGroups.Layout.DragPastCollapsedGroupSkipsHiddenTabs", TestFlags)
bool FLayoutDragPastCollapsedGroupSkipsHiddenTabs::RunTest(const FString&)
{
	TArray<FLayoutItem> Items = MakeTabs(5, 100.f);
	Items[1].Group = 0; Items[1].bHidden = true;
	Items[2].Group = 0; Items[2].bHidden = true;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });

	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragWidth = 100.f;

	Params.DragCenterX = 200.f; // Covering more than half of the chip.
	TestEqual(TEXT("lands after the group"), ComputeLayout(Items, Groups, Params).DropIndex, 3);

	Params.DragCenterX = 110.f; // Not there yet.
	TestEqual(TEXT("lands before the group"), ComputeLayout(Items, Groups, Params).DropIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutGroupWithNoTabsSitsAtTheEnd, "TabGroups.Layout.GroupWithNoTabsSitsAtTheEnd", TestFlags)
bool FLayoutGroupWithNoTabsSitsAtTheEnd::RunTest(const FString&)
{
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });
	const FLayoutResult Result = ComputeLayout(MakeTabs(2, 100.f), Groups, MakeParams(1000.f));
	TestTrue(TEXT("visible"), Result.Chips[0].bVisible);
	TestEqual(TEXT("after the tabs"), Result.Chips[0].X, 200.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrderPullsGroupTogether, "TabGroups.Order.PullsGroupTogether", TestFlags)
bool FOrderPullsGroupTogether::RunTest(const FString&)
{
	TArray<FOrderItem> Items;
	Items.SetNum(5);
	Items[1].Group = 0;
	Items[3].Group = 0;
	const TArray<int32> Expected = { 0, 1, 3, 2, 4 };
	TestTrue(TEXT("order"), ComputeNormalizedOrder(Items) == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrderHeadThenPinnedFirst, "TabGroups.Order.HeadThenPinnedFirst", TestFlags)
bool FOrderHeadThenPinnedFirst::RunTest(const FString&)
{
	TArray<FOrderItem> Items;
	Items.SetNum(4);
	Items[1].bHead = true;
	Items[3].bPinned = true;
	const TArray<int32> Expected = { 1, 3, 0, 2 };
	TestTrue(TEXT("order"), ComputeNormalizedOrder(Items) == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrderIsStableWhenAlreadyValid, "TabGroups.Order.IsStableWhenAlreadyValid", TestFlags)
bool FOrderIsStableWhenAlreadyValid::RunTest(const FString&)
{
	TArray<FOrderItem> Items;
	Items.SetNum(4);
	Items[1].Group = 2;
	Items[2].Group = 2;
	const TArray<int32> Expected = { 0, 1, 2, 3 };
	TestTrue(TEXT("order"), ComputeNormalizedOrder(Items) == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlockDropGathersAtPivot, "TabGroups.Order.BlockDropGathersAtPivot", TestFlags)
bool FBlockDropGathersAtPivot::RunTest(const FString&)
{
	// Tabs 0-5. Selected 1, 3 and 5, in that order; tab 3 was dragged and dropped at index 3.
	const TArray<int32> Expected = { 0, 2, 1, 3, 5, 4 };
	TestTrue(TEXT("gathers around the dragged tab"), ComputeBlockDropOrder(6, { 1, 3, 5 }, 3) == Expected);

	// A single tab changes nothing.
	const TArray<int32> Same = { 0, 1, 2 };
	TestTrue(TEXT("one tab is unchanged"), ComputeBlockDropOrder(3, { 1 }, 1) == Same);

	// Pivot at the far left pulls the others to it.
	const TArray<int32> Left = { 0, 2, 4, 1, 3 };
	TestTrue(TEXT("pulls to the left"), ComputeBlockDropOrder(5, { 0, 2, 4 }, 0) == Left);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDropOnSingleTabGroupJoins, "TabGroups.Drop.OnSingleTabGroupJoins", TestFlags)
bool FDropOnSingleTabGroupJoins::RunTest(const FString&)
{
	// [Chip 0-60] [A 64-164]   [B 164-264]: B is being dragged, so only A is in the items.
	TArray<FLayoutItem> Items = MakeTabs(1, 100.f);
	Items[0].Group = 0;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });
	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragWidth = 100.f;

	Params.DragCenterX = 120.f;
	TestEqual(TEXT("over the only tab joins"), ComputeLayout(Items, Groups, Params).DropGroup, 0);
	Params.DragCenterX = 20.f;
	TestEqual(TEXT("before the chip's middle lands in front of the group"), ComputeLayout(Items, Groups, Params).DropGroup, INDEX_NONE);
	Params.DragCenterX = 75.f;
	TestEqual(TEXT("covering a third of the chip joins"), ComputeLayout(Items, Groups, Params).DropGroup, 0);
	Params.DragCenterX = 300.f;
	TestEqual(TEXT("past the group stays out"), ComputeLayout(Items, Groups, Params).DropGroup, INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDropKeepsRoomForOwnTab, "TabGroups.Drop.KeepsRoomForOwnTab", TestFlags)
bool FDropKeepsRoomForOwnTab::RunTest(const FString&)
{
	// A group whose only tab is being dragged: its chip holds its place (0-60), then one loose tab (64-164).
	TArray<FLayoutItem> Items = MakeTabs(1, 100.f);
	TArray<FLayoutGroup> Groups;
	FLayoutGroup Group;
	Group.ChipWidth = 60.f;
	Group.AnchorItem = 0;
	Groups.Add(Group);
	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragWidth = 100.f;
	Params.DragCenterX = 110.f;

	TestEqual(TEXT("another group's tab is outside here"), ComputeLayout(Items, Groups, Params).DropGroup, INDEX_NONE);
	Params.DragOwnGroup = 0;
	TestEqual(TEXT("its own tab is still inside"), ComputeLayout(Items, Groups, Params).DropGroup, 0);
	Params.DragCenterX = 400.f;
	TestEqual(TEXT("dragged well away it leaves"), ComputeLayout(Items, Groups, Params).DropGroup, INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutAnchorKeepsChipInPlace, "TabGroups.Layout.AnchorKeepsChipInPlace", TestFlags)
bool FLayoutAnchorKeepsChipInPlace::RunTest(const FString&)
{
	// A group whose only tab is being dragged, between two loose tabs.
	TArray<FLayoutGroup> Groups;
	FLayoutGroup Group;
	Group.ChipWidth = 60.f;
	Group.AnchorItem = 1;
	Groups.Add(Group);

	const FLayoutResult Result = ComputeLayout(MakeTabs(2, 100.f), Groups, MakeParams(1000.f));
	TestEqual(TEXT("chip stays in front of its anchor"), Result.Chips[0].X, 100.f);
	TestEqual(TEXT("following tab goes after the chip"), Result.Items[1].X, 164.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutBlockDrag, "TabGroups.Layout.BlockDrag", TestFlags)
bool FLayoutBlockDrag::RunTest(const FString&)
{
	// Loose tab 0-100, chip 100-160, group tabs 164-264 and 264-364.
	TArray<FLayoutItem> Items = MakeTabs(3, 100.f);
	Items[1].Group = 0;
	Items[2].Group = 0;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });

	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragWidth = 200.f;
	Params.DragCenterX = 40.f;
	Params.DragHeldWidth = 100.f;
	Params.DragHeldCenterX = 200.f;

	const FLayoutResult Result = ComputeLayout(Items, Groups, Params);
	TestEqual(TEXT("the gap is as wide as the whole block"), Result.Items[0].X, 200.f);
	TestEqual(TEXT("the tab under the pointer decides the group"), Result.DropGroup, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutUnderlineReachesGap, "TabGroups.Layout.UnderlineReachesGap", TestFlags)
bool FLayoutUnderlineReachesGap::RunTest(const FString&)
{
	// Same bar, dragging over the last tab of the group, so the gap opens at the group's end.
	TArray<FLayoutItem> Items = MakeTabs(3, 100.f);
	Items[1].Group = 0;
	Items[2].Group = 0;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });

	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragWidth = 100.f;
	Params.DragCenterX = 350.f;

	const FLayoutResult Result = ComputeLayout(Items, Groups, Params);
	TestEqual(TEXT("over the group"), Result.DropGroup, 0);
	TestEqual(TEXT("one underline"), Result.Underlines.Num(), 1);
	TestEqual(TEXT("it runs from the chip through the gap"), Result.Underlines[0].Width, 364.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEaseTowardLands, "TabGroups.Layout.EaseTowardLands", TestFlags)
bool FEaseTowardLands::RunTest(const FString&)
{
	TestEqual(TEXT("no time, no movement"), EaseToward(0.f, 100.f, 0.f), 0.f);
	const float Step = EaseToward(0.f, 100.f, 1.f / 60.f);
	TestTrue(TEXT("one frame moves part of the way"), Step > 5.f && Step < 60.f);

	float X = 0.f;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		X = EaseToward(X, 100.f, 1.f / 60.f);
	}
	TestEqual(TEXT("it lands exactly within half a second"), X, 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutGroupNotDetectedEarly, "TabGroups.Layout.GroupNotDetectedEarly", TestFlags)
bool FLayoutGroupNotDetectedEarly::RunTest(const FString&)
{
	// Loose tab 0-100, chip 100-160, group tab 164-264. The dragged tab came from the left of the group.
	TArray<FLayoutItem> Items = MakeTabs(2, 100.f);
	Items[1].Group = 0;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });

	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragWidth = 165.f;

	Params.DragCenterX = 110.f;
	const FLayoutResult Before = ComputeLayout(Items, Groups, Params);
	TestEqual(TEXT("not yet past the chip's middle: still outside the group"), Before.DropGroup, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("and the chip is pushed clear of the tab"), Before.Chips[0].X, 265.f);

	Params.DragCenterX = 205.f;
	const FLayoutResult Arriving = ComputeLayout(Items, Groups, Params);
	TestEqual(TEXT("covering a third of the chip: arriving"), Arriving.DropGroup, 0);
	TestEqual(TEXT("but the chip has not hopped yet"), Arriving.Chips[0].X, 265.f);

	Params.DragCenterX = 240.f;
	const FLayoutResult After = ComputeLayout(Items, Groups, Params);
	TestEqual(TEXT("covering half of the chip: inside the group"), After.DropGroup, 0);
	TestEqual(TEXT("and the chip hops to the left of the tab"), After.Chips[0].X, 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutGroupDetectedFromRight, "TabGroups.Layout.GroupDetectedFromRight", TestFlags)
bool FLayoutGroupDetectedFromRight::RunTest(const FString&)
{
	// Chip 0-60, group tab 64-164, loose tab 164-264; a 100-wide tab arrives from the right.
	TArray<FLayoutItem> Items = MakeTabs(2, 100.f);
	Items[0].Group = 0;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });

	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragWidth = 100.f;

	Params.DragCenterX = 190.f; // Left edge at 140: covers less than a third of the last tab.
	TestEqual(TEXT("barely touching the last tab stays out"), ComputeLayout(Items, Groups, Params).DropGroup, static_cast<int32>(INDEX_NONE));
	Params.DragCenterX = 175.f; // Left edge at 125: more than a third.
	TestEqual(TEXT("a third over the last tab joins"), ComputeLayout(Items, Groups, Params).DropGroup, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutGroupDrag, "TabGroups.Layout.GroupDrag", TestFlags)
bool FLayoutGroupDrag::RunTest(const FString&)
{
	// Tab 0-100, the other group's chip 100-160, its tabs 164-264 and 264-364, tab 364-464. Group 0 is being dragged.
	TArray<FLayoutItem> Items = MakeTabs(4, 100.f);
	Items[1].Group = 1;
	Items[2].Group = 1;
	TArray<FLayoutGroup> Groups;
	FLayoutGroup Lifted;
	Lifted.ChipWidth = 40.f;
	Lifted.bLifted = true;
	Groups.Add(Lifted);
	Groups.Add({ 60.f });

	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.bDragIsGroup = true;
	Params.DragWidth = 200.f;

	Params.DragCenterX = 260.f;
	FLayoutResult Result = ComputeLayout(Items, Groups, Params);
	TestFalse(TEXT("the dragged group's chip is left to the caller"), Result.Chips[0].bVisible);
	TestEqual(TEXT("over another group it lands after that group"), Result.DropIndex, 3);
	TestEqual(TEXT("and doesn't join it"), Result.DropGroup, static_cast<int32>(INDEX_NONE));

	Params.DragMinItem = 1;
	Params.DragCenterX = 20.f;
	Result = ComputeLayout(Items, Groups, Params);
	TestEqual(TEXT("it can't land before the pinned tabs"), Result.DropIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDropOnCollapsedChipJoins, "TabGroups.Drop.OnCollapsedChipJoins", TestFlags)
bool FDropOnCollapsedChipJoins::RunTest(const FString&)
{
	TArray<FLayoutItem> Items = MakeTabs(2, 100.f);
	Items[0].Group = 0;
	Items[0].bHidden = true;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });
	FLayoutParams Params = MakeParams(1000.f);
	Params.bHasDrag = true;
	Params.DragWidth = 100.f;

	Params.DragCenterX = 75.f;
	TestEqual(TEXT("over the collapsed chip joins"), ComputeLayout(Items, Groups, Params).DropGroup, 0);
	Params.DragCenterX = 110.f;
	TestEqual(TEXT("over the loose tab stays out"), ComputeLayout(Items, Groups, Params).DropGroup, INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDropOnlyCountsThePointerRow, "TabGroups.Drop.OnlyCountsThePointerRow", TestFlags)
bool FDropOnlyCountsThePointerRow::RunTest(const FString&)
{
	// Two rows; the group is on the first. Being level with it on the second row must not join it.
	TArray<FLayoutItem> Items = MakeTabs(12, 150.f);
	Items[0].Group = 0;
	TArray<FLayoutGroup> Groups;
	Groups.Add({ 60.f });
	FLayoutParams Params = MakeParams(1000.f, 2);
	Params.bHasDrag = true;
	Params.DragWidth = 150.f;
	Params.DragCenterX = 100.f;

	Params.DragRow = 0;
	TestEqual(TEXT("same row joins"), ComputeLayout(Items, Groups, Params).DropGroup, 0);
	Params.DragRow = 1;
	TestEqual(TEXT("other row stays out"), ComputeLayout(Items, Groups, Params).DropGroup, INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStateRoundTrips, "TabGroups.State.RoundTrips", TestFlags)
bool FStateRoundTrips::RunTest(const FString&)
{
	FSavedState State;
	FSavedGroup Group;
	Group.Id = TEXT("g1");
	Group.Name = TEXT("Materials");
	Group.Color = 4;
	Group.bCollapsed = true;
	Group.Members = { TEXT("asset:/Game/M_Rock.M_Rock"), TEXT("tab:OutputLog") };
	State.Layout.Groups.Add(Group);
	State.Layout.Pinned = { TEXT("asset:/Game/BP.BP") };
	FSavedWorkspace Workspace;
	Workspace.Name = TEXT("VFX");
	Workspace.Layout.Groups.Add(Group);
	Workspace.Assets = { TEXT("/Game/M_Rock.M_Rock") };
	State.Workspaces.Add(Workspace);
	State.ActiveWorkspace = TEXT("VFX");
	FSavedGroup Closed = Group;
	Closed.Id = TEXT("g2");
	Closed.CustomColor = TEXT("12AB9F");
	State.ClosedGroups.Add(Closed);

	FSavedState Loaded;
	TestTrue(TEXT("parsed"), DeserializeState(SerializeState(State), Loaded));
	TestEqual(TEXT("groups"), Loaded.Layout.Groups.Num(), 1);
	TestEqual(TEXT("name"), Loaded.Layout.Groups[0].Name, FString(TEXT("Materials")));
	TestEqual(TEXT("color"), Loaded.Layout.Groups[0].Color, 4);
	TestTrue(TEXT("collapsed"), Loaded.Layout.Groups[0].bCollapsed);
	TestEqual(TEXT("members"), Loaded.Layout.Groups[0].Members.Num(), 2);
	TestEqual(TEXT("pinned"), Loaded.Layout.Pinned.Num(), 1);
	TestEqual(TEXT("workspace"), Loaded.Workspaces[0].Name, FString(TEXT("VFX")));
	TestEqual(TEXT("workspace assets"), Loaded.Workspaces[0].Assets.Num(), 1);
	TestEqual(TEXT("active"), Loaded.ActiveWorkspace, FString(TEXT("VFX")));
	TestEqual(TEXT("closed groups"), Loaded.ClosedGroups.Num(), 1);
	TestEqual(TEXT("custom colour"), Loaded.ClosedGroups[0].CustomColor, FString(TEXT("12AB9F")));
	TestTrue(TEXT("palette groups have no custom colour"), Loaded.Layout.Groups[0].CustomColor.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStateSurvivesBadInput, "TabGroups.State.SurvivesBadInput", TestFlags)
bool FStateSurvivesBadInput::RunTest(const FString&)
{
	FSavedState Loaded;
	TestFalse(TEXT("garbage"), DeserializeState(TEXT("not json {"), Loaded));
	TestEqual(TEXT("left empty"), Loaded.Layout.Groups.Num(), 0);

	TestTrue(TEXT("repairable"), DeserializeState(TEXT("{\"groups\":[{\"name\":\"A\",\"color\":99,\"members\":[\"x\"]},{\"name\":\"NoMembers\"}]}"), Loaded));
	TestEqual(TEXT("empty group dropped"), Loaded.Layout.Groups.Num(), 1);
	TestEqual(TEXT("colour clamped"), Loaded.Layout.Groups[0].Color, NumGroupColors - 1);

	TestTrue(TEXT("bad custom colour"), DeserializeState(TEXT("{\"groups\":[{\"name\":\"A\",\"customColor\":\"nothex\",\"members\":[\"x\"]}]}"), Loaded));
	TestTrue(TEXT("bad custom colour dropped"), Loaded.Layout.Groups[0].CustomColor.IsEmpty());
	return true;
}
