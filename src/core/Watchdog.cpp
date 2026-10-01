#include <te/core/Watchdog.h>

#include <cwchar>

namespace te
{

bool IsModalLoopMessage(UINT msg) noexcept
{
    switch (msg)
    {
    case WM_NCLBUTTONDOWN: // move/size loop on the caption or borders
    case WM_SYSCOMMAND:    // SC_MOVE, SC_SIZE, SC_KEYMENU (system menu)
    case WM_NCRBUTTONUP:   // system menu from a right-click on the caption
    case WM_CONTEXTMENU:   // TrackPopupMenuEx for Shell context menus
        return true;
    default:
        return false;
    }
}

std::wstring FormatWatchdogMessage(UINT msg, long long elapsedMs)
{
    wchar_t line[80]{};
    swprintf_s(line, L"[te-watchdog] msg=0x%04X took %lld ms\n", msg, elapsedMs);
    return line;
}

#ifdef _DEBUG

namespace
{

DispatchWatchdog::Sink g_sink = nullptr;

long long ElapsedMs(const LARGE_INTEGER& start)
{
    LARGE_INTEGER now{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return (now.QuadPart - start.QuadPart) * 1000 / frequency.QuadPart;
}

} // namespace

DispatchWatchdog::DispatchWatchdog(UINT msg) noexcept : m_msg(msg)
{
    QueryPerformanceCounter(&m_start);
}

DispatchWatchdog::~DispatchWatchdog()
{
    const long long elapsed = ElapsedMs(m_start);
    if (elapsed <= kWatchdogThresholdMs || IsModalLoopMessage(m_msg))
    {
        return;
    }
    const std::wstring line = FormatWatchdogMessage(m_msg, elapsed);
    if (g_sink)
    {
        g_sink(line.c_str());
    }
    else
    {
        OutputDebugStringW(line.c_str());
    }
}

void DispatchWatchdog::SetSinkForTesting(Sink sink) noexcept
{
    g_sink = sink;
}

#endif

} // namespace te
