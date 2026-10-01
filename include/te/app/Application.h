#pragma once

// The application: command-line options, the main window and the message loop (T025).

#include <te/app/MainWindow.h>
#include <te/core/Types.h>

#include <windows.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace te
{

// Parsed command line (UI contract §4, "Command line").
struct CommandLine
{
    // First non-option argument: the folder to open instead of This PC (R-11).
    std::optional<std::wstring> folderPath;
    // --backdrop=acrylic|mica|solid, accepted in Debug builds only (T037).
    std::optional<BackdropMode> backdropOverride;
    // Arguments that were not understood (or not allowed in this build).
    std::vector<std::wstring> ignored;
};

class Application
{
  public:
    // Resource identifiers from resources/resource.h, which only the executable sees.
    struct Resources
    {
        int iconId = 0;         // IDI_APP
        int acceleratorsId = 0; // IDR_ACCEL
        UINT titleStringId = 0; // IDS_APP_TITLE
        StatusBar::StringIds statusStrings;
        UINT settingsResetStringId = 0;      // IDS_SETTINGS_RESET
        UINT pickerAutomationNameId = 0;     // IDS_A11Y_PICKER_NAME (T075)
        UINT fileListAutomationNameId = 0;   // IDS_A11Y_FILE_LIST (T076)
        UINT toolbarAutomationNameId = 0;    // IDS_A11Y_TOOLBAR (T078)
        UINT addressAutomationNameId = 0;    // IDS_A11Y_ADDRESS (T078)
        UINT paneAutomationNameId = 0;       // IDS_A11Y_NAV_PANE (T078)
        UINT renameAutomationNameId = 0;     // IDS_A11Y_RENAME (T083)
        UINT filterPlaceholderId = 0;        // IDS_FILTER_PLACEHOLDER (T092)
        UINT windowTitleFmtId = 0;           // IDS_WINDOW_TITLE_FMT
        UINT pathNotFoundFmtId = 0;          // IDS_ERR_PATH_NOT_FOUND_FMT
        UINT locationErrorFmtId = 0;         // IDS_ERR_LOCATION_FMT
        FileOpText::StringIds fileOpStrings; // IDS_OP_*, IDS_ERR_INVALID_NAME (T072)
    };

    // args excludes the program name. allowDebugOptions enables --backdrop.
    static CommandLine ParseCommandLine(std::span<const std::wstring> args, bool allowDebugOptions);

    Application(HINSTANCE instance, Resources resources, CommandLine commandLine);

    // Creates the main window and runs the message loop until WM_QUIT. Returns
    // the WM_QUIT exit code, or a non-zero code if the window cannot be created.
    int Run(int showCommand);

    // The modeless appearance popup (T045) registers itself so the message loop
    // routes keyboard navigation to it with IsDialogMessageW. nullptr unregisters.
    void SetModelessDialog(HWND dialog) noexcept;

    [[nodiscard]] const CommandLine& Options() const noexcept
    {
        return m_commandLine;
    }

  private:
    std::wstring LoadResourceString(UINT id) const;

    HINSTANCE m_instance;
    Resources m_resources;
    CommandLine m_commandLine;
    HWND m_modelessDialog = nullptr;
};

} // namespace te
