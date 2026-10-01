// The file list's UI Automation provider (T076; research R-09; UI contract §5), through
// the real UIA client against the main window over a temp folder of 60 files: a DataGrid
// "Items" with Selection, Grid, Table and Scroll; a Header of four HeaderItems whose
// Invoke sorts; scrolling; hit testing onto the headers; focus; disconnection.
//
// Run each test in its own process, as ctest does (gtest_discover_tests): UI Automation
// keeps process-wide state keyed by window handles, and when many windows are created and
// destroyed in one process a reused handle can carry an earlier test's state into a later
// one (seen with --gtest_shuffle in T077). The app has one main window per process.

#include <te/app/CommandIds.h>
#include <te/app/MainWindow.h>
#include <te/core/Messages.h>
#include <te/ui/FileItemFormat.h>

#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

namespace fs = std::filesystem;

void Pump(DWORD ms, const std::function<bool()>& done = {})
{
    const ULONGLONG until = GetTickCount64() + ms;
    MSG msg{};
    while (GetTickCount64() < until)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (done && done())
        {
            return;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
}

void WithClient(const std::function<void(IUIAutomation*)>& client)
{
    std::atomic<bool> done{false};
    std::thread thread([&] {
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        {
            {
                wil::com_ptr<IUIAutomation> uia;
                if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(&uia))))
                {
                    client(uia.get());
                }
            }
            CoUninitialize();
        }
        done = true;
    });
    Pump(60000, [&] { return done.load(); });
    thread.join();
}

// Creates the window's UIA root the way UIA does, through a client request. (Sending
// WM_GETOBJECT directly from the test made UIA stop asking later windows of the same
// process for their providers.)
void RequestRoot(HWND hwnd)
{
    WithClient([hwnd](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> element;
        uia->ElementFromHandle(hwnd, &element);
    });
}

std::wstring NameOf(IUIAutomationElement* element)
{
    wil::unique_bstr name;
    if (!element || FAILED(element->get_CurrentName(&name)) || !name)
    {
        return L"<none>";
    }
    return name.get();
}

wil::com_ptr<IUIAutomationElement> FindDataGrid(IUIAutomation* uia, HWND hwnd)
{
    wil::com_ptr<IUIAutomationElement> window;
    if (FAILED(uia->ElementFromHandle(hwnd, &window)))
    {
        return nullptr;
    }
    wil::unique_variant value;
    value.vt = VT_I4;
    value.lVal = UIA_DataGridControlTypeId;
    wil::com_ptr<IUIAutomationCondition> condition;
    wil::com_ptr<IUIAutomationElement> grid;
    if (SUCCEEDED(uia->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &condition)))
    {
        window->FindFirst(TreeScope_Descendants, condition.get(), &grid);
    }
    return grid;
}

class UiaFileListTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_uninitialize = SUCCEEDED(OleInitialize(nullptr));
        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-uialist-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root);
        for (int i = 0; i < 60; ++i)
        {
            std::ofstream(m_root / (L"file" + std::to_wstring(100 + i) + L".txt")) << std::string(i, 'x');
        }

        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"UIA file list test";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = m_root.wstring();
        m_window = std::make_unique<te::MainWindow>(std::move(options));
        ASSERT_HRESULT_SUCCEEDED(m_window->Create(SW_SHOWNOACTIVATE));
        Pump(15000,
             [this] { return !m_window->NavigationPending() && m_window->Files().Items().size() == 60; });
        Pump(200);
        ASSERT_EQ(m_window->Files().Items().size(), 60u);
    }

    void TearDown() override
    {
        if (m_window)
        {
            DestroyWindow(m_window->Hwnd());
            m_window.reset();
        }
        std::error_code ignored;
        fs::remove_all(m_root, ignored);
        if (m_uninitialize)
        {
            OleUninitialize();
        }
    }

    bool m_uninitialize = false;
    fs::path m_root;
    std::unique_ptr<te::MainWindow> m_window;
};

TEST_F(UiaFileListTest, IsADataGridNamedItemsWithItsPatterns)
{
    std::wstring name;
    std::wstring automationId;
    RECT bounds{};
    int rows = 0;
    int columns = 0;
    BOOL multiple = FALSE;
    bool grid = false;
    bool table = false;
    bool scroll = false;
    bool selection = false;
    WithClient([&](IUIAutomation* uia) {
        const auto element = FindDataGrid(uia, m_window->Hwnd());
        ASSERT_TRUE(element);
        name = NameOf(element.get());
        wil::unique_bstr id;
        element->get_CurrentAutomationId(&id);
        automationId = id.get();
        element->get_CurrentBoundingRectangle(&bounds);

        wil::com_ptr<IUIAutomationGridPattern> gridPattern;
        grid = SUCCEEDED(element->GetCurrentPatternAs(UIA_GridPatternId, IID_PPV_ARGS(&gridPattern))) &&
               gridPattern;
        if (gridPattern)
        {
            gridPattern->get_CurrentRowCount(&rows);
            gridPattern->get_CurrentColumnCount(&columns);
        }
        wil::com_ptr<IUIAutomationSelectionPattern> selectionPattern;
        selection = SUCCEEDED(element->GetCurrentPatternAs(UIA_SelectionPatternId,
                                                           IID_PPV_ARGS(&selectionPattern))) &&
                    selectionPattern;
        if (selectionPattern)
        {
            selectionPattern->get_CurrentCanSelectMultiple(&multiple);
        }
        wil::com_ptr<IUIAutomationTablePattern> tablePattern;
        table = SUCCEEDED(element->GetCurrentPatternAs(UIA_TablePatternId, IID_PPV_ARGS(&tablePattern))) &&
                tablePattern;
        wil::com_ptr<IUIAutomationScrollPattern> scrollPattern;
        scroll = SUCCEEDED(element->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scrollPattern))) &&
                 scrollPattern;
    });
    EXPECT_EQ(name, L"Items");
    EXPECT_EQ(automationId, L"FileList");
    EXPECT_EQ(rows, 60);
    EXPECT_EQ(columns, 4);
    EXPECT_TRUE(grid && table && scroll && selection);
    EXPECT_TRUE(multiple);

    // Bounds: the file list's rectangle on screen.
    const float scale = static_cast<float>(m_window->Dpi()) / 96.0f;
    const D2D1_RECT_F list = m_window->Layout().fileList;
    POINT origin{0, 0};
    ClientToScreen(m_window->Hwnd(), &origin);
    EXPECT_NEAR(bounds.left, origin.x + list.left * scale, 1.5);
    EXPECT_NEAR(bounds.top, origin.y + list.top * scale, 1.5);
    EXPECT_NEAR(bounds.right - bounds.left, (list.right - list.left) * scale, 2.0);
}

TEST_F(UiaFileListTest, TheHeaderHasOneHeaderItemPerColumn)
{
    std::vector<std::wstring> tableHeaders;
    std::vector<std::wstring> headerChildren;
    CONTROLTYPEID firstChildType = 0;
    RowOrColumnMajor major = RowOrColumnMajor_Indeterminate;
    WithClient([&](IUIAutomation* uia) {
        const auto element = FindDataGrid(uia, m_window->Hwnd());
        ASSERT_TRUE(element);
        wil::com_ptr<IUIAutomationTablePattern> table;
        ASSERT_HRESULT_SUCCEEDED(element->GetCurrentPatternAs(UIA_TablePatternId, IID_PPV_ARGS(&table)));
        wil::com_ptr<IUIAutomationElementArray> headers;
        ASSERT_HRESULT_SUCCEEDED(table->GetCurrentColumnHeaders(&headers));
        int count = 0;
        headers->get_Length(&count);
        for (int i = 0; i < count; ++i)
        {
            wil::com_ptr<IUIAutomationElement> header;
            headers->GetElement(i, &header);
            tableHeaders.push_back(NameOf(header.get()));
        }
        table->get_CurrentRowOrColumnMajor(&major);

        wil::com_ptr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        wil::com_ptr<IUIAutomationElement> header;
        walker->GetFirstChildElement(element.get(), &header);
        ASSERT_TRUE(header);
        header->get_CurrentControlType(&firstChildType);
        wil::com_ptr<IUIAutomationElement> item;
        walker->GetFirstChildElement(header.get(), &item);
        while (item)
        {
            CONTROLTYPEID type = 0;
            item->get_CurrentControlType(&type);
            EXPECT_EQ(type, UIA_HeaderItemControlTypeId);
            headerChildren.push_back(NameOf(item.get()));
            wil::com_ptr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(item.get(), &next);
            item = std::move(next);
        }
    });
    const std::vector<std::wstring> expected{L"Name", L"Date modified", L"Type", L"Size"};
    EXPECT_EQ(tableHeaders, expected);
    EXPECT_EQ(headerChildren, expected);
    EXPECT_EQ(firstChildType, UIA_HeaderControlTypeId);
    EXPECT_EQ(major, RowOrColumnMajor_RowMajor);
}

TEST_F(UiaFileListTest, InvokingAHeaderSortsByItsColumn)
{
    std::wstring statusAfterSecond;
    WithClient([&](IUIAutomation* uia) {
        const auto element = FindDataGrid(uia, m_window->Hwnd());
        ASSERT_TRUE(element);
        wil::com_ptr<IUIAutomationTablePattern> table;
        ASSERT_HRESULT_SUCCEEDED(element->GetCurrentPatternAs(UIA_TablePatternId, IID_PPV_ARGS(&table)));
        wil::com_ptr<IUIAutomationElementArray> headers;
        ASSERT_HRESULT_SUCCEEDED(table->GetCurrentColumnHeaders(&headers));
        wil::com_ptr<IUIAutomationElement> size;
        headers->GetElement(3, &size);
        wil::com_ptr<IUIAutomationInvokePattern> invoke;
        ASSERT_HRESULT_SUCCEEDED(size->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke)));
        ASSERT_HRESULT_SUCCEEDED(invoke->Invoke());
        EXPECT_EQ(m_window->Files().Sort().field, te::SortField::Size);
        EXPECT_EQ(m_window->Files().Sort().direction, te::SortDirection::Ascending);
        ASSERT_HRESULT_SUCCEEDED(invoke->Invoke());
        EXPECT_EQ(m_window->Files().Sort().direction, te::SortDirection::Descending);
        wil::unique_bstr status;
        size->get_CurrentItemStatus(&status);
        statusAfterSecond = status ? status.get() : L"";
    });
    EXPECT_EQ(statusAfterSecond, L"Sorted descending");
    EXPECT_EQ(m_window->Files().Items().front().info.name.substr(0, 7), L"file159") << "largest first";
}

TEST_F(UiaFileListTest, ScrollPatternScrollsTheList)
{
    BOOL vertical = FALSE;
    BOOL horizontal = TRUE;
    double viewSize = 0;
    double atEnd = 0;
    HRESULT badHorizontal = S_OK;
    float offsetAtEnd = 0;
    float offsetAfterPageUp = 0;
    WithClient([&](IUIAutomation* uia) {
        const auto element = FindDataGrid(uia, m_window->Hwnd());
        ASSERT_TRUE(element);
        wil::com_ptr<IUIAutomationScrollPattern> scroll;
        ASSERT_HRESULT_SUCCEEDED(element->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scroll)));
        scroll->get_CurrentVerticallyScrollable(&vertical);
        scroll->get_CurrentHorizontallyScrollable(&horizontal);
        scroll->get_CurrentVerticalViewSize(&viewSize);
        ASSERT_HRESULT_SUCCEEDED(scroll->SetScrollPercent(UIA_ScrollPatternNoScroll, 100.0));
        offsetAtEnd = m_window->Files().ScrollOffset();
        scroll->get_CurrentVerticalScrollPercent(&atEnd);
        ASSERT_HRESULT_SUCCEEDED(scroll->Scroll(ScrollAmount_NoAmount, ScrollAmount_LargeDecrement));
        offsetAfterPageUp = m_window->Files().ScrollOffset();
        badHorizontal = scroll->SetScrollPercent(50.0, UIA_ScrollPatternNoScroll);
    });
    const te::FileView& view = m_window->Files();
    EXPECT_TRUE(vertical);
    EXPECT_FALSE(horizontal);
    EXPECT_GT(viewSize, 0.0);
    EXPECT_LT(viewSize, 100.0);
    EXPECT_NEAR(offsetAtEnd, view.ContentHeightDip() - view.ViewportHeightDip(), 0.5f);
    EXPECT_NEAR(atEnd, 100.0, 0.01);
    EXPECT_NEAR(offsetAtEnd - offsetAfterPageUp, view.ViewportHeightDip(), 0.5f);
    EXPECT_TRUE(FAILED(badHorizontal)) << "no horizontal scrolling";
}

TEST_F(UiaFileListTest, HitTestingFindsTheHeaderItemsAndFocusTheList)
{
    const float scale = static_cast<float>(m_window->Dpi()) / 96.0f;
    const D2D1_RECT_F type = m_window->Files().HeaderCellRect(te::FileView::Column::Type);
    POINT typeCenter{static_cast<LONG>((type.left + type.right) / 2 * scale),
                     static_cast<LONG>((type.top + type.bottom) / 2 * scale)};
    ClientToScreen(m_window->Hwnd(), &typeCenter);
    const D2D1_RECT_F row = m_window->Files().RowRect(2);
    POINT rowPoint{static_cast<LONG>((row.left + 200) * scale), static_cast<LONG>((row.top + 10) * scale)};
    ClientToScreen(m_window->Hwnd(), &rowPoint);

    std::wstring atType;
    std::wstring atRow;
    // ElementFromPoint hit-tests the real screen: keep the window above any other while it
    // does, or a window that happens to cover the point answers instead (seen once in a
    // full run, T086).
    SetWindowPos(m_window->Hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    WithClient([&](IUIAutomation* uia) {
        wil::com_ptr<IUIAutomationElement> element;
        uia->ElementFromPoint(typeCenter, &element);
        atType = NameOf(element.get());
        element.reset();
        uia->ElementFromPoint(rowPoint, &element);
        atRow = NameOf(element.get());
    });
    SetWindowPos(m_window->Hwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    EXPECT_EQ(atType, L"Type");
    EXPECT_EQ(atRow, m_window->Files().Items()[2].info.name) << "the Name cell of the row (T077)";

    // The root reports the list as focused while the file list has the keyboard.
    RequestRoot(m_window->Hwnd());
    wil::com_ptr<IRawElementProviderFragment> focused;
    ASSERT_HRESULT_SUCCEEDED(m_window->Automation()->GetFocus(&focused));
    ASSERT_TRUE(focused);
    wil::com_ptr<IRawElementProviderSimple> simple = focused.query<IRawElementProviderSimple>();
    wil::unique_variant focusedName;
    ASSERT_HRESULT_SUCCEEDED(simple->GetPropertyValue(UIA_NamePropertyId, &focusedName));
    // The focused row (T077), or the list when no row has the focus yet.
    const auto focus = m_window->Files().SelectionState().FocusIndex();
    EXPECT_EQ(std::wstring(focusedName.bstrVal),
              focus ? m_window->Files().Items()[*focus].info.name : L"Items");
}

TEST_F(UiaFileListTest, TheListIsUnavailableAfterTheWindowCloses)
{
    RequestRoot(m_window->Hwnd());
    wil::com_ptr<IRawElementProviderFragment> focused;
    ASSERT_HRESULT_SUCCEEDED(m_window->Automation()->GetFocus(&focused));
    wil::com_ptr<IGridProvider> grid = focused.query<IGridProvider>();
    int rows = 0;
    ASSERT_HRESULT_SUCCEEDED(grid->get_RowCount(&rows));
    EXPECT_EQ(rows, 60);

    DestroyWindow(m_window->Hwnd());
    m_window.reset();
    EXPECT_EQ(grid->get_RowCount(&rows), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
}

// ---------------------------------------------------------------------------
// Rows and cells (T077)
// ---------------------------------------------------------------------------

std::wstring ControlName(IUIAutomationElement* element, CONTROLTYPEID* type = nullptr)
{
    if (type)
    {
        element->get_CurrentControlType(type);
    }
    return NameOf(element);
}

TEST_F(UiaFileListTest, RowsFollowTheHeaderAsDataItemsWithTextCells)
{
    std::size_t rows = 0;
    std::wstring firstRow;
    CONTROLTYPEID firstRowType = 0;
    std::vector<std::wstring> cells;
    std::vector<CONTROLTYPEID> cellTypes;
    std::wstring parentOfFirstRow;
    std::wstring previousOfFirstRow;
    WithClient([&](IUIAutomation* uia) {
        const auto grid = FindDataGrid(uia, m_window->Hwnd());
        ASSERT_TRUE(grid);
        wil::com_ptr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        wil::com_ptr<IUIAutomationElement> header;
        walker->GetFirstChildElement(grid.get(), &header);
        wil::com_ptr<IUIAutomationElement> row;
        walker->GetNextSiblingElement(header.get(), &row);
        ASSERT_TRUE(row);
        firstRow = ControlName(row.get(), &firstRowType);
        wil::com_ptr<IUIAutomationElement> parent;
        walker->GetParentElement(row.get(), &parent);
        parentOfFirstRow = NameOf(parent.get());
        wil::com_ptr<IUIAutomationElement> previous;
        walker->GetPreviousSiblingElement(row.get(), &previous);
        CONTROLTYPEID previousType = 0;
        previous->get_CurrentControlType(&previousType);
        previousOfFirstRow = previousType == UIA_HeaderControlTypeId ? L"Header" : NameOf(previous.get());

        wil::com_ptr<IUIAutomationElement> cell;
        walker->GetFirstChildElement(row.get(), &cell);
        while (cell)
        {
            CONTROLTYPEID type = 0;
            cells.push_back(ControlName(cell.get(), &type));
            cellTypes.push_back(type);
            wil::com_ptr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(cell.get(), &next);
            cell = std::move(next);
        }

        wil::com_ptr<IUIAutomationElement> each = row;
        while (each)
        {
            ++rows;
            wil::com_ptr<IUIAutomationElement> next;
            walker->GetNextSiblingElement(each.get(), &next);
            each = std::move(next);
        }
    });
    const te::FileItem& first = m_window->Files().Items()[0];
    EXPECT_EQ(rows, 60u);
    EXPECT_EQ(firstRowType, UIA_DataItemControlTypeId);
    EXPECT_EQ(firstRow, first.info.name);
    EXPECT_EQ(parentOfFirstRow, L"Items");
    EXPECT_EQ(previousOfFirstRow, L"Header");
    EXPECT_EQ(cells,
              (std::vector<std::wstring>{first.info.name, te::FileItemFormat::Modified(first.info.modified),
                                         first.info.typeText, te::FileItemFormat::Size(first.info.size)}));
    EXPECT_EQ(cellTypes, std::vector<CONTROLTYPEID>(4, UIA_TextControlTypeId));
}

TEST_F(UiaFileListTest, SelectionItemSelectsAddsAndRemoves)
{
    std::vector<std::wstring> selectedByPattern;
    BOOL secondSelected = FALSE;
    std::wstring container;
    WithClient([&](IUIAutomation* uia) {
        const auto grid = FindDataGrid(uia, m_window->Hwnd());
        ASSERT_TRUE(grid);
        wil::com_ptr<IUIAutomationGridPattern> gridPattern;
        ASSERT_HRESULT_SUCCEEDED(grid->GetCurrentPatternAs(UIA_GridPatternId, IID_PPV_ARGS(&gridPattern)));
        const auto rowElement = [&](int index) {
            wil::com_ptr<IUIAutomationElement> cell;
            gridPattern->GetItem(index, 0, &cell);
            wil::com_ptr<IUIAutomationTreeWalker> walker;
            uia->get_RawViewWalker(&walker);
            wil::com_ptr<IUIAutomationElement> row;
            walker->GetParentElement(cell.get(), &row);
            return row;
        };
        const auto select = [](IUIAutomationElement* row) {
            wil::com_ptr<IUIAutomationSelectionItemPattern> item;
            row->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&item));
            return item;
        };
        const auto third = rowElement(2);
        const auto fifth = rowElement(4);
        ASSERT_HRESULT_SUCCEEDED(select(third.get())->Select());
        ASSERT_HRESULT_SUCCEEDED(select(fifth.get())->AddToSelection());
        select(fifth.get())->get_CurrentIsSelected(&secondSelected);
        wil::com_ptr<IUIAutomationElement> containerElement;
        select(fifth.get())->get_CurrentSelectionContainer(&containerElement);
        container = NameOf(containerElement.get());

        wil::com_ptr<IUIAutomationSelectionPattern> selection;
        ASSERT_HRESULT_SUCCEEDED(grid->GetCurrentPatternAs(UIA_SelectionPatternId, IID_PPV_ARGS(&selection)));
        wil::com_ptr<IUIAutomationElementArray> selected;
        ASSERT_HRESULT_SUCCEEDED(selection->GetCurrentSelection(&selected));
        int count = 0;
        selected->get_Length(&count);
        for (int i = 0; i < count; ++i)
        {
            wil::com_ptr<IUIAutomationElement> element;
            selected->GetElement(i, &element);
            selectedByPattern.push_back(NameOf(element.get()));
        }
        ASSERT_HRESULT_SUCCEEDED(select(third.get())->RemoveFromSelection());
    });
    const auto& items = m_window->Files().Items();
    EXPECT_TRUE(secondSelected);
    EXPECT_EQ(container, L"Items");
    EXPECT_EQ(selectedByPattern, (std::vector<std::wstring>{items[2].info.name, items[4].info.name}));
    ASSERT_EQ(m_window->Files().Selection().size(), 1u) << "RemoveFromSelection left one";
    EXPECT_EQ(m_window->Files().Selection().front()->info.name, items[4].info.name);
}

TEST_F(UiaFileListTest, GridItemsAndCellsKnowTheirPositionAndColumnHeader)
{
    int row = -1;
    int column = -1;
    std::wstring sizeCell;
    std::wstring sizeHeader;
    std::wstring cellParent;
    int rowOfRow = -1;
    int rowColumnSpan = 0;
    WithClient([&](IUIAutomation* uia) {
        const auto grid = FindDataGrid(uia, m_window->Hwnd());
        ASSERT_TRUE(grid);
        wil::com_ptr<IUIAutomationGridPattern> gridPattern;
        ASSERT_HRESULT_SUCCEEDED(grid->GetCurrentPatternAs(UIA_GridPatternId, IID_PPV_ARGS(&gridPattern)));
        wil::com_ptr<IUIAutomationElement> cell;
        ASSERT_HRESULT_SUCCEEDED(gridPattern->GetItem(5, 3, &cell));
        sizeCell = NameOf(cell.get());
        wil::com_ptr<IUIAutomationGridItemPattern> gridItem;
        ASSERT_HRESULT_SUCCEEDED(cell->GetCurrentPatternAs(UIA_GridItemPatternId, IID_PPV_ARGS(&gridItem)));
        gridItem->get_CurrentRow(&row);
        gridItem->get_CurrentColumn(&column);
        wil::com_ptr<IUIAutomationTableItemPattern> tableItem;
        ASSERT_HRESULT_SUCCEEDED(cell->GetCurrentPatternAs(UIA_TableItemPatternId, IID_PPV_ARGS(&tableItem)));
        wil::com_ptr<IUIAutomationElementArray> headers;
        ASSERT_HRESULT_SUCCEEDED(tableItem->GetCurrentColumnHeaderItems(&headers));
        wil::com_ptr<IUIAutomationElement> header;
        headers->GetElement(0, &header);
        sizeHeader = NameOf(header.get());

        wil::com_ptr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        wil::com_ptr<IUIAutomationElement> parent;
        walker->GetParentElement(cell.get(), &parent);
        cellParent = NameOf(parent.get());
        wil::com_ptr<IUIAutomationGridItemPattern> rowItem;
        ASSERT_HRESULT_SUCCEEDED(parent->GetCurrentPatternAs(UIA_GridItemPatternId, IID_PPV_ARGS(&rowItem)));
        rowItem->get_CurrentRow(&rowOfRow);
        rowItem->get_CurrentColumnSpan(&rowColumnSpan);
    });
    const te::FileItem& item = m_window->Files().Items()[5];
    EXPECT_EQ(row, 5);
    EXPECT_EQ(column, 3);
    EXPECT_EQ(sizeCell, te::FileItemFormat::Size(item.info.size));
    EXPECT_EQ(sizeHeader, L"Size");
    EXPECT_EQ(cellParent, item.info.name);
    EXPECT_EQ(rowOfRow, 5);
    EXPECT_EQ(rowColumnSpan, 4);
}

TEST_F(UiaFileListTest, ScrollItemBringsAnOffscreenRowIntoView)
{
    BOOL offscreenBefore = FALSE;
    BOOL offscreenAfter = TRUE;
    WithClient([&](IUIAutomation* uia) {
        const auto grid = FindDataGrid(uia, m_window->Hwnd());
        ASSERT_TRUE(grid);
        wil::com_ptr<IUIAutomationTreeWalker> walker;
        uia->get_RawViewWalker(&walker);
        wil::com_ptr<IUIAutomationElement> last;
        walker->GetLastChildElement(grid.get(), &last);
        ASSERT_TRUE(last);
        last->get_CurrentIsOffscreen(&offscreenBefore);
        wil::com_ptr<IUIAutomationScrollItemPattern> scrollItem;
        ASSERT_HRESULT_SUCCEEDED(
            last->GetCurrentPatternAs(UIA_ScrollItemPatternId, IID_PPV_ARGS(&scrollItem)));
        ASSERT_HRESULT_SUCCEEDED(scrollItem->ScrollIntoView());
        last->get_CurrentIsOffscreen(&offscreenAfter);
    });
    EXPECT_TRUE(offscreenBefore) << "row 60 starts below the visible rows";
    EXPECT_FALSE(offscreenAfter);
    const te::FileView& view = m_window->Files();
    EXPECT_NEAR(view.ScrollOffset(), view.ContentHeightDip() - view.ViewportHeightDip(), 0.5f);
}

// Invoke is called directly on the UI thread here, so nothing is pumped between the call
// and the checks: the item must not open inside the call, and the open request must be
// waiting in the queue. (Checking from the client thread raced with the UI thread.)
TEST_F(UiaFileListTest, InvokeOpensAFolderRowAfterTheCallReturns)
{
    fs::create_directories(m_root / L"a-sub");
    SendMessageW(m_window->Hwnd(), WM_COMMAND, IDM_REFRESH, 0);
    Pump(15000, [this] { return !m_window->NavigationPending() && m_window->Files().Items().size() == 61; });
    ASSERT_EQ(m_window->Files().Items().front().info.name, L"a-sub") << "folders first";

    RequestRoot(m_window->Hwnd());
    ASSERT_NE(m_window->Automation(), nullptr);
    wil::com_ptr<IRawElementProviderFragment> focused;
    m_window->Files().FocusRow(0);
    ASSERT_HRESULT_SUCCEEDED(m_window->Automation()->GetFocus(&focused));
    wil::com_ptr<IInvokeProvider> invoke = focused.query<IInvokeProvider>();
    ASSERT_TRUE(invoke) << "the focused row";

    const std::wstring before = m_window->CurrentLocation().DisplayName();
    ASSERT_HRESULT_SUCCEEDED(invoke->Invoke());
    EXPECT_FALSE(m_window->NavigationPending()) << "nothing opens inside the UIA call";
    EXPECT_EQ(m_window->CurrentLocation().DisplayName(), before);
    MSG msg{};
    EXPECT_TRUE(
        PeekMessageW(&msg, m_window->Hwnd(), te::WM_TE_UIA_OPEN_ITEM, te::WM_TE_UIA_OPEN_ITEM, PM_NOREMOVE))
        << "the open request was posted";

    Pump(15000, [this] {
        return !m_window->NavigationPending() && m_window->CurrentLocation().DisplayName() == L"a-sub";
    });
    EXPECT_EQ(m_window->CurrentLocation().DisplayName(), L"a-sub");

    // The provider belonged to the previous listing.
    EXPECT_EQ(invoke->Invoke(), static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE));
}

TEST_F(UiaFileListTest, RowsFromAnEarlierListingAreUnavailable)
{
    RequestRoot(m_window->Hwnd());
    wil::com_ptr<IRawElementProviderFragment> list;
    ASSERT_HRESULT_SUCCEEDED(m_window->Automation()->Navigate(NavigateDirection_LastChild, &list));
    // The root's last child is the status bar once T078 exists; find the grid instead.
    wil::com_ptr<IGridProvider> grid;
    for (wil::com_ptr<IRawElementProviderFragment> child = list; child && !grid;)
    {
        grid = child.try_query<IGridProvider>();
        if (!grid)
        {
            wil::com_ptr<IRawElementProviderFragment> previous;
            child->Navigate(NavigateDirection_PreviousSibling, &previous);
            child = previous;
        }
    }
    ASSERT_TRUE(grid);
    wil::com_ptr<IRawElementProviderSimple> cell;
    ASSERT_HRESULT_SUCCEEDED(grid->GetItem(3, 0, &cell));
    wil::com_ptr<IRawElementProviderFragment> cellFragment = cell.query<IRawElementProviderFragment>();
    wil::com_ptr<IRawElementProviderFragment> row;
    ASSERT_HRESULT_SUCCEEDED(cellFragment->Navigate(NavigateDirection_Parent, &row));
    wil::com_ptr<ISelectionItemProvider> selectionItem = row.query<ISelectionItemProvider>();
    BOOL selected = FALSE;
    ASSERT_HRESULT_SUCCEEDED(selectionItem->get_IsSelected(&selected));

    // Listing the folder again (a new generation) retires every row and cell provider.
    SendMessageW(m_window->Hwnd(), WM_COMMAND, IDM_REFRESH, 0);
    Pump(15000, [this] { return !m_window->NavigationPending(); });
    Pump(200);
    constexpr HRESULT kNotAvailable = static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE);
    EXPECT_EQ(selectionItem->get_IsSelected(&selected), kNotAvailable);
    EXPECT_EQ(selectionItem->Select(), kNotAvailable);
    wil::unique_variant name;
    EXPECT_EQ(cell->GetPropertyValue(UIA_NamePropertyId, &name), kNotAvailable);
}

} // namespace
