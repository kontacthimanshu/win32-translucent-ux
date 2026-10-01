#pragma once

// Debug-only UI-thread watchdog (research R-14). Wrap each DispatchMessageW in a
// DispatchWatchdog: if the handler takes longer than the threshold it logs, e.g.
//   [te-watchdog] msg=0x000F took 63 ms
// with OutputDebugStringW (visible in the debugger or DebugView). This is a
// diagnostic aid, not a published latency guarantee.
//
// In Release builds DispatchWatchdog is an empty type and compiles away.

#include <windows.h>

#include <string>

namespace te
{

inline constexpr long long kWatchdogThresholdMs = 50;

// Messages whose handling normally runs a nested modal loop inside
// DispatchMessageW (moving/sizing the window, system and context menus); their
// duration is the user's interaction time, so they are not reported.
[[nodiscard]] bool IsModalLoopMessage(UINT msg) noexcept;

// The log line for a slow message, e.g. L"[te-watchdog] msg=0x000F took 63 ms\n".
[[nodiscard]] std::wstring FormatWatchdogMessage(UINT msg, long long elapsedMs);

#ifdef _DEBUG

class DispatchWatchdog
{
  public:
    explicit DispatchWatchdog(UINT msg) noexcept;
    ~DispatchWatchdog();

    DispatchWatchdog(const DispatchWatchdog&) = delete;
    DispatchWatchdog& operator=(const DispatchWatchdog&) = delete;

    // Replaces OutputDebugStringW, for tests. nullptr restores the default.
    using Sink = void (*)(const wchar_t* line);
    static void SetSinkForTesting(Sink sink) noexcept;

  private:
    UINT m_msg;
    LARGE_INTEGER m_start{};
};

#else

class DispatchWatchdog
{
  public:
    explicit DispatchWatchdog(UINT) noexcept {}
};

#endif

} // namespace te
