// MainLayout (UI contract §1) with the Windows text size (T082): the toolbar and the
// status bar grow with their text; the caption does not (the DWM sizes it).

#include <te/app/MainLayout.h>

#include <gtest/gtest.h>

namespace
{

float Height(const D2D1_RECT_F& r)
{
    return r.bottom - r.top;
}

TEST(MainLayoutTextScale, ToolbarAndStatusBarGrowWithTheText)
{
    const SIZE client{1200, 800};
    const te::MainLayout normal = te::MainLayout::Compute(client, 96, 31);
    const te::MainLayout large = te::MainLayout::Compute(client, 96, 31, 1.5f);
    EXPECT_FLOAT_EQ(Height(normal.toolbar), te::MainLayout::kToolbarHeightDip);
    EXPECT_FLOAT_EQ(Height(normal.statusBar), te::MainLayout::kStatusBarHeightDip);
    EXPECT_FLOAT_EQ(Height(large.toolbar), te::MainLayout::kToolbarHeightDip * 1.5f);
    EXPECT_FLOAT_EQ(Height(large.statusBar), te::MainLayout::kStatusBarHeightDip * 1.5f);
    EXPECT_FLOAT_EQ(Height(large.caption), Height(normal.caption));
    EXPECT_FLOAT_EQ(large.fileList.top, large.toolbar.bottom);
    EXPECT_FLOAT_EQ(large.fileList.bottom, large.statusBar.top);
}

TEST(MainLayoutTextScale, AScaleBelowOneIsTreatedAsOne)
{
    const te::MainLayout layout = te::MainLayout::Compute({800, 600}, 144, 45, 0.0f);
    EXPECT_FLOAT_EQ(Height(layout.toolbar), te::MainLayout::kToolbarHeightDip);
}

TEST(MainLayoutTextScale, TheStatusBarKeepsItsHeightInASmallWindow)
{
    // Too short for everything at 225 %: the content shrinks first, never below zero.
    const te::MainLayout layout = te::MainLayout::Compute({600, 200}, 96, 31, 2.25f);
    EXPECT_FLOAT_EQ(Height(layout.statusBar), te::MainLayout::kStatusBarHeightDip * 2.25f);
    EXPECT_GE(Height(layout.fileList), 0.0f);
    EXPECT_GE(Height(layout.toolbar), 0.0f);
}

} // namespace
