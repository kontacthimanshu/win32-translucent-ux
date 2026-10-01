// Effective-appearance resolution (T031): the data-model "EffectiveAppearance" rules as a
// truth table, plus the Solid/translucent alpha rules, the independence of tint and
// surface opacity (constitution Principle VII), accent resolution, base colors and the
// text-scrim floor (research R-04, R-05).

#include <te/appearance/Contrast.h>
#include <te/appearance/ThemeManager.h>
#include <te/render/TextHalo.h>

#include <gtest/gtest.h>

#include <cmath>
#include <ostream>
#include <string>
#include <vector>

namespace
{

constexpr te::Rgb kDarkBase{0x20, 0x20, 0x20};
constexpr te::Rgb kLightBase{0xF3, 0xF3, 0xF3};
constexpr te::Rgb kAccent{0x00, 0x78, 0xD4};
constexpr te::Rgb kCustomTint{0xC4, 0x2B, 0x1C};

te::Rgb SysColor(int index)
{
    const COLORREF c = GetSysColor(index);
    return {GetRValue(c), GetGValue(c), GetBValue(c)};
}

// A session where every translucent mode works: nothing forces a fallback.
te::RenderingCapabilities CapableSession(bool darkMode = true)
{
    te::RenderingCapabilities caps;
    caps.systemBackdropSupported = true;
    caps.transparencyEffectsEnabled = true;
    caps.highContrast = false;
    caps.darkMode = darkMode;
    caps.accent = kAccent;
    caps.backdropApplyFailed = false;
    return caps;
}

te::AppearanceSettings Settings(te::BackdropMode mode, double tintOpacity = 0.20, double surfaceOpacity = 0.0)
{
    te::AppearanceSettings settings;
    settings.backdropMode = mode;
    settings.tintOpacity = tintOpacity;
    settings.surfaceOpacity = surfaceOpacity;
    return settings;
}

te::EffectiveAppearance Resolve(const te::AppearanceSettings& settings, const te::RenderingCapabilities& caps)
{
    const te::ThemeManager themes;
    return themes.Resolve(settings, caps);
}

const char* Name(te::BackdropMode mode)
{
    switch (mode)
    {
    case te::BackdropMode::Acrylic:
        return "Acrylic";
    case te::BackdropMode::Mica:
        return "Mica";
    case te::BackdropMode::Solid:
        return "Solid";
    case te::BackdropMode::Transparent:
        return "Transparent";
    }
    return "?";
}

const char* Name(te::FallbackReason reason)
{
    switch (reason)
    {
    case te::FallbackReason::None:
        return "None";
    case te::FallbackReason::HighContrast:
        return "HighContrast";
    case te::FallbackReason::TransparencyOff:
        return "TransparencyOff";
    case te::FallbackReason::BackdropUnsupported:
        return "BackdropUnsupported";
    case te::FallbackReason::BackdropApplyFailed:
        return "BackdropApplyFailed";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Resolution truth table: every combination of the four fallback inputs, for
// each requested mode. The first matching rule wins:
//   1. high contrast              -> Solid, HighContrast
//   2. transparency effects off   -> Solid, TransparencyOff
//   3. translucent + unsupported  -> Solid, BackdropUnsupported
//   4. translucent + apply failed -> Solid, BackdropApplyFailed
//   5. otherwise                  -> requested, None
// ---------------------------------------------------------------------------

struct ResolveCase
{
    te::BackdropMode requested;
    bool highContrast;
    bool transparencyOn;
    bool supported;
    bool applyFailed;
    te::BackdropMode expectedMode;
    te::FallbackReason expectedReason;
};

void PrintTo(const ResolveCase& c, std::ostream* os)
{
    *os << Name(c.requested) << " hc=" << c.highContrast << " transparency=" << c.transparencyOn
        << " supported=" << c.supported << " applyFailed=" << c.applyFailed << " -> " << Name(c.expectedMode)
        << "/" << Name(c.expectedReason);
}

std::vector<ResolveCase> AllResolveCases()
{
    std::vector<ResolveCase> cases;
    for (const auto requested : {te::BackdropMode::Acrylic, te::BackdropMode::Mica, te::BackdropMode::Solid,
                                 te::BackdropMode::Transparent})
    {
        for (int bits = 0; bits < 16; ++bits)
        {
            ResolveCase c{};
            c.requested = requested;
            c.highContrast = (bits & 1) != 0;
            c.transparencyOn = (bits & 2) != 0;
            c.supported = (bits & 4) != 0;
            c.applyFailed = (bits & 8) != 0;

            const bool translucent = requested != te::BackdropMode::Solid;
            c.expectedMode = te::BackdropMode::Solid;
            if (c.highContrast)
            {
                c.expectedReason = te::FallbackReason::HighContrast;
            }
            else if (!c.transparencyOn)
            {
                c.expectedReason = te::FallbackReason::TransparencyOff;
            }
            else if (translucent && !c.supported)
            {
                c.expectedReason = te::FallbackReason::BackdropUnsupported;
            }
            else if (translucent && c.applyFailed)
            {
                c.expectedReason = te::FallbackReason::BackdropApplyFailed;
            }
            else
            {
                c.expectedMode = requested;
                c.expectedReason = te::FallbackReason::None;
            }
            cases.push_back(c);
        }
    }
    return cases;
}

class ResolveTruthTable : public ::testing::TestWithParam<ResolveCase>
{
};

TEST_P(ResolveTruthTable, FirstMatchingRuleDecidesModeAndReason)
{
    const ResolveCase& c = GetParam();
    te::RenderingCapabilities caps = CapableSession();
    caps.highContrast = c.highContrast;
    caps.transparencyEffectsEnabled = c.transparencyOn;
    caps.systemBackdropSupported = c.supported;
    caps.backdropApplyFailed = c.applyFailed;

    const te::EffectiveAppearance e = Resolve(Settings(c.requested), caps);

    EXPECT_EQ(e.requested, c.requested);
    EXPECT_EQ(e.applied, c.expectedMode) << "applied " << Name(e.applied);
    EXPECT_EQ(e.reason, c.expectedReason) << "reason " << Name(e.reason);
}

INSTANTIATE_TEST_SUITE_P(AllInputs, ResolveTruthTable, ::testing::ValuesIn(AllResolveCases()),
                         [](const ::testing::TestParamInfo<ResolveCase>& info) {
                             const ResolveCase& c = info.param;
                             return std::string(Name(c.requested)) + (c.highContrast ? "_HC" : "") +
                                    (c.transparencyOn ? "" : "_NoTransparency") +
                                    (c.supported ? "" : "_Unsupported") +
                                    (c.applyFailed ? "_ApplyFailed" : "");
                         });

// ---------------------------------------------------------------------------
// Alphas: Solid is fully opaque with no tint; translucent modes pass the user's
// values through unchanged.
// ---------------------------------------------------------------------------

TEST(ThemeResolve, SolidIsOpaqueWithNoTint)
{
    const te::EffectiveAppearance e =
        Resolve(Settings(te::BackdropMode::Solid, 0.60, 0.40), CapableSession());
    ASSERT_EQ(e.applied, te::BackdropMode::Solid);
    EXPECT_FLOAT_EQ(e.tintAlpha, 0.0f);
    EXPECT_FLOAT_EQ(e.surfaceAlpha, 1.0f);
}

TEST(ThemeResolve, FallbackToSolidIsOpaqueWithNoTint)
{
    te::RenderingCapabilities caps = CapableSession();
    caps.transparencyEffectsEnabled = false;
    const te::EffectiveAppearance e = Resolve(Settings(te::BackdropMode::Acrylic, 0.60, 0.40), caps);
    ASSERT_EQ(e.applied, te::BackdropMode::Solid);
    EXPECT_FLOAT_EQ(e.tintAlpha, 0.0f);
    EXPECT_FLOAT_EQ(e.surfaceAlpha, 1.0f);
}

class TranslucentModes : public ::testing::TestWithParam<te::BackdropMode>
{
};

TEST_P(TranslucentModes, AlphasEqualTheUserSettings)
{
    const te::EffectiveAppearance e = Resolve(Settings(GetParam(), 0.35, 0.55), CapableSession());
    ASSERT_EQ(e.applied, GetParam());
    EXPECT_FLOAT_EQ(e.tintAlpha, 0.35f);
    EXPECT_FLOAT_EQ(e.surfaceAlpha, 0.55f);
}

// Constitution Principle VII: tint and opacity are independent.
TEST_P(TranslucentModes, ChangingSurfaceOpacityNeverChangesTint)
{
    const te::RenderingCapabilities caps = CapableSession();
    for (const double surface : {0.0, 0.25, 0.50, 0.90})
    {
        const te::EffectiveAppearance e = Resolve(Settings(GetParam(), 0.30, surface), caps);
        EXPECT_FLOAT_EQ(e.tintAlpha, 0.30f) << "surfaceOpacity " << surface;
        EXPECT_EQ(e.tint, kAccent) << "surfaceOpacity " << surface;
        EXPECT_FLOAT_EQ(e.surfaceAlpha, static_cast<float>(surface));
    }
}

TEST_P(TranslucentModes, ChangingTintOpacityNeverChangesSurface)
{
    const te::RenderingCapabilities caps = CapableSession();
    for (const double tint : {0.0, 0.20, 0.45, 0.80})
    {
        const te::EffectiveAppearance e = Resolve(Settings(GetParam(), tint, 0.40), caps);
        EXPECT_FLOAT_EQ(e.surfaceAlpha, 0.40f) << "tintOpacity " << tint;
        EXPECT_FLOAT_EQ(e.tintAlpha, static_cast<float>(tint));
    }
}

TEST_P(TranslucentModes, ChangingTintColorNeverChangesEitherAlpha)
{
    const te::RenderingCapabilities caps = CapableSession();
    te::AppearanceSettings settings = Settings(GetParam(), 0.25, 0.45);
    const te::EffectiveAppearance withAccent = Resolve(settings, caps);
    settings.tintColor = kCustomTint;
    const te::EffectiveAppearance withCustom = Resolve(settings, caps);

    EXPECT_EQ(withCustom.tint, kCustomTint);
    EXPECT_FLOAT_EQ(withCustom.tintAlpha, withAccent.tintAlpha);
    EXPECT_FLOAT_EQ(withCustom.surfaceAlpha, withAccent.surfaceAlpha);
}

TEST_P(TranslucentModes, OpacityControlIsEnabled)
{
    EXPECT_TRUE(Resolve(Settings(GetParam()), CapableSession()).opacityControlEnabled);
}

INSTANTIATE_TEST_SUITE_P(Modes, TranslucentModes,
                         ::testing::Values(te::BackdropMode::Acrylic, te::BackdropMode::Mica),
                         [](const ::testing::TestParamInfo<te::BackdropMode>& info) {
                             return std::string(Name(info.param));
                         });

// ---------------------------------------------------------------------------
// Opacity control availability
// ---------------------------------------------------------------------------

TEST(ThemeResolve, OpacityControlDisabledInSolid)
{
    EXPECT_FALSE(Resolve(Settings(te::BackdropMode::Solid), CapableSession()).opacityControlEnabled);

    te::RenderingCapabilities unsupported = CapableSession();
    unsupported.systemBackdropSupported = false;
    EXPECT_FALSE(Resolve(Settings(te::BackdropMode::Mica), unsupported).opacityControlEnabled);
}

TEST(ThemeResolve, OpacityControlDisabledInHighContrast)
{
    te::RenderingCapabilities caps = CapableSession();
    caps.highContrast = true;
    for (const auto mode : {te::BackdropMode::Acrylic, te::BackdropMode::Mica, te::BackdropMode::Solid})
    {
        EXPECT_FALSE(Resolve(Settings(mode), caps).opacityControlEnabled) << Name(mode);
    }
}

// ---------------------------------------------------------------------------
// Tint color, base color, high-contrast system colors
// ---------------------------------------------------------------------------

TEST(ThemeResolve, AccentTintResolvesToTheSystemAccent)
{
    te::AppearanceSettings settings = Settings(te::BackdropMode::Mica);
    settings.tintColor = std::monostate{};
    EXPECT_EQ(Resolve(settings, CapableSession()).tint, kAccent);

    te::RenderingCapabilities otherAccent = CapableSession();
    otherAccent.accent = {0x10, 0x7C, 0x10};
    EXPECT_EQ(Resolve(settings, otherAccent).tint, otherAccent.accent);
}

TEST(ThemeResolve, CustomTintIsUsedAsIs)
{
    te::AppearanceSettings settings = Settings(te::BackdropMode::Acrylic);
    settings.tintColor = kCustomTint;
    EXPECT_EQ(Resolve(settings, CapableSession()).tint, kCustomTint);
}

TEST(ThemeResolve, BaseColorFollowsDarkAndLightMode)
{
    for (const auto mode : {te::BackdropMode::Acrylic, te::BackdropMode::Mica, te::BackdropMode::Solid})
    {
        EXPECT_EQ(Resolve(Settings(mode), CapableSession(true)).base, kDarkBase) << Name(mode);
        EXPECT_EQ(Resolve(Settings(mode), CapableSession(false)).base, kLightBase) << Name(mode);
    }
}

TEST(ThemeResolve, HighContrastUsesSystemColors)
{
    te::RenderingCapabilities caps = CapableSession();
    caps.highContrast = true;
    const te::EffectiveAppearance e = Resolve(Settings(te::BackdropMode::Acrylic), caps);

    EXPECT_EQ(e.base, SysColor(COLOR_WINDOW));
    EXPECT_EQ(e.text, SysColor(COLOR_WINDOWTEXT));
    EXPECT_EQ(e.selection, SysColor(COLOR_HIGHLIGHT));
    EXPECT_EQ(e.focus, SysColor(COLOR_HOTLIGHT));
    EXPECT_EQ(e.selectedText, SysColor(COLOR_HIGHLIGHTTEXT));
    EXPECT_FLOAT_EQ(e.selectionAlpha, 1.0f);
    EXPECT_FLOAT_EQ(e.textScrimAlpha, 0.0f);
}

TEST(ThemeResolve, SolidTypicalSurfaceIsTheBase)
{
    const te::EffectiveAppearance e = Resolve(Settings(te::BackdropMode::Solid), CapableSession(false));
    EXPECT_EQ(e.typicalSurfaceColor, kLightBase);
}

// ---------------------------------------------------------------------------
// Text scrim floor (research R-05)
// ---------------------------------------------------------------------------

// Acrylic can show a white or a black backdrop, and a fully translucent surface cannot
// keep one text color at 4.5:1 over both, so text areas need a surface-opacity floor.
TEST(ThemeResolve, AcrylicDarkWithTransparentSurfaceNeedsTextScrim)
{
    const te::EffectiveAppearance e =
        Resolve(Settings(te::BackdropMode::Acrylic, 0.0, 0.0), CapableSession(true));
    ASSERT_EQ(e.applied, te::BackdropMode::Acrylic);
    EXPECT_GT(e.textScrimAlpha, 0.0f);
    EXPECT_GE(e.textScrimAlpha, e.surfaceAlpha);
    EXPECT_LE(e.textScrimAlpha, 1.0f);
    const float steps = e.textScrimAlpha / 0.05f;
    EXPECT_NEAR(steps, std::round(steps), 1e-3f) << "not a multiple of 0.05: " << e.textScrimAlpha;
}

TEST(ThemeResolve, SolidNeedsNoTextScrim)
{
    for (const bool dark : {true, false})
    {
        const te::EffectiveAppearance e = Resolve(Settings(te::BackdropMode::Solid), CapableSession(dark));
        EXPECT_FLOAT_EQ(e.textScrimAlpha, 0.0f) << (dark ? "dark" : "light");
    }
}

// ---------------------------------------------------------------------------
// Transparent: clear glass, legibility from a glyph halo (TextHalo)
// ---------------------------------------------------------------------------

TEST(ThemeResolve, TransparentUsesAHaloInsteadOfATextScrim)
{
    for (const bool dark : {true, false})
    {
        // Surface 0% and no tint: the case that needs the strongest floor in Acrylic.
        const te::EffectiveAppearance e =
            Resolve(Settings(te::BackdropMode::Transparent, 0.0, 0.0), CapableSession(dark));
        ASSERT_EQ(e.applied, te::BackdropMode::Transparent) << (dark ? "dark" : "light");
        EXPECT_TRUE(e.textHalo);
        EXPECT_FLOAT_EQ(e.textScrimAlpha, 0.0f) << "the whole window stays equally clear";
        EXPECT_FLOAT_EQ(e.surfaceAlpha, 0.0f);
        // All text white, in both modes; it reads against its black halo.
        EXPECT_EQ(e.text, te::Contrast::kLightText);
        EXPECT_EQ(e.secondaryText, te::Contrast::kLightText);
        EXPECT_GE(te::Contrast::ContrastRatio(e.text, te::TextHalo::ColorFor(e.text)),
                  te::Contrast::kTextMinimum);
        EXPECT_TRUE(e.opacityControlEnabled);
    }
}

TEST(ThemeResolve, OnlyTransparentUsesTheHalo)
{
    for (const auto mode : {te::BackdropMode::Acrylic, te::BackdropMode::Mica, te::BackdropMode::Solid})
    {
        EXPECT_FALSE(Resolve(Settings(mode, 0.0, 0.0), CapableSession(true)).textHalo) << Name(mode);
    }
    te::RenderingCapabilities off = CapableSession(true);
    off.transparencyEffectsEnabled = false; // falls back to Solid
    EXPECT_FALSE(Resolve(Settings(te::BackdropMode::Transparent), off).textHalo);
}

TEST(ThemeResolve, TransparentSelectionStaysTranslucent)
{
    for (const bool dark : {true, false})
    {
        const te::EffectiveAppearance e =
            Resolve(Settings(te::BackdropMode::Transparent, 0.0, 0.0), CapableSession(dark));
        // Not raised toward opaque: the selected text has its halo.
        EXPECT_FLOAT_EQ(e.selectionAlpha, 0.40f) << (dark ? "dark" : "light");
        EXPECT_EQ(e.selectedText, e.text);
    }
}

} // namespace
