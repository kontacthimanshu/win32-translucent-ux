// Caption layout (T020): picker placement, drag region and top resize band at
// 96/144/192 DPI, normal and maximized (research R-03, UI contract §1–§2).

#include <te/window/CaptionHitTester.h>
#include <te/window/DpiManager.h>

#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <string>

namespace
{

// Windows 11 system metrics and caption-button bounds at common DPIs.
te::CaptionMetrics FakeMetrics(UINT dpi, bool maximized)
{
    const te::DpiManager scale(dpi);
    te::CaptionMetrics m;
    m.dpi = dpi;
    m.captionPx = scale.ToPx(23.0f);
    m.framePx = scale.ToPx(4.0f);
    m.paddedBorderPx = scale.ToPx(4.0f);
    m.clientSize = {scale.ToPx(1200.0f), scale.ToPx(800.0f)};
    m.maximized = maximized;
    const int buttonsWidth = scale.ToPx(138.0f); // Minimize, Maximize, Close: 3 x 46 DIP
    const int buttonsHeight = scale.ToPx(31.0f);
    m.captionButtons = {m.clientSize.cx - buttonsWidth, 0, m.clientSize.cx, buttonsHeight};
    return m;
}

class FakeMetricsProvider final : public te::ICaptionMetricsProvider
{
  public:
    explicit FakeMetricsProvider(bool maximized) : m_maximized(maximized) {}

    te::CaptionMetrics Query(HWND, UINT dpi) override
    {
        lastDpi = dpi;
        return FakeMetrics(dpi, m_maximized);
    }

    UINT lastDpi = 0;

  private:
    bool m_maximized;
};

bool Intersects(const RECT& a, const RECT& b)
{
    RECT overlap{};
    return IntersectRect(&overlap, &a, &b) != FALSE;
}

struct LayoutCase
{
    UINT dpi;
    bool maximized;
};

std::ostream& operator<<(std::ostream& os, const LayoutCase& c)
{
    return os << c.dpi << " DPI, " << (c.maximized ? "maximized" : "normal");
}

class CaptionLayout : public ::testing::TestWithParam<LayoutCase>
{
};

} // namespace

TEST_P(CaptionLayout, CaptionHeightIsCaptionPlusFramePlusPaddedBorder)
{
    const auto m = FakeMetrics(GetParam().dpi, GetParam().maximized);
    const auto layout = te::CaptionHitTester::ComputeLayout(m);
    EXPECT_EQ(layout.captionHeightPx, m.captionPx + m.framePx + m.paddedBorderPx);
    EXPECT_EQ(layout.captionButtons.left, m.captionButtons.left);
    EXPECT_EQ(layout.captionButtons.right, m.captionButtons.right);
}

TEST_P(CaptionLayout, PickerSitsLeftOfMinimizeWithMargin)
{
    const auto m = FakeMetrics(GetParam().dpi, GetParam().maximized);
    const te::DpiManager scale(m.dpi);
    const auto layout = te::CaptionHitTester::ComputeLayout(m);
    EXPECT_EQ(layout.picker.right,
              m.captionButtons.left - scale.ToPx(te::CaptionHitTester::kPickerMarginDip));
    EXPECT_EQ(layout.picker.right - layout.picker.left, scale.ToPx(te::CaptionHitTester::kPickerWidthDip));
    EXPECT_EQ(layout.picker.bottom, m.captionButtons.bottom); // aligned with the caption buttons
    EXPECT_FALSE(Intersects(layout.picker, m.captionButtons));
}

TEST_P(CaptionLayout, DragRegionExcludesPickerAndCaptionButtons)
{
    const auto m = FakeMetrics(GetParam().dpi, GetParam().maximized);
    const auto layout = te::CaptionHitTester::ComputeLayout(m);
    EXPECT_EQ(layout.dragRegion.left, 0);
    EXPECT_EQ(layout.dragRegion.bottom, layout.captionHeightPx);
    EXPECT_GT(layout.dragRegion.right, layout.dragRegion.left);
    EXPECT_FALSE(Intersects(layout.dragRegion, layout.picker));
    EXPECT_FALSE(Intersects(layout.dragRegion, m.captionButtons));
}

TEST_P(CaptionLayout, TopResizeBandOnlyWhenNotMaximized)
{
    const auto m = FakeMetrics(GetParam().dpi, GetParam().maximized);
    const auto layout = te::CaptionHitTester::ComputeLayout(m);
    if (m.maximized)
    {
        EXPECT_EQ(layout.resizeBandPx, 0);
        // The client area starts at the window's top edge, above the screen by the
        // frame thickness (DwmDefWindowProc needs this); visible content starts below.
        EXPECT_EQ(layout.contentTopPx, m.framePx + m.paddedBorderPx);
        EXPECT_GE(layout.picker.top, layout.contentTopPx);
        EXPECT_GE(layout.dragRegion.top, layout.contentTopPx);
    }
    else
    {
        EXPECT_EQ(layout.contentTopPx, 0);
        EXPECT_EQ(layout.resizeBandPx, m.framePx + m.paddedBorderPx);
        // The picker and the drag region start below the band (UI contract §2 invariant).
        EXPECT_GE(layout.picker.top, layout.resizeBandPx);
        EXPECT_GE(layout.dragRegion.top, layout.resizeBandPx);
    }
}

TEST_P(CaptionLayout, ComputeUsesInjectedMetricsForTheRequestedDpi)
{
    auto provider = std::make_unique<FakeMetricsProvider>(GetParam().maximized);
    FakeMetricsProvider* fake = provider.get();
    te::CaptionHitTester tester(std::move(provider));

    const auto viaCompute = tester.Compute(nullptr, GetParam().dpi);
    const auto direct =
        te::CaptionHitTester::ComputeLayout(FakeMetrics(GetParam().dpi, GetParam().maximized));
    EXPECT_EQ(fake->lastDpi, GetParam().dpi);
    EXPECT_TRUE(EqualRect(&viaCompute.picker, &direct.picker));
    EXPECT_TRUE(EqualRect(&viaCompute.dragRegion, &direct.dragRegion));
    EXPECT_EQ(viaCompute.captionHeightPx, direct.captionHeightPx);
    EXPECT_GT(viaCompute.captionHeightPx, 0);
}

TEST_P(CaptionLayout, ClassifiesPointsInHitTestOrder)
{
    const auto m = FakeMetrics(GetParam().dpi, GetParam().maximized);
    const auto layout = te::CaptionHitTester::ComputeLayout(m);
    const auto classify = [&](LONG x, LONG y) {
        return te::CaptionHitTester::ClassifyPoint(POINT{x, y}, m.clientSize, layout);
    };
    const LONG captionMid = (layout.resizeBandPx + layout.captionHeightPx) / 2;

    EXPECT_EQ(classify((layout.picker.left + layout.picker.right) / 2,
                       (layout.picker.top + layout.picker.bottom) / 2),
              HTCLIENT);                                                     // picker
    EXPECT_EQ(classify(layout.dragRegion.right / 2, captionMid), HTCAPTION); // drag region
    EXPECT_EQ(classify((layout.picker.right + m.captionButtons.left) / 2, captionMid), HTCAPTION); // gap
    EXPECT_EQ(classify(m.clientSize.cx / 2, layout.captionHeightPx + 10), HTCLIENT); // below caption

    if (m.maximized)
    {
        EXPECT_EQ(classify(layout.dragRegion.right / 2, 0), HTCAPTION); // no resize band
    }
    else
    {
        EXPECT_EQ(classify(m.clientSize.cx / 2, 0), HTTOP);
        EXPECT_EQ(classify(0, 0), HTTOPLEFT);
        EXPECT_EQ(classify(m.clientSize.cx - 1, 0), HTTOPRIGHT);
    }
}

TEST(CaptionLayoutFallback, AssumesStandardButtonsWhenDwmBoundsAreMissing)
{
    auto m = FakeMetrics(96, false);
    m.captionButtons = {};
    const auto layout = te::CaptionHitTester::ComputeLayout(m);
    EXPECT_EQ(layout.captionButtons.right, m.clientSize.cx);
    EXPECT_EQ(layout.captionButtons.left, m.clientSize.cx - 138);
    EXPECT_EQ(layout.picker.right, layout.captionButtons.left - 8);
    EXPECT_FALSE(Intersects(layout.picker, layout.captionButtons));
}

TEST(CaptionLayoutSystem, RealWindowGivesSaneLayout)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"caption", WS_OVERLAPPEDWINDOW, 0, 0, 900, 600, nullptr,
                                nullptr, nullptr, nullptr);
    ASSERT_NE(hwnd, nullptr);
    te::CaptionHitTester tester; // system metrics provider
    const UINT dpi = GetDpiForWindow(hwnd);
    const auto layout = tester.Compute(hwnd, dpi);
    RECT client{};
    GetClientRect(hwnd, &client);
    EXPECT_EQ(layout.captionHeightPx, GetSystemMetricsForDpi(SM_CYCAPTION, dpi) +
                                          GetSystemMetricsForDpi(SM_CYFRAME, dpi) +
                                          GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi));
    EXPECT_GT(layout.picker.right, layout.picker.left);
    EXPECT_LE(layout.picker.right, client.right);
    EXPECT_FALSE(Intersects(layout.picker, layout.captionButtons));
    DestroyWindow(hwnd);
}

INSTANTIATE_TEST_SUITE_P(AllDpis, CaptionLayout,
                         ::testing::Values(LayoutCase{96, false}, LayoutCase{96, true},
                                           LayoutCase{144, false}, LayoutCase{144, true},
                                           LayoutCase{192, false}, LayoutCase{192, true}),
                         [](const ::testing::TestParamInfo<LayoutCase>& info) {
                             return std::to_string(info.param.dpi) +
                                    (info.param.maximized ? "Maximized" : "Normal");
                         });
