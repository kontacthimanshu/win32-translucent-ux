// AddressBar (T061; research R-02, UI contract §4, §6) with a real parent window and the
// layered, colour-keyed EDIT: display text, entering and leaving edit mode, Enter, Escape
// and focus loss, the inline error, and the colour key and WM_CTLCOLOREDIT handling.

#include <te/ui/AddressBar.h>

#include <shlobj.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

std::wstring EditText(HWND edit)
{
    wchar_t text[512]{};
    GetWindowTextW(edit, text, 512);
    return text;
}

class AddressBarTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize =
            SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
        m_parent = CreateWindowExW(0, L"STATIC", L"parent", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 100, 100,
                                   900, 300, nullptr, nullptr, nullptr, nullptr);
        ASSERT_NE(m_parent, nullptr);
        // CTest starts tests hidden (STARTF_USESHOWWINDOW + SW_HIDE), and Windows applies
        // that to the first ShowWindow of the process; the second call takes effect.
        ShowWindow(m_parent, SW_SHOWNORMAL);
        ShowWindow(m_parent, SW_SHOWNORMAL);
        SetForegroundWindow(m_parent);
        m_bar.Attach(m_parent, GetDpiForWindow(m_parent));
        m_bar.SetBounds(D2D1::RectF(100.0f, 0.0f, 700.0f, 40.0f));
        m_bar.SetNavigateCallback([this](std::wstring_view text) { m_navigated.emplace_back(text); });
        m_windows = Location(L"C:\\Windows");
        m_bar.SetLocation(m_windows);
    }

    void TearDown() override
    {
        if (m_parent)
        {
            DestroyWindow(m_parent);
        }
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    static te::ShellLocation Location(const wchar_t* path)
    {
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        EXPECT_HRESULT_SUCCEEDED(SHParseDisplayName(path, nullptr, wil::out_param(pidl), 0, nullptr));
        te::ShellLocation location;
        EXPECT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location));
        return location;
    }

    void Key(UINT vk) const
    {
        SendMessageW(m_bar.Edit(), WM_KEYDOWN, vk, 0);
    }

    bool m_uninitialize = false;
    HWND m_parent = nullptr;
    te::AddressBar m_bar;
    te::ShellLocation m_windows;
    std::vector<std::wstring> m_navigated;
};

TEST_F(AddressBarTest, ShowsThePathForFileSystemFoldersAndTheNameOtherwise)
{
    EXPECT_EQ(_wcsicmp(m_bar.DisplayText().c_str(), L"C:\\Windows"), 0);
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
    ASSERT_HRESULT_SUCCEEDED(
        SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, wil::out_param(pidl)));
    te::ShellLocation thisPc;
    ASSERT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &thisPc));
    m_bar.SetLocation(thisPc);
    EXPECT_EQ(m_bar.DisplayText(), thisPc.DisplayName());
    EXPECT_EQ(m_bar.DisplayText().find(L"::{"), std::wstring::npos) << "no CLSID parsing name";
}

TEST_F(AddressBarTest, BeginEditShowsALayeredEditWithEverythingSelected)
{
    m_bar.BeginEdit();
    ASSERT_TRUE(m_bar.IsEditing());
    const HWND edit = m_bar.Edit();
    ASSERT_NE(edit, nullptr);
    EXPECT_TRUE(IsWindowVisible(edit));
    EXPECT_EQ(GetParent(edit), m_parent);
    EXPECT_TRUE(GetWindowLongW(edit, GWL_EXSTYLE) & WS_EX_LAYERED);
    EXPECT_TRUE(GetWindowLongW(edit, GWL_STYLE) & ES_AUTOHSCROLL);
    EXPECT_EQ(_wcsicmp(EditText(edit).c_str(), L"C:\\Windows"), 0);
    DWORD start = 1, end = 0;
    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    EXPECT_EQ(start, 0u);
    EXPECT_EQ(end, static_cast<DWORD>(EditText(edit).size())) << "select all";

    // Positioned over the field, inside the parent.
    RECT r{};
    GetWindowRect(edit, &r);
    MapWindowPoints(nullptr, m_parent, reinterpret_cast<POINT*>(&r), 2);
    const float scale = static_cast<float>(GetDpiForWindow(m_parent)) / 96.0f;
    EXPECT_GE(r.left, static_cast<LONG>(100.0f * scale));
    EXPECT_LE(r.right, static_cast<LONG>(700.0f * scale) + 1);
    EXPECT_GT(r.bottom - r.top, 0);
}

TEST_F(AddressBarTest, ColourKeyIsTheTypicalSurfaceColour)
{
    te::EffectiveAppearance e;
    e.typicalSurfaceColor = {0x20, 0x30, 0x40};
    e.text = {0xFF, 0xFF, 0xFF};
    m_bar.ApplyAppearance(e);
    m_bar.BeginEdit();
    COLORREF key = 0;
    BYTE alpha = 0;
    DWORD flags = 0;
    ASSERT_TRUE(GetLayeredWindowAttributes(m_bar.Edit(), &key, &alpha, &flags));
    EXPECT_EQ(key, RGB(0x20, 0x30, 0x40));
    EXPECT_TRUE(flags & LWA_COLORKEY);

    // WM_CTLCOLOREDIT: key-colour background and brush, text in effective.text.
    const HDC dc = GetDC(m_bar.Edit());
    LRESULT brush = 0;
    ASSERT_TRUE(m_bar.HandleCtlColor(m_bar.Edit(), dc, &brush));
    EXPECT_NE(brush, 0);
    EXPECT_EQ(GetBkColor(dc), RGB(0x20, 0x30, 0x40));
    EXPECT_EQ(GetTextColor(dc), RGB(0xFF, 0xFF, 0xFF));
    ReleaseDC(m_bar.Edit(), dc);
    LRESULT unused = 0;
    EXPECT_FALSE(m_bar.HandleCtlColor(m_parent, nullptr, &unused)) << "other controls are not ours";

    // A new appearance re-applies the key.
    e.typicalSurfaceColor = {0xF0, 0xF0, 0xF0};
    m_bar.ApplyAppearance(e);
    ASSERT_TRUE(GetLayeredWindowAttributes(m_bar.Edit(), &key, &alpha, &flags));
    EXPECT_EQ(key, RGB(0xF0, 0xF0, 0xF0));
}

TEST_F(AddressBarTest, EnterNavigatesWithTheTypedText)
{
    m_bar.BeginEdit();
    SetWindowTextW(m_bar.Edit(), L"C:\\Windows\\System32");
    Key(VK_RETURN);
    ASSERT_EQ(m_navigated.size(), 1u);
    EXPECT_EQ(m_navigated[0], L"C:\\Windows\\System32");
    EXPECT_TRUE(m_bar.IsEditing()) << "stays open until the owner reports the result";

    m_bar.SetLocation(Location(L"C:\\Windows\\System32")); // success
    EXPECT_FALSE(m_bar.IsEditing());
    EXPECT_FALSE(IsWindowVisible(m_bar.Edit()));
}

TEST_F(AddressBarTest, EnterWithUnchangedTextJustLeavesEditMode)
{
    m_bar.BeginEdit();
    Key(VK_RETURN);
    EXPECT_TRUE(m_navigated.empty());
    EXPECT_FALSE(m_bar.IsEditing());
}

TEST_F(AddressBarTest, EscapeRestoresThePath)
{
    m_bar.BeginEdit();
    SetWindowTextW(m_bar.Edit(), L"C:\\typed");
    Key(VK_ESCAPE);
    EXPECT_FALSE(m_bar.IsEditing());
    EXPECT_TRUE(m_navigated.empty());
    EXPECT_EQ(_wcsicmp(EditText(m_bar.Edit()).c_str(), L"C:\\Windows"), 0);
    m_bar.BeginEdit();
    EXPECT_EQ(_wcsicmp(EditText(m_bar.Edit()).c_str(), L"C:\\Windows"), 0);
}

TEST_F(AddressBarTest, InvalidPathShowsTheErrorAndKeepsEditing)
{
    m_bar.BeginEdit();
    SetWindowTextW(m_bar.Edit(), L"C:\\nope");
    Key(VK_RETURN);
    m_bar.ShowError(L"Windows can't find 'C:\\nope'.");
    EXPECT_EQ(m_bar.Error(), L"Windows can't find 'C:\\nope'.");
    EXPECT_TRUE(m_bar.IsEditing());
    EXPECT_EQ(m_bar.DisplayText().find(L"nope"), std::wstring::npos) << "the current folder is kept";

    m_bar.SetLocation(m_windows); // the next successful navigation clears it
    EXPECT_TRUE(m_bar.Error().empty());
}

TEST_F(AddressBarTest, FocusLossCancelsEditing)
{
    m_bar.BeginEdit();
    SetWindowTextW(m_bar.Edit(), L"C:\\typed");
    ASSERT_EQ(GetFocus(), m_bar.Edit());
    SetFocus(m_parent);
    EXPECT_FALSE(m_bar.IsEditing());
    EXPECT_TRUE(m_navigated.empty());
}

TEST_F(AddressBarTest, ClickInTheFieldStartsEditing)
{
    EXPECT_FALSE(m_bar.OnPointerDown(D2D1::Point2F(50.0f, 20.0f))) << "left of the field";
    EXPECT_FALSE(m_bar.IsEditing());
    EXPECT_TRUE(m_bar.OnPointerDown(D2D1::Point2F(300.0f, 20.0f)));
    EXPECT_TRUE(m_bar.IsEditing());
}

TEST_F(AddressBarTest, EditWantsEnterAndEscape)
{
    m_bar.BeginEdit();
    EXPECT_TRUE(SendMessageW(m_bar.Edit(), WM_GETDLGCODE, 0, 0) & DLGC_WANTALLKEYS);
}

} // namespace
