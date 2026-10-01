#include <te/shell/DirectoryEnumerator.h>

#include <te/core/Messages.h>
#include <te/core/StaWorker.h>

#include <propkey.h>
#include <shlobj.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <memory>
#include <mutex>
#include <stop_token>
#include <utility>
#include <vector>

namespace te
{

namespace
{

constexpr ULONG kFetchCount = 64; // IEnumShellItems::Next batch (R-07)
constexpr SFGAOF kAttributes = SFGAO_FOLDER | SFGAO_STREAM | SFGAO_HIDDEN | SFGAO_FILESYSTEM |
                               SFGAO_CANRENAME | SFGAO_CANDELETE | SFGAO_CANCOPY | SFGAO_CANMOVE;

bool ShowHiddenItems()
{
    SHELLSTATEW state{};
    SHGetSetSettings(&state, SSF_SHOWALLOBJECTS, FALSE);
    return state.fShowAllObjects != FALSE;
}

// Reads one item on the worker thread. Returns false for items that are not listed
// (hidden items while Explorer hides them).
bool ReadItem(IShellItem* item, bool showHidden, ShellItemInfo& info)
{
    SFGAOF attributes = 0;
    (void)item->GetAttributes(kAttributes, &attributes); // S_FALSE when only some are set
    info.isHidden = (attributes & SFGAO_HIDDEN) != 0;
    if (info.isHidden && !showHidden)
    {
        return false;
    }
    // Zip files are folders to the Shell but streams too; list them as files.
    info.isFolder = (attributes & SFGAO_FOLDER) != 0 && (attributes & SFGAO_STREAM) == 0;
    info.canRename = (attributes & SFGAO_CANRENAME) != 0;
    info.canDelete = (attributes & SFGAO_CANDELETE) != 0;
    info.canCopy = (attributes & SFGAO_CANCOPY) != 0;
    info.canMove = (attributes & SFGAO_CANMOVE) != 0;

    wil::unique_cotaskmem_string name;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name)
    {
        info.name = name.get();
    }
    if (info.canRename)
    {
        const SIGDN editing =
            (attributes & SFGAO_FILESYSTEM) != 0 ? SIGDN_PARENTRELATIVEPARSING : SIGDN_PARENTRELATIVEEDITING;
        wil::unique_cotaskmem_string editName;
        info.editName =
            SUCCEEDED(item->GetDisplayName(editing, &editName)) && editName ? editName.get() : info.name;
    }

    // Identity within the folder: the last ID of the absolute ID list.
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> absolute;
    // A separate statement: wil::out_param writes `absolute` back only when its proxy is
    // destroyed, at the end of the full expression, so testing `absolute` in the same
    // condition would always see null.
    const HRESULT idHr = SHGetIDListFromObject(item, wil::out_param(absolute));
    if (SUCCEEDED(idHr) && absolute)
    {
        info.childPidl.reset(reinterpret_cast<ITEMID_CHILD*>(ILClone(ILFindLastID(absolute.get()))));
    }

    if (wil::com_ptr<IShellItem2> item2 = wil::try_com_query<IShellItem2>(item))
    {
        wil::unique_cotaskmem_string typeText;
        if (SUCCEEDED(item2->GetString(PKEY_ItemTypeText, &typeText)) && typeText)
        {
            info.typeText = typeText.get();
        }
        ULONGLONG size = 0;
        if (!info.isFolder && SUCCEEDED(item2->GetUInt64(PKEY_Size, &size)))
        {
            info.size = size;
        }
        FILETIME modified{};
        if (SUCCEEDED(item2->GetFileTime(PKEY_DateModified, &modified)))
        {
            info.modified = modified;
        }
    }
    return true;
}

enum class PostResult
{
    Posted,
    Cancelled, // the request was cancelled first: nothing posted
    WindowGone,
};

// Sends what has been read so far. The stop check and the post happen under `postLock`,
// which CancelAll, Start and Shutdown also take: once they return, a cancelled request
// posts no further batch (T084 b).
template <class Stopped>
PostResult PostBatch(std::mutex& postLock, const Stopped& stopped, HWND notify, Generation gen,
                     std::vector<ShellItemInfo>& items)
{
    if (items.empty())
    {
        return PostResult::Posted;
    }
    auto batch = std::make_unique<EnumBatch>();
    batch->gen = gen;
    batch->items = std::move(items);
    items.clear();
    const std::scoped_lock lock(postLock);
    if (stopped())
    {
        return PostResult::Cancelled; // `batch` is freed here
    }
    // On failure `batch` is freed here.
    return PostOwned(notify, WM_TE_ENUM_BATCH, batch) ? PostResult::Posted : PostResult::WindowGone;
}

// The enumeration task: runs on the STA worker. Cancelled when either the worker stops
// (Shutdown) or this request is superseded (Start / CancelAll).
void Enumerate(std::mutex& postLock, HWND notify, Generation gen, const ShellLocation& location,
               std::stop_token worker, std::stop_token request)
{
    const auto stopped = [&] { return worker.stop_requested() || request.stop_requested(); };
    HRESULT result = S_OK;
    bool cancelled = false;

    [&] {
        wil::com_ptr<IShellItem> folder;
        result = location.Item(&folder); // created here, in the worker's own apartment
        if (FAILED(result))
        {
            return;
        }
        wil::com_ptr<IEnumShellItems> items;
        result = folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items));
        if (FAILED(result))
        {
            return;
        }

        const bool showHidden = ShowHiddenItems();
        std::vector<ShellItemInfo> pending;
        pending.reserve(DirectoryEnumerator::kBatchSize);
        for (;;)
        {
            if (stopped())
            {
                cancelled = true;
                return;
            }
            IShellItem* fetched[kFetchCount]{};
            ULONG count = 0;
            const HRESULT hr = items->Next(kFetchCount, fetched, &count);
            for (ULONG i = 0; i < count; ++i)
            {
                wil::com_ptr<IShellItem> item;
                item.attach(fetched[i]);
                ShellItemInfo info;
                if (ReadItem(item.get(), showHidden, info))
                {
                    pending.push_back(std::move(info));
                }
                if (pending.size() == DirectoryEnumerator::kBatchSize &&
                    PostBatch(postLock, stopped, notify, gen, pending) != PostResult::Posted)
                {
                    cancelled = true; // cancelled, or nobody to deliver to
                    return;
                }
            }
            if (FAILED(hr))
            {
                result = hr; // e.g. E_ACCESSDENIED, or a disconnected network location
                return;
            }
            if (hr == S_FALSE || count == 0)
            {
                break; // end of the folder
            }
        }
        if (PostBatch(postLock, stopped, notify, gen, pending) != PostResult::Posted)
        {
            cancelled = true;
        }
    }();

    auto done = std::make_unique<EnumDone>();
    done->gen = gen;
    done->hr = cancelled ? S_OK : result;
    done->cancelled = cancelled;
    (void)PostOwned(notify, WM_TE_ENUM_DONE, done);
}

} // namespace

struct DirectoryEnumerator::Impl
{
    std::mutex postLock;      // a batch's stop check and post vs. cancelling (T084 b)
    std::stop_source request; // the running (or last) request; replaced by Start
    bool shutDown = false;
    StaWorker worker; // last: its thread stops before the members above go away
};

DirectoryEnumerator::DirectoryEnumerator() : m_impl(std::make_unique<Impl>()) {}

DirectoryEnumerator::~DirectoryEnumerator()
{
    Shutdown();
}

void DirectoryEnumerator::Start(HWND notifyHwnd, Generation gen, const ShellLocation& loc)
{
    if (m_impl->shutDown)
    {
        return;
    }
    // Supersede the previous request: it posts no further batch and reports cancelled.
    std::stop_token request;
    {
        const std::scoped_lock lock(m_impl->postLock);
        m_impl->request.request_stop();
        m_impl->request = std::stop_source{};
        request = m_impl->request.get_token();
    }

    // A copy owns its own ID list and never shares an IShellItem across threads (T053).
    // Shutdown() joins the worker before Impl goes away, so the lock outlives the task.
    std::mutex* postLock = &m_impl->postLock;
    m_impl->worker.Post([postLock, notifyHwnd, gen, location = ShellLocation(loc),
                         request = std::move(request)](std::stop_token worker) {
        Enumerate(*postLock, notifyHwnd, gen, location, std::move(worker), request);
    });
}

void DirectoryEnumerator::CancelAll()
{
    // Under the post lock: when this returns, the cancelled request posts no batch.
    const std::scoped_lock lock(m_impl->postLock);
    m_impl->request.request_stop();
}

void DirectoryEnumerator::Shutdown()
{
    if (!m_impl || m_impl->shutDown)
    {
        return;
    }
    m_impl->shutDown = true;
    {
        const std::scoped_lock lock(m_impl->postLock);
        m_impl->request.request_stop();
    }
    m_impl->worker.RequestStop();
    m_impl->worker.Join();
}

} // namespace te
