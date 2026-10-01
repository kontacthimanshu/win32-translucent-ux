// NavigationPane (T063) with the real Shell: This PC and the known folders first, then
// drives; the current location is highlighted by identity; click, Enter and arrow keys;
// drive-change events; icons and a rendering smoke test.

#include <te/ui/NavigationPane.h>

#include <dbt.h>
#include <shlobj.h>

#include <d3d11.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

te::ShellLocation Known(REFKNOWNFOLDERID id)
{
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
    EXPECT_HRESULT_SUCCEEDED(SHGetKnownFolderIDList(id, 0, nullptr, wil::out_param(pidl)));
    te::ShellLocation location;
    EXPECT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location));
    return location;
}

D2D1_POINT_2F Center(const te::NavigationPane& pane, std::size_t index)
{
    const D2D1_RECT_F r = pane.EntryRect(index);
    return D2D1::Point2F((r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f);
}

class NavigationPaneTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize =
            SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
        m_pane.SetBounds(D2D1::RectF(0.0f, 100.0f, 220.0f, 700.0f));
        m_pane.SetNavigateCallback([this](const te::ShellLocation& l) { m_navigated.push_back(l); });
        m_pane.Refresh();
    }
    void TearDown() override
    {
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    bool m_uninitialize = false;
    te::NavigationPane m_pane;
    std::vector<te::ShellLocation> m_navigated;
};

TEST_F(NavigationPaneTest, ThisPcAndKnownFoldersComeFirstThenDrives)
{
    const auto& entries = m_pane.Entries();
    ASSERT_GE(entries.size(), 8u) << "This PC, 6 known folders and at least C:";
    EXPECT_TRUE(entries[0].location == Known(FOLDERID_ComputerFolder));
    EXPECT_TRUE(entries[1].location == Known(FOLDERID_Desktop));
    EXPECT_TRUE(entries[2].location == Known(FOLDERID_Documents));
    EXPECT_FALSE(entries[0].isDrive);

    bool sawDrive = false;
    bool sawSystemDrive = false;
    for (const auto& entry : entries)
    {
        if (entry.isDrive)
        {
            sawDrive = true;
            const auto& path = entry.location.ParsingPath();
            ASSERT_TRUE(path.has_value());
            EXPECT_EQ(path->size(), 3u) << "a drive root such as C:\\";
            wchar_t windows[MAX_PATH]{};
            GetWindowsDirectoryW(windows, MAX_PATH);
            sawSystemDrive = sawSystemDrive || _wcsnicmp(path->c_str(), windows, 3) == 0;
        }
        else
        {
            EXPECT_FALSE(sawDrive) << "known folders are listed before drives";
        }
    }
    EXPECT_TRUE(sawSystemDrive);
}

TEST_F(NavigationPaneTest, EveryEntryHasAnIconAndADisplayName)
{
    for (const auto& entry : m_pane.Entries())
    {
        EXPECT_FALSE(entry.location.DisplayName().empty());
        EXPECT_TRUE(entry.iconSource)
            << "Shell icon for " << ::testing::PrintToString(entry.location.DisplayName());
    }
}

TEST_F(NavigationPaneTest, CurrentLocationIsHighlightedByIdentity)
{
    m_pane.SetCurrent(Known(FOLDERID_Documents));
    EXPECT_EQ(m_pane.CurrentIndex(), 2u);
    m_pane.SetCurrent(Known(FOLDERID_Windows)); // not in the pane
    EXPECT_FALSE(m_pane.CurrentIndex().has_value());
    m_pane.SetCurrent(Known(FOLDERID_Desktop));
    m_pane.Refresh(); // re-matched after the list is rebuilt
    EXPECT_EQ(m_pane.CurrentIndex(), 1u);
}

TEST_F(NavigationPaneTest, ClickNavigates)
{
    ASSERT_TRUE(m_pane.OnPointerDown(Center(m_pane, 3)));
    ASSERT_EQ(m_navigated.size(), 1u);
    EXPECT_TRUE(m_navigated[0] == m_pane.Entries()[3].location);
    EXPECT_TRUE(m_pane.Focused());
    EXPECT_FALSE(m_pane.OnPointerDown(D2D1::Point2F(300.0f, 200.0f))) << "outside the pane";
}

TEST_F(NavigationPaneTest, ArrowsMoveAndEnterNavigates)
{
    m_pane.SetCurrent(Known(FOLDERID_ComputerFolder));
    m_pane.SetFocused(true);
    EXPECT_EQ(m_pane.FocusIndex(), 0u) << "focus starts on the current entry";
    m_pane.OnKeyDown(VK_DOWN);
    m_pane.OnKeyDown(VK_DOWN);
    EXPECT_EQ(m_pane.FocusIndex(), 2u);
    m_pane.OnKeyDown(VK_UP);
    EXPECT_EQ(m_pane.FocusIndex(), 1u);
    EXPECT_TRUE(m_navigated.empty()) << "moving does not navigate";
    EXPECT_TRUE(m_pane.OnKeyDown(VK_RETURN));
    ASSERT_EQ(m_navigated.size(), 1u);
    EXPECT_TRUE(m_navigated[0] == Known(FOLDERID_Desktop));
    m_pane.OnKeyDown(VK_END);
    EXPECT_EQ(m_pane.FocusIndex(), m_pane.Entries().size() - 1);
    m_pane.OnKeyDown(VK_HOME);
    EXPECT_EQ(m_pane.FocusIndex(), 0u);
    EXPECT_FALSE(m_pane.OnKeyDown('X'));
}

TEST_F(NavigationPaneTest, DrivesAreSeparatedFromKnownFolders)
{
    std::size_t firstDrive = 0;
    while (firstDrive < m_pane.Entries().size() && !m_pane.Entries()[firstDrive].isDrive)
    {
        ++firstDrive;
    }
    ASSERT_LT(firstDrive, m_pane.Entries().size());
    const float gap = m_pane.EntryRect(firstDrive).top - m_pane.EntryRect(firstDrive - 1).bottom;
    EXPECT_FLOAT_EQ(gap, te::NavigationPane::kGroupGapDip);
}

TEST_F(NavigationPaneTest, OnlyDriveArrivalAndRemovalRefresh)
{
    EXPECT_TRUE(m_pane.OnDeviceChange(DBT_DEVICEARRIVAL));
    EXPECT_TRUE(m_pane.OnDeviceChange(DBT_DEVICEREMOVECOMPLETE));
    EXPECT_FALSE(m_pane.OnDeviceChange(DBT_DEVNODES_CHANGED));
    EXPECT_GE(m_pane.Entries().size(), 8u);
}

TEST_F(NavigationPaneTest, WheelScrollsWhenThePaneIsShort)
{
    m_pane.SetBounds(D2D1::RectF(0.0f, 0.0f, 220.0f, 3 * te::NavigationPane::kRowHeightDip));
    const float before = m_pane.EntryRect(0).top;
    m_pane.OnWheel(-WHEEL_DELTA);
    EXPECT_LT(m_pane.EntryRect(0).top, before);
    m_pane.OnWheel(WHEEL_DELTA * 20);
    EXPECT_FLOAT_EQ(m_pane.EntryRect(0).top, before);
}

TEST_F(NavigationPaneTest, RendersIconsAndNames)
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
    constexpr UINT width = 220, height = 400;
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

    m_pane.SetTextFormats(&formats);
    m_pane.SetBounds(D2D1::RectF(0, 0, static_cast<float>(width), static_cast<float>(height)));
    m_pane.SetCurrent(Known(FOLDERID_Documents));
    te::EffectiveAppearance e;
    e.applied = te::BackdropMode::Solid;
    e.base = {0xF3, 0xF3, 0xF3};
    e.text = {0, 0, 0};
    e.selection = {0x00, 0x78, 0xD4};
    e.selectionAlpha = 0.4f;
    e.selectedText = {0, 0, 0};
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    m_pane.Render(dc.get(), e);
    ASSERT_HRESULT_SUCCEEDED(dc->EndDraw());
    for (const auto& entry : m_pane.Entries())
    {
        EXPECT_TRUE(entry.icon) << "converted for Direct2D on first render";
    }

    const D2D1_POINT_2U origin{0, 0};
    const D2D1_RECT_U all{0, 0, width, height};
    ASSERT_HRESULT_SUCCEEDED(readback->CopyFromBitmap(&origin, target.get(), &all));
    D2D1_MAPPED_RECT mapped{};
    ASSERT_HRESULT_SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
    const D2D1_RECT_F current = m_pane.EntryRect(2);
    const std::uint8_t* p =
        mapped.bits + static_cast<size_t>((current.top + current.bottom) / 2) * mapped.pitch + 200 * 4;
    const int blue = p[0], red = p[2], alpha = p[3];
    int iconInk = 0;
    const D2D1_RECT_F first = m_pane.EntryRect(0);
    for (UINT x = 12; x < 28; ++x)
    {
        iconInk +=
            mapped.bits[static_cast<size_t>((first.top + first.bottom) / 2) * mapped.pitch + x * 4 + 3] > 0;
    }
    ASSERT_HRESULT_SUCCEEDED(readback->Unmap());
    EXPECT_GT(alpha, 0);
    EXPECT_GT(blue, red) << "the current entry has the selection fill";
    EXPECT_GT(iconInk, 0) << "This PC's icon is drawn";
}

} // namespace
