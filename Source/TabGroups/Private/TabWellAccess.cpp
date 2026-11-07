// Copyright 2026 Jot. All Rights Reserved.

#include "TabWellAccess.h"

namespace
{
	// Explicit template instantiation may legally name private members; the friend function hands the pointer out.
	template <typename Tag, typename Tag::Type Member>
	struct TExpose
	{
		friend typename Tag::Type Exposed(Tag) { return Member; }
	};

	struct FTabsTag { using Type = TSlotlessChildren<SDockTab> SDockingTabWell::*; friend Type Exposed(FTabsTag); };
	struct FDraggedTag { using Type = TSharedPtr<SDockTab> SDockingTabWell::*; friend Type Exposed(FDraggedTag); };
	struct FOffsetTag { using Type = float SDockingTabWell::*; friend Type Exposed(FOffsetTag); };
	struct FForegroundTag { using Type = int32 SDockingTabWell::*; friend Type Exposed(FForegroundTag); };
	struct FGrabTag { using Type = FVector2D SDockingTabWell::*; friend Type Exposed(FGrabTag); };
	struct FMenuTag { using Type = SDockTab::FExtendContextMenu SDockTab::*; friend Type Exposed(FMenuTag); };
	struct FNameHiddenTag { using Type = void (SDockTab::*)(bool); friend Type Exposed(FNameHiddenTag); };

	template struct TExpose<FTabsTag, &SDockingTabWell::Tabs>;
	template struct TExpose<FDraggedTag, &SDockingTabWell::TabBeingDraggedPtr>;
	template struct TExpose<FOffsetTag, &SDockingTabWell::ChildBeingDraggedOffset>;
	template struct TExpose<FForegroundTag, &SDockingTabWell::ForegroundTabIndex>;
	template struct TExpose<FGrabTag, &SDockingTabWell::TabGrabOffsetFraction>;
	template struct TExpose<FMenuTag, &SDockTab::OnExtendContextMenu>;
	template struct TExpose<FNameHiddenTag, &SDockTab::SetIsTabNameHidden>;

	template <typename Tag>
	auto& Field(const SDockingTabWell& Well)
	{
		return const_cast<SDockingTabWell&>(Well).*Exposed(Tag{});
	}
}

namespace TabWellAccess
{
	TSlotlessChildren<SDockTab>& Tabs(const SDockingTabWell& Well) { return Field<FTabsTag>(Well); }
	TSharedPtr<SDockTab>& DraggedTab(const SDockingTabWell& Well) { return Field<FDraggedTag>(Well); }
	float& DragOffset(const SDockingTabWell& Well) { return Field<FOffsetTag>(Well); }
	int32& ForegroundIndex(const SDockingTabWell& Well) { return Field<FForegroundTag>(Well); }
	FVector2D& GrabFraction(const SDockingTabWell& Well) { return Field<FGrabTag>(Well); }
	SDockTab::FExtendContextMenu& ContextMenuDelegate(SDockTab& Tab) { return Tab.*Exposed(FMenuTag{}); }
	void SetTabNameHidden(SDockTab& Tab, bool bHidden) { (Tab.*Exposed(FNameHiddenTag{}))(bHidden); }

	static void RestoreForeground(const SDockingTabWell& Well, const TSharedPtr<SDockTab>& Foreground)
	{
		if (Foreground.IsValid())
		{
			ForegroundIndex(Well) = Tabs(Well).Find(Foreground.ToSharedRef());
		}
	}

	static TSharedPtr<SDockTab> CurrentForeground(const SDockingTabWell& Well)
	{
		TSlotlessChildren<SDockTab>& List = Tabs(Well);
		const int32 Index = ForegroundIndex(Well);
		return (Index >= 0 && Index < List.Num()) ? TSharedPtr<SDockTab>(List[Index]) : TSharedPtr<SDockTab>();
	}

	void MoveTab(const SDockingTabWell& Well, const TSharedRef<SDockTab>& Tab, int32 NewIndex)
	{
		TSlotlessChildren<SDockTab>& List = Tabs(Well);
		if (List.Find(Tab) == INDEX_NONE)
		{
			return;
		}
		const TSharedPtr<SDockTab> Foreground = CurrentForeground(Well);
		List.Remove(Tab);
		List.Insert(Tab, FMath::Clamp(NewIndex, 0, List.Num()));
		RestoreForeground(Well, Foreground);
	}

	void SetOrder(const SDockingTabWell& Well, const TArray<TSharedRef<SDockTab>>& NewOrder)
	{
		TSlotlessChildren<SDockTab>& List = Tabs(Well);
		const TSharedPtr<SDockTab> Foreground = CurrentForeground(Well);
		for (int32 Index = 0; Index < NewOrder.Num() && Index < List.Num(); ++Index)
		{
			if (List[Index] != NewOrder[Index])
			{
				List.Remove(NewOrder[Index]);
				List.Insert(NewOrder[Index], Index);
			}
		}
		RestoreForeground(Well, Foreground);
	}
}
