// FileView (T060): generations, sorted merging of batches, header sorting and resizing,
// mouse and keyboard selection, scrolling, icons by key, rename, and a rendering smoke
// test on an offscreen WARP device.

#include <te/ui/FileView.h>

#include <d3d11.h>
#include <gtest/gtest.h>
#include <wil/com.h>
#include <wil/result.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{

constexpr D2D1_RECT_F kBounds{0.0f, 0.0f, 800.0f, 28.0f + 10 * 28.0f}; // header + 10 rows

te::FileItem Item(const wchar_t* name, bool folder = false, std::uint64_t size = 1)
{
    te::FileItem item;
    item.info.name = name;
    item.info.isFolder = folder;
    if (!folder)
    {
        item.info.size = size;
    }
    item.info.typeText = folder ? L"File folder" : L"File";
    return item;
}

std::vector<te::FileItem> Batch(std::initializer_list<const wchar_t*> names)
{
    std::vector<te::FileItem> items;
    for (const wchar_t* name : names)
    {
        items.push_back(Item(name));
    }
    return items;
}

std::vector<std::wstring> Names(const te::FileView& view)
{
    std::vector<std::wstring> names;
    for (const te::FileItem& item : view.Items())
    {
        names.push_back(item.info.name);
    }
    return names;
}

D2D1_POINT_2F RowPoint(const te::FileView& view, std::size_t row)
{
    const D2D1_RECT_F r = view.RowRect(row);
    return D2D1::Point2F(100.0f, (r.top + r.bottom) / 2.0f);
}

class FileViewTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_view.SetBounds(kBounds);
        m_view.SetCallbacks({[this](const te::FileItem& item) { m_opened.push_back(item.info.name); },
                             [this](std::size_t items, std::size_t selected) {
                                 m_items = items;
                                 m_selected = selected;
                             },
                             [this] { ++m_invalidations; }});
        m_view.BeginLocation(1);
    }

    std::vector<std::wstring> SelectedNames() const
    {
        std::vector<std::wstring> names;
        for (const te::FileItem* item : m_view.Selection())
        {
            names.push_back(item->info.name);
        }
        return names;
    }

    te::FileView m_view;
    std::vector<std::wstring> m_opened;
    std::size_t m_items = 0;
    std::size_t m_selected = 0;
    int m_invalidations = 0;
};

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

TEST_F(FileViewTest, BatchesMergeIntoSortedOrder)
{
    m_view.AppendItems(1, Batch({L"file10", L"b"}));
    m_view.AppendItems(1, Batch({L"file2", L"a"}));
    auto folder = std::vector<te::FileItem>{};
    folder.push_back(Item(L"zeta", true));
    m_view.AppendItems(1, std::move(folder));
    EXPECT_EQ(Names(m_view), (std::vector<std::wstring>{L"zeta", L"a", L"b", L"file2", L"file10"}));
    EXPECT_EQ(m_items, 5u);
}

TEST_F(FileViewTest, StaleBatchesAreIgnoredAndBeginLocationClears)
{
    m_view.AppendItems(1, Batch({L"a", L"b"}));
    m_view.OnPointerDown(RowPoint(m_view, 0), false, false);
    m_view.BeginLocation(2);
    EXPECT_TRUE(m_view.Items().empty());
    EXPECT_EQ(m_view.SelectionState().SelectedCount(), 0u);
    m_view.AppendItems(1, Batch({L"late"}));
    EXPECT_TRUE(m_view.Items().empty()) << "a batch for the previous folder";
    m_view.AppendItems(2, Batch({L"new"}));
    EXPECT_EQ(Names(m_view), (std::vector<std::wstring>{L"new"}));
}

TEST_F(FileViewTest, KeysAreUniqueAndIconsFollowTheirItem)
{
    m_view.AppendItems(1, Batch({L"c", L"a", L"b"}));
    std::size_t keyOfB = 0;
    for (const te::FileItem& item : m_view.Items())
    {
        EXPECT_NE(item.key, 0u);
        if (item.info.name == L"b")
        {
            keyOfB = item.key;
        }
    }
    m_view.SetSort({te::SortField::Name, te::SortDirection::Descending}); // c b a
    m_view.SetIcon(1, keyOfB, nullptr); // a null bitmap marks the icon failed
    for (const te::FileItem& item : m_view.Items())
    {
        EXPECT_EQ(item.icon.state == te::IconSlot::State::Failed, item.info.name == L"b");
    }
    m_view.SetIcon(99, keyOfB, nullptr); // stale generation: ignored, no crash
}

TEST_F(FileViewTest, RenameResortsAndKeepsTheSelection)
{
    m_view.AppendItems(1, Batch({L"a", L"b", L"c"}));
    m_view.OnPointerDown(RowPoint(m_view, 0), false, false); // "a"
    const std::size_t key = m_view.Items()[0].key;
    m_view.ApplyRename(key, L"z");
    EXPECT_EQ(Names(m_view), (std::vector<std::wstring>{L"b", L"c", L"z"}));
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"z"}));
}

TEST_F(FileViewTest, IconRequestsCoverVisibleRowsOnce)
{
    std::vector<te::FileItem> items;
    for (int i = 0; i < 50; ++i)
    {
        items.push_back(Item((L"f" + std::to_wstring(1000 + i)).c_str()));
    }
    m_view.AppendItems(1, std::move(items));
    std::vector<std::wstring> order;
    m_view.ForEachVisibleWithoutIcon([&](const te::FileItem& item) { order.push_back(item.info.name); });
    // T087: the 10 rows that fit and one screen ahead; the look-ahead first and the visible
    // rows last, bottom to top, so a LIFO worker serves the top row first.
    ASSERT_EQ(order.size(), 20u);
    EXPECT_EQ(order.front(), L"f1019") << "the end of the look-ahead first";
    EXPECT_EQ(order[9], L"f1010");
    EXPECT_EQ(order[10], L"f1009") << "then the visible rows, bottom to top";
    EXPECT_EQ(order.back(), L"f1000") << "the top row last: served first";

    std::size_t again = 0;
    m_view.ForEachVisibleWithoutIcon([&](const te::FileItem&) { ++again; });
    EXPECT_EQ(again, 0u) << "already pending";
    m_view.OnKeyDown(VK_END, false, false);
    std::size_t atEnd = 0;
    m_view.ForEachVisibleWithoutIcon([&](const te::FileItem&) { ++atEnd; });
    EXPECT_EQ(atEnd, 10u) << "the last 10 rows; nothing ahead of them";
}

TEST_F(FileViewTest, AnIconRequestedAgainIsExtractedAgain)
{
    // T087: the worker skipped a shared icon that the UI no longer has.
    m_view.AppendItems(1, Batch({L"a", L"b"}));
    m_view.ForEachVisibleWithoutIcon([](const te::FileItem&) {});
    const std::size_t key = m_view.Items()[1].key;
    m_view.RequestIconAgain(99, key); // another icon generation: ignored
    EXPECT_EQ(m_view.Items()[1].icon.state, te::IconSlot::State::Pending);
    m_view.RequestIconAgain(m_view.IconGeneration(), key);
    EXPECT_EQ(m_view.Items()[1].icon.state, te::IconSlot::State::NotRequested);
    EXPECT_TRUE(m_view.Items()[1].icon.forceExtract);
    std::vector<bool> forced;
    m_view.ForEachVisibleWithoutIcon(
        [&](const te::FileItem& item) { forced.push_back(item.icon.forceExtract); });
    ASSERT_EQ(forced.size(), 1u);
    EXPECT_TRUE(forced[0]);
    m_view.SetIcon(m_view.IconGeneration(), key, nullptr);
    EXPECT_FALSE(m_view.Items()[1].icon.forceExtract) << "cleared once the icon arrived";

    // After a device loss every icon is dropped and forced.
    m_view.ResetIcons(5, false);
    for (const te::FileItem& item : m_view.Items())
    {
        EXPECT_FALSE(item.icon.bitmap);
        EXPECT_TRUE(item.icon.forceExtract);
    }
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

TEST_F(FileViewTest, HeaderClickSortsAndSecondClickReverses)
{
    std::vector<te::FileItem> sized;
    sized.push_back(Item(L"big", false, 300));
    sized.push_back(Item(L"small", false, 1));
    sized.push_back(Item(L"mid", false, 20));
    m_view.AppendItems(1, std::move(sized));
    const float sizeX = m_view.ColumnWidth(te::FileView::Column::Name) +
                        m_view.ColumnWidth(te::FileView::Column::DateModified) +
                        m_view.ColumnWidth(te::FileView::Column::Type) + 20.0f;
    m_view.OnPointerDown(D2D1::Point2F(sizeX, 14.0f), false, false);
    EXPECT_EQ(m_view.Sort().field, te::SortField::Size);
    EXPECT_EQ(Names(m_view), (std::vector<std::wstring>{L"small", L"mid", L"big"}));
    m_view.OnPointerDown(D2D1::Point2F(sizeX, 14.0f), false, false);
    EXPECT_EQ(m_view.Sort().direction, te::SortDirection::Descending);
    EXPECT_EQ(Names(m_view), (std::vector<std::wstring>{L"big", L"mid", L"small"}));
}

TEST_F(FileViewTest, CtrlShiftDigitsSortByColumn)
{
    m_view.AppendItems(1, Batch({L"b", L"a"}));
    EXPECT_TRUE(m_view.OnKeyDown('3', true, true));
    EXPECT_EQ(m_view.Sort().field, te::SortField::Type);
    EXPECT_TRUE(m_view.OnKeyDown('3', true, true));
    EXPECT_EQ(m_view.Sort().direction, te::SortDirection::Descending);
    EXPECT_TRUE(m_view.OnKeyDown('1', true, true));
    EXPECT_EQ(m_view.Sort().field, te::SortField::Name);
    EXPECT_EQ(m_view.Sort().direction, te::SortDirection::Ascending);
}

TEST_F(FileViewTest, DividerDragResizesWithAMinimum)
{
    const float nameWidth = m_view.ColumnWidth(te::FileView::Column::Name);
    EXPECT_TRUE(m_view.OnPointerDown(D2D1::Point2F(nameWidth, 14.0f), false, false));
    EXPECT_TRUE(m_view.Dragging());
    m_view.OnPointerMove(D2D1::Point2F(nameWidth + 50.0f, 14.0f));
    EXPECT_FLOAT_EQ(m_view.ColumnWidth(te::FileView::Column::Name), nameWidth + 50.0f);
    m_view.OnPointerMove(D2D1::Point2F(-500.0f, 14.0f));
    EXPECT_FLOAT_EQ(m_view.ColumnWidth(te::FileView::Column::Name), te::FileView::kMinColumnWidthDip);
    m_view.OnPointerUp(D2D1::Point2F(0, 0));
    EXPECT_FALSE(m_view.Dragging());
    EXPECT_EQ(m_view.Sort().field, te::SortField::Name) << "a divider drag does not sort";
}

// ---------------------------------------------------------------------------
// Mouse and keyboard
// ---------------------------------------------------------------------------

TEST_F(FileViewTest, ClickCtrlClickShiftClick)
{
    m_view.AppendItems(1, Batch({L"a", L"b", L"c", L"d", L"e"}));
    m_view.OnPointerDown(RowPoint(m_view, 1), false, false);
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"b"}));
    m_view.OnPointerDown(RowPoint(m_view, 3), true, false);
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"b", L"d"}));
    m_view.OnPointerDown(RowPoint(m_view, 4), false, true); // range from the anchor (d)
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"d", L"e"}));
    EXPECT_EQ(m_selected, 2u);
    EXPECT_TRUE(m_view.Focused());

    m_view.OnPointerDown(D2D1::Point2F(100.0f, kBounds.bottom - 5.0f), false, false); // empty space
    EXPECT_TRUE(SelectedNames().empty());
}

TEST_F(FileViewTest, DoubleClickAndEnterOpen)
{
    m_view.AppendItems(1, Batch({L"a", L"b"}));
    EXPECT_TRUE(m_view.OnDoubleClick(RowPoint(m_view, 1)));
    EXPECT_EQ(m_opened, (std::vector<std::wstring>{L"b"}));
    m_view.OnKeyDown(VK_UP, false, false);
    EXPECT_TRUE(m_view.OnKeyDown(VK_RETURN, false, false));
    EXPECT_EQ(m_opened, (std::vector<std::wstring>{L"b", L"a"}));
    EXPECT_FALSE(m_view.OnDoubleClick(D2D1::Point2F(100.0f, 14.0f))) << "header";
}

TEST_F(FileViewTest, ArrowsHomeEndAndPages)
{
    std::vector<te::FileItem> items;
    for (int i = 0; i < 40; ++i)
    {
        items.push_back(Item((L"f" + std::to_wstring(100 + i)).c_str()));
    }
    m_view.AppendItems(1, std::move(items));

    m_view.OnKeyDown(VK_DOWN, false, false);
    EXPECT_EQ(m_view.SelectionState().FocusIndex(), 0u) << "first press lands on the first row";
    m_view.OnKeyDown(VK_DOWN, false, false);
    EXPECT_EQ(m_view.SelectionState().FocusIndex(), 1u);
    m_view.OnKeyDown(VK_NEXT, false, false);
    EXPECT_EQ(m_view.SelectionState().FocusIndex(), 11u) << "10 rows per page";
    m_view.OnKeyDown(VK_END, false, false);
    EXPECT_EQ(m_view.SelectionState().FocusIndex(), 39u);
    EXPECT_FLOAT_EQ(m_view.ScrollOffset(), 30 * te::FileView::kRowHeightDip) << "scrolled into view";
    m_view.OnKeyDown(VK_HOME, false, false);
    EXPECT_EQ(m_view.SelectionState().FocusIndex(), 0u);
    EXPECT_FLOAT_EQ(m_view.ScrollOffset(), 0.0f);
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"f100"}));
}

TEST_F(FileViewTest, ShiftAndCtrlVariants)
{
    m_view.AppendItems(1, Batch({L"a", L"b", L"c", L"d"}));
    m_view.OnKeyDown(VK_HOME, false, false);
    m_view.OnKeyDown(VK_DOWN, false, true);
    m_view.OnKeyDown(VK_DOWN, false, true);
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"a", L"b", L"c"}));
    m_view.OnKeyDown(VK_DOWN, true, false); // Ctrl+Down: focus only
    EXPECT_EQ(m_view.SelectionState().FocusIndex(), 3u);
    EXPECT_EQ(SelectedNames().size(), 3u);
    m_view.OnKeyDown(VK_SPACE, true, false); // Ctrl+Space toggles the focused row
    EXPECT_EQ(SelectedNames().size(), 4u);
    m_view.OnKeyDown(VK_SPACE, true, false);
    EXPECT_EQ(SelectedNames().size(), 3u);
    m_view.OnKeyDown('A', true, false);
    EXPECT_EQ(m_selected, 4u);
}

TEST_F(FileViewTest, WheelScrollsThreeRowsAndClamps)
{
    std::vector<te::FileItem> items;
    for (int i = 0; i < 20; ++i)
    {
        items.push_back(Item((L"f" + std::to_wstring(100 + i)).c_str()));
    }
    m_view.AppendItems(1, std::move(items));
    m_view.OnWheel(-WHEEL_DELTA);
    EXPECT_FLOAT_EQ(m_view.ScrollOffset(), 3 * te::FileView::kRowHeightDip);
    m_view.OnWheel(-WHEEL_DELTA * 10);
    EXPECT_FLOAT_EQ(m_view.ScrollOffset(), 10 * te::FileView::kRowHeightDip) << "20 rows, 10 visible";
    m_view.OnWheel(WHEEL_DELTA * 10);
    EXPECT_FLOAT_EQ(m_view.ScrollOffset(), 0.0f);
    EXPECT_EQ(m_view.RowAt(RowPoint(m_view, 0)), 0u);
}

TEST_F(FileViewTest, ScrollbarThumbDrag)
{
    std::vector<te::FileItem> items;
    for (int i = 0; i < 40; ++i)
    {
        items.push_back(Item((L"f" + std::to_wstring(100 + i)).c_str()));
    }
    m_view.AppendItems(1, std::move(items));
    const float x = kBounds.right - te::FileView::kScrollbarWidthDip / 2.0f;
    ASSERT_TRUE(m_view.OnPointerDown(D2D1::Point2F(x, 28.0f + 10.0f), false, false));
    EXPECT_TRUE(m_view.Dragging());
    m_view.OnPointerMove(D2D1::Point2F(x, kBounds.bottom + 500.0f));
    EXPECT_FLOAT_EQ(m_view.ScrollOffset(), 30 * te::FileView::kRowHeightDip) << "dragged to the end";
    m_view.OnPointerUp(D2D1::Point2F(x, 0));
    EXPECT_TRUE(m_view.SelectionState().SelectedIndices().empty()) << "the scrollbar does not select";
}

TEST_F(FileViewTest, KeysOnAnEmptyListAreNotHandled)
{
    EXPECT_FALSE(m_view.OnKeyDown(VK_DOWN, false, false));
    EXPECT_FALSE(m_view.OnKeyDown(VK_RETURN, false, false));
}

// ---------------------------------------------------------------------------
// Rendering and icon conversion
// ---------------------------------------------------------------------------

TEST(FileViewRender, DrawsRowsAndConvertsIcons)
{
    const bool uninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    wil::com_ptr<ID3D11Device> d3d;
    ASSERT_HRESULT_SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                               D3D11_SDK_VERSION, &d3d, nullptr, nullptr));
    wil::com_ptr<ID2D1Factory1> factory;
    ASSERT_HRESULT_SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.addressof()));
    wil::com_ptr<ID2D1Device> device;
    ASSERT_HRESULT_SUCCEEDED(factory->CreateDevice(d3d.query<IDXGIDevice>().get(), &device));
    wil::com_ptr<ID2D1DeviceContext> dc;
    ASSERT_HRESULT_SUCCEEDED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc));
    constexpr UINT width = 800, height = 28 + 10 * 28;
    const D2D1_PIXEL_FORMAT format{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    wil::com_ptr<ID2D1Bitmap1> target, readback;
    ASSERT_HRESULT_SUCCEEDED(dc->CreateBitmap(
        {width, height}, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target));
    ASSERT_HRESULT_SUCCEEDED(dc->CreateBitmap(
        {width, height}, nullptr, 0,
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
        &readback));
    dc->SetTarget(target.get());
    wil::com_ptr<IDWriteFactory3> dwrite;
    ASSERT_HRESULT_SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                                 reinterpret_cast<IUnknown**>(dwrite.put())));
    te::TextFormats formats(dwrite.get());
    ASSERT_HRESULT_SUCCEEDED(formats.Rebuild(1.0f));

    // A 16x16 opaque red HBITMAP becomes a Direct2D icon.
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 16, -16, 1, 32, BI_RGB};
    void* bits = nullptr;
    wil::unique_hbitmap hbitmap(CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    ASSERT_TRUE(hbitmap);
    for (int i = 0; i < 16 * 16; ++i)
    {
        static_cast<std::uint32_t*>(bits)[i] = 0xFFFF0000; // premultiplied opaque red
    }
    wil::com_ptr<ID2D1Bitmap1> icon;
    ASSERT_HRESULT_SUCCEEDED(te::FileView::IconFromHBitmap(dc.get(), hbitmap.get(), &icon));
    EXPECT_EQ(icon->GetPixelSize().width, 16u);

    te::FileView view;
    view.SetTextFormats(&formats);
    view.SetBounds(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)));
    view.BeginLocation(1);
    view.AppendItems(1, Batch({L"alpha.txt", L"beta.txt"}));
    view.SetIcon(1, view.Items()[0].key, icon);
    view.OnPointerDown(RowPoint(view, 1), false, false);

    te::EffectiveAppearance e;
    e.applied = te::BackdropMode::Solid;
    e.base = {0xF3, 0xF3, 0xF3};
    e.text = {0, 0, 0};
    e.secondaryText = {0x40, 0x40, 0x40};
    e.selection = {0x00, 0x78, 0xD4};
    e.selectionAlpha = 0.4f;
    e.selectedText = {0, 0, 0};
    e.focus = {0x00, 0x5A, 0x9E};
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    view.Render(dc.get(), e);
    ASSERT_HRESULT_SUCCEEDED(dc->EndDraw());
    const D2D1_POINT_2U origin{0, 0};
    const D2D1_RECT_U all{0, 0, width, height};
    ASSERT_HRESULT_SUCCEEDED(readback->CopyFromBitmap(&origin, target.get(), &all));

    D2D1_MAPPED_RECT mapped{};
    ASSERT_HRESULT_SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
    const auto pixel = [&](UINT x, UINT y) {
        const std::uint8_t* p = mapped.bits + static_cast<size_t>(y) * mapped.pitch + x * 4;
        return std::array<int, 4>{p[2], p[1], p[0], p[3]}; // r g b a
    };
    // The icon in row 0 (red), the selection fill in row 1 (blue-ish), ink for the names.
    const D2D1_RECT_F row0 = view.RowRect(0);
    const auto iconPixel = pixel(static_cast<UINT>(te::FileView::kCellPaddingDip + 8),
                                 static_cast<UINT>((row0.top + row0.bottom) / 2));
    const D2D1_RECT_F row1 = view.RowRect(1);
    const auto selectedPixel = pixel(700, static_cast<UINT>((row1.top + row1.bottom) / 2));
    int ink = 0;
    for (UINT x = 40; x < 200; ++x)
    {
        ink += pixel(x, static_cast<UINT>((row0.top + row0.bottom) / 2))[3] > 0 ? 1 : 0;
    }
    ASSERT_HRESULT_SUCCEEDED(readback->Unmap());

    EXPECT_GT(iconPixel[0], 200);
    EXPECT_LT(iconPixel[2], 60);
    EXPECT_GT(selectedPixel[2], selectedPixel[0]) << "selection fill tints the row blue";
    EXPECT_GT(selectedPixel[3], 0);
    EXPECT_GT(ink, 0) << "the name is drawn";

    if (uninit)
    {
        CoUninitialize();
    }
}

} // namespace

namespace
{

// Manual visual check (not run by CTest): renders a sample list over a light base to
// %TEMP%\te-file-view.bmp. Run with --gtest_also_run_disabled_tests.
TEST(FileViewRender, DISABLED_CaptureSample)
{
    wil::com_ptr<ID3D11Device> d3d;
    ASSERT_HRESULT_SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                               D3D11_SDK_VERSION, &d3d, nullptr, nullptr));
    wil::com_ptr<ID2D1Factory1> factory;
    ASSERT_HRESULT_SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.addressof()));
    wil::com_ptr<ID2D1Device> device;
    ASSERT_HRESULT_SUCCEEDED(factory->CreateDevice(d3d.query<IDXGIDevice>().get(), &device));
    wil::com_ptr<ID2D1DeviceContext> dc;
    ASSERT_HRESULT_SUCCEEDED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc));
    constexpr UINT width = 800, height = 28 + 8 * 28;
    const D2D1_PIXEL_FORMAT format{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    wil::com_ptr<ID2D1Bitmap1> target, readback;
    ASSERT_HRESULT_SUCCEEDED(dc->CreateBitmap(
        {width, height}, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target));
    ASSERT_HRESULT_SUCCEEDED(dc->CreateBitmap(
        {width, height}, nullptr, 0,
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
        &readback));
    dc->SetTarget(target.get());
    wil::com_ptr<IDWriteFactory3> dwrite;
    ASSERT_HRESULT_SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                                 reinterpret_cast<IUnknown**>(dwrite.put())));
    te::TextFormats formats(dwrite.get());
    ASSERT_HRESULT_SUCCEEDED(formats.Rebuild(1.0f));

    te::FileView view;
    view.SetTextFormats(&formats);
    view.SetBounds(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)));
    view.BeginLocation(1);
    std::vector<te::FileItem> items;
    items.push_back(Item(L"Documents", true));
    items.push_back(Item(L"Pictures", true));
    const wchar_t* names[] = {L"budget-2024.xlsx",
                              L"notes.txt",
                              L"a very long file name that will not fit in the name column.pdf",
                              L"photo10.jpg",
                              L"photo2.jpg",
                              L"setup.exe",
                              L"readme.md",
                              L"archive.zip",
                              L"z.bin"};
    std::uint64_t size = 900;
    for (const wchar_t* name : names)
    {
        te::FileItem item = Item(name, false, size);
        size *= 7;
        SYSTEMTIME st{2024, 3, 5, 15, 12, 34, 0, 0};
        FILETIME ft{};
        SystemTimeToFileTime(&st, &ft);
        item.info.modified = ft;
        item.info.typeText = L"File";
        items.push_back(std::move(item));
    }
    view.AppendItems(1, std::move(items));
    view.OnPointerDown(RowPoint(view, 3), false, false);
    view.OnPointerDown(RowPoint(view, 5), true, false);

    te::EffectiveAppearance e;
    e.applied = te::BackdropMode::Solid;
    e.base = {0xF3, 0xF3, 0xF3};
    e.text = {0, 0, 0};
    e.secondaryText = {0x40, 0x40, 0x40};
    e.selection = {0x00, 0x78, 0xD4};
    e.selectionAlpha = 0.4f;
    e.selectedText = {0, 0, 0};
    e.focus = {0x00, 0x5A, 0x9E};
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0xF3F3F3));
    view.Render(dc.get(), e);
    ASSERT_HRESULT_SUCCEEDED(dc->EndDraw());
    const D2D1_POINT_2U origin{0, 0};
    const D2D1_RECT_U all{0, 0, width, height};
    ASSERT_HRESULT_SUCCEEDED(readback->CopyFromBitmap(&origin, target.get(), &all));
    D2D1_MAPPED_RECT mapped{};
    ASSERT_HRESULT_SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
    BITMAPINFOHEADER header{sizeof(header), static_cast<LONG>(width), -static_cast<LONG>(height), 1, 32,
                            BI_RGB};
    const DWORD imageSize = width * height * 4;
    BITMAPFILEHEADER file{0x4D42, sizeof(BITMAPFILEHEADER) + sizeof(header) + imageSize, 0, 0,
                          sizeof(BITMAPFILEHEADER) + sizeof(header)};
    wchar_t path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"te-file-view.bmp");
    const HANDLE out = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    WriteFile(out, &file, sizeof(file), &written, nullptr);
    WriteFile(out, &header, sizeof(header), &written, nullptr);
    for (UINT y = 0; y < height; ++y)
    {
        WriteFile(out, mapped.bits + static_cast<size_t>(y) * mapped.pitch, width * 4, &written, nullptr);
    }
    CloseHandle(out);
    ASSERT_HRESULT_SUCCEEDED(readback->Unmap());
}

TEST_F(FileViewTest, ResetIconsRequestsEveryIconAgainUnderANewGeneration)
{
    // A DPI change (T081): the listing, its generation and the selection stay; icons move
    // to a new generation and every visible row asks for its icon again.
    m_view.AppendItems(1, Batch({L"a", L"b", L"c"}));
    m_view.OnPointerDown(RowPoint(m_view, 1), false, false);
    int requested = 0;
    m_view.ForEachVisibleWithoutIcon([&](const te::FileItem& item) {
        ++requested;
        m_view.SetIcon(m_view.IconGeneration(), item.key, nullptr);
    });
    ASSERT_EQ(requested, 3);
    EXPECT_EQ(m_view.IconGeneration(), 1u) << "BeginLocation starts icons at the listing's generation";

    m_view.ResetIcons(7);
    EXPECT_EQ(m_view.CurrentGeneration(), 1u) << "the listing keeps its generation";
    EXPECT_EQ(m_view.IconGeneration(), 7u);
    EXPECT_EQ(m_view.Items().size(), 3u);
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"b"}));
    for (const te::FileItem& item : m_view.Items())
    {
        EXPECT_EQ(item.icon.state, te::IconSlot::State::NotRequested);
    }

    const std::size_t key = m_view.Items()[0].key;
    m_view.SetIcon(1, key, nullptr); // a result requested at the old size
    EXPECT_EQ(m_view.Items()[0].icon.state, te::IconSlot::State::NotRequested)
        << "old icon generation: ignored";
    requested = 0;
    m_view.ForEachVisibleWithoutIcon([&](const te::FileItem&) { ++requested; });
    EXPECT_EQ(requested, 3) << "requested again at the new size";
    m_view.SetIcon(7, key, nullptr);
    EXPECT_EQ(m_view.Items()[0].icon.state, te::IconSlot::State::Failed);

    m_view.BeginLocation(9);
    EXPECT_EQ(m_view.IconGeneration(), 9u);
}

TEST_F(FileViewTest, TextScaleGrowsTheRowsAndKeepsTheTopRow)
{
    // The Windows text size (T082): rows and the header grow with the text; the row at the
    // top of the view stays there.
    std::vector<te::FileItem> items;
    for (int i = 0; i < 50; ++i)
    {
        items.push_back(Item((L"f" + std::to_wstring(100 + i)).c_str()));
    }
    m_view.AppendItems(1, std::move(items));
    m_view.SetScrollOffset(20 * te::FileView::kRowHeightDip);
    ASSERT_FLOAT_EQ(m_view.ScrollOffset(), 20 * te::FileView::kRowHeightDip);

    m_view.SetTextScale(1.5f);
    EXPECT_FLOAT_EQ(m_view.RowHeight(), te::FileView::kRowHeightDip * 1.5f);
    EXPECT_FLOAT_EQ(m_view.HeaderHeight(), te::FileView::kHeaderHeightDip * 1.5f);
    EXPECT_FLOAT_EQ(m_view.ScrollOffset(), 20 * m_view.RowHeight()) << "row 20 is still the top row";
    const D2D1_RECT_F row = m_view.RowRect(20);
    EXPECT_FLOAT_EQ(row.top, kBounds.top + m_view.HeaderHeight());
    EXPECT_FLOAT_EQ(row.bottom - row.top, m_view.RowHeight());
    EXPECT_EQ(m_view.RowAt(D2D1::Point2F(100.0f, row.top + 1.0f)), 20u);

    m_view.SetTextScale(0.5f); // below the Windows range: treated as 100 %
    EXPECT_FLOAT_EQ(m_view.RowHeight(), te::FileView::kRowHeightDip);
    EXPECT_FLOAT_EQ(m_view.ScrollOffset(), 20 * te::FileView::kRowHeightDip);
}

TEST_F(FileViewTest, MergedBatchesKeepTheSelectionAndFocusOnTheirItems)
{
    // T086: each batch is merged in; rows that land before the selected one move it down,
    // and the selection and focus follow the item, not the row number.
    m_view.AppendItems(1, Batch({L"m", L"n"}));
    m_view.OnPointerDown(RowPoint(m_view, 1), false, false); // "n"
    ASSERT_EQ(SelectedNames(), (std::vector<std::wstring>{L"n"}));
    m_view.AppendItems(1, Batch({L"z", L"a", L"c"}));
    EXPECT_EQ(Names(m_view), (std::vector<std::wstring>{L"a", L"c", L"m", L"n", L"z"}));
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"n"}));
    ASSERT_TRUE(m_view.SelectionState().FocusIndex());
    EXPECT_EQ(m_view.Items()[*m_view.SelectionState().FocusIndex()].info.name, L"n");
    m_view.AppendItems(1, Batch({L"b", L"o"}));
    EXPECT_EQ(Names(m_view), (std::vector<std::wstring>{L"a", L"b", L"c", L"m", L"n", L"o", L"z"}));
    EXPECT_EQ(SelectedNames(), (std::vector<std::wstring>{L"n"}));
    // The key index follows the merge: every key finds its own row.
    for (std::size_t i = 0; i < m_view.Items().size(); ++i)
    {
        EXPECT_EQ(m_view.IndexOfKey(m_view.Items()[i].key), i);
    }
}

// T086: text layouts are built once for the rows on screen and reused frame after frame;
// scrolling builds only the rows that came into view, and a column resize, a new size or a
// new text scale builds them again.
TEST(FileViewRender, TextLayoutsAreCachedForTheVisibleRows)
{
    wil::com_ptr<ID3D11Device> d3d;
    ASSERT_HRESULT_SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                               D3D11_SDK_VERSION, &d3d, nullptr, nullptr));
    wil::com_ptr<ID2D1Factory1> factory;
    ASSERT_HRESULT_SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.addressof()));
    wil::com_ptr<ID2D1Device> device;
    ASSERT_HRESULT_SUCCEEDED(factory->CreateDevice(d3d.query<IDXGIDevice>().get(), &device));
    wil::com_ptr<ID2D1DeviceContext> dc;
    ASSERT_HRESULT_SUCCEEDED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc));
    const D2D1_PIXEL_FORMAT format{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    wil::com_ptr<ID2D1Bitmap1> target;
    ASSERT_HRESULT_SUCCEEDED(dc->CreateBitmap(
        {800, 400}, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target));
    dc->SetTarget(target.get());
    wil::com_ptr<IDWriteFactory3> dwrite;
    ASSERT_HRESULT_SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                                 reinterpret_cast<IUnknown**>(dwrite.put())));
    te::TextFormats formats(dwrite.get());
    ASSERT_HRESULT_SUCCEEDED(formats.Rebuild(1.0f));

    te::FileView view;
    view.SetTextFormats(&formats);
    view.SetBounds(kBounds); // header + 10 rows
    view.BeginLocation(1);
    std::vector<te::FileItem> items;
    for (int i = 0; i < 1000; ++i)
    {
        items.push_back(Item((L"f" + std::to_wstring(1000 + i)).c_str()));
    }
    view.AppendItems(1, std::move(items));

    te::EffectiveAppearance e;
    e.applied = te::BackdropMode::Solid;
    e.text = {0, 0, 0};
    const auto frame = [&] {
        dc->BeginDraw();
        view.Render(dc.get(), e);
        EXPECT_HRESULT_SUCCEEDED(dc->EndDraw());
    };

    frame();
    const std::uint64_t first = view.CreatedTextLayouts();
    // Only the rows on screen, 4 cells each (10 rows, plus the one partly below).
    EXPECT_LE(view.CachedRowLayouts(), 11u);
    EXPECT_GE(view.CachedRowLayouts(), 10u);
    EXPECT_EQ(first, 4 * view.CachedRowLayouts());

    frame();
    EXPECT_EQ(view.CreatedTextLayouts(), first) << "a repaint reuses every layout";

    view.SetScrollOffset(te::FileView::kRowHeightDip); // one row down
    frame();
    EXPECT_EQ(view.CreatedTextLayouts(), first + 4) << "only the row that came into view";
    EXPECT_LE(view.CachedRowLayouts(), 11u) << "the row that left is dropped";

    view.SetScrollOffset(500 * te::FileView::kRowHeightDip); // far away
    frame();
    const std::uint64_t afterJump = view.CreatedTextLayouts();
    EXPECT_EQ(afterJump, first + 4 + 4 * view.CachedRowLayouts());

    // A column resize (divider drag) builds the visible rows again.
    const D2D1_RECT_F header = view.HeaderCellRect(te::FileView::Column::Name);
    view.OnPointerDown(D2D1::Point2F(header.right - 1.0f, (header.top + header.bottom) / 2.0f), false, false);
    view.OnPointerMove(D2D1::Point2F(header.right + 40.0f, (header.top + header.bottom) / 2.0f));
    view.OnPointerUp(D2D1::Point2F(header.right + 40.0f, (header.top + header.bottom) / 2.0f));
    frame();
    EXPECT_EQ(view.CreatedTextLayouts(), afterJump + 4 * view.CachedRowLayouts()) << "after a column resize";

    const std::uint64_t beforeResize = view.CreatedTextLayouts();
    view.SetBounds(D2D1::RectF(0.0f, 0.0f, 700.0f, kBounds.bottom));
    frame();
    EXPECT_EQ(view.CreatedTextLayouts(), beforeResize + 4 * view.CachedRowLayouts()) << "after a resize";

    const std::uint64_t beforeScale = view.CreatedTextLayouts();
    view.SetTextScale(1.5f);
    frame();
    EXPECT_EQ(view.CreatedTextLayouts(), beforeScale + 4 * view.CachedRowLayouts())
        << "after a text scale change";

    const std::uint64_t beforeDpi = view.CreatedTextLayouts();
    view.SetDpi(192);
    frame();
    EXPECT_EQ(view.CreatedTextLayouts(), beforeDpi + 4 * view.CachedRowLayouts()) << "after a DPI change";
}

} // namespace
