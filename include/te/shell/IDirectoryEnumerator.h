#pragma once

// Asynchronous folder enumeration on an STA worker (research R-07, FR-018, FR-019).

#include <te/core/Types.h>
#include <te/shell/ShellTypes.h>

#include <windows.h>

namespace te
{

class IDirectoryEnumerator
{
  public:
    virtual ~IDirectoryEnumerator() = default;

    // Starts enumeration on the STA enumeration worker. Cancels any earlier
    // request. Results arrive as WM_TE_ENUM_BATCH / WM_TE_ENUM_DONE posted
    // to notifyHwnd.
    virtual void Start(HWND notifyHwnd, Generation gen, const ShellLocation& loc) = 0;
    virtual void CancelAll() = 0; // request_stop; non-blocking
    virtual void Shutdown() = 0;  // request_stop + join; called from WM_DESTROY
};

} // namespace te
