#include <te/shell/FileOperationService.h>

#include <te/core/Messages.h>
#include <te/core/Result.h>

#include <shellapi.h>
#include <sherrors.h>
#include <shlobj.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <wrl/implements.h>

#include <memory>
#include <utility>
#include <vector>

namespace te
{

namespace
{

// The failure dialog lists at most this many items (FR-013); a copy of a large tree that
// fails everywhere would otherwise build an unbounded list.
constexpr std::size_t kMaxErrors = 200;

bool IsCancel(HRESULT hr) noexcept
{
    return hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) || hr == COPYENGINE_E_USER_CANCELLED;
}

std::wstring_view Trim(std::wstring_view text) noexcept
{
    while (!text.empty() && (text.front() == L' ' || text.front() == L'\t'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == L' ' || text.back() == L'\t'))
    {
        text.remove_suffix(1);
    }
    return text;
}

std::wstring NameOf(IShellItem* item)
{
    wil::unique_cotaskmem_string name;
    if (item && SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name)
    {
        return name.get();
    }
    return {};
}

// A paste's IDataObject belongs to the UI thread's apartment: it is marshalled to the
// worker, and the marshal data is released if the request never runs.
class MarshaledDataObject
{
  public:
    explicit MarshaledDataObject(IDataObject* object)
    {
        LOG_IF_FAILED(CoMarshalInterThreadInterfaceInStream(__uuidof(IDataObject), object, &m_stream));
    }
    ~MarshaledDataObject()
    {
        if (m_stream)
        {
            LOG_IF_FAILED(CoReleaseMarshalData(m_stream.get()));
        }
    }
    MarshaledDataObject(const MarshaledDataObject&) = delete;
    MarshaledDataObject& operator=(const MarshaledDataObject&) = delete;

    // On the worker thread; once.
    HRESULT Take(IDataObject** object)
    {
        RETURN_HR_IF(E_UNEXPECTED, !m_stream);
        // CoGetInterfaceAndReleaseStream releases the stream and its marshal data.
        return CoGetInterfaceAndReleaseStream(m_stream.detach(), IID_PPV_ARGS(object));
    }

  private:
    wil::com_ptr<IStream> m_stream;
};

struct Job
{
    FileOpRequest request;
    std::unique_ptr<MarshaledDataObject> dataObject;
};

// Collects per-item results (IFileOperationProgressSink::PostXxxItem) and posts one
// WM_TE_FILEOP_ITEM for each item the request named. Nested items (the contents of a
// copied folder) are counted but not posted, so a large tree cannot flood the UI queue.
class ProgressSink final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IFileOperationProgressSink>
{
  public:
    ProgressSink(HWND notify, std::uint64_t opId, FileOpKind kind,
                 std::vector<wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>> topLevel)
        : m_notify(notify), m_opId(opId), m_kind(kind), m_topLevel(std::move(topLevel))
    {
    }

    // IFileOperationProgressSink
    IFACEMETHODIMP StartOperations() override
    {
        return S_OK;
    }
    IFACEMETHODIMP FinishOperations(HRESULT) override
    {
        return S_OK;
    }
    IFACEMETHODIMP PreRenameItem(DWORD, IShellItem*, LPCWSTR) override
    {
        return S_OK;
    }
    IFACEMETHODIMP PostRenameItem(DWORD, IShellItem* item, LPCWSTR newName, HRESULT hr, IShellItem*) override
    {
        Record(item, hr, newName);
        return S_OK;
    }
    IFACEMETHODIMP PreMoveItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override
    {
        return S_OK;
    }
    IFACEMETHODIMP PostMoveItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT hr,
                                IShellItem*) override
    {
        Record(item, hr, nullptr);
        return S_OK;
    }
    IFACEMETHODIMP PreCopyItem(DWORD, IShellItem*, IShellItem*, LPCWSTR) override
    {
        return S_OK;
    }
    IFACEMETHODIMP PostCopyItem(DWORD, IShellItem* item, IShellItem*, LPCWSTR, HRESULT hr,
                                IShellItem*) override
    {
        Record(item, hr, nullptr);
        return S_OK;
    }
    IFACEMETHODIMP PreDeleteItem(DWORD, IShellItem*) override
    {
        return S_OK;
    }
    IFACEMETHODIMP PostDeleteItem(DWORD, IShellItem* item, HRESULT hr, IShellItem*) override
    {
        Record(item, hr, nullptr);
        return S_OK;
    }
    IFACEMETHODIMP PreNewItem(DWORD, IShellItem*, LPCWSTR) override
    {
        return S_OK;
    }
    IFACEMETHODIMP PostNewItem(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) override
    {
        return S_OK;
    }
    IFACEMETHODIMP UpdateProgress(UINT, UINT) override
    {
        return S_OK;
    }
    IFACEMETHODIMP ResetTimer() override
    {
        return S_OK;
    }
    IFACEMETHODIMP PauseTimer() override
    {
        return S_OK;
    }
    IFACEMETHODIMP ResumeTimer() override
    {
        return S_OK;
    }

    [[nodiscard]] std::size_t Succeeded() const noexcept
    {
        return m_succeeded;
    }
    [[nodiscard]] std::size_t FailedCount() const noexcept
    {
        return m_failed;
    }
    [[nodiscard]] std::vector<Status>& Errors() noexcept
    {
        return m_errors;
    }

  private:
    void Record(IShellItem* item, HRESULT hr, LPCWSTR newName)
    {
        // A conflict is being resolved (the Shell's "Replace or Skip" dialog): the item is
        // reported again with its final result, so this one is neither done nor posted.
        // Found in T073 (V-4b), where counting it reported skipped items as moved.
        if (hr == COPYENGINE_S_PENDING)
        {
            return;
        }
        // A rename reports COPYENGINE_S_DONT_PROCESS_CHILDREN on success ("no recursion"),
        // which says nothing more than S_OK to the UI.
        if (hr == COPYENGINE_S_DONT_PROCESS_CHILDREN)
        {
            hr = S_OK;
        }
        // Skipped by the user in the Shell's conflict dialog: neither done nor failed.
        const bool skipped = hr == COPYENGINE_S_USER_IGNORED;
        if (FAILED(hr) && !IsCancel(hr))
        {
            ++m_failed;
            if (m_errors.size() < kMaxErrors)
            {
                m_errors.push_back(MakeStatus(hr, NameOf(item)));
            }
        }
        else if (SUCCEEDED(hr) && !skipped)
        {
            ++m_succeeded;
        }

        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        const HRESULT idHr =
            SHGetIDListFromObject(item, wil::out_param(pidl)); // separate statement (out_param)
        if (FAILED(idHr) || !pidl || !IsTopLevel(pidl.get()))
        {
            return;
        }
        auto message = std::make_unique<FileOpItem>();
        message->opId = m_opId;
        message->kind = m_kind;
        message->hr = hr;
        LOG_IF_FAILED(ShellLocation::FromIdList(pidl.get(), &message->item));
        if (m_kind == FileOpKind::Rename && SUCCEEDED(hr) && !skipped && newName)
        {
            message->newName = newName;
        }
        static_cast<void>(PostOwned(m_notify, WM_TE_FILEOP_ITEM, message)); // freed if the window is gone
    }

    [[nodiscard]] bool IsTopLevel(PCIDLIST_ABSOLUTE pidl) const
    {
        for (const auto& top : m_topLevel)
        {
            if (ILIsEqual(top.get(), pidl))
            {
                return true;
            }
        }
        return false;
    }

    HWND m_notify;
    std::uint64_t m_opId;
    FileOpKind m_kind;
    std::vector<wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>> m_topLevel;
    std::size_t m_succeeded = 0;
    std::size_t m_failed = 0;
    std::vector<Status> m_errors;
};

// The ID lists of the items in a Shell item array (the items a paste names).
std::vector<wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>> IdListsOf(IShellItemArray* items)
{
    std::vector<wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>> result;
    DWORD count = 0;
    if (!items || FAILED(items->GetCount(&count)))
    {
        return result;
    }
    for (DWORD i = 0; i < count; ++i)
    {
        wil::com_ptr<IShellItem> item;
        if (FAILED(items->GetItemAt(i, &item)))
        {
            continue;
        }
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        const HRESULT hr = SHGetIDListFromObject(item.get(), wil::out_param(pidl));
        if (SUCCEEDED(hr) && pidl)
        {
            result.push_back(std::move(pidl));
        }
    }
    return result;
}

// What a request operates on, built on the worker thread.
struct Prepared
{
    wil::com_ptr<IShellItemArray> items;
    wil::com_ptr<IDataObject> dataObject; // a paste
    wil::com_ptr<IShellItem> destination; // Copy and Move
    std::vector<wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>> topLevel;
};

HRESULT Prepare(Job& job, Prepared& prepared)
{
    const FileOpRequest& request = job.request;
    // The items: from the sources' ID lists, or from a paste's data object.
    if (job.dataObject)
    {
        RETURN_IF_FAILED(job.dataObject->Take(&prepared.dataObject));
        RETURN_IF_FAILED(
            SHCreateShellItemArrayFromDataObject(prepared.dataObject.get(), IID_PPV_ARGS(&prepared.items)));
    }
    else
    {
        RETURN_HR_IF(E_INVALIDARG, request.sources.empty());
        std::vector<PCIDLIST_ABSOLUTE> pidls;
        for (const ShellLocation& source : request.sources)
        {
            RETURN_HR_IF(E_INVALIDARG, !source.IsValid());
            pidls.push_back(source.IdList());
        }
        RETURN_IF_FAILED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(pidls.size()), pidls.data(),
                                                           &prepared.items));
    }
    prepared.topLevel = IdListsOf(prepared.items.get());
    RETURN_HR_IF(E_INVALIDARG, prepared.topLevel.empty());

    if (request.kind == FileOpKind::Copy || request.kind == FileOpKind::Move)
    {
        RETURN_HR_IF(E_INVALIDARG, !request.destination || !request.destination->IsValid());
        RETURN_IF_FAILED(request.destination->Item(&prepared.destination)); // created in this apartment
    }
    if (request.kind == FileOpKind::Rename)
    {
        RETURN_HR_IF(E_INVALIDARG, prepared.topLevel.size() != 1 || !request.newName ||
                                       !FileOperationService::ValidateNewName(*request.newName));
    }
    return S_OK;
}

// Queues the request's operations. Results reach the sink advised on `operation`.
HRESULT Queue(IFileOperation* operation, const FileOpRequest& request, const Prepared& prepared)
{
    // A data object is passed as is, so the Shell keeps its formats (for example virtual
    // files); otherwise the item array.
    IUnknown* const what = prepared.dataObject ? static_cast<IUnknown*>(prepared.dataObject.get())
                                               : static_cast<IUnknown*>(prepared.items.get());
    switch (request.kind)
    {
    case FileOpKind::Copy:
        return operation->CopyItems(what, prepared.destination.get());
    case FileOpKind::Move:
        return operation->MoveItems(what, prepared.destination.get());
    case FileOpKind::Recycle:
    case FileOpKind::DeletePermanent:
        return operation->DeleteItems(what);
    case FileOpKind::Rename: {
        wil::com_ptr<IShellItem> item;
        RETURN_IF_FAILED(prepared.items->GetItemAt(0, &item));
        return operation->RenameItem(item.get(), std::wstring(Trim(*request.newName)).c_str(), nullptr);
    }
    }
    return E_INVALIDARG;
}

void Run(HWND notify, Job& job, const FileOperationService::Factory& factory)
{
    auto done = std::make_unique<FileOpDone>();
    done->opId = job.request.id;

    wil::com_ptr<IFileOperation> operation;
    HRESULT hr = factory
                     ? factory(&operation)
                     : CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
    if (SUCCEEDED(hr) && !operation)
    {
        hr = E_NOINTERFACE;
    }
    if (SUCCEEDED(hr))
    {
        hr = operation->SetOperationFlags(FileOperationService::FlagsFor(job.request.kind));
    }
    if (SUCCEEDED(hr))
    {
        // Progress, conflict and confirmation UI is owned by the main window (R-07).
        hr = operation->SetOwnerWindow(notify);
    }
    Prepared prepared;
    if (SUCCEEDED(hr))
    {
        hr = Prepare(job, prepared);
    }
    Microsoft::WRL::ComPtr<ProgressSink> sink;
    DWORD cookie = 0;
    if (SUCCEEDED(hr))
    {
        sink = Microsoft::WRL::Make<ProgressSink>(notify, job.request.id, job.request.kind,
                                                  std::move(prepared.topLevel));
        hr = sink ? operation->Advise(sink.Get(), &cookie) : E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
    {
        hr = Queue(operation.get(), job.request, prepared);
    }

    HRESULT performed = hr;
    BOOL aborted = FALSE;
    if (SUCCEEDED(hr))
    {
        performed = operation->PerformOperations();
        LOG_IF_FAILED(operation->GetAnyOperationsAborted(&aborted));
    }
    if (cookie != 0)
    {
        LOG_IF_FAILED(operation->Unadvise(cookie));
    }
    done->aborted = aborted != FALSE;

    // Final state (data-model "FileOperationRequest"). Item failures decide first: a
    // failure must never read as a cancellation or a success.
    const std::size_t succeeded = sink ? sink->Succeeded() : 0;
    const std::size_t failed = sink ? sink->FailedCount() : 0;
    if (sink)
    {
        done->errors = std::move(sink->Errors());
    }
    if (failed > 0)
    {
        done->state = succeeded > 0 ? FileOpFinalState::PartiallySucceeded : FileOpFinalState::Failed;
    }
    else if (done->aborted || IsCancel(performed))
    {
        done->state = FileOpFinalState::Cancelled;
    }
    else if (FAILED(performed))
    {
        done->state = succeeded > 0 ? FileOpFinalState::PartiallySucceeded : FileOpFinalState::Failed;
    }
    else
    {
        done->state = FileOpFinalState::Succeeded;
    }
    if ((done->state == FileOpFinalState::Failed || done->state == FileOpFinalState::PartiallySucceeded) &&
        done->errors.empty())
    {
        done->errors.push_back(MakeStatus(FAILED(performed) ? performed : E_FAIL, {}));
    }
    // Release everything before the final message. A paste's data object is a proxy to
    // the UI thread's apartment, and releasing it is a call into that thread: once the UI
    // has seen FileOpDone it may stop pumping (for example to shut down), so no call may
    // follow it.
    sink.Reset();
    prepared = Prepared{};
    operation.reset();
    static_cast<void>(PostOwned(notify, WM_TE_FILEOP_DONE, done));
}

} // namespace

FileOperationService::FileOperationService(Factory factory)
    : m_factory(std::move(factory)), m_worker(std::make_unique<StaWorker>())
{
}

FileOperationService::~FileOperationService()
{
    Shutdown();
}

void FileOperationService::Submit(HWND notifyHwnd, FileOpRequest request)
{
    if (!m_worker)
    {
        auto done = std::make_unique<FileOpDone>();
        done->opId = request.id;
        done->state = FileOpFinalState::Failed;
        done->errors.push_back(MakeStatus(HRESULT_FROM_WIN32(ERROR_SHUTDOWN_IN_PROGRESS), {}));
        static_cast<void>(PostOwned(notifyHwnd, WM_TE_FILEOP_DONE, done));
        return;
    }
    auto job = std::make_shared<Job>();
    if (request.dataObject)
    {
        job->dataObject = std::make_unique<MarshaledDataObject>(request.dataObject.get());
        request.dataObject.reset(); // released in this apartment
    }
    job->request = std::move(request);
    // Requests run one at a time, in order. The Shell's own UI keeps each one modal to
    // the owner window without blocking the UI thread.
    m_worker->Post(
        [notifyHwnd, job, factory = m_factory](std::stop_token) { Run(notifyHwnd, *job, factory); });
}

void FileOperationService::Shutdown()
{
    if (!m_worker)
    {
        return;
    }
    // The running operation finishes (it cannot be interrupted safely); requests still
    // queued are dropped. Nothing has touched their files yet.
    m_worker->RequestStop();
    // A running paste calls into its data object, which lives in this (the UI) thread's
    // apartment, so this thread must keep serving COM calls while it waits.
    m_worker->JoinServingComCalls();
    m_worker.reset();
}

DWORD FileOperationService::FlagsFor(FileOpKind kind) noexcept
{
    // Research R-08. Never FOF_NOCONFIRMATION, FOF_NOERRORUI or FOF_RENAMEONCOLLISION: the
    // Shell asks about conflicts and permanent deletes and reports errors itself.
    DWORD flags = FOFX_ADDUNDORECORD;
    if (kind == FileOpKind::Recycle)
    {
        // To the Recycle Bin, and warn instead of silently deleting an item the Recycle
        // Bin cannot take (too large, or on a volume without one).
        flags |= FOF_ALLOWUNDO | FOF_WANTNUKEWARNING;
    }
    return flags;
}

bool FileOperationService::ValidateNewName(std::wstring_view name) noexcept
{
    const std::wstring_view trimmed = Trim(name);
    if (trimmed.empty())
    {
        return false;
    }
    for (const wchar_t c : trimmed)
    {
        if (c < 0x20 || std::wstring_view(LR"(\/:*?"<>|)").find(c) != std::wstring_view::npos)
        {
            return false;
        }
    }
    return true;
}

} // namespace te
