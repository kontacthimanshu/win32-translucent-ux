// Exact window scaling on a DPI change (T081): WM_GETDPISCALEDSIZE asks for the window
// size that keeps the client area's size in DIPs, so moving between monitors and back
// never lets the window grow or shrink by rounding.

#include <te/window/DpiManager.h>

#include <gtest/gtest.h>

namespace
{

constexpr SIZE kFrame96{16, 8};   // left + right borders, bottom border at 100%
constexpr SIZE kFrame144{22, 11}; // the same at 150%

TEST(DpiScalingTest, ScalesTheClientAreaAndAddsTheNewFrame)
{
    const SIZE size = te::DpiManager::ScaledWindowSize({1200, 800}, 96, 144, kFrame144);
    EXPECT_EQ(size.cx, 1800 + 22);
    EXPECT_EQ(size.cy, 1200 + 11);
}

TEST(DpiScalingTest, RoundsToTheNearestPixel)
{
    // 1001 * 1.5 = 1501.5 -> 1502; 333 * 1.5 = 499.5 -> 500 (halves away from zero).
    const SIZE up = te::DpiManager::ScaledWindowSize({1001, 333}, 96, 144, {});
    EXPECT_EQ(up.cx, 1502);
    EXPECT_EQ(up.cy, 500);
    // 125% -> 200%: 1000 * 192 / 120 = 1600 exactly.
    const SIZE exact = te::DpiManager::ScaledWindowSize({1000, 750}, 120, 192, {});
    EXPECT_EQ(exact.cx, 1600);
    EXPECT_EQ(exact.cy, 1200);
}

TEST(DpiScalingTest, ARoundTripKeepsTheClientSize)
{
    // 100% -> 200% -> 100%: the client area is back to its size, only the frame differs.
    const SIZE there = te::DpiManager::ScaledWindowSize({1200, 800}, 96, 192, {32, 16});
    const SIZE thereClient{there.cx - 32, there.cy - 16};
    const SIZE back = te::DpiManager::ScaledWindowSize(thereClient, 192, 96, kFrame96);
    EXPECT_EQ(back.cx, 1200 + kFrame96.cx);
    EXPECT_EQ(back.cy, 800 + kFrame96.cy);
}

TEST(DpiScalingTest, AZeroDpiIsTreatedAsNoChange)
{
    const SIZE size = te::DpiManager::ScaledWindowSize({640, 480}, 0, 0, kFrame96);
    EXPECT_EQ(size.cx, 640 + kFrame96.cx);
    EXPECT_EQ(size.cy, 480 + kFrame96.cy);
}

} // namespace
