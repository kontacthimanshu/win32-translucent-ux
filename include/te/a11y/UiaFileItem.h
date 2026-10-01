#pragma once

// UI Automation providers for the file list's rows and cells (T077; research R-09; UI
// contract §5). A row is a DataItem named after the item, with SelectionItem, Invoke
// (open), ScrollItem and GridItem; its children are Text cells (name, date modified, type,
// size) with GridItem and TableItem. Providers are keyed by the item's key and the listing
// generation: after the folder is listed again they report UIA_E_ELEMENTNOTAVAILABLE.

#include <te/core/Types.h>

#include <objbase.h>

#include <UIAutomation.h>

#include <cstddef>

namespace te
{

class UiaFileList;

HRESULT MakeFileItemProvider(UiaFileList* list, std::size_t key, Generation gen,
                             IRawElementProviderFragment** result);
HRESULT MakeFileCellProvider(UiaFileList* list, std::size_t key, Generation gen, int column,
                             IRawElementProviderFragment** result);

} // namespace te
