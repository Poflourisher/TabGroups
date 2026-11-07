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

	Params.DragCenterX = 170.f; // Just past the chip's middle.
	TestEqual(TEXT("lands after the group"), ComputeLayout(Items, Groups, Params).DropIndex, 3);

	Params.DragCenterX = 110.f; // Before the chip's middle.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDropGroupRules, "TabGroups.Order.DropGroupRules", TestFlags)
bool FDropGroupRules::RunTest(const FString&)
{
	TestEqual(TEXT("between two members joins"), ResolveDropGroup(1, 1, INDEX_NONE), 1);
	TestEqual(TEXT("foreign group between members moves over"), ResolveDropGroup(1, 1, 2), 1);
	TestEqual(TEXT("own group at its edge stays"), ResolveDropGroup(1, INDEX_NONE, 1), 1);
	TestEqual(TEXT("loose tab at a group edge stays loose"), ResolveDropGroup(1, INDEX_NONE, INDEX_NONE), INDEX_NONE);
	TestEqual(TEXT("away from its group leaves"), ResolveDropGroup(INDEX_NONE, INDEX_NONE, 1), INDEX_NONE);
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
	return true;
}
