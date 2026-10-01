#pragma once

// The toolbar's navigation buttons: Back, Forward, Up and Refresh (T062; UI contract §1,
// §4). Windowless Direct2D glyph buttons with hover, pressed and disabled states, and
// real tooltips (TOOLTIPS_CLASSW, TTF_SUBCLASS). The address bar sits to their right.

#include <te/appearance/AppearanceSettings.h>
#include <te/render/TextFormats.h>

#include <windows.h>

#include <d2d1_1.h>
#include <dwrite.h>

#include <wil/com.h>

#include <array>
#include <functional>
#include <optional>
#include <string>

namespace te
{

class Toolbar
{
  public:
    enum class Button
    {
        Back,
        Forward,
        Up,
        Refresh,
    };
    static constexpr std::size_t kButtonCount = 4;

    static constexpr float kButtonSizeDip = 32.0f;
    static constexpr float kButtonGapDip = 2.0f;
    static constexpr float kLeftPaddingDip = 8.0f;
    static constexpr float kGlyphSizeDip = 16.0f;
    static constexpr float kCornerRadiusDip = 4.0f;

    struct Strings
    {
        std::wstring back = L"Back (Alt+Left Arrow)";
        std::wstring forward = L"Forward (Alt+Right Arrow)";
        std::wstring up = L"Up to the parent folder (Alt+Up Arrow)";
        std::wstring refresh = L"Refresh (F5)";
    };

    explicit Toolbar(Strings strings = {});
    ~Toolbar();

    Toolbar(const Toolbar&) = delete;
    Toolbar& operator=(const Toolbar&) = delete;

    // The window the tooltips belong to (created here) and that is repainted.
    void Attach(HWND parent, UINT dpi);
    void SetDpi(UINT dpi);
    void SetTextFormats(const TextFormats* formats);
    void SetCallback(std::function<void(Button)> onClick);

    // Enabled states come from NavigationHistory (Back, Forward) and the current
    // location's parent (Up). Refresh is always enabled.
    void SetState(bool canBack, bool canForward, bool canUp);

    // The toolbar row in DIPs; the buttons are laid out from its left edge.
    void SetBounds(const D2D1_RECT_F& bounds);
    // Width taken by the buttons (the address bar starts after it).
    [[nodiscard]] static constexpr float ButtonsWidth() noexcept
    {
        return kLeftPaddingDip + kButtonCount * kButtonSizeDip + (kButtonCount - 1) * kButtonGapDip;
    }
    [[nodiscard]] D2D1_RECT_F ButtonRect(Button button) const noexcept;
    [[nodiscard]] std::optional<Button> ButtonAt(D2D1_POINT_2F point) const noexcept;
    [[nodiscard]] bool IsEnabled(Button button) const noexcept
    {
        return m_enabled[static_cast<std::size_t>(button)];
    }
    [[nodiscard]] std::optional<Button> Hot() const noexcept
    {
        return m_hot;
    }
    [[nodiscard]] std::optional<Button> Pressed() const noexcept
    {
        return m_pressed;
    }
    [[nodiscard]] HWND Tooltip() const noexcept
    {
        return m_tooltip;
    }
    // The buttons' tooltip texts, e.g. "Back (Alt+Left Arrow)" (also their UIA names, T078).
    [[nodiscard]] const Strings& Texts() const noexcept
    {
        return m_strings;
    }
    // "Segoe Fluent Icons", or "Segoe MDL2 Assets" where Fluent is not installed.
    [[nodiscard]] const std::wstring& GlyphFamily() const noexcept
    {
        return m_glyphFamily;
    }

    // Input in DIPs. A click is press and release on the same enabled button.
    bool OnPointerDown(D2D1_POINT_2F point);
    bool OnPointerMove(D2D1_POINT_2F point);
    bool OnPointerUp(D2D1_POINT_2F point);
    void OnPointerLeave();

    void Render(ID2D1DeviceContext* dc, const EffectiveAppearance& effective);

  private:
    void UpdateTooltipRects();
    HRESULT EnsureGlyphFormat();
    void Invalidate() const;

    Strings m_strings;
    std::function<void(Button)> m_onClick;
    HWND m_parent = nullptr;
    HWND m_tooltip = nullptr;
    UINT m_dpi = USER_DEFAULT_SCREEN_DPI;
    const TextFormats* m_formats = nullptr;
    wil::com_ptr<IDWriteTextFormat> m_glyphs;
    std::wstring m_glyphFamily;

    D2D1_RECT_F m_bounds{};
    std::array<bool, kButtonCount> m_enabled{false, false, false, true};
    std::optional<Button> m_hot;
    std::optional<Button> m_pressed;
};

} // namespace te
