#include <te/ui/FileItemFormat.h>

#include <shlwapi.h>

#include <wil/result.h>

namespace te::FileItemFormat
{

std::wstring Size(const std::optional<std::uint64_t>& bytes)
{
    if (!bytes)
    {
        return {};
    }
    wchar_t text[64]{};
    if (FAILED_LOG(StrFormatByteSizeEx(*bytes, SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT, text,
                                       static_cast<UINT>(std::size(text)))))
    {
        return {};
    }
    return text;
}

std::wstring Modified(const std::optional<FILETIME>& utc)
{
    if (!utc)
    {
        return {};
    }
    // FileTimeToLocalFileTime would apply today's daylight-saving bias to every date;
    // converting through SYSTEMTIME uses the rules for the date itself, as Explorer does.
    SYSTEMTIME utcTime{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&*utc, &utcTime) || !SystemTimeToTzSpecificLocalTime(nullptr, &utcTime, &local))
    {
        return {};
    }

    wchar_t date[80]{};
    wchar_t time[80]{};
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date,
                        static_cast<int>(std::size(date)), nullptr) == 0 ||
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time,
                        static_cast<int>(std::size(time))) == 0)
    {
        return {};
    }
    return std::wstring(date) + L' ' + time;
}

} // namespace te::FileItemFormat
