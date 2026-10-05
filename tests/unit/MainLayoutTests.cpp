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

TEST(MainLayoutSlab, ThePanesMakeRoomForTheLeftAndBottomFaces)
{
    // 150 %: 18 px of slab is 12 DIPs; the caption height already includes it.
    const SIZE client{1800, 1200};
    const te::MainLayout flat = te::MainLayout::Compute(client, 144, 45);
    const te::MainLayout slab = te::MainLayout::Compute(client, 144, 45 + 18, 1.0f, {18, 18, 18});
    EXPECT_FLOAT_EQ(slab.slab.top, 12.0f);
    EXPECT_FLOAT_EQ(slab.slab.left, 12.0f);
    EXPECT_FLOAT_EQ(slab.slab.bottom, 12.0f);
    EXPECT_FLOAT_EQ(slab.caption.left, 0.0f);
    EXPECT_FLOAT_EQ(slab.caption.right, flat.caption.right);
    EXPECT_FLOAT_EQ(slab.toolbar.left, 12.0f);
    EXPECT_FLOAT_EQ(slab.navigationPane.left, 12.0f);
    EXPECT_FLOAT_EQ(slab.statusBar.left, 12.0f);
    EXPECT_FLOAT_EQ(slab.statusBar.bottom, 800.0f - 12.0f);
    EXPECT_FLOAT_EQ(Height(slab.statusBar), te::MainLayout::kStatusBarHeightDip);
    EXPECT_FLOAT_EQ(slab.fileList.right, 1200.0f) << "no face on the right";
    // The face strips fill exactly what the panes left.
    EXPECT_FLOAT_EQ(slab.slabLeft.top, slab.caption.bottom);
    EXPECT_FLOAT_EQ(slab.slabLeft.right, slab.toolbar.left);
    EXPECT_FLOAT_EQ(slab.slabBottom.top, slab.statusBar.bottom);
    EXPECT_FLOAT_EQ(slab.slabBottom.bottom, 800.0f);
}

TEST(MainLayoutSlab, NoSlabLeavesTheLayoutAsBefore)
{
    const te::MainLayout layout = te::MainLayout::Compute({1200, 800}, 96, 31, 1.0f, {});
    EXPECT_FLOAT_EQ(layout.slab.left, 0.0f);
    EXPECT_FLOAT_EQ(layout.slab.bottom, 0.0f);
    EXPECT_FLOAT_EQ(layout.toolbar.left, 0.0f);
    EXPECT_FLOAT_EQ(layout.statusBar.bottom, 800.0f);
    EXPECT_FLOAT_EQ(Height(layout.slabLeft), 0.0f);
    EXPECT_FLOAT_EQ(Height(layout.slabBottom), 0.0f);
}

TEST(MainLayoutSlab, EachFaceMovesOnlyItsOwnEdge)
{
    const SIZE client{1200, 800};
    const te::MainLayout leftOnly = te::MainLayout::Compute(client, 96, 31, 1.0f, {0, 10, 0});
    EXPECT_FLOAT_EQ(leftOnly.toolbar.left, 10.0f);
    EXPECT_FLOAT_EQ(leftOnly.statusBar.bottom, 800.0f);
    EXPECT_FLOAT_EQ(Height(leftOnly.slabBottom), 0.0f);
    EXPECT_FLOAT_EQ(leftOnly.slabLeft.right, 10.0f);

    const te::MainLayout bottomOnly = te::MainLayout::Compute(client, 96, 31, 1.0f, {0, 0, 10});
    EXPECT_FLOAT_EQ(bottomOnly.toolbar.left, 0.0f);
    EXPECT_FLOAT_EQ(bottomOnly.statusBar.bottom, 790.0f);
    EXPECT_FLOAT_EQ(bottomOnly.slabBottom.left, 0.0f);
    EXPECT_FLOAT_EQ(bottomOnly.slabBottom.top, 790.0f);
    EXPECT_FLOAT_EQ(bottomOnly.slabLeft.right - bottomOnly.slabLeft.left, 0.0f);

    const te::MainLayout topOnly = te::MainLayout::Compute(client, 96, 31 + 10, 1.0f, {10, 0, 0});
    EXPECT_FLOAT_EQ(topOnly.slab.top, 10.0f);
    EXPECT_FLOAT_EQ(topOnly.toolbar.top, 41.0f);
    EXPECT_FLOAT_EQ(topOnly.toolbar.left, 0.0f);
    EXPECT_FLOAT_EQ(topOnly.statusBar.bottom, 800.0f);
}

} // namespace
