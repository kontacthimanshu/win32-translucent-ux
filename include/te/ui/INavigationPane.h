#pragma once

// The navigation pane of quick locations and drives (T063).

#include <te/appearance/AppearanceSettings.h>
#include <te/shell/ShellTypes.h>

#include <windows.h>

#include <ole2.h>

#include <UIAutomationCore.h>
#include <d2d1_1.h>

#include <functional>

namespace te
{

class INavigationPane
{
  public:
    virtual ~INavigationPane() = default;

    // Rebuilds the list of known folders and drives (also on WM_DEVICECHANGE).
    virtual void Refresh() = 0;
    // Highlights the entry matching the current location, if any.
    virtual void SetCurrent(const ShellLocation& location) = 0;
    virtual void SetNavigateCallback(std::function<void(const ShellLocation&)>) = 0;

    virtual void SetBounds(const D2D1_RECT_F& bounds) = 0;
    virtual void Render(ID2D1DeviceContext*, const EffectiveAppearance&) = 0;
    virtual IRawElementProviderFragment* Automation() = 0;
};

} // namespace te
