#include <te/app/MainLayout.h>

#include <algorithm>

namespace te
{

MainLayout MainLayout::Compute(SIZE clientSize, UINT dpi, int captionHeightPx, float textScale, SlabPx slabPx)
{
    textScale = std::max(1.0f, textScale);
    const float scale = static_cast<float>(dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI) /
                        static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    const float width = std::max(0.0f, static_cast<float>(clientSize.cx) / scale);
    const float height = std::max(0.0f, static_cast<float>(clientSize.cy) / scale);

    // Top to bottom: caption, toolbar, content, status bar. The status bar keeps
    // its height and the content area absorbs any shortfall.
    const float captionBottom = std::clamp(static_cast<float>(captionHeightPx) / scale, 0.0f, height);
    // The slab's left, bottom and right faces take their thickness from the panes below the
    // caption.
    const auto toDip = [&](int px, float limit) {
        return std::clamp(static_cast<float>(std::max(0, px)) / scale, 0.0f, std::max(0.0f, limit));
    };
    const float slabLeft = toDip(slabPx.left, width);
    const float slabRight = toDip(slabPx.right, width - slabLeft);
    const float right = width - slabRight;
    const float slabBottom = toDip(slabPx.bottom, height - captionBottom);
    const float bottom = height - slabBottom;
    const float statusTop = std::max(captionBottom, bottom - kStatusBarHeightDip * textScale);
    const float toolbarBottom = std::min(captionBottom + kToolbarHeightDip * textScale, statusTop);

    const float paneLeft = slabLeft;
    const float paneRight =
        paneLeft + std::min(kNavigationPaneWidthDip, (right - slabLeft) * kNavigationPaneMaxShare);

    MainLayout layout;
    layout.caption = D2D1::RectF(0.0f, 0.0f, width, captionBottom);
    layout.toolbar = D2D1::RectF(paneLeft, captionBottom, right, toolbarBottom);
    layout.navigationPane = D2D1::RectF(paneLeft, toolbarBottom, paneRight, statusTop);
    layout.fileList = D2D1::RectF(paneRight, toolbarBottom, std::max(paneRight, right), statusTop);
    layout.statusBar = D2D1::RectF(paneLeft, statusTop, right, std::max(statusTop, bottom));
    layout.slab = {toDip(slabPx.top, captionBottom), slabLeft, slabBottom, slabRight};
    if (slabLeft > 0.0f)
    {
        layout.slabLeft = D2D1::RectF(0.0f, captionBottom, slabLeft, height);
    }
    if (slabRight > 0.0f)
    {
        layout.slabRight = D2D1::RectF(right, captionBottom, width, height);
    }
    if (slabBottom > 0.0f)
    {
        layout.slabBottom = D2D1::RectF(slabLeft, std::max(captionBottom, bottom), right, height);
    }
    return layout;
}

D2D1_RECT_F MainLayout::ToDip(const RECT& px, UINT dpi)
{
    const float scale = static_cast<float>(dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI) /
                        static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    return D2D1::RectF(static_cast<float>(px.left) / scale, static_cast<float>(px.top) / scale,
                       static_cast<float>(px.right) / scale, static_cast<float>(px.bottom) / scale);
}

} // namespace te
