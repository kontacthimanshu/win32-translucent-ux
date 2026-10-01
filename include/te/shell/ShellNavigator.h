#pragma once

// IShellNavigator (T057, T069; research R-08, R-11, UI contract §4, §6): address parsing,
// parent, the initial location, opening items and the Shell's context menus.

#include <te/shell/ContextMenu.h>
#include <te/shell/IShellNavigator.h>

#include <functional>
#include <memory>
#include <string>

namespace te
{

class ShellNavigator final : public IShellNavigator
{
  public:
    // Text for the "no associated app" task dialog (IDS_ERR_CANT_OPEN_TITLE,
    // IDS_ERR_CANT_OPEN_BODY_FMT, IDS_CMD_OPEN_WITH). English defaults match the
    // STRINGTABLE.
    struct Strings
    {
        std::wstring cantOpenTitle = L"Windows can't open this file";
        std::wstring cantOpenBodyFmt = L"No app is associated with '%1'. Choose an app to open it.";
        std::wstring openWith = L"Open with…";
    };

    // Asks whether to choose an app for `itemName` (true = "Open with..."). The default
    // shows TaskDialogIndirect; tests inject their own so no modal UI appears.
    using AskOpenWith = std::function<bool(HWND owner, const std::wstring& itemName)>;

    explicit ShellNavigator(Strings strings = {}, AskOpenWith askOpenWith = nullptr);

    // Trims spaces and surrounding quotes, expands %VARIABLES%, then SHParseDisplayName.
    // On failure `out` is unchanged and the Shell's HRESULT is returned (spec US3-6).
    HRESULT Parse(std::wstring_view text, ShellLocation* out) override;
    std::optional<ShellLocation> Parent(const ShellLocation& location) override;
    // The command-line folder if it parses to a folder, else This PC, else the user
    // profile, else the Desktop (R-11).
    ShellLocation InitialLocation(std::optional<std::wstring_view> cmdLinePath) override;
    // S_FALSE for folders (the caller navigates instead). Otherwise ShellExecuteExW with
    // the item's ID list; with no associated app, offers "Open with..." (UI §6).
    HRESULT Open(HWND owner, const ShellLocation& folder, const ShellItemInfo& item) override;

    // The Shell's menu for `items` in `folder`, or the folder background when `items` is
    // empty; extended verbs with Shift. Modal until the menu closes. Returns S_FALSE if
    // dismissed, kContextMenuRename for Rename, otherwise the command's result.
    HRESULT ShowContextMenu(HWND owner, POINT screenPt, const ShellLocation& folder,
                            std::span<const ShellItemInfo* const> items, bool extended) override;
    // Forwards menu messages to the open menu (submenus such as "Send to", owner-drawn
    // items). False when no menu is open.
    bool HandleMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result) override;

    // Replaces TrackPopupMenuEx, so tests can pick a command without a modal menu.
    void SetMenuTracker(ContextMenu::Track track)
    {
        m_track = std::move(track);
    }

  private:
    bool DefaultAskOpenWith(HWND owner, const std::wstring& itemName) const;

    Strings m_strings;
    AskOpenWith m_askOpenWith;
    ContextMenu::Track m_track;
    std::unique_ptr<ContextMenu> m_openMenu; // while ShowContextMenu runs
};

} // namespace te
