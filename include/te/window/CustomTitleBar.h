#pragma once

// The custom DWM frame (research R-03, UI contract §1–§2): the standard caption is
// removed and the frame is extended over the whole client area, so the DWM keeps
// drawing the native caption buttons while the app draws the icon, title and
// (from US2) the picker itself.

#include <te/appearance/AppearanceSettings.h>
#include <te/window/ICaptionHitTester.h>
#include <te/window/IDpiManager.h>

#include <windows.h>

#include <d2d1_1.h>
#include <dwrite.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <array>
#include <functional>
#include <string>

namespace te
{

class CustomTitleBar
{
  public:
    // Minimum client-area budget in DIPs (UI contract §1).
    static constexpr float kMinTitleWidthDip = 160.0f; // beyond caption buttons + picker + margin
    static constexpr float kToolbarHeightDip = 40.0f;
    static constexpr float kRowHeightDip = 28.0f;
    static constexpr int kMinVisibleRows = 5;
    static constexpr float kStatusBarHeightDip = 24.0f;
    static constexpr float kIconSizeDip = 16.0f;
    static constexpr float kIconMarginDip = 12.0f;  // left edge to icon
    static constexpr float kIconTitleGapDip = 8.0f; // icon to title text

    // Both references must outlive this object.
    CustomTitleBar(ICaptionHitTester& hitTester, IDpiManager& dpi);

    // Frame messages: WM_CREATE and WM_DWMCOMPOSITIONCHANGED (extend the frame),
    // WM_NCCALCSIZE (remove the caption), WM_NCHITTEST (R-03 order),
    // WM_GETMINMAXINFO (minimum size) and WM_GETDPISCALEDSIZE (exact scaling, T081). Returns true and sets
    // *result when the message is fully handled; WM_CREATE returns false so the caller continues.
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result);

    // The non-client frame this window keeps at `dpi` (WM_NCCALCSIZE gives the caption to
    // the client area; the left, right and bottom resize borders stay), both borders summed.
    [[nodiscard]] static SIZE FrameSizeForDpi(HWND hwnd, UINT dpi);

    // Recomputes the caption layout. Call on WM_SIZE, WM_DPICHANGED and after
    // the frame changes.
    void UpdateLayout(HWND hwnd);
    [[nodiscard]] const CaptionLayout& Layout() const noexcept
    {
        return m_layout;
    }

    // WM_PAINT: fills the GDI redirection surface with black, which is zero alpha
    // inside the extended frame, so the backdrop shows through (research R-02).
    // The Direct2D content is drawn separately by the render pass.
    static void PaintRedirectionSurface(HWND hwnd);

    void SetTitle(std::wstring title);
    // The Windows text size (T082): the minimum height keeps five rows of the grown text.
    void SetTextScale(float scale) noexcept;
    // Small application icon; the caller keeps ownership.
    void SetIcon(HICON icon);
    // Drops the Direct2D resources made on the current device (WM_DESTROY, T088).
    void ReleaseDeviceResources() noexcept
    {
        m_iconBitmap.reset();
        m_textBrush.reset();
        m_resourceDevice.reset();
    }

    // Draws the icon and the title (trimmed with an ellipsis) inside the drag
    // region, in effective.text. The legibility floor is painted behind TitleArea()
    // first (SurfacePainter::PaintTextScrim, research R-05). Nothing is drawn over the
    // picker or the caption buttons, except in Transparent mode (effective.textHalo): a
    // halo around the DWM's caption-button glyphs, their own pixels left clear.
    void Render(ID2D1DeviceContext* dc, IDWriteTextFormat* titleFormat, const EffectiveAppearance& effective);

    // The picker button (T046, UI contract §2): a 14-DIP circle in the tint color with a
    // 1-DIP ring, and hover / pressed fills like the caption buttons. A click (press and
    // release inside it) calls `onActivate`; the owner toggles the appearance popup.
    static constexpr float kPickerDiameterDip = 14.0f;
    static constexpr float kCaptionGlyphSizeDip = 10.0f; // the DWM caption-button glyphs
    void SetPickerCallback(std::function<void()> onActivate);
    // The picker rectangle in screen coordinates (the popup's anchor).
    [[nodiscard]] RECT PickerScreenRect(HWND hwnd) const;
    [[nodiscard]] bool PickerHot() const noexcept
    {
        return m_pickerHot;
    }
    // The picker has the keyboard focus (F6 ring, T080), and whether focus cues show.
    void SetPickerFocused(bool focused, bool visible);
    [[nodiscard]] bool PickerFocused() const noexcept
    {
        return m_pickerFocused;
    }
    [[nodiscard]] bool PickerPressed() const noexcept
    {
        return m_pickerPressed;
    }

    // The slab button, left of the picker: a small slab of glass drawn in perspective, with
    // the picker's hover and pressed fills. A click calls `onActivate`; the owner toggles the
    // slab-thickness popup.
    void SetSlabCallback(std::function<void()> onActivate);
    // The slab button rectangle in screen coordinates (the popup's anchor).
    [[nodiscard]] RECT SlabScreenRect(HWND hwnd) const;

    // The icon-and-title text area in DIPs: the caption strip from the left edge to the
    // end of the drag region (not over the picker or the caption buttons).
    [[nodiscard]] D2D1_RECT_F TitleArea() const;

  private:
    static void ExtendFrame(HWND hwnd);
    void ApplyMinimumSize(HWND hwnd, MINMAXINFO* info) const;
    [[nodiscard]] float ToDip(LONG px) const;
    HRESULT EnsureIconBitmap(ID2D1DeviceContext* dc);
    bool HandlePickerMouse(HWND hwnd, UINT msg, LPARAM lParam);
    // One title-bar button's pointer input (the picker and the slab button share it).
    bool HandleButtonMouse(HWND hwnd, UINT msg, POINT point, const RECT& rect, bool& hot, bool& pressed,
                           const std::function<void()>& onActivate);
    void RenderPicker(ID2D1DeviceContext* dc, const EffectiveAppearance& effective);
    void RenderSlabButton(ID2D1DeviceContext* dc, const EffectiveAppearance& effective);
    void RenderCaptionButtonHalos(ID2D1DeviceContext* dc, const EffectiveAppearance& effective);
    // Where the DWM puts Minimize, Maximize and Close inside layout.captionButtons, found by
    // hit-testing it with DwmDefWindowProc (the reported bounds are wider than the three
    // buttons, and their width varies with the DPI and the Windows version). Equal thirds
    // of the bounds when the DWM does not answer, e.g. before the window is shown.
    void ProbeCaptionButtons(HWND hwnd);
    [[nodiscard]] bool InPicker(POINT client) const noexcept;

    ICaptionHitTester& m_hitTester;
    IDpiManager& m_dpi;
    CaptionLayout m_layout;
    std::wstring m_title;
    HICON m_icon = nullptr;

    std::function<void()> m_onPicker;
    bool m_pickerHot = false;                        // pointer over the picker (TrackMouseEvent for leave)
    bool m_pickerPressed = false;                    // left button went down on the picker; mouse captured
    bool m_pickerFocused = false;                    // keyboard focus (T080)
    std::function<void()> m_onSlab;
    bool m_slabHot = false;
    bool m_slabPressed = false;
    wil::com_ptr<IDWriteFactory> m_dwrite;           // the shared factory, for the title layout
    wil::com_ptr<IDWriteTextFormat> m_captionGlyphs; // the caption buttons' icon font
    std::array<RECT, 3> m_buttonRects{};             // Minimize, Maximize, Close; client px
    float m_textScale = 1.0f;
    bool m_focusVisible = true;
    bool m_trackingLeave = false;

    // Device-dependent: recreated when the Direct2D device (or, for the icon, the
    // DPI) changes.
    wil::com_ptr<ID2D1Device> m_resourceDevice;
    UINT m_iconDpi = 0;
    wil::com_ptr<ID2D1Bitmap> m_iconBitmap;
    wil::com_ptr<ID2D1SolidColorBrush> m_textBrush;
};

} // namespace te
