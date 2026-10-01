#pragma once

// Preset tint palette and opacity limits (T042; research R-11, UI contract §3).

#include <te/core/Types.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace te
{

struct NamedColor
{
    const wchar_t* name; // English name; the picker shows IDS_COLOR_BLUE + index instead
    Rgb rgb;
};

// The Windows accent palette, in the order of the IDS_COLOR_* strings.
inline constexpr std::array<NamedColor, 12> kPresetPalette{{
    {L"Blue", {0x00, 0x78, 0xD4}},
    {L"Navy", {0x00, 0x63, 0xB1}},
    {L"Teal", {0x00, 0xB7, 0xC3}},
    {L"Sea green", {0x00, 0xB2, 0x94}},
    {L"Green", {0x10, 0x7C, 0x10}},
    {L"Gold", {0xFF, 0xB9, 0x00}},
    {L"Orange", {0xF7, 0x63, 0x0C}},
    {L"Red", {0xE8, 0x11, 0x23}},
    {L"Rose", {0xEA, 0x00, 0x5E}},
    {L"Purple", {0x87, 0x64, 0xB8}},
    {L"Slate", {0x51, 0x5C, 0x6B}},
    {L"Graphite", {0x4C, 0x4A, 0x48}},
}};

// Tint strength and surface opacity limits (R-04, R-11), in steps of 5%.
inline constexpr double kTintMin = 0.0;
inline constexpr double kTintMax = 0.80;
inline constexpr double kSurfaceMin = 0.0;
inline constexpr double kSurfaceMax = 0.90;
inline constexpr double kOpacityStep = 0.05;

// Transparent mode has no material: the tint layer is the glass's only color, and below
// about this strength a color barely shows over a busy desktop. Choosing a color (or
// Transparent itself) raises the tint strength to at least this; the slider can still
// take it lower afterwards.
inline constexpr double kTransparentTintFloor = 0.45;

// Clamps to [min, max] and rounds to the nearest kOpacityStep (settings validation, sliders).
inline double SnapOpacity(double value, double min, double max)
{
    if (!std::isfinite(value))
    {
        return min;
    }
    const double clamped = std::clamp(value, min, max);
    // Whole percent over 100, so 6 steps is exactly the double nearest 0.30 (6 * 0.05 is
    // 0.30000000000000004, which would also show up in settings.json).
    const double percent = std::round(clamped / kOpacityStep) * 5.0;
    return std::clamp(percent / 100.0, min, max);
}

} // namespace te
