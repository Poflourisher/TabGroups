// Copyright 2026 Jot. All Rights Reserved.

#include "TabGroupsLayout.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, TabGroupsCore)

namespace TabGroups
{
	namespace
	{
		constexpr float AbsoluteMinTabWidth = 30.f;

		enum class EEntry : uint8 { Tab, Chip, Gap };

		struct FEntry
		{
			EEntry Kind = EEntry::Tab;
			int32 Index = INDEX_NONE;
			float Width = 0.f;
			float Advance = 0.f; // Added to Width to reach the next entry (negative for tab overlap).
			bool bFlexible = false;
			float X = 0.f;
			int32 Row = 0;
		};

		void BuildEntries(const TArray<FLayoutItem>& Items, const TArray<FLayoutGroup>& Groups, const FLayoutParams& Params, TArray<FEntry>& Out)
		{
			TBitArray<> GroupSeen(false, Groups.Num());
			int32 Previous = INDEX_NONE;

			auto AddChip = [&](int32 Group)
			{
				FEntry Chip;
				Chip.Kind = EEntry::Chip;
				Chip.Index = Group;
				Chip.Width = Groups[Group].ChipWidth;
				Chip.Advance = Params.ChipGap;
				Out.Add(Chip);
				GroupSeen[Group] = true;
			};

			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				for (int32 Group = 0; Group < Groups.Num(); ++Group)
				{
					if (!GroupSeen[Group] && Groups[Group].AnchorItem == Index)
					{
						AddChip(Group);
					}
				}

				const FLayoutItem& Item = Items[Index];
				const bool bValidGroup = Groups.IsValidIndex(Item.Group);
				if (bValidGroup && Item.Group != Previous && !GroupSeen[Item.Group])
				{
					AddChip(Item.Group);
				}
				Previous = bValidGroup ? Item.Group : INDEX_NONE;

				if (Item.bHidden)
				{
					continue;
				}
				FEntry Tab;
				Tab.Kind = EEntry::Tab;
				Tab.Index = Index;
				Tab.Width = Item.NaturalWidth;
				Tab.Advance = -Item.Overlap;
				Tab.bFlexible = !Item.bFixedWidth;
				Out.Add(Tab);
			}

			// Groups with no open tabs yet (restored but not reopened) sit at the end of the bar.
			for (int32 Group = 0; Group < Groups.Num(); ++Group)
			{
				if (!GroupSeen[Group] && !Groups[Group].bLifted)
				{
					AddChip(Group);
				}
			}
		}

		/** Decides row count and shrinks flexible entries so everything fits. */
		void FitAndPlace(TArray<FEntry>& Entries, const FLayoutParams& Params, int32& OutRows)
		{
			const float Available = FMath::Max(Params.AvailableWidth, 1.f);
			const int32 MaxRows = FMath::Max(Params.MaxRows, 1);

			float FixedTotal = 0.f;
			float FlexTotal = 0.f;
			float SmallestFlex = TNumericLimits<float>::Max();
			for (const FEntry& Entry : Entries)
			{
				const float Footprint = Entry.Width + Entry.Advance;
				if (Entry.bFlexible)
				{
					FlexTotal += Footprint;
					SmallestFlex = FMath::Min(SmallestFlex, Entry.Width);
				}
				else
				{
					FixedTotal += Footprint;
				}
			}

			int32 Rows = 1;
			float Scale = 1.f;
			for (Rows = 1; Rows <= MaxRows; ++Rows)
			{
				const float Room = Available * Rows - FixedTotal;
				Scale = FlexTotal > 0.f ? FMath::Clamp(Room / FlexTotal, 0.f, 1.f) : 1.f;
				const bool bComfortable = Scale >= 1.f || SmallestFlex * Scale >= FMath::Min(Params.MinTabWidth, SmallestFlex);
				if (bComfortable || Rows == MaxRows)
				{
					break;
				}
			}
			Rows = FMath::Min(Rows, MaxRows);

			for (FEntry& Entry : Entries)
			{
				if (Entry.bFlexible)
				{
					Entry.Width = FMath::Max(Entry.Width * Scale, FMath::Min(AbsoluteMinTabWidth, Entry.Width));
				}
			}

			// Greedy row packing; a row that still overflows shrinks its own flexible tabs.
			int32 Row = 0;
			float X = 0.f;
			for (FEntry& Entry : Entries)
			{
				if (X > 0.f && X + Entry.Width > Available && Row < Rows - 1)
				{
					++Row;
					X = 0.f;
				}
				Entry.X = X;
				Entry.Row = Row;
				X += Entry.Width + Entry.Advance;
			}
			OutRows = Row + 1;

			for (int32 CurrentRow = 0; CurrentRow < OutRows; ++CurrentRow)
			{
				float RowFixed = 0.f;
				float RowFlex = 0.f;
				for (const FEntry& Entry : Entries)
				{
					if (Entry.Row == CurrentRow)
					{
						(Entry.bFlexible ? RowFlex : RowFixed) += Entry.Width + Entry.Advance;
					}
				}
				if (RowFixed + RowFlex <= Available || RowFlex <= 0.f)
				{
					continue;
				}
				const float RowScale = FMath::Max((Available - RowFixed) / RowFlex, 0.05f);
				float RowX = 0.f;
				for (FEntry& Entry : Entries)
				{
					if (Entry.Row == CurrentRow)
					{
						if (Entry.bFlexible)
						{
							Entry.Width *= RowScale;
						}
						Entry.X = RowX;
						RowX += Entry.Width + Entry.Advance;
					}
				}
			}
		}

		/**
		 * Position in Entries where a dragged tab centred at the given point belongs, like Chrome: an entry changes
		 * sides once the tab covers half of it, from either direction. Entries are measured without the gap, so one
		 * still to the right of the tab really sits a tab-width further on, hence the extra half width.
		 */
		int32 FindGapPosition(const TArray<FEntry>& Entries, const FLayoutParams& Params, int32 Rows, float CenterX, float HalfDragWidth)
		{
			const int32 TargetRow = FMath::Clamp(Params.DragRow, 0, Rows - 1);
			for (int32 Position = 0; Position < Entries.Num(); ++Position)
			{
				const FEntry& Entry = Entries[Position];
				if (Entry.Row > TargetRow)
				{
					return Position;
				}
				if (Entry.Row == TargetRow && CenterX < Entry.X + Entry.Width * 0.5f + HalfDragWidth)
				{
					return Position;
				}
			}
			return Entries.Num();
		}

		/**
		 * The group the dragged tab belongs to at this moment, like Chrome: it is inside the group once it would land
		 * inside it, and it counts as arriving once it covers a third of the first thing it meets (the last tab or chip
		 * coming from the right, the chip coming from the left).
		 */
		int32 FindGroupUnderDrag(const TArray<FEntry>& Entries, const TArray<FLayoutItem>& Items, const FLayoutParams& Params, int32 Rows)
		{
			const bool bBlock = Params.DragHeldWidth > 0.f;
			const float ProbeX = bBlock ? Params.DragHeldCenterX : Params.DragCenterX;
			const float HeldWidth = bBlock ? Params.DragHeldWidth : Params.DragWidth;
			const float HeldLeft = ProbeX - HeldWidth * 0.5f;
			const float HeldRight = ProbeX + HeldWidth * 0.5f;
			const int32 TargetRow = FMath::Clamp(Params.DragRow, 0, Rows - 1);
			const int32 Landing = FindGapPosition(Entries, Params, Rows, ProbeX, HeldWidth * 0.5f);

			auto GroupOf = [&Items](const FEntry* Entry)
			{
				if (!Entry)
				{
					return static_cast<int32>(INDEX_NONE);
				}
				return Entry->Kind == EEntry::Chip ? Entry->Index : Items[Entry->Index].Group;
			};

			const FEntry* Before = Landing > 0 ? &Entries[Landing - 1] : nullptr;
			const FEntry* After = Entries.IsValidIndex(Landing) ? &Entries[Landing] : nullptr;
			const int32 GroupBefore = GroupOf(Before);
			const int32 GroupAfter = GroupOf(After);

			if (GroupBefore != INDEX_NONE && GroupBefore == GroupAfter)
			{
				return GroupBefore;
			}

			if (GroupBefore != INDEX_NONE && Before->Row == TargetRow)
			{
				float Right = Before->X + Before->Width;
				if (GroupBefore == Params.DragOwnGroup)
				{
					// The dragged tab isn't among the entries. Without this, a group's only tab would count as
					// outside its own group the moment it is picked up, and a small accidental drag would remove it.
					Right += HeldWidth;
				}
				if (HeldLeft <= Right - Before->Width / 3.f)
				{
					return GroupBefore;
				}
			}

			// Coming from the left: the group's chip hasn't hopped yet, so it sits a tab-width right of its measured X.
			if (GroupAfter != INDEX_NONE && After->Row == TargetRow && HeldRight >= After->X + HeldWidth + After->Width / 3.f)
			{
				return GroupAfter;
			}
			return INDEX_NONE;
		}

		/** Where a dragged group may land: never between another group's members, never before the pinned tabs. */
		int32 PlaceGroupDrag(const TArray<FEntry>& Entries, const TArray<FLayoutItem>& Items, const FLayoutParams& Params, int32 Landing)
		{
			auto GroupOf = [&Items](const FEntry& Entry)
			{
				return Entry.Kind == EEntry::Chip ? Entry.Index : Items[Entry.Index].Group;
			};

			if (Landing > 0 && Landing < Entries.Num() && Entries[Landing].Kind == EEntry::Tab)
			{
				const int32 Group = GroupOf(Entries[Landing - 1]);
				if (Group != INDEX_NONE && Group == GroupOf(Entries[Landing]))
				{
					int32 First = Landing - 1;
					while (First > 0 && GroupOf(Entries[First - 1]) == Group)
					{
						--First;
					}
					int32 Last = Landing;
					while (Last + 1 < Entries.Num() && GroupOf(Entries[Last + 1]) == Group)
					{
						++Last;
					}
					const float ToLeft = Params.DragCenterX - Entries[First].X;
					const float ToRight = Entries[Last].X + Entries[Last].Width - Params.DragCenterX;
					Landing = ToLeft <= ToRight ? First : Last + 1;
				}
			}

			for (int32 Position = Entries.Num() - 1; Position >= Landing; --Position)
			{
				if (Entries[Position].Kind == EEntry::Tab && Entries[Position].Index < Params.DragMinItem)
				{
					return Position + 1;
				}
			}
			return Landing;
		}

		int32 FirstItemOfGroup(const TArray<FLayoutItem>& Items, int32 Group)
		{
			return Items.IndexOfByPredicate([Group](const FLayoutItem& Item) { return Item.Group == Group; });
		}
	}

	FLayoutResult ComputeLayout(const TArray<FLayoutItem>& Items, const TArray<FLayoutGroup>& Groups, const FLayoutParams& Params)
	{
		FLayoutResult Result;
		Result.Items.SetNum(Items.Num());
		Result.Chips.SetNum(Groups.Num());

		TArray<FEntry> Entries;
		BuildEntries(Items, Groups, Params, Entries);

		int32 Rows = 1;
		if (Params.bHasDrag)
		{
			// First pass finds where the drag lands, second pass lays out with a gap there.
			TArray<FEntry> Probe = Entries;
			FitAndPlace(Probe, Params, Rows);
			int32 GapPosition = FindGapPosition(Probe, Params, Rows, Params.DragCenterX, Params.DragWidth * 0.5f);
			if (Params.bDragIsGroup)
			{
				GapPosition = PlaceGroupDrag(Probe, Items, Params, GapPosition);
			}
			else
			{
				Result.DropGroup = FindGroupUnderDrag(Probe, Items, Params, Rows);
			}

			Result.DropIndex = Items.Num();
			for (int32 Position = GapPosition; Position < Entries.Num(); ++Position)
			{
				if (Entries[Position].Kind == EEntry::Tab)
				{
					Result.DropIndex = Entries[Position].Index;
					break;
				}
				if (Entries[Position].Kind == EEntry::Chip)
				{
					const int32 First = FirstItemOfGroup(Items, Entries[Position].Index);
					if (First != INDEX_NONE)
					{
						Result.DropIndex = First;
						break;
					}
				}
			}

			FEntry Gap;
			Gap.Kind = EEntry::Gap;
			Gap.Width = Params.DragWidth;
			Entries.Insert(Gap, GapPosition);
		}

		FitAndPlace(Entries, Params, Rows);
		Result.NumRows = Rows;

		FPlacedRect GapRect;
		for (const FEntry& Entry : Entries)
		{
			FPlacedRect Placed;
			Placed.bVisible = true;
			Placed.X = Entry.X;
			Placed.Width = Entry.Width;
			Placed.Row = Entry.Row;
			switch (Entry.Kind)
			{
			case EEntry::Tab: Result.Items[Entry.Index] = Placed; break;
			case EEntry::Chip: Result.Chips[Entry.Index] = Placed; break;
			case EEntry::Gap:
				Result.DragX = Entry.X;
				Result.DragRowResolved = Entry.Row;
				GapRect = Placed;
				break;
			}
		}

		// One underline per group per row, from the chip to its last visible tab.
		for (int32 Group = 0; Group < Groups.Num(); ++Group)
		{
			TMap<int32, FUnderline> PerRow;
			auto Extend = [&](const FPlacedRect& Rect)
			{
				FUnderline* Line = PerRow.Find(Rect.Row);
				if (!Line)
				{
					FUnderline New;
					New.Group = Group;
					New.X = Rect.X;
					New.Width = Rect.Width;
					New.Row = Rect.Row;
					PerRow.Add(Rect.Row, New);
					return;
				}
				const float Right = FMath::Max(Line->X + Line->Width, Rect.X + Rect.Width);
				Line->X = FMath::Min(Line->X, Rect.X);
				Line->Width = Right - Line->X;
			};

			bool bAnyTab = false;
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				if (Items[Index].Group == Group && Result.Items[Index].bVisible)
				{
					Extend(Result.Items[Index]);
					bAnyTab = true;
				}
			}
			const bool bDropTarget = Params.bHasDrag && Result.DropGroup == Group;
			if (bAnyTab || bDropTarget)
			{
				Extend(Result.Chips[Group]);
				if (bDropTarget && GapRect.X >= Result.Chips[Group].X)
				{
					Extend(GapRect); // The line reaches the tab that is about to join.
				}
				for (const TPair<int32, FUnderline>& Pair : PerRow)
				{
					Result.Underlines.Add(Pair.Value);
				}
			}
		}

		return Result;
	}

	float EaseToward(float Current, float Target, float DeltaSeconds)
	{
		constexpr float TimeConstant = 0.06f;
		constexpr float LandingDistance = 0.5f;
		const float Next = Target + (Current - Target) * FMath::Exp(-FMath::Max(DeltaSeconds, 0.f) / TimeConstant);
		return (DeltaSeconds > 0.f && FMath::Abs(Next - Target) < LandingDistance) ? Target : Next;
	}

	TArray<int32> ComputeBlockDropOrder(int32 NumTabs, const TArray<int32>& Block, int32 Pivot)
	{
		TArray<int32> Order;
		Order.Reserve(NumTabs);
		for (int32 Index = 0; Index < NumTabs; ++Index)
		{
			if (Index == Pivot)
			{
				Order.Append(Block);
			}
			else if (!Block.Contains(Index))
			{
				Order.Add(Index);
			}
		}
		return Order;
	}

	TArray<int32> ComputeNormalizedOrder(const TArray<FOrderItem>& Items)
	{
		TArray<int32> Order;
		Order.Reserve(Items.Num());

		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (Items[Index].bHead)
			{
				Order.Add(Index);
			}
		}
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (!Items[Index].bHead && Items[Index].bPinned)
			{
				Order.Add(Index);
			}
		}

		TSet<int32> PlacedGroups;
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			const FOrderItem& Item = Items[Index];
			if (Item.bHead || Item.bPinned)
			{
				continue;
			}
			if (Item.Group == INDEX_NONE)
			{
				Order.Add(Index);
			}
			else if (!PlacedGroups.Contains(Item.Group))
			{
				PlacedGroups.Add(Item.Group);
				for (int32 Member = Index; Member < Items.Num(); ++Member)
				{
					if (Items[Member].Group == Item.Group && !Items[Member].bHead && !Items[Member].bPinned)
					{
						Order.Add(Member);
					}
				}
			}
		}
		return Order;
	}

}
