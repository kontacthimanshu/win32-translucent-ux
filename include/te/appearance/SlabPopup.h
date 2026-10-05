#pragma once

// The slab popup: a modeless IDD_SLAB dialog under the title bar's slab button, with a
// check box that turns the window's slab faces on or off (AppearanceSettings::slabEnabled),
// a trackbar for their thickness in pixels (AppearanceSettings::slabThicknessPx,
// kSlabMinPx-kSlabMaxPx), its value, and Default. The thickness controls are disabled while
// the faces are off; the thickness is kept.
// It opens and closes like the appearance popup (ColorPicker): the button toggles it,
// Escape or a click elsewhere dismisses it. Every change applies at once through the
// changed callback; the owner persists it.

#include <windows.h>

#include <functional>

namespace te
{

class SlabPopup
{
  public:
    // `instance` holds IDD_SLAB (the executable). `registerDialog` as for ColorPicker.
    SlabPopup(HINSTANCE instance, std::function<void(HWND)> registerDialog);
    ~SlabPopup();

    SlabPopup(const SlabPopup&) = delete;
    SlabPopup& operator=(const SlabPopup&) = delete;

    void Show(HWND owner, const RECT& anchorScreen, bool enabled, int thicknessPx);
    void Hide();
    [[nodiscard]] bool IsOpen() const;
    // Called with the faces' state and thickness for each change.
    void SetChangedCallback(std::function<void(bool enabled, int thicknessPx)> callback);
    // The slab button moved (a DPI change): an open popup moves with it.
    void Reposition(const RECT& anchorScreen);

    // As ColorPicker::ShouldSwallowClick: the click that dismissed the popup goes no
    // further, so a second click on the slab button closes it instead of reopening it.
    bool ShouldSwallowClick(UINT message);
    static constexpr DWORD kSwallowWindowMs = 500;

    [[nodiscard]] HWND Dialog() const noexcept
    {
        return m_dialog;
    }

  private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool EnsureDialog(HWND owner);
    void Position(HWND owner, const RECT& anchorScreen);
    void SyncControls();
    void SetThickness(int px);
    void SetEnabled(bool enabled);
    void Changed();

    HINSTANCE m_instance;
    std::function<void(HWND)> m_registerDialog;
    std::function<void(bool, int)> m_changed;

    HWND m_dialog = nullptr;
    HWND m_owner = nullptr;
    RECT m_anchor{};
    int m_thickness = 0;
    bool m_enabled = true;
    bool m_syncing = false;
    DWORD m_dismissedAt = 0;
    bool m_swallowing = false;
};

} // namespace te
