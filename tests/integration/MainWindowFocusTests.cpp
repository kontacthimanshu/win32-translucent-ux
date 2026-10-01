// Keyboard focus management (T080; UI contract §4): the F6 / Shift+F6 ring picker ->
// address bar -> navigation pane -> file list, Enter and Space on the picker, and focus
// cues that Windows may hide (UISF_HIDEFOCUS) until keyboard input shows them.
//
// Run each test in its own process, as ctest does (gtest_discover_tests): the UIA check
// shares UI Automation's process-wide state (see UiaFileListTests.cpp).

#include <te/app/MainWindow.h>
#include <te/appearance/AppearanceIds.h>

#include <UIAutomationClient.h>

#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace
{

using Focus = te::MainWindow::Focus;

void Pump(DWORD ms, const std::function<bool()>& done = {})
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
        if (done && done())
        {
            return;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}

class MainWindowFocusTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"focus test";
        options.quitOnDestroy = false;
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNORMAL));
        ShowWindow(m_window->Hwnd(), SW_SHOWNORMAL); // CTest starts processes hidden
        SetFocus(m_window->Hwnd());
        Pump(200);
    }

    void TearDown() override
    {
        if (m_window)
        {
            if (m_window->Picker() && m_window->Picker()->IsOpen())
            {
                m_window->TogglePicker();
            }
            DestroyWindow(m_window->Hwnd());
            m_window.reset();
        }
        if (m_uninitialize)
        {
            OleUninitialize();
        }
    }

    void Key(UINT vk, bool shift = false)
    {
        BYTE keys[256]{};
        GetKeyboardState(keys);
        const BYTE saved = keys[VK_SHIFT];
        keys[VK_SHIFT] = shift ? 0x80 : 0;
        SetKeyboardState(keys);
        SendMessageW(m_window->Hwnd(), WM_KEYDOWN, vk, 0);
        keys[VK_SHIFT] = saved;
        SetKeyboardState(keys);
    }

    [[nodiscard]] Focus Current() const
    {
        return m_window->KeyboardFocus();
    }

    bool m_uninitialize = false;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(MainWindowFocusTest, F6CyclesPickerAddressPaneAndFileList)
{
    ASSERT_EQ(Current(), Focus::Files) << "the file list starts with the keyboard";
    Key(VK_F6);
    EXPECT_EQ(Current(), Focus::Picker);
    Key(VK_F6);
    EXPECT_EQ(Current(), Focus::Address);
    EXPECT_TRUE(m_window->Address().IsEditing()) << "the address field takes the keyboard";
    EXPECT_EQ(GetFocus(), m_window->Address().Edit());
    // F6 in the address edit, which has the keyboard (the edit passes it to the window).
    SendMessageW(m_window->Address().Edit(), WM_KEYDOWN, VK_F6, 0);
    EXPECT_EQ(Current(), Focus::Places);
    EXPECT_FALSE(m_window->Address().IsEditing());
    Key(VK_F6);
    EXPECT_EQ(Current(), Focus::Files);
    Key(VK_F6);
    EXPECT_EQ(Current(), Focus::Picker) << "the ring wraps around";
}

TEST_F(MainWindowFocusTest, ShiftF6GoesBackwards)
{
    Key(VK_F6, true);
    EXPECT_EQ(Current(), Focus::Places);
    Key(VK_F6, true);
    EXPECT_EQ(Current(), Focus::Address);
    // Shift+F6 in the address edit.
    BYTE keys[256]{};
    GetKeyboardState(keys);
    keys[VK_SHIFT] = 0x80;
    SetKeyboardState(keys);
    SendMessageW(m_window->Address().Edit(), WM_KEYDOWN, VK_F6, 0);
    keys[VK_SHIFT] = 0;
    SetKeyboardState(keys);
    EXPECT_EQ(Current(), Focus::Picker);
    Key(VK_F6, true);
    EXPECT_EQ(Current(), Focus::Files) << "wraps backwards";
}

TEST_F(MainWindowFocusTest, EnterAndSpaceOnThePickerOpenThePopup)
{
    Key(VK_F6);
    ASSERT_EQ(Current(), Focus::Picker);
    Key(VK_RETURN);
    ASSERT_NE(m_window->Picker(), nullptr);
    EXPECT_TRUE(m_window->Picker()->IsOpen()) << "Enter";
    m_window->TogglePicker(); // close
    ASSERT_FALSE(m_window->Picker()->IsOpen());
    SetFocus(m_window->Hwnd());
    Key(VK_SPACE);
    EXPECT_TRUE(m_window->Picker()->IsOpen()) << "Space";
}

TEST_F(MainWindowFocusTest, KeyboardInputShowsHiddenFocusCues)
{
    SendMessageW(m_window->Hwnd(), WM_UPDATEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    ASSERT_FALSE(m_window->FocusCuesVisible()) << "Windows hid the focus cues";
    Key(VK_DOWN);
    EXPECT_TRUE(m_window->FocusCuesVisible()) << "any key shows them again";

    SendMessageW(m_window->Hwnd(), WM_UPDATEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    ASSERT_FALSE(m_window->FocusCuesVisible());
    SendMessageW(m_window->Hwnd(), WM_COMMAND, MAKEWPARAM(IDM_OPEN_APPEARANCE, 1), 0); // an accelerator
    EXPECT_TRUE(m_window->FocusCuesVisible()) << "accelerator keys count as keyboard input";
}

TEST_F(MainWindowFocusTest, AMouseClickDoesNotShowHiddenFocusCues)
{
    SendMessageW(m_window->Hwnd(), WM_UPDATEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    ASSERT_FALSE(m_window->FocusCuesVisible());
    const float scale = static_cast<float>(m_window->Dpi()) / 96.0f;
    const D2D1_RECT_F list = m_window->Layout().fileList;
    const LPARAM point =
        MAKELPARAM(static_cast<int>((list.left + 50) * scale), static_cast<int>((list.top + 60) * scale));
    SendMessageW(m_window->Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(m_window->Hwnd(), WM_LBUTTONUP, 0, point);
    EXPECT_FALSE(m_window->FocusCuesVisible()) << "as in Windows: only the keyboard shows focus cues";
}

TEST_F(MainWindowFocusTest, ThePickerIsTheFocusedElementForUiAutomation)
{
    // Create the UIA root through a real client request.
    std::atomic<bool> done{false};
    std::thread client([&] {
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            {
                wil::com_ptr<IUIAutomation> uia;
                if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&uia))))
                {
                    wil::com_ptr<IUIAutomationElement> element;
                    uia->ElementFromHandle(m_window->Hwnd(), &element);
                }
            }
            CoUninitialize();
        }
        done = true;
    });
    Pump(30000, [&] { return done.load(); });
    client.join();
    ASSERT_NE(m_window->Automation(), nullptr);

    Key(VK_F6);
    ASSERT_EQ(Current(), Focus::Picker);
    wil::com_ptr<IRawElementProviderFragment> focused;
    ASSERT_HRESULT_SUCCEEDED(m_window->Automation()->GetFocus(&focused));
    ASSERT_TRUE(focused);
    wil::com_ptr<IRawElementProviderSimple> simple = focused.query<IRawElementProviderSimple>();
    wil::unique_variant name;
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &name));
    EXPECT_EQ(std::wstring(name.bstrVal), L"Appearance and color");
    wil::unique_variant hasFocus;
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_HasKeyboardFocusPropertyId, &hasFocus));
    EXPECT_EQ(hasFocus.boolVal, VARIANT_TRUE);
    wil::unique_variant focusable;
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_IsKeyboardFocusablePropertyId, &focusable));
    EXPECT_EQ(focusable.boolVal, VARIANT_TRUE);
}

} // namespace
