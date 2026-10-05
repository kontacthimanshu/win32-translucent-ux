// SettingsManager (T041; research R-11, R-12, contracts/settings.schema.json): defaults,
// round trip, per-field validation (clamping, rounding, bad values, padding), corrupt and
// future-version files, and the atomic write through settings.json.tmp.

#include <te/settings/SettingsManager.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <variant>

namespace
{

namespace fs = std::filesystem;

constexpr te::Rgb kWhite{0xFF, 0xFF, 0xFF};

class SettingsTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / L"te-settings-tests" /
                (std::string(info->test_suite_name()) + "." + info->name());
        fs::remove_all(m_dir);
        fs::create_directories(m_dir);
    }

    void TearDown() override
    {
        std::error_code ignored;
        fs::remove_all(m_dir, ignored);
    }

    fs::path File() const
    {
        return m_dir / te::SettingsManager::kFileName;
    }

    void WriteFile(const std::string& utf8) const
    {
        std::ofstream out(File(), std::ios::binary | std::ios::trunc);
        out << utf8;
    }

    std::string ReadFile(const fs::path& path) const
    {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    // A file with one field overridden inside "appearance".
    void WriteAppearance(const std::string& fields) const
    {
        WriteFile(R"({"schemaVersion": 1, "appearance": {)" + fields + "}}");
    }

    te::LoadResult Load() const
    {
        te::SettingsManager manager(m_dir);
        return manager.Load();
    }

    int CountFiles(const std::wstring& prefix, const std::wstring& suffix) const
    {
        int count = 0;
        for (const auto& entry : fs::directory_iterator(m_dir))
        {
            const std::wstring name = entry.path().filename().wstring();
            if (name.starts_with(prefix) && name.ends_with(suffix))
            {
                ++count;
            }
        }
        return count;
    }

    fs::path m_dir;
};

void ExpectDefaults(const te::AppearanceSettings& s)
{
    EXPECT_EQ(s.backdropMode, te::BackdropMode::Mica);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(s.tintColor)) << "tint should follow the accent";
    EXPECT_NEAR(s.tintOpacity, 0.20, 1e-9);
    EXPECT_NEAR(s.surfaceOpacity, 0.00, 1e-9);
    for (const te::Rgb c : s.customColors)
    {
        EXPECT_EQ(c, kWhite);
    }
}

// ---------------------------------------------------------------------------
// Defaults and round trip
// ---------------------------------------------------------------------------

TEST_F(SettingsTest, MissingFileGivesDefaults)
{
    const te::LoadResult result = Load();
    ExpectDefaults(result.settings);
    EXPECT_FALSE(result.fileWasCorrupt);
}

TEST_F(SettingsTest, DefaultsMatchResearchR11)
{
    ExpectDefaults(te::ISettingsStore::Defaults());
}

TEST_F(SettingsTest, SaveThenLoadRoundTrips)
{
    te::AppearanceSettings saved;
    saved.backdropMode = te::BackdropMode::Acrylic;
    saved.tintColor = te::Rgb{0x87, 0x64, 0xB8};
    saved.tintOpacity = 0.35;
    saved.surfaceOpacity = 0.25;
    saved.customColors[0] = {0x12, 0x34, 0x56};
    saved.customColors[15] = {0xAB, 0xCD, 0xEF};

    te::SettingsManager manager(m_dir);
    ASSERT_HRESULT_SUCCEEDED(manager.Save(saved));
    const te::LoadResult result = te::SettingsManager(m_dir).Load();

    EXPECT_FALSE(result.fileWasCorrupt);
    EXPECT_EQ(result.settings.backdropMode, te::BackdropMode::Acrylic);
    ASSERT_TRUE(std::holds_alternative<te::Rgb>(result.settings.tintColor));
    EXPECT_EQ(std::get<te::Rgb>(result.settings.tintColor), (te::Rgb{0x87, 0x64, 0xB8}));
    EXPECT_NEAR(result.settings.tintOpacity, 0.35, 1e-9);
    EXPECT_NEAR(result.settings.surfaceOpacity, 0.25, 1e-9);
    EXPECT_EQ(result.settings.customColors[0], (te::Rgb{0x12, 0x34, 0x56}));
    EXPECT_EQ(result.settings.customColors[15], (te::Rgb{0xAB, 0xCD, 0xEF}));
    EXPECT_TRUE(result.fieldsDefaulted.empty());
}

TEST_F(SettingsTest, TransparentModeRoundTrips)
{
    te::AppearanceSettings saved;
    saved.backdropMode = te::BackdropMode::Transparent;
    ASSERT_HRESULT_SUCCEEDED(te::SettingsManager(m_dir).Save(saved));
    const te::LoadResult result = Load();
    EXPECT_EQ(result.settings.backdropMode, te::BackdropMode::Transparent);
    EXPECT_TRUE(result.fieldsDefaulted.empty());
}

TEST_F(SettingsTest, AccentTintRoundTrips)
{
    te::AppearanceSettings saved;
    saved.backdropMode = te::BackdropMode::Solid;
    ASSERT_HRESULT_SUCCEEDED(te::SettingsManager(m_dir).Save(saved));
    const te::LoadResult result = Load();
    EXPECT_EQ(result.settings.backdropMode, te::BackdropMode::Solid);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result.settings.tintColor));
}

// ---------------------------------------------------------------------------
// Per-field validation (FR-007): one bad field never resets the others
// ---------------------------------------------------------------------------

TEST_F(SettingsTest, TintOpacityAboveRangeClampsAndKeepsMode)
{
    WriteAppearance(R"("backdropMode": "Acrylic", "tintOpacity": 5)");
    const te::LoadResult result = Load();
    EXPECT_NEAR(result.settings.tintOpacity, 0.80, 1e-9);
    EXPECT_EQ(result.settings.backdropMode, te::BackdropMode::Acrylic);
    EXPECT_FALSE(result.fileWasCorrupt);
}

TEST_F(SettingsTest, TintOpacityRoundsToNearestStep)
{
    WriteAppearance(R"("tintOpacity": 0.33)");
    EXPECT_NEAR(Load().settings.tintOpacity, 0.35, 1e-9);
}

TEST_F(SettingsTest, SurfaceOpacityClampsWithoutTouchingTint)
{
    WriteAppearance(R"("surfaceOpacity": 1.5, "tintOpacity": 0.45)");
    te::LoadResult result = Load();
    EXPECT_NEAR(result.settings.surfaceOpacity, 0.90, 1e-9);
    EXPECT_NEAR(result.settings.tintOpacity, 0.45, 1e-9);

    WriteAppearance(R"("surfaceOpacity": -1, "tintOpacity": 0.45)");
    result = Load();
    EXPECT_NEAR(result.settings.surfaceOpacity, 0.00, 1e-9);
    EXPECT_NEAR(result.settings.tintOpacity, 0.45, 1e-9);
}

TEST_F(SettingsTest, BadTintColorFallsBackForThatFieldOnly)
{
    for (const char* bad : {R"("#12345")", R"("#GGGGGG")", R"("red")", R"(42)", R"("#1234567")"})
    {
        SCOPED_TRACE(bad);
        WriteAppearance(std::string(R"("backdropMode": "Solid", "tintOpacity": 0.5, "tintColor": )") + bad);
        const te::LoadResult result = Load();
        EXPECT_TRUE(std::holds_alternative<std::monostate>(result.settings.tintColor));
        EXPECT_EQ(result.settings.backdropMode, te::BackdropMode::Solid);
        EXPECT_NEAR(result.settings.tintOpacity, 0.50, 1e-9);
        EXPECT_FALSE(result.fieldsDefaulted.empty());
    }
}

TEST_F(SettingsTest, ValidTintColorIsCaseInsensitive)
{
    WriteAppearance(R"("tintColor": "#8764b8")");
    const te::LoadResult result = Load();
    ASSERT_TRUE(std::holds_alternative<te::Rgb>(result.settings.tintColor));
    EXPECT_EQ(std::get<te::Rgb>(result.settings.tintColor), (te::Rgb{0x87, 0x64, 0xB8}));
}

TEST_F(SettingsTest, UnknownBackdropModeFallsBackForThatFieldOnly)
{
    WriteAppearance(R"("backdropMode": "Glass", "surfaceOpacity": 0.3)");
    const te::LoadResult result = Load();
    EXPECT_EQ(result.settings.backdropMode, te::BackdropMode::Mica);
    EXPECT_NEAR(result.settings.surfaceOpacity, 0.30, 1e-9);
}

TEST_F(SettingsTest, ShortCustomColorsArePaddedTo16)
{
    WriteAppearance(R"("customColors": ["#010203", "#040506", "#070809"])");
    const te::LoadResult result = Load();
    EXPECT_EQ(result.settings.customColors[0], (te::Rgb{0x01, 0x02, 0x03}));
    EXPECT_EQ(result.settings.customColors[1], (te::Rgb{0x04, 0x05, 0x06}));
    EXPECT_EQ(result.settings.customColors[2], (te::Rgb{0x07, 0x08, 0x09}));
    for (size_t i = 3; i < 16; ++i)
    {
        EXPECT_EQ(result.settings.customColors[i], kWhite) << i;
    }
}

TEST_F(SettingsTest, InvalidCustomColorEntriesBecomeWhite)
{
    WriteAppearance(R"("customColors": ["#010203", "nope", 7])");
    const te::LoadResult result = Load();
    EXPECT_EQ(result.settings.customColors[0], (te::Rgb{0x01, 0x02, 0x03}));
    EXPECT_EQ(result.settings.customColors[1], kWhite);
    EXPECT_EQ(result.settings.customColors[2], kWhite);
}

// ---------------------------------------------------------------------------
// Corrupt and future-version files
// ---------------------------------------------------------------------------

TEST_F(SettingsTest, CorruptFileIsRenamedAndDefaultsLoad)
{
    WriteFile("{not json");
    const te::LoadResult result = Load();

    EXPECT_TRUE(result.fileWasCorrupt);
    ExpectDefaults(result.settings);
    ASSERT_EQ(CountFiles(L"settings.corrupt-", L".json"), 1);
    for (const auto& entry : fs::directory_iterator(m_dir))
    {
        if (entry.path().filename().wstring().starts_with(L"settings.corrupt-"))
        {
            EXPECT_EQ(ReadFile(entry.path()), "{not json") << "the original bytes are kept";
        }
    }
}

TEST_F(SettingsTest, FutureSchemaVersionLoadsDefaultsAndKeepsTheFile)
{
    const std::string future =
        R"({"schemaVersion": 2, "appearance": {"backdropMode": "Acrylic", "tintOpacity": 0.5}})";
    WriteFile(future);
    const te::LoadResult result = Load();

    ExpectDefaults(result.settings);
    EXPECT_FALSE(result.fileWasCorrupt);
    EXPECT_EQ(ReadFile(File()), future) << "not overwritten until the user makes a change";
    EXPECT_EQ(CountFiles(L"settings.corrupt-", L".json"), 0);
}

// ---------------------------------------------------------------------------
// Atomic save
// ---------------------------------------------------------------------------

TEST_F(SettingsTest, SaveReplacesThroughTempFileAndLeavesNoTemp)
{
    // A stale temp file from an interrupted save must not survive or block the next one.
    {
        std::ofstream stale(m_dir / te::SettingsManager::kTempFileName, std::ios::binary);
        stale << "stale";
    }
    WriteFile(R"({"schemaVersion": 1, "appearance": {"backdropMode": "Solid"}})");

    te::AppearanceSettings settings;
    settings.backdropMode = te::BackdropMode::Acrylic;
    ASSERT_HRESULT_SUCCEEDED(te::SettingsManager(m_dir).Save(settings));

    EXPECT_FALSE(fs::exists(m_dir / te::SettingsManager::kTempFileName));
    const std::string written = ReadFile(File());
    EXPECT_FALSE(written.starts_with("\xEF\xBB\xBF")) << "UTF-8 without a BOM";
    EXPECT_NE(written.find("\"schemaVersion\""), std::string::npos);
    EXPECT_EQ(Load().settings.backdropMode, te::BackdropMode::Acrylic);
}

TEST_F(SettingsTest, SaveCreatesTheDirectory)
{
    const fs::path nested = m_dir / L"nested" / L"TranslucentExplorer";
    ASSERT_HRESULT_SUCCEEDED(te::SettingsManager(nested).Save(te::AppearanceSettings{}));
    EXPECT_TRUE(fs::exists(nested / te::SettingsManager::kFileName));
}

// ---------------------------------------------------------------------------
// Reset (R-11): everything back to defaults except the custom color slots
// ---------------------------------------------------------------------------

TEST_F(SettingsTest, ResetKeepsCustomColors)
{
    te::AppearanceSettings current;
    current.backdropMode = te::BackdropMode::Solid;
    current.tintColor = te::Rgb{1, 2, 3};
    current.tintOpacity = 0.7;
    current.surfaceOpacity = 0.6;
    current.customColors[4] = {9, 9, 9};

    const te::AppearanceSettings reset = te::SettingsManager::Reset(current);
    EXPECT_EQ(reset.backdropMode, te::BackdropMode::Mica);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(reset.tintColor));
    EXPECT_NEAR(reset.tintOpacity, 0.20, 1e-9);
    EXPECT_NEAR(reset.surfaceOpacity, 0.00, 1e-9);
    EXPECT_EQ(reset.customColors[4], (te::Rgb{9, 9, 9}));
}

TEST_F(SettingsTest, SlabThicknessRoundTripsAndClamps)
{
    te::AppearanceSettings saved;
    saved.slabThicknessPx = 20;
    ASSERT_HRESULT_SUCCEEDED(te::SettingsManager(m_dir).Save(saved));
    EXPECT_EQ(Load().settings.slabThicknessPx, 20);

    WriteAppearance(R"("slabThicknessPx": 500, "tintOpacity": 0.45)");
    te::LoadResult result = Load();
    EXPECT_EQ(result.settings.slabThicknessPx, te::kSlabMaxPx);
    EXPECT_NEAR(result.settings.tintOpacity, 0.45, 1e-9);

    WriteAppearance(R"("slabThicknessPx": "thick")");
    EXPECT_EQ(Load().settings.slabThicknessPx, te::kSlabDefaultPx);
}

TEST_F(SettingsTest, ResetKeepsSlabThickness)
{
    te::AppearanceSettings current;
    current.slabThicknessPx = 30;
    current.slabEnabled = false;
    const te::AppearanceSettings reset = te::SettingsManager::Reset(current);
    EXPECT_EQ(reset.slabThicknessPx, 30);
    EXPECT_FALSE(reset.slabEnabled);
}

TEST_F(SettingsTest, SlabEnabledRoundTripsAndKeepsTheThickness)
{
    EXPECT_TRUE(te::AppearanceSettings{}.slabEnabled) << "on by default";
    te::AppearanceSettings saved;
    saved.slabEnabled = false;
    saved.slabThicknessPx = 24;
    ASSERT_HRESULT_SUCCEEDED(te::SettingsManager(m_dir).Save(saved));
    const te::AppearanceSettings loaded = Load().settings;
    EXPECT_FALSE(loaded.slabEnabled);
    EXPECT_EQ(loaded.slabThicknessPx, 24);

    WriteAppearance(R"("slabEnabled": "yes", "slabThicknessPx": 8)");
    const te::LoadResult result = Load();
    EXPECT_TRUE(result.settings.slabEnabled) << "a non-boolean falls back to the default";
    EXPECT_EQ(result.settings.slabThicknessPx, 8);
}

} // namespace
