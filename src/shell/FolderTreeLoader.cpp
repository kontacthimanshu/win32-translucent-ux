#include <te/shell/FolderTreeLoader.h>

#include <te/core/StaWorker.h>

#include <shlobj.h>
#include <shlwapi.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <utility>

namespace te
{

struct FolderTreeLoader::Impl
{
    // Requests issued before this number are dropped when they reach the front.
    std::atomic<std::uint64_t> floor{0};
    std::atomic<std::uint64_t> issued{0};
    bool shutDown = false;
    StaWorker worker; // last: its thread stops before the members above go away
};

namespace
{

bool ShowHiddenItems()
{
    SHELLSTATEW state{};
    SHGetSetSettings(&state, SSF_SHOWALLOBJECTS, FALSE);
    return state.fShowAllObjects != FALSE;
}

// "C:\" and the like: drives come after the folders, as in Explorer's tree under This PC.
bool IsDriveRoot(const ShellLocation& location)
{
    const std::optional<std::wstring> path = location.ParsingPath();
    return path && path->size() == 3 && std::iswalpha((*path)[0]) && (*path)[1] == L':' &&
           (*path)[2] == L'\\';
}

wil::unique_hbitmap IconOf(PCIDLIST_ABSOLUTE pidl, int sizePx)
{
    wil::com_ptr<IShellItemImageFactory> factory;
    HBITMAP bitmap = nullptr;
    if (SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->GetImage(SIZE{sizePx, sizePx}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)))
    {
        return wil::unique_hbitmap(bitmap);
    }
    return {};
}

// Runs on the loader's worker (its own STA). Always posts a result, so the node stops
// showing that it is loading.
void List(HWND notify, std::uint64_t nodeId, const ShellLocation& folder, int iconPx)
{
    auto result = std::make_unique<FolderChildren>();
    result->nodeId = nodeId;
    result->hr = [&]() -> HRESULT {
        wil::com_ptr<IShellItem> item;
        RETURN_IF_FAILED(folder.Item(&item));
        wil::com_ptr<IShellFolder> shellFolder;
        RETURN_IF_FAILED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&shellFolder)));
        wil::com_ptr<IEnumIDList> children;
        const SHCONTF flags = SHCONTF_FOLDERS | (ShowHiddenItems() ? SHCONTF_INCLUDEHIDDEN : 0);
        const HRESULT enumHr = shellFolder->EnumObjects(nullptr, flags, &children);
        RETURN_IF_FAILED(enumHr);
        if (enumHr == S_FALSE || !children)
        {
            return S_OK; // nothing to list
        }
        for (;;)
        {
            wil::unique_cotaskmem_ptr<ITEMID_CHILD> child;
            ULONG fetched = 0;
            if (children->Next(1, wil::out_param(child), &fetched) != S_OK || fetched == 0)
            {
                break;
            }
            PCUITEMID_CHILD childList[] = {child.get()};
            SFGAOF attributes = SFGAO_FOLDER | SFGAO_STREAM | SFGAO_HASSUBFOLDER;
            if (FAILED(shellFolder->GetAttributesOf(1, childList, &attributes)))
            {
                continue;
            }
            // Zip files are folders to the Shell but streams too; the tree leaves them out.
            if ((attributes & SFGAO_FOLDER) == 0 || (attributes & SFGAO_STREAM) != 0)
            {
                continue;
            }
            wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> absolute(
                reinterpret_cast<ITEMIDLIST_ABSOLUTE*>(ILCombine(folder.IdList(), child.get())));
            FolderChildren::Child entry;
            if (!absolute || FAILED(ShellLocation::FromIdList(absolute.get(), &entry.location)))
            {
                continue;
            }
            entry.hasSubfolders = (attributes & SFGAO_HASSUBFOLDER) != 0;
            entry.icon = IconOf(absolute.get(), iconPx);
            result->children.push_back(std::move(entry));
        }
        return S_OK;
    }();

    std::stable_sort(result->children.begin(), result->children.end(),
                     [](const FolderChildren::Child& a, const FolderChildren::Child& b) {
                         const bool aDrive = IsDriveRoot(a.location);
                         const bool bDrive = IsDriveRoot(b.location);
                         if (aDrive != bDrive)
                         {
                             return !aDrive;
                         }
                         return StrCmpLogicalW(a.location.DisplayName().c_str(),
                                               b.location.DisplayName().c_str()) < 0;
                     });
    (void)PostOwned(notify, WM_TE_TREE_CHILDREN, result); // freed here if the window is gone
}

} // namespace

FolderTreeLoader::FolderTreeLoader() : m_impl(std::make_unique<Impl>()) {}

FolderTreeLoader::~FolderTreeLoader()
{
    Shutdown();
}

void FolderTreeLoader::Request(HWND notifyHwnd, std::uint64_t nodeId, const ShellLocation& folder, int iconPx)
{
    // No window: PostMessage(nullptr) would post to the worker thread itself, and leak.
    if (m_impl->shutDown || !notifyHwnd || !folder.IsValid())
    {
        return;
    }
    Impl* impl = m_impl.get(); // Shutdown() joins the worker before Impl goes away
    const std::uint64_t number = ++impl->issued;
    impl->worker.Post(
        [impl, number, notifyHwnd, nodeId, location = ShellLocation(folder), iconPx](std::stop_token stop) {
            if (stop.stop_requested() || number <= impl->floor.load())
            {
                return; // cancelled
            }
            List(notifyHwnd, nodeId, location, iconPx);
        });
}

void FolderTreeLoader::CancelAll()
{
    m_impl->floor.store(m_impl->issued.load());
}

void FolderTreeLoader::Shutdown()
{
    if (!m_impl || m_impl->shutDown)
    {
        return;
    }
    m_impl->shutDown = true;
    m_impl->worker.RequestStop();
    m_impl->worker.Join();
}

} // namespace te
