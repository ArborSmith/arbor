#pragma once

#include "CoreMinimal.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateColor.h"

/**
 * Centralized color palette, typography, and spacing constants for all GameCodex widgets.
 * Header-only — no registration required.
 */
namespace ArborCodexStyle
{
	// ═══════════════════════════════════════════════════
	// COLOR PALETTE
	// ═══════════════════════════════════════════════════

	namespace Bg
	{
		static const FLinearColor Panel      = FLinearColor(0.012f, 0.012f, 0.016f, 1.0f);
		static const FLinearColor Surface    = FLinearColor(0.035f, 0.038f, 0.050f, 1.0f);
		static const FLinearColor SurfaceAlt = FLinearColor(0.045f, 0.048f, 0.060f, 1.0f);
		static const FLinearColor Elevated   = FLinearColor(0.055f, 0.060f, 0.078f, 1.0f);
		static const FLinearColor Input      = FLinearColor(0.028f, 0.030f, 0.042f, 1.0f);
		static const FLinearColor TabBar     = FLinearColor(0.020f, 0.022f, 0.030f, 1.0f);
	}

	namespace Accent
	{
		static const FLinearColor Primary     = FLinearColor(0.20f, 0.40f, 0.85f, 1.0f);
		static const FLinearColor PrimaryDim  = FLinearColor(0.12f, 0.22f, 0.50f, 1.0f);
		static const FLinearColor PrimaryFaint= FLinearColor(0.08f, 0.14f, 0.32f, 0.4f);
		static const FLinearColor Hover       = FLinearColor(0.25f, 0.48f, 0.92f, 1.0f);
		static const FLinearColor AI          = FLinearColor(0.55f, 0.35f, 0.85f, 1.0f);
		static const FLinearColor AIDim       = FLinearColor(0.30f, 0.18f, 0.50f, 0.6f);
	}

	namespace Text
	{
		static const FLinearColor Primary   = FLinearColor(0.88f, 0.90f, 0.95f, 1.0f);
		static const FLinearColor Secondary = FLinearColor(0.55f, 0.58f, 0.65f, 1.0f);
		static const FLinearColor Muted     = FLinearColor(0.38f, 0.40f, 0.45f, 1.0f);
		static const FLinearColor OnAccent  = FLinearColor(0.95f, 0.96f, 1.0f, 1.0f);
	}

	namespace Border
	{
		static const FLinearColor Subtle  = FLinearColor(0.08f, 0.09f, 0.12f, 1.0f);
		static const FLinearColor Default = FLinearColor(0.12f, 0.13f, 0.17f, 1.0f);
	}

	namespace State
	{
		static const FLinearColor Selected       = FLinearColor(0.12f, 0.22f, 0.50f, 0.8f);
		static const FLinearColor SelectedBorder = FLinearColor(0.20f, 0.40f, 0.85f, 0.4f);
		static const FLinearColor Hover          = FLinearColor(0.06f, 0.07f, 0.10f, 1.0f);
		static const FLinearColor Locked         = FLinearColor(0.65f, 0.45f, 0.20f, 1.0f);
		static const FLinearColor Unlocked       = FLinearColor(0.45f, 0.48f, 0.55f, 1.0f);
		static const FLinearColor Danger         = FLinearColor(0.85f, 0.25f, 0.25f, 1.0f);
	}

	// ═══════════════════════════════════════════════════
	// TYPOGRAPHY
	// ═══════════════════════════════════════════════════

	namespace Font
	{
		inline FSlateFontInfo PageHeader()    { return FCoreStyle::GetDefaultFontStyle("Bold", 16); }
		inline FSlateFontInfo SectionHeader() { return FCoreStyle::GetDefaultFontStyle("Bold", 12); }
		inline FSlateFontInfo FieldLabel()    { return FCoreStyle::GetDefaultFontStyle("Bold", 10); }
		inline FSlateFontInfo Body()          { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
		inline FSlateFontInfo Input()         { return FCoreStyle::GetDefaultFontStyle("Regular", 11); }
		inline FSlateFontInfo BodySmall()     { return FCoreStyle::GetDefaultFontStyle("Regular", 9); }
		inline FSlateFontInfo ButtonText()    { return FCoreStyle::GetDefaultFontStyle("Bold", 9); }
		inline FSlateFontInfo TabActive()     { return FCoreStyle::GetDefaultFontStyle("Bold", 10); }
		inline FSlateFontInfo TabInactive()   { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
		inline FSlateFontInfo Caption()       { return FCoreStyle::GetDefaultFontStyle("Italic", 8); }
		inline FSlateFontInfo LockLabel()     { return FCoreStyle::GetDefaultFontStyle("Regular", 8); }
	}

	// ═══════════════════════════════════════════════════
	// SPACING
	// ═══════════════════════════════════════════════════

	namespace Spacing
	{
		static const FMargin PagePadding      = FMargin(12.0f, 10.0f, 12.0f, 10.0f);
		static const FMargin SectionPadding   = FMargin(12.0f, 8.0f, 12.0f, 8.0f);
		static const FMargin FieldOuter       = FMargin(12.0f, 3.0f, 12.0f, 3.0f);
		static const FMargin InputPadding     = FMargin(12.0f, 0.0f, 12.0f, 6.0f);
		static const FMargin ListItemPadding  = FMargin(10.0f, 6.0f, 10.0f, 6.0f);
		static const FMargin ButtonPadding    = FMargin(16.0f, 6.0f, 16.0f, 6.0f);
		static const FMargin TabPadding       = FMargin(14.0f, 6.0f, 14.0f, 6.0f);
		static const float   TabGap           = 2.0f;
		static const float   SectionGap       = 16.0f;
		static const float   ListThumbSize    = 52.0f;
		static const float   ListThumbGap     = 8.0f;
		static const float   ImagePanelHeight = 280.0f;
		static const float   GalleryThumbSize = 72.0f;
	}
}
