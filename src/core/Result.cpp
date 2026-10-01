#include <te/core/Result.h>

#include <wil/resource.h>

#include <cwchar>
#include <cwctype>
#include <utility>

namespace te
{

namespace
{

// FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM) for one message id; empty if the
// system has no text for it.
std::wstring SystemMessage(DWORD messageId)
{
    wil::unique_hlocal_string buffer;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, messageId, 0, reinterpret_cast<PWSTR>(&buffer), 0, nullptr);
    if (length == 0 || !buffer)
    {
        return {};
    }

    std::wstring text(buffer.get(), length);
    while (!text.empty() && std::iswspace(text.back()))
    {
        text.pop_back();
    }
    return text;
}

} // namespace

std::wstring HresultMessage(HRESULT hr)
{
    std::wstring text = SystemMessage(static_cast<DWORD>(hr));
    if (text.empty() && HRESULT_FACILITY(hr) == FACILITY_WIN32)
    {
        text = SystemMessage(static_cast<DWORD>(HRESULT_CODE(hr)));
    }
    if (text.empty())
    {
        wchar_t fallback[40]{};
        swprintf_s(fallback, L"Unknown error (0x%08X)", static_cast<unsigned int>(hr));
        text = fallback;
    }
    return text;
}

Status MakeStatus(HRESULT hr, std::wstring_view context)
{
    std::wstring message = HresultMessage(hr);
    if (!context.empty())
    {
        message.insert(0, L": ");
        message.insert(0, context);
    }
    return Status{hr, std::move(message)};
}

} // namespace te
