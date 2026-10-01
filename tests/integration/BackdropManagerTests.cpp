// BackdropManager against the real DWM (T035): the probe result, the backdrop type set
// for each mode, and the returned HRESULT. On builds before 22621 the probe must report
// unsupported and Apply must return the failure so the caller falls back to Solid.

#include <te/appearance/BackdropManager.h>

#include <dwmapi.h>
#include <gtest/gtest.h>

namespace
{

class BackdropManagerTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_hwnd = CreateWindowExW(0, L"STATIC", L"backdrop test", WS_OVERLAPPEDWINDOW, 0, 0, 200, 200, nullptr,
                                 nullptr, nullptr, nullptr);
        ASSERT_NE(m_hwnd, nullptr);
    }

    void TearDown() override
    {
        if (m_hwnd != nullptr)
        {
            DestroyWindow(m_hwnd);
        }
    }

    static bool SystemBackdropBuild()
    {
        OSVERSIONINFOEXW version{sizeof(version)};
        version.dwMajorVersion = 10;
        version.dwBuildNumber = 22621;
        DWORDLONG mask = 0;
        VER_SET_CONDITION(mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
        VER_SET_CONDITION(mask, VER_BUILDNUMBER, VER_GREATER_EQUAL);
        return VerifyVersionInfoW(&version, VER_MAJORVERSION | VER_BUILDNUMBER, mask) != FALSE;
    }

    DWM_SYSTEMBACKDROP_TYPE ReadBackdropType() const
    {
        DWM_SYSTEMBACKDROP_TYPE type = DWMSBT_AUTO;
        EXPECT_HRESULT_SUCCEEDED(
            DwmGetWindowAttribute(m_hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type)));
        return type;
    }

    static te::EffectiveAppearance Appearance(te::BackdropMode mode)
    {
        te::EffectiveAppearance e;
        e.requested = mode;
        e.applied = mode;
        e.base = {0x20, 0x20, 0x20};
        return e;
    }

    HWND m_hwnd = nullptr;
};

TEST_F(BackdropManagerTest, ProbeMatchesOsBuild)
{
    te::BackdropManager backdrops;
    EXPECT_EQ(backdrops.ProbeSystemBackdrop(m_hwnd), SystemBackdropBuild());
}

TEST_F(BackdropManagerTest, ApplySetsTheBackdropTypeForEachMode)
{
    if (!SystemBackdropBuild())
    {
        GTEST_SKIP() << "DWMWA_SYSTEMBACKDROP_TYPE needs build 22621 or later";
    }

    te::BackdropManager backdrops;
    const struct
    {
        te::BackdropMode mode;
        DWM_SYSTEMBACKDROP_TYPE expected;
    } cases[] = {
        {te::BackdropMode::Mica, DWMSBT_MAINWINDOW},
        {te::BackdropMode::Acrylic, DWMSBT_TRANSIENTWINDOW},
        {te::BackdropMode::Solid, DWMSBT_NONE},
        {te::BackdropMode::Transparent, DWMSBT_NONE},
    };
    for (const auto& c : cases)
    {
        EXPECT_HRESULT_SUCCEEDED(backdrops.Apply(m_hwnd, Appearance(c.mode)));
        EXPECT_EQ(ReadBackdropType(), c.expected);
    }
}

TEST_F(BackdropManagerTest, ApplySetsDarkModeFromTheBase)
{
    te::BackdropManager backdrops;
    te::EffectiveAppearance e = Appearance(te::BackdropMode::Solid);

    BOOL dark = FALSE;
    (void)backdrops.Apply(m_hwnd, e);
    ASSERT_HRESULT_SUCCEEDED(
        DwmGetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)));
    EXPECT_TRUE(dark);

    e.base = {0xF3, 0xF3, 0xF3};
    (void)backdrops.Apply(m_hwnd, e);
    ASSERT_HRESULT_SUCCEEDED(
        DwmGetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)));
    EXPECT_FALSE(dark);
}

// Transparent: the caption follows the (white) text, so the DWM's glyphs are white too,
// even over a light base.
TEST_F(BackdropManagerTest, TransparentCaptionFollowsTheText)
{
    te::BackdropManager backdrops;
    te::EffectiveAppearance e = Appearance(te::BackdropMode::Transparent);
    e.base = {0xF3, 0xF3, 0xF3};
    e.text = {0xFF, 0xFF, 0xFF};
    BOOL dark = FALSE;
    (void)backdrops.Apply(m_hwnd, e);
    ASSERT_HRESULT_SUCCEEDED(
        DwmGetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)));
    EXPECT_TRUE(dark);
}

TEST_F(BackdropManagerTest, ApplyReturnsFailureForAnInvalidWindow)
{
    te::BackdropManager backdrops;
    EXPECT_FALSE(SUCCEEDED(backdrops.Apply(nullptr, Appearance(te::BackdropMode::Mica))));
}

} // namespace
