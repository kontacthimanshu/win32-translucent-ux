#pragma once

// ISettingsStore implementation (T043; research R-11, R-12,
// contracts/settings.schema.json): %LOCALAPPDATA%\TranslucentExplorer\settings.json,
// validated per field, written atomically.

#include <te/settings/ISettingsStore.h>

#include <filesystem>

namespace te
{

class SettingsManager final : public ISettingsStore
{
  public:
    static constexpr const wchar_t* kFileName = L"settings.json";
    static constexpr const wchar_t* kTempFileName = L"settings.json.tmp";

    // Uses SHGetKnownFolderPath(FOLDERID_LocalAppData)\TranslucentExplorer.
    SettingsManager();
    // Uses `directory` instead (tests). It is created on the first Save if missing.
    explicit SettingsManager(std::filesystem::path directory);

    LoadResult Load() override;
    HRESULT Save(const AppearanceSettings& settings) override;

    // Reset to defaults (R-11), keeping the user's custom color slots.
    static AppearanceSettings Reset(const AppearanceSettings& current);

    [[nodiscard]] const std::filesystem::path& Directory() const noexcept
    {
        return m_directory;
    }
    [[nodiscard]] std::filesystem::path FilePath() const
    {
        return m_directory / kFileName;
    }

  private:
    std::filesystem::path m_directory;
};

} // namespace te
