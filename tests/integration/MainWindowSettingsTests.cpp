// MainWindow settings integration (T047): settings load before the window is shown, a
// corrupt file shows the reset notice, popup changes apply at once and save after a
// 100 ms debounce, closing flushes a pending save, and a Debug --backdrop override is
// never saved. Uses a fake ISettingsStore, so the user's settings file is not touched.

#include <te/app/MainWindow.h>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace
{

class FakeStore final : public te::ISettingsStore
{
  public:
    te::LoadResult Load() override
    {
        ++loads;
        return next;
    }
    HRESULT Save(const te::AppearanceSettings& settings) override
    {
        saved.push_back(settings);
        return S_OK;
    }

    te::LoadResult next;
    int loads = 0;
    std::vector<te::AppearanceSettings> saved;
};

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

class MainWindowSettingsTest : public ::testing::Test
{
  protected:
    std::unique_ptr<te::MainWindow> Open(std::optional<te::BackdropMode> override = std::nullopt)
    {
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"settings test";
        options.quitOnDestroy = false;
        options.settingsStore = &m_store;
        options.requestedMode = override;
        auto window = std::make_unique<te::MainWindow>(std::move(options));
        EXPECT_HRESULT_SUCCEEDED(window->Create(SW_SHOWNOACTIVATE));
        return window;
    }

    static void Close(std::unique_ptr<te::MainWindow>& window)
    {
        DestroyWindow(window->Hwnd());
        window.reset();
    }

    FakeStore m_store;
};

TEST_F(MainWindowSettingsTest, LoadedSettingsApplyFromTheFirstFrame)
{
    m_store.next.settings.backdropMode = te::BackdropMode::Acrylic;
    m_store.next.settings.tintOpacity = 0.45;
    auto window = Open();
    EXPECT_EQ(m_store.loads, 1);
    EXPECT_EQ(window->Settings().backdropMode, te::BackdropMode::Acrylic);
    EXPECT_EQ(window->Effective().requested, te::BackdropMode::Acrylic);
    EXPECT_NEAR(window->Settings().tintOpacity, 0.45, 1e-9);
    Close(window);
    EXPECT_TRUE(m_store.saved.empty()) << "nothing changed, nothing saved";
}

TEST_F(MainWindowSettingsTest, CorruptFileShowsTheResetNotice)
{
    m_store.next.fileWasCorrupt = true;
    auto window = Open();
    EXPECT_EQ(window->Status().LeftText(), L"Appearance settings were reset");
    Close(window);
}

TEST_F(MainWindowSettingsTest, ChangesApplyAtOnceAndSaveAfterTheDebounce)
{
    auto window = Open();
    te::AppearanceSettings changed = window->Settings();
    changed.tintColor = te::Rgb{0x87, 0x64, 0xB8};
    changed.surfaceOpacity = 0.30;
    window->ApplyUserSettings(changed);
    EXPECT_EQ(window->Effective().tint, (te::Rgb{0x87, 0x64, 0xB8})) << "applied immediately";
    EXPECT_TRUE(window->SavePending());
    EXPECT_TRUE(m_store.saved.empty()) << "not saved before the debounce";

    // A burst of changes: only one save, with the last values.
    changed.surfaceOpacity = 0.35;
    window->ApplyUserSettings(changed);
    changed.surfaceOpacity = 0.40;
    window->ApplyUserSettings(changed);
    Pump(te::MainWindow::kSaveDelayMs * 4);

    ASSERT_EQ(m_store.saved.size(), 1u);
    EXPECT_NEAR(m_store.saved[0].surfaceOpacity, 0.40, 1e-9);
    EXPECT_FALSE(window->SavePending());
    Close(window);
    EXPECT_EQ(m_store.saved.size(), 1u) << "nothing left to flush";
}

TEST_F(MainWindowSettingsTest, ClosingFlushesAPendingSave)
{
    auto window = Open();
    te::AppearanceSettings changed = window->Settings();
    changed.backdropMode = te::BackdropMode::Solid;
    window->ApplyUserSettings(changed);
    Close(window); // within the debounce window
    ASSERT_EQ(m_store.saved.size(), 1u);
    EXPECT_EQ(m_store.saved[0].backdropMode, te::BackdropMode::Solid);
}

TEST_F(MainWindowSettingsTest, BackdropOverrideIsForThisSessionOnly)
{
    m_store.next.settings.backdropMode = te::BackdropMode::Mica;
    auto window = Open(te::BackdropMode::Acrylic);
    EXPECT_EQ(window->Settings().backdropMode, te::BackdropMode::Acrylic);

    // Another change (not the mode) is saved with the loaded mode, not the override.
    te::AppearanceSettings changed = window->Settings();
    changed.tintOpacity = 0.60;
    window->ApplyUserSettings(changed);
    Close(window);
    ASSERT_EQ(m_store.saved.size(), 1u);
    EXPECT_EQ(m_store.saved[0].backdropMode, te::BackdropMode::Mica);
    EXPECT_NEAR(m_store.saved[0].tintOpacity, 0.60, 1e-9);
}

TEST_F(MainWindowSettingsTest, ChoosingAModeReplacesTheOverride)
{
    m_store.next.settings.backdropMode = te::BackdropMode::Mica;
    auto window = Open(te::BackdropMode::Acrylic);
    te::AppearanceSettings changed = window->Settings();
    changed.backdropMode = te::BackdropMode::Solid;
    window->ApplyUserSettings(changed);
    Close(window);
    ASSERT_EQ(m_store.saved.size(), 1u);
    EXPECT_EQ(m_store.saved[0].backdropMode, te::BackdropMode::Solid);
}

TEST_F(MainWindowSettingsTest, OpeningAndClosingThePickerChangesNothing)
{
    auto window = Open();
    const te::AppearanceSettings before = window->Settings();
    window->TogglePicker();
    window->TogglePicker();
    Pump(te::MainWindow::kSaveDelayMs * 2);
    EXPECT_EQ(window->Settings().backdropMode, before.backdropMode);
    EXPECT_FALSE(window->SavePending());
    Close(window);
    EXPECT_TRUE(m_store.saved.empty());
}

} // namespace
