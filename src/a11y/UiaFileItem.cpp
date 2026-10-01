#include <te/a11y/UiaFileItem.h>

#include <te/a11y/UiaCommon.h>
#include <te/a11y/UiaFileList.h>
#include <te/ui/FileItemFormat.h>

#include <wil/result.h>

#include <algorithm>
#include <optional>
#include <string>

namespace te
{

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using uia::kNotAvailable;

namespace
{

constexpr int kRuntimeIdRow = 5;
constexpr int kRuntimeIdCell = 6;

template <class T> HRESULT Return(T* object, T** result)
{
    *result = object;
    if (object)
    {
        object->AddRef();
    }
    return S_OK;
}

int Low31(std::uint64_t value)
{
    return static_cast<int>(value & 0x7FFFFFFF);
}

// What rows and cells share: the item they stand for, found again on every call.
class ItemRef
{
  public:
    void Init(UiaFileList* list, std::size_t key, Generation gen)
    {
        m_list = list;
        m_key = key;
        m_gen = gen;
    }

    // The item's current row, or nothing if the list was listed again or the item is gone.
    [[nodiscard]] std::optional<std::size_t> Index() const
    {
        if (!m_list->Available() || m_list->View().CurrentGeneration() != m_gen)
        {
            return std::nullopt;
        }
        return m_list->View().IndexOfKey(m_key);
    }
    [[nodiscard]] const FileItem& Item(std::size_t index) const
    {
        return m_list->View().Items()[index];
    }
    // The part of `dip` inside the rows area (below the header), on screen; empty if none.
    [[nodiscard]] UiaRect Visible(D2D1_RECT_F dip) const
    {
        const FileView& view = m_list->View();
        const float top = view.Bounds().top + view.HeaderHeight();
        dip.top = std::max(dip.top, top);
        dip.bottom = std::min(dip.bottom, view.Bounds().bottom);
        if (dip.bottom <= dip.top)
        {
            return UiaRect{};
        }
        return m_list->ToScreen(dip);
    }

    ComPtr<UiaFileList> m_list;
    std::size_t m_key = 0;
    Generation m_gen = 0;
};

std::wstring CellText(const FileItem& item, int column)
{
    switch (column)
    {
    case 0:
        return item.info.name;
    case 1:
        return FileItemFormat::Modified(item.info.modified);
    case 2:
        return item.info.typeText;
    default:
        return FileItemFormat::Size(item.info.size);
    }
}

// ---------------------------------------------------------------------------
// A cell: Text, with GridItem and TableItem.
// ---------------------------------------------------------------------------
class UiaFileCell final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                          IRawElementProviderFragment, IGridItemProvider, ITableItemProvider>
{
  public:
    HRESULT RuntimeClassInitialize(UiaFileList* list, std::size_t key, Generation gen, int column)
    {
        m_ref.Init(list, key, gen);
        m_column = column;
        return S_OK;
    }

    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override
    {
        *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** pattern) override
    {
        *pattern = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        if (patternId == UIA_GridItemPatternId)
        {
            *pattern = static_cast<IGridItemProvider*>(this);
        }
        else if (patternId == UIA_TableItemPatternId)
        {
            *pattern = static_cast<ITableItemProvider*>(this);
        }
        if (*pattern)
        {
            (*pattern)->AddRef();
        }
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override
    {
        result->vt = VT_EMPTY;
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        switch (propertyId)
        {
        case UIA_ControlTypePropertyId:
            uia::SetInt(result, UIA_TextControlTypeId);
            break;
        case UIA_NamePropertyId:
            uia::SetString(result, CellText(m_ref.Item(*index), m_column));
            break;
        case UIA_IsOffscreenPropertyId:
            uia::SetBool(result, !m_ref.m_list->View().IsRowVisible(*index));
            break;
        case UIA_IsEnabledPropertyId:
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            uia::SetBool(result, true);
            break;
        default:
            break;
        }
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** host) override
    {
        *host = nullptr;
        return S_OK;
    }

    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        *result = nullptr;
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        switch (direction)
        {
        case NavigateDirection_Parent:
            return m_ref.m_list->MakeRow(*index, result);
        case NavigateDirection_NextSibling:
            return m_column + 1 < UiaFileList::kColumnCount
                       ? m_ref.m_list->MakeCell(*index, m_column + 1, result)
                       : S_OK;
        case NavigateDirection_PreviousSibling:
            return m_column > 0 ? m_ref.m_list->MakeCell(*index, m_column - 1, result) : S_OK;
        default:
            return S_OK;
        }
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtimeId) override
    {
        *runtimeId = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        return uia::MakeRuntimeId({kRuntimeIdCell, Low31(m_ref.m_key), Low31(m_ref.m_gen), m_column},
                                  runtimeId);
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override
    {
        *rect = UiaRect{};
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        *rect = m_ref.Visible(m_ref.m_list->View().CellRect(*index, static_cast<FileView::Column>(m_column)));
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override
    {
        *roots = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override
    {
        return S_OK; // the row takes the focus, not the cell
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override
    {
        *root = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        return Return(static_cast<IRawElementProviderFragmentRoot*>(m_ref.m_list->Root()), root);
    }

    // IGridItemProvider
    IFACEMETHODIMP get_Row(int* row) override
    {
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        *row = static_cast<int>(*index);
        return S_OK;
    }
    IFACEMETHODIMP get_Column(int* column) override
    {
        *column = m_column;
        return S_OK;
    }
    IFACEMETHODIMP get_RowSpan(int* span) override
    {
        *span = 1;
        return S_OK;
    }
    IFACEMETHODIMP get_ColumnSpan(int* span) override
    {
        *span = 1;
        return S_OK;
    }
    IFACEMETHODIMP get_ContainingGrid(IRawElementProviderSimple** grid) override
    {
        *grid = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        return Return(static_cast<IRawElementProviderSimple*>(m_ref.m_list.Get()), grid);
    }

    // ITableItemProvider
    IFACEMETHODIMP GetRowHeaderItems(SAFEARRAY** result) override
    {
        *result = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP GetColumnHeaderItems(SAFEARRAY** result) override
    {
        *result = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        ComPtr<IRawElementProviderFragment> header;
        RETURN_IF_FAILED(m_ref.m_list->MakeHeaderItem(m_column, &header));
        const ComPtr<IRawElementProviderFragment> headers[] = {header};
        return uia::MakeProviderArray(headers, result);
    }

  private:
    ItemRef m_ref;
    int m_column = 0;
};

// ---------------------------------------------------------------------------
// A row: DataItem with SelectionItem, Invoke, ScrollItem and GridItem.
// ---------------------------------------------------------------------------
class UiaFileItem final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                                              IRawElementProviderFragment, ISelectionItemProvider,
                                              IInvokeProvider, IScrollItemProvider, IGridItemProvider>
{
  public:
    HRESULT RuntimeClassInitialize(UiaFileList* list, std::size_t key, Generation gen)
    {
        m_ref.Init(list, key, gen);
        return S_OK;
    }

    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override
    {
        *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** pattern) override
    {
        *pattern = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        switch (patternId)
        {
        case UIA_SelectionItemPatternId:
            *pattern = static_cast<ISelectionItemProvider*>(this);
            break;
        case UIA_InvokePatternId:
            *pattern = static_cast<IInvokeProvider*>(this);
            break;
        case UIA_ScrollItemPatternId:
            *pattern = static_cast<IScrollItemProvider*>(this);
            break;
        case UIA_GridItemPatternId:
            *pattern = static_cast<IGridItemProvider*>(this);
            break;
        default:
            return S_OK;
        }
        (*pattern)->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override
    {
        result->vt = VT_EMPTY;
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        const FileView& view = m_ref.m_list->View();
        switch (propertyId)
        {
        case UIA_ControlTypePropertyId:
            uia::SetInt(result, UIA_DataItemControlTypeId);
            break;
        case UIA_NamePropertyId:
            uia::SetString(result, m_ref.Item(*index).info.name);
            break;
        case UIA_IsKeyboardFocusablePropertyId:
        case UIA_IsEnabledPropertyId:
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            uia::SetBool(result, true);
            break;
        case UIA_HasKeyboardFocusPropertyId: {
            const auto& hasFocus = m_ref.m_list->HostCallbacks().hasFocus;
            uia::SetBool(result, hasFocus && hasFocus() && view.SelectionState().FocusIndex() == *index);
            break;
        }
        case UIA_IsOffscreenPropertyId:
            uia::SetBool(result, !view.IsRowVisible(*index));
            break;
        default:
            break;
        }
        return S_OK;
    }
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** host) override
    {
        *host = nullptr;
        return S_OK;
    }

    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override
    {
        *result = nullptr;
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        UiaFileList* const list = m_ref.m_list.Get();
        switch (direction)
        {
        case NavigateDirection_Parent:
            return Return(static_cast<IRawElementProviderFragment*>(list), result);
        case NavigateDirection_NextSibling:
            return *index + 1 < list->View().Items().size() ? list->MakeRow(*index + 1, result) : S_OK;
        case NavigateDirection_PreviousSibling:
            // Before the first row comes the header.
            return *index > 0 ? list->MakeRow(*index - 1, result) : list->MakeHeader(result);
        case NavigateDirection_FirstChild:
            return list->MakeCell(*index, 0, result);
        case NavigateDirection_LastChild:
            return list->MakeCell(*index, UiaFileList::kColumnCount - 1, result);
        default:
            return S_OK;
        }
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtimeId) override
    {
        *runtimeId = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        return uia::MakeRuntimeId({kRuntimeIdRow, Low31(m_ref.m_key), Low31(m_ref.m_gen)}, runtimeId);
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override
    {
        *rect = UiaRect{};
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        *rect = m_ref.Visible(m_ref.m_list->View().RowRect(*index));
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override
    {
        *roots = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override
    {
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        if (const auto& setFocus = m_ref.m_list->HostCallbacks().setFocus)
        {
            setFocus();
        }
        m_ref.m_list->View().FocusRow(*index);
        return S_OK;
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override
    {
        *root = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        return Return(static_cast<IRawElementProviderFragmentRoot*>(m_ref.m_list->Root()), root);
    }

    // ISelectionItemProvider
    IFACEMETHODIMP Select() override
    {
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        m_ref.m_list->View().SelectOnly(*index);
        return S_OK;
    }
    IFACEMETHODIMP AddToSelection() override
    {
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        m_ref.m_list->View().SetSelected(*index, true);
        return S_OK;
    }
    IFACEMETHODIMP RemoveFromSelection() override
    {
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        m_ref.m_list->View().SetSelected(*index, false);
        return S_OK;
    }
    IFACEMETHODIMP get_IsSelected(BOOL* selected) override
    {
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        *selected = m_ref.m_list->View().SelectionState().IsSelected(*index) ? TRUE : FALSE;
        return S_OK;
    }
    IFACEMETHODIMP get_SelectionContainer(IRawElementProviderSimple** container) override
    {
        *container = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        return Return(static_cast<IRawElementProviderSimple*>(m_ref.m_list.Get()), container);
    }

    // IInvokeProvider: open, as Enter or a double-click does (posted by the owner).
    IFACEMETHODIMP Invoke() override
    {
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        if (const auto& open = m_ref.m_list->HostCallbacks().open)
        {
            open(m_ref.m_key, m_ref.m_gen);
        }
        return S_OK;
    }

    // IScrollItemProvider
    IFACEMETHODIMP ScrollIntoView() override
    {
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        m_ref.m_list->View().EnsureVisible(*index);
        return S_OK;
    }

    // IGridItemProvider: the row spans the grid's columns.
    IFACEMETHODIMP get_Row(int* row) override
    {
        const auto index = m_ref.Index();
        RETURN_HR_IF(kNotAvailable, !index);
        *row = static_cast<int>(*index);
        return S_OK;
    }
    IFACEMETHODIMP get_Column(int* column) override
    {
        *column = 0;
        return S_OK;
    }
    IFACEMETHODIMP get_RowSpan(int* span) override
    {
        *span = 1;
        return S_OK;
    }
    IFACEMETHODIMP get_ColumnSpan(int* span) override
    {
        *span = UiaFileList::kColumnCount;
        return S_OK;
    }
    IFACEMETHODIMP get_ContainingGrid(IRawElementProviderSimple** grid) override
    {
        *grid = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_ref.Index());
        return Return(static_cast<IRawElementProviderSimple*>(m_ref.m_list.Get()), grid);
    }

  private:
    ItemRef m_ref;
};

} // namespace

HRESULT MakeFileItemProvider(UiaFileList* list, std::size_t key, Generation gen,
                             IRawElementProviderFragment** result)
{
    *result = nullptr;
    ComPtr<UiaFileItem> item;
    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<UiaFileItem>(&item, list, key, gen));
    *result = item.Detach();
    return S_OK;
}

HRESULT MakeFileCellProvider(UiaFileList* list, std::size_t key, Generation gen, int column,
                             IRawElementProviderFragment** result)
{
    *result = nullptr;
    ComPtr<UiaFileCell> cell;
    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<UiaFileCell>(&cell, list, key, gen, column));
    *result = cell.Detach();
    return S_OK;
}

} // namespace te
