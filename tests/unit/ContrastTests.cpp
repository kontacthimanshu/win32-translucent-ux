// Contrast math and the legibility guard (T032; research R-05): WCAG relative luminance
// and contrast ratio, compositing, the backdrop extremes for each applied mode, the text
// color choice and the text-scrim floor.

#include <te/appearance/Contrast.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace
{

namespace C = te::Contrast;

constexpr te::Rgb kBlack{0x00, 0x00, 0x00};
constexpr te::Rgb kWhite{0xFF, 0xFF, 0xFF};
constexpr te::Rgb kDarkBase{0x20, 0x20, 0x20};
constexpr te::Rgb kLightBase{0xF3, 0xF3, 0xF3};
constexpr te::Rgb kAccent{0x00, 0x78, 0xD4};

// The composite behind text for one backdrop extreme, in rendering order (R-04, R-05).
te::Rgb TextAreaComposite(te::Rgb extreme, te::Rgb base, float surfaceAlpha, te::Rgb tint, float tintAlpha)
{
    return C::Composite(C::Composite(extreme, base, surfaceAlpha), tint, tintAlpha);
}

// Lowest contrast of `text` across all extremes, with the surface drawn at `surfaceAlpha`.
double MinContrast(te::Rgb text, std::span<const te::Rgb> extremes, te::Rgb base, float surfaceAlpha,
                   te::Rgb tint, float tintAlpha)
{
    EXPECT_FALSE(extremes.empty()) << "no backdrop extremes to check against";
    double lowest = 21.0;
    for (const te::Rgb extreme : extremes)
    {
        lowest = std::min(
            lowest, C::ContrastRatio(text, TextAreaComposite(extreme, base, surfaceAlpha, tint, tintAlpha)));
    }
    return lowest;
}

// The opacity actually used inside text areas: the floor if there is one.
float EffectiveTextAlpha(const C::TextChoice& choice, float surfaceAlpha)
{
    return std::max(surfaceAlpha, choice.scrimAlpha);
}

bool IsAlphaStep(float alpha)
{
    const float steps = alpha / C::kAlphaStep;
    return std::fabs(steps - std::round(steps)) < 1e-3f;
}

void ExpectSameColor(te::Rgb actual, te::Rgb expected, int tolerance = 0)
{
    EXPECT_NEAR(actual.r, expected.r, tolerance);
    EXPECT_NEAR(actual.g, expected.g, tolerance);
    EXPECT_NEAR(actual.b, expected.b, tolerance);
}

// ---------------------------------------------------------------------------
// WCAG luminance and contrast ratio
// ---------------------------------------------------------------------------

TEST(Contrast, RelativeLuminanceOfBlackAndWhite)
{
    EXPECT_NEAR(C::RelativeLuminance(kBlack), 0.0, 1e-9);
    EXPECT_NEAR(C::RelativeLuminance(kWhite), 1.0, 1e-9);
}

TEST(Contrast, RelativeLuminanceLinearizesSrgb)
{
    // sRGB 0x80 (128/255) linearizes to about 0.2159, not 0.5.
    EXPECT_NEAR(C::RelativeLuminance({0x80, 0x80, 0x80}), 0.2159, 1e-3);
    // Below the 0.04045 knee the curve is linear: 10/255 / 12.92.
    EXPECT_NEAR(C::RelativeLuminance({0x0A, 0x0A, 0x0A}), (10.0 / 255.0) / 12.92, 1e-6);
    // Channel weights 0.2126 R, 0.7152 G, 0.0722 B.
    EXPECT_NEAR(C::RelativeLuminance({0xFF, 0x00, 0x00}), 0.2126, 1e-4);
    EXPECT_NEAR(C::RelativeLuminance({0x00, 0xFF, 0x00}), 0.7152, 1e-4);
    EXPECT_NEAR(C::RelativeLuminance({0x00, 0x00, 0xFF}), 0.0722, 1e-4);
}

TEST(Contrast, BlackOnWhiteIs21)
{
    EXPECT_NEAR(C::ContrastRatio(kBlack, kWhite), 21.0, 1e-6);
    EXPECT_NEAR(C::ContrastRatio(kWhite, kBlack), 21.0, 1e-6);
}

TEST(Contrast, Grey767676OnWhiteIsAbout4point54)
{
    EXPECT_NEAR(C::ContrastRatio({0x76, 0x76, 0x76}, kWhite), 4.54, 0.01);
}

TEST(Contrast, SameColorIs1)
{
    EXPECT_NEAR(C::ContrastRatio(kAccent, kAccent), 1.0, 1e-9);
}

// ---------------------------------------------------------------------------
// Composite(under, over, alpha): `over` drawn at `alpha` on top of `under`
// ---------------------------------------------------------------------------

TEST(Contrast, CompositeAtAlphaZeroIsUnder)
{
    ExpectSameColor(C::Composite(kDarkBase, kAccent, 0.0f), kDarkBase);
}

TEST(Contrast, CompositeAtAlphaOneIsOver)
{
    ExpectSameColor(C::Composite(kDarkBase, kAccent, 1.0f), kAccent);
}

TEST(Contrast, CompositeAtHalfIsTheMidpoint)
{
    ExpectSameColor(C::Composite(kBlack, kWhite, 0.5f), {0x80, 0x80, 0x80}, 1);
    ExpectSameColor(C::Composite({0x00, 0x78, 0xD4}, {0xFF, 0x00, 0x20}, 0.5f), {0x80, 0x3C, 0x7A}, 1);
}

// ---------------------------------------------------------------------------
// Backdrop extremes per applied mode (R-05 table)
// ---------------------------------------------------------------------------

TEST(Contrast, AcrylicExtremesAreBlackAndWhite)
{
    for (const bool dark : {true, false})
    {
        const C::BackdropExtremeSet set =
            C::BackdropExtremes(te::BackdropMode::Acrylic, dark, dark ? kDarkBase : kLightBase);
        const std::span<const te::Rgb> extremes = set;
        ASSERT_EQ(extremes.size(), 2u);
        EXPECT_NE(std::find(extremes.begin(), extremes.end(), kBlack), extremes.end());
        EXPECT_NE(std::find(extremes.begin(), extremes.end(), kWhite), extremes.end());
    }
}

TEST(Contrast, MicaDarkExtremesAreBaseAndBase25PercentTowardWhite)
{
    const C::BackdropExtremeSet set = C::BackdropExtremes(te::BackdropMode::Mica, true, kDarkBase);
    const std::span<const te::Rgb> extremes = set;
    ASSERT_EQ(extremes.size(), 2u);
    const te::Rgb shifted = C::Composite(kDarkBase, kWhite, 0.25f);
    EXPECT_NE(std::find(extremes.begin(), extremes.end(), kDarkBase), extremes.end());
    EXPECT_NE(std::find(extremes.begin(), extremes.end(), shifted), extremes.end());
    ExpectSameColor(shifted, {0x58, 0x58, 0x58}, 1);
}

TEST(Contrast, MicaLightExtremesAreBaseAndBase25PercentTowardBlack)
{
    const C::BackdropExtremeSet set = C::BackdropExtremes(te::BackdropMode::Mica, false, kLightBase);
    const std::span<const te::Rgb> extremes = set;
    ASSERT_EQ(extremes.size(), 2u);
    EXPECT_NE(std::find(extremes.begin(), extremes.end(), kLightBase), extremes.end());
    EXPECT_NE(std::find(extremes.begin(), extremes.end(), C::Composite(kLightBase, kBlack, 0.25f)),
              extremes.end());
}

TEST(Contrast, TransparentExtremesAreBlackAndWhite)
{
    // Clear glass shows the desktop unblurred: anything from black to white is behind it.
    for (const bool dark : {true, false})
    {
        const C::BackdropExtremeSet set =
            C::BackdropExtremes(te::BackdropMode::Transparent, dark, dark ? kDarkBase : kLightBase);
        const std::span<const te::Rgb> extremes = set;
        ASSERT_EQ(extremes.size(), 2u);
        EXPECT_NE(std::find(extremes.begin(), extremes.end(), kBlack), extremes.end());
        EXPECT_NE(std::find(extremes.begin(), extremes.end(), kWhite), extremes.end());
    }
}

TEST(Contrast, SolidExtremeIsTheBaseOnly)
{
    const C::BackdropExtremeSet set = C::BackdropExtremes(te::BackdropMode::Solid, true, kDarkBase);
    const std::span<const te::Rgb> extremes = set;
    ASSERT_EQ(extremes.size(), 1u);
    EXPECT_EQ(extremes[0], kDarkBase);
}

// ---------------------------------------------------------------------------
// PickTextColors
// ---------------------------------------------------------------------------

TEST(Contrast, PicksLightTextOverDarkBackdropsAndDarkTextOverLight)
{
    const std::vector<te::Rgb> dark{kDarkBase};
    const C::TextChoice onDark = C::PickTextColors(dark, kDarkBase, 1.0f, kAccent, 0.0f);
    EXPECT_GT(C::RelativeLuminance(onDark.text), 0.5);
    EXPECT_GE(C::ContrastRatio(onDark.text, kDarkBase), C::kTextMinimum);
    EXPECT_FLOAT_EQ(onDark.scrimAlpha, 0.0f);

    const std::vector<te::Rgb> light{kLightBase};
    const C::TextChoice onLight = C::PickTextColors(light, kLightBase, 1.0f, kAccent, 0.0f);
    EXPECT_LT(C::RelativeLuminance(onLight.text), 0.5);
    EXPECT_GE(C::ContrastRatio(onLight.text, kLightBase), C::kTextMinimum);
    EXPECT_FLOAT_EQ(onLight.scrimAlpha, 0.0f);
}

// Two backdrops where only one text color can serve both: the minimum across extremes
// decides, not the average or the first extreme.
TEST(Contrast, ChoosesHighestMinimumAcrossExtremes)
{
    // Mid-dark grey and white: light text fails badly on white, dark text passes on both.
    const std::vector<te::Rgb> extremes{{0x90, 0x90, 0x90}, kWhite};
    const C::TextChoice choice = C::PickTextColors(extremes, kWhite, 0.0f, kAccent, 0.0f);
    EXPECT_LT(C::RelativeLuminance(choice.text), 0.5);
    EXPECT_GE(MinContrast(choice.text, extremes, kWhite, 0.0f, kAccent, 0.0f), C::kTextMinimum);
    EXPECT_FLOAT_EQ(choice.scrimAlpha, 0.0f);
}

// Acrylic over a bright wallpaper: dark mode, surface fully transparent, no tint. The
// backdrop may be black or white, so no text color reaches 4.5:1 on both without a floor.
TEST(Contrast, AcrylicOverBrightWallpaperNeedsScrim)
{
    const C::BackdropExtremeSet extremes = C::BackdropExtremes(te::BackdropMode::Acrylic, true, kDarkBase);
    const C::TextChoice choice = C::PickTextColors(extremes, kDarkBase, 0.0f, kAccent, 0.0f);

    ASSERT_GT(choice.scrimAlpha, 0.0f);
    const float alpha = EffectiveTextAlpha(choice, 0.0f);
    EXPECT_GE(C::ContrastRatio(choice.text, TextAreaComposite(kWhite, kDarkBase, alpha, kAccent, 0.0f)),
              C::kTextMinimum);
    EXPECT_GE(C::ContrastRatio(choice.text, TextAreaComposite(kBlack, kDarkBase, alpha, kAccent, 0.0f)),
              C::kTextMinimum);
}

TEST(Contrast, AcrylicWithMostlyOpaqueSurfaceNeedsNoScrim)
{
    const C::BackdropExtremeSet extremes = C::BackdropExtremes(te::BackdropMode::Acrylic, true, kDarkBase);
    const C::TextChoice choice = C::PickTextColors(extremes, kDarkBase, 0.9f, kAccent, 0.0f);

    EXPECT_FLOAT_EQ(choice.scrimAlpha, 0.0f);
    EXPECT_GE(MinContrast(choice.text, extremes, kDarkBase, 0.9f, kAccent, 0.0f), C::kTextMinimum);
}

// Mica, dark, transparent surface: the text must hold against the base and against the
// base shifted 25% toward white, both.
TEST(Contrast, MicaDarkChecksBothExtremes)
{
    const C::BackdropExtremeSet extremes = C::BackdropExtremes(te::BackdropMode::Mica, true, kDarkBase);
    const C::TextChoice choice = C::PickTextColors(extremes, kDarkBase, 0.0f, kAccent, 0.0f);
    const float alpha = EffectiveTextAlpha(choice, 0.0f);

    EXPECT_GE(C::ContrastRatio(choice.text, TextAreaComposite(kDarkBase, kDarkBase, alpha, kAccent, 0.0f)),
              C::kTextMinimum);
    const te::Rgb shifted = C::Composite(kDarkBase, kWhite, 0.25f);
    EXPECT_GE(C::ContrastRatio(choice.text, TextAreaComposite(shifted, kDarkBase, alpha, kAccent, 0.0f)),
              C::kTextMinimum);
}

// ---------------------------------------------------------------------------
// Scrim floor properties across a sweep of settings
// ---------------------------------------------------------------------------

struct ScrimCase
{
    te::BackdropMode mode;
    bool dark;
    float surfaceAlpha;
    float tintAlpha;
};

std::vector<ScrimCase> ScrimCases()
{
    std::vector<ScrimCase> cases;
    for (const auto mode : {te::BackdropMode::Acrylic, te::BackdropMode::Mica})
    {
        for (const bool dark : {true, false})
        {
            for (int s = 0; s <= 18; s += 3)
            {
                for (const float tint : {0.0f, 0.2f, 0.5f, 0.8f})
                {
                    cases.push_back({mode, dark, static_cast<float>(s) * C::kAlphaStep, tint});
                }
            }
        }
    }
    return cases;
}

TEST(Contrast, ScrimIsZeroOrAStepAtLeastTheSurfaceAndIsTheSmallestThatPasses)
{
    for (const ScrimCase& c : ScrimCases())
    {
        SCOPED_TRACE(::testing::Message() << (c.mode == te::BackdropMode::Acrylic ? "Acrylic" : "Mica")
                                          << (c.dark ? " dark" : " light") << " surface=" << c.surfaceAlpha
                                          << " tint=" << c.tintAlpha);
        const te::Rgb base = c.dark ? kDarkBase : kLightBase;
        const C::BackdropExtremeSet extremes = C::BackdropExtremes(c.mode, c.dark, base);
        const C::TextChoice choice = C::PickTextColors(extremes, base, c.surfaceAlpha, kAccent, c.tintAlpha);

        EXPECT_GE(choice.scrimAlpha, 0.0f);
        EXPECT_LE(choice.scrimAlpha, 1.0f);
        if (choice.scrimAlpha > 0.0f)
        {
            EXPECT_GE(choice.scrimAlpha, c.surfaceAlpha);
            EXPECT_TRUE(IsAlphaStep(choice.scrimAlpha)) << "scrim " << choice.scrimAlpha;

            // One step lower (but not below the user's surface) would not have been enough.
            const float lower = choice.scrimAlpha - C::kAlphaStep;
            if (lower >= c.surfaceAlpha - 1e-4f)
            {
                EXPECT_LT(MinContrast(choice.text, extremes, base, lower, kAccent, c.tintAlpha),
                          C::kTextMinimum);
            }
        }
        EXPECT_GE(MinContrast(choice.text, extremes, base, EffectiveTextAlpha(choice, c.surfaceAlpha),
                              kAccent, c.tintAlpha),
                  C::kTextMinimum);
    }
}

// ---------------------------------------------------------------------------
// PickSelectedRowColors (R-05 "Text on selected rows")
// ---------------------------------------------------------------------------

TEST(Contrast, SelectedRowTextReachesTextMinimum)
{
    const std::vector<te::Rgb> composites{kDarkBase, {0x58, 0x58, 0x58}};
    const C::SelectionChoice choice = C::PickSelectedRowColors(composites, kAccent, kWhite, kBlack);

    EXPECT_GE(choice.selectionAlpha, 0.40f - 1e-4f);
    EXPECT_LE(choice.selectionAlpha, 1.0f);
    EXPECT_TRUE(IsAlphaStep(choice.selectionAlpha));
    for (const te::Rgb composite : composites)
    {
        EXPECT_GE(
            C::ContrastRatio(choice.selectedText, C::Composite(composite, kAccent, choice.selectionAlpha)),
            C::kTextMinimum);
    }
}

TEST(Contrast, SelectedRowSwitchesTextBeforeRaisingAlpha)
{
    // White text fails on a 40% light-yellow fill over a light surface; black text passes,
    // so the alpha stays at 0.40 and the text switches.
    const std::vector<te::Rgb> composites{kLightBase};
    const C::SelectionChoice choice =
        C::PickSelectedRowColors(composites, {0xFF, 0xE0, 0x60}, kWhite, kBlack);
    EXPECT_FLOAT_EQ(choice.selectionAlpha, 0.40f);
    EXPECT_EQ(choice.selectedText, kBlack);
}

} // namespace
