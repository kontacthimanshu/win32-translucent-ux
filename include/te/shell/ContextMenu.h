#pragma once

// The Shell's own context menu for items or a folder background (T069; research R-08;
// UI contract §4). The menu, its verbs and their UI all come from the Shell and its
// extensions; the app only shows it and routes "rename" to inline rename.

#include <te/shell/ShellTypes.h>

#include <windows.h>

#include <shobjidl.h>

#include <wil/com.h>

#include <functional>
#include <optional>
#include <span>
#include <string>

namespace te
{

class ContextMenu
{
  public:
    // QueryContextMenu command range (idCmdFirst..idCmdLast).
    static constexpr UINT kFirstCommand = 1;
    static constexpr UINT kLastCommand = 0x7FFF;

    // Shows the populated menu and returns the chosen command ID, or 0 if dismissed.
    // The default is TrackPopupMenuEx(TPM_RETURNCMD | TPM_RIGHTBUTTON); tests choose a
    // command without a modal menu.
    using Track = std::function<UINT(HMENU menu, HWND owner, POINT screenPt)>;

    ContextMenu() = default;
    ContextMenu(const ContextMenu&) = delete;
    ContextMenu& operator=(const ContextMenu&) = delete;

    // The menu for `items` (child items of `folder`), or for the folder's background when
    // `items` is empty.
    HRESULT Load(HWND owner, const ShellLocation& folder, std::span<const ShellItemInfo* const> items);

    // Fills `menu`: QueryContextMenu with CMF_NORMAL | CMF_CANRENAME, plus
    // CMF_EXTENDEDVERBS when `extended` (Shift held).
    HRESULT Populate(HMENU menu, bool extended);

    // The language-independent verb of a command ID ("rename", "delete", "properties"),
    // if the handler reports one.
    [[nodiscard]] std::optional<std::wstring> VerbOf(UINT commandId) const;

    // Runs a command with CMINVOKECOMMANDINFOEX (Unicode, invoke point, and the Shift and
    // Ctrl state).
    HRESULT Invoke(HWND owner, UINT commandId, POINT screenPt, bool shift, bool ctrl);

    // Populate, track, then invoke. Returns S_FALSE if the menu was dismissed,
    // kContextMenuRename if the user chose Rename (not invoked: the caller starts inline
    // rename), otherwise the result of InvokeCommand.
    HRESULT Show(HWND owner, POINT screenPt, bool extended, const Track& track = {});

    // While the menu is open, the owner forwards WM_INITMENUPOPUP, WM_DRAWITEM,
    // WM_MEASUREITEM and WM_MENUCHAR here, for submenus and owner-drawn items
    // (IContextMenu3 / IContextMenu2). Returns true if handled.
    bool HandleMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result);

    [[nodiscard]] bool IsLoaded() const noexcept
    {
        return m_menu != nullptr;
    }

  private:
    wil::com_ptr<IContextMenu> m_menu;
    wil::com_ptr<IContextMenu2> m_menu2;
    wil::com_ptr<IContextMenu3> m_menu3;
    bool m_tracking = false;
};

} // namespace te
