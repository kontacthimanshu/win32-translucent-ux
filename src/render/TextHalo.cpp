#include <te/render/TextHalo.h>

#include <te/appearance/Contrast.h>

#include <wil/com.h>

#include <utility>

namespace te::TextHalo
{

namespace
{

// The eight neighbours at kRadiusPx: a one-pixel outline all around each glyph.
constexpr std::pair<float, float> kOffsets[] = {
    {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 0.0f},
    {1.0f, 0.0f},   {-1.0f, 1.0f}, {0.0f, 1.0f},  {1.0f, 1.0f},
};

// Draws `draw(dx, dy, options)` once per halo offset in the halo color, then once at
// (0, 0) in the brush's own color.
template <typename Draw>
void WithHalo(ID2D1DeviceContext* dc, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options,
              const EffectiveAppearance& effective, Draw&& draw)
{
    wil::com_ptr<ID2D1SolidColorBrush> solid;
    if (!effective.textHalo || !brush || FAILED(brush->QueryInterface(IID_PPV_ARGS(solid.put()))))
    {
        draw(0.0f, 0.0f, options);
        return;
    }
    const D2D1_COLOR_F color = solid->GetColor();
    const Rgb text{static_cast<std::uint8_t>(color.r * 255.0f + 0.5f),
                   static_cast<std::uint8_t>(color.g * 255.0f + 0.5f),
                   static_cast<std::uint8_t>(color.b * 255.0f + 0.5f)};
    const Rgb halo = ColorFor(text);
    solid->SetColor(D2D1::ColorF(halo.r / 255.0f, halo.g / 255.0f, halo.b / 255.0f, kAlpha * color.a));
    // In the halo color only: a color font would draw the emoji itself eight times.
    const auto haloOptions =
        static_cast<D2D1_DRAW_TEXT_OPTIONS>(options & ~D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    dc->GetDpi(&dpiX, &dpiY);
    const float stepX = kRadiusPx * 96.0f / dpiX;
    const float stepY = kRadiusPx * 96.0f / dpiY;
    for (const auto& [dx, dy] : kOffsets)
    {
        draw(dx * stepX, dy * stepY, haloOptions);
    }
    solid->SetColor(color);
    draw(0.0f, 0.0f, options);
}

} // namespace

Rgb ColorFor(Rgb text)
{
    return Contrast::RelativeLuminance(text) > 0.5 ? Rgb{0x00, 0x00, 0x00} : Rgb{0xFF, 0xFF, 0xFF};
}

void DrawTextW(ID2D1DeviceContext* dc, const wchar_t* text, UINT32 length, IDWriteTextFormat* format,
               const D2D1_RECT_F& rect, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options,
               const EffectiveAppearance& effective)
{
    WithHalo(dc, brush, options, effective, [&](float dx, float dy, D2D1_DRAW_TEXT_OPTIONS o) {
        dc->DrawTextW(text, length, format,
                      D2D1::RectF(rect.left + dx, rect.top + dy, rect.right + dx, rect.bottom + dy), brush,
                      o);
    });
}

void DrawTextLayout(ID2D1DeviceContext* dc, D2D1_POINT_2F origin, IDWriteTextLayout* layout,
                    ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options, const EffectiveAppearance& effective)
{
    WithHalo(dc, brush, options, effective, [&](float dx, float dy, D2D1_DRAW_TEXT_OPTIONS o) {
        dc->DrawTextLayout(D2D1::Point2F(origin.x + dx, origin.y + dy), layout, brush, o);
    });
}

void DrawLines(ID2D1DeviceContext* dc, std::span<const Line> lines, ID2D1Brush* brush, float strokeWidth,
               const EffectiveAppearance& effective)
{
    WithHalo(dc, brush, D2D1_DRAW_TEXT_OPTIONS_NONE, effective,
             [&](float dx, float dy, D2D1_DRAW_TEXT_OPTIONS) {
                 for (const Line& line : lines)
                 {
                     dc->DrawLine(D2D1::Point2F(line.from.x + dx, line.from.y + dy),
                                  D2D1::Point2F(line.to.x + dx, line.to.y + dy), brush, strokeWidth);
                 }
             });
}

void DrawHaloAround(ID2D1DeviceContext* dc, const wchar_t* text, UINT32 length, IDWriteTextFormat* format,
                    const D2D1_RECT_F& rect, Rgb textColor, const D2D1_COLOR_F& inside,
                    const EffectiveAppearance& effective)
{
    if (!effective.textHalo || !format)
    {
        return;
    }
    const Rgb halo = ColorFor(textColor);
    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(
            D2D1::ColorF(halo.r / 255.0f, halo.g / 255.0f, halo.b / 255.0f, kAlpha), brush.put())))
    {
        return;
    }
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    dc->GetDpi(&dpiX, &dpiY);
    const float pxX = 96.0f / dpiX;
    const float pxY = 96.0f / dpiY;
    const auto stamp = [&](int radius) {
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const float x = static_cast<float>(dx) * pxX;
                const float y = static_cast<float>(dy) * pxY;
                dc->DrawTextW(text, length, format,
                              D2D1::RectF(rect.left + x, rect.top + y, rect.right + x, rect.bottom + y),
                              brush.get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
        }
    };
    // The DWM rasterizes its glyphs itself and may place them a pixel away from ours. So
    // the halo is the glyph grown by two pixels, and the glyph grown by one pixel is then
    // set back to the glass color: a real glyph up to a pixel off still shows through as
    // it does everywhere else, ringed by the halo one pixel out.
    stamp(2);
    const D2D1_PRIMITIVE_BLEND previous = dc->GetPrimitiveBlend();
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    brush->SetColor(inside);
    stamp(1);
    dc->SetPrimitiveBlend(previous);
}

} // namespace te::TextHalo
