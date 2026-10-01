#include <te/ui/IconCache.h>

#include <te/ui/FileView.h>

#include <wil/result.h>

namespace te
{

bool IconCache::SetDevice(ID2D1Device* device)
{
    if (device == m_device.get())
    {
        return false;
    }
    const bool changed = m_device != nullptr;
    m_bitmaps.clear();
    m_device = device;
    return changed;
}

wil::com_ptr<ID2D1Bitmap1> IconCache::Find(int imageIndex, int sizePx) const
{
    if (imageIndex < 0)
    {
        return nullptr;
    }
    const auto it = m_bitmaps.find({imageIndex, sizePx});
    return it == m_bitmaps.end() ? nullptr : it->second;
}

bool IconCache::Drain(ID2D1DeviceContext* dc, Generation iconGen,
                      std::deque<std::unique_ptr<IconReady>>& queue, const Deliver& deliver)
{
    std::size_t converted = 0;
    while (!queue.empty())
    {
        IconReady& result = *queue.front();
        if (result.gen != iconGen)
        {
            queue.pop_front(); // for a listing or a size no longer shown
            continue;
        }
        if (wil::com_ptr<ID2D1Bitmap1> cached = Find(result.imageIndex, result.sizePx))
        {
            deliver(result, std::move(cached), false);
            queue.pop_front();
            continue;
        }
        if (result.shared)
        {
            // The worker sent this icon once already, but the cache no longer has it.
            deliver(result, nullptr, true);
            queue.pop_front();
            continue;
        }
        wil::com_ptr<ID2D1Bitmap1> bitmap;
        if (result.bmp)
        {
            if (converted == kMaxConversionsPerFrame)
            {
                return true; // the rest in the next frame
            }
            ++converted;
            ++m_conversions;
            LOG_IF_FAILED(FileView::IconFromHBitmap(dc, result.bmp.get(), &bitmap));
            if (bitmap && result.imageIndex >= 0)
            {
                m_bitmaps[{result.imageIndex, result.sizePx}] = bitmap;
            }
        }
        deliver(result, std::move(bitmap), false); // null: the fallback icon
        queue.pop_front();
    }
    return false;
}

} // namespace te
