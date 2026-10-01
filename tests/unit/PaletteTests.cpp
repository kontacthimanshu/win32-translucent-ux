// Palette and opacity limits (T042; research R-11): the 12 presets, the ranges and
// SnapOpacity, and that every preset stays legible at maximum tint strength in dark and
// light mode (research R-05: an opaque surface always passes).

#include <te/appearance/Contrast.h>
#include <te/appearance/Palette.h>
#include <te/appearance/ThemeManager.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <string>

namespace
{

namespace C = te::Contrast;

TEST(Palette, PresetsMatchResearchR11)
{
    const struct
    {
        const wchar_t* name;
        te::Rgb rgb;
    } expected[] = {
        {L"Blue", {0x00, 0x78, 0xD4}},      {L"Navy", {0x00, 0x63, 0xB1}},  {L"Teal", {0x00, 0xB7, 0xC3}},
        {L"Sea green", {0x00, 0xB2, 0x94}}, {L"Green", {0x10, 0x7C, 0x10}}, {L"Gold", {0xFF, 0xB9, 0x00}},
        {L"Orange", {0xF7, 0x63, 0x0C}},    {L"Red", {0xE8, 0x11, 0x23}},   {L"Rose", {0xEA, 0x00, 0x5E}},
        {L"Purple", {0x87, 0x64, 0xB8}},    {L"Slate", {0x51, 0x5C, 0x6B}}, {L"Graphite", {0x4C, 0x4A, 0x48}},
    };
    ASSERT_EQ(te::kPresetPalette.size(), std::size(expected));
    for (size_t i = 0; i < std::size(expected); ++i)
    {
        EXPECT_STREQ(te::kPresetPalette[i].name, expected[i].name);
        EXPECT_EQ(te::kPresetPalette[i].rgb, expected[i].rgb) << i;
    }
}

TEST(Palette, OpacityLimits)
{
    EXPECT_DOUBLE_EQ(te::kTintMin, 0.0);
    EXPECT_DOUBLE_EQ(te::kTintMax, 0.80);
    EXPECT_DOUBLE_EQ(te::kSurfaceMin, 0.0);
    EXPECT_DOUBLE_EQ(te::kSurfaceMax, 0.90);
    EXPECT_DOUBLE_EQ(te::kOpacityStep, 0.05);
}

TEST(Palette, SnapOpacityClampsAndRoundsToSteps)
{
    EXPECT_NEAR(te::SnapOpacity(0.33, te::kTintMin, te::kTintMax), 0.35, 1e-9);
    EXPECT_NEAR(te::SnapOpacity(0.32, te::kTintMin, te::kTintMax), 0.30, 1e-9);
    // Exact decimal values, so settings.json holds 0.3, not 0.30000000000000004.
    EXPECT_EQ(te::SnapOpacity(0.3, te::kSurfaceMin, te::kSurfaceMax), 0.3);
    EXPECT_EQ(te::SnapOpacity(0.35, te::kSurfaceMin, te::kSurfaceMax), 0.35);
    EXPECT_EQ(te::SnapOpacity(0.7, te::kTintMin, te::kTintMax), 0.7);
    EXPECT_NEAR(te::SnapOpacity(5.0, te::kTintMin, te::kTintMax), 0.80, 1e-9);
    EXPECT_NEAR(te::SnapOpacity(1.5, te::kSurfaceMin, te::kSurfaceMax), 0.90, 1e-9);
    EXPECT_NEAR(te::SnapOpacity(-1.0, te::kSurfaceMin, te::kSurfaceMax), 0.00, 1e-9);
    EXPECT_NEAR(te::SnapOpacity(std::numeric_limits<double>::quiet_NaN(), te::kTintMin, te::kTintMax), 0.0,
                1e-9);
    EXPECT_NEAR(te::SnapOpacity(std::numeric_limits<double>::infinity(), te::kTintMin, te::kTintMax), 0.0,
                1e-9);
}

// The pure black / white text candidates always leave one of them at 4.5:1 or better
// against any opaque color.
TEST(Palette, SomeTextColorPassesOnAnyOpaqueColor)
{
    for (int v = 0; v <= 255; ++v)
    {
        for (const te::Rgb c :
             {te::Rgb{static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v),
                      static_cast<std::uint8_t>(v)},
              te::Rgb{static_cast<std::uint8_t>(v), 0x80, static_cast<std::uint8_t>(255 - v)}})
        {
            const double best =
                std::max(C::ContrastRatio(C::kLightText, c), C::ContrastRatio(C::kDarkText, c));
            EXPECT_GE(best, C::kTextMinimum) << int(c.r) << "," << int(c.g) << "," << int(c.b);
        }
    }
}

// Every preset at 80% tint, over dark and light bases, in each mode, with a transparent
// surface: text and secondary text reach 4.5:1 over every backdrop extreme at the
// text-area floor, and selected-row text reaches 4.5:1 over the selection fill.
TEST(Palette, EveryPresetIsLegibleAtMaximumTint)
{
    const te::ThemeManager themes;
    for (const te::NamedColor& preset : te::kPresetPalette)
    {
        for (const bool dark : {true, false})
        {
            for (const auto mode : {te::BackdropMode::Acrylic, te::BackdropMode::Mica})
            {
                SCOPED_TRACE(::testing::Message()
                             << preset.name << (dark ? " dark " : " light ")
                             << (mode == te::BackdropMode::Acrylic ? "Acrylic" : "Mica"));
                te::RenderingCapabilities caps;
                caps.systemBackdropSupported = true;
                caps.darkMode = dark;
                caps.accent = preset.rgb;
                te::AppearanceSettings settings;
                settings.backdropMode = mode;
                settings.tintColor = preset.rgb;
                settings.tintOpacity = te::kTintMax;
                settings.surfaceOpacity = 0.0;
                const te::EffectiveAppearance e = themes.Resolve(settings, caps);

                const float floor = std::max(e.surfaceAlpha, e.textScrimAlpha);
                for (const te::Rgb extreme : C::BackdropExtremes(e.applied, dark, e.base).Span())
                {
                    const te::Rgb area =
                        C::Composite(C::Composite(extreme, e.base, floor), e.tint, e.tintAlpha);
                    EXPECT_GE(C::ContrastRatio(e.text, area), C::kTextMinimum);
                    EXPECT_GE(C::ContrastRatio(e.secondaryText, area), C::kTextMinimum);
                    EXPECT_GE(
                        C::ContrastRatio(e.selectedText, C::Composite(area, e.selection, e.selectionAlpha)),
                        C::kTextMinimum);
                }
            }
        }
    }
}

} // namespace
