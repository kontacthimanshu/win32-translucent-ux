// ColorPicker (T045; research R-10, UI contract §3) with the real IDD_APPEARANCE template,
// which the test executable embeds: placement, control state, live independent changes,
// disabled controls with reasons, grid keyboard navigation, Reset, dismissal and the
// swallowed click.

#include <te/appearance/AppearanceIds.h>
#include <te/appearance/ColorPicker.h>
#include <te/appearance/Palette.h>

#include <commctrl.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace
{

std::wstring Text(HWND dialog, int id)
{
    wchar_t buffer[256]{};
    GetDlgItemTextW(dialog, id, buffer, static_cast<int>(std::size(buffer)));
    return buffer;
}

class ColorPickerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);
        m_owner = CreateWindowExW(0, L"STATIC", L"owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 800,
                                  600, nullptr, nullptr, nullptr, nullptr);
        ASSERT_NE(m_owner, nullptr);
        m_picker.emplace(GetModuleHandleW(nullptr), [this](HWND dialog) { m_registered.push_back(dialog); });
        m_picker->SetChangedCallback([this](const te::AppearanceSettings& s) { m_changes.push_back(s); });

        m_settings.backdropMode = te::BackdropMode::Mica;
        m_settings.surfaceOpacity = 0.25;
        m_settings.tintOpacity = 0.20;
        m_settings.customColors[3] = {1, 2, 3};
        m_effective.requested = te::BackdropMode::Mica;
        m_effective.applied = te::BackdropMode::Mica;
        m_effective.base = {0x20, 0x20, 0x20};
        m_effective.opacityControlEnabled = true;
        m_caps.systemBackdropSupported = true;
        m_caps.accent = {0x00, 0x78, 0xD4};
        m_picker->SetCapabilities(m_caps);
    }

    void TearDown() override
    {
        m_picker.reset();
        if (m_owner)
        {
            DestroyWindow(m_owner);
        }
    }

    HWND Show()
    {
        m_anchor = {600, 130, 640, 160};
        m_picker->Show(m_owner, m_anchor, m_settings, m_effective);
        return m_picker->Dialog();
    }

    void Click(int id) const
    {
        const HWND dialog = m_picker->Dialog();
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                     reinterpret_cast<LPARAM>(GetDlgItem(dialog, id)));
    }

    void Scroll(int id, int steps) const
    {
        const HWND dialog = m_picker->Dialog();
        const HWND trackbar = GetDlgItem(dialog, id);
        SendMessageW(trackbar, TBM_SETPOS, TRUE, steps);
        SendMessageW(dialog, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, steps),
                     reinterpret_cast<LPARAM>(trackbar));
    }

    HWND m_owner = nullptr;
    std::optional<te::ColorPicker> m_picker;
    std::vector<HWND> m_registered;
    std::vector<te::AppearanceSettings> m_changes;
    te::AppearanceSettings m_settings;
    te::EffectiveAppearance m_effective;
    te::RenderingCapabilities m_caps;
    RECT m_anchor{};
};

TEST_F(ColorPickerTest, OpensUnderTheAnchorInsideTheWorkArea)
{
    const HWND dialog = Show();
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(m_picker->IsOpen());
    ASSERT_EQ(m_registered.size(), 1u);
    EXPECT_EQ(m_registered[0], dialog) << "registered for IsDialogMessageW";

    RECT r{};
    GetWindowRect(dialog, &r);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(m_owner, MONITOR_DEFAULTTONEAREST), &monitor);
    EXPECT_EQ(r.top, m_anchor.bottom);
    EXPECT_EQ(r.right, m_anchor.right);
    EXPECT_GE(r.left, monitor.rcWork.left);
    EXPECT_LE(r.bottom, monitor.rcWork.bottom);
    EXPECT_TRUE(GetWindowLongW(dialog, GWL_STYLE) & WS_BORDER);
}

TEST_F(ColorPickerTest, RepositionMovesAnOpenPopupWithTheButton)
{
    // A DPI change moves the picker button (T081): an open popup follows it.
    const HWND dialog = Show();
    ASSERT_TRUE(m_picker->IsOpen());
    const RECT moved{500, 200, 560, 245};
    m_picker->Reposition(moved);
    RECT r{};
    GetWindowRect(dialog, &r);
    EXPECT_EQ(r.right, moved.right);
    EXPECT_EQ(r.top, moved.bottom);

    // Closed: nothing moves now, and the next Show uses its own anchor.
    m_picker->Hide();
    m_picker->Reposition({100, 100, 140, 130});
    RECT closed{};
    GetWindowRect(dialog, &closed);
    EXPECT_EQ(closed.left, r.left);
    EXPECT_EQ(closed.top, r.top);
}

TEST_F(ColorPickerTest, ClampsToTheWorkAreaNearTheEdge)
{
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(m_owner, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;
    const RECT anchor{work.left, work.bottom - 10, work.left + 20, work.bottom};
    m_picker->Show(m_owner, anchor, m_settings, m_effective);

    RECT r{};
    GetWindowRect(m_picker->Dialog(), &r);
    EXPECT_GE(r.left, work.left);
    EXPECT_GE(r.top, work.top);
    EXPECT_LE(r.bottom, anchor.top) << "above the anchor when there is no room below";
}

TEST_F(ColorPickerTest, ControlsReflectTheSettings)
{
    const HWND dialog = Show();
    EXPECT_EQ(IsDlgButtonChecked(dialog, IDC_MODE_MICA), BST_CHECKED);
    EXPECT_EQ(IsDlgButtonChecked(dialog, IDC_MODE_ACRYLIC), BST_UNCHECKED);
    EXPECT_EQ(SendDlgItemMessageW(dialog, IDC_SURFACE, TBM_GETRANGEMAX, 0, 0), 18);
    EXPECT_EQ(SendDlgItemMessageW(dialog, IDC_TINT, TBM_GETRANGEMAX, 0, 0), 16);
    EXPECT_EQ(SendDlgItemMessageW(dialog, IDC_SURFACE, TBM_GETPOS, 0, 0), 5);
    EXPECT_EQ(SendDlgItemMessageW(dialog, IDC_TINT, TBM_GETPOS, 0, 0), 4);
    EXPECT_EQ(SendDlgItemMessageW(dialog, IDC_SURFACE, TBM_GETPAGESIZE, 0, 0), 2);
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(dialog, IDC_SURFACE)));
    EXPECT_EQ(Text(dialog, IDC_OPACITY_REASON), L"");
    EXPECT_EQ(Text(dialog, IDC_MODE_REASON), L"");
    for (size_t i = 0; i < te::kPresetPalette.size(); ++i)
    {
        EXPECT_EQ(Text(dialog, IDC_SWATCH_0 + static_cast<int>(i)), te::kPresetPalette[i].name)
            << "accessible name of swatch " << i;
    }
}

// Principle VII: each trackbar writes only its own field.
TEST_F(ColorPickerTest, TrackbarsChangeOnlyTheirOwnField)
{
    Show();
    Scroll(IDC_SURFACE, 10);
    ASSERT_EQ(m_changes.size(), 1u);
    EXPECT_NEAR(m_changes.back().surfaceOpacity, 0.50, 1e-9);
    EXPECT_NEAR(m_changes.back().tintOpacity, 0.20, 1e-9);

    Scroll(IDC_TINT, 16);
    ASSERT_EQ(m_changes.size(), 2u);
    EXPECT_NEAR(m_changes.back().tintOpacity, 0.80, 1e-9);
    EXPECT_NEAR(m_changes.back().surfaceOpacity, 0.50, 1e-9);

    Scroll(IDC_TINT, 16); // same position: no change reported
    EXPECT_EQ(m_changes.size(), 2u);
}

TEST_F(ColorPickerTest, SwatchesAccentAndModesApplyImmediately)
{
    Show();
    Click(IDC_SWATCH_0 + 2);
    ASSERT_FALSE(m_changes.empty());
    ASSERT_TRUE(std::holds_alternative<te::Rgb>(m_changes.back().tintColor));
    EXPECT_EQ(std::get<te::Rgb>(m_changes.back().tintColor), te::kPresetPalette[2].rgb); // Teal
    EXPECT_NEAR(m_changes.back().surfaceOpacity, 0.25, 1e-9) << "colour does not change opacity";

    Click(IDC_SWATCH_ACCENT);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(m_changes.back().tintColor));

    Click(IDC_MODE_SOLID);
    EXPECT_EQ(m_changes.back().backdropMode, te::BackdropMode::Solid);
    EXPECT_EQ(IsDlgButtonChecked(m_picker->Dialog(), IDC_MODE_SOLID), BST_CHECKED);
    EXPECT_NEAR(m_changes.back().tintOpacity, 0.20, 1e-9) << "values kept in Solid";
}

TEST_F(ColorPickerTest, TransparentSwatchSelectsClearGlassAndKeepsTheTint)
{
    const HWND dialog = Show();
    EXPECT_EQ(Text(dialog, IDC_SWATCH_TRANSPARENT), L"Transparent");
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(dialog, IDC_SWATCH_TRANSPARENT)));

    Click(IDC_SWATCH_TRANSPARENT);
    ASSERT_FALSE(m_changes.empty());
    EXPECT_EQ(m_changes.back().backdropMode, te::BackdropMode::Transparent);
    EXPECT_NEAR(m_changes.back().tintOpacity, te::kTransparentTintFloor, 1e-9) << "raised so the color shows";
    EXPECT_NEAR(m_changes.back().surfaceOpacity, 0.25, 1e-9) << "surface opacity kept";
    EXPECT_EQ(SendDlgItemMessageW(dialog, IDC_TINT, TBM_GETPOS, 0, 0), 9) << "the trackbar follows (45%)";
    for (const int id : {IDC_MODE_ACRYLIC, IDC_MODE_MICA, IDC_MODE_SOLID})
    {
        EXPECT_EQ(IsDlgButtonChecked(dialog, id), BST_UNCHECKED) << id;
    }

    // A color tints the clear glass; it does not leave Transparent.
    Click(IDC_SWATCH_0 + 2);
    EXPECT_EQ(m_changes.back().backdropMode, te::BackdropMode::Transparent);
    EXPECT_EQ(std::get<te::Rgb>(m_changes.back().tintColor), te::kPresetPalette[2].rgb);

    // A mode radio leaves it.
    Click(IDC_MODE_MICA);
    EXPECT_EQ(m_changes.back().backdropMode, te::BackdropMode::Mica);
    EXPECT_EQ(IsDlgButtonChecked(dialog, IDC_MODE_MICA), BST_CHECKED);
}

TEST_F(ColorPickerTest, AColorChoiceInTransparentMakesTheColorShow)
{
    m_settings.backdropMode = te::BackdropMode::Transparent;
    m_settings.tintOpacity = 0.20;
    const HWND dialog = Show();

    // The slider can go below the floor...
    Scroll(IDC_TINT, 2);
    EXPECT_NEAR(m_changes.back().tintOpacity, 0.10, 1e-9);
    // ...and a color choice (swatch, accent or Custom...) brings it back up.
    Click(IDC_SWATCH_0 + 7); // Red
    EXPECT_NEAR(m_changes.back().tintOpacity, te::kTransparentTintFloor, 1e-9);
    EXPECT_EQ(SendDlgItemMessageW(dialog, IDC_TINT, TBM_GETPOS, 0, 0), 9);
    Scroll(IDC_TINT, 2);
    Click(IDC_SWATCH_ACCENT);
    EXPECT_NEAR(m_changes.back().tintOpacity, te::kTransparentTintFloor, 1e-9);

    // A stronger tint is never lowered.
    Scroll(IDC_TINT, 14);
    Click(IDC_SWATCH_0);
    EXPECT_NEAR(m_changes.back().tintOpacity, 0.70, 1e-9);
}

TEST_F(ColorPickerTest, AColorChoiceOutsideTransparentKeepsTheTintStrength)
{
    m_settings.tintOpacity = 0.20; // Mica
    Show();
    Click(IDC_SWATCH_0 + 7);
    EXPECT_NEAR(m_changes.back().tintOpacity, 0.20, 1e-9);
}

TEST_F(ColorPickerTest, SolidDisablesBothOpacityControlsWithReason)
{
    m_effective.applied = te::BackdropMode::Solid;
    m_effective.opacityControlEnabled = false;
    const HWND dialog = Show();
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(dialog, IDC_SURFACE)));
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(dialog, IDC_TINT)));
    EXPECT_EQ(Text(dialog, IDC_OPACITY_REASON), L"Solid mode is fully opaque");

    // Re-resolved to a translucent mode while open: enabled again.
    m_effective.applied = te::BackdropMode::Mica;
    m_effective.opacityControlEnabled = true;
    m_picker->Update(m_settings, m_effective);
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(dialog, IDC_SURFACE)));
    EXPECT_EQ(Text(dialog, IDC_OPACITY_REASON), L"");
}

TEST_F(ColorPickerTest, UnsupportedModesAreDisabledWithTheReason)
{
    const struct
    {
        bool supported, transparency, highContrast;
        const wchar_t* reason;
    } cases[] = {
        {false, true, false, L"Requires Windows 11 build 22621 or later"},
        {true, false, false, L"Transparency effects are turned off in Windows settings"},
        {true, true, true, L"High contrast is on"},
    };
    const HWND dialog = Show();
    for (const auto& c : cases)
    {
        te::RenderingCapabilities caps = m_caps;
        caps.systemBackdropSupported = c.supported;
        caps.transparencyEffectsEnabled = c.transparency;
        caps.highContrast = c.highContrast;
        m_picker->SetCapabilities(caps);
        EXPECT_FALSE(IsWindowEnabled(GetDlgItem(dialog, IDC_MODE_ACRYLIC)));
        EXPECT_FALSE(IsWindowEnabled(GetDlgItem(dialog, IDC_MODE_MICA)));
        EXPECT_FALSE(IsWindowEnabled(GetDlgItem(dialog, IDC_SWATCH_TRANSPARENT)));
        EXPECT_TRUE(IsWindowEnabled(GetDlgItem(dialog, IDC_MODE_SOLID)));
        EXPECT_EQ(Text(dialog, IDC_MODE_REASON), c.reason);
        EXPECT_EQ(IsDlgButtonChecked(dialog, IDC_MODE_MICA), BST_CHECKED) << "the preference stays";
    }
    m_picker->SetCapabilities(m_caps);
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(dialog, IDC_MODE_MICA)));
    EXPECT_EQ(Text(dialog, IDC_MODE_REASON), L"");
}

TEST_F(ColorPickerTest, ArrowKeysMoveWithinTheFourByThreeGrid)
{
    const HWND dialog = Show();
    const auto focusOn = [&](int index) {
        SendMessageW(dialog, WM_NEXTDLGCTL,
                     reinterpret_cast<WPARAM>(GetDlgItem(dialog, IDC_SWATCH_0 + index)), TRUE);
    };
    const auto press = [&](UINT key) { SendMessageW(GetFocus(), WM_KEYDOWN, key, 0); };
    const auto focused = [&] { return GetDlgCtrlID(GetFocus()) - IDC_SWATCH_0; };

    EXPECT_TRUE(SendMessageW(GetDlgItem(dialog, IDC_SWATCH_0), WM_GETDLGCODE, 0, 0) & DLGC_WANTARROWS);
    focusOn(0);
    ASSERT_EQ(focused(), 0);
    press(VK_RIGHT);
    EXPECT_EQ(focused(), 1);
    press(VK_DOWN);
    EXPECT_EQ(focused(), 5);
    press(VK_DOWN);
    EXPECT_EQ(focused(), 9);
    press(VK_DOWN);
    EXPECT_EQ(focused(), 9) << "bottom row stays";
    press(VK_LEFT);
    press(VK_LEFT);
    EXPECT_EQ(focused(), 8);
    press(VK_UP);
    EXPECT_EQ(focused(), 4);
    focusOn(3);
    press(VK_RIGHT);
    EXPECT_EQ(focused(), 3) << "right edge stays";
    // Roving tab stop: only the focused swatch is a tab stop.
    EXPECT_TRUE(GetWindowLongW(GetDlgItem(dialog, IDC_SWATCH_0 + 4), GWL_STYLE) & WS_TABSTOP);
}

TEST_F(ColorPickerTest, ResetRestoresDefaultsButKeepsCustomColors)
{
    m_settings.backdropMode = te::BackdropMode::Acrylic;
    m_settings.tintColor = te::Rgb{9, 9, 9};
    m_settings.tintOpacity = 0.7;
    Show();
    Click(IDC_RESET);
    ASSERT_FALSE(m_changes.empty());
    const te::AppearanceSettings& reset = m_changes.back();
    EXPECT_EQ(reset.backdropMode, te::BackdropMode::Transparent);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(reset.tintColor));
    EXPECT_NEAR(reset.tintOpacity, te::kTransparentTintFloor, 1e-9);
    EXPECT_NEAR(reset.surfaceOpacity, 0.00, 1e-9);
    EXPECT_EQ(reset.customColors[3], (te::Rgb{1, 2, 3}));
    EXPECT_EQ(SendDlgItemMessageW(m_picker->Dialog(), IDC_SURFACE, TBM_GETPOS, 0, 0), 0);
    EXPECT_EQ(IsDlgButtonChecked(m_picker->Dialog(), IDC_MODE_MICA), BST_UNCHECKED) << "Transparent has no radio";
}

TEST_F(ColorPickerTest, EscapeHidesWithoutDestroyingOrChanging)
{
    const HWND dialog = Show();
    Click(IDCANCEL);
    EXPECT_FALSE(m_picker->IsOpen());
    EXPECT_TRUE(IsWindow(dialog)) << "hidden, not destroyed";
    EXPECT_TRUE(m_changes.empty()) << "dismissal has no side effects";
    ASSERT_EQ(m_registered.size(), 2u);
    EXPECT_EQ(m_registered[1], nullptr) << "unregistered from IsDialogMessageW";

    // Reopening reuses the same window.
    EXPECT_EQ(Show(), dialog);
}

TEST_F(ColorPickerTest, DeactivationDismissesAndSwallowsTheClick)
{
    const HWND dialog = Show();
    SendMessageW(dialog, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), reinterpret_cast<LPARAM>(m_owner));
    EXPECT_FALSE(m_picker->IsOpen());
    EXPECT_TRUE(m_changes.empty());

    // The click that caused it is swallowed, down and up; the next one is not.
    EXPECT_TRUE(m_picker->ShouldSwallowClick(WM_LBUTTONDOWN));
    EXPECT_TRUE(m_picker->ShouldSwallowClick(WM_LBUTTONUP));
    EXPECT_FALSE(m_picker->ShouldSwallowClick(WM_LBUTTONDOWN));
    EXPECT_FALSE(m_picker->ShouldSwallowClick(WM_LBUTTONUP));
}

TEST_F(ColorPickerTest, StaleDismissalDoesNotSwallowLaterClicks)
{
    const HWND dialog = Show();
    SendMessageW(dialog, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), 0);
    Sleep(te::ColorPicker::kSwallowWindowMs + 100);
    EXPECT_FALSE(m_picker->ShouldSwallowClick(WM_LBUTTONDOWN));
}

TEST_F(ColorPickerTest, EscapeDoesNotSwallowClicks)
{
    Show();
    Click(IDCANCEL);
    EXPECT_FALSE(m_picker->ShouldSwallowClick(WM_LBUTTONDOWN));
}

} // namespace

namespace
{

// Manual visual check (not run by CTest): renders the open popup to
// %TEMP%\te-color-picker.bmp. Run with --gtest_also_run_disabled_tests.
TEST_F(ColorPickerTest, DISABLED_CapturePopup)
{
    m_settings.tintColor = te::kPresetPalette[2].rgb; // Teal selected
    const HWND dialog = Show();
    SendMessageW(dialog, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(GetDlgItem(dialog, IDC_SWATCH_0 + 5)), TRUE);
    MSG msg{};
    for (int i = 0; i < 20; ++i)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }

    RECT r{};
    GetWindowRect(dialog, &r);
    const int width = r.right - r.left;
    const int height = r.bottom - r.top;
    // Copy what is on screen (PrintWindow does not render into a 24-bit DIB).
    SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    UpdateWindow(dialog);
    Sleep(300);
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 24, BI_RGB};
    void* bits = nullptr;
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const HGDIOBJ old = SelectObject(memory, bitmap);
    ASSERT_TRUE(BitBlt(memory, 0, 0, width, height, screen, r.left, r.top, SRCCOPY));
    GdiFlush();

    const DWORD stride = ((static_cast<DWORD>(width) * 3 + 3) & ~3u);
    const DWORD imageSize = stride * static_cast<DWORD>(height);
    BITMAPFILEHEADER file{0x4D42, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imageSize, 0, 0,
                          sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    wchar_t path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"te-color-picker.bmp");
    const HANDLE out = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    WriteFile(out, &file, sizeof(file), &written, nullptr);
    WriteFile(out, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr);
    WriteFile(out, bits, imageSize, &written, nullptr);
    CloseHandle(out);

    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

} // namespace
