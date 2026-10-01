#pragma once

// IFileOperation on a dedicated STA worker (T068; research R-08, R-07; FR-012–FR-014;
// constitution Principle IX). Each request runs CopyItems / MoveItems / RenameItem /
// DeleteItems with the fixed flags of FlagsFor, an IFileOperationProgressSink posts one
// WM_TE_FILEOP_ITEM per item, and WM_TE_FILEOP_DONE carries the final state.

#include <te/core/StaWorker.h>
#include <te/shell/IFileOperationService.h>

#include <shobjidl.h>

#include <functional>
#include <memory>
#include <string_view>

namespace te
{

class FileOperationService final : public IFileOperationService
{
  public:
    // Creates the IFileOperation for one request, on the worker thread. The default
    // (empty) uses CoCreateInstance(CLSID_FileOperation). Tests pass a factory that wraps
    // the real object to observe the calls; the service's flags are the same either way
    // (research R-14, "test-only IFileOperation factory").
    using Factory = std::function<HRESULT(IFileOperation** operation)>;

    explicit FileOperationService(Factory factory = {});
    // Calls Shutdown.
    ~FileOperationService() override;

    FileOperationService(const FileOperationService&) = delete;
    FileOperationService& operator=(const FileOperationService&) = delete;

    void Submit(HWND notifyHwnd, FileOpRequest request) override;
    void Shutdown() override;

    // The operation flags for a kind (research R-08). Never FOF_NOCONFIRMATION,
    // FOF_NOERRORUI or FOF_RENAMEONCOLLISION; always FOFX_ADDUNDORECORD; FOF_ALLOWUNDO
    // for Recycle and never for DeletePermanent. Callers cannot change them.
    [[nodiscard]] static DWORD FlagsFor(FileOpKind kind) noexcept;

    // A new name for Rename: not empty after trimming spaces, and none of \ / : * ? " < > |
    // (data-model "FileOperationRequest"). Checked before a request is submitted.
    [[nodiscard]] static bool ValidateNewName(std::wstring_view name) noexcept;

  private:
    Factory m_factory;
    std::unique_ptr<StaWorker> m_worker;
};

} // namespace te
