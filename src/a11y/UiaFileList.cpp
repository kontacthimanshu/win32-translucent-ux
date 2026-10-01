#include <te/a11y/UiaFileList.h>

#include <te/a11y/UiaCommon.h>
#include <te/a11y/UiaFileItem.h>

#include <UIAutomationCoreApi.h>

#include <wil/result.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace te
{

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using uia::kNotAvailable;

namespace
{

template <class T> HRESULT Return(T* object, T** result)
{
    *result = object;
    if (object)
    {
        object->AddRef();
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// HeaderItem: one column header; Invoke sorts by it (repeating reverses).
// ---------------------------------------------------------------------------
class UiaHeaderItem final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                                                IRawElementProviderFragment, IInvokeProvider>
{
  public:
    HRESULT RuntimeClassInitialize(UiaFileList* list, int column)
    {
        m_list = list;
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
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        if (patternId == UIA_InvokePatternId)
        {
            *pattern = static_cast<IInvokeProvider*>(this);
            AddRef();
        }
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override
    {
        result->vt = VT_EMPTY;
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        static constexpr const wchar_t* kIds[] = {L"Header.Name", L"Header.DateModified", L"Header.Type",
                                                  L"Header.Size"};
        const SortState sort = m_list->View().Sort();
        switch (propertyId)
        {
        case UIA_ControlTypePropertyId:
            uia::SetInt(result, UIA_HeaderItemControlTypeId);
            break;
        case UIA_NamePropertyId:
            uia::SetString(result, Name());
            break;
        case UIA_AutomationIdPropertyId:
            uia::SetString(result, kIds[m_column]);
            break;
        case UIA_ItemStatusPropertyId:
            // Which column the list is sorted by, and how.
            if (static_cast<int>(sort.field) == m_column)
            {
                uia::SetString(result, sort.direction == SortDirection::Ascending ? L"Sorted ascending"
                                                                                  : L"Sorted descending");
            }
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
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        switch (direction)
        {
        case NavigateDirection_Parent:
            return m_list->MakeHeader(result);
        case NavigateDirection_NextSibling:
            return m_column + 1 < UiaFileList::kColumnCount ? m_list->MakeHeaderItem(m_column + 1, result)
                                                            : S_OK;
        case NavigateDirection_PreviousSibling:
            return m_column > 0 ? m_list->MakeHeaderItem(m_column - 1, result) : S_OK;
        default:
            return S_OK;
        }
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtimeId) override
    {
        *runtimeId = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        return uia::MakeRuntimeId({UiaFileList::kRuntimeIdHeaderItem, m_column}, runtimeId);
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override
    {
        *rect = UiaRect{};
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        *rect = m_list->ToScreen(m_list->View().HeaderCellRect(static_cast<FileView::Column>(m_column)));
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override
    {
        *roots = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override
    {
        return S_OK; // not keyboard-focusable: sorting is Ctrl+Shift+1..4
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override
    {
        *root = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        return Return(static_cast<IRawElementProviderFragmentRoot*>(m_list->Root()), root);
    }

    IFACEMETHODIMP Invoke() override
    {
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        m_list->View().SortByColumn(static_cast<FileView::Column>(m_column));
        return S_OK;
    }

  private:
    [[nodiscard]] const std::wstring& Name() const
    {
        const FileView::Strings& names = m_list->View().ColumnNames();
        const std::wstring* all[] = {&names.name, &names.dateModified, &names.type, &names.size};
        return *all[m_column];
    }

    ComPtr<UiaFileList> m_list;
    int m_column = 0;
};

// ---------------------------------------------------------------------------
// Header: the row of column headers, the list's first child.
// ---------------------------------------------------------------------------
class UiaHeader final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                                            IRawElementProviderFragment>
{
  public:
    HRESULT RuntimeClassInitialize(UiaFileList* list)
    {
        m_list = list;
        return S_OK;
    }

    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override
    {
        *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
        return S_OK;
    }
    IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** pattern) override
    {
        *pattern = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override
    {
        result->vt = VT_EMPTY;
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        switch (propertyId)
        {
        case UIA_ControlTypePropertyId:
            uia::SetInt(result, UIA_HeaderControlTypeId);
            break;
        case UIA_AutomationIdPropertyId:
            uia::SetString(result, L"Header");
            break;
        case UIA_IsEnabledPropertyId:
        case UIA_IsControlElementPropertyId:
            uia::SetBool(result, true);
            break;
        case UIA_IsContentElementPropertyId:
            uia::SetBool(result, false);
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
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        switch (direction)
        {
        case NavigateDirection_Parent:
            return Return(static_cast<IRawElementProviderFragment*>(m_list.Get()), result);
        case NavigateDirection_NextSibling:
            // The first row (T077), if the list has rows and row providers.
            // The first row, if any.
            return m_list->View().Items().empty() ? S_OK : m_list->MakeRow(0, result);
        case NavigateDirection_FirstChild:
            return m_list->MakeHeaderItem(0, result);
        case NavigateDirection_LastChild:
            return m_list->MakeHeaderItem(UiaFileList::kColumnCount - 1, result);
        default:
            return S_OK;
        }
    }
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtimeId) override
    {
        *runtimeId = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        return uia::MakeRuntimeId({UiaFileList::kRuntimeIdHeader}, runtimeId);
    }
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override
    {
        *rect = UiaRect{};
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        const FileView& view = m_list->View();
        const D2D1_RECT_F first = view.HeaderCellRect(FileView::Column::Name);
        const D2D1_RECT_F bounds = view.Bounds();
        *rect = m_list->ToScreen(D2D1::RectF(bounds.left, first.top, bounds.right, first.bottom));
        return S_OK;
    }
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override
    {
        *roots = nullptr;
        return S_OK;
    }
    IFACEMETHODIMP SetFocus() override
    {
        return S_OK;
    }
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override
    {
        *root = nullptr;
        RETURN_HR_IF(kNotAvailable, !m_list->Available());
        return Return(static_cast<IRawElementProviderFragmentRoot*>(m_list->Root()), root);
    }

  private:
    ComPtr<UiaFileList> m_list;
};

} // namespace

// ---------------------------------------------------------------------------
// UiaFileList
// ---------------------------------------------------------------------------

HRESULT UiaFileList::RuntimeClassInitialize(UiaRoot* root, Host host)
{
    RETURN_HR_IF(E_INVALIDARG, !root || !host.view || !host.toScreen || !host.fromScreen);
    m_root = root;
    m_host = std::move(host);
    return S_OK;
}

void UiaFileList::Disconnect()
{
    if (!m_root)
    {
        return;
    }
    m_root.Reset();
    m_host = {};
    LOG_IF_FAILED(UiaDisconnectProvider(this));
}

UiaRect UiaFileList::ToScreen(const D2D1_RECT_F& dip) const
{
    return m_host.toScreen ? m_host.toScreen(dip) : UiaRect{};
}

HRESULT UiaFileList::MakeHeader(IRawElementProviderFragment** result)
{
    *result = nullptr;
    ComPtr<UiaHeader> header;
    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<UiaHeader>(&header, this));
    *result = header.Detach();
    return S_OK;
}

HRESULT UiaFileList::MakeRow(std::size_t index, IRawElementProviderFragment** result)
{
    *result = nullptr;
    RETURN_HR_IF(E_INVALIDARG, index >= View().Items().size());
    return MakeFileItemProvider(this, View().Items()[index].key, View().CurrentGeneration(), result);
}

HRESULT UiaFileList::MakeCell(std::size_t index, int column, IRawElementProviderFragment** result)
{
    *result = nullptr;
    RETURN_HR_IF(E_INVALIDARG, index >= View().Items().size() || column < 0 || column >= kColumnCount);
    return MakeFileCellProvider(this, View().Items()[index].key, View().CurrentGeneration(), column, result);
}

HRESULT UiaFileList::MakeHeaderItem(int column, IRawElementProviderFragment** result)
{
    *result = nullptr;
    RETURN_HR_IF(E_INVALIDARG, column < 0 || column >= kColumnCount);
    ComPtr<UiaHeaderItem> item;
    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<UiaHeaderItem>(&item, this, column));
    *result = item.Detach();
    return S_OK;
}

float UiaFileList::ScrollRange() const noexcept
{
    return std::max(0.0f, View().ContentHeightDip() - View().ViewportHeightDip());
}

// IRawElementProviderSimple ---------------------------------------------------

IFACEMETHODIMP UiaFileList::get_ProviderOptions(ProviderOptions* options)
{
    RETURN_HR_IF_NULL(E_POINTER, options);
    *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

IFACEMETHODIMP UiaFileList::GetPatternProvider(PATTERNID patternId, IUnknown** pattern)
{
    RETURN_HR_IF_NULL(E_POINTER, pattern);
    *pattern = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    switch (patternId)
    {
    case UIA_SelectionPatternId:
        *pattern = static_cast<ISelectionProvider*>(this);
        break;
    case UIA_GridPatternId:
        *pattern = static_cast<IGridProvider*>(this);
        break;
    case UIA_TablePatternId:
        *pattern = static_cast<ITableProvider*>(this);
        break;
    case UIA_ScrollPatternId:
        *pattern = static_cast<IScrollProvider*>(this);
        break;
    default:
        return S_OK;
    }
    (*pattern)->AddRef();
    return S_OK;
}

IFACEMETHODIMP UiaFileList::GetPropertyValue(PROPERTYID propertyId, VARIANT* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    result->vt = VT_EMPTY;
    RETURN_HR_IF(kNotAvailable, !Available());
    switch (propertyId)
    {
    case UIA_ControlTypePropertyId:
        uia::SetInt(result, UIA_DataGridControlTypeId);
        break;
    case UIA_NamePropertyId:
        uia::SetString(result, m_host.name);
        break;
    case UIA_AutomationIdPropertyId:
        uia::SetString(result, L"FileList");
        break;
    case UIA_IsKeyboardFocusablePropertyId:
    case UIA_IsEnabledPropertyId:
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        uia::SetBool(result, true);
        break;
    case UIA_HasKeyboardFocusPropertyId:
        uia::SetBool(result, m_host.hasFocus && m_host.hasFocus());
        break;
    default:
        break;
    }
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_HostRawElementProvider(IRawElementProviderSimple** host)
{
    RETURN_HR_IF_NULL(E_POINTER, host);
    *host = nullptr;
    return S_OK;
}

// IRawElementProviderFragment -------------------------------------------------

IFACEMETHODIMP UiaFileList::Navigate(NavigateDirection direction, IRawElementProviderFragment** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    switch (direction)
    {
    case NavigateDirection_Parent:
        return Return(static_cast<IRawElementProviderFragment*>(m_root.Get()), result);
    case NavigateDirection_NextSibling:
    case NavigateDirection_PreviousSibling:
        return m_root->Sibling(this, direction, result);
    case NavigateDirection_FirstChild:
        return MakeHeader(result);
    case NavigateDirection_LastChild:
        return View().Items().empty() ? MakeHeader(result) : MakeRow(View().Items().size() - 1, result);
    default:
        return S_OK;
    }
}

IFACEMETHODIMP UiaFileList::GetRuntimeId(SAFEARRAY** runtimeId)
{
    RETURN_HR_IF_NULL(E_POINTER, runtimeId);
    *runtimeId = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    return uia::MakeRuntimeId({kRuntimeIdList}, runtimeId);
}

IFACEMETHODIMP UiaFileList::get_BoundingRectangle(UiaRect* rect)
{
    RETURN_HR_IF_NULL(E_POINTER, rect);
    *rect = UiaRect{};
    RETURN_HR_IF(kNotAvailable, !Available());
    *rect = ToScreen(View().Bounds());
    return S_OK;
}

IFACEMETHODIMP UiaFileList::GetEmbeddedFragmentRoots(SAFEARRAY** roots)
{
    RETURN_HR_IF_NULL(E_POINTER, roots);
    *roots = nullptr;
    return S_OK;
}

IFACEMETHODIMP UiaFileList::SetFocus()
{
    RETURN_HR_IF(kNotAvailable, !Available());
    if (m_host.setFocus)
    {
        m_host.setFocus();
    }
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_FragmentRoot(IRawElementProviderFragmentRoot** root)
{
    RETURN_HR_IF_NULL(E_POINTER, root);
    *root = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    return Return(static_cast<IRawElementProviderFragmentRoot*>(m_root.Get()), root);
}

// ISelectionProvider ------------------------------------------------------------

IFACEMETHODIMP UiaFileList::GetSelection(SAFEARRAY** selection)
{
    RETURN_HR_IF_NULL(E_POINTER, selection);
    *selection = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    std::vector<ComPtr<IRawElementProviderFragment>> rows;
    const SelectionModel& selectionState = View().SelectionState();
    for (std::size_t i = 0; i < View().Items().size(); ++i)
    {
        ComPtr<IRawElementProviderFragment> row;
        if (selectionState.IsSelected(i) && SUCCEEDED(MakeRow(i, &row)))
        {
            rows.push_back(std::move(row));
        }
    }
    return uia::MakeProviderArray(rows, selection);
}

IFACEMETHODIMP UiaFileList::get_CanSelectMultiple(BOOL* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = TRUE;
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_IsSelectionRequired(BOOL* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = FALSE;
    return S_OK;
}

// IGridProvider -----------------------------------------------------------------

IFACEMETHODIMP UiaFileList::GetItem(int row, int column, IRawElementProviderSimple** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    RETURN_HR_IF(E_INVALIDARG, row < 0 || static_cast<std::size_t>(row) >= View().Items().size() ||
                                   column < 0 || column >= kColumnCount);
    ComPtr<IRawElementProviderFragment> cell;
    RETURN_IF_FAILED(MakeCell(static_cast<std::size_t>(row), column, &cell));
    return cell.CopyTo(result);
}

IFACEMETHODIMP UiaFileList::get_RowCount(int* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    RETURN_HR_IF(kNotAvailable, !Available());
    *result = static_cast<int>(View().Items().size());
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_ColumnCount(int* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = kColumnCount;
    return S_OK;
}

// ITableProvider ----------------------------------------------------------------

IFACEMETHODIMP UiaFileList::GetRowHeaders(SAFEARRAY** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr; // no row headers
    return S_OK;
}

IFACEMETHODIMP UiaFileList::GetColumnHeaders(SAFEARRAY** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    std::vector<ComPtr<IRawElementProviderFragment>> headers;
    for (int column = 0; column < kColumnCount; ++column)
    {
        ComPtr<IRawElementProviderFragment> item;
        RETURN_IF_FAILED(MakeHeaderItem(column, &item));
        headers.push_back(std::move(item));
    }
    return uia::MakeProviderArray(headers, result);
}

IFACEMETHODIMP UiaFileList::get_RowOrColumnMajor(RowOrColumnMajor* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = RowOrColumnMajor_RowMajor;
    return S_OK;
}

// IScrollProvider ---------------------------------------------------------------

IFACEMETHODIMP UiaFileList::Scroll(ScrollAmount horizontal, ScrollAmount vertical)
{
    RETURN_HR_IF(kNotAvailable, !Available());
    RETURN_HR_IF(E_INVALIDARG, horizontal != ScrollAmount_NoAmount); // no horizontal scrolling
    RETURN_HR_IF(uia::kInvalidOperation, vertical != ScrollAmount_NoAmount && ScrollRange() <= 0.0f);
    float delta = 0.0f;
    switch (vertical)
    {
    case ScrollAmount_SmallDecrement:
        delta = -View().RowHeight();
        break;
    case ScrollAmount_SmallIncrement:
        delta = View().RowHeight();
        break;
    case ScrollAmount_LargeDecrement:
        delta = -View().ViewportHeightDip();
        break;
    case ScrollAmount_LargeIncrement:
        delta = View().ViewportHeightDip();
        break;
    default:
        break;
    }
    View().SetScrollOffset(View().ScrollOffset() + delta);
    return S_OK;
}

IFACEMETHODIMP UiaFileList::SetScrollPercent(double horizontal, double vertical)
{
    RETURN_HR_IF(kNotAvailable, !Available());
    RETURN_HR_IF(E_INVALIDARG, horizontal != UIA_ScrollPatternNoScroll);
    if (vertical == UIA_ScrollPatternNoScroll)
    {
        return S_OK;
    }
    RETURN_HR_IF(E_INVALIDARG, vertical < 0.0 || vertical > 100.0);
    RETURN_HR_IF(uia::kInvalidOperation, ScrollRange() <= 0.0f);
    View().SetScrollOffset(static_cast<float>(vertical / 100.0) * ScrollRange());
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_HorizontalScrollPercent(double* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = UIA_ScrollPatternNoScroll;
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_VerticalScrollPercent(double* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    RETURN_HR_IF(kNotAvailable, !Available());
    const float range = ScrollRange();
    *result = range > 0.0f ? 100.0 * View().ScrollOffset() / range : UIA_ScrollPatternNoScroll;
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_HorizontalViewSize(double* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = 100.0;
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_VerticalViewSize(double* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    RETURN_HR_IF(kNotAvailable, !Available());
    const float content = View().ContentHeightDip();
    *result = content > View().ViewportHeightDip() ? 100.0 * View().ViewportHeightDip() / content : 100.0;
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_HorizontallyScrollable(BOOL* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = FALSE;
    return S_OK;
}

IFACEMETHODIMP UiaFileList::get_VerticallyScrollable(BOOL* result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    RETURN_HR_IF(kNotAvailable, !Available());
    *result = ScrollRange() > 0.0f ? TRUE : FALSE;
    return S_OK;
}

// IUiaFragmentExtension ---------------------------------------------------------

IFACEMETHODIMP UiaFileList::HitTest(double x, double y, IRawElementProviderFragment** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    const D2D1_POINT_2F point = m_host.fromScreen(x, y);
    for (int column = 0; column < kColumnCount; ++column)
    {
        const D2D1_RECT_F cell = View().HeaderCellRect(static_cast<FileView::Column>(column));
        if (point.x >= cell.left && point.x < cell.right && point.y >= cell.top && point.y < cell.bottom)
        {
            return MakeHeaderItem(column, result);
        }
    }
    if (const auto row = View().RowAt(point))
    {
        // The cell under the point, within its row.
        for (int column = 0; column < kColumnCount; ++column)
        {
            const D2D1_RECT_F cell = View().CellRect(*row, static_cast<FileView::Column>(column));
            if (point.x >= cell.left && point.x < cell.right)
            {
                return MakeCell(*row, column, result);
            }
        }
        return MakeRow(*row, result);
    }
    return Return(static_cast<IRawElementProviderFragment*>(this), result);
}

IFACEMETHODIMP UiaFileList::FocusedDescendant(IRawElementProviderFragment** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(kNotAvailable, !Available());
    if (const auto focus = View().SelectionState().FocusIndex(); focus && *focus < View().Items().size())
    {
        return MakeRow(*focus, result);
    }
    return Return(static_cast<IRawElementProviderFragment*>(this), result);
}

} // namespace te
