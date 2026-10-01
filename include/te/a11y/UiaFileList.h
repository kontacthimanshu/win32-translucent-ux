#pragma once

// UI Automation provider for the file list (T076; research R-09; UI contract §5): a
// DataGrid named "Items" with Selection (multiple), Grid, Table and Scroll, backed by
// FileView. Its first child is a Header with one HeaderItem per column, whose Invoke
// sorts; then one DataItem per row (UiaFileItem, T077) with Text cells.

#include <te/a11y/UiaRoot.h>
#include <te/ui/FileView.h>

#include <d2d1.h>

#include <wrl/client.h>

#include <functional>
#include <string>

namespace te
{

class UiaFileList final
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IRawElementProviderSimple, IRawElementProviderFragment,
                                          ISelectionProvider, IGridProvider, ITableProvider, IScrollProvider,
                                          IUiaFragmentExtension>
{
  public:
    struct Host
    {
        FileView* view = nullptr;
        std::wstring name = L"Items"; // IDS_A11Y_FILE_LIST
        // Client DIPs -> screen pixels, and back.
        std::function<UiaRect(const D2D1_RECT_F&)> toScreen;
        std::function<D2D1_POINT_2F(double x, double y)> fromScreen;
        std::function<bool()> hasFocus;
        std::function<void()> setFocus;
        // Opens an item (a row's Invoke, T077). Should post: opening can navigate away
        // or show a modal dialog, which must not happen inside the UIA call.
        std::function<void(std::size_t key, Generation gen)> open;
    };

    static constexpr int kColumnCount = 4;
    static constexpr int kRuntimeIdList = 2;
    static constexpr int kRuntimeIdHeader = 3;
    static constexpr int kRuntimeIdHeaderItem = 4;

    UiaFileList() = default;
    HRESULT RuntimeClassInitialize(UiaRoot* root, Host host);
    void Disconnect();
    [[nodiscard]] bool Available() const noexcept
    {
        return m_root && m_root->IsConnected() && m_host.view;
    }

    // For the Header, HeaderItem and (T077) DataItem providers.
    [[nodiscard]] FileView& View() const noexcept
    {
        return *m_host.view;
    }
    [[nodiscard]] UiaRoot* Root() const noexcept
    {
        return m_root.Get();
    }
    [[nodiscard]] const Host& HostCallbacks() const noexcept
    {
        return m_host;
    }
    [[nodiscard]] UiaRect ToScreen(const D2D1_RECT_F& dip) const;
    // A new Header provider (a child of this list).
    HRESULT MakeHeader(IRawElementProviderFragment** result);
    // A new HeaderItem provider for a column.
    HRESULT MakeHeaderItem(int column, IRawElementProviderFragment** result);
    // A new DataItem provider for the row at `index`, and a Text cell of it (T077).
    HRESULT MakeRow(std::size_t index, IRawElementProviderFragment** result);
    HRESULT MakeCell(std::size_t index, int column, IRawElementProviderFragment** result);

    // IRawElementProviderSimple
    IFACEMETHODIMP get_ProviderOptions(ProviderOptions* options) override;
    IFACEMETHODIMP GetPatternProvider(PATTERNID patternId, IUnknown** pattern) override;
    IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId, VARIANT* result) override;
    IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** host) override;

    // IRawElementProviderFragment
    IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** result) override;
    IFACEMETHODIMP GetRuntimeId(SAFEARRAY** runtimeId) override;
    IFACEMETHODIMP get_BoundingRectangle(UiaRect* rect) override;
    IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) override;
    IFACEMETHODIMP SetFocus() override;
    IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) override;

    // ISelectionProvider
    IFACEMETHODIMP GetSelection(SAFEARRAY** selection) override;
    IFACEMETHODIMP get_CanSelectMultiple(BOOL* result) override;
    IFACEMETHODIMP get_IsSelectionRequired(BOOL* result) override;

    // IGridProvider
    IFACEMETHODIMP GetItem(int row, int column, IRawElementProviderSimple** result) override;
    IFACEMETHODIMP get_RowCount(int* result) override;
    IFACEMETHODIMP get_ColumnCount(int* result) override;

    // ITableProvider
    IFACEMETHODIMP GetRowHeaders(SAFEARRAY** result) override;
    IFACEMETHODIMP GetColumnHeaders(SAFEARRAY** result) override;
    IFACEMETHODIMP get_RowOrColumnMajor(RowOrColumnMajor* result) override;

    // IScrollProvider
    IFACEMETHODIMP Scroll(ScrollAmount horizontal, ScrollAmount vertical) override;
    IFACEMETHODIMP SetScrollPercent(double horizontal, double vertical) override;
    IFACEMETHODIMP get_HorizontalScrollPercent(double* result) override;
    IFACEMETHODIMP get_VerticalScrollPercent(double* result) override;
    IFACEMETHODIMP get_HorizontalViewSize(double* result) override;
    IFACEMETHODIMP get_VerticalViewSize(double* result) override;
    IFACEMETHODIMP get_HorizontallyScrollable(BOOL* result) override;
    IFACEMETHODIMP get_VerticallyScrollable(BOOL* result) override;

    // IUiaFragmentExtension
    IFACEMETHODIMP HitTest(double x, double y, IRawElementProviderFragment** result) override;
    IFACEMETHODIMP FocusedDescendant(IRawElementProviderFragment** result) override;

  private:
    [[nodiscard]] float ScrollRange() const noexcept;

    Microsoft::WRL::ComPtr<UiaRoot> m_root;
    Host m_host;
};

} // namespace te
