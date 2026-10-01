#pragma once

// The address bar: translucent display mode plus a child EDIT while editing
// (research R-02, T061; UI contract §4, §6).

#include <te/appearance/AppearanceSettings.h>
#include <te/shell/ShellTypes.h>

#include <windows.h>

#include <ole2.h>

#include <UIAutomationCore.h>
#include <d2d1_1.h>

#include <functional>
#include <string_view>

namespace te
{

class IAddressBar
{
  public:
    virtual ~IAddressBar() = default;

    virtual void SetLocation(const ShellLocation& location) = 0;
    virtual void BeginEdit() = 0;  // Ctrl+L / Alt+D / F4 / click
    virtual void CancelEdit() = 0; // Esc: restore the path
    virtual bool IsEditing() const = 0;

    // Called with the typed text when the user presses Enter.
    virtual void SetNavigateCallback(std::function<void(std::wstring_view text)>) = 0;
    // Inline error under the field (UI contract §6); empty text clears it.
    virtual void ShowError(std::wstring_view message) = 0;

    virtual void SetBounds(const D2D1_RECT_F& bounds) = 0;
    virtual void Render(ID2D1DeviceContext*, const EffectiveAppearance&) = 0;
    virtual IRawElementProviderFragment* Automation() = 0;
};

} // namespace te
