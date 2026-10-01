#pragma once

// Shell data types shared by the messages, interfaces and UI (data-model: Location,
// FileItem). Declarations and plain data only; ShellLocation's behaviour is
// implemented in src/shell/ShellLocation.cpp (T053).

#include <windows.h>

#include <shobjidl_core.h>
#include <shtypes.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <cstdint>
#include <optional>
#include <string>

namespace te
{

// A navigable Shell location (data-model: Location). Owns an absolute PIDL; the
// display strings and attributes are captured when the location is created.
class ShellLocation
{
  public:
    // An empty location. IsValid() is false until assigned from FromIdList().
    ShellLocation() = default;

    // Clones pidl (ILCloneFull) and reads the display name, parsing path and
    // SFGAO_FILESYSTEM from the Shell.
    static HRESULT FromIdList(PCIDLIST_ABSOLUTE pidl, ShellLocation* out);

    ShellLocation(const ShellLocation& other);            // clones the PIDL
    ShellLocation& operator=(const ShellLocation& other); // clones the PIDL
    ShellLocation(ShellLocation&&) noexcept = default;
    ShellLocation& operator=(ShellLocation&&) noexcept = default;
    ~ShellLocation() = default;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_pidl != nullptr;
    }
    [[nodiscard]] PCIDLIST_ABSOLUTE IdList() const noexcept
    {
        return m_pidl.get();
    }
    [[nodiscard]] const std::wstring& DisplayName() const noexcept
    {
        return m_displayName;
    }
    [[nodiscard]] const std::optional<std::wstring>& ParsingPath() const noexcept
    {
        return m_parsingPath;
    }
    [[nodiscard]] bool IsFileSystem() const noexcept
    {
        return m_isFileSystem;
    }

    // The parent location (ILRemoveLastID on a clone); empty for the Desktop root.
    [[nodiscard]] std::optional<ShellLocation> Parent() const;

    // Lazily created with SHCreateItemFromIDList.
    [[nodiscard]] HRESULT Item(IShellItem** item) const;

    // Identity comparison with ILIsEqual.
    [[nodiscard]] bool operator==(const ShellLocation& other) const;

  private:
    wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> m_pidl;
    mutable wil::com_ptr<IShellItem> m_item;
    std::wstring m_displayName;                // SIGDN_NORMALDISPLAY
    std::optional<std::wstring> m_parsingPath; // SIGDN_DESKTOPABSOLUTEPARSING
    bool m_isFileSystem = false;               // SFGAO_FILESYSTEM
};

// One enumerated item: the data-model FileItem fields without UI state. Produced on
// the enumeration worker and moved to the UI thread in an EnumBatch.
struct ShellItemInfo
{
    wil::unique_cotaskmem_ptr<ITEMID_CHILD> childPidl; // identity within the parent folder
    std::wstring name;                                 // SIGDN_NORMALDISPLAY
    // The name inline rename starts from (T071). For file-system items the real file name
    // with its extension (SIGDN_PARENTRELATIVEPARSING): IFileOperation::RenameItem takes
    // the raw name, so an edit without the (possibly hidden) extension would drop it.
    // Otherwise SIGDN_PARENTRELATIVEEDITING.
    std::wstring editName;
    std::wstring typeText;             // PKEY_ItemTypeText
    std::optional<std::uint64_t> size; // PKEY_Size; empty for folders
    std::optional<FILETIME> modified;  // PKEY_DateModified
    bool isFolder = false;             // SFGAO_FOLDER and not SFGAO_STREAM
    bool isHidden = false;             // SFGAO_HIDDEN
    bool canRename = false;            // SFGAO_CANRENAME
    bool canDelete = false;            // SFGAO_CANDELETE
    bool canCopy = false;              // SFGAO_CANCOPY
    bool canMove = false;              // SFGAO_CANMOVE
};

} // namespace te
