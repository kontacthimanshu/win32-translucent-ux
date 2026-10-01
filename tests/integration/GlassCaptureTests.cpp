// Manual visual check of Transparent mode, the depth edges and the navigation swing
// (DISABLED_: run with --gtest_also_run_disabled_tests). The window opens over a striped
// magenta and black backdrop window, so every see-through pixel is obvious; the BMPs go
// to %TE_CAPTURE_DIR% or %TEMP%. Needs the test data from tools/New-TestData.ps1.

#include <te/app/CommandIds.h>
#include <te/app/MainWindow.h>
#include <te/ui/AddressBar.h>
#include <te/ui/FilterBox.h>
#include <te/settings/ISettingsStore.h>

#include "../../resources/resource.h" // IDI_APP, embedded in the test executable
#include "ScreenCapture.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace
{

void Pump(DWORD ms)
{
    const ULONGLONG end = GetTickCount64() + ms;
    MSG msg{};
    while (GetTickCount64() < end)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
}

LRESULT CALLBACK StripesProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps{};
        const HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        const HBRUSH magenta = CreateSolidBrush(RGB(0xFF, 0x00, 0xFF));
        FillRect(dc, &client, magenta);
        DeleteObject(magenta);
        const auto black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        for (LONG x = 0; x < client.right; x += 120)
        {
            const RECT stripe{x, 0, x + 40, client.bottom};
            FillRect(dc, &stripe, black);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateStripes(const RECT& bounds)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = StripesProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"te-test-stripes";
    RegisterClassW(&wc);
    return CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP | WS_VISIBLE, bounds.left,
                           bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr,
                           nullptr, wc.hInstance, nullptr);
}

// Settings in memory only: the window starts from `settings`, and nothing is saved.
class MemoryStore final : public te::ISettingsStore
{
  public:
    explicit MemoryStore(const te::AppearanceSettings& settings) : m_settings(settings) {}
    te::LoadResult Load() override
    {
        return {m_settings, {}, false};
    }
    HRESULT Save(const te::AppearanceSettings& settings) override
    {
        m_settings = settings;
        return S_OK;
    }

  private:
    te::AppearanceSettings m_settings;
};

std::unique_ptr<te::MainWindow> Open(te::BackdropMode mode, const std::filesystem::path& folder,
                                     te::ISettingsStore* store = nullptr)
{
    te::MainWindow::Options options;
    options.settingsStore = store;
    options.startShell = true;
    options.initialPath = folder.wstring();
    options.iconResourceId = IDI_APP;
    options.instance = GetModuleHandleW(nullptr);
    options.title = L"glass capture";
    options.quitOnDestroy = false;
    options.requestedMode = mode;
    auto window = std::make_unique<te::MainWindow>(std::move(options));
    if (FAILED(window->Create(SW_SHOWNORMAL)))
    {
        return nullptr;
    }
    return window;
}

TEST(GlassCapture, DISABLED_TransparentDepthAndSwing)
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path folder = std::filesystem::path(temp) / L"te-test" / L"nested";
    ASSERT_TRUE(std::filesystem::exists(folder)) << "run tools/New-TestData.ps1";

    const RECT backdrop{100, 100, 1200, 850};
    const HWND stripes = CreateStripes(backdrop);
    ASSERT_NE(stripes, nullptr);
    Pump(200);

    for (const auto& [mode, name] : {std::pair{te::BackdropMode::Transparent, std::wstring(L"transparent")},
                                     std::pair{te::BackdropMode::Mica, std::wstring(L"mica")}})
    {
        auto window = Open(mode, folder);
        ASSERT_TRUE(window);
        const HWND hwnd = window->Hwnd();
        SetWindowPos(hwnd, HWND_TOP, 150, 150, 1000, 650, SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        Pump(1500); // listing and icons
        // The first row selected and the second hovered: the selection and hover fills.
        SendMessageW(hwnd, WM_KEYDOWN, VK_HOME, 0);
        RECT client{};
        GetClientRect(hwnd, &client);
        Pump(200);
        te::test::SaveScreen(stripes, L"te-glass-" + name + L".bmp");

        if (mode == te::BackdropMode::Transparent)
        {
            // Up to te-test, then frames early in the swing, later and at rest.
            PostMessageW(hwnd, WM_COMMAND, IDM_UP, 0);
            Pump(90);
            te::test::SaveScreen(stripes, L"te-glass-swing-early.bmp");
            Pump(80);
            te::test::SaveScreen(stripes, L"te-glass-swing-late.bmp");
            Pump(600);
            te::test::SaveScreen(stripes, L"te-glass-swing-done.bmp");
        }
        DestroyWindow(hwnd);
        Pump(100);
    }
    DestroyWindow(stripes);
}

// The same red at three tint strengths, in Transparent mode over the stripes.
TEST(GlassCapture, DISABLED_TintStrengths)
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path folder = std::filesystem::path(temp) / L"te-test" / L"nested";
    ASSERT_TRUE(std::filesystem::exists(folder)) << "run tools/New-TestData.ps1";
    const HWND stripes = CreateStripes(RECT{100, 100, 1200, 850});
    ASSERT_NE(stripes, nullptr);
    Pump(200);
    for (const double strength : {0.20, 0.45, 0.60})
    {
        te::AppearanceSettings settings;
        settings.backdropMode = te::BackdropMode::Transparent;
        settings.tintColor = te::Rgb{0xE8, 0x11, 0x23}; // Red
        settings.tintOpacity = strength;
        MemoryStore store(settings);
        auto window = Open(te::BackdropMode::Transparent, folder, &store);
        ASSERT_TRUE(window);
        SetWindowPos(window->Hwnd(), HWND_TOP, 150, 150, 1000, 650, SWP_SHOWWINDOW);
        SetForegroundWindow(window->Hwnd());
        Pump(1200);
        te::test::SaveScreen(stripes,
                             L"te-tint-" + std::to_wstring(static_cast<int>(strength * 100)) + L".bmp");
        DestroyWindow(window->Hwnd());
        Pump(100);
    }
    DestroyWindow(stripes);
}


// The native edits in Transparent: the address being edited and a filter being typed.
TEST(GlassCapture, DISABLED_EditBoxes)
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path folder = std::filesystem::path(temp) / L"te-test" / L"nested";
    ASSERT_TRUE(std::filesystem::exists(folder)) << "run tools/New-TestData.ps1";
    const HWND stripes = CreateStripes(RECT{100, 100, 1200, 850});
    ASSERT_NE(stripes, nullptr);
    Pump(200);

    te::AppearanceSettings settings;
    settings.backdropMode = te::BackdropMode::Transparent;
    settings.tintColor = te::Rgb{0x00, 0x78, 0xD4}; // Blue
    settings.tintOpacity = 0.45;
    MemoryStore store(settings);
    auto window = Open(te::BackdropMode::Transparent, folder, &store);
    ASSERT_TRUE(window);
    const HWND hwnd = window->Hwnd();
    SetWindowPos(hwnd, HWND_TOP, 150, 150, 1000, 650, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    Pump(1200);

    // Filter: typed text.
    SendMessageW(hwnd, WM_COMMAND, IDM_FOCUS_FILTER, 0);
    Pump(100);
    for (const wchar_t c : std::wstring(L"read"))
    {
        SendMessageW(window->Filter().Edit(), WM_CHAR, c, 0);
    }
    Pump(300);
    te::test::SaveScreen(stripes, L"te-edit-filter.bmp");

    // Address: editing, caret at the end so the native selection does not cover it.
    SendMessageW(hwnd, WM_COMMAND, IDM_FOCUS_ADDRESS, 0);
    Pump(100);
    const HWND address = window->Address().Edit();
    const int length = GetWindowTextLengthW(address);
    SendMessageW(address, EM_SETSEL, length, length);
    Pump(300);
    te::test::SaveScreen(stripes, L"te-edit-address.bmp");

    DestroyWindow(hwnd);
    Pump(100);
    DestroyWindow(stripes);
}

} // namespace
