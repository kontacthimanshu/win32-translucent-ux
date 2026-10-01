#pragma once

// The status bar (T038; UI contract §1, §3, §6). Left: an operation message, else a
// transient message, else the item counts (empty until US3). Right: the applied mode,
// surface opacity and tint strength, plus "(fallback: <reason>)" when the applied mode
// differs from the requested one.

#include <te/render/TextFormats.h>
#include <te/ui/IStatusBar.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace te
{

class StatusBar final : public IStatusBar
{
  public:
    // Resource IDs of the STRINGTABLE entries (resources/resource.h, which only the
    // executable includes); Load() reads them.
    struct StringIds
    {
        UINT modeAcrylic = 0, modeMica = 0, modeSolid = 0, modeTransparent = 0;
        UINT appearanceFmt = 0, appearanceSolidFmt = 0, fallbackFmt = 0;
        UINT fallbackHighContrast = 0, fallbackTransparencyOff = 0, fallbackUnsupported = 0,
             fallbackApplyFailed = 0;
        UINT itemsFmt = 0, selectedFmt = 0;
    };

    // FormatMessage templates and words. The defaults match the English STRINGTABLE, so
    // the status bar works (and is testable) without the executable's resources.
    struct Strings
    {
        std::wstring modeAcrylic = L"Acrylic";
        std::wstring modeMica = L"Mica";
        std::wstring modeSolid = L"Solid";
        std::wstring modeTransparent = L"Transparent";
        std::wstring appearanceFmt = L"%1 · Surface %2!u!%% · Tint %3!u!%%";
        std::wstring appearanceSolidFmt = L"%1";
        std::wstring fallbackFmt = L"(fallback: %1)";
        std::wstring fallbackHighContrast = L"high contrast";
        std::wstring fallbackTransparencyOff = L"transparency effects off";
        std::wstring fallbackUnsupported = L"requires Windows 11 build 22621";
        std::wstring fallbackApplyFailed = L"backdrop unavailable";
        std::wstring itemsFmt = L"%1!u! items";
        std::wstring selectedFmt = L"%1!u! selected";

        // Each non-zero ID replaces the default when the resource string exists.
        static Strings Load(HINSTANCE instance, const StringIds& ids);
    };

    // Horizontal padding inside the bar.
    static constexpr float kPaddingDip = 12.0f;
    // WM_TIMER id used on the owner window for transient messages.
    static constexpr UINT_PTR kTransientTimerId = 0x5445; // 'TE'

    explicit StatusBar(Strings strings = {});
    ~StatusBar() override;

    StatusBar(const StatusBar&) = delete;
    StatusBar& operator=(const StatusBar&) = delete;

    // The window that is repainted when the text changes and that owns the transient
    // message timer; its WM_TIMER handler calls OnTimer. May be nullptr (tests).
    void Attach(HWND owner) noexcept;
    // The formats come from the window's TextFormats (Status()); it must outlive this.
    void SetTextFormats(const TextFormats* formats) noexcept;
    // Returns true if timerId was the transient-message timer (the message is cleared).
    bool OnTimer(UINT_PTR timerId);

    // IStatusBar
    void SetItemCounts(std::size_t items, std::size_t selected) override;
    void SetAppearance(const AppearanceSettings& requested, const EffectiveAppearance& effective) override;
    void SetTransientMessage(std::wstring text, UINT durationMs) override;
    // Called after a transient or operation message is set (not for item counts), so the
    // owner can announce it (UI Automation LiveRegionChanged, T079).
    void SetMessageCallback(std::function<void()> callback)
    {
        m_messageSet = std::move(callback);
    }
    void SetOperationMessage(std::wstring text) override;
    void ClearOperationMessage() override;
    void SetBounds(const D2D1_RECT_F& bounds) override;
    void Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective) override;
    // UI Automation for the status bar is added by T078/T079.
    IRawElementProviderFragment* Automation() override;

    // The texts as they are drawn (also the accessible name source for T078).
    [[nodiscard]] std::wstring LeftText() const;
    [[nodiscard]] const std::wstring& RightText() const noexcept
    {
        return m_rightText;
    }
    [[nodiscard]] const D2D1_RECT_F& Bounds() const noexcept
    {
        return m_bounds;
    }

    // Right-hand text for an appearance (pure; unit-tested).
    static std::wstring FormatAppearance(const Strings& strings, const AppearanceSettings& requested,
                                         const EffectiveAppearance& effective);

  private:
    void Invalidate() const;

    Strings m_strings;
    HWND m_owner = nullptr;
    const TextFormats* m_formats = nullptr;
    D2D1_RECT_F m_bounds{};

    std::optional<std::size_t> m_items;
    std::size_t m_selected = 0;
    std::wstring m_rightText;
    std::wstring m_transient;
    std::wstring m_operation;
    std::function<void()> m_messageSet;
};

} // namespace te
