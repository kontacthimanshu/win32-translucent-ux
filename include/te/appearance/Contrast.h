#pragma once

// WCAG contrast math and the legibility guard (T033; research R-05). Pure functions,
// unit-tested by T032.

#include <te/core/Types.h>

#include <array>
#include <cstddef>
#include <span>

namespace te::Contrast
{

// WCAG 2.x text contrast target, and the target for non-text indicators (selection, focus).
inline constexpr double kTextMinimum = 4.5;
inline constexpr double kIndicatorMinimum = 3.0;

// The step used for textScrimAlpha and selectionAlpha.
inline constexpr float kAlphaStep = 0.05f;

// Text candidates: white for dark backdrops, black for light ones. Pure black and white
// guarantee that an opaque surface always passes: for any color, one of them reaches at
// least 4.58:1 (the thresholds cross at relative luminance ~0.18). Fluent's near-black
// #1B1B1B does not: a strong mid-luminance tint (Navy or Slate at 80% over the light base)
// left neither text color at 4.5:1 (found in T042).
inline constexpr Rgb kLightText{0xFF, 0xFF, 0xFF};
inline constexpr Rgb kDarkText{0x00, 0x00, 0x00};

// A non-text indicator (a focus ring) drawn on `backgrounds` (T080): `preferred` if it
// reaches kIndicatorMinimum against every one of them; otherwise whichever of preferred,
// white and black has the highest minimum contrast.
Rgb PickIndicatorColor(Rgb preferred, std::span<const Rgb> backgrounds);

// WCAG relative luminance of an sRGB color (sRGB channels linearized), 0.0–1.0.
double RelativeLuminance(Rgb color);

// WCAG contrast ratio (L1 + 0.05) / (L2 + 0.05), 1.0–21.0; order does not matter.
double ContrastRatio(Rgb a, Rgb b);

// `over` drawn at `alpha` (0–1) on top of `under`, per sRGB channel, rounded.
Rgb Composite(Rgb under, Rgb over, float alpha);

// The backdrop colors the DWM may show behind the window for the applied mode
// (research R-05 table). Holds one or two colors and converts to a span; pass it
// directly or keep it in a variable while the span is in use.
struct BackdropExtremeSet
{
    std::array<Rgb, 2> colors{};
    std::size_t count = 0;

    std::span<const Rgb> Span() const noexcept
    {
        return {colors.data(), count};
    }
    operator std::span<const Rgb>() const noexcept
    {
        return Span();
    }
};

// Mica: base and Composite(base, opposite extreme, 0.25); Acrylic and Transparent: black
// and white; Solid: base only. The opposite extreme is white in dark mode and black in light mode.
BackdropExtremeSet BackdropExtremes(BackdropMode applied, bool darkMode, Rgb base);

struct TextChoice
{
    Rgb text;
    Rgb secondary;
    float scrimAlpha; // 0 when not needed; otherwise a multiple of 0.05, >= surfaceAlpha
};

// Composes each extreme as Composite(Composite(extreme, base, surfaceAlpha), tint,
// tintAlpha), picks the text color with the highest minimum contrast across them and,
// if that is below 4.5:1, finds the smallest scrim alpha (in steps of 0.05, at least
// surfaceAlpha) used in place of surfaceAlpha that reaches it.
TextChoice PickTextColors(std::span<const Rgb> extremes, Rgb base, float surfaceAlpha, Rgb tint,
                          float tintAlpha);

struct SelectionChoice
{
    float selectionAlpha;
    Rgb selectedText;
};

// Research R-05 "Text on selected rows": start at alpha 0.40 with `text`, then try
// `otherText`, then raise the alpha in steps of 0.05 up to 1.0 until one of them reaches
// 4.5:1 against every Composite(textAreaComposite, selection, alpha).
SelectionChoice PickSelectedRowColors(std::span<const Rgb> textAreaComposites, Rgb selection, Rgb text,
                                      Rgb otherText);

} // namespace te::Contrast
