#include <te/app/MainLayout.h>

#include <algorithm>

namespace te
{

MainLayout MainLayout::Compute(SIZE clientSize, UINT dpi, int captionHeightPx, float textScale)
{
    textScale = std::max(1.0f, textScale);
    const float scale = static_cast<float>(dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI) /
                        static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    const float width = std::max(0.0f, static_cast<float>(clientSize.cx) / scale);
    const float height = std::max(0.0f, static_cast<float>(clientSize.cy) / scale);

    // Top to bottom: caption, toolbar, content, status bar. The status bar keeps
    // its height and the content area absorbs any shortfall.
    const float captionBottom = std::clamp(static_cast<float>(captionHeightPx) / scale, 0.0f, height);
    const float statusTop = std::max(captionBottom, height - kStatusBarHeightDip * textScale);
    const float toolbarBottom = std::min(captionBottom + kToolbarHeightDip * textScale, statusTop);

    const float paneWidth = std::min(kNavigationPaneWidthDip, width * kNavigationPaneMaxShare);

    MainLayout layout;
    layout.caption = D2D1::RectF(0.0f, 0.0f, width, captionBottom);
    layout.toolbar = D2D1::RectF(0.0f, captionBottom, width, toolbarBottom);
    layout.navigationPane = D2D1::RectF(0.0f, toolbarBottom, paneWidth, statusTop);
    layout.fileList = D2D1::RectF(paneWidth, toolbarBottom, width, statusTop);
    layout.statusBar = D2D1::RectF(0.0f, statusTop, width, height);
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
