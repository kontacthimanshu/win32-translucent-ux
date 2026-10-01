#pragma once

// Status-bar and dialog text for file operations (T072; UI contract §6; spec US4-6).
// Pure formatting, so it is unit-tested without a window.

#include <te/core/Types.h>
#include <te/shell/IFileOperationService.h>

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace te
{

struct FileOpText
{
    // STRINGTABLE IDs (resources/resource.h); 0 keeps the English default.
    struct StringIds
    {
        UINT copyingFmt = 0;   // IDS_OP_COPYING_FMT
        UINT movingFmt = 0;    // IDS_OP_MOVING_FMT
        UINT deletingFmt = 0;  // IDS_OP_DELETING_FMT
        UINT renaming = 0;     // IDS_OP_RENAMING
        UINT copiedFmt = 0;    // IDS_OP_COPIED_FMT
        UINT movedFmt = 0;     // IDS_OP_MOVED_FMT
        UINT deletedFmt = 0;   // IDS_OP_DELETED_FMT
        UINT renamed = 0;      // IDS_OP_RENAMED
        UINT partialFmt = 0;   // IDS_OP_PARTIAL_FMT
        UINT cancelledFmt = 0; // IDS_OP_CANCELLED_FMT
        UINT failedTitle = 0;  // IDS_OP_FAILED_TITLE
        UINT badName = 0;      // IDS_ERR_INVALID_NAME
    };

    // FormatMessage patterns (%1!u!, %2!u!).
    std::wstring copyingFmt = L"Copying %1!u! items…";
    std::wstring movingFmt = L"Moving %1!u! items…";
    std::wstring deletingFmt = L"Deleting %1!u! items…";
    std::wstring renaming = L"Renaming…";
    std::wstring copiedFmt = L"%1!u! items copied";
    std::wstring movedFmt = L"%1!u! items moved";
    std::wstring deletedFmt = L"%1!u! items deleted";
    std::wstring renamed = L"Item renamed";
    std::wstring partialFmt = L"%1!u! of %2!u! items completed";
    std::wstring cancelledFmt = L"Operation cancelled — %1!u! items completed before cancellation";
    std::wstring failedTitle = L"Some items couldn't be processed";
    std::wstring badName =
        L"A file name can't contain any of the following characters:\r\n\\ / : * ? \" < > |";

    // Strings from the module's STRINGTABLE; any ID that is 0 or missing keeps the default.
    static FileOpText Load(HINSTANCE instance, const StringIds& ids);

    // While the operation runs: "Copying 3 items…" (persistent in the status bar).
    [[nodiscard]] std::wstring Progress(FileOpKind kind, std::size_t total) const;

    // When it ends (UI §6): "3 items copied", "2 of 3 items completed" (anything short of
    // all), or "Operation cancelled — N items completed before cancellation".
    [[nodiscard]] std::wstring Result(FileOpKind kind, FileOpFinalState state, std::size_t completed,
                                      std::size_t total) const;

    // The failure dialog body: one line per failed item ("name: system message"), at most
    // `maxLines`, then "…" if there are more.
    [[nodiscard]] static std::wstring FailureList(const std::vector<Status>& errors, std::size_t maxLines);
};

} // namespace te
