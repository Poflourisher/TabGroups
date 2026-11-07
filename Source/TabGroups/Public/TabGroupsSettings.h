// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TabGroupsSettings.generated.h"

/** Editor Preferences > Plugins > Tab Groups. */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Tab Groups"))
class TABGROUPS_API UTabGroupsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Tabs are only as wide as their name, instead of all sharing one width. */
	UPROPERTY(config, EditAnywhere, Category = "Tabs")
	bool bCompactTabs = true;

	/** Tabs stop shrinking at this width and wrap to the next row instead, when more rows are allowed. */
	UPROPERTY(config, EditAnywhere, Category = "Tabs", meta = (ClampMin = "40", ClampMax = "200"))
	float MinTabWidth = 90.f;

	/** How many rows a tab bar may use. Set to 1 to never wrap. */
	UPROPERTY(config, EditAnywhere, Category = "Tabs", meta = (ClampMin = "1", ClampMax = "2"))
	int32 MaxRows = 2;

	/** Also allow the top row of asset tabs to wrap. It takes height away from the editor below it. */
	UPROPERTY(config, EditAnywhere, Category = "Tabs")
	bool bWrapTopRowTabs = true;

	/** Bring groups and pinned tabs back when the project reopens. */
	UPROPERTY(config, EditAnywhere, Category = "Groups")
	bool bRememberGroups = true;

	/** Reopen the assets of expanded groups at startup. Assets in collapsed groups open when you expand the group. */
	UPROPERTY(config, EditAnywhere, Category = "Groups", meta = (EditCondition = "bRememberGroups"))
	bool bReopenGroupedAssets = true;
};
