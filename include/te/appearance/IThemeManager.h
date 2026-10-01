#pragma once

// OS appearance capabilities and effective-appearance resolution (research R-05).

#include <te/appearance/AppearanceSettings.h>

#include <functional>

namespace te
{

class IThemeManager
{
  public:
    virtual ~IThemeManager() = default;

    virtual RenderingCapabilities QueryCapabilities() = 0;

    // Pure function. Unit-tested as a truth table (data-model: EffectiveAppearance).
    virtual EffectiveAppearance Resolve(const AppearanceSettings&, const RenderingCapabilities&) const = 0;

    // Raised on WM_SETTINGCHANGE / UISettings events, marshalled to the UI thread.
    virtual void SetChangedCallback(std::function<void()>) = 0;
};

} // namespace te
