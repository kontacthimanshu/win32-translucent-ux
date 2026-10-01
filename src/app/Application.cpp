#include <te/app/Application.h>

#include <te/core/Watchdog.h>
#include <te/settings/SettingsManager.h>

#include <wil/resource.h>
#include <wil/result.h>

#include <cstdint>
#include <cwctype>
#include <utility>

namespace te
{

namespace
{

std::wstring Lower(std::wstring_view text)
{
    std::wstring lower(text);
    for (wchar_t& c : lower)
    {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return lower;
}

std::optional<BackdropMode> ParseBackdrop(std::wstring_view value)
{
    const std::wstring lower = Lower(value);
    if (lower == L"acrylic")
    {
        return BackdropMode::Acrylic;
    }
    if (lower == L"mica")
    {
        return BackdropMode::Mica;
    }
    if (lower == L"solid")
    {
        return BackdropMode::Solid;
    }
    if (lower == L"transparent")
    {
        return BackdropMode::Transparent;
    }
    return std::nullopt;
}

} // namespace

CommandLine Application::ParseCommandLine(std::span<const std::wstring> args, bool allowDebugOptions)
{
    constexpr std::wstring_view backdropPrefix = L"--backdrop=";

    CommandLine result;
    for (const std::wstring& arg : args)
    {
        if (arg.starts_with(L"--"))
        {
            const std::wstring lower = Lower(arg);
            if (allowDebugOptions && lower.starts_with(backdropPrefix))
            {
                if (auto mode = ParseBackdrop(std::wstring_view(arg).substr(backdropPrefix.size())))
                {
                    result.backdropOverride = mode;
                    continue;
                }
            }
            result.ignored.push_back(arg); // unknown option, bad value, or Debug-only option in Release
            continue;
        }
        if (!result.folderPath && !arg.empty())
        {
            result.folderPath = arg;
            continue;
        }
        result.ignored.push_back(arg);
    }
    return result;
}

Application::Application(HINSTANCE instance, Resources resources, CommandLine commandLine)
    : m_instance(instance), m_resources(resources), m_commandLine(std::move(commandLine))
{
}

std::wstring Application::LoadResourceString(UINT id) const
{
    const wchar_t* text = nullptr;
    // With a zero buffer size LoadStringW returns a read-only pointer into the
    // resource; the string is not null-terminated, so use the returned length.
    const int length = LoadStringW(m_instance, id, reinterpret_cast<LPWSTR>(&text), 0);
    return length > 0 ? std::wstring(text, static_cast<std::size_t>(length)) : std::wstring();
}

void Application::SetModelessDialog(HWND dialog) noexcept
{
    m_modelessDialog = dialog;
}

#ifdef _DEBUG
namespace
{

// Debug builds only (T037, UI §4 "Command line"): a second launch with --backdrop hands
// the mode to the running window and exits, so runtime switching can be tested before
// the picker (US2) exists. Release builds contain none of this code.
constexpr wchar_t kDebugInstanceMutex[] = L"Local\\TranslucentExplorer.Debug.Instance";

bool ForwardBackdropToRunningWindow(BackdropMode mode)
{
    const HWND running = FindWindowExW(nullptr, nullptr, MainWindow::kClassName, nullptr);
    if (!running)
    {
        return false; // the other instance is still starting or already closing
    }
    std::int32_t value = static_cast<std::int32_t>(mode);
    COPYDATASTRUCT data{MainWindow::kCopyDataBackdrop, sizeof(value), &value};
    DWORD_PTR handled = FALSE;
    return SendMessageTimeoutW(running, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG,
                               2000, &handled) != 0 &&
           handled != FALSE;
}

} // namespace
#endif

int Application::Run(int showCommand)
{
#ifdef _DEBUG
    // Held for the life of this instance, so later Debug launches can find it.
    const wil::unique_handle instanceMutex(CreateMutexW(nullptr, FALSE, kDebugInstanceMutex));
    const bool anotherInstance = instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS;
    if (anotherInstance && m_commandLine.backdropOverride &&
        ForwardBackdropToRunningWindow(*m_commandLine.backdropOverride))
    {
        return 0;
    }
#endif

    MainWindow::Options options;
    options.requestedMode = m_commandLine.backdropOverride; // only ever set in Debug builds
    options.statusStrings = StatusBar::Strings::Load(m_instance, m_resources.statusStrings);
    options.fileOpText = FileOpText::Load(m_instance, m_resources.fileOpStrings);
    options.registerModelessDialog = [this](HWND dialog) { SetModelessDialog(dialog); };
    SettingsManager settings; // %LOCALAPPDATA%\TranslucentExplorer\settings.json
    options.settingsStore = &settings;
    if (const std::wstring reset = LoadResourceString(m_resources.settingsResetStringId); !reset.empty())
    {
        options.settingsResetText = reset;
    }
    // Navigation (T064): open the command-line folder, or This PC.
    options.startShell = true;
    options.initialPath = m_commandLine.folderPath;
    for (const auto& [id, target] :
         {std::pair{m_resources.windowTitleFmtId, &options.windowTitleFmt},
          std::pair{m_resources.pathNotFoundFmtId, &options.pathNotFoundFmt},
          std::pair{m_resources.locationErrorFmtId, &options.locationErrorFmt},
          std::pair{m_resources.pickerAutomationNameId, &options.pickerAutomationName},
          std::pair{m_resources.fileListAutomationNameId, &options.fileListAutomationName},
          std::pair{m_resources.toolbarAutomationNameId, &options.toolbarAutomationName},
          std::pair{m_resources.addressAutomationNameId, &options.addressAutomationName},
          std::pair{m_resources.paneAutomationNameId, &options.paneAutomationName},
          std::pair{m_resources.renameAutomationNameId, &options.renameAutomationName},
          std::pair{m_resources.filterPlaceholderId, &options.filterPlaceholder}})
    {
        if (std::wstring text = LoadResourceString(id); !text.empty())
        {
            *target = std::move(text);
        }
    }
    options.instance = m_instance;
    options.iconResourceId = m_resources.iconId;
    options.title = LoadResourceString(m_resources.titleStringId);
    if (options.title.empty())
    {
        options.title = L"Inference Explorer - The PC";
    }

    MainWindow window(std::move(options));
    if (const HRESULT hr = window.Create(showCommand); FAILED(hr))
    {
        LOG_HR(hr);
        return static_cast<int>(hr);
    }

    // nullptr until the table has entries (rc.exe drops an empty ACCELERATORS
    // block); TranslateAcceleratorW is then skipped.
    const HACCEL accelerators =
        m_resources.acceleratorsId != 0
            ? LoadAcceleratorsW(m_instance, MAKEINTRESOURCEW(m_resources.acceleratorsId))
            : nullptr;

    MSG msg{};
    BOOL result = 0;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) != 0)
    {
        if (result == -1)
        {
            LOG_LAST_ERROR();
            return 1;
        }
        if (m_modelessDialog && IsDialogMessageW(m_modelessDialog, &msg))
        {
            continue;
        }
        if (accelerators && TranslateAcceleratorW(window.Hwnd(), accelerators, &msg))
        {
            continue;
        }
        TranslateMessage(&msg);
        const DispatchWatchdog watchdog(msg.message); // Debug only: logs handlers slower than 50 ms
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace te
