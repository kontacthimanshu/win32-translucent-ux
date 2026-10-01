#include <te/shell/ContextMenu.h>

#include <te/shell/IShellNavigator.h>

#include <shellapi.h>
#include <shlobj.h>

#include <wil/resource.h>
#include <wil/result.h>

#include <vector>

namespace te
{

HRESULT ContextMenu::Load(HWND owner, const ShellLocation& folder,
                          std::span<const ShellItemInfo* const> items)
{
    m_menu.reset();
    m_menu2.reset();
    m_menu3.reset();
    RETURN_HR_IF(E_INVALIDARG, !folder.IsValid());

    if (items.empty())
    {
        // The folder background: New, Paste, Properties and the like.
        wil::com_ptr<IShellFolder> shellFolder;
        RETURN_IF_FAILED(SHBindToObject(nullptr, folder.IdList(), nullptr, IID_PPV_ARGS(&shellFolder)));
        RETURN_IF_FAILED(shellFolder->CreateViewObject(owner, IID_PPV_ARGS(&m_menu)));
    }
    else
    {
        std::vector<wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>> owned;
        std::vector<PCIDLIST_ABSOLUTE> pidls;
        for (const ShellItemInfo* item : items)
        {
            RETURN_HR_IF(E_INVALIDARG, !item || !item->childPidl);
            // ILCombine returns an __unaligned pointer on x64; the allocation is aligned.
            owned.emplace_back(
                reinterpret_cast<ITEMIDLIST_ABSOLUTE*>(ILCombine(folder.IdList(), item->childPidl.get())));
            RETURN_IF_NULL_ALLOC(owned.back());
            pidls.push_back(owned.back().get());
        }
        wil::com_ptr<IShellItemArray> array;
        RETURN_IF_FAILED(
            SHCreateShellItemArrayFromIDLists(static_cast<UINT>(pidls.size()), pidls.data(), &array));
        RETURN_IF_FAILED(array->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&m_menu)));
    }
    m_menu2 = m_menu.try_query<IContextMenu2>();
    m_menu3 = m_menu.try_query<IContextMenu3>();
    return S_OK;
}

HRESULT ContextMenu::Populate(HMENU menu, bool extended)
{
    RETURN_HR_IF(E_UNEXPECTED, !m_menu);
    RETURN_HR_IF(E_INVALIDARG, !menu);
    const UINT flags = CMF_NORMAL | CMF_CANRENAME | (extended ? CMF_EXTENDEDVERBS : 0);
    const HRESULT hr = m_menu->QueryContextMenu(menu, 0, kFirstCommand, kLastCommand, flags);
    RETURN_IF_FAILED(hr);
    return S_OK; // QueryContextMenu returns the number of IDs used as a success code
}

std::optional<std::wstring> ContextMenu::VerbOf(UINT commandId) const
{
    if (!m_menu || commandId < kFirstCommand || commandId > kLastCommand)
    {
        return std::nullopt;
    }
    wchar_t verb[256]{};
    // Some handlers write an ANSI string even when asked for Unicode, or fail; either way
    // the command simply has no known verb.
    if (FAILED(m_menu->GetCommandString(commandId - kFirstCommand, GCS_VERBW, nullptr,
                                        reinterpret_cast<LPSTR>(verb), static_cast<UINT>(std::size(verb)))) ||
        verb[0] == L'\0')
    {
        return std::nullopt;
    }
    return std::wstring(verb);
}

HRESULT ContextMenu::Invoke(HWND owner, UINT commandId, POINT screenPt, bool shift, bool ctrl)
{
    RETURN_HR_IF(E_UNEXPECTED, !m_menu);
    RETURN_HR_IF(E_INVALIDARG, commandId < kFirstCommand || commandId > kLastCommand);
    const UINT offset = commandId - kFirstCommand;

    CMINVOKECOMMANDINFOEX info{sizeof(info)};
    info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE | (shift ? CMIC_MASK_SHIFT_DOWN : 0) |
                 (ctrl ? CMIC_MASK_CONTROL_DOWN : 0);
    info.hwnd = owner;
    info.lpVerb = MAKEINTRESOURCEA(offset);
    info.lpVerbW = MAKEINTRESOURCEW(offset);
    info.nShow = SW_SHOWNORMAL;
    info.ptInvoke = screenPt;
    return m_menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
}

HRESULT ContextMenu::Show(HWND owner, POINT screenPt, bool extended, const Track& track)
{
    RETURN_HR_IF(E_UNEXPECTED, !m_menu);
    wil::unique_hmenu menu(CreatePopupMenu());
    RETURN_LAST_ERROR_IF_NULL(menu);
    RETURN_IF_FAILED(Populate(menu.get(), extended));

    UINT command = 0;
    m_tracking = true;
    if (track)
    {
        command = track(menu.get(), owner, screenPt);
    }
    else
    {
        // A popup menu needs its owner in the foreground to close on an outside click.
        SetForegroundWindow(owner);
        command = static_cast<UINT>(TrackPopupMenuEx(menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x,
                                                     screenPt.y, owner, nullptr));
    }
    m_tracking = false;

    if (command == 0)
    {
        return S_FALSE; // dismissed
    }
    const auto verb = VerbOf(command);
    if (verb && _wcsicmp(verb->c_str(), L"rename") == 0)
    {
        return kContextMenuRename;
    }
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    return Invoke(owner, command, screenPt, shift, ctrl);
}

bool ContextMenu::HandleMenuMessage(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* result)
{
    if (!m_tracking ||
        (msg != WM_INITMENUPOPUP && msg != WM_DRAWITEM && msg != WM_MEASUREITEM && msg != WM_MENUCHAR))
    {
        return false;
    }
    LRESULT local = 0;
    LRESULT* const out = result ? result : &local;
    if (m_menu3)
    {
        return SUCCEEDED(m_menu3->HandleMenuMsg2(msg, wParam, lParam, out));
    }
    if (m_menu2 && msg != WM_MENUCHAR) // IContextMenu2 does not handle WM_MENUCHAR
    {
        *out = 0;
        return SUCCEEDED(m_menu2->HandleMenuMsg(msg, wParam, lParam));
    }
    return false;
}

} // namespace te
