#pragma once

// Common value types shared across component areas (contracts/component-interfaces.md,
// "Common types").

#include <windows.h>

#include <cstdint>
#include <string>

namespace te
{

// Monotonic navigation/request counter. Results tagged with an older generation are
// stale and must be discarded (data-model: DirectoryRequest, research R-07).
using Generation = std::uint64_t;

// Requested or applied window appearance (research R-01, R-04).
enum class BackdropMode
{
    Acrylic,
    Mica,
    Solid,
    // Clear glass: no system backdrop material, so the desktop shows through unblurred.
    // Appended last so the stored and forwarded mode indexes of the others stay valid.
    Transparent,
};

// Why the applied appearance differs from the requested one (data-model:
// EffectiveAppearance resolution rules).
enum class FallbackReason
{
    None,
    HighContrast,
    TransparencyOff,
    BackdropUnsupported,
    BackdropApplyFailed,
};

// An sRGB colour, 8 bits per channel.
struct Rgb
{
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;

    friend constexpr bool operator==(const Rgb&, const Rgb&) = default;
};

// A result paired with a message the UI can show to the user.
struct Status
{
    HRESULT hr;
    std::wstring message;
};

} // namespace te
