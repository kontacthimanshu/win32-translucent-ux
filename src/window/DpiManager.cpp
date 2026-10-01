#include <te/window/DpiManager.h>

#include <cmath>

namespace te
{

DpiManager::DpiManager(UINT dpi) noexcept : m_dpi(dpi != 0 ? dpi : USER_DEFAULT_SCREEN_DPI) {}

void DpiManager::Attach(HWND hwnd) noexcept
{
    if (const UINT dpi = GetDpiForWindow(hwnd); dpi != 0)
    {
        m_dpi = dpi;
    }
}

UINT DpiManager::Dpi() const
{
    return m_dpi;
}

float DpiManager::Scale() const
{
    return static_cast<float>(m_dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
}

int DpiManager::ToPx(float dip) const
{
    // Same result as MulDiv(dip, dpi, 96) for whole DIPs, but also correct for
    // fractional DIPs such as 0.5.
    return static_cast<int>(std::lround(static_cast<double>(dip) * m_dpi / USER_DEFAULT_SCREEN_DPI));
}

SIZE DpiManager::ScaledWindowSize(SIZE clientPx, UINT oldDpi, UINT newDpi, SIZE frameAtNewDpi) noexcept
{
    if (oldDpi == 0)
    {
        oldDpi = USER_DEFAULT_SCREEN_DPI;
    }
    if (newDpi == 0)
    {
        newDpi = oldDpi;
    }
    const auto scale = [&](LONG px) {
        return static_cast<LONG>(std::lround(static_cast<double>(px) * newDpi / oldDpi));
    };
    return SIZE{scale(clientPx.cx) + frameAtNewDpi.cx, scale(clientPx.cy) + frameAtNewDpi.cy};
}

void DpiManager::OnDpiChanged(HWND hwnd, UINT newDpi, const RECT& suggested)
{
    if (newDpi != 0)
    {
        m_dpi = newDpi;
    }
    SetWindowPos(hwnd, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
                 suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace te
