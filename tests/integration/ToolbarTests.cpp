// Toolbar (T062): layout, enabled states, press-and-release clicks, hover, tooltips on
// a real parent window, the icon-font fallback and a rendering smoke test.

#include <te/ui/Toolbar.h>

#include <commctrl.h>
#include <d3d11.h>
#include <gtest/gtest.h>
#include <wil/com.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{

using Button = te::Toolbar::Button;

D2D1_POINT_2F Center(const te::Toolbar& toolbar, Button button)
{
    const D2D1_RECT_F r = toolbar.ButtonRect(button);
    return D2D1::Point2F((r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f);
}

class ToolbarTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        m_parent = CreateWindowExW(0, L"STATIC", L"parent", WS_OVERLAPPEDWINDOW, 100, 100, 900, 300, nullptr,
                                   nullptr, nullptr, nullptr);
        ASSERT_NE(m_parent, nullptr);
        m_toolbar.Attach(m_parent, 144);
        m_toolbar.SetBounds(D2D1::RectF(0.0f, 40.0f, 900.0f, 80.0f));
        m_toolbar.SetCallback([this](Button b) { m_clicked.push_back(b); });
    }

    void TearDown() override
    {
        if (m_parent)
        {
            DestroyWindow(m_parent);
        }
    }

    HWND m_parent = nullptr;
    te::Toolbar m_toolbar;
    std::vector<Button> m_clicked;
};

TEST_F(ToolbarTest, ButtonsSitInARowAndLeaveRoomForTheAddressBar)
{
    const D2D1_RECT_F back = m_toolbar.ButtonRect(Button::Back);
    const D2D1_RECT_F refresh = m_toolbar.ButtonRect(Button::Refresh);
    EXPECT_FLOAT_EQ(back.left, te::Toolbar::kLeftPaddingDip);
    EXPECT_FLOAT_EQ(back.bottom - back.top, te::Toolbar::kButtonSizeDip);
    EXPECT_FLOAT_EQ((back.top + back.bottom) / 2.0f, 60.0f) << "centred in the 40-DIP row";
    EXPECT_FLOAT_EQ(refresh.right, te::Toolbar::ButtonsWidth());
    EXPECT_EQ(m_toolbar.ButtonAt(Center(m_toolbar, Button::Up)), Button::Up);
    EXPECT_FALSE(m_toolbar.ButtonAt(D2D1::Point2F(te::Toolbar::ButtonsWidth() + 20.0f, 60.0f)).has_value());
}

TEST_F(ToolbarTest, OnlyRefreshIsEnabledBeforeTheFirstNavigation)
{
    EXPECT_FALSE(m_toolbar.IsEnabled(Button::Back));
    EXPECT_FALSE(m_toolbar.IsEnabled(Button::Forward));
    EXPECT_FALSE(m_toolbar.IsEnabled(Button::Up));
    EXPECT_TRUE(m_toolbar.IsEnabled(Button::Refresh));
    m_toolbar.SetState(true, false, true);
    EXPECT_TRUE(m_toolbar.IsEnabled(Button::Back));
    EXPECT_FALSE(m_toolbar.IsEnabled(Button::Forward));
    EXPECT_TRUE(m_toolbar.IsEnabled(Button::Up));
}

TEST_F(ToolbarTest, ClickIsPressAndReleaseOnTheSameEnabledButton)
{
    m_toolbar.SetState(true, true, true);
    EXPECT_TRUE(m_toolbar.OnPointerDown(Center(m_toolbar, Button::Back)));
    EXPECT_EQ(m_toolbar.Pressed(), Button::Back);
    EXPECT_TRUE(m_toolbar.OnPointerUp(Center(m_toolbar, Button::Back)));
    EXPECT_EQ(m_clicked, (std::vector<Button>{Button::Back}));
    EXPECT_FALSE(m_toolbar.Pressed().has_value());

    // Pressed on Up, released on Refresh: cancelled.
    m_toolbar.OnPointerDown(Center(m_toolbar, Button::Up));
    m_toolbar.OnPointerUp(Center(m_toolbar, Button::Refresh));
    EXPECT_EQ(m_clicked.size(), 1u);
}

TEST_F(ToolbarTest, DisabledButtonsDoNotClickButStillOwnThePointer)
{
    EXPECT_TRUE(m_toolbar.OnPointerDown(Center(m_toolbar, Button::Forward)));
    EXPECT_FALSE(m_toolbar.Pressed().has_value());
    EXPECT_FALSE(m_toolbar.OnPointerUp(Center(m_toolbar, Button::Forward)));
    EXPECT_TRUE(m_clicked.empty());
    EXPECT_FALSE(m_toolbar.OnPointerDown(D2D1::Point2F(600.0f, 60.0f))) << "not a button";
}

TEST_F(ToolbarTest, HoverFollowsThePointer)
{
    m_toolbar.OnPointerMove(Center(m_toolbar, Button::Up));
    EXPECT_EQ(m_toolbar.Hot(), Button::Up);
    m_toolbar.OnPointerMove(D2D1::Point2F(600.0f, 60.0f));
    EXPECT_FALSE(m_toolbar.Hot().has_value());
    m_toolbar.OnPointerMove(Center(m_toolbar, Button::Refresh));
    m_toolbar.OnPointerLeave();
    EXPECT_FALSE(m_toolbar.Hot().has_value());
}

TEST_F(ToolbarTest, TooltipsCoverEachButtonInPixels)
{
    const HWND tooltip = m_toolbar.Tooltip();
    ASSERT_NE(tooltip, nullptr);
    EXPECT_EQ(SendMessageW(tooltip, TTM_GETTOOLCOUNT, 0, 0), 4);

    wchar_t text[128]{};
    TTTOOLINFOW tool{sizeof(tool)};
    tool.hwnd = m_parent;
    tool.uId = static_cast<UINT_PTR>(Button::Refresh);
    tool.lpszText = text;
    ASSERT_TRUE(SendMessageW(tooltip, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&tool)));
    EXPECT_STREQ(text, L"Refresh (F5)");
    EXPECT_TRUE(tool.uFlags & TTF_SUBCLASS);
    const D2D1_RECT_F dip = m_toolbar.ButtonRect(Button::Refresh);
    EXPECT_NEAR(tool.rect.left, dip.left * 1.5f, 1.0f) << "144 DPI";
    EXPECT_NEAR(tool.rect.bottom, dip.bottom * 1.5f, 1.0f);

    m_toolbar.SetDpi(96);
    ASSERT_TRUE(SendMessageW(tooltip, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&tool)));
    EXPECT_NEAR(tool.rect.left, dip.left, 1.0f);
}

TEST(ToolbarRender, GlyphsDrawInTheIconFontAndDisabledOnesAreDimmer)
{
    wil::com_ptr<ID3D11Device> d3d;
    ASSERT_HRESULT_SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                               D3D11_SDK_VERSION, &d3d, nullptr, nullptr));
    wil::com_ptr<ID2D1Factory1> factory;
    ASSERT_HRESULT_SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.addressof()));
    wil::com_ptr<ID2D1Device> device;
    ASSERT_HRESULT_SUCCEEDED(factory->CreateDevice(d3d.query<IDXGIDevice>().get(), &device));
    wil::com_ptr<ID2D1DeviceContext> dc;
    ASSERT_HRESULT_SUCCEEDED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc));
    constexpr UINT width = 200, height = 40;
    const D2D1_PIXEL_FORMAT format{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    wil::com_ptr<ID2D1Bitmap1> target, readback;
    ASSERT_HRESULT_SUCCEEDED(dc->CreateBitmap(
        {width, height}, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target));
    ASSERT_HRESULT_SUCCEEDED(dc->CreateBitmap(
        {width, height}, nullptr, 0,
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
        &readback));
    dc->SetTarget(target.get());
    wil::com_ptr<IDWriteFactory3> dwrite;
    ASSERT_HRESULT_SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                                 reinterpret_cast<IUnknown**>(dwrite.put())));
    te::TextFormats formats(dwrite.get());
    ASSERT_HRESULT_SUCCEEDED(formats.Rebuild(1.0f));

    te::Toolbar toolbar;
    toolbar.SetTextFormats(&formats);
    toolbar.SetBounds(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)));
    toolbar.SetState(true, false, true); // Forward disabled
    te::EffectiveAppearance e;
    e.text = {0xFF, 0xFF, 0xFF};
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    toolbar.Render(dc.get(), e);
    ASSERT_HRESULT_SUCCEEDED(dc->EndDraw());
    EXPECT_TRUE(toolbar.GlyphFamily() == L"Segoe Fluent Icons" ||
                toolbar.GlyphFamily() == L"Segoe MDL2 Assets");

    const D2D1_POINT_2U origin{0, 0};
    const D2D1_RECT_U all{0, 0, width, height};
    ASSERT_HRESULT_SUCCEEDED(readback->CopyFromBitmap(&origin, target.get(), &all));
    D2D1_MAPPED_RECT mapped{};
    ASSERT_HRESULT_SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
    const auto maxAlpha = [&](Button button) {
        const D2D1_RECT_F r = toolbar.ButtonRect(button);
        int best = 0;
        for (UINT y = static_cast<UINT>(r.top); y < static_cast<UINT>(r.bottom); ++y)
        {
            for (UINT x = static_cast<UINT>(r.left); x < static_cast<UINT>(r.right); ++x)
            {
                best = std::max<int>(best, mapped.bits[static_cast<size_t>(y) * mapped.pitch + x * 4 + 3]);
            }
        }
        return best;
    };
    const int back = maxAlpha(Button::Back);
    const int forward = maxAlpha(Button::Forward);
    ASSERT_HRESULT_SUCCEEDED(readback->Unmap());
    EXPECT_GT(back, 200) << "an enabled glyph is drawn at full strength";
    EXPECT_GT(forward, 0) << "a disabled glyph is still drawn";
    EXPECT_LT(forward, back) << "and dimmer";
}

} // namespace
