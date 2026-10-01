#include <te/shell/ShellTypes.h>

#include <shlobj.h>

#include <wil/result.h>

#include <utility>

namespace te
{

namespace
{

wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> Clone(PCIDLIST_ABSOLUTE pidl)
{
    // ILCloneFull returns an __unaligned pointer on x64; the allocation itself is aligned.
    return wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE>(
        pidl ? reinterpret_cast<ITEMIDLIST_ABSOLUTE*>(ILCloneFull(pidl)) : nullptr);
}

std::optional<std::wstring> ReadName(IShellItem* item, SIGDN form)
{
    wil::unique_cotaskmem_string name;
    if (SUCCEEDED(item->GetDisplayName(form, &name)) && name)
    {
        return std::wstring(name.get());
    }
    return std::nullopt;
}

} // namespace

HRESULT ShellLocation::FromIdList(PCIDLIST_ABSOLUTE pidl, ShellLocation* out)
{
    RETURN_HR_IF(E_POINTER, out == nullptr);
    RETURN_HR_IF(E_INVALIDARG, pidl == nullptr);

    ShellLocation location;
    location.m_pidl = Clone(pidl);
    RETURN_IF_NULL_ALLOC(location.m_pidl);

    // The item is only used here to read the strings and attributes; it is not kept, so a
    // copy handed to a worker thread creates its own item in its own apartment (Item()).
    wil::com_ptr<IShellItem> item;
    RETURN_IF_FAILED(SHCreateItemFromIDList(location.m_pidl.get(), IID_PPV_ARGS(&item)));
    location.m_displayName = ReadName(item.get(), SIGDN_NORMALDISPLAY).value_or(L"");
    // Also filled for virtual folders, e.g. "::{20D04FE0-...}" for This PC.
    location.m_parsingPath = ReadName(item.get(), SIGDN_DESKTOPABSOLUTEPARSING);
    SFGAOF attributes = 0;
    if (SUCCEEDED(item->GetAttributes(SFGAO_FILESYSTEM, &attributes)))
    {
        location.m_isFileSystem = (attributes & SFGAO_FILESYSTEM) != 0;
    }

    *out = std::move(location);
    return S_OK;
}

// Copies clone the PIDL and the captured strings, but never share the IShellItem: the
// copy may be used on another thread (the enumeration and icon workers).
ShellLocation::ShellLocation(const ShellLocation& other)
    : m_pidl(Clone(other.m_pidl.get())), m_displayName(other.m_displayName),
      m_parsingPath(other.m_parsingPath), m_isFileSystem(other.m_isFileSystem)
{
}

ShellLocation& ShellLocation::operator=(const ShellLocation& other)
{
    if (this != &other)
    {
        ShellLocation copy(other);
        *this = std::move(copy);
    }
    return *this;
}

std::optional<ShellLocation> ShellLocation::Parent() const
{
    // The Desktop (the empty ID list) is the root and has no parent.
    if (!m_pidl || ILIsEmpty(m_pidl.get()))
    {
        return std::nullopt;
    }
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> parent = Clone(m_pidl.get());
    if (!parent || !ILRemoveLastID(parent.get()))
    {
        return std::nullopt;
    }
    ShellLocation location;
    if (FAILED_LOG(FromIdList(parent.get(), &location)))
    {
        return std::nullopt;
    }
    return location;
}

HRESULT ShellLocation::Item(IShellItem** item) const
{
    RETURN_HR_IF(E_POINTER, item == nullptr);
    *item = nullptr;
    RETURN_HR_IF(E_UNEXPECTED, !m_pidl);
    if (!m_item)
    {
        RETURN_IF_FAILED(SHCreateItemFromIDList(m_pidl.get(), IID_PPV_ARGS(&m_item)));
    }
    m_item.copy_to(item);
    return S_OK;
}

bool ShellLocation::operator==(const ShellLocation& other) const
{
    if (!m_pidl || !other.m_pidl)
    {
        return !m_pidl && !other.m_pidl;
    }
    return ILIsEqual(m_pidl.get(), other.m_pidl.get()) != FALSE;
}

} // namespace te
