#pragma once

// The filter box (T092; FR-021, SHOULD; spec: "search" means filtering the current folder):
// at the right end of the toolbar row. Ctrl+F focuses it; typing filters the file list by
// name (case-insensitive substring, FileView::SetFilter); Escape clears it and gives the
// keyboard back to the list; Enter keeps the filter and goes back to the list. Like the
// address bar, it is drawn with Direct2D over the translucent toolbar, and while it has
// the keyboard a layered, colour-keyed EDIT takes the typing (research R-02, T061).

#include <te/appearance/AppearanceSettings.h>
#include <te/render/TextFormats.h>

#include <windows.h>

#include <d2d1_1.h>

#include <wil/resource.h>

#include <functional>
#include <string>

namespace te
{

class FilterBox
{
  public:
    static constexpr float kWidthDip = 200.0f;
    static constexpr float kFieldHeightDip = 28.0f;
    static constexpr float kTextInsetDip = 8.0f;
    static constexpr float kCornerRadiusDip = 4.0f;
    static constexpr int kEditControlId = 0x4649; // 'FI'

    FilterBox() = default;
    ~FilterBox();
    FilterBox(const FilterBox&) = delete;
    FilterBox& operator=(const FilterBox&) = delete;

    void Attach(HWND parent, UINT dpi) noexcept;
    void SetDpi(UINT dpi);
    void SetTextFormats(const TextFormats* formats) noexcept;
    // "Filter" (IDS_FILTER_PLACEHOLDER): the placeholder, and the edit's UIA name.
    void SetPlaceholder(std::wstring text);
    // Called with the filter text after every change (empty: no filter).
    void SetChangedCallback(std::function<void(const std::wstring&)> callback);
    // Enter or Escape: the keyboard goes back to the file list.
    void SetDoneCallback(std::function<void()> callback);

    void SetBounds(const D2D1_RECT_F& bounds);
    [[nodiscard]] D2D1_RECT_F FieldRect() const noexcept;
    void Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective);
    void ApplyAppearance(const EffectiveAppearance& effective);

    // Ctrl+F or a click: the edit shows and takes the keyboard, its text selected.
    void Focus();
    // Clears the filter (Escape, or a new folder) and reports the change once.
    void Clear();
    [[nodiscard]] bool IsEditing() const noexcept
    {
        return m_editing;
    }
    [[nodiscard]] const std::wstring& Text() const noexcept
    {
        return m_text;
    }
    [[nodiscard]] HWND Edit() const noexcept
    {
        return m_edit;
    }

    // Parent messages the owner forwards: WM_CTLCOLOREDIT and WM_COMMAND(EN_CHANGE).
    bool HandleCtlColor(HWND control, HDC dc, LRESULT* result);
    bool OnCommand(HWND control, UINT code);
    bool OnPointerDown(D2D1_POINT_2F point);
    void OnTextScaleChanged();
    void SetFocusVisible(bool visible);
    // WM_DESTROY: annotations are cleared before the edit goes.
    void Detach();

  private:
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR data);
    bool EnsureEdit();
    void PositionEdit();
    void UpdateFont();
    void EndEdit();
    void Invalidate() const;
    [[nodiscard]] float ToPx(float dip) const noexcept;
    [[nodiscard]] float FieldHeight() const noexcept;

    HWND m_parent = nullptr;
    HWND m_edit = nullptr;
    UINT m_dpi = USER_DEFAULT_SCREEN_DPI;
    const TextFormats* m_formats = nullptr;
    wil::unique_hfont m_font;
    wil::unique_hbrush m_keyBrush;
    COLORREF m_key = RGB(0xF3, 0xF3, 0xF3);
    COLORREF m_textColor = RGB(0, 0, 0);
    bool m_ownText = false; // Transparent: the edit's text is keyed out and drawn by Render (EditHalo)
    bool m_editing = false;
    bool m_ending = false;
    bool m_focusVisible = true;
    bool m_settingText = false; // SetWindowTextW in progress: no change report
    std::wstring m_text;
    std::wstring m_placeholder = L"Filter";
    D2D1_RECT_F m_bounds{};
    std::function<void(const std::wstring&)> m_changed;
    std::function<void()> m_done;
};

} // namespace te
