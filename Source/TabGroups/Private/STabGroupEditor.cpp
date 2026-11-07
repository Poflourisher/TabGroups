// Copyright 2026 Jot. All Rights Reserved.

#include "STabGroupEditor.h"
#include "TabGroupsColors.h"
#include "TabGroupsManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TabGroups"

namespace
{
	const FSlateBrush* CircleBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 9.f);
		return &Brush;
	}

	const FSlateBrush* RingBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, 11.f, FLinearColor::White, 1.5f);
		return &Brush;
	}
}

void STabGroupEditor::Construct(const FArguments&, const FGuid& InGroupId)
{
	GroupId = InGroupId;

	FTabGroupsManager* Manager = FTabGroupsManager::Get();
	const TSharedPtr<FTabGroup> Group = Manager ? Manager->FindGroup(GroupId) : nullptr;
	const FString Name = Group.IsValid() ? Group->Name : FString();

	TSharedRef<SHorizontalBox> Swatches = SNew(SHorizontalBox);
	for (int32 ColorIndex = 0; ColorIndex < TabGroups::NumGroupColors; ++ColorIndex)
	{
		Swatches->AddSlot().AutoWidth().Padding(1.f)[MakeSwatch(ColorIndex)];
	}

	const FGuid Id = GroupId;
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(10.f)
		[
			SNew(SBox)
			.WidthOverride(250.f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SAssignNew(NameBox, SEditableTextBox)
					.Text(FText::FromString(Name))
					.HintText(LOCTEXT("NameHint", "Name this group"))
					.SelectAllTextWhenFocused(true)
					.OnTextChanged_Lambda([Id](const FText& Text)
					{
						if (FTabGroupsManager* Current = FTabGroupsManager::Get())
						{
							Current->SetGroupName(Id, Text.ToString());
						}
					})
					.OnTextCommitted_Lambda([](const FText&, ETextCommit::Type Commit)
					{
						if (Commit == ETextCommit::OnEnter)
						{
							FSlateApplication::Get().DismissAllMenus();
						}
					})
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					Swatches
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("Ungroup", "Ungroup"))
						.ToolTipText(LOCTEXT("UngroupTip", "Keep the tabs open and remove the group"))
						.OnClicked_Lambda([Id]()
						{
							FSlateApplication::Get().DismissAllMenus();
							if (FTabGroupsManager* Current = FTabGroupsManager::Get())
							{
								Current->Ungroup(Id);
							}
							return FReply::Handled();
						})
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("CloseGroup", "Close group"))
						.ToolTipText(LOCTEXT("CloseGroupTip", "Close every tab in this group"))
						.OnClicked_Lambda([Id]()
						{
							FSlateApplication::Get().DismissAllMenus();
							if (FTabGroupsManager* Current = FTabGroupsManager::Get())
							{
								Current->CloseGroup(Id);
							}
							return FReply::Handled();
						})
					]
				]
			]
		]
	];
}

TSharedRef<SWidget> STabGroupEditor::MakeSwatch(int32 ColorIndex)
{
	const FGuid Id = GroupId;
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(0.f)
		.ToolTipText(TabGroups::GetGroupColorName(ColorIndex))
		.OnClicked_Lambda([Id, ColorIndex]()
		{
			if (FTabGroupsManager* Current = FTabGroupsManager::Get())
			{
				Current->SetGroupColor(Id, ColorIndex);
			}
			return FReply::Handled();
		})
		[
			SNew(SBox)
			.WidthOverride(24.f)
			.HeightOverride(24.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(18.f)
					.HeightOverride(18.f)
					[
						SNew(SImage)
						.Image(CircleBrush())
						.ColorAndOpacity(TabGroups::GetGroupColor(ColorIndex))
					]
				]

				+ SOverlay::Slot()
				[
					SNew(SBox)
					.WidthOverride(22.f)
					.HeightOverride(22.f)
					[
						SNew(SImage)
						.Image(RingBrush())
						.Visibility_Lambda([Id, ColorIndex]()
						{
							const FTabGroupsManager* Current = FTabGroupsManager::Get();
							const TSharedPtr<FTabGroup> Group = Current ? Current->FindGroup(Id) : nullptr;
							return Group.IsValid() && Group->Color == ColorIndex ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						})
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
