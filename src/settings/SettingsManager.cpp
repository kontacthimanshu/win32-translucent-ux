#include <te/settings/SettingsManager.h>

#include <te/appearance/Palette.h>

#include <shlobj.h>

#include <nlohmann/json.hpp>
#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace te
{

namespace
{

using json = nlohmann::json;

constexpr int kSchemaVersion = 1;
constexpr Rgb kWhite{0xFF, 0xFF, 0xFF};

constexpr std::string_view kModeNames[] = {"Acrylic", "Mica", "Solid", "Transparent"};

std::optional<BackdropMode> ParseMode(const json& value)
{
    if (value.is_string())
    {
        const std::string& text = value.get_ref<const std::string&>();
        for (size_t i = 0; i < std::size(kModeNames); ++i)
        {
            if (text == kModeNames[i])
            {
                return static_cast<BackdropMode>(i);
            }
        }
    }
    return std::nullopt;
}

int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// "#RRGGBB" (either case), as the schema pattern ^#[0-9A-Fa-f]{6}$.
std::optional<Rgb> ParseHex(const json& value)
{
    if (!value.is_string())
    {
        return std::nullopt;
    }
    const std::string& text = value.get_ref<const std::string&>();
    if (text.size() != 7 || text[0] != '#')
    {
        return std::nullopt;
    }
    std::uint8_t channels[3]{};
    for (int i = 0; i < 3; ++i)
    {
        const int high = HexDigit(text[1 + i * 2]);
        const int low = HexDigit(text[2 + i * 2]);
        if (high < 0 || low < 0)
        {
            return std::nullopt;
        }
        channels[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return Rgb{channels[0], channels[1], channels[2]};
}

std::string ToHex(Rgb c)
{
    char text[8]{};
    std::snprintf(text, sizeof(text), "#%02X%02X%02X", c.r, c.g, c.b);
    return text;
}

// Reads one opacity field: missing keeps the default silently; a non-number resets it to
// the default; a number is clamped and rounded to the 5% grid.
void ReadOpacity(const json& appearance, const char* key, double min, double max, double& target,
                 std::vector<std::wstring>& defaulted, const wchar_t* name)
{
    const auto it = appearance.find(key);
    if (it == appearance.end())
    {
        return;
    }
    if (!it->is_number())
    {
        defaulted.emplace_back(name);
        return;
    }
    const double raw = it->get<double>();
    target = SnapOpacity(raw, min, max);
    if (std::fabs(target - raw) > 1e-9)
    {
        defaulted.emplace_back(name); // clamped or rounded; reported for diagnostics
    }
}

AppearanceSettings ParseAppearance(const json& appearance, std::vector<std::wstring>& defaulted)
{
    AppearanceSettings settings = ISettingsStore::Defaults();
    if (!appearance.is_object())
    {
        defaulted.emplace_back(L"appearance");
        return settings;
    }

    if (const auto it = appearance.find("backdropMode"); it != appearance.end())
    {
        if (const auto mode = ParseMode(*it))
        {
            settings.backdropMode = *mode;
        }
        else
        {
            defaulted.emplace_back(L"backdropMode");
        }
    }

    if (const auto it = appearance.find("tintColor"); it != appearance.end())
    {
        if (it->is_string() && it->get_ref<const std::string&>() == "accent")
        {
            settings.tintColor = std::monostate{};
        }
        else if (const auto rgb = ParseHex(*it))
        {
            settings.tintColor = *rgb;
        }
        else
        {
            defaulted.emplace_back(L"tintColor");
        }
    }

    ReadOpacity(appearance, "tintOpacity", kTintMin, kTintMax, settings.tintOpacity, defaulted,
                L"tintOpacity");
    ReadOpacity(appearance, "surfaceOpacity", kSurfaceMin, kSurfaceMax, settings.surfaceOpacity, defaulted,
                L"surfaceOpacity");

    // Slab thicknesses: a number is clamped to kSlabMinPx-kSlabMaxPx and rounded; anything
    // else keeps the default. "slabThicknessPx", the one thickness for every edge that
    // earlier versions saved, sets all four; an edge's own key wins over it.
    const auto readThickness = [&](const char* key, const wchar_t* name, int& target) {
        const auto it = appearance.find(key);
        if (it == appearance.end())
        {
            return;
        }
        if (!it->is_number())
        {
            defaulted.emplace_back(name);
            return;
        }
        const double raw = it->get<double>();
        target = static_cast<int>(std::lround(std::clamp(raw, double{kSlabMinPx}, double{kSlabMaxPx})));
        if (std::fabs(target - raw) > 1e-9)
        {
            defaulted.emplace_back(name); // clamped or rounded
        }
    };
    int legacyThickness = -1;
    readThickness("slabThicknessPx", L"slabThicknessPx", legacyThickness);
    if (legacyThickness >= 0)
    {
        settings.slabTopPx = settings.slabLeftPx = settings.slabBottomPx = settings.slabRightPx = legacyThickness;
    }
    readThickness("slabTopPx", L"slabTopPx", settings.slabTopPx);
    readThickness("slabLeftPx", L"slabLeftPx", settings.slabLeftPx);
    readThickness("slabBottomPx", L"slabBottomPx", settings.slabBottomPx);
    readThickness("slabRightPx", L"slabRightPx", settings.slabRightPx);

    for (const auto& [key, name, target] :
         {std::tuple{"slabTop", L"slabTop", &settings.slabTop}, std::tuple{"slabLeft", L"slabLeft", &settings.slabLeft},
          std::tuple{"slabBottom", L"slabBottom", &settings.slabBottom},
          std::tuple{"slabRight", L"slabRight", &settings.slabRight}})
    {
        if (const auto it = appearance.find(key); it != appearance.end())
        {
            if (it->is_boolean())
            {
                *target = it->get<bool>();
            }
            else
            {
                defaulted.emplace_back(name);
            }
        }
    }

    if (const auto it = appearance.find("customColors"); it != appearance.end())
    {
        if (!it->is_array())
        {
            defaulted.emplace_back(L"customColors");
        }
        else
        {
            // Invalid entries become white; the list is padded or truncated to 16.
            if (it->size() != settings.customColors.size())
            {
                defaulted.emplace_back(L"customColors");
            }
            for (size_t i = 0; i < settings.customColors.size(); ++i)
            {
                std::optional<Rgb> rgb = i < it->size() ? ParseHex((*it)[i]) : std::nullopt;
                if (i < it->size() && !rgb)
                {
                    defaulted.emplace_back(L"customColors[" + std::to_wstring(i) + L"]");
                }
                settings.customColors[i] = rgb.value_or(kWhite);
            }
        }
    }
    return settings;
}

json ToJson(const AppearanceSettings& s)
{
    json colors = json::array();
    for (const Rgb c : s.customColors)
    {
        colors.push_back(ToHex(c));
    }
    json appearance;
    appearance["backdropMode"] = std::string(kModeNames[static_cast<size_t>(s.backdropMode)]);
    appearance["tintColor"] =
        std::holds_alternative<Rgb>(s.tintColor) ? ToHex(std::get<Rgb>(s.tintColor)) : std::string("accent");
    appearance["tintOpacity"] = SnapOpacity(s.tintOpacity, kTintMin, kTintMax);
    appearance["surfaceOpacity"] = SnapOpacity(s.surfaceOpacity, kSurfaceMin, kSurfaceMax);
    appearance["slabTopPx"] = std::clamp(s.slabTopPx, kSlabMinPx, kSlabMaxPx);
    appearance["slabLeftPx"] = std::clamp(s.slabLeftPx, kSlabMinPx, kSlabMaxPx);
    appearance["slabBottomPx"] = std::clamp(s.slabBottomPx, kSlabMinPx, kSlabMaxPx);
    appearance["slabRightPx"] = std::clamp(s.slabRightPx, kSlabMinPx, kSlabMaxPx);
    appearance["slabTop"] = s.slabTop;
    appearance["slabLeft"] = s.slabLeft;
    appearance["slabBottom"] = s.slabBottom;
    appearance["slabRight"] = s.slabRight;
    appearance["customColors"] = std::move(colors);

    json root;
    root["schemaVersion"] = kSchemaVersion;
    root["appearance"] = std::move(appearance);
    return root;
}

// settings.corrupt-<yyyyMMddHHmmss>.json, with -2, -3, ... if that name is taken.
std::filesystem::path CorruptName(const std::filesystem::path& directory)
{
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[32]{};
    swprintf_s(stamp, L"%04u%02u%02u%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond);
    std::filesystem::path candidate = directory / (std::wstring(L"settings.corrupt-") + stamp + L".json");
    for (int n = 2; std::filesystem::exists(candidate); ++n)
    {
        candidate =
            directory / (std::wstring(L"settings.corrupt-") + stamp + L"-" + std::to_wstring(n) + L".json");
    }
    return candidate;
}

std::filesystem::path DefaultDirectory()
{
    wil::unique_cotaskmem_string localAppData;
    if (SUCCEEDED_LOG(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData)))
    {
        return std::filesystem::path(localAppData.get()) / L"TranslucentExplorer";
    }
    // Without LocalAppData, settings still work for this session under %TEMP%.
    return std::filesystem::temp_directory_path() / L"TranslucentExplorer";
}

} // namespace

AppearanceSettings ISettingsStore::Defaults()
{
    // The AppearanceSettings member defaults are the research R-11 values.
    return AppearanceSettings{};
}

SettingsManager::SettingsManager() : m_directory(DefaultDirectory()) {}

SettingsManager::SettingsManager(std::filesystem::path directory) : m_directory(std::move(directory)) {}

LoadResult SettingsManager::Load()
{
    LoadResult result;
    result.settings = Defaults();
    try
    {
        const std::filesystem::path file = FilePath();
        std::error_code error;
        if (!std::filesystem::exists(file, error))
        {
            return result; // first run
        }

        std::string bytes;
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
            {
                result.fieldsDefaulted.emplace_back(L"<unreadable file>");
                return result;
            }
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        const json root = json::parse(bytes, nullptr, /*allow_exceptions*/ false);
        if (root.is_discarded() || !root.is_object())
        {
            // Keep the original for inspection and continue with defaults (edge case
            // "Corrupted settings file"). The next Save writes a fresh file.
            const std::filesystem::path corrupt = CorruptName(m_directory);
            if (MoveFileExW(file.c_str(), corrupt.c_str(), MOVEFILE_WRITE_THROUGH))
            {
                result.fileWasCorrupt = true;
            }
            else
            {
                LOG_LAST_ERROR_MSG("Could not rename the corrupt settings file");
                result.fileWasCorrupt = true;
            }
            return result;
        }

        const auto version = root.find("schemaVersion");
        if (version != root.end() && version->is_number_integer() &&
            version->get<long long>() > kSchemaVersion)
        {
            // Written by a newer version: use defaults and leave the file alone until the
            // user changes something.
            result.fieldsDefaulted.emplace_back(L"schemaVersion");
            return result;
        }
        if (version == root.end() || !version->is_number_integer() ||
            version->get<long long>() != kSchemaVersion)
        {
            result.fieldsDefaulted.emplace_back(L"schemaVersion"); // read as version 1
        }

        const auto appearance = root.find("appearance");
        if (appearance != root.end())
        {
            result.settings = ParseAppearance(*appearance, result.fieldsDefaulted);
        }
    }
    catch (...)
    {
        // Never throws: anything unexpected leaves the defaults in place.
        LOG_CAUGHT_EXCEPTION();
        result.settings = Defaults();
    }
    return result;
}

HRESULT SettingsManager::Save(const AppearanceSettings& settings)
try
{
    std::error_code error;
    std::filesystem::create_directories(m_directory, error);
    RETURN_HR_IF_MSG(HRESULT_FROM_WIN32(static_cast<DWORD>(error.value())), static_cast<bool>(error),
                     "Could not create the settings directory");

    const std::string text = ToJson(settings).dump(2) + "\n"; // UTF-8, no BOM
    const std::filesystem::path temp = m_directory / kTempFileName;
    const std::filesystem::path file = FilePath();

    {
        wil::unique_hfile handle(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                             FILE_ATTRIBUTE_NORMAL, nullptr));
        RETURN_LAST_ERROR_IF(!handle);
        DWORD written = 0;
        const bool ok =
            WriteFile(handle.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
            written == text.size() && FlushFileBuffers(handle.get());
        if (!ok)
        {
            const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            handle.reset();
            DeleteFileW(temp.c_str());
            RETURN_HR(FAILED(hr) ? hr : E_FAIL);
        }
    }

    // Atomic replace: readers see either the old file or the complete new one.
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        DeleteFileW(temp.c_str());
        RETURN_HR(hr);
    }
    return S_OK;
}
CATCH_RETURN()

AppearanceSettings SettingsManager::Reset(const AppearanceSettings& current)
{
    AppearanceSettings reset = Defaults();
    reset.customColors = current.customColors; // Reset keeps the custom palette (R-11)
    // The slab is set from its own popup, not the color one.
    reset.slabTopPx = current.slabTopPx;
    reset.slabLeftPx = current.slabLeftPx;
    reset.slabBottomPx = current.slabBottomPx;
    reset.slabRightPx = current.slabRightPx;
    reset.slabTop = current.slabTop;
    reset.slabLeft = current.slabLeft;
    reset.slabBottom = current.slabBottom;
    reset.slabRight = current.slabRight;
    return reset;
}

} // namespace te
