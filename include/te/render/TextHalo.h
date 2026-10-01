#pragma once

// Text legibility without a scrim (Transparent mode). Clear glass keeps every surface
// equally see-through, so instead of raising the opacity behind text areas
// (SurfacePainter::PaintTextScrim) each glyph gets a thin outline in the opposite
// lightness: light text a dark halo, dark text a light one. The text then reads against
// its own halo whatever the desktop shows behind the window.

#include <te/appearance/AppearanceSettings.h>

#include <d2d1_1.h>
#include <dwrite.h>

#include <span>

namespace te::TextHalo
{

inline constexpr float kRadiusPx = 1.0f; // one physical pixel at any scale: a crisp outline
inline constexpr float kAlpha = 0.85f;

// Black for light text, white for dark text.
Rgb ColorFor(Rgb text);

// ID2D1DeviceContext::DrawTextW / DrawTextLayout, with the halo underneath when
// effective.textHalo is set. The halo takes the brush's alpha into account (dimmed
// glyphs get a dimmer halo) and ignores color fonts, so emoji get an outline too.
// Without effective.textHalo, or with a brush that is not a solid color, these are
// exactly the plain calls.
void DrawTextW(ID2D1DeviceContext* dc, const wchar_t* text, UINT32 length, IDWriteTextFormat* format,
               const D2D1_RECT_F& rect, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options,
               const EffectiveAppearance& effective);
void DrawTextLayout(ID2D1DeviceContext* dc, D2D1_POINT_2F origin, IDWriteTextLayout* layout,
                    ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options, const EffectiveAppearance& effective);

// Line-drawn glyphs (chevrons): ID2D1DeviceContext::DrawLine for each line, with the same
// halo underneath.
struct Line
{
    D2D1_POINT_2F from;
    D2D1_POINT_2F to;
};
void DrawLines(ID2D1DeviceContext* dc, std::span<const Line> lines, ID2D1Brush* brush, float strokeWidth,
               const EffectiveAppearance& effective);

// Only the halo of text that something below this target draws (the DWM caption
// buttons, which show through what is drawn here): a ring for `textColor` from one to two
// pixels outside the glyphs, with everything inside it set back to `inside` (the glass
// color that was there), so the text underneath shows exactly as through the rest of
// the glass even if it sits a pixel away from where `format` puts it. Does nothing
// without effective.textHalo.
void DrawHaloAround(ID2D1DeviceContext* dc, const wchar_t* text, UINT32 length, IDWriteTextFormat* format,
                    const D2D1_RECT_F& rect, Rgb textColor, const D2D1_COLOR_F& inside,
                    const EffectiveAppearance& effective);

} // namespace te::TextHalo
