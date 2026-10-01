// End-to-end legibility (T039, research R-05, V-1a): resolve the appearance for dark,
// light and high contrast, render the surface layers and the text scrim exactly as the
// window does, blend the rendered pixel of each text area over every backdrop extreme,
// and check the text and secondary-text colors reach 4.5:1 against the result.

#include <te/appearance/Contrast.h>
#include <te/appearance/ThemeManager.h>
#include <te/render/SurfacePainter.h>

#include <d3d11.h>
#include <gtest/gtest.h>
#include <wil/com.h>
#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

namespace C = te::Contrast;

constexpr UINT kSize = 64;

// One pixel of the premultiplied render target, as 0–1 floats.
struct Premultiplied
{
    float r, g, b, a;
};

class Renderer
{
  public:
    Renderer()
    {
        wil::com_ptr<ID3D11Device> d3d;
        THROW_IF_FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                          &d3d, nullptr, nullptr));
        wil::com_ptr<ID2D1Factory1> factory;
        THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.addressof()));
        wil::com_ptr<ID2D1Device> device;
        THROW_IF_FAILED(factory->CreateDevice(d3d.query<IDXGIDevice>().get(), &device));
        THROW_IF_FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_dc));
        const D2D1_PIXEL_FORMAT format{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        THROW_IF_FAILED(m_dc->CreateBitmap({kSize, kSize}, nullptr, 0,
                                           D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format),
                                           &m_target));
        THROW_IF_FAILED(m_dc->CreateBitmap(
            {kSize, kSize}, nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
            &m_readback));
        m_dc->SetTarget(m_target.get());
    }

    // The status bar region covers the whole target; the text scrim is painted over it
    // as MainWindow::Render does. Returns the pixel in the middle.
    Premultiplied RenderTextArea(const te::EffectiveAppearance& e)
    {
        te::MainLayout layout;
        layout.statusBar = D2D1::RectF(0, 0, kSize, kSize);
        m_dc->BeginDraw();
        te::SurfacePainter::PaintSurfaces(m_dc.get(), layout, e);
        te::SurfacePainter::PaintTextScrim(m_dc.get(), layout.statusBar, e);
        THROW_IF_FAILED(m_dc->EndDraw());

        const D2D1_POINT_2U origin{0, 0};
        const D2D1_RECT_U all{0, 0, kSize, kSize};
        THROW_IF_FAILED(m_readback->CopyFromBitmap(&origin, m_target.get(), &all));
        D2D1_MAPPED_RECT mapped{};
        THROW_IF_FAILED(m_readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
        const std::uint8_t* p = mapped.bits + static_cast<size_t>(kSize / 2) * mapped.pitch + (kSize / 2) * 4;
        const Premultiplied pixel{p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f, p[3] / 255.0f};
        THROW_IF_FAILED(m_readback->Unmap());
        return pixel;
    }

  private:
    wil::com_ptr<ID2D1DeviceContext> m_dc;
    wil::com_ptr<ID2D1Bitmap1> m_target;
    wil::com_ptr<ID2D1Bitmap1> m_readback;
};

// What the DWM shows: the premultiplied app pixel over the backdrop color.
te::Rgb OverBackdrop(const Premultiplied& p, te::Rgb backdrop)
{
    const auto channel = [&](float premultiplied, std::uint8_t under) {
        const float value = premultiplied * 255.0f + (1.0f - p.a) * under;
        return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
    };
    return {channel(p.r, backdrop.r), channel(p.g, backdrop.g), channel(p.b, backdrop.b)};
}

struct Case
{
    bool dark;
    bool highContrast;
    te::BackdropMode mode;
    double surface;
    double tint;
};

std::string Name(const Case& c)
{
    const char* mode = c.mode == te::BackdropMode::Acrylic ? "Acrylic"
                       : c.mode == te::BackdropMode::Mica  ? "Mica"
                                                           : "Solid";
    return std::string(c.highContrast ? "HighContrast "
                       : c.dark       ? "Dark "
                                      : "Light ") +
           mode + " surface=" + std::to_string(c.surface) + " tint=" + std::to_string(c.tint);
}

std::vector<Case> Cases()
{
    std::vector<Case> cases;
    for (const bool dark : {true, false})
    {
        for (const auto mode : {te::BackdropMode::Acrylic, te::BackdropMode::Mica, te::BackdropMode::Solid})
        {
            for (const double surface : {0.0, 0.3, 0.9})
            {
                for (const double tint : {0.0, 0.2, 0.8})
                {
                    cases.push_back({dark, false, mode, surface, tint});
                }
            }
        }
    }
    cases.push_back({false, true, te::BackdropMode::Acrylic, 0.0, 0.2});
    cases.push_back({true, true, te::BackdropMode::Mica, 0.5, 0.8});
    return cases;
}

TEST(Legibility, RenderedTextAreasReachTextContrastOverEveryBackdrop)
{
    Renderer renderer;
    const te::ThemeManager themes;
    for (const Case& c : Cases())
    {
        SCOPED_TRACE(Name(c));
        te::RenderingCapabilities caps;
        caps.systemBackdropSupported = true;
        caps.darkMode = c.dark;
        caps.highContrast = c.highContrast;
        caps.accent = {0x00, 0x78, 0xD4};
        te::AppearanceSettings settings;
        settings.backdropMode = c.mode;
        settings.surfaceOpacity = c.surface;
        settings.tintOpacity = c.tint;
        const te::EffectiveAppearance e = themes.Resolve(settings, caps);

        const Premultiplied pixel = renderer.RenderTextArea(e);
        const C::BackdropExtremeSet extremes = C::BackdropExtremes(e.applied, c.dark, e.base);
        for (const te::Rgb backdrop : extremes.Span())
        {
            const te::Rgb shown = OverBackdrop(pixel, backdrop);
            // 8-bit rounding in the render target can move the ratio by a few hundredths.
            EXPECT_GE(C::ContrastRatio(e.text, shown), C::kTextMinimum - 0.05)
                << "text over backdrop " << int(backdrop.r) << "," << int(backdrop.g) << ","
                << int(backdrop.b);
            EXPECT_GE(C::ContrastRatio(e.secondaryText, shown), C::kTextMinimum - 0.05) << "secondary text";
        }
    }
}

} // namespace
