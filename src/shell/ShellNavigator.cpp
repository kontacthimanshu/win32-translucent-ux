#include <te/shell/ShellNavigator.h>

#include <commctrl.h>
#include <pathcch.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <wil/resource.h>
#include <wil/result.h>

#include <cwctype>
#include <utility>
#include <vector>

namespace te
{

namespace
{

constexpr int kOpenWithButton = 100;

std::wstring Trim(std::wstring_view text)
{
    while (!text.empty() && std::iswspace(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::iswspace(text.back()))
    {
        text.remove_suffix(1);
    }
    // A path pasted from "Copy as path" is quoted.
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
    {
        text = text.substr(1, text.size() - 2);
    }
    return std::wstring(text);
}

std::wstring Expand(const std::wstring& text)
{
    const DWORD needed = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (needed == 0)
    {
        return text;
    }
    std::wstring expanded(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(text.c_str(), expanded.data(), needed);
    if (written == 0 || written > needed)
    {
        return text;
    }
    expanded.resize(written - 1); // drop the terminator
    return expanded;
}

// Resolves "." and ".." in file-system paths ("C:\a\..\b", "\\server\share\..."),
// which SHParseDisplayName does not. Other text (shell:, ::{...}, names) is unchanged.
std::wstring Canonicalize(const std::wstring& path)
{
    constexpr wchar_t kSlash = 0x5C; // backslash
    const bool drive = path.size() >= 3 && path[1] == L':' && (path[2] == kSlash || path[2] == L'/');
    const bool unc = path.size() >= 2 && path[0] == kSlash && path[1] == kSlash;
    if (!drive && !unc)
    {
        return path;
    }
    wil::unique_hlocal_string canonical;
    if (FAILED(PathAllocCanonicalize(path.c_str(), PATHCCH_ALLOW_LONG_PATHS, &canonical)) || !canonical)
    {
        return path;
    }
    // Past MAX_PATH the result gets the \\?\ prefix, which SHParseDisplayName rejects; the
    // Shell parses long paths without it. The same for a prefix the user typed.
    std::wstring result = canonical.get();
    constexpr std::wstring_view kUncPrefix = LR"(\\?\UNC\)";
    constexpr std::wstring_view kPrefix = LR"(\\?\)";
    if (result.size() > kUncPrefix.size() &&
        _wcsnicmp(result.c_str(), kUncPrefix.data(), kUncPrefix.size()) == 0)
    {
        result.replace(0, kUncPrefix.size(), LR"(\\)");
    }
    else if (result.size() > kPrefix.size() + 1 && result.starts_with(kPrefix) &&
             result[kPrefix.size() + 1] == L':')
    {
        result.erase(0, kPrefix.size());
    }
    return result;
}

HRESULT FromKnownFolder(REFKNOWNFOLDERID id, ShellLocation* out)
{
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
    RETURN_IF_FAILED(SHGetKnownFolderIDList(id, KF_FLAG_DEFAULT, nullptr, wil::out_param(pidl)));
    return ShellLocation::FromIdList(pidl.get(), out);
}

bool IsFolder(const ShellLocation& location)
{
    wil::com_ptr<IShellItem> item;
    SFGAOF attributes = 0;
    return SUCCEEDED(location.Item(&item)) && SUCCEEDED(item->GetAttributes(SFGAO_FOLDER, &attributes)) &&
           (attributes & SFGAO_FOLDER) != 0;
}

std::wstring FormatBody(const std::wstring& pattern, const std::wstring& name)
{
    const DWORD_PTR args[] = {reinterpret_cast<DWORD_PTR>(name.c_str())};
    LPWSTR raw = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY |
                                            FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                        pattern.c_str(), 0, 0, reinterpret_cast<LPWSTR>(&raw), 0,
                                        reinterpret_cast<va_list*>(const_cast<DWORD_PTR*>(args)));
    const wil::unique_hlocal buffer(raw);
    return length > 0 ? std::wstring(raw, length) : pattern;
}

// ShellExecuteExW on an absolute ID list; returns the Win32 error as an HRESULT.
HRESULT Execute(HWND owner, PCIDLIST_ABSOLUTE pidl, const wchar_t* verb)
{
    SHELLEXECUTEINFOW info{sizeof(info)};
    // SEE_MASK_FLAG_NO_UI: failures come back as errors instead of Shell message boxes,
    // so the app can explain them itself (UI §6).
    info.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpVerb = verb;
    info.lpIDList = const_cast<LPITEMIDLIST>(reinterpret_cast<LPCITEMIDLIST>(pidl));
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info))
    {
        return S_OK;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_NO_ASSOCIATION ||
        reinterpret_cast<INT_PTR>(info.hInstApp) == static_cast<INT_PTR>(SE_ERR_NOASSOC))
    {
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    }
    return HRESULT_FROM_WIN32(error != 0 ? error : ERROR_GEN_FAILURE);
}

// True when a file-system item's type has no app to open it. ShellExecuteExW cannot be
// relied on to report this: on Windows 11 it succeeds and starts the system "How do you
// want to open this file?" window even with SEE_MASK_FLAG_NO_UI, so the app asks first.
bool HasNoAssociation(PCIDLIST_ABSOLUTE pidl)
{
    wchar_t path[MAX_PATH * 4]{};
    if (!SHGetPathFromIDListEx(pidl, path, static_cast<DWORD>(std::size(path)), GPFIDL_DEFAULT))
    {
        return false; // not a file-system item: let the Shell decide
    }
    const wchar_t* extension = PathFindExtensionW(path);
    if (!extension || !*extension)
    {
        return true; // no extension: nothing is registered
    }
    // A real buffer: with a null output buffer the call does not report the association
    // state (it did not return ERROR_NO_ASSOCIATION for an unknown type).
    wchar_t command[MAX_PATH * 2]{};
    DWORD length = static_cast<DWORD>(std::size(command));
    const HRESULT hr =
        AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_COMMAND, extension, nullptr, command, &length);
    return hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
}

} // namespace

ShellNavigator::ShellNavigator(Strings strings, AskOpenWith askOpenWith)
    : m_strings(std::move(strings)), m_askOpenWith(std::move(askOpenWith))
{
}

HRESULT ShellNavigator::Parse(std::wstring_view text, ShellLocation* out)
{
    RETURN_HR_IF(E_POINTER, out == nullptr);
    const std::wstring path = Canonicalize(Expand(Trim(text)));
    RETURN_HR_IF(E_INVALIDARG, path.empty());

    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
    const HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, wil::out_param(pidl), 0, nullptr);
    if (FAILED(hr))
    {
        return hr; // not logged: a mistyped address is a normal user error
    }
    ShellLocation location;
    RETURN_IF_FAILED(ShellLocation::FromIdList(pidl.get(), &location));
    *out = std::move(location);
    return S_OK;
}

std::optional<ShellLocation> ShellNavigator::Parent(const ShellLocation& location)
{
    return location.Parent();
}

ShellLocation ShellNavigator::InitialLocation(std::optional<std::wstring_view> cmdLinePath)
{
    ShellLocation location;
    if (cmdLinePath && SUCCEEDED(Parse(*cmdLinePath, &location)) && IsFolder(location))
    {
        return location;
    }
    for (const KNOWNFOLDERID* id : {&FOLDERID_ComputerFolder, &FOLDERID_Profile, &FOLDERID_Desktop})
    {
        if (SUCCEEDED_LOG(FromKnownFolder(*id, &location)))
        {
            return location;
        }
    }
    return {};
}

HRESULT ShellNavigator::Open(HWND owner, const ShellLocation& folder, const ShellItemInfo& item)
{
    if (item.isFolder)
    {
        return S_FALSE; // folders navigate instead of opening
    }
    RETURN_HR_IF(E_INVALIDARG, !folder.IsValid() || !item.childPidl);
    // ILCombine returns an __unaligned pointer on x64; the allocation itself is aligned.
    const wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> full(
        reinterpret_cast<ITEMIDLIST_ABSOLUTE*>(ILCombine(folder.IdList(), item.childPidl.get())));
    RETURN_IF_NULL_ALLOC(full);

    const HRESULT hr = HasNoAssociation(full.get()) ? HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION)
                                                    : Execute(owner, full.get(), nullptr);
    if (hr != HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION))
    {
        return hr;
    }
    // No associated app: explain and offer "Open with..." (UI §6).
    const bool chooseApp =
        m_askOpenWith ? m_askOpenWith(owner, item.name) : DefaultAskOpenWith(owner, item.name);
    return chooseApp ? Execute(owner, full.get(), L"openas") : hr;
}

bool ShellNavigator::DefaultAskOpenWith(HWND owner, const std::wstring& itemName) const
{
    const std::wstring body = FormatBody(m_strings.cantOpenBodyFmt, itemName);
    const TASKDIALOG_BUTTON buttons[] = {{kOpenWithButton, m_strings.openWith.c_str()}};

    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = owner;
    config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = m_strings.cantOpenTitle.c_str();
    config.pszMainIcon = TD_INFORMATION_ICON;
    config.pszMainInstruction = m_strings.cantOpenTitle.c_str();
    config.pszContent = body.c_str();
    config.cButtons = static_cast<UINT>(std::size(buttons));
    config.pButtons = buttons;

    int pressed = 0;
    return SUCCEEDED_LOG(TaskDialogIndirect(&config, &pressed, nullptr, nullptr)) &&
           pressed == kOpenWithButton;
}

HRESULT ShellNavigator::ShowContextMenu(HWND owner, POINT screenPt, const ShellLocation& folder,
                                        std::span<const ShellItemInfo* const> items, bool extended)
{
    RETURN_HR_IF(E_UNEXPECTED, m_openMenu != nullptr); // one menu at a time
    auto menu = std::make_unique<ContextMenu>();
    RETURN_IF_FAILED(menu->Load(owner, folder, items));
    m_openMenu = std::move(menu);
    // Kept alive while the menu (and any command it runs) is on screen, so the window
    // procedure can forward menu messages to it.
    const auto close = wil::scope_exit([this] { m_openMenu.reset(); });
    return m_openMenu->Show(owner, screenPt, extended, m_track);
}

bool ShellNavigator::HandleMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    return m_openMenu && m_openMenu->HandleMenuMessage(msg, wParam, lParam, result);
}

} // namespace te
