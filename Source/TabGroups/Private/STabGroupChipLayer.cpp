// Copyright 2026 Jot. All Rights Reserved.

#include "STabGroupChipLayer.h"
#include "TabGroupsColors.h"
#include "TabGroupsManager.h"
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
	for (const TabGroups::FUnderline& Line : State->Layout.Underlines)
	{
		if (!State->Groups.IsValidIndex(Line.Group))
		{
			continue;
		}
		const FVector2D TopLeft(Line.X, (Line.Row + 1) * State->RowHeight - UnderlineThickness);
		const FVector2D Position = WellToLayer(Geometry, State->Geometry, TopLeft);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			MaxLayer,
			Geometry.ToPaintGeometry(FVector2f(Line.Width, UnderlineThickness), FSlateLayoutTransform(FVector2f(Position))),
			White,
			ESlateDrawEffect::None,
			TabGroups::GetGroupDisplayColor(State->Groups[Line.Group]->Color, State->Groups[Line.Group]->CustomColor));
	}
	return MaxLayer;
}

#undef LOCTEXT_NAMESPACE
