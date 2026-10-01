#pragma once

// The keyboard focus indicator of the custom-drawn components (T080; UI contract §4
// "Focus visibility"): a 2-DIP ring whose color reaches 3:1 against what it is drawn on.

#include <te/appearance/AppearanceSettings.h>

namespace te::FocusIndicator
{

inline constexpr float kWidthDip = 2.0f;

// The ring color over the surface, or over a selection fill (a selected row, the current
// place). The focus color equals the selection color, so on a selection it would all but
// vanish; there the contrast check picks white or black instead.
Rgb ColorOver(const EffectiveAppearance& effective, bool onSelection);

} // namespace te::FocusIndicator
