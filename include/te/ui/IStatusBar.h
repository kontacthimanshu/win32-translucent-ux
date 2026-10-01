#pragma once

// The status bar: item counts, applied appearance and messages (T038; UI contract §1,
// §3, §6).

#include <te/appearance/AppearanceSettings.h>

#include <windows.h>

#include <ole2.h>

#include <UIAutomationCore.h>
#include <d2d1_1.h>

#include <cstddef>
#include <string>

namespace te
{

class IStatusBar
{
  public:
    virtual ~IStatusBar() = default;

    virtual void SetItemCounts(std::size_t items, std::size_t selected) = 0;
    // Right-hand text: applied mode, surface opacity, tint strength and any fallback.
    virtual void SetAppearance(const AppearanceSettings& requested, const EffectiveAppearance& effective) = 0;
    // A message that disappears after durationMs.
    virtual void SetTransientMessage(std::wstring text, UINT durationMs) = 0;
    // A message that stays until replaced or cleared, e.g. "Copying 3 items…" (T072).
    virtual void SetOperationMessage(std::wstring text) = 0;
    virtual void ClearOperationMessage() = 0;

    virtual void SetBounds(const D2D1_RECT_F& bounds) = 0;
    virtual void Render(ID2D1DeviceContext*, const EffectiveAppearance&) = 0;
    virtual IRawElementProviderFragment* Automation() = 0;
};

} // namespace te
