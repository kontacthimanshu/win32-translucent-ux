// Window lifecycle leak tests (T029, T085; SC-010, quickstart V-6b).
//
// Creates, shows, renders and destroys the main window repeatedly and checks that
// GDI objects, USER objects and kernel handles return to their baseline. Two
// warm-up cycles run first, because the first window in a process loads things
// once for the process lifetime (graphics driver DLLs and their worker threads,
// the window class); those are not leaks.

#include <te/app/MainWindow.h>
#include <te/core/ComInit.h>
#include <te/core/Messages.h>

#include <gtest/gtest.h>

#include <windows.h>
#include <winternl.h>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

namespace
{

constexpr int kWarmUpCycles = 2;
constexpr int kMeasuredCycles = 50;

struct ResourceCounts
{
    DWORD gdiObjects = 0;
    DWORD userObjects = 0;
    DWORD handles = 0;
    // Of `handles`: mappings of the Windows component-catalog cache (see below).
    DWORD catalogCacheSections = 0;
};

// COM and the Shell map the component-catalog cache,
// C:\ProgramData\Microsoft\Windows\Caches\cversions.<n>.ro, into the process as a named
// section, and map it again whenever the system-wide catalog version changes (another
// program registers or updates COM classes). That happens at random moments during a long
// run and is not the application's handle (found in T086: one extra Section handle, with
// that name, after 1 in about 3 runs). It is counted separately so the check stays exact
// for everything else. Handles are probed with the documented GetHandleInformation and
// NtQueryObject; only sections are asked for their name.
DWORD CountCatalogCacheSections()
{
    using QueryObject = LONG(NTAPI*)(HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    const auto queryObject =
        reinterpret_cast<QueryObject>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
    if (!queryObject)
    {
        return 0;
    }
    constexpr auto kObjectNameInformation = static_cast<OBJECT_INFORMATION_CLASS>(1);
    DWORD count = 0;
    DWORD total = 0;
    GetProcessHandleCount(GetCurrentProcess(), &total);
    DWORD seen = 0;
    // Handle values are multiples of 4; stop once every open handle has been seen.
    for (ULONG_PTR value = 4; value < 0x100000 && seen < total; value += 4)
    {
        const auto handle = reinterpret_cast<HANDLE>(value);
        DWORD flags = 0;
        if (!GetHandleInformation(handle, &flags))
        {
            continue;
        }
        ++seen;
        alignas(8) unsigned char type[512]{};
        if (queryObject(handle, ObjectTypeInformation, type, sizeof(type), nullptr) < 0)
        {
            continue;
        }
        const auto* typeInfo = reinterpret_cast<const PUBLIC_OBJECT_TYPE_INFORMATION*>(type);
        if (std::wstring(typeInfo->TypeName.Buffer, typeInfo->TypeName.Length / sizeof(wchar_t)) !=
            L"Section")
        {
            continue;
        }
        alignas(8) unsigned char name[2048]{};
        if (queryObject(handle, kObjectNameInformation, name, sizeof(name), nullptr) < 0)
        {
            continue;
        }
        const auto* nameInfo = reinterpret_cast<const UNICODE_STRING*>(name);
        if (nameInfo->Buffer &&
            std::wstring(nameInfo->Buffer, nameInfo->Length / sizeof(wchar_t)).find(L"Caches*cversions") !=
                std::wstring::npos)
        {
            ++count;
        }
    }
    return count;
}

ResourceCounts Snapshot()
{
    ResourceCounts counts;
    const HANDLE process = GetCurrentProcess();
    // GR_GDIOBJECTS and GR_USEROBJECTS are separate values, not combinable flags.
    counts.gdiObjects = GetGuiResources(process, GR_GDIOBJECTS);
    counts.userObjects = GetGuiResources(process, GR_USEROBJECTS);
    GetProcessHandleCount(process, &counts.handles);
    counts.catalogCacheSections = CountCatalogCacheSections();
    return counts;
}

// The process's handles without the component-catalog cache's sections.
DWORD OwnHandles(const ResourceCounts& counts)
{
    return counts.handles - counts.catalogCacheSections;
}

void PumpMessages()
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// Pumps until pred() is true or the timeout expires; returns pred().
template <class Pred> bool PumpUntil(Pred pred, DWORD timeoutMs = 5000)
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (!pred() && GetTickCount64() < deadline)
    {
        PumpMessages();
        MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    return pred();
}

// One full lifecycle: create, show (so the render device draws and presents),
// destroy, and let the destruction messages run.
void RunOneCycle()
{
    te::MainWindow::Options options;
    options.instance = GetModuleHandleW(nullptr);
    options.title = L"Translucent Explorer (lifecycle test)";
    options.quitOnDestroy = false;

    te::MainWindow window(std::move(options));
    ASSERT_HRESULT_SUCCEEDED(window.Create(SW_SHOWNOACTIVATE));
    const HWND hwnd = window.Hwnd();
    ASSERT_TRUE(PumpUntil([&] { return IsWindowVisible(hwnd) != FALSE; }));
    UpdateWindow(hwnd); // make sure WM_PAINT (and a Direct2D present) has run
    PumpMessages();

    DestroyWindow(hwnd);
    ASSERT_TRUE(PumpUntil([&] { return IsWindow(hwnd) == FALSE; }));
    PumpMessages();
}

std::filesystem::path TenK()
{
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    return std::filesystem::path(temp) / L"te-test" / L"10k";
}

std::ptrdiff_t LivePayloads()
{
    return te::EnumBatch::Live() + te::EnumDone::Live() + te::IconReady::Live() + te::FileOpItem::Live() +
           te::FileOpDone::Live();
}

// One lifecycle over the 10k folder (T085): create with the Shell started, wait for the
// first items, then destroy the window while the enumeration (and icon extraction) is
// still running. Returns whether the listing was still incomplete when it was destroyed.
bool RunOneCycleWhileEnumerating()
{
    te::MainWindow::Options options;
    options.instance = GetModuleHandleW(nullptr);
    options.title = L"Translucent Explorer (lifecycle test, 10k)";
    options.quitOnDestroy = false;
    options.startShell = true;
    options.initialPath = TenK().wstring();

    te::MainWindow window(std::move(options));
    EXPECT_HRESULT_SUCCEEDED(window.Create(SW_SHOWNOACTIVATE));
    const HWND hwnd = window.Hwnd();
    // One message at a time, so the window is destroyed right after the first batch is on
    // screen (icons requested), not after everything already queued has been handled.
    const ULONGLONG deadline = GetTickCount64() + 15000;
    MSG msg{};
    while (window.Files().Items().empty() && GetTickCount64() < deadline)
    {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else
        {
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    }
    EXPECT_FALSE(window.Files().Items().empty()) << "the first batch arrived";
    const bool incomplete = window.Files().Items().size() < 10000;

    DestroyWindow(hwnd);
    EXPECT_TRUE(PumpUntil([&] { return IsWindow(hwnd) == FALSE; }));
    PumpMessages();
    return incomplete;
}

} // namespace

TEST(WindowLifecycle, RepeatedCreateAndDestroyDoesNotLeak)
{
    const te::OleScope ole; // the UI thread's apartment, as in wWinMain

    for (int i = 0; i < kWarmUpCycles; ++i)
    {
        RunOneCycle();
        if (HasFatalFailure())
        {
            return;
        }
    }
    const ResourceCounts baseline = Snapshot();

    for (int i = 0; i < kMeasuredCycles; ++i)
    {
        RunOneCycle();
        if (HasFatalFailure())
        {
            return;
        }
    }
    const ResourceCounts after = Snapshot();

    std::printf(
        "baseline: GDI=%lu USER=%lu handles=%lu (catalog cache %lu); after %d cycles: GDI=%lu USER=%lu "
        "handles=%lu (catalog cache %lu)\n",
        baseline.gdiObjects, baseline.userObjects, baseline.handles, baseline.catalogCacheSections,
        kMeasuredCycles, after.gdiObjects, after.userObjects, after.handles, after.catalogCacheSections);
    EXPECT_EQ(after.gdiObjects, baseline.gdiObjects);
    EXPECT_EQ(after.userObjects, baseline.userObjects);
    EXPECT_EQ(OwnHandles(after), OwnHandles(baseline));
}

// T085: the same, destroying the window while `10k` is being listed, before EnumDone.
TEST(WindowLifecycle, DestroyingWhileEnumeratingTenThousandItemsDoesNotLeak)
{
    if (!std::filesystem::exists(TenK()))
    {
        GTEST_SKIP() << "Test data missing: run tools\\New-TestData.ps1";
    }
    const te::OleScope ole;

    for (int i = 0; i < kWarmUpCycles; ++i)
    {
        (void)RunOneCycleWhileEnumerating();
        if (HasFailure())
        {
            return;
        }
    }
    const ResourceCounts baseline = Snapshot();
    const std::ptrdiff_t payloads = LivePayloads();

    int interrupted = 0;
    for (int i = 0; i < kMeasuredCycles; ++i)
    {
        interrupted += RunOneCycleWhileEnumerating() ? 1 : 0;
        if (HasFailure())
        {
            return;
        }
    }
    const ResourceCounts after = Snapshot();

    std::printf(
        "10k: baseline: GDI=%lu USER=%lu handles=%lu (catalog cache %lu); after %d cycles (%d destroyed "
        "mid-listing): GDI=%lu USER=%lu handles=%lu (catalog cache %lu); live payloads %td -> %td\n",
        baseline.gdiObjects, baseline.userObjects, baseline.handles, baseline.catalogCacheSections,
        kMeasuredCycles, interrupted, after.gdiObjects, after.userObjects, after.handles,
        after.catalogCacheSections, payloads, LivePayloads());
    EXPECT_EQ(interrupted, kMeasuredCycles) << "every window was destroyed before its listing finished";
    EXPECT_EQ(after.gdiObjects, baseline.gdiObjects);
    EXPECT_EQ(after.userObjects, baseline.userObjects);
    EXPECT_EQ(OwnHandles(after), OwnHandles(baseline));
    EXPECT_EQ(LivePayloads(), payloads) << "every WM_TE_* payload freed";
}

// T093 (SC-010): the Debug CRT heap returns to where it was after windows that browsed -
// listed `nested`, filtered it, expanded a folder in the tree, went up and refreshed - and
// were closed. The app reports leaks at exit (_CRTDBG_LEAK_CHECK_DF in wWinMain); this
// checks the same heap between window lifecycles, so a leak fails a test run. Blocks
// allocated once for the process (caches, the first window's classes) are made in the
// warm-up cycles.
TEST(WindowLifecycle, TheCrtHeapReturnsToBaselineAfterBrowsing)
{
#ifndef _DEBUG
    GTEST_SKIP() << "the CRT heap is tracked in Debug builds only";
#else
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path nested = std::filesystem::path(temp) / L"te-test" / L"nested";
    if (!std::filesystem::exists(nested / L"a"))
    {
        GTEST_SKIP() << "Test data missing: run tools\\New-TestData.ps1";
    }
    const te::OleScope ole;
    const auto browse = [&] {
        te::MainWindow::Options options;
        options.instance = GetModuleHandleW(nullptr);
        options.title = L"Translucent Explorer (heap test)";
        options.quitOnDestroy = false;
        options.startShell = true;
        options.initialPath = nested.wstring();
        te::MainWindow window(std::move(options));
        EXPECT_HRESULT_SUCCEEDED(window.Create(SW_SHOWNOACTIVATE));
        const HWND hwnd = window.Hwnd();
        EXPECT_TRUE(
            PumpUntil([&] { return !window.NavigationPending() && !window.Files().Items().empty(); }, 15000));
        UpdateWindow(hwnd);
        // Filter (T092), then clear it.
        window.Filter().Focus();
        SetWindowTextW(window.Filter().Edit(), L"a");
        window.Filter().Clear();
        // Expand the first place in the tree (T090) and let its subfolders arrive.
        window.Places().Expand(0);
        PumpUntil([&] { return !window.Places().Entries().empty() && !window.Places().Entries()[0].loading; },
                  10000);
        // Up, then Refresh.
        window.GoUp();
        PumpUntil([&] { return !window.NavigationPending(); }, 10000);
        window.RefreshFolder();
        PumpUntil([&] { return !window.NavigationPending(); }, 10000);
        UpdateWindow(hwnd);
        PumpMessages();
        DestroyWindow(hwnd);
        EXPECT_TRUE(PumpUntil([&] { return IsWindow(hwnd) == FALSE; }));
        PumpMessages();
    };

    for (int i = 0; i < kWarmUpCycles; ++i)
    {
        browse();
    }
    _CrtMemState before{};
    _CrtMemCheckpoint(&before);
    constexpr int kCycles = 5;
    for (int i = 0; i < kCycles; ++i)
    {
        browse();
    }
    _CrtMemState after{};
    _CrtMemCheckpoint(&after);
    _CrtMemState difference{};
    const bool changed = _CrtMemDifference(&difference, &before, &after) != FALSE;
    std::printf("CRT heap after %d browsing cycles: %zu normal blocks (%zu bytes) more than before\n",
                kCycles, difference.lCounts[_NORMAL_BLOCK], difference.lSizes[_NORMAL_BLOCK]);
    if (changed && difference.lCounts[_NORMAL_BLOCK] > 0)
    {
        // Which blocks: printed to stdout with their allocation numbers.
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
        _CrtMemDumpAllObjectsSince(&before);
    }
    EXPECT_EQ(difference.lCounts[_NORMAL_BLOCK], 0u) << "normal blocks left by closed windows";
#endif
}
