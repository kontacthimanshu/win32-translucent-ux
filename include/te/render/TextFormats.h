#pragma once

// DirectWrite text formats for the UI (T019; FR-016 text scaling).
//
// Sizes are in DIPs. The Direct2D device context applies the monitor DPI, so a
// DPI change needs no new formats; the Windows text-size setting
// (UISettings.TextScaleFactor) does, so Rebuild() takes it.

#include <windows.h>

#include <dwrite_3.h>

#include <wil/com.h>

#include <string>

namespace te
{

class TextFormats
{
  public:
    // Base sizes in DIPs at 100% text scale (the Windows 11 Explorer body size).
    static constexpr float kTitleSize = 12.0f;
    static constexpr float kBodySize = 12.0f;
    static constexpr float kHeaderSize = 12.0f;
    static constexpr float kStatusSize = 12.0f;

    // Windows allows text scaling from 100% to 225%.
    static constexpr float kMinTextScale = 1.0f;
    static constexpr float kMaxTextScale = 2.25f;

    // factory is typically IRenderDevice::DWrite(); it must outlive this object.
    explicit TextFormats(IDWriteFactory3* factory);

    // (Re)creates all formats at base size × textScale (clamped to the Windows
    // range). Call at startup, on TextScaleFactorChanged and on WM_DPICHANGED.
    HRESULT Rebuild(float textScale);

    [[nodiscard]] IDWriteTextFormat* Title() const noexcept
    {
        return m_title.get();
    }
    [[nodiscard]] IDWriteTextFormat* Body() const noexcept
    {
        return m_body.get();
    }
    [[nodiscard]] IDWriteTextFormat* Header() const noexcept
    {
        return m_header.get();
    }
    [[nodiscard]] IDWriteTextFormat* Status() const noexcept
    {
        return m_status.get();
    }

    // The family actually in use: "Segoe UI Variable Text", "Segoe UI Variable"
    // or "Segoe UI", whichever the system font collection has first.
    // The DirectWrite factory the formats were made with (e.g. for trimming signs).
    [[nodiscard]] IDWriteFactory3* Factory() const noexcept
    {
        return m_factory.get();
    }

    [[nodiscard]] const std::wstring& FamilyName() const noexcept
    {
        return m_family;
    }
    [[nodiscard]] float TextScale() const noexcept
    {
        return m_textScale;
    }

  private:
    HRESULT CreateFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format) const;

    wil::com_ptr<IDWriteFactory3> m_factory;
    std::wstring m_family;
    std::wstring m_locale;
    float m_textScale = 1.0f;
    wil::com_ptr<IDWriteTextFormat> m_title;
    wil::com_ptr<IDWriteTextFormat> m_body;
    wil::com_ptr<IDWriteTextFormat> m_header;
    wil::com_ptr<IDWriteTextFormat> m_status;
};

} // namespace te
