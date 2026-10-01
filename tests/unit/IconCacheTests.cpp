// IconCache (T087; research R-06, R-07): icons that share a Shell icon share one Direct2D
// bitmap, at most 32 HBITMAP conversions happen per frame, results for another icon
// generation are dropped, and a device change empties the cache. Runs on a WARP device.

#include <te/ui/IconCache.h>

#include <d3d11.h>
#include <gtest/gtest.h>
#include <wil/com.h>
#include <wil/resource.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace
{

struct Delivered
{
    std::size_t key = 0;
    wil::com_ptr<ID2D1Bitmap1> bitmap;
    bool extractAgain = false;
};

class IconCacheTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // WIC (the HBITMAP conversion) is a COM object.
        m_uninitialize = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        ASSERT_HRESULT_SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                                   D3D11_SDK_VERSION, &m_d3d, nullptr, nullptr));
        ASSERT_HRESULT_SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_factory.addressof()));
        ASSERT_HRESULT_SUCCEEDED(m_factory->CreateDevice(m_d3d.query<IDXGIDevice>().get(), &m_device));
        ASSERT_HRESULT_SUCCEEDED(m_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_dc));
        EXPECT_FALSE(m_cache.SetDevice(m_device.get())) << "the first device is not a change";
    }

    // A result as the worker posts it: a 16 x 16 icon unless `shared` or `failed`.
    std::unique_ptr<te::IconReady> Result(te::Generation gen, std::size_t key, int imageIndex,
                                          bool shared = false, bool failed = false)
    {
        auto result = std::make_unique<te::IconReady>();
        result->gen = gen;
        result->itemKey = key;
        result->imageIndex = imageIndex;
        result->sizePx = 16;
        result->shared = shared;
        if (!shared && !failed)
        {
            BITMAPINFO info{};
            info.bmiHeader = {sizeof(BITMAPINFOHEADER), 16, -16, 1, 32, BI_RGB};
            void* bits = nullptr;
            result->bmp.reset(CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
        }
        return result;
    }

    bool Drain(te::Generation gen)
    {
        return m_cache.Drain(m_dc.get(), gen, m_queue,
                             [this](const te::IconReady& r, wil::com_ptr<ID2D1Bitmap1> bitmap, bool again) {
                                 m_delivered.push_back({r.itemKey, std::move(bitmap), again});
                             });
    }

    void TearDown() override
    {
        m_queue.clear();
        m_delivered.clear();
        m_cache.SetDevice(nullptr);
        m_dc.reset();
        m_device.reset();
        m_factory.reset();
        m_d3d.reset();
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    bool m_uninitialize = false;
    wil::com_ptr<ID3D11Device> m_d3d;
    wil::com_ptr<ID2D1Factory1> m_factory;
    wil::com_ptr<ID2D1Device> m_device;
    wil::com_ptr<ID2D1DeviceContext> m_dc;
    te::IconCache m_cache;
    std::deque<std::unique_ptr<te::IconReady>> m_queue;
    std::vector<Delivered> m_delivered;
};

TEST_F(IconCacheTest, ItemsThatShareAnIconShareOneBitmap)
{
    m_queue.push_back(Result(1, 10, 7));              // first .txt file: extracted
    m_queue.push_back(Result(1, 11, 7, true));        // second: shared, no bitmap sent
    m_queue.push_back(Result(1, 12, 7));              // extracted anyway (forced): still one copy
    m_queue.push_back(Result(1, 13, -1));             // no image index: converted, not cached
    m_queue.push_back(Result(1, 14, 8, false, true)); // extraction failed: the fallback
    EXPECT_FALSE(Drain(1));
    ASSERT_EQ(m_delivered.size(), 5u);
    ASSERT_TRUE(m_delivered[0].bitmap);
    EXPECT_EQ(m_delivered[1].bitmap.get(), m_delivered[0].bitmap.get()) << "the same Direct2D bitmap";
    EXPECT_EQ(m_delivered[2].bitmap.get(), m_delivered[0].bitmap.get()) << "found in the cache first";
    EXPECT_TRUE(m_delivered[3].bitmap);
    EXPECT_FALSE(m_delivered[4].bitmap);
    for (const Delivered& d : m_delivered)
    {
        EXPECT_FALSE(d.extractAgain);
    }
    EXPECT_EQ(m_cache.Conversions(), 2u) << "one per distinct icon, plus the unindexed one";
    EXPECT_EQ(m_cache.Size(), 1u);
    EXPECT_TRUE(m_queue.empty());
}

TEST_F(IconCacheTest, AtMost32ConversionsPerFrame)
{
    for (int i = 0; i < 40; ++i)
    {
        m_queue.push_back(Result(1, static_cast<std::size_t>(i), 100 + i)); // 40 different icons
    }
    m_queue.push_back(Result(1, 99, 100, true)); // shares the first one, behind the others
    EXPECT_TRUE(Drain(1)) << "results left for the next frame";
    EXPECT_EQ(m_delivered.size(), 32u);
    EXPECT_EQ(m_cache.Conversions(), 32u);
    EXPECT_EQ(m_queue.size(), 9u);

    EXPECT_FALSE(Drain(1));
    EXPECT_EQ(m_delivered.size(), 41u);
    EXPECT_EQ(m_cache.Conversions(), 40u);
    EXPECT_EQ(m_delivered.back().key, 99u);
    EXPECT_EQ(m_delivered.back().bitmap.get(), m_delivered.front().bitmap.get()) << "order kept";
}

TEST_F(IconCacheTest, CachedIconsDoNotCountAgainstTheBudget)
{
    m_queue.push_back(Result(1, 0, 5));
    for (int i = 1; i <= 100; ++i)
    {
        m_queue.push_back(Result(1, static_cast<std::size_t>(i), 5, true));
    }
    EXPECT_FALSE(Drain(1));
    EXPECT_EQ(m_delivered.size(), 101u);
    EXPECT_EQ(m_cache.Conversions(), 1u);
}

TEST_F(IconCacheTest, OtherGenerationsAreDropped)
{
    m_queue.push_back(Result(1, 1, 3)); // for a listing or size no longer shown
    m_queue.push_back(Result(2, 2, 4));
    EXPECT_FALSE(Drain(2));
    ASSERT_EQ(m_delivered.size(), 1u);
    EXPECT_EQ(m_delivered[0].key, 2u);
    EXPECT_EQ(m_cache.Conversions(), 1u);
}

TEST_F(IconCacheTest, ASharedIconMissingFromTheCacheIsExtractedAgain)
{
    m_queue.push_back(Result(1, 1, 9, true)); // the cache never had index 9
    EXPECT_FALSE(Drain(1));
    ASSERT_EQ(m_delivered.size(), 1u);
    EXPECT_TRUE(m_delivered[0].extractAgain);
    EXPECT_FALSE(m_delivered[0].bitmap);
}

TEST_F(IconCacheTest, ANewDeviceEmptiesTheCache)
{
    m_queue.push_back(Result(1, 1, 3));
    EXPECT_FALSE(Drain(1));
    ASSERT_TRUE(m_cache.Find(3, 16));
    EXPECT_FALSE(m_cache.SetDevice(m_device.get())) << "the same device";
    EXPECT_TRUE(m_cache.Find(3, 16));

    wil::com_ptr<ID2D1Device> other;
    ASSERT_HRESULT_SUCCEEDED(m_factory->CreateDevice(m_d3d.query<IDXGIDevice>().get(), &other));
    EXPECT_TRUE(m_cache.SetDevice(other.get())) << "after a device loss";
    EXPECT_FALSE(m_cache.Find(3, 16));
    EXPECT_EQ(m_cache.Size(), 0u);
}

TEST_F(IconCacheTest, SizesAreCachedSeparately)
{
    auto big = Result(1, 2, 3);
    big->sizePx = 24; // the same icon at another DPI
    m_queue.push_back(Result(1, 1, 3));
    m_queue.push_back(std::move(big));
    EXPECT_FALSE(Drain(1));
    EXPECT_EQ(m_cache.Conversions(), 2u);
    EXPECT_NE(m_cache.Find(3, 16).get(), m_cache.Find(3, 24).get());
}

} // namespace
