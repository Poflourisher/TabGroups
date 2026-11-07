// Copyright 2026 Jot. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TabGroupsState.h"

namespace TabGroups
{
	/** Chrome's dark theme group colours: grey, blue, red, yellow, green, pink, purple, cyan, orange. */
	inline FLinearColor GetGroupColor(int32 Index)
	{
		static const FColor Colors[NumGroupColors] =
		{
			FColor(0xDA, 0xDC, 0xE0), FColor(0x8A, 0xB4, 0xF8), FColor(0xF2, 0x8B, 0x82),
			FColor(0xFD, 0xD6, 0x63), FColor(0x81, 0xC9, 0x95), FColor(0xFF, 0x8B, 0xCB),
			FColor(0xC5, 0x8A, 0xF9), FColor(0x78, 0xD9, 0xEC), FColor(0xFC, 0xAD, 0x70),
		};
		return FLinearColor(Colors[FMath::Clamp(Index, 0, NumGroupColors - 1)]);
	}

	inline FText GetGroupColorName(int32 Index)
	{
		static const TCHAR* Names[NumGroupColors] = { TEXT("Grey"), TEXT("Blue"), TEXT("Red"), TEXT("Yellow"), TEXT("Green"), TEXT("Pink"), TEXT("Purple"), TEXT("Cyan"), TEXT("Orange") };
		return FText::FromString(Names[FMath::Clamp(Index, 0, NumGroupColors - 1)]);
	}

	/** Dark text that reads on every palette colour. */
	inline FLinearColor GetChipTextColor()
	{
		return FLinearColor(FColor(0x20, 0x21, 0x24));
	}

	/** A group's colour: the colour-wheel pick (hex RRGGBB) when there is one, else the palette entry. */
	inline FLinearColor GetGroupDisplayColor(int32 PaletteIndex, const FString& CustomColor)
	{
		return CustomColor.Len() == 6 ? FLinearColor(FColor::FromHex(CustomColor)) : GetGroupColor(PaletteIndex);
	}

	/** Dark text on light colours, white text on dark ones (colour-wheel picks can be either). */
	inline FLinearColor GetChipTextColorFor(const FLinearColor& Background)
	{
		const float Luminance = 0.2126f * Background.R + 0.7152f * Background.G + 0.0722f * Background.B;
		return Luminance > 0.18f ? GetChipTextColor() : FLinearColor(FColor(0xF1, 0xF3, 0xF4));
	}

	inline FString ToHexColor(const FLinearColor& Color)
	{
		return Color.ToFColor(true).ToHex().Left(6);
	}
}
