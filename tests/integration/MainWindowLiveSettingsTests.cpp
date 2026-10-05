// Live OS settings (T082; the automated part of quickstart V-5e): text size, animation
// effects, light/dark mode and high contrast take effect on the running window. The user's
// settings are never changed: Options::adjustCapabilities changes what the window reads,
// and the change notification Windows would send is sent to the window directly
// (WM_TE_SETTINGS_CHANGED for the UISettings events, WM_SETTINGCHANGE for the others).

#include <te/app/MainWindow.h>
#include <te/appearance/Contrast.h>
#include <te/core/Messages.h>
#include <te/render/TextFormats.h>
#include <te/window/CustomTitleBar.h>

#include "../../resources/resource.h" // IDI_APP, embedded in the test executable
#include "ScreenCapture.h"

#include <dwmapi.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace
{

void Pump(DWORD ms)
{
    const ULONGLONG until = GetTickCount64() + ms;
    MSG msg{};
    while (GetTickCount64() < until)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}

// What the simulated settings say; unset fields keep what the OS reported.
struct Simulated
{
    std::optional<double> textScale;
    std::optional<bool> animations;
    std::optional<bool> darkMode;
    std::optional<bool> highContrast;
    int reads = 0;
};

class MainWindowLiveSettingsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Open(std::nullopt);
    }

    // `folder`: list it (the Shell starts); none: no Shell, as for the other tests.
    void Open(const std::optional<std::wstring>& folder)
    {
        if (m_window)
        {
            DestroyWindow(m_window->Hwnd());
            m_window.reset();
        }
        te::MainWindow::Options options;
        if (folder)
        {
            options.startShell = true;
            options.initialPath = *folder;
            options.iconResourceId = IDI_APP;
        }
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"live settings test";
        options.quitOnDestroy = false;
        options.adjustCapabilities = [this](te::RenderingCapabilities& caps) {
            ++m_sim.reads;
            if (m_sim.textScale)
            {
                caps.textScaleFactor = *m_sim.textScale;
            }
            if (m_sim.animations)
            {
                caps.animationsEnabled = *m_sim.animations;
            }
            if (m_sim.darkMode)
            {
                caps.darkMode = *m_sim.darkMode;
            }
            if (m_sim.highContrast)
            {
                caps.highContrast = *m_sim.highContrast;
            }
        };
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNOACTIVATE));
        Pump(100);
    }

    void TearDown() override
    {
        if (m_window)
        {
            DestroyWindow(m_window->Hwnd());
            m_window.reset();
        }
    }

    [[nodiscard]] HWND Hwnd() const
    {
        return m_window->Hwnd();
    }

    // A UISettings event (text size, colors, transparency) as the theme manager posts it.
    void UiSettingsChanged()
    {
        PostMessageW(Hwnd(), te::WM_TE_SETTINGS_CHANGED, 0, 0);
        Pump(100);
    }

    void SettingChange(WPARAM action, const wchar_t* area = nullptr)
    {
        SendMessageW(Hwnd(), WM_SETTINGCHANGE, action, reinterpret_cast<LPARAM>(area));
        Pump(50);
    }

    [[nodiscard]] LONG MinTrackHeight() const
    {
        MINMAXINFO info{};
        SendMessageW(Hwnd(), WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&info));
        return info.ptMinTrackSize.y;
    }

    static float Height(const D2D1_RECT_F& r)
    {
        return r.bottom - r.top;
    }

    Simulated m_sim;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(MainWindowLiveSettingsTest, TextSizeRebuildsTheFormatsAndGrowsTheRows)
{
    m_sim.textScale = 1.0;
    UiSettingsChanged();
    ASSERT_FLOAT_EQ(m_window->Text()->TextScale(), 1.0f);
    const float toolbar = Height(m_window->Layout().toolbar);
    const float status = Height(m_window->Layout().statusBar);
    const float field = Height(m_window->Address().FieldRect());
    const LONG minHeight = MinTrackHeight();
    IDWriteTextFormat* const body = m_window->Text()->Body();
    const float bodySize = body->GetFontSize();

    m_sim.textScale = 1.5; // Settings > Accessibility > Text size 150 %
    UiSettingsChanged();

    EXPECT_FLOAT_EQ(m_window->Text()->TextScale(), 1.5f);
    EXPECT_FLOAT_EQ(m_window->Text()->Body()->GetFontSize(), bodySize * 1.5f) << "formats rebuilt";
    EXPECT_FLOAT_EQ(m_window->Files().RowHeight(), te::FileView::kRowHeightDip * 1.5f);
    EXPECT_FLOAT_EQ(m_window->Files().HeaderHeight(), te::FileView::kHeaderHeightDip * 1.5f);
    EXPECT_FLOAT_EQ(m_window->Places().RowHeight(), te::NavigationPane::kRowHeightDip * 1.5f);
    EXPECT_FLOAT_EQ(Height(m_window->Layout().toolbar), toolbar * 1.5f);
    EXPECT_FLOAT_EQ(Height(m_window->Layout().statusBar), status * 1.5f);
    EXPECT_FLOAT_EQ(Height(m_window->Address().FieldRect()), field * 1.5f);
    EXPECT_GT(MinTrackHeight(), minHeight) << "five rows of the larger text still fit";

    m_sim.textScale = 1.0; // and back, without a restart
    UiSettingsChanged();
    EXPECT_FLOAT_EQ(m_window->Text()->Body()->GetFontSize(), bodySize);
    EXPECT_FLOAT_EQ(m_window->Files().RowHeight(), te::FileView::kRowHeightDip);
    EXPECT_FLOAT_EQ(Height(m_window->Layout().toolbar), toolbar);
    EXPECT_EQ(MinTrackHeight(), minHeight);
}

TEST_F(MainWindowLiveSettingsTest, TextSizeIsLimitedToTheWindowsRange)
{
    m_sim.textScale = 3.0;
    UiSettingsChanged();
    EXPECT_FLOAT_EQ(m_window->Text()->TextScale(), te::TextFormats::kMaxTextScale);
    EXPECT_FLOAT_EQ(m_window->Files().RowHeight(),
                    te::FileView::kRowHeightDip * te::TextFormats::kMaxTextScale);
}

TEST_F(MainWindowLiveSettingsTest, AnimationEffectsAreReadAgainWhenTheyChange)
{
    m_sim.animations = false; // Settings > Accessibility > Visual effects > Animation effects
    SettingChange(SPI_SETCLIENTAREAANIMATION);
    EXPECT_FALSE(m_window->Capabilities().animationsEnabled);
    m_sim.animations = true;
    SettingChange(SPI_SETCLIENTAREAANIMATION);
    EXPECT_TRUE(m_window->Capabilities().animationsEnabled);

    // Unrelated setting changes do not re-read anything.
    const int reads = m_sim.reads;
    SettingChange(SPI_SETMOUSESPEED);
    SettingChange(0, L"Environment");
    EXPECT_EQ(m_sim.reads, reads);
}

TEST_F(MainWindowLiveSettingsTest, LightAndDarkModeUpdateTheFrameAndTheColors)
{
    // A material mode: Transparent (the default) keeps white text with a halo in both.
    m_window->SetRequestedMode(te::BackdropMode::Mica);
    for (const bool dark : {true, false, true})
    {
        m_sim.darkMode = dark;
        SettingChange(0, L"ImmersiveColorSet"); // what Windows broadcasts on a mode switch
        BOOL frameDark = FALSE;
        ASSERT_HRESULT_SUCCEEDED(
            DwmGetWindowAttribute(Hwnd(), DWMWA_USE_IMMERSIVE_DARK_MODE, &frameDark, sizeof(frameDark)));
        if (m_window->Capabilities().highContrast)
        {
            continue; // high contrast decides the colors (system colors)
        }
        EXPECT_EQ(frameDark != FALSE, dark) << "DWMWA_USE_IMMERSIVE_DARK_MODE";
        const double base = te::Contrast::RelativeLuminance(m_window->Effective().base);
        const double text = te::Contrast::RelativeLuminance(m_window->Effective().text);
        EXPECT_EQ(base < 0.5, dark) << "a dark base in dark mode";
        EXPECT_EQ(text > base, dark) << "light text on dark, dark text on light";
    }
}

TEST_F(MainWindowLiveSettingsTest, HighContrastOnAndOffReResolves)
{
    m_sim.highContrast = true;
    SettingChange(SPI_SETHIGHCONTRAST);
    EXPECT_EQ(m_window->Effective().applied, te::BackdropMode::Solid);
    EXPECT_EQ(m_window->Effective().reason, te::FallbackReason::HighContrast);

    m_sim.highContrast = false;
    SettingChange(SPI_SETHIGHCONTRAST);
    EXPECT_NE(m_window->Effective().reason, te::FallbackReason::HighContrast);
    EXPECT_EQ(m_window->Effective().requested, m_window->Settings().backdropMode);
}

// Manual visual check (not run by CTest): the window at text size 100 %, 150 % and 225 %,
// copied from the screen to te-text-<percent>.bmp in %TE_CAPTURE_DIR% (or %TEMP%). Needs
// the test data (tools/New-TestData.ps1). Run with --gtest_also_run_disabled_tests.
TEST_F(MainWindowLiveSettingsTest, DISABLED_CaptureAtEachTextSize)
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path folder = std::filesystem::path(temp) / L"te-test" / L"nested";
    ASSERT_TRUE(std::filesystem::exists(folder)) << "run tools/New-TestData.ps1";
    Open(folder.wstring());
    ShowWindow(Hwnd(), SW_SHOWNORMAL);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(Hwnd(), MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;
    SetWindowPos(Hwnd(), HWND_TOPMOST, work.left + 20, work.top + 20, (work.right - work.left) / 2,
                 (work.bottom - work.top) * 6 / 10, SWP_NOACTIVATE);
    Pump(1500); // listing and icons
    SendMessageW(Hwnd(), WM_KEYDOWN, VK_HOME, 0);
    for (const double scale : {1.0, 1.5, 2.25})
    {
        m_sim.textScale = scale;
        UiSettingsChanged();
        RedrawWindow(Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        Pump(500);
        te::test::SaveScreen(Hwnd(), L"te-text-" + std::to_wstring(static_cast<int>(scale * 100)) + L".bmp");
    }
    SetWindowPos(Hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace
