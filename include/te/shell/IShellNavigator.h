#pragma once

// Location parsing, opening items and Shell context menus (research R-08, R-11).

#include <te/shell/ShellTypes.h>

#include <windows.h>

#include <optional>
#include <span>
#include <string_view>

namespace te
{

// ShowContextMenu's result when the user chose Rename: the verb is not invoked, and the
// caller starts inline rename instead (research R-08). A success code, distinct from
// S_FALSE (menu dismissed).
inline constexpr HRESULT kContextMenuRename = MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_ITF, 0x0201);

class IShellNavigator
{
  public:
    virtual ~IShellNavigator() = default;

    virtual HRESULT Parse(std::wstring_view text, ShellLocation* out) = 0; // SHParseDisplayName
    virtual std::optional<ShellLocation> Parent(const ShellLocation&) = 0;
    virtual ShellLocation InitialLocation(std::optional<std::wstring_view> cmdLinePath) = 0;
    // ShellExecuteExW; folders navigate instead of opening.
    virtual HRESULT Open(HWND owner, const ShellLocation& folder, const ShellItemInfo& item) = 0;
    virtual HRESULT ShowContextMenu(HWND owner, POINT screenPt, const ShellLocation& folder,
                                    std::span<const ShellItemInfo* const> items, bool extended) = 0;

    // Must be forwarded from the window procedure while a menu is open
    // (IContextMenu2/3).
    virtual bool HandleMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result) = 0;
};

} // namespace te
