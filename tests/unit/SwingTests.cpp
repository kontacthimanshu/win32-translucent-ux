// The navigation swing (Swing::At) and the text halo color (TextHalo::ColorFor): pure
// functions, checked by transforming points as D2D does (row vector times the matrix,
// then divide by w).

#include <te/appearance/Contrast.h>
#include <te/render/Swing.h>
#include <te/render/TextHalo.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{

constexpr D2D1_RECT_F kArea{100.0f, 50.0f, 500.0f, 350.0f};

D2D1_POINT_2F Apply(const D2D1_MATRIX_4X4_F& m, float x, float y)
{
    const float tx = x * m._11 + y * m._21 + m._41;
    const float ty = x * m._12 + y * m._22 + m._42;
    const float w = x * m._14 + y * m._24 + m._44;
    return {tx / w, ty / w};
}

TEST(Swing, EaseOutRunsFromZeroToOneAndSlowsDown)
{
    EXPECT_FLOAT_EQ(te::Swing::EaseOut(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(te::Swing::EaseOut(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(te::Swing::EaseOut(2.0f), 1.0f);
    EXPECT_FLOAT_EQ(te::Swing::EaseOut(-1.0f), 0.0f);
    // More than half the way after half the time.
    EXPECT_GT(te::Swing::EaseOut(0.5f), 0.5f);
}

TEST(Swing, EndsAtRestWithTheIdentityAndFullOpacity)
{
    for (const bool hingeRight : {false, true})
    {
        const te::Swing::Frame frame = te::Swing::At(kArea, te::Swing::kDurationMs, hingeRight);
        EXPECT_TRUE(frame.done);
        EXPECT_FLOAT_EQ(frame.opacity, 1.0f);
        const D2D1_POINT_2F corner = Apply(frame.transform, kArea.right, kArea.bottom);
        EXPECT_FLOAT_EQ(corner.x, kArea.right);
        EXPECT_FLOAT_EQ(corner.y, kArea.bottom);
    }
}

TEST(Swing, StartsFadedAndOpening)
{
    const te::Swing::Frame frame = te::Swing::At(kArea, 0, false);
    EXPECT_FALSE(frame.done);
    EXPECT_FLOAT_EQ(frame.opacity, te::Swing::kStartOpacity);
    EXPECT_LT(te::Swing::At(kArea, 0, false).opacity, te::Swing::At(kArea, 100, false).opacity);
}

TEST(Swing, TheHingeStaysAndTheFreeEdgeRecedes)
{
    const float middle = (kArea.top + kArea.bottom) / 2.0f;
    const float height = kArea.bottom - kArea.top;
    for (const bool hingeRight : {false, true})
    {
        SCOPED_TRACE(hingeRight ? "right hinge (Back)" : "left hinge");
        const D2D1_MATRIX_4X4_F m = te::Swing::At(kArea, 40, hingeRight).transform;
        const float hingeX = hingeRight ? kArea.right : kArea.left;
        const float freeX = hingeRight ? kArea.left : kArea.right;

        // Points on the hinge do not move.
        const D2D1_POINT_2F hinge = Apply(m, hingeX, kArea.top);
        EXPECT_NEAR(hinge.x, hingeX, 0.01f);
        EXPECT_NEAR(hinge.y, kArea.top, 0.01f);

        // The free edge moves toward the hinge and gets shorter: it is farther away.
        const D2D1_POINT_2F top = Apply(m, freeX, kArea.top);
        const D2D1_POINT_2F bottom = Apply(m, freeX, kArea.bottom);
        EXPECT_LT(std::abs(top.x - hingeX), std::abs(freeX - hingeX));
        EXPECT_LT(bottom.y - top.y, height);
        EXPECT_NEAR((top.y + bottom.y) / 2.0f, middle, 0.01f) << "symmetric about the centre line";
    }
}

TEST(TextHalo, IsTheOppositeLightness)
{
    EXPECT_EQ(te::TextHalo::ColorFor(te::Contrast::kLightText), (te::Rgb{0x00, 0x00, 0x00}));
    EXPECT_EQ(te::TextHalo::ColorFor(te::Contrast::kDarkText), (te::Rgb{0xFF, 0xFF, 0xFF}));
    for (const te::Rgb text : {te::Contrast::kLightText, te::Contrast::kDarkText})
    {
        EXPECT_GE(te::Contrast::ContrastRatio(text, te::TextHalo::ColorFor(text)),
                  te::Contrast::kTextMinimum);
    }
}

} // namespace
