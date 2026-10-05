// SurfacePainter (T036): renders into an offscreen premultiplied BGRA bitmap on a WARP
// device and checks the pixels: Solid is opaque base, translucent modes are the surface
// layer then the tint layer, caption buttons stay at zero alpha, and a text scrim gives
// exactly the pixels of a region painted at the floor opacity.

#include <te/render/SurfacePainter.h>

#include <d3d11.h>
#include <gtest/gtest.h>
#include <wil/com.h>
#include <wil/result.h>

#include <cmath>
#include <cstdint>
#include <utility>

namespace
{

constexpr UINT kWidth = 200;
constexpr UINT kHeight = 120;
constexpr te::Rgb kDarkBase{0x20, 0x20, 0x20};
constexpr te::Rgb kAccent{0x00, 0x78, 0xD4};

struct Pixel // premultiplied BGRA
{
    int b, g, r, a;
};

class Offscreen
{
  public:
    Offscreen()
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
        const D2D1_BITMAP_PROPERTIES1 target =
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, 96.0f, 96.0f);
        THROW_IF_FAILED(m_dc->CreateBitmap({kWidth, kHeight}, nullptr, 0, target, &m_target));
        const D2D1_BITMAP_PROPERTIES1 readback = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format, 96.0f, 96.0f);
        THROW_IF_FAILED(m_dc->CreateBitmap({kWidth, kHeight}, nullptr, 0, readback, &m_readback));
        m_dc->SetTarget(m_target.get());
    }

    ID2D1DeviceContext* Begin()
    {
        m_dc->BeginDraw();
        return m_dc.get();
    }

    void End()
    {
        THROW_IF_FAILED(m_dc->EndDraw());
        const D2D1_POINT_2U origin{0, 0};
        const D2D1_RECT_U all{0, 0, kWidth, kHeight};
        THROW_IF_FAILED(m_readback->CopyFromBitmap(&origin, m_target.get(), &all));
    }

    Pixel At(UINT x, UINT y)
    {
        D2D1_MAPPED_RECT mapped{};
        THROW_IF_FAILED(m_readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
        const std::uint8_t* p =
            mapped.bits + static_cast<size_t>(y) * mapped.pitch + static_cast<size_t>(x) * 4;
        const Pixel pixel{p[0], p[1], p[2], p[3]};
        THROW_IF_FAILED(m_readback->Unmap());
        return pixel;
    }

  private:
    wil::com_ptr<ID2D1DeviceContext> m_dc;
    wil::com_ptr<ID2D1Bitmap1> m_target;
    wil::com_ptr<ID2D1Bitmap1> m_readback;
};

// A 200 x 120 DIP window: caption 0–30 with buttons at 140–200, toolbar 30–60, pane and
// file list 60–100, status bar 100–120.
te::MainLayout TestLayout()
{
    te::MainLayout layout;
    layout.caption = D2D1::RectF(0, 0, 200, 30);
    layout.toolbar = D2D1::RectF(0, 30, 200, 60);
    layout.navigationPane = D2D1::RectF(0, 60, 60, 100);
    layout.fileList = D2D1::RectF(60, 60, 200, 100);
    layout.statusBar = D2D1::RectF(0, 100, 200, 120);
    layout.captionButtons = D2D1::RectF(140, 0, 200, 30);
    return layout;
}

te::EffectiveAppearance Appearance(te::BackdropMode mode, float surfaceAlpha, float tintAlpha,
                                   float scrim = 0.0f)
{
    te::EffectiveAppearance e;
    e.requested = mode;
    e.applied = mode;
    e.base = kDarkBase;
    e.tint = kAccent;
    e.surfaceAlpha = mode == te::BackdropMode::Solid ? 1.0f : surfaceAlpha;
    e.tintAlpha = mode == te::BackdropMode::Solid ? 0.0f : tintAlpha;
    e.textScrimAlpha = scrim;
    return e;
}

// Surface layer then tint layer, premultiplied, as 0–255 values.
Pixel ExpectedLayers(float surfaceAlpha, float tintAlpha)
{
    const auto channel = [&](std::uint8_t base, std::uint8_t tint) {
        const double surface = base / 255.0 * surfaceAlpha;
        return static_cast<int>(
            std::lround((tint / 255.0 * tintAlpha + surface * (1.0 - tintAlpha)) * 255.0));
    };
    const double alpha = tintAlpha + surfaceAlpha * (1.0 - tintAlpha);
    return {channel(kDarkBase.b, kAccent.b), channel(kDarkBase.g, kAccent.g), channel(kDarkBase.r, kAccent.r),
            static_cast<int>(std::lround(alpha * 255.0))};
}

void ExpectPixel(const Pixel& actual, const Pixel& expected, const char* where)
{
    SCOPED_TRACE(where);
    EXPECT_NEAR(actual.b, expected.b, 2);
    EXPECT_NEAR(actual.g, expected.g, 2);
    EXPECT_NEAR(actual.r, expected.r, 2);
    EXPECT_NEAR(actual.a, expected.a, 2);
}

TEST(SurfacePainter, SolidFillsEveryRegionWithOpaqueBase)
{
    Offscreen target;
    te::SurfacePainter::PaintSurfaces(target.Begin(), TestLayout(),
                                      Appearance(te::BackdropMode::Solid, 0, 0));
    target.End();

    const Pixel base{kDarkBase.b, kDarkBase.g, kDarkBase.r, 255};
    ExpectPixel(target.At(10, 10), base, "caption");
    ExpectPixel(target.At(10, 45), base, "toolbar");
    ExpectPixel(target.At(30, 80), base, "navigation pane");
    ExpectPixel(target.At(120, 80), base, "file list");
    ExpectPixel(target.At(100, 110), base, "status bar");
}

TEST(SurfacePainter, TranslucentPaintsSurfaceThenTint)
{
    for (const auto mode : {te::BackdropMode::Mica, te::BackdropMode::Acrylic})
    {
        Offscreen target;
        te::SurfacePainter::PaintSurfaces(target.Begin(), TestLayout(), Appearance(mode, 0.5f, 0.2f));
        target.End();

        const Pixel expected = ExpectedLayers(0.5f, 0.2f);
        ExpectPixel(target.At(10, 10), expected, "caption");
        ExpectPixel(target.At(120, 80), expected, "file list");
        ExpectPixel(target.At(100, 110), expected, "status bar");
    }
}

TEST(SurfacePainter, TransparentSurfaceAndNoTintLeavesBackdropVisible)
{
    Offscreen target;
    te::SurfacePainter::PaintSurfaces(target.Begin(), TestLayout(), Appearance(te::BackdropMode::Mica, 0, 0));
    target.End();
    ExpectPixel(target.At(120, 80), {0, 0, 0, 0}, "file list");
}

TEST(SurfacePainter, CaptionButtonsStayTransparentInEveryMode)
{
    for (const auto mode : {te::BackdropMode::Solid, te::BackdropMode::Mica, te::BackdropMode::Acrylic})
    {
        Offscreen target;
        te::SurfacePainter::PaintSurfaces(target.Begin(), TestLayout(), Appearance(mode, 0.6f, 0.4f));
        target.End();
        ExpectPixel(target.At(170, 15), {0, 0, 0, 0}, "caption buttons");
        EXPECT_GT(target.At(100, 15).a, 0) << "caption left of the buttons is painted";
    }
}

// The floor replaces the surface layer inside the text area, so the pixels equal a
// region painted at the floor opacity; outside the area the user's opacity is kept.
TEST(SurfacePainter, TextScrimEqualsPaintingAtTheFloor)
{
    const D2D1_RECT_F row = D2D1::RectF(60, 70, 200, 90);

    Offscreen scrimmed;
    ID2D1DeviceContext* dc = scrimmed.Begin();
    const te::EffectiveAppearance e = Appearance(te::BackdropMode::Acrylic, 0.1f, 0.3f, 0.65f);
    te::SurfacePainter::PaintSurfaces(dc, TestLayout(), e);
    te::SurfacePainter::PaintTextScrim(dc, row, e);
    scrimmed.End();

    ExpectPixel(scrimmed.At(120, 80), ExpectedLayers(0.65f, 0.3f), "inside the text area");
    ExpectPixel(scrimmed.At(120, 65), ExpectedLayers(0.1f, 0.3f), "file list outside the text area");
}

TEST(SurfacePainter, TextScrimDoesNothingBelowTheSurface)
{
    const D2D1_RECT_F row = D2D1::RectF(60, 70, 200, 90);
    Offscreen target;
    ID2D1DeviceContext* dc = target.Begin();
    const te::EffectiveAppearance e = Appearance(te::BackdropMode::Mica, 0.5f, 0.2f, 0.0f);
    te::SurfacePainter::PaintSurfaces(dc, TestLayout(), e);
    te::SurfacePainter::PaintTextScrim(dc, row, e);
    target.End();
    ExpectPixel(target.At(120, 80), ExpectedLayers(0.5f, 0.2f), "text area");
}

// Depth: a shadow fading out below the toolbar and right of the navigation pane, and a
// light hairline along the top of each pane; nothing in high contrast.
TEST(SurfacePainter, DepthShadowsFadeAwayFromTheirEdge)
{
    Offscreen target;
    ID2D1DeviceContext* dc = target.Begin();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    te::SurfacePainter::PaintDepth(dc, TestLayout(), Appearance(te::BackdropMode::Transparent, 0, 0));
    target.End();

    // Below the toolbar (bottom at 60): darkest near the edge, gone past kShadowDip.
    const Pixel nearEdge = target.At(120, 61);
    const Pixel farther = target.At(120, 66);
    const Pixel past = target.At(120, 60 + static_cast<UINT>(te::SurfacePainter::kShadowDip) + 2);
    EXPECT_GT(nearEdge.a, farther.a);
    EXPECT_GT(farther.a, 0);
    EXPECT_EQ(past.a, 0);
    EXPECT_EQ(nearEdge.r, 0) << "shadows are black";

    // Right of the navigation pane (right edge at 60), below the toolbar shadow.
    EXPECT_GT(target.At(61, 90).a, target.At(66, 90).a);
    EXPECT_EQ(target.At(60 + static_cast<UINT>(te::SurfacePainter::kShadowDip) + 2, 90).a, 0);
}

TEST(SurfacePainter, DepthHighlightRunsAlongTheTopOfEachPane)
{
    Offscreen target;
    ID2D1DeviceContext* dc = target.Begin();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    te::SurfacePainter::PaintDepth(dc, TestLayout(), Appearance(te::BackdropMode::Mica, 0, 0));
    target.End();

    // The toolbar's top row (30) and the status bar's (100): white, partly transparent.
    for (const UINT y : {30u, 100u})
    {
        const Pixel p = target.At(20, y);
        EXPECT_GT(p.a, 0) << y;
        EXPECT_EQ(p.r, p.a) << "premultiplied white";
        EXPECT_EQ(target.At(20, y + 2).a, 0) << "one DIP only";
    }
    EXPECT_EQ(target.At(20, 10).a, 0) << "the caption gets none: the DWM draws the window edge";
}

TEST(SurfacePainter, NoDepthInHighContrast)
{
    te::EffectiveAppearance e = Appearance(te::BackdropMode::Solid, 1, 0);
    e.reason = te::FallbackReason::HighContrast;
    Offscreen target;
    ID2D1DeviceContext* dc = target.Begin();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    te::SurfacePainter::PaintDepth(dc, TestLayout(), e);
    target.End();
    for (const auto& [x, y] : {std::pair{120u, 61u}, std::pair{61u, 90u}, std::pair{20u, 30u}})
    {
        EXPECT_EQ(target.At(x, y).a, 0) << x << "," << y;
    }
}

// The rim: light along the top and left, shade along the bottom and right, nothing in
// the middle of the window or over the caption buttons while they show the DWM's material.
TEST(SurfacePainter, FrameBevelIsLitFromTheTopLeft)
{
    const te::MainLayout layout = TestLayout();
    Offscreen target;
    ID2D1DeviceContext* dc = target.Begin();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    te::SurfacePainter::PaintFrameBevel(dc, D2D1::SizeF(kWidth, kHeight), layout.captionButtons, true,
                                        Appearance(te::BackdropMode::Mica, 0, 0));
    target.End();

    const Pixel top = target.At(20, 0);
    const Pixel left = target.At(0, 20);
    EXPECT_GT(top.a, 0);
    EXPECT_EQ(top.r, top.a) << "premultiplied white";
    EXPECT_GT(left.a, 0);
    EXPECT_EQ(left.r, left.a);

    const Pixel bottom = target.At(kWidth - 20, kHeight - 1);
    const Pixel right = target.At(kWidth - 1, kHeight - 20);
    EXPECT_GT(bottom.a, 0);
    EXPECT_EQ(bottom.r, 0) << "black";
    EXPECT_GT(right.a, 0);
    EXPECT_EQ(right.r, 0);

    // The band fades inward: the crisp edge is stronger than the band behind it.
    EXPECT_GT(top.a, target.At(20, 3).a);
    EXPECT_EQ(target.At(kWidth / 2, kHeight / 2).a, 0) << "the middle stays clear";
    EXPECT_EQ(target.At(170, 0).a, 0) << "the caption buttons stay the DWM's";
    EXPECT_EQ(target.At(199, 10).a, 0);
}

// In Transparent the glass covers the caption buttons, so the rim runs across them too:
// skipping them would leave a darker block in the lit top edge.
TEST(SurfacePainter, FrameBevelCrossesTheCaptionButtonsInTransparent)
{
    const te::MainLayout layout = TestLayout();
    Offscreen target;
    ID2D1DeviceContext* dc = target.Begin();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    te::SurfacePainter::PaintFrameBevel(dc, D2D1::SizeF(kWidth, kHeight), layout.captionButtons, true,
                                        Appearance(te::BackdropMode::Transparent, 0, 0));
    target.End();
    EXPECT_GT(target.At(170, 0).a, 0) << "the crisp top edge continues over the buttons";
    EXPECT_GT(target.At(199, 10).a, 0);
}

TEST(SurfacePainter, FrameBevelFollowsTheRoundedCorners)
{
    Offscreen target;
    ID2D1DeviceContext* dc = target.Begin();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    te::SurfacePainter::PaintFrameBevel(dc, D2D1::SizeF(kWidth, kHeight), D2D1::RectF(0, 0, 0, 0), false,
                                        Appearance(te::BackdropMode::Mica, 0, 0));
    target.End();
    EXPECT_EQ(target.At(0, 0).a, 0) << "outside the rounded corner";
    EXPECT_GT(target.At(20, 0).a, 0);

    Offscreen square;
    dc = square.Begin();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    te::SurfacePainter::PaintFrameBevel(dc, D2D1::SizeF(kWidth, kHeight), D2D1::RectF(0, 0, 0, 0), true,
                                        Appearance(te::BackdropMode::Mica, 0, 0));
    square.End();
    EXPECT_GT(square.At(0, 0).a, 0) << "maximized: square corners";
}

TEST(SurfacePainter, NoFrameBevelInHighContrast)
{
    te::EffectiveAppearance e = Appearance(te::BackdropMode::Solid, 1, 0);
    e.reason = te::FallbackReason::HighContrast;
    Offscreen target;
    ID2D1DeviceContext* dc = target.Begin();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    te::SurfacePainter::PaintFrameBevel(dc, D2D1::SizeF(kWidth, kHeight), D2D1::RectF(0, 0, 0, 0), true, e);
    target.End();
    EXPECT_EQ(target.At(20, 0).a, 0);
    EXPECT_EQ(target.At(kWidth - 20, kHeight - 1).a, 0);
}

// Transparent: the glass color covers the caption buttons too (the DWM buttons show
// through it), so the window is one evenly tinted pane.
TEST(SurfacePainter, TransparentTintsTheCaptionButtonsToo)
{
    Offscreen target;
    te::SurfacePainter::PaintSurfaces(target.Begin(), TestLayout(),
                                      Appearance(te::BackdropMode::Transparent, 0.0f, 0.45f));
    target.End();
    ExpectPixel(target.At(170, 15), ExpectedLayers(0.0f, 0.45f), "caption buttons");
    ExpectPixel(target.At(170, 15), target.At(60, 15), "same as the rest of the caption");
}

// GlassColor drawn with COPY on a cleared pixel gives exactly what PaintSurfaces leaves.
TEST(SurfacePainter, GlassColorIsBothLayersInOne)
{
    for (const auto& [surface, tint] : {std::pair{0.0f, 0.45f}, std::pair{0.3f, 0.2f}, std::pair{0.6f, 0.0f}})
    {
        const te::EffectiveAppearance e = Appearance(te::BackdropMode::Transparent, surface, tint);
        Offscreen target;
        ID2D1DeviceContext* dc = target.Begin();
        dc->Clear(D2D1::ColorF(0, 0.0f));
        wil::com_ptr<ID2D1SolidColorBrush> brush;
        THROW_IF_FAILED(dc->CreateSolidColorBrush(te::SurfacePainter::GlassColor(e), &brush));
        dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
        dc->FillRectangle(D2D1::RectF(0, 0, 10, 10), brush.get());
        target.End();
        ExpectPixel(target.At(5, 5), ExpectedLayers(surface, tint), "glass color");
    }
}

} // namespace
