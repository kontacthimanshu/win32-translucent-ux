#pragma once

// File operations through IFileOperation on an STA worker (research R-08, FR-012–FR-014).

#include <te/shell/ShellTypes.h>

#include <windows.h>

#include <objidl.h>

#include <wil/com.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace te
{

enum class FileOpKind
{
    Copy,
    Move,
    Rename,
    Recycle,
    DeletePermanent,
};

// Final state of a FileOperationRequest (data-model: FileOperationRequest states).
enum class FileOpFinalState
{
    Succeeded,
    PartiallySucceeded,
    Cancelled,
    Failed,
};

struct FileOpRequest
{
    std::uint64_t id = 0;
    FileOpKind kind = FileOpKind::Copy;
    std::vector<ShellLocation> sources; // or dataObject for paste
    wil::com_ptr<IDataObject> dataObject;
    std::optional<ShellLocation> destination;
    std::optional<std::wstring> newName;
};

class IFileOperationService
{
  public:
    virtual ~IFileOperationService() = default;

    // Runs IFileOperation on the STA file-op worker. Owner window = mainHwnd.
    // Flags are fixed by research R-08 and cannot be overridden by the caller.
    // Progress and results arrive as WM_TE_FILEOP_ITEM / WM_TE_FILEOP_DONE.
    virtual void Submit(HWND notifyHwnd, FileOpRequest request) = 0;
    virtual void Shutdown() = 0; // waits for the running operation; Shell UI stays modal to the owner
};

} // namespace te
