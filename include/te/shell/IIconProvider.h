#pragma once

// Asynchronous icon extraction on an STA worker (research R-06, R-07).

#include <te/core/Types.h>
#include <te/shell/ShellTypes.h>

#include <windows.h>

#include <cstddef>

namespace te
{

class IIconProvider
{
  public:
    virtual ~IIconProvider() = default;

    // Thread-safe. Results arrive as WM_TE_ICON_READY posted to notifyHwnd, tagged with
    // gen and itemKey (the row's FileItem::key) so the UI can find the row, or drop the
    // result if the list has moved on. Served LIFO (T087): the newest request first, so
    // the rows on screen, requested last, are served before the look-ahead.
    // An icon whose system image-list index was already sent at this size comes back
    // without a bitmap (IconReady::shared); `forceExtract` sends the bitmap anyway (the UI
    // lost its copy).
    virtual void Request(HWND notifyHwnd, Generation gen, std::size_t itemKey, const ShellLocation& folder,
                         const ShellItemInfo& item, int sizePx, bool forceExtract = false) = 0;
    virtual void CancelOlderThan(Generation gen) = 0;
    virtual void Shutdown() = 0;
};

} // namespace te
