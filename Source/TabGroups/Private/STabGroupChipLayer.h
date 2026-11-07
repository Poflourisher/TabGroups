// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Layout/Children.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SPanel.h"

class SDockingTabWell;
class STextBlock;

/** The coloured pill for one group. Left click collapses or expands, right click edits. */
class STabGroupChip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STabGroupChip) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FGuid& InGroupId);

	const FGuid& GetGroupId() const { return GroupId; }

	static FText GetChipText(const struct FTabGroup& Group);

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual EWindowZone::Type GetWindowZoneOverride() const override { return EWindowZone::ClientArea; }

private:
	FSlateColor GetBackground() const;
	FText GetLabel() const;
	FText GetTooltip() const;

	FGuid GroupId;
	FSlateRoundedBoxBrush Brush = FSlateRoundedBoxBrush(FLinearColor::White, 6.f);
};

/**
 * Sits on top of a tab well and draws its group chips and underlines. The well leaves gaps for
 * the chips in its layout; this layer fills them. It sits in the tab bar's own overlay and lets
 * every click through except on the chips.
 */
class STabGroupChipLayer : public SPanel
{
public:
	SLATE_BEGIN_ARGS(STabGroupChipLayer) {}
	SLATE_END_ARGS()

	STabGroupChipLayer();
	void Construct(const FArguments& InArgs, const SDockingTabWell* InWell);

	/** Matches the chip widgets to the well's groups. */
	void Sync();

	virtual void OnArrangeChildren(const FGeometry& Geometry, FArrangedChildren& Out) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual FChildren* GetChildren() override { return &Chips; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	FVector2D WellToLayer(const FGeometry& Geometry, const FGeometry& WellGeometry, const FVector2D& Point) const;

	const SDockingTabWell* Well = nullptr;
	TSlotlessChildren<STabGroupChip> Chips;
};
