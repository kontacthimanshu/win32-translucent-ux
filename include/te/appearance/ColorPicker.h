#pragma once

// The appearance popup (T045; research R-10, UI contract §3): a modeless IDD_APPEARANCE
// dialog with the mode radios, 12 preset swatches in a 4 x 3 grid, the Transparent
// swatch (clear glass, BackdropMode::Transparent), Accent color,
// Custom..., the surface-opacity and tint-strength trackbars, a preview and Reset.
// Every change applies at once through the changed callback; the owner persists it.

#include <te/appearance/IColorPicker.h>

#include <windows.h>

#include <array>
#include <functional>
#include <string>

namespace te
{

class ColorPicker final : public IColorPicker
{
  public:
    // `instance` holds IDD_APPEARANCE and the popup strings (the executable).
    // `registerDialog` is called with the popup when it opens and with nullptr when it
    // closes, so the message loop can route keys with IsDialogMessageW
    // (Application::SetModelessDialog).
    ColorPicker(HINSTANCE instance, std::function<void(HWND)> registerDialog);
    ~ColorPicker() override;

    ColorPicker(const ColorPicker&) = delete;
    ColorPicker& operator=(const ColorPicker&) = delete;

    // IColorPicker
    void Show(HWND owner, const RECT& anchorScreen, const AppearanceSettings& current,
              const EffectiveAppearance& effective) override;
    void Hide() override;
    [[nodiscard]] bool IsOpen() const override;
    void SetChangedCallback(std::function<void(const AppearanceSettings&)> callback) override;
    // Called with true when the popup is shown and false when it is hidden, however it
    // closes (Escape, deactivation, the picker button) - for UI Automation (T075).
    void SetOpenChangedCallback(std::function<void(bool open)> callback);

    // What the session supports; decides which mode radios are enabled and why
    // (FR-022). Call before Show and whenever the capabilities change.
    void SetCapabilities(const RenderingCapabilities& capabilities);
    // The picker button moved (a DPI change, T081): an open popup moves with it, inside
    // the work area of the monitor the owner is now on. Nothing happens while closed.
    void Reposition(const RECT& anchorScreen);

    // Refreshes the controls after the owner re-resolved the appearance while open.
    void Update(const AppearanceSettings& current, const EffectiveAppearance& effective);

    // The click that dismissed the popup by deactivating it must not reach the window
    // underneath (R-10). The owner calls this for WM_LBUTTONDOWN / WM_LBUTTONUP; it
    // returns true (swallow) for the down/up pair that follows a dismissal within
    // kSwallowWindowMs. This also makes a second click on the picker button close the
    // popup instead of reopening it.
    bool ShouldSwallowClick(UINT message);
    static constexpr DWORD kSwallowWindowMs = 500;

    [[nodiscard]] HWND Dialog() const noexcept
    {
        return m_dialog;
    }
    [[nodiscard]] const AppearanceSettings& Settings() const noexcept
    {
        return m_settings;
    }

  private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SwatchProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                       DWORD_PTR data);
    INT_PTR HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool EnsureDialog(HWND owner);
    void Position(HWND owner, const RECT& anchorScreen);
    void SyncControls();
    void OnCommand(int id, int code, HWND control);
    void OnScroll(HWND trackbar);
    void OnCustomColor();
    void SetMode(BackdropMode mode);
    // In Transparent, a color choice asks for colored glass: the tint strength is raised
    // to kTransparentTintFloor if it is lower (and the trackbar follows).
    void RaiseTintForClearGlass();
    void CheckModeRadios();
    void InvalidateSwatches() const;
    void DrawSwatch(const DRAWITEMSTRUCT& item) const;
    void DrawTransparentSwatch(const DRAWITEMSTRUCT& item) const;
    void DrawPreview(const DRAWITEMSTRUCT& item) const;
    void Changed();
    [[nodiscard]] Rgb TintRgb() const;
    [[nodiscard]] std::wstring LoadText(UINT id, const wchar_t* fallback) const;

    HINSTANCE m_instance;
    std::function<void(HWND)> m_registerDialog;
    std::function<void(const AppearanceSettings&)> m_changed;
    std::function<void(bool)> m_openChanged;

    HWND m_dialog = nullptr;
    HWND m_owner = nullptr;
    RECT m_anchor{}; // the picker button in screen pixels, as last shown or repositioned
    AppearanceSettings m_settings;
    EffectiveAppearance m_effective;
    RenderingCapabilities m_capabilities;
    bool m_syncing = false;        // controls are being set from m_settings
    bool m_inCustomDialog = false; // ChooseColorW is open: deactivation must not dismiss
    DWORD m_dismissedAt = 0;       // GetTickCount of the last dismissal by deactivation
    bool m_swallowing = false;     // a swallowed WM_LBUTTONDOWN awaits its WM_LBUTTONUP
};

} // namespace te
