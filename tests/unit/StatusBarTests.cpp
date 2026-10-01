// StatusBar (T038): the appearance summary (applied mode, surface and tint, fallback
// reason), the left-hand text priority (operation > transient > counts), the transient
// timer, and that Render draws text in the status region.

#include <te/ui/StatusBar.h>

#include <d3d11.h>
#include <gtest/gtest.h>
#include <wil/com.h>
#include <wil/result.h>

#include <cstdint>
#include <string>

namespace
{

te::AppearanceSettings Requested(te::BackdropMode mode, double surface, double tint)
{
    te::AppearanceSettings settings;
    settings.backdropMode = mode;
    settings.surfaceOpacity = surface;
    settings.tintOpacity = tint;
    return settings;
}

te::EffectiveAppearance Effective(te::BackdropMode requested, te::BackdropMode applied,
                                  te::FallbackReason reason = te::FallbackReason::None)
{
    te::EffectiveAppearance e;
    e.requested = requested;
    e.applied = applied;
    e.reason = reason;
    return e;
}

std::wstring Summary(const te::AppearanceSettings& requested, const te::EffectiveAppearance& effective)
{
    return te::StatusBar::FormatAppearance(te::StatusBar::Strings{}, requested, effective);
}

// ---------------------------------------------------------------------------
// Appearance summary (UI §1, §3, §6)
// ---------------------------------------------------------------------------

TEST(StatusBar, MicaShowsSurfaceAndTint)
{
    EXPECT_EQ(Summary(Requested(te::BackdropMode::Mica, 0.0, 0.20),
                      Effective(te::BackdropMode::Mica, te::BackdropMode::Mica)),
              L"Mica · Surface 0% · Tint 20%");
}

TEST(StatusBar, AcrylicRoundsPercentages)
{
    EXPECT_EQ(Summary(Requested(te::BackdropMode::Acrylic, 0.35, 0.8),
                      Effective(te::BackdropMode::Acrylic, te::BackdropMode::Acrylic)),
              L"Acrylic · Surface 35% · Tint 80%");
}

TEST(StatusBar, TransparentShowsSurfaceAndTint)
{
    EXPECT_EQ(Summary(Requested(te::BackdropMode::Transparent, 0.0, 0.20),
                      Effective(te::BackdropMode::Transparent, te::BackdropMode::Transparent)),
              L"Transparent · Surface 0% · Tint 20%");
}

TEST(StatusBar, SolidShowsOnlyTheMode)
{
    EXPECT_EQ(Summary(Requested(te::BackdropMode::Solid, 0.5, 0.5),
                      Effective(te::BackdropMode::Solid, te::BackdropMode::Solid)),
              L"Solid");
}

TEST(StatusBar, FallbackNamesTheAppliedModeAndTheReason)
{
    const auto requested = Requested(te::BackdropMode::Mica, 0.0, 0.2);
    const struct
    {
        te::FallbackReason reason;
        const wchar_t* expected;
    } cases[] = {
        {te::FallbackReason::HighContrast, L"Solid (fallback: high contrast)"},
        {te::FallbackReason::TransparencyOff, L"Solid (fallback: transparency effects off)"},
        {te::FallbackReason::BackdropUnsupported, L"Solid (fallback: requires Windows 11 build 22621)"},
        {te::FallbackReason::BackdropApplyFailed, L"Solid (fallback: backdrop unavailable)"},
    };
    for (const auto& c : cases)
    {
        EXPECT_EQ(Summary(requested, Effective(te::BackdropMode::Mica, te::BackdropMode::Solid, c.reason)),
                  c.expected);
    }
}

// High contrast while Solid was requested: the applied mode is what was asked for, so no
// fallback is shown (UI §3 shows it only when applied != requested).
TEST(StatusBar, NoFallbackWhenAppliedEqualsRequested)
{
    EXPECT_EQ(Summary(Requested(te::BackdropMode::Solid, 0, 0),
                      Effective(te::BackdropMode::Solid, te::BackdropMode::Solid,
                                te::FallbackReason::HighContrast)),
              L"Solid");
}

TEST(StatusBar, UsesSuppliedStrings)
{
    te::StatusBar::Strings strings;
    strings.modeMica = L"Glimmer";
    strings.appearanceFmt = L"[%1|%2!u!|%3!u!]";
    EXPECT_EQ(te::StatusBar::FormatAppearance(strings, Requested(te::BackdropMode::Mica, 0.1, 0.4),
                                              Effective(te::BackdropMode::Mica, te::BackdropMode::Mica)),
              L"[Glimmer|10|40]");
}

TEST(StatusBar, LoadWithoutIdsKeepsDefaults)
{
    const te::StatusBar::Strings loaded = te::StatusBar::Strings::Load(nullptr, {});
    EXPECT_EQ(loaded.modeMica, L"Mica");
    EXPECT_EQ(loaded.fallbackApplyFailed, L"backdrop unavailable");
}

// ---------------------------------------------------------------------------
// Left-hand text
// ---------------------------------------------------------------------------

TEST(StatusBar, LeftTextIsEmptyUntilCountsAreSet)
{
    te::StatusBar bar;
    EXPECT_EQ(bar.LeftText(), L"");
}

TEST(StatusBar, CountsShowSelectionOnlyWhenSomethingIsSelected)
{
    te::StatusBar bar;
    bar.SetItemCounts(1204, 0);
    EXPECT_EQ(bar.LeftText(), L"1204 items");
    bar.SetItemCounts(1204, 3);
    EXPECT_EQ(bar.LeftText(), L"1204 items   3 selected");
}

TEST(StatusBar, OperationBeatsTransientBeatsCounts)
{
    te::StatusBar bar;
    bar.SetItemCounts(10, 0);
    bar.SetTransientMessage(L"Appearance settings were reset", 5000);
    EXPECT_EQ(bar.LeftText(), L"Appearance settings were reset");
    bar.SetOperationMessage(L"Copying 3 items…");
    EXPECT_EQ(bar.LeftText(), L"Copying 3 items…");
    bar.ClearOperationMessage();
    EXPECT_EQ(bar.LeftText(), L"Appearance settings were reset");
    EXPECT_TRUE(bar.OnTimer(te::StatusBar::kTransientTimerId));
    EXPECT_EQ(bar.LeftText(), L"10 items");
}

TEST(StatusBar, OnTimerIgnoresOtherTimers)
{
    te::StatusBar bar;
    bar.SetTransientMessage(L"hello", 5000);
    EXPECT_FALSE(bar.OnTimer(te::StatusBar::kTransientTimerId + 1));
    EXPECT_EQ(bar.LeftText(), L"hello");
}

// The transient message expires through a real timer on the owner window.
TEST(StatusBar, TransientMessageExpiresOnTheOwnerTimer)
{
    const HWND owner =
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ASSERT_NE(owner, nullptr);
    {
        te::StatusBar bar;
        bar.Attach(owner);
        bar.SetTransientMessage(L"short", 50);

        MSG msg{};
        const ULONGLONG deadline = GetTickCount64() + 2000;
        bool fired = false;
        while (!fired && GetTickCount64() < deadline)
        {
            if (PeekMessageW(&msg, owner, WM_TIMER, WM_TIMER, PM_REMOVE))
            {
                fired = bar.OnTimer(msg.wParam);
            }
            else
            {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_TIMER);
            }
        }
        EXPECT_TRUE(fired);
        EXPECT_EQ(bar.LeftText(), L"");
    }
    DestroyWindow(owner);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

TEST(StatusBar, RenderDrawsTextInsideTheBounds)
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

    constexpr UINT width = 400, height = 24;
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

    te::StatusBar bar;
    bar.SetTextFormats(&formats);
    bar.SetBounds(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)));
    bar.SetItemCounts(42, 0);
    te::EffectiveAppearance e = Effective(te::BackdropMode::Mica, te::BackdropMode::Mica);
    e.text = {0xFF, 0xFF, 0xFF};
    e.secondaryText = {0xFF, 0xFF, 0xFF};
    bar.SetAppearance(Requested(te::BackdropMode::Mica, 0, 0.2), e);

    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    bar.Render(dc.get(), e);
    ASSERT_HRESULT_SUCCEEDED(dc->EndDraw());
    const D2D1_POINT_2U origin{0, 0};
    const D2D1_RECT_U all{0, 0, width, height};
    ASSERT_HRESULT_SUCCEEDED(readback->CopyFromBitmap(&origin, target.get(), &all));

    D2D1_MAPPED_RECT mapped{};
    ASSERT_HRESULT_SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
    // Ink on the left (counts) and on the right (appearance), nothing inside the padding.
    int leftInk = 0, rightInk = 0, paddingInk = 0;
    for (UINT y = 0; y < height; ++y)
    {
        const std::uint8_t* row = mapped.bits + static_cast<size_t>(y) * mapped.pitch;
        for (UINT x = 0; x < width; ++x)
        {
            const bool ink = row[x * 4 + 3] > 0;
            if (x < static_cast<UINT>(te::StatusBar::kPaddingDip) ||
                x >= width - static_cast<UINT>(te::StatusBar::kPaddingDip))
            {
                paddingInk += ink ? 1 : 0;
            }
            else if (x < width / 2)
            {
                leftInk += ink ? 1 : 0;
            }
            else
            {
                rightInk += ink ? 1 : 0;
            }
        }
    }
    ASSERT_HRESULT_SUCCEEDED(readback->Unmap());
    EXPECT_GT(leftInk, 0);
    EXPECT_GT(rightInk, 0);
    EXPECT_EQ(paddingInk, 0);
}

} // namespace
