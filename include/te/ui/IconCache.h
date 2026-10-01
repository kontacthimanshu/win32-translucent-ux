#pragma once

// UI side of the file icons (T087; research R-06, R-07). Icons arrive from the icon worker
// as WM_TE_ICON_READY and wait in a queue until the next frame; there they become Direct2D
// bitmaps, at most kMaxConversionsPerFrame per frame, the rest in the frames after.
// Items that share a Shell icon (all .txt files, all folders, ...) share one bitmap:
// bitmaps are kept by system image-list index and pixel size, and the worker sends no
// bitmap for an index it has already sent (IconReady::shared).

#include <te/core/Messages.h>
#include <te/core/Types.h>

#include <d2d1_1.h>

#include <wil/com.h>

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <utility>

namespace te
{

class IconCache
{
  public:
    static constexpr std::size_t kMaxConversionsPerFrame = 32;

    // What becomes of one result: the bitmap for the row (null = the fallback icon), or
    // `extractAgain` when the worker skipped a shared icon the cache no longer has (after a
    // device loss); the row then asks for it again with extraction forced.
    using Deliver =
        std::function<void(const IconReady& result, wil::com_ptr<ID2D1Bitmap1> bitmap, bool extractAgain)>;

    // Bitmaps belong to one Direct2D device. Returns true when `device` differs from the
    // one the cached bitmaps were made on; they are then dropped (after a device loss).
    bool SetDevice(ID2D1Device* device);

    // Handles the queued results in order: results for another icon generation are dropped;
    // a shared or already cached icon is delivered from the cache without a conversion;
    // anything else is converted from its HBITMAP, at most kMaxConversionsPerFrame times.
    // Returns true when results are left for the next frame.
    bool Drain(ID2D1DeviceContext* dc, Generation iconGen, std::deque<std::unique_ptr<IconReady>>& queue,
               const Deliver& deliver);

    [[nodiscard]] wil::com_ptr<ID2D1Bitmap1> Find(int imageIndex, int sizePx) const;
    [[nodiscard]] std::size_t Size() const noexcept
    {
        return m_bitmaps.size();
    }
    // HBITMAP -> Direct2D conversions since construction (tests).
    [[nodiscard]] std::size_t Conversions() const noexcept
    {
        return m_conversions;
    }

  private:
    std::map<std::pair<int, int>, wil::com_ptr<ID2D1Bitmap1>> m_bitmaps; // (image index, px)
    wil::com_ptr<ID2D1Device> m_device;
    std::size_t m_conversions = 0;
};

} // namespace te
