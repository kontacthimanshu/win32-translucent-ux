// ThemeManager against the real OS (T034): QueryCapabilities reads the session's
// settings, Subscribe registers the UISettings events, and NotifyChanged raises the
// changed callback on the calling thread.

#include <te/appearance/ThemeManager.h>

#include <gtest/gtest.h>

namespace
{

TEST(ThemeManager, QueryCapabilitiesMatchesSystemParameters)
{
    te::ThemeManager themes;
    const te::RenderingCapabilities caps = themes.QueryCapabilities();

    HIGHCONTRASTW highContrast{sizeof(highContrast)};
    ASSERT_TRUE(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0));
    EXPECT_EQ(caps.highContrast, (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0);

    BOOL animations = TRUE;
    ASSERT_TRUE(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0));
    EXPECT_EQ(caps.animationsEnabled, animations != FALSE);

    EXPECT_GE(caps.textScaleFactor, 1.0);
    EXPECT_LE(caps.textScaleFactor, 2.25);
    // Left for the owner to fill in from IBackdropManager / WM_TE_BACKDROP_FAILED.
    EXPECT_FALSE(caps.systemBackdropSupported);
    EXPECT_FALSE(caps.backdropApplyFailed);
}

TEST(ThemeManager, SubscribeAndNotifyChangedRaiseCallback)
{
    const HWND window =
        CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ASSERT_NE(window, nullptr);

    int calls = 0;
    {
        te::ThemeManager themes;
        themes.SetChangedCallback([&calls] { ++calls; });
        EXPECT_TRUE(themes.Subscribe(window));
        themes.NotifyChanged();
        EXPECT_EQ(calls, 1);
    }

    DestroyWindow(window);
}

TEST(ThemeManager, ResolveWithQueriedCapabilitiesGivesLegibleText)
{
    te::ThemeManager themes;
    te::RenderingCapabilities caps = themes.QueryCapabilities();
    caps.systemBackdropSupported = true;
    const te::EffectiveAppearance e = themes.Resolve(te::AppearanceSettings{}, caps);
    EXPECT_NE(e.text, e.base);
}

} // namespace
