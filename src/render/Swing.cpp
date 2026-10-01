#include <te/render/Swing.h>

#include <algorithm>

namespace te::Swing
{

float EaseOut(float t)
{
    const float rest = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - rest * rest * rest;
}

Frame At(const D2D1_RECT_F& area, std::uint64_t elapsedMs, bool hingeRight)
{
    Frame frame{};
    frame.done = elapsedMs >= kDurationMs;
    const float progress = frame.done ? 1.0f : EaseOut(static_cast<float>(elapsedMs) / kDurationMs);
    frame.opacity = kStartOpacity + (1.0f - kStartOpacity) * progress;

    const float angle = kStartAngleDeg * (1.0f - progress);
    if (angle <= 0.0f)
    {
        frame.transform = D2D1::Matrix4x4F(); // identity
        return frame;
    }

    // Rotate about the hinge (a vertical line through the left or right edge, at the
    // vertical centre), then project: the free edge recedes and shrinks. A positive
    // angle sends points right of the hinge away from the viewer, so the right hinge
    // uses a negative one.
    const float width = std::max(1.0f, area.right - area.left);
    const float hingeX = hingeRight ? area.right : area.left;
    const float centreY = (area.top + area.bottom) / 2.0f;
    const float depth = width * 1.5f;
    frame.transform = D2D1::Matrix4x4F::Translation(-hingeX, -centreY, 0.0f) *
                      D2D1::Matrix4x4F::RotationY(hingeRight ? -angle : angle) *
                      D2D1::Matrix4x4F::PerspectiveProjection(depth) *
                      D2D1::Matrix4x4F::Translation(hingeX, centreY, 0.0f);
    return frame;
}

} // namespace te::Swing
