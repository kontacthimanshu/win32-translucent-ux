#include <te/shell/IconProvider.h>

#include <te/core/Messages.h>
#include <te/core/StaWorker.h>

#include <shellapi.h>
#include <shlobj.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <atomic>
#include <memory>
#include <set>
#include <utility>

namespace te
{

struct IconProvider::Impl
{
    // Requests for generations below this are dropped when they reach the front.
    std::atomic<Generation> floor{0};
    bool shutDown = false;
    // (image index, size) pairs already sent with a bitmap. Worker thread only.
    std::set<std::pair<int, int>> sent;
    StaWorker worker; // last: its thread stops before the members above go away
};

namespace
{

// Runs on the icon worker (its own STA). Always posts a result, with a null bitmap on
// failure, so the row can show a fallback instead of waiting forever.
void Extract(std::set<std::pair<int, int>>& sent, HWND notify, Generation gen, std::size_t itemKey,
             PCIDLIST_ABSOLUTE pidl, int sizePx, bool forceExtract)
{
    auto ready = std::make_unique<IconReady>();
    ready->gen = gen;
    ready->itemKey = itemKey;
    ready->sizePx = sizePx;

    // Items that share an icon share its system image-list index (T087): the bitmap is sent
    // once per index and size, and the UI reuses its Direct2D copy for the others.
    SHFILEINFOW info{};
    if (pidl && SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &info, sizeof(info),
                               SHGFI_PIDL | SHGFI_SYSICONINDEX) != 0)
    {
        ready->imageIndex = info.iIcon;
    }
    if (!forceExtract && ready->imageIndex >= 0 && sent.contains({ready->imageIndex, sizePx}))
    {
        ready->shared = true;
        (void)PostOwned(notify, WM_TE_ICON_READY, ready);
        return;
    }

    wil::com_ptr<IShellItemImageFactory> factory;
    if (pidl && SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&factory))))
    {
        HBITMAP bitmap = nullptr;
        // The file-type icon (not a thumbnail) at the row's size; a larger image is fine,
        // the UI scales it (R-06).
        if (SUCCEEDED(
                factory->GetImage(SIZE{sizePx, sizePx}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap)))
        {
            ready->bmp.reset(bitmap);
        }
    }
    const std::pair<int, int> key{ready->imageIndex, sizePx};
    const bool extracted = ready->bmp != nullptr;
    // Freed here if the window is gone; then the UI never got it, so it is not marked sent.
    if (PostOwned(notify, WM_TE_ICON_READY, ready) && extracted && key.first >= 0)
    {
        sent.insert(key);
    }
}

} // namespace

IconProvider::IconProvider() : m_impl(std::make_unique<Impl>()) {}

IconProvider::~IconProvider()
{
    Shutdown();
}

void IconProvider::Request(HWND notifyHwnd, Generation gen, std::size_t itemKey, const ShellLocation& folder,
                           const ShellItemInfo& item, int sizePx, bool forceExtract)
{
    if (m_impl->shutDown || !folder.IsValid() || !item.childPidl)
    {
        return;
    }
    // The absolute ID list is built here; the worker creates its own IShellItem from it.
    // Shared, because StaWorker tasks are std::function and must be copyable.
    std::shared_ptr<ITEMIDLIST_ABSOLUTE> pidl(
        reinterpret_cast<ITEMIDLIST_ABSOLUTE*>(ILCombine(folder.IdList(), item.childPidl.get())),
        [](ITEMIDLIST_ABSOLUTE* p) { CoTaskMemFree(p); });
    if (!pidl)
    {
        return;
    }
    // Shutdown() joins the worker before Impl is destroyed, so the raw pointer is safe.
    Impl* impl = m_impl.get();
    // LIFO (T087, R-07): the newest request runs first, so the rows the user looks at now
    // are served before older ones and before the look-ahead.
    impl->worker.PostFront(
        [impl, notifyHwnd, gen, itemKey, sizePx, pidl, forceExtract](std::stop_token stop) {
            if (stop.stop_requested() || gen < impl->floor.load())
            {
                return; // cancelled: the list has moved on
            }
            Extract(impl->sent, notifyHwnd, gen, itemKey, pidl.get(), sizePx, forceExtract);
        });
}

void IconProvider::CancelOlderThan(Generation gen)
{
    // Monotonic: a late call with an older generation never lowers the floor.
    Generation current = m_impl->floor.load();
    while (gen > current && !m_impl->floor.compare_exchange_weak(current, gen))
    {
    }
}

void IconProvider::Shutdown()
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
