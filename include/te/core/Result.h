#pragma once

// Turning HRESULTs into text the UI can show (UI contract §6, research R-08).

#include <te/core/Types.h>

#include <windows.h>

#include <string>
#include <string_view>

namespace te
{

// The system message for hr in the user's language, with trailing whitespace
// and line breaks removed, e.g. "Access is denied.". HRESULTs wrapping a Win32
// error are looked up by their Win32 code if the full value has no message.
// Codes with no system message give "Unknown error (0x8XXXXXXX)".
[[nodiscard]] std::wstring HresultMessage(HRESULT hr);

// A Status whose message is "<context>: <HresultMessage(hr)>", or just the
// system message when context is empty.
[[nodiscard]] Status MakeStatus(HRESULT hr, std::wstring_view context);

} // namespace te
