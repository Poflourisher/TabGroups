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
#include "Widgets/Colors/SColorPicker.h"

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
	Swatches->AddSlot().AutoWidth().Padding(1.f)[MakeCustomSwatch()];

	const FGuid Id = GroupId;
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(10.f)
		[
			SNew(SBox)
			.WidthOverride(330.f)
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
						.ToolTipText(LOCTEXT("CloseGroupTip", "Close the tabs and keep the group in the Tab Groups menu, to reopen later"))
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

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("DeleteGroup", "Delete group"))
						.ToolTipText(LOCTEXT("DeleteGroupTip", "Close the tabs and forget the group"))
						.OnClicked_Lambda([Id]()
						{
							FSlateApplication::Get().DismissAllMenus();
							if (FTabGroupsManager* Current = FTabGroupsManager::Get())
							{
								Current->DeleteGroup(Id);
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
							return Group.IsValid() && Group->CustomColor.IsEmpty() && Group->Color == ColorIndex ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						})
					]
				]
			]
		];
}

TSharedRef<SWidget> STabGroupEditor::MakeCustomSwatch()
{
	const FGuid Id = GroupId;
	auto CurrentCustom = [Id]() -> TOptional<FLinearColor>
	{
		const FTabGroupsManager* Current = FTabGroupsManager::Get();
		const TSharedPtr<FTabGroup> Group = Current ? Current->FindGroup(Id) : nullptr;
		if (Group.IsValid() && !Group->CustomColor.IsEmpty())
		{
			return TabGroups::GetGroupDisplayColor(Group->Color, Group->CustomColor);
		}
		return {};
	};

	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(0.f)
		.ToolTipText(LOCTEXT("MoreColours", "More colours: pick any colour on the colour wheel"))
		.OnClicked_Lambda([Id]()
		{
			FTabGroupsManager* Current = FTabGroupsManager::Get();
			const TSharedPtr<FTabGroup> Group = Current ? Current->FindGroup(Id) : nullptr;
			if (!Group.IsValid())
			{
				return FReply::Handled();
			}
			// Remember what to go back to if the picker is cancelled.
			const int32 OldPalette = Group->Color;
			const FString OldCustom = Group->CustomColor;

			FColorPickerArgs Args(TabGroups::GetGroupDisplayColor(Group->Color, Group->CustomColor),
				FOnLinearColorValueChanged::CreateLambda([Id](FLinearColor Picked)
				{
					if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
					{
						Manager->SetGroupCustomColor(Id, Picked);
					}
				}));
			Args.bUseAlpha = false;
			Args.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda([Id, OldPalette, OldCustom](FLinearColor)
			{
				if (FTabGroupsManager* Manager = FTabGroupsManager::Get())
				{
					Manager->SetGroupColor(Id, OldPalette);
					if (!OldCustom.IsEmpty())
					{
						Manager->SetGroupCustomColor(Id, FLinearColor(FColor::FromHex(OldCustom)));
					}
				}
			});
			FSlateApplication::Get().DismissAllMenus();
			OpenColorPicker(Args);
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

				// The current custom colour, or a neutral disc with a plus when there is none yet.
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
						.ColorAndOpacity_Lambda([CurrentCustom]()
						{
							return FSlateColor(CurrentCustom().Get(FLinearColor(FColor(0x5F, 0x63, 0x68))));
						})
					]
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PlusSign", "+"))
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
					.Visibility_Lambda([CurrentCustom]() { return CurrentCustom().IsSet() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				]

				+ SOverlay::Slot()
				[
					SNew(SBox)
					.WidthOverride(22.f)
					.HeightOverride(22.f)
					[
						SNew(SImage)
						.Image(RingBrush())
						.Visibility_Lambda([CurrentCustom]() { return CurrentCustom().IsSet() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
