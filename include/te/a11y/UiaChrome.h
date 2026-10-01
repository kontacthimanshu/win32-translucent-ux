#pragma once

// UI Automation providers for the window chrome (T078; research R-09; UI contract §5):
// the ToolBar "Navigation" (Back, Forward, Up, Refresh buttons and the Edit "Address"),
// the Tree "Navigation pane" (one TreeItem per place and per listed subfolder, nested as
// the folder tree is; T090) and the StatusBar (a polite live region). Children of UiaRoot.

#include <te/a11y/UiaRoot.h>
#include <te/ui/AddressBar.h>
#include <te/ui/NavigationPane.h>
#include <te/ui/StatusBar.h>
#include <te/ui/Toolbar.h>

#include <d2d1.h>

#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace te
{

struct UiaChromeHost
{
    Toolbar* toolbar = nullptr;
    AddressBar* address = nullptr;
    NavigationPane* pane = nullptr;
    StatusBar* status = nullptr;

    std::wstring toolbarName = L"Navigation";   // IDS_A11Y_TOOLBAR
    std::wstring addressName = L"Address";      // IDS_A11Y_ADDRESS
    std::wstring paneName = L"Navigation pane"; // IDS_A11Y_NAV_PANE

    // Client DIPs -> screen pixels, and back.
    std::function<UiaRect(const D2D1_RECT_F&)> toScreen;
    std::function<D2D1_POINT_2F(double x, double y)> fromScreen;
    // The toolbar row (buttons and address) in client DIPs.
    std::function<D2D1_RECT_F()> toolbarBounds;
    // Actions: each should post, so nothing runs inside the UIA call.
    std::function<void(Toolbar::Button)> invokeButton;
    std::function<void(std::wstring text)> navigateToText;     // Address SetValue
    std::function<void(const ShellLocation&)> navigateToPlace; // a pane item
    // A tree item's Expand (true) or Collapse (false), by node id (T090).
    std::function<void(std::uint64_t nodeId, bool expand)> expandPlace;
    std::function<bool()> paneHasFocus;
    std::function<void()> focusPane;
};

// The three top-level chrome providers. `host` must stay valid while the root is connected.
HRESULT MakeUiaToolbar(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                       Microsoft::WRL::ComPtr<IRawElementProviderFragment>* result);
HRESULT MakeUiaNavigationPane(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                              Microsoft::WRL::ComPtr<IRawElementProviderFragment>* result);
HRESULT MakeUiaStatusBar(UiaRoot* root, std::shared_ptr<const UiaChromeHost> host,
                         Microsoft::WRL::ComPtr<IRawElementProviderFragment>* result);

} // namespace te
