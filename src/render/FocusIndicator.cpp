#include <te/render/FocusIndicator.h>

#include <te/appearance/Contrast.h>

namespace te::FocusIndicator
{

Rgb ColorOver(const EffectiveAppearance& effective, bool onSelection)
{
    const Rgb surface = effective.typicalSurfaceColor;
    const Rgb background =
        onSelection ? Contrast::Composite(surface, effective.selection, effective.selectionAlpha) : surface;
    const Rgb backgrounds[] = {background};
    return Contrast::PickIndicatorColor(effective.focus, backgrounds);
}

} // namespace te::FocusIndicator
