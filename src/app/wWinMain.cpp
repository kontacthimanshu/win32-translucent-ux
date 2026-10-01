#include <te/app/Application.h>
#include <te/core/ComInit.h>
#include <te/core/Result.h>

#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>

#include <wil/resource.h>
#include <wil/result.h>

#include "resource.h"

#include <string>
#include <vector>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace
{

#ifdef _DEBUG
// Debug CRT leak check (T093, SC-010): every heap block still allocated when the CRT shuts
// down is reported, with its allocation number, to the debugger output (Visual Studio,
// DebugView). When TE_CRT_REPORT names a file, the report also goes there, so a scripted
// run can read it; the file stays open until the process ends, because the report is
// written after main returns.
void EnableLeakCheck()
{
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    wchar_t path[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"TE_CRT_REPORT", path, MAX_PATH) == 0)
    {
        return;
    }
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, file); // intentionally never closed: see above
    }
}
#endif

// Command-line arguments without the program name.
std::vector<std::wstring> Arguments()
{
    int count = 0;
    wil::unique_hlocal_ptr<PWSTR[]> argv(CommandLineToArgvW(GetCommandLineW(), &count));
    std::vector<std::wstring> args;
    for (int i = 1; argv && i < count; ++i)
    {
        args.emplace_back(argv.get()[i]);
    }
    return args;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
#ifdef _DEBUG
    EnableLeakCheck();
#endif
    try
    {
        // The UI thread is a single-threaded apartment with OLE services
        // (clipboard, drag and drop).
        const te::OleScope ole;

        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);

#ifdef _DEBUG
        constexpr bool allowDebugOptions = true; // --backdrop (UI contract §4)
#else
        constexpr bool allowDebugOptions = false;
#endif
        const std::vector<std::wstring> args = Arguments();
        te::Application::Resources resources{IDI_APP, IDR_ACCEL, IDS_APP_TITLE};
        resources.settingsResetStringId = IDS_SETTINGS_RESET;
        resources.pickerAutomationNameId = IDS_A11Y_PICKER_NAME;
        resources.fileListAutomationNameId = IDS_A11Y_FILE_LIST;
        resources.toolbarAutomationNameId = IDS_A11Y_TOOLBAR;
        resources.addressAutomationNameId = IDS_A11Y_ADDRESS;
        resources.renameAutomationNameId = IDS_A11Y_RENAME;
        resources.filterPlaceholderId = IDS_FILTER_PLACEHOLDER;
        resources.paneAutomationNameId = IDS_A11Y_NAV_PANE;
        resources.windowTitleFmtId = IDS_WINDOW_TITLE_FMT;
        resources.pathNotFoundFmtId = IDS_ERR_PATH_NOT_FOUND_FMT;
        resources.locationErrorFmtId = IDS_ERR_LOCATION_FMT;
        resources.statusStrings = {IDS_MODE_ACRYLIC,
                                   IDS_MODE_MICA,
                                   IDS_MODE_SOLID,
                                   IDS_MODE_TRANSPARENT,
                                   IDS_STATUS_APPEARANCE_FMT,
                                   IDS_STATUS_APPEARANCE_SOLID_FMT,
                                   IDS_STATUS_FALLBACK_FMT,
                                   IDS_FALLBACK_HIGH_CONTRAST,
                                   IDS_FALLBACK_TRANSPARENCY_OFF,
                                   IDS_FALLBACK_UNSUPPORTED,
                                   IDS_FALLBACK_APPLY_FAILED,
                                   IDS_STATUS_ITEMS_FMT,
                                   IDS_STATUS_SELECTED_FMT};
        resources.fileOpStrings = {IDS_OP_COPYING_FMT,   IDS_OP_MOVING_FMT,   IDS_OP_DELETING_FMT,
                                   IDS_OP_RENAMING,      IDS_OP_COPIED_FMT,   IDS_OP_MOVED_FMT,
                                   IDS_OP_DELETED_FMT,   IDS_OP_RENAMED,      IDS_OP_PARTIAL_FMT,
                                   IDS_OP_CANCELLED_FMT, IDS_OP_FAILED_TITLE, IDS_ERR_INVALID_NAME};
        te::Application app(instance, resources, te::Application::ParseCommandLine(args, allowDebugOptions));
        return app.Run(showCommand);
    }
    catch (...)
    {
        // Initialization failed before the window existed: tell the user rather
        // than exit silently.
        const HRESULT hr = wil::ResultFromCaughtException();
        const std::wstring message = L"Translucent Explorer could not start.\n\n" + te::HresultMessage(hr);
        MessageBoxW(nullptr, message.c_str(), L"Translucent Explorer", MB_OK | MB_ICONERROR);
        return static_cast<int>(hr);
    }
}
