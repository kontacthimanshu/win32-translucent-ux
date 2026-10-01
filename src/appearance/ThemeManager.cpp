#include <te/appearance/ThemeManager.h>

#include <te/appearance/Contrast.h>
#include <te/core/Messages.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>
#include <atomic>
#include <utility>
#include <variant>
#include <vector>

namespace te
{

namespace
{

namespace vm = winrt::Windows::UI::ViewManagement;

constexpr Rgb kDarkBase{0x20, 0x20, 0x20};
constexpr Rgb kLightBase{0xF3, 0xF3, 0xF3};
// The selection fill in Transparent mode: the standard strength, never raised (textHalo).
constexpr float kTransparentSelectionAlpha = 0.40f;

Rgb ToRgb(winrt::Windows::UI::Color color)
{
    return {color.R, color.G, color.B};
}

Rgb SysColor(int index)
{
    const COLORREF c = GetSysColor(index);
    return {GetRValue(c), GetGValue(c), GetBValue(c)};
}

// Resolution rules (data-model: EffectiveAppearance); the first match wins.
std::pair<BackdropMode, FallbackReason> ResolveMode(BackdropMode requested, const RenderingCapabilities& caps)
{
    const bool translucent = requested != BackdropMode::Solid;
    if (caps.highContrast)
    {
        return {BackdropMode::Solid, FallbackReason::HighContrast};
    }
    if (!caps.transparencyEffectsEnabled)
    {
        return {BackdropMode::Solid, FallbackReason::TransparencyOff};
    }
    if (translucent && !caps.systemBackdropSupported)
    {
        return {BackdropMode::Solid, FallbackReason::BackdropUnsupported};
    }
    if (translucent && caps.backdropApplyFailed)
    {
        return {BackdropMode::Solid, FallbackReason::BackdropApplyFailed};
    }
    return {requested, FallbackReason::None};
}

double MinContrast(Rgb color, const std::vector<Rgb>& composites)
{
    double lowest = 21.0;
    for (const Rgb composite : composites)
    {
        lowest = std::min(lowest, Contrast::ContrastRatio(color, composite));
    }
    return lowest;
}

// The accent moved toward `toward` in steps of 0.05, just far enough to reach 3:1
// against every composite (research R-05: selection and focus indicators). Falls back
// to the step with the best minimum if none reaches it.
Rgb IndicatorColor(Rgb accent, Rgb toward, const std::vector<Rgb>& composites)
{
    Rgb best = accent;
    double bestContrast = MinContrast(accent, composites);
    for (int step = 0; step <= 20 && bestContrast < Contrast::kIndicatorMinimum; ++step)
    {
        const Rgb candidate =
            Contrast::Composite(accent, toward, static_cast<float>(step) * Contrast::kAlphaStep);
        const double contrast = MinContrast(candidate, composites);
        if (contrast > bestContrast)
        {
            best = candidate;
            bestContrast = contrast;
        }
    }
    return best;
}

void ResolveHighContrast(EffectiveAppearance& e)
{
    e.tintAlpha = 0.0f;
    e.surfaceAlpha = 1.0f;
    e.base = SysColor(COLOR_WINDOW);
    e.text = SysColor(COLOR_WINDOWTEXT);
    e.secondaryText = e.text;
    e.selection = SysColor(COLOR_HIGHLIGHT);
    e.focus = SysColor(COLOR_HOTLIGHT);
    e.selectedText = SysColor(COLOR_HIGHLIGHTTEXT);
    e.selectionAlpha = 1.0f;
    e.textScrimAlpha = 0.0f;
    e.typicalSurfaceColor = e.base;
    e.opacityControlEnabled = false;
}

} // namespace

// UISettings and its event subscriptions. Handlers run on WinRT threads and share
// `pending` and the target window through a shared_ptr, so a handler already in flight
// while the manager is destroyed never touches freed memory.
struct ThemeManager::Events
{
    struct Shared
    {
        std::atomic<HWND> window{nullptr};
        std::atomic<bool> pending{false};

        void Post()
        {
            const HWND hwnd = window.load();
            if (hwnd != nullptr && !pending.exchange(true))
            {
                PostMessageW(hwnd, WM_TE_SETTINGS_CHANGED, 0, 0);
            }
        }
    };

    vm::UISettings settings{nullptr};
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    vm::UISettings::ColorValuesChanged_revoker colorsChanged;
    vm::UISettings::AdvancedEffectsEnabledChanged_revoker effectsChanged;
    vm::UISettings::TextScaleFactorChanged_revoker textScaleChanged;
};

ThemeManager::ThemeManager() = default;

ThemeManager::~ThemeManager()
{
    if (m_events)
    {
        m_events->shared->window.store(nullptr);
    }
}

RenderingCapabilities ThemeManager::QueryCapabilities()
{
    RenderingCapabilities caps;

    HIGHCONTRASTW highContrast{sizeof(highContrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0))
    {
        caps.highContrast = (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }

    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
    {
        caps.animationsEnabled = animations != FALSE;
    }

    try
    {
        const vm::UISettings settings =
            m_events && m_events->settings ? m_events->settings : vm::UISettings();
        caps.transparencyEffectsEnabled = settings.AdvancedEffectsEnabled();
        // Microsoft's documented Win32 approach: a light foreground means dark mode.
        const Rgb foreground = ToRgb(settings.GetColorValue(vm::UIColorType::Foreground));
        caps.darkMode = Contrast::RelativeLuminance(foreground) > 0.5;
        caps.accent = ToRgb(settings.GetColorValue(vm::UIColorType::Accent));
        caps.textScaleFactor = settings.TextScaleFactor();
    }
    catch (const winrt::hresult_error&)
    {
        // UISettings unavailable: keep the defaults (transparency on, light mode, black
        // accent, text scale 1.0); the backdrop probe still decides what is shown.
    }

    // systemBackdropSupported and backdropApplyFailed are filled in by the owner
    // (IBackdropManager::ProbeSystemBackdrop, WM_TE_BACKDROP_FAILED).
    return caps;
}

EffectiveAppearance ThemeManager::Resolve(const AppearanceSettings& settings,
                                          const RenderingCapabilities& caps) const
{
    EffectiveAppearance e;
    e.requested = settings.backdropMode;
    std::tie(e.applied, e.reason) = ResolveMode(settings.backdropMode, caps);
    e.tint =
        std::holds_alternative<Rgb>(settings.tintColor) ? std::get<Rgb>(settings.tintColor) : caps.accent;

    if (caps.highContrast)
    {
        ResolveHighContrast(e);
        return e;
    }

    const bool solid = e.applied == BackdropMode::Solid;
    e.tintAlpha = solid ? 0.0f : static_cast<float>(std::clamp(settings.tintOpacity, 0.0, 1.0));
    e.surfaceAlpha = solid ? 1.0f : static_cast<float>(std::clamp(settings.surfaceOpacity, 0.0, 1.0));
    e.base = caps.darkMode ? kDarkBase : kLightBase;
    e.opacityControlEnabled = !solid;
    e.typicalSurfaceColor =
        Contrast::Composite(Contrast::Composite(e.base, e.base, e.surfaceAlpha), e.tint, e.tintAlpha);

    // Text colors and the text-area opacity floor over the backdrop extremes (R-05).
    const Contrast::BackdropExtremeSet extremes =
        Contrast::BackdropExtremes(e.applied, caps.darkMode, e.base);
    if (e.applied == BackdropMode::Transparent)
    {
        // Clear glass everywhere: no floor behind text. All text is white, in light and
        // dark mode alike, and each glyph carries a black halo (TextHalo::ColorFor), which
        // keeps it at 4.5:1 over whatever the glass shows.
        e.text = Contrast::kLightText;
        e.secondaryText = Contrast::kLightText;
        e.textScrimAlpha = 0.0f;
        e.textHalo = true;
    }
    else
    {
        const Contrast::TextChoice text =
            Contrast::PickTextColors(extremes, e.base, e.surfaceAlpha, e.tint, e.tintAlpha);
        e.text = text.text;
        e.secondaryText = text.secondary;
        e.textScrimAlpha = text.scrimAlpha;
    }

    // What is behind text for each extreme, with the floor applied.
    const float textAlpha = std::max(e.surfaceAlpha, e.textScrimAlpha);
    std::vector<Rgb> textAreas;
    for (const Rgb extreme : extremes.Span())
    {
        textAreas.push_back(
            Contrast::Composite(Contrast::Composite(extreme, e.base, textAlpha), e.tint, e.tintAlpha));
    }

    // Selection and focus come from the accent, adjusted to 3:1 against the same areas:
    // lighter over dark surfaces, darker over light ones.
    const Rgb toward =
        Contrast::RelativeLuminance(e.base) < 0.5 ? Rgb{0xFF, 0xFF, 0xFF} : Rgb{0x00, 0x00, 0x00};
    e.selection = IndicatorColor(caps.accent, toward, textAreas);
    e.focus = e.selection;

    if (e.textHalo)
    {
        // Transparent: selected text carries its halo like all other text, so the fill
        // need not be raised toward opaque to reach 4.5:1 over black and white alike;
        // it keeps the standard strength and the selection stays as clear as the rest.
        e.selectionAlpha = kTransparentSelectionAlpha;
        e.selectedText = e.text;
        return e;
    }

    // Text on selected rows (R-05): fill opacity and text color reaching 4.5:1.
    const Rgb otherText = e.text == Contrast::kLightText ? Contrast::kDarkText : Contrast::kLightText;
    const Contrast::SelectionChoice selected =
        Contrast::PickSelectedRowColors(textAreas, e.selection, e.text, otherText);
    e.selectionAlpha = selected.selectionAlpha;
    e.selectedText = selected.selectedText;
    return e;
}

void ThemeManager::SetChangedCallback(std::function<void()> callback)
{
    m_changed = std::move(callback);
}

bool ThemeManager::Subscribe(HWND notifyWindow)
{
    try
    {
        auto events = std::make_unique<Events>();
        events->settings = vm::UISettings();
        events->shared->window.store(notifyWindow);

        const std::shared_ptr<Events::Shared> shared = events->shared;
        events->colorsChanged = events->settings.ColorValuesChanged(
            winrt::auto_revoke,
            [shared](const vm::UISettings&, const winrt::Windows::Foundation::IInspectable&) {
                shared->Post();
            });
        events->effectsChanged = events->settings.AdvancedEffectsEnabledChanged(
            winrt::auto_revoke,
            [shared](const vm::UISettings&, const winrt::Windows::Foundation::IInspectable&) {
                shared->Post();
            });
        events->textScaleChanged = events->settings.TextScaleFactorChanged(
            winrt::auto_revoke,
            [shared](const vm::UISettings&, const winrt::Windows::Foundation::IInspectable&) {
                shared->Post();
            });

        if (m_events)
        {
            m_events->shared->window.store(nullptr);
        }
        m_events = std::move(events);
        return true;
    }
    catch (const winrt::hresult_error&)
    {
        return false;
    }
}

void ThemeManager::NotifyChanged()
{
    if (m_events)
    {
        m_events->shared->pending.store(false);
    }
    if (m_changed)
    {
        m_changed();
    }
}

} // namespace te
