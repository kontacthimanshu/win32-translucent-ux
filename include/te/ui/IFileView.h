#pragma once

// The custom Direct2D file list (research R-06, R-09; FR-010, FR-011).

#include <te/appearance/AppearanceSettings.h>
#include <te/core/Types.h>
#include <te/ui/FileItem.h>
#include <te/ui/SortTypes.h>

#include <windows.h>

#include <ole2.h>

#include <UIAutomationCore.h>
#include <d2d1_1.h>

#include <wil/com.h>

#include <cstddef>
#include <string>
#include <vector>

namespace te
{

class IFileView
{
  public:
    virtual ~IFileView() = default;

    virtual void BeginLocation(Generation gen) = 0; // clears items and selection
    virtual void AppendItems(Generation gen, std::vector<FileItem>&&) = 0;
    virtual void SetIcon(Generation gen, std::size_t itemKey, wil::com_ptr<ID2D1Bitmap1>) = 0;
    // Only after PostRenameItem reports success (spec US4-3).
    virtual void ApplyRename(std::size_t itemKey, std::wstring newName) = 0;
    virtual void SetSort(SortState) = 0;
    virtual std::vector<const FileItem*> Selection() const = 0;

    // Layout rectangle in DIPs, set by MainWindow from MainLayout (T023).
    virtual void SetBounds(const D2D1_RECT_F& bounds) = 0;
    virtual void Render(ID2D1DeviceContext*, const EffectiveAppearance&) = 0;
    virtual IRawElementProviderFragment* Automation() = 0; // UIA (research R-09)
};

} // namespace te
