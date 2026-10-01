#include <te/render/TextFormats.h>

#include <wil/result.h>

#include <algorithm>
#include <utility>

namespace te
{

namespace
{

// "Segoe UI Variable" is listed in the system collection under its optical-size
// names; "... Text" is the one meant for UI text at these sizes.
constexpr const wchar_t* kFamilyCandidates[] = {L"Segoe UI Variable Text", L"Segoe UI Variable", L"Segoe UI"};

std::wstring PickFamily(IDWriteFactory3* factory)
{
    wil::com_ptr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(factory->GetSystemFontCollection(fonts.put())))
    {
        for (const wchar_t* candidate : kFamilyCandidates)
        {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fonts->FindFamilyName(candidate, &index, &exists)) && exists)
            {
                return candidate;
            }
        }
    }
    return L"Segoe UI";
}

std::wstring UserLocale()
{
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    return GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0 ? name : L"en-us";
}

} // namespace

TextFormats::TextFormats(IDWriteFactory3* factory) : m_factory(factory)
{
    if (m_factory)
    {
        m_family = PickFamily(m_factory.get());
    }
    m_locale = UserLocale();
}

HRESULT TextFormats::Rebuild(float textScale)
{
    RETURN_HR_IF(E_UNEXPECTED, !m_factory);
    const float scale = std::clamp(textScale, kMinTextScale, kMaxTextScale);

    wil::com_ptr<IDWriteTextFormat> title;
    wil::com_ptr<IDWriteTextFormat> body;
    wil::com_ptr<IDWriteTextFormat> header;
    wil::com_ptr<IDWriteTextFormat> status;
    RETURN_IF_FAILED(CreateFormat(kTitleSize * scale, DWRITE_FONT_WEIGHT_NORMAL, title.put()));
    RETURN_IF_FAILED(CreateFormat(kBodySize * scale, DWRITE_FONT_WEIGHT_NORMAL, body.put()));
    RETURN_IF_FAILED(CreateFormat(kHeaderSize * scale, DWRITE_FONT_WEIGHT_NORMAL, header.put()));
    RETURN_IF_FAILED(CreateFormat(kStatusSize * scale, DWRITE_FONT_WEIGHT_NORMAL, status.put()));

    // Replace only once all four succeeded, so a failure keeps the old set usable.
    m_title = std::move(title);
    m_body = std::move(body);
    m_header = std::move(header);
    m_status = std::move(status);
    m_textScale = scale;
    return S_OK;
}

HRESULT TextFormats::CreateFormat(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** format) const
{
    wil::com_ptr<IDWriteTextFormat> created;
    RETURN_IF_FAILED(m_factory->CreateTextFormat(m_family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                                 DWRITE_FONT_STRETCH_NORMAL, size, m_locale.c_str(),
                                                 created.put()));

    // Single-line UI text: no wrapping, vertically centred in its box, and cut
    // with an ellipsis when too long (e.g. the title in narrow windows, T022).
    RETURN_IF_FAILED(created->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
    RETURN_IF_FAILED(created->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    wil::com_ptr<IDWriteInlineObject> ellipsis;
    RETURN_IF_FAILED(m_factory->CreateEllipsisTrimmingSign(created.get(), ellipsis.put()));
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    RETURN_IF_FAILED(created->SetTrimming(&trimming, ellipsis.get()));

    *format = created.detach();
    return S_OK;
}

} // namespace te
