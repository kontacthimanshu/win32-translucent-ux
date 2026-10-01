#pragma once

// The inline-rename field over a file-list name cell (T071; research R-02, R-08; UI
// contract §4). A child EDIT created layered and colour-keyed on the typical surface
// colour, exactly like the address edit (T061): a plain GDI EDIT in the glass area would
// draw transparent text (spike-rendering.md, finding b).
//
// Enter validates with FileOperationService::ValidateNewName. An invalid name shows a
// balloon tip and editing continues; a valid, changed name is handed to the commit
// callback; an unchanged name, Escape or an invalid name at focus loss cancels.

#include <te/appearance/AppearanceSettings.h>
#include <te/render/TextFormats.h>

#include <windows.h>

#include <d2d1_1.h>

#include <wil/resource.h>

#include <functional>
#include <string>

namespace te
{

class RenameEdit
{
  public:
    static constexpr int kEditControlId = 0x524E; // 'RN'

    struct Callbacks
    {
        std::function<void(const std::wstring& newName)> commit; // a valid, changed name
        std::function<void()> ended;                             // after commit or cancel
    };

    RenameEdit() = default;
    ~RenameEdit();
    RenameEdit(const RenameEdit&) = delete;
    RenameEdit& operator=(const RenameEdit&) = delete;

    void Attach(HWND parent, UINT dpi) noexcept;
    void SetDpi(UINT dpi);
    void SetTextFormats(const TextFormats* formats) noexcept;
    // "A file name can't contain any of the following characters: ..." (IDS_ERR_BAD_NAME).
    void SetBadNameText(std::wstring text);
    // The UI Automation name of the edit (IDS_A11Y_RENAME, "Name"), set with Dynamic
    // Annotation together with the AutomationId "RenameEdit" (T083).
    void SetAutomationName(std::wstring name);

    // Starts editing `name` over `cellDip` (client-area DIPs). Files select the name
    // without its extension, folders the whole name. False if there is no parent window.
    bool Begin(const std::wstring& name, bool isFolder, const D2D1_RECT_F& cellDip, Callbacks callbacks);
    // Follows the cell when the list scrolls or resizes.
    void Move(const D2D1_RECT_F& cellDip);
    // Ends editing without committing.
    void Cancel();

    [[nodiscard]] bool IsActive() const noexcept
    {
        return m_active;
    }
    [[nodiscard]] HWND Edit() const noexcept
    {
        return m_edit;
    }
    // The text currently in the field.
    [[nodiscard]] std::wstring Text() const;
    // Whether the last Enter was rejected (the balloon tip was shown).
    [[nodiscard]] bool LastRejected() const noexcept
    {
        return m_rejected;
    }

    // The owner forwards WM_CTLCOLOREDIT and appearance changes, as for the address edit.
    bool HandleCtlColor(HWND control, HDC dc, LRESULT* result);
    // Transparent: the edit's text, white with a halo, under the keyed-out edit (EditHalo).
    void Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective) const;
    void ApplyAppearance(const EffectiveAppearance& effective);
    // The edit's font from the body format's size at the current DPI and text scale; call
    // after the text formats were rebuilt (the Windows text size changed, T082).
    void UpdateFont();

  private:
    bool EnsureEdit();
    void Commit(bool fromEnter);
    void End();
    [[nodiscard]] float ToPx(float dip) const noexcept;
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                     DWORD_PTR data);

    HWND m_parent = nullptr;
    HWND m_edit = nullptr;
    UINT m_dpi = USER_DEFAULT_SCREEN_DPI;
    const TextFormats* m_formats = nullptr;
    std::wstring m_badNameText =
        L"A file name can't contain any of the following characters:\r\n\\ / : * ? \" < > |";
    std::wstring m_original;
    std::wstring m_automationName = L"Name";
    Callbacks m_callbacks;
    bool m_active = false;
    bool m_ending = false;
    bool m_rejected = false;
    COLORREF m_key = RGB(0xF3, 0xF3, 0xF3);
    COLORREF m_textColor = RGB(0, 0, 0);
    bool m_ownText = false; // Transparent: the edit's text is keyed out and drawn by Render (EditHalo)
    wil::unique_hfont m_font;
    wil::unique_hbrush m_keyBrush;
};

} // namespace te
