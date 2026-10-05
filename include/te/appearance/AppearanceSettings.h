#pragma once

// Appearance data (data-model: AppearanceSettings, RenderingCapabilities,
// EffectiveAppearance; research R-04, R-05). The preset palette and limits are
// added in Palette.h (T042).

#include <te/core/Types.h>

#include <array>
#include <variant>

namespace te
{

// The user's requested appearance, persisted in settings.json. Member defaults match
// the research R-11 defaults that ISettingsStore::Defaults() returns.
struct AppearanceSettings
{
    // Clear glass, as the appearance popup's Transparent swatch gives it: the tint at
    // kTransparentTintFloor (Palette.h), so the color still shows over a busy desktop.
    BackdropMode backdropMode = BackdropMode::Transparent;
    std::variant<std::monostate /*accent*/, Rgb> tintColor; // default: follow the accent
    double tintOpacity = 0.45;                              // 0.00–0.80, tint layer strength
    double surfaceOpacity = 0.0; // 0.00–0.90, surface layer opacity, independent of tint
    std::array<Rgb, 16> customColors = [] {
        std::array<Rgb, 16> colors{};
        colors.fill(Rgb{0xFF, 0xFF, 0xFF});
        return colors;
    }();
    // The window drawn as a slab of glass: the thickness of the faces seen along its top,
    // left and bottom edges, in pixels at 100% scale (scaled with the monitor's DPI like the
    // rest of the window; kSlabMinPx-kSlabMaxPx; 0 = a flat pane).
    int slabThicknessPx = 12;
    // Which of the slab's faces are drawn; with all four off the window is a flat pane.
    // slabThicknessPx is kept for when one is turned on again.
    bool slabTop = true;
    bool slabLeft = true;
    bool slabBottom = true;
    bool slabRight = true;
};

inline constexpr int kSlabMinPx = 0;
inline constexpr int kSlabMaxPx = 48;
inline constexpr int kSlabDefaultPx = 12;

// What the OS and session support right now.
struct RenderingCapabilities
{
    bool systemBackdropSupported = false;
    bool transparencyEffectsEnabled = true;
    bool highContrast = false;
    bool darkMode = false;
    // SPI_GETCLIENTAREAANIMATION (Windows "Animation effects"), read again on every change
    // (T082). The app has no animations (R-05); any animation added later MUST be skipped
    // (show the end state at once) while this is false - docs/review-checklist.md.
    bool animationsEnabled = true;
    // UISettings.TextScaleFactor (Windows "Text size", 1.0 to 2.25): text and row heights.
    double textScaleFactor = 1.0;
    Rgb accent{};
    // Set by the owner when IBackdropManager::Apply fails (WM_TE_BACKDROP_FAILED), so the
    // pure Resolve can apply resolution rule 4 (FallbackReason::BackdropApplyFailed).
    bool backdropApplyFailed = false;
};

// Output of IThemeManager::Resolve: what is actually drawn.
struct EffectiveAppearance
{
    BackdropMode requested = BackdropMode::Mica;
    BackdropMode applied = BackdropMode::Solid;
    FallbackReason reason = FallbackReason::None;
    Rgb tint{};
    float tintAlpha = 0.0f;
    float surfaceAlpha = 1.0f; // 1.0 in Solid
    Rgb base{};
    Rgb text{};
    Rgb secondaryText{};
    Rgb selection{};
    Rgb focus{};
    float selectionAlpha = 0.40f; // 0.40 unless raised so selected-row text reaches 4.5:1
    Rgb selectedText{};           // text color on selected rows (research R-05)
    float textScrimAlpha = 0.0f;  // surface-opacity floor inside text areas only; 0 = not needed
    // Transparent: legibility comes from a halo around each glyph (TextHalo) instead of a
    // text-area floor, so the whole window stays equally clear; textScrimAlpha is then 0.
    bool textHalo = false;
    Rgb typicalSurfaceColor{};    // opaque stand-in for the surface (address-edit fallback)
    bool opacityControlEnabled = false;
};

} // namespace te
