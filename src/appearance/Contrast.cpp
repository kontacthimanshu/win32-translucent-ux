#include <te/appearance/Contrast.h>

#include <algorithm>
#include <cmath>

namespace te::Contrast
{

namespace
{

constexpr Rgb kBlack{0x00, 0x00, 0x00};
constexpr Rgb kWhite{0xFF, 0xFF, 0xFF};

// Secondary text is the primary text faded toward the other candidate by the largest of
// these amounts that still reaches 4.5:1; 0 keeps the primary color.
constexpr int kSecondaryFadeSteps = 6; // 30%, 25%, ... 5%

constexpr int kStepsPerUnit = 20; // 1.0 / kAlphaStep
constexpr int kNoStep = kStepsPerUnit + 1;

double Linearize(std::uint8_t channel)
{
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

std::uint8_t Mix(std::uint8_t under, std::uint8_t over, float alpha)
{
    const double value = under + (static_cast<double>(over) - under) * alpha;
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

float StepToAlpha(int step)
{
    return static_cast<float>(step) * kAlphaStep;
}

// Lowest contrast of `text` over every extreme, with the surface at `surfaceAlpha`.
double MinContrast(Rgb text, std::span<const Rgb> extremes, Rgb base, float surfaceAlpha, Rgb tint,
                   float tintAlpha)
{
    double lowest = 21.0;
    for (const Rgb extreme : extremes)
    {
        const Rgb composite = Composite(Composite(extreme, base, surfaceAlpha), tint, tintAlpha);
        lowest = std::min(lowest, ContrastRatio(text, composite));
    }
    return lowest;
}

double MinContrastOver(Rgb text, std::span<const Rgb> composites, Rgb fill, float alpha)
{
    double lowest = 21.0;
    for (const Rgb composite : composites)
    {
        lowest = std::min(lowest, ContrastRatio(text, Composite(composite, fill, alpha)));
    }
    return lowest;
}

} // namespace

double RelativeLuminance(Rgb color)
{
    return 0.2126 * Linearize(color.r) + 0.7152 * Linearize(color.g) + 0.0722 * Linearize(color.b);
}

double ContrastRatio(Rgb a, Rgb b)
{
    const double la = RelativeLuminance(a);
    const double lb = RelativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Rgb Composite(Rgb under, Rgb over, float alpha)
{
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    return {Mix(under.r, over.r, alpha), Mix(under.g, over.g, alpha), Mix(under.b, over.b, alpha)};
}

BackdropExtremeSet BackdropExtremes(BackdropMode applied, bool darkMode, Rgb base)
{
    BackdropExtremeSet set;
    switch (applied)
    {
    case BackdropMode::Acrylic:
    case BackdropMode::Transparent: // anything on the desktop, unblurred
        set.colors = {kBlack, kWhite};
        set.count = 2;
        break;
    case BackdropMode::Mica:
        set.colors = {base, Composite(base, darkMode ? kWhite : kBlack, 0.25f)};
        set.count = 2;
        break;
    case BackdropMode::Solid:
        set.colors = {base, base};
        set.count = 1;
        break;
    }
    return set;
}

TextChoice PickTextColors(std::span<const Rgb> extremes, Rgb base, float surfaceAlpha, Rgb tint,
                          float tintAlpha)
{
    // For each candidate: the first step (in 0.05 units, above the user's surface opacity)
    // at which it reaches 4.5:1; 0 when it already does, kNoStep when even an opaque
    // surface is not enough (cannot happen for the better of kLightText / kDarkText).
    const int firstScrimStep = static_cast<int>(std::floor(surfaceAlpha * kStepsPerUnit + 1e-3f)) + 1;
    const auto requiredStep = [&](Rgb text) {
        if (MinContrast(text, extremes, base, surfaceAlpha, tint, tintAlpha) >= kTextMinimum)
        {
            return 0;
        }
        for (int step = firstScrimStep; step <= kStepsPerUnit; ++step)
        {
            if (MinContrast(text, extremes, base, StepToAlpha(step), tint, tintAlpha) >= kTextMinimum)
            {
                return step;
            }
        }
        return kNoStep;
    };

    // The candidate that needs the least extra opacity; ties go to the higher minimum
    // contrast at the user's surface opacity. Without a scrim this is exactly "the text
    // color with the highest minimum contrast" (research R-05 step 1), and it never picks
    // a color that no scrim can rescue (for example dark text over a dark base).
    const int lightStep = requiredStep(kLightText);
    const int darkStep = requiredStep(kDarkText);
    bool light = lightStep < darkStep;
    if (lightStep == darkStep)
    {
        light = MinContrast(kLightText, extremes, base, surfaceAlpha, tint, tintAlpha) >=
                MinContrast(kDarkText, extremes, base, surfaceAlpha, tint, tintAlpha);
    }

    TextChoice choice{};
    choice.text = light ? kLightText : kDarkText;
    const Rgb other = light ? kDarkText : kLightText;
    const int step = light ? lightStep : darkStep;
    choice.scrimAlpha = step == 0 ? 0.0f : step == kNoStep ? 1.0f : StepToAlpha(step);
    const float textAlpha = std::max(surfaceAlpha, choice.scrimAlpha);

    // Secondary text: as faded as possible while still reaching 4.5:1 where text is drawn.
    choice.secondary = choice.text;
    for (int fade = kSecondaryFadeSteps; fade > 0; --fade)
    {
        const Rgb candidate = Composite(choice.text, other, StepToAlpha(fade));
        if (MinContrast(candidate, extremes, base, textAlpha, tint, tintAlpha) >= kTextMinimum)
        {
            choice.secondary = candidate;
            break;
        }
    }
    return choice;
}

SelectionChoice PickSelectedRowColors(std::span<const Rgb> textAreaComposites, Rgb selection, Rgb text,
                                      Rgb otherText)
{
    constexpr int kStartStep = 8; // 0.40
    for (int step = kStartStep; step <= kStepsPerUnit; ++step)
    {
        const float alpha = StepToAlpha(step);
        if (MinContrastOver(text, textAreaComposites, selection, alpha) >= kTextMinimum)
        {
            return {alpha, text};
        }
        if (MinContrastOver(otherText, textAreaComposites, selection, alpha) >= kTextMinimum)
        {
            return {alpha, otherText};
        }
    }

    // Only reachable when neither color reaches 4.5:1 on the opaque fill (callers pass a
    // light and a dark text color, so one always does). Keep the better of the two.
    const bool keepText = MinContrastOver(text, textAreaComposites, selection, 1.0f) >=
                          MinContrastOver(otherText, textAreaComposites, selection, 1.0f);
    return {1.0f, keepText ? text : otherText};
}

Rgb PickIndicatorColor(Rgb preferred, std::span<const Rgb> backgrounds)
{
    const auto minimum = [&](Rgb color) {
        double lowest = 21.0;
        for (const Rgb background : backgrounds)
        {
            lowest = std::min(lowest, ContrastRatio(color, background));
        }
        return lowest;
    };
    if (minimum(preferred) >= kIndicatorMinimum)
    {
        return preferred;
    }
    Rgb best = preferred;
    double bestContrast = minimum(preferred);
    for (const Rgb candidate : {kLightText, kDarkText})
    {
        if (const double contrast = minimum(candidate); contrast > bestContrast)
        {
            best = candidate;
            bestContrast = contrast;
        }
    }
    return best;
}

} // namespace te::Contrast
