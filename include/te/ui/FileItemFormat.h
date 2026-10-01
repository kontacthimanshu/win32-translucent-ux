#pragma once

// Display text for the file list's Size and Date modified columns (T056), in the user's
// locale, as Explorer shows them. Missing values give empty strings.

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

namespace te::FileItemFormat
{

// "0 bytes", "1.00 KB", "12.3 MB"... (StrFormatByteSizeEx, rounded to the nearest
// displayed digit). Empty for folders and virtual items (no size).
std::wstring Size(const std::optional<std::uint64_t>& bytes);

// Short date and time without seconds in the user's locale, e.g. "3/15/2024 12:34 PM".
// The UTC time is converted with the time-zone rules in effect on that date, so a
// summer date keeps its summer offset in winter (unlike FileTimeToLocalFileTime).
std::wstring Modified(const std::optional<FILETIME>& utc);

} // namespace te::FileItemFormat
