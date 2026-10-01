#pragma once

// The address bar (T061; research R-02, spike-rendering.md; UI contract §4, §6).
// Display mode draws the location over the translucent toolbar with Direct2D. Editing
// shows a child EDIT that is layered and colour-keyed on the typical surface colour, so
// the translucent field shows through behind the text (spike result (d)).
//
// Breadcrumbs (T091; FR-021, SHOULD): in display mode the location is shown as its
// ancestors, "This PC > Local Disk (C:) > Users > ...". Clicking a segment navigates to
// it; the chevron after a segment lists its subfolders (FolderTreeLoader, then a menu);
// when the segments do not fit, the leading ones move behind an overflow button whose
// menu lists them. Clicking empty space in the field starts editing, as before.

#include <te/render/TextFormats.h>
#include <te/shell/ContextMenu.h>
#include <te/shell/FolderTreeLoader.h>
#include <te/ui/IAddressBar.h>

#include <wil/resource.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace te
{

class AddressBar final : public IAddressBar
{
  public:
    static constexpr float kFieldHeightDip = 28.0f;
    static constexpr float kTextInsetDip = 8.0f;
    static constexpr float kCornerRadiusDip = 4.0f;
    static constexpr float kErrorGapDip = 4.0f;
    static constexpr int kEditControlId = 0x4144; // 'AD'

    AddressBar() = default;
    ~AddressBar() override;

    AddressBar(const AddressBar&) = delete;
    AddressBar& operator=(const AddressBar&) = delete;

    // The window the edit is created in (created on first BeginEdit) and repainted.
    void Attach(HWND parent, UINT dpi) noexcept;
    void SetDpi(UINT dpi);
    void SetTextFormats(const TextFormats* formats) noexcept;
    // The UI Automation name of the native EDIT while editing (IDS_A11Y_ADDRESS). It is
    // set on the EDIT with Dynamic Annotation (IAccPropServices), together with the
    // AutomationId "AddressBar" (T078, T083).
    void SetAutomationName(std::wstring name);

    // Breadcrumbs (T091). A segment or a menu item navigates to its location.
    struct Crumb
    {
        ShellLocation location;
        std::wstring name;
        bool visible = false;   // false: behind the overflow button
        D2D1_RECT_F nameRect{}; // DIPs; empty while not laid out
        D2D1_RECT_F chevronRect{};
    };
    static constexpr float kCrumbPaddingDip = 6.0f;
    static constexpr float kChevronWidthDip = 16.0f;
    static constexpr float kOverflowWidthDip = 22.0f;
    // Node ids of the address bar's subfolder listings carry this bit, so the owner can
    // route WM_TE_TREE_CHILDREN results (the navigation pane's ids never have it).
    static constexpr std::uint64_t kNodeTag = std::uint64_t{1} << 63;

    void SetLocationCallback(std::function<void(const ShellLocation&)> callback);
    // Replaces TrackPopupMenuEx for the chevron and overflow menus (tests).
    void SetMenuTracker(ContextMenu::Track track);
    // WM_TE_TREE_CHILDREN for a node id with kNodeTag: shows the chevron's menu.
    void OnChildren(FolderChildren& result);
    [[nodiscard]] static bool OwnsNode(std::uint64_t nodeId) noexcept
    {
        return (nodeId & kNodeTag) != 0;
    }
    // Hover highlight of segments and chevrons.
    void OnPointerMove(D2D1_POINT_2F point);
    void OnPointerLeave();
    // WM_DESTROY (T088): subfolder listings are cancelled, then the worker stops.
    void CancelLoads();
    void ShutdownLoader();

    [[nodiscard]] const std::vector<Crumb>& Crumbs() const noexcept
    {
        return m_crumbs;
    }
    // The overflow button, when segments are hidden behind it.
    [[nodiscard]] std::optional<D2D1_RECT_F> OverflowRect() const noexcept
    {
        return m_overflow;
    }

    // IAddressBar
    void SetLocation(const ShellLocation& location) override;
    void BeginEdit() override;
    void CancelEdit() override;
    [[nodiscard]] bool IsEditing() const override;
    void SetNavigateCallback(std::function<void(std::wstring_view text)> callback) override;
    void ShowError(std::wstring_view message) override;
    void SetBounds(const D2D1_RECT_F& bounds) override;
    void Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective) override;
    IRawElementProviderFragment* Automation() override; // T078

    // The inline error, drawn after the file list so it is not covered (UI §6).
    void RenderOverlay(ID2D1DeviceContext* dc, const EffectiveAppearance& effective);

    // Parent window messages the owner forwards.
    // WM_CTLCOLOREDIT: the key-colour background makes the edit translucent.
    bool HandleCtlColor(HWND control, HDC dc, LRESULT* result);
    // A click in the field starts editing.
    bool OnPointerDown(D2D1_POINT_2F point);

    // Re-applies the colour key and text colour after the appearance changes.
    void ApplyAppearance(const EffectiveAppearance& effective);
    // Whether keyboard focus cues are shown: while editing, the field then has a 2-DIP
    // focus ring (T080).
    void SetFocusVisible(bool visible);

    [[nodiscard]] const std::wstring& DisplayText() const noexcept
    {
        return m_displayText;
    }
    [[nodiscard]] const std::wstring& Error() const noexcept
    {
        return m_error;
    }
    [[nodiscard]] HWND Edit() const noexcept
    {
        return m_edit;
    }
    [[nodiscard]] COLORREF ColorKey() const noexcept
    {
        return m_key;
    }
    // The field (rounded box) in DIPs; its height grows with the Windows text size.
    [[nodiscard]] D2D1_RECT_F FieldRect() const noexcept;
    // The text formats were rebuilt for a new Windows text size (T082): the edit gets the
    // new font and is placed in the resized field.
    void OnTextScaleChanged();

  private:
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR data);
    bool EnsureEdit();
    void PositionEdit();
    void UpdateFont();
    void AnnotateEdit();
    void ClearEditAnnotation();
    [[nodiscard]] float FieldHeight() const noexcept;
    void Commit();
    void EndEdit();
    void Invalidate() const;
    // Breadcrumbs (T091).
    void LayoutCrumbs();
    [[nodiscard]] float MeasureText(const std::wstring& text) const;
    // Shows a menu of `items` under `anchor`; navigates to the chosen one.
    void ShowLocationMenu(const std::vector<ShellLocation>& items, const D2D1_RECT_F& anchor);
    void DrawChevron(ID2D1DeviceContext* dc, ID2D1Brush* brush, const D2D1_RECT_F& rect, bool down,
                     const EffectiveAppearance& effective) const;
    [[nodiscard]] float ToPx(float dip) const noexcept;

    HWND m_parent = nullptr;
    HWND m_edit = nullptr;
    UINT m_dpi = USER_DEFAULT_SCREEN_DPI;
    const TextFormats* m_formats = nullptr;
    wil::unique_hfont m_font;
    wil::unique_hbrush m_keyBrush;
    bool m_focusVisible = true;
    std::wstring m_automationName = L"Address";
    COLORREF m_key = RGB(0xF3, 0xF3, 0xF3);
    COLORREF m_textColor = RGB(0, 0, 0);
    bool m_ownText = false; // Transparent: the edit's text is keyed out and drawn by Render (EditHalo)
    bool m_editing = false;
    bool m_ending = false; // guards WM_KILLFOCUS while the edit is hidden

    std::wstring m_displayText; // shown in display mode
    std::wstring m_editText;    // what editing starts with (path, or the display name)
    std::wstring m_error;
    D2D1_RECT_F m_bounds{};
    std::function<void(std::wstring_view)> m_navigate;

    // Breadcrumbs (T091).
    std::vector<Crumb> m_crumbs; // root first
    std::optional<D2D1_RECT_F> m_overflow;
    std::function<void(const ShellLocation&)> m_navigateTo;
    ContextMenu::Track m_track;
    FolderTreeLoader m_loader;
    std::uint64_t m_nextRequest = 0;
    std::optional<std::uint64_t> m_pendingMenu; // the listing the open chevron waits for
    std::optional<std::size_t> m_menuCrumb;     // the chevron that is open or loading
    std::optional<std::size_t> m_hotCrumb;      // hovered segment
    std::optional<std::size_t> m_hotChevron;    // hovered chevron
    bool m_hotOverflow = false;
};

} // namespace te
