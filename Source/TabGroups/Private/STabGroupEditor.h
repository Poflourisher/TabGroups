// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;

/** The popup for renaming and colouring a group, like Chrome's group bubble. */
class STabGroupEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STabGroupEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FGuid& InGroupId);

	TSharedPtr<SEditableTextBox> GetNameBox() const { return NameBox; }

private:
	TSharedRef<SWidget> MakeSwatch(int32 ColorIndex);
	TSharedRef<SWidget> MakeCustomSwatch();

	FGuid GroupId;
	TSharedPtr<SEditableTextBox> NameBox;
};
