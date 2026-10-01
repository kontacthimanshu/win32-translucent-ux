// FileItemFormat (T056): empty text for missing values, locale-formatted sizes and dates.
// Locale-dependent output is compared with the same Windows APIs, plus locale-free
// properties (digits of the year and minutes), so the tests pass in any locale.

#include <te/ui/FileItemFormat.h>

#include <shlwapi.h>

#include <gtest/gtest.h>

#include <string>

namespace
{

FILETIME Utc(WORD year, WORD month, WORD day, WORD hour, WORD minute)
{
    SYSTEMTIME st{};
    st.wYear = year;
    st.wMonth = month;
    st.wDay = day;
    st.wHour = hour;
    st.wMinute = minute;
    FILETIME ft{};
    EXPECT_TRUE(SystemTimeToFileTime(&st, &ft));
    return ft;
}

std::wstring ExpectedSize(std::uint64_t bytes)
{
    wchar_t text[64]{};
    EXPECT_HRESULT_SUCCEEDED(
        StrFormatByteSizeEx(bytes, SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT, text, 64));
    return text;
}

TEST(FileItemFormat, MissingValuesAreEmpty)
{
    EXPECT_EQ(te::FileItemFormat::Size(std::nullopt), L"");
    EXPECT_EQ(te::FileItemFormat::Modified(std::nullopt), L"");
}

TEST(FileItemFormat, SizeUsesTheShellByteFormat)
{
    for (const std::uint64_t bytes : {0ull, 7ull, 1023ull, 1024ull, 1536ull, 10'485'760ull, 5'368'709'120ull})
    {
        const std::wstring text = te::FileItemFormat::Size(bytes);
        EXPECT_EQ(text, ExpectedSize(bytes)) << bytes;
        EXPECT_FALSE(text.empty());
    }
}

TEST(FileItemFormat, SizesAreNotAllTheSameUnit)
{
    EXPECT_NE(te::FileItemFormat::Size(512), te::FileItemFormat::Size(512ull * 1024));
    EXPECT_NE(te::FileItemFormat::Size(512ull * 1024), te::FileItemFormat::Size(512ull * 1024 * 1024));
}

TEST(FileItemFormat, DateHasDateAndTimeWithoutSeconds)
{
    // 2024-03-15 12:34:56 UTC: the local minutes are still 34 (all zone offsets are
    // whole or half hours, and 34 + 30 would still not collide with the seconds).
    SYSTEMTIME st{2024, 3, 5, 15, 12, 34, 56, 0};
    FILETIME ft{};
    ASSERT_TRUE(SystemTimeToFileTime(&st, &ft));
    const std::wstring text = te::FileItemFormat::Modified(ft);
    ASSERT_FALSE(text.empty());
    EXPECT_NE(text.find(L"24"), std::wstring::npos) << "year";
    EXPECT_EQ(text.find(L"56"), std::wstring::npos) << "no seconds";
    EXPECT_NE(text.find(L' '), std::wstring::npos) << "date and time";
}

// Summer and winter dates each use their own offset, so the local hour matches what the
// time-zone rules give for that date, not today's offset.
TEST(FileItemFormat, EachDateUsesItsOwnDaylightSavingOffset)
{
    for (const FILETIME ft : {Utc(2024, 1, 15, 10, 0), Utc(2024, 7, 15, 10, 0)})
    {
        SYSTEMTIME utc{}, local{};
        ASSERT_TRUE(FileTimeToSystemTime(&ft, &utc));
        ASSERT_TRUE(SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local));
        wchar_t time[80]{};
        ASSERT_NE(GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, 80), 0);
        EXPECT_NE(te::FileItemFormat::Modified(ft).find(time), std::wstring::npos)
            << "expected local time " << ::testing::PrintToString(std::wstring(time));
    }
}

} // namespace
