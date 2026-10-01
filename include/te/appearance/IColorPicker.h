#pragma once

// The title-bar appearance popup (research R-10, UI contract §3).

#include <te/appearance/AppearanceSettings.h>

#include <windows.h>

#include <functional>

namespace te
{

class IColorPicker
{
  public:
    virtual ~IColorPicker() = default;

    virtual void Show(HWND owner, const RECT& anchorScreen, const AppearanceSettings& current,
                      const EffectiveAppearance& effective) = 0;
    virtual void Hide() = 0;
    virtual bool IsOpen() const = 0;

    // Called for each live change and on Reset. The owner persists (debounced)
    // and re-resolves.
    virtual void SetChangedCallback(std::function<void(const AppearanceSettings&)>) = 0;
};

} // namespace te
