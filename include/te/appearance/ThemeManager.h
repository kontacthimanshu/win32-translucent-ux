#pragma once

// IThemeManager implementation (T034; research R-05, data-model: RenderingCapabilities,
// EffectiveAppearance). Constructing it does no OS work, so Resolve can be unit-tested
// on its own (T031).

#include <te/appearance/IThemeManager.h>

#include <windows.h>

#include <memory>

namespace te
{

class ThemeManager final : public IThemeManager
{
  public:
    ThemeManager();
    ~ThemeManager() override;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    RenderingCapabilities QueryCapabilities() override;
    EffectiveAppearance Resolve(const AppearanceSettings&, const RenderingCapabilities&) const override;
    void SetChangedCallback(std::function<void()> callback) override;

    // Subscribes to the UISettings ColorValuesChanged, AdvancedEffectsEnabledChanged and
    // TextScaleFactorChanged events. They fire on WinRT threads, so each one only posts
    // WM_TE_SETTINGS_CHANGED to `notifyWindow` (coalesced until NotifyChanged runs).
    // Returns false if UISettings is unavailable; WM_SETTINGCHANGE still works then.
    bool Subscribe(HWND notifyWindow);

    // Called by the owner on the UI thread for WM_TE_SETTINGS_CHANGED and
    // WM_SETTINGCHANGE: raises the changed callback.
    void NotifyChanged();

  private:
    struct Events;

    std::function<void()> m_changed;
    std::unique_ptr<Events> m_events;
};

} // namespace te
