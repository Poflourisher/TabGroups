// Copyright 2026 Jot. All Rights Reserved.

#include "STabGroupChipLayer.h"
#include "TabGroupsColors.h"
#include "TabGroupsManager.h"
#include "TabWellAccess.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TabGroups"

namespace
{
	constexpr float ChipVerticalInset = 3.f;
	constexpr float UnderlineThickness = 2.f;
	constexpr float OutlineTopRadius = 6.f;
	constexpr float OutlineFootRadius = 6.f;
	constexpr float OutlineFadeSeconds = 0.07f;

	/** Adds points along an arc. Angles are in degrees, clockwise on screen, with 0 pointing right. */
	void AddArc(TArray<FVector2f>& Points, const FVector2f& Center, float Radius, float StartDegrees, float EndDegrees)
	{
		constexpr int32 Steps = 8;
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Radians = FMath::DegreesToRadians(FMath::Lerp(StartDegrees, EndDegrees, static_cast<float>(Step) / Steps));
			Points.Add(Center + FVector2f(FMath::Cos(Radians), FMath::Sin(Radians)) * Radius);
		}
	}
}

void STabGroupChip::Construct(const FArguments&, const FGuid& InGroupId)
{
	GroupId = InGroupId;
	SetToolTipText(TAttribute<FText>::CreateSP(this, &STabGroupChip::GetTooltip));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(&Brush)
		.BorderBackgroundColor(this, &STabGroupChip::GetBackground)
		.Padding(FMargin(8.f, 0.f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &STabGroupChip::GetLabel)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity_Lambda([this]()
			{
				const FTabGroupsManager* Manager = FTabGroupsManager::Get();
				const TSharedPtr<FTabGroup> Group = Manager ? Manager->FindGroup(GroupId) : nullptr;
				return FSlateColor(TabGroups::GetChipTextColorFor(Group.IsValid() ? TabGroups::GetGroupDisplayColor(Group->Color, Group->CustomColor) : TabGroups::GetGroupColor(0)));
			})
		]
	];
}

FText STabGroupChip::GetChipText(const FTabGroup& Group)
{
	const int32 Count = Group.Tabs.Num() + Group.UnopenedKeys.Num();
	if (Group.bCollapsed)
	{
		return Group.Name.IsEmpty()
			? FText::AsNumber(Count)
			: FText::Format(LOCTEXT("CollapsedChip", "{0} · {1}"), FText::FromString(Group.Name), Count);
	}
	return FText::FromString(Group.Name);
}

FText STabGroupChip::GetLabel() const
{
	const FTabGroupsManager* Manager = FTabGroupsManager::Get();
	const TSharedPtr<FTabGroup> Group = Manager ? Manager->FindGroup(GroupId) : nullptr;
	return Group.IsValid() ? GetChipText(*Group) : FText::GetEmpty();
}

FText STabGroupChip::GetTooltip() const
{
	const FTabGroupsManager* Manager = FTabGroupsManager::Get();
	const TSharedPtr<FTabGroup> Group = Manager ? Manager->FindGroup(GroupId) : nullptr;
	if (!Group.IsValid())
	{
		return FText::GetEmpty();
	}
	FString Lines = Group->Name.IsEmpty() ? LOCTEXT("UnnamedGroup", "Unnamed group").ToString() : Group->Name;
	for (const TWeakPtr<SDockTab>& Weak : Group->Tabs)
	{
		if (const TSharedPtr<SDockTab> Tab = Weak.Pin())
		{
			Lines += TEXT("\n  ") + Tab->GetTabLabel().ToString();
		}
	}
	for (const FString& Key : Group->UnopenedKeys)
	{
		Lines += TEXT("\n  ") + FPaths::GetBaseFilename(Key);
	}
	return FText::FromString(Lines);
}

FSlateColor STabGroupChip::GetBackground() const
{
	const FTabGroupsManager* Manager = FTabGroupsManager::Get();
	const TSharedPtr<FTabGroup> Group = Manager ? Manager->FindGroup(GroupId) : nullptr;
	FLinearColor Color = Group.IsValid() ? TabGroups::GetGroupDisplayColor(Group->Color, Group->CustomColor) : TabGroups::GetGroupColor(0);
	if (IsHovered())
	{
		Color = FLinearColor::LerpUsingHSV(Color, FLinearColor::White, 0.2f);
	}
	return FSlateColor(Color);
}

FReply STabGroupChip::OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event)
{
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton || Event.GetEffectingButton() == EKeys::RightMouseButton)
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STabGroupChip::OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event)
{
	FTabGroupsManager* Manager = FTabGroupsManager::Get();
	if (!Manager)
	{
		return FReply::Unhandled();
	}
	if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (const TSharedPtr<FTabGroup> Group = Manager->FindGroup(GroupId))
		{
			Manager->SetCollapsed(GroupId, !Group->bCollapsed);
		}
		return FReply::Handled();
	}
	if (Event.GetEffectingButton() == EKeys::RightMouseButton)
	{
		Manager->OpenGroupEditor(GroupId);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

STabGroupChipLayer::STabGroupChipLayer()
	: Chips(this)
{
}

void STabGroupChipLayer::Construct(const FArguments&, const SDockingTabWell* InWell)
{
	Well = InWell;
	SetVisibility(EVisibility::SelfHitTestInvisible);
}

void STabGroupChipLayer::Sync()
{
	const FTabGroupsManager* Manager = FTabGroupsManager::Get();
	const FTabWellState* State = Manager ? Manager->FindWellState(Well) : nullptr;
	if (!State)
	{
		return;
	}

	bool bSame = Chips.Num() == State->Groups.Num();
	for (int32 Index = 0; bSame && Index < Chips.Num(); ++Index)
	{
		bSame = Chips[Index]->GetGroupId() == State->Groups[Index]->Id;
	}
	if (bSame)
	{
		return;
	}

	Chips.Empty();
	for (const TSharedRef<FTabGroup>& Group : State->Groups)
	{
		Chips.Add(SNew(STabGroupChip, Group->Id));
	}
}

FVector2D STabGroupChipLayer::WellToLayer(const FGeometry& Geometry, const FGeometry& WellGeometry, const FVector2D& Point) const
{
	return FVector2D(Geometry.AbsoluteToLocal(WellGeometry.LocalToAbsolute(FVector2f(Point))));
}

void STabGroupChipLayer::OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Out) const
{
	const FTabGroupsManager* Manager = FTabGroupsManager::Get();
	const FTabWellState* State = Manager ? Manager->FindWellState(Well) : nullptr;
	if (!State || State->Layout.Chips.Num() != Chips.Num())
	{
		return;
	}
	for (int32 Index = 0; Index < Chips.Num(); ++Index)
	{
		const TabGroups::FPlacedRect& Rect = State->Layout.Chips[Index];
		if (!Rect.bVisible)
		{
			continue;
		}
		const FVector2D TopLeft(Rect.X, Rect.Row * State->RowHeight + ChipVerticalInset);
		const FVector2D Position = WellToLayer(Geometry, State->Geometry, TopLeft);
		const FVector2D Size(Rect.Width, FMath::Max(State->RowHeight - ChipVerticalInset * 2.f, 8.f));
		Out.AddWidget(Geometry.MakeChild(Chips[Index], Position, Size));
	}
}

int32 STabGroupChipLayer::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	int32 MaxLayer = SPanel::OnPaint(Args, Geometry, CullingRect, OutDrawElements, LayerId, Style, bParentEnabled);

	const FTabGroupsManager* Manager = FTabGroupsManager::Get();
	const FTabWellState* State = Manager ? Manager->FindWellState(Well) : nullptr;
	if (!State)
	{
		return MaxLayer;
	}

	++MaxLayer;
	const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");

	// The tab wearing its group's outline: the one being dragged (in the colour of the group it is over),
	// otherwise the active tab if it is in a group.
	TSharedPtr<SDockTab> OutlineTab;
	int32 OutlineGroup = INDEX_NONE;
	if (State->Well.IsValid())
	{
		const TSharedPtr<SDockTab> Dragged = TabWellAccess::DraggedTab(*Well);
		if (Dragged.IsValid())
		{
			OutlineTab = Dragged;
			OutlineGroup = State->DropGroup;
		}
		else
		{
			const TSlotlessChildren<SDockTab>& Tabs = TabWellAccess::Tabs(*Well);
			const int32 Foreground = TabWellAccess::ForegroundIndex(*Well);
			if (Foreground >= 0 && Foreground < Tabs.Num())
			{
				const int32* Group = State->TabToGroup.Find(&Tabs[Foreground].Get());
				if (Group && State->Groups.IsValidIndex(*Group) && !State->Groups[*Group]->bCollapsed)
				{
					OutlineTab = Tabs[Foreground];
					OutlineGroup = *Group;
				}
			}
		}
	}
	if (!State->Groups.IsValidIndex(OutlineGroup))
	{
		OutlineGroup = INDEX_NONE;
	}

	float OutlineLeft = 0.f, OutlineRight = 0.f, OutlineTop = 0.f, OutlineBottom = 0.f;
	if (OutlineTab.IsValid())
	{
		const FGeometry& TabGeometry = OutlineTab->GetPaintSpaceGeometry();
		const FVector2f TabPosition = Geometry.AbsoluteToLocal(TabGeometry.GetAbsolutePosition());
		const FVector2f TabSize = TabGeometry.GetAbsoluteSize() / Geometry.Scale;
		const float Inset = OutlineTab->GetOverlapWidth() * 0.5f;
		OutlineLeft = TabPosition.X + Inset;
		OutlineRight = TabPosition.X + TabSize.X - Inset;
		OutlineTop = TabPosition.Y + 1.f;
		OutlineBottom = TabPosition.Y + TabSize.Y - UnderlineThickness * 0.5f;
		if (TabSize.X <= 1.f)
		{
			OutlineTab.Reset(); // Not laid out yet.
			OutlineGroup = INDEX_NONE;
		}
	}

	auto DrawLine = [&](float Left, float Right, float Y, const FLinearColor& Color)
	{
		if (Right > Left)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, MaxLayer, Geometry.ToPaintGeometry(FVector2f(Right - Left, UnderlineThickness), FSlateLayoutTransform(FVector2f(Left, Y))), White, ESlateDrawEffect::None, Color);
		}
	};

	for (const TabGroups::FUnderline& Line : State->Layout.Underlines)
	{
		if (!State->Groups.IsValidIndex(Line.Group))
		{
			continue;
		}
		const FVector2D TopLeft(Line.X, (Line.Row + 1) * State->RowHeight - UnderlineThickness);
		const FVector2D Position = WellToLayer(Geometry, State->Geometry, TopLeft);
		const FLinearColor Color = TabGroups::GetGroupDisplayColor(State->Groups[Line.Group]->Color, State->Groups[Line.Group]->CustomColor);
		const float Left = static_cast<float>(Position.X);
		const float Right = Left + Line.Width;
		const float Y = static_cast<float>(Position.Y);
		const bool bSameRow = FMath::Abs(Y + UnderlineThickness * 0.5f - OutlineBottom) < State->RowHeight * 0.5f;
		if (Line.Group == OutlineGroup && bSameRow)
		{
			// The line stops at the outline's feet; the outline carries the colour up around the tab.
			DrawLine(Left, FMath::Min(Right, OutlineLeft - OutlineFootRadius), Y, Color);
			DrawLine(FMath::Max(Left, OutlineRight + OutlineFootRadius), Right, Y, Color);
		}
		else
		{
			DrawLine(Left, Right, Y, Color);
		}
	}

	if (OutlineTab.IsValid())
	{
		const FLinearColor Target = OutlineGroup != INDEX_NONE
			? TabGroups::GetGroupDisplayColor(State->Groups[OutlineGroup]->Color, State->Groups[OutlineGroup]->CustomColor)
			: OutlineColor.CopyWithNewOpacity(0.f);
		const double Now = Args.GetCurrentTime();
		const float Blend = 1.f - FMath::Exp(-FMath::Max(static_cast<float>(Now - LastOutlineTime), 0.f) / OutlineFadeSeconds);
		LastOutlineTime = Now;
		OutlineColor = OutlineColor.A < 0.02f ? Target.CopyWithNewOpacity(FMath::Lerp(0.f, Target.A, Blend)) : FMath::Lerp(OutlineColor, Target, Blend);

		if (OutlineColor.A >= 0.02f)
		{
			const float Top = FMath::Min(OutlineTopRadius, (OutlineRight - OutlineLeft) * 0.5f);
			TArray<FVector2f> Points;
			AddArc(Points, FVector2f(OutlineLeft - OutlineFootRadius, OutlineBottom - OutlineFootRadius), OutlineFootRadius, 90.f, 0.f);
			AddArc(Points, FVector2f(OutlineLeft + Top, OutlineTop + Top), Top, 180.f, 270.f);
			AddArc(Points, FVector2f(OutlineRight - Top, OutlineTop + Top), Top, 270.f, 360.f);
			AddArc(Points, FVector2f(OutlineRight + OutlineFootRadius, OutlineBottom - OutlineFootRadius), OutlineFootRadius, 180.f, 90.f);
			FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, OutlineColor, true, UnderlineThickness);
		}
	}
	else
	{
		OutlineColor.A = 0.f;
		LastOutlineTime = Args.GetCurrentTime();
	}

	// Ctrl/Shift-selected tabs: a light wash and a blue line along the top, like Chrome's selected tabs.
	const FLinearColor SelectionWash(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor SelectionLine = TabGroups::GetGroupColor(1);
	for (const TSharedRef<SDockTab>& Tab : Manager->GetSelectedTabs())
	{
		if (Tab->GetParent().Get() != Well)
		{
			continue;
		}
		const int32* Group = State->TabToGroup.Find(&Tab.Get());
		if (Group && State->Groups.IsValidIndex(*Group) && State->Groups[*Group]->bCollapsed && !Tab->IsForeground())
		{
			continue; // Hidden inside a collapsed group.
		}
		const FGeometry& TabGeometry = Tab->GetPaintSpaceGeometry();
		const FVector2f Position = Geometry.AbsoluteToLocal(TabGeometry.GetAbsolutePosition());
		const FVector2f Size = TabGeometry.GetAbsoluteSize() / Geometry.Scale;
		FSlateDrawElement::MakeBox(OutDrawElements, MaxLayer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), White, ESlateDrawEffect::None, SelectionWash);
		if (Tab != OutlineTab || OutlineGroup == INDEX_NONE)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, MaxLayer, Geometry.ToPaintGeometry(FVector2f(Size.X, 2.f), FSlateLayoutTransform(Position)), White, ESlateDrawEffect::None, SelectionLine);
		}
	}
	return MaxLayer;
}

#undef LOCTEXT_NAMESPACE
