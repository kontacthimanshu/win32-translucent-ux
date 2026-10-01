// The focus indicator's color (T080; UI contract §4 "Focus visibility"): at least 3:1
// against what it is drawn on, including a selection fill of the same accent.

#include <te/appearance/Contrast.h>
#include <te/render/FocusIndicator.h>

#include <gtest/gtest.h>

namespace
{

using te::Contrast::ContrastRatio;
using te::Contrast::kIndicatorMinimum;

TEST(FocusIndicator, PickKeepsAPreferredColorThatAlreadyContrasts)
{
    const te::Rgb blue{0x00, 0x5F, 0xB8};
    const te::Rgb white{0xFF, 0xFF, 0xFF};
    const te::Rgb backgrounds[] = {white};
    EXPECT_EQ(te::Contrast::PickIndicatorColor(blue, backgrounds), blue);
}

TEST(FocusIndicator, PickFallsBackToBlackOrWhiteBelowThreeToOne)
{
    const te::Rgb blue{0x00, 0x5F, 0xB8};
    const te::Rgb nearBlue{0x10, 0x60, 0xB0};
    const te::Rgb light{0xF0, 0xF0, 0xF0};
    const te::Rgb backgrounds[] = {nearBlue};
    const te::Rgb picked = te::Contrast::PickIndicatorColor(blue, backgrounds);
    EXPECT_NE(picked, blue);
    EXPECT_GE(ContrastRatio(picked, nearBlue), kIndicatorMinimum);
    const te::Rgb onLight[] = {light};
    EXPECT_EQ(te::Contrast::PickIndicatorColor(te::Rgb{0xE0, 0xE0, 0xE0}, onLight), te::Contrast::kDarkText);
}

te::EffectiveAppearance Appearance(te::Rgb accent, te::Rgb surface, float selectionAlpha)
{
    te::EffectiveAppearance effective;
    effective.focus = accent;
    effective.selection = accent;
    effective.selectionAlpha = selectionAlpha;
    effective.typicalSurfaceColor = surface;
    return effective;
}

TEST(FocusIndicator, OnASelectedRowTheRingStillReachesThreeToOne)
{
    // Every preset-like accent over a light and a dark surface, at the selection alphas the
    // resolver produces.
    const te::Rgb accents[] = {{0x00, 0x5F, 0xB8}, {0x00, 0x78, 0xD4}, {0x10, 0x7C, 0x10}, {0xC4, 0x2B, 0x1C},
                               {0x87, 0x64, 0xB8}, {0xFF, 0xB9, 0x00}, {0x60, 0x60, 0x60}};
    const te::Rgb surfaces[] = {{0xF3, 0xF3, 0xF3}, {0x20, 0x20, 0x20}};
    for (const te::Rgb accent : accents)
    {
        for (const te::Rgb surface : surfaces)
        {
            for (const float alpha : {0.40f, 0.60f, 0.80f})
            {
                const te::EffectiveAppearance effective = Appearance(accent, surface, alpha);
                const te::Rgb fill = te::Contrast::Composite(surface, accent, alpha);
                const te::Rgb onFill = te::FocusIndicator::ColorOver(effective, true);
                SCOPED_TRACE(::testing::Message() << "accent " << int(accent.r) << "," << int(accent.g) << ","
                                                  << int(accent.b) << " alpha " << alpha);
                EXPECT_GE(ContrastRatio(onFill, fill), kIndicatorMinimum);
            }
        }
    }
}

TEST(FocusIndicator, OnTheSurfaceTheResolvedFocusColorIsKept)
{
    // The resolver already makes the focus color 3:1 against the surface (T033).
    const te::EffectiveAppearance effective = Appearance({0x00, 0x5F, 0xB8}, {0xF3, 0xF3, 0xF3}, 0.40f);
    EXPECT_EQ(te::FocusIndicator::ColorOver(effective, false), effective.focus);
}

TEST(FocusIndicator, TheRingIsTwoDipWide)
{
    EXPECT_FLOAT_EQ(te::FocusIndicator::kWidthDip, 2.0f);
}

} // namespace
