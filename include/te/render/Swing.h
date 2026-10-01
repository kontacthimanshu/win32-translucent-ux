#pragma once

// The navigation transition: the new folder's listing swings into place in perspective,
// like a pane of glass hinged on one edge (Debug and Release; skipped while Windows
// "Animation effects" is off, RenderingCapabilities::animationsEnabled).

#include <d2d1_1.h>
#include <d2d1_1helper.h>

#include <cstdint>

namespace te::Swing
{

inline constexpr std::uint64_t kDurationMs = 280;
inline constexpr float kStartAngleDeg = 24.0f; // how far open the pane starts
inline constexpr float kStartOpacity = 0.25f;
inline constexpr unsigned kFrameIntervalMs = 15; // WM_TIMER period while it runs

struct Frame
{
    D2D1_MATRIX_4X4_F transform; // for ID2D1DeviceContext::DrawBitmap(..., perspectiveTransform)
    float opacity;
    bool done; // elapsedMs reached kDurationMs: the frame is the resting state
};

// Ease-out cubic on [0, 1]: fast start, gentle landing.
float EaseOut(float t);

// The frame `elapsedMs` into the swing of `area` (DIPs). Forward navigation hinges the
// pane on its left edge, Back on its right edge, so the motion follows the direction of
// travel. At the end the transform is the identity and the opacity 1.
Frame At(const D2D1_RECT_F& area, std::uint64_t elapsedMs, bool hingeRight);

} // namespace te::Swing
