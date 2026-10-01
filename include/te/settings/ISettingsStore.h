#pragma once

// Persisted appearance settings (research R-11, R-12; contracts/settings.schema.json).

#include <te/appearance/AppearanceSettings.h>

#include <windows.h>

#include <string>
#include <vector>

namespace te
{

struct LoadResult
{
    AppearanceSettings settings;
    std::vector<std::wstring> fieldsDefaulted; // for diagnostics
    bool fileWasCorrupt = false;               // original renamed to settings.corrupt-<ts>.json
};

class ISettingsStore
{
  public:
    virtual ~ISettingsStore() = default;

    virtual LoadResult Load() = 0;                       // never throws; always returns usable settings
    virtual HRESULT Save(const AppearanceSettings&) = 0; // atomic replace (research R-12)

    static AppearanceSettings Defaults(); // research R-11; defined with SettingsManager (T043)
};

} // namespace te
