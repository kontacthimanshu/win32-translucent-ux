// FileOperationService (T067; research R-08, R-14; FR-012–FR-014; constitution Principle IX)
// against real IFileOperation on a fresh temp tree per test. Results are posted to a
// message-only window on this thread and collected with PeekMessageW.
//
// Only operations that show no Shell UI are exercised: no name conflicts, no deletes.
// The service is built with the test-only factory of research R-14: it wraps the real
// IFileOperation, records the flags the service sets (always checked against FlagsFor),
// and forwards everything else unchanged. Where the Shell would show its error dialog
// (a destination that denies writing, a source past MAX_PATH), the wrapper also sets
// FOF_NOERRORUI on the wrapped object, so the test reports the failure instead of
// blocking on a modal dialog or an elevation prompt. The service's own flags stay as
// production sets them.

#include <te/app/MainWindow.h>
#include <te/core/Messages.h>
#include <te/shell/FileOperationService.h>

#include <aclapi.h>
#include <shellapi.h>
#include <sherrors.h>
#include <shlobj.h>
#include <wrl/implements.h>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{

namespace fs = std::filesystem;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

constexpr DWORD kTimeoutMs = 30000;

std::wstring Long(const fs::path& path)
{
    return LR"(\\?\)" + path.wstring();
}

// What the wrapper saw, shared with the test (written on the worker thread, read after
// WM_TE_FILEOP_DONE).
struct Observed
{
    std::atomic<DWORD> flags{0};
    std::atomic<int> flagCalls{0};
    std::atomic<bool> ownerSet{false};
};

// Forwards every IFileOperation call to the real object.
class RecordingFileOperation final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IFileOperation>
{
  public:
    RecordingFileOperation(wil::com_ptr<IFileOperation> inner, std::shared_ptr<Observed> observed, bool quiet)
        : m_inner(std::move(inner)), m_observed(std::move(observed)), m_quiet(quiet)
    {
    }

    IFACEMETHODIMP Advise(IFileOperationProgressSink* sink, DWORD* cookie) override
    {
        return m_inner->Advise(sink, cookie);
    }
    IFACEMETHODIMP Unadvise(DWORD cookie) override
    {
        return m_inner->Unadvise(cookie);
    }
    IFACEMETHODIMP SetOperationFlags(DWORD flags) override
    {
        m_observed->flags = flags;
        ++m_observed->flagCalls;
        return m_inner->SetOperationFlags(m_quiet ? (flags | FOF_NOERRORUI) : flags);
    }
    IFACEMETHODIMP SetProgressMessage(LPCWSTR message) override
    {
        return m_inner->SetProgressMessage(message);
    }
    IFACEMETHODIMP SetProgressDialog(IOperationsProgressDialog* dialog) override
    {
        return m_inner->SetProgressDialog(dialog);
    }
    IFACEMETHODIMP SetProperties(IPropertyChangeArray* properties) override
    {
        return m_inner->SetProperties(properties);
    }
    IFACEMETHODIMP SetOwnerWindow(HWND owner) override
    {
        m_observed->ownerSet = true;
        return m_inner->SetOwnerWindow(owner);
    }
    IFACEMETHODIMP ApplyPropertiesToItem(IShellItem* item) override
    {
        return m_inner->ApplyPropertiesToItem(item);
    }
    IFACEMETHODIMP ApplyPropertiesToItems(IUnknown* items) override
    {
        return m_inner->ApplyPropertiesToItems(items);
    }
    IFACEMETHODIMP RenameItem(IShellItem* item, LPCWSTR newName, IFileOperationProgressSink* sink) override
    {
        return m_inner->RenameItem(item, newName, sink);
    }
    IFACEMETHODIMP RenameItems(IUnknown* items, LPCWSTR newName) override
    {
        return m_inner->RenameItems(items, newName);
    }
    IFACEMETHODIMP MoveItem(IShellItem* item, IShellItem* destination, LPCWSTR newName,
                            IFileOperationProgressSink* sink) override
    {
        return m_inner->MoveItem(item, destination, newName, sink);
    }
    IFACEMETHODIMP MoveItems(IUnknown* items, IShellItem* destination) override
    {
        return m_inner->MoveItems(items, destination);
    }
    IFACEMETHODIMP CopyItem(IShellItem* item, IShellItem* destination, LPCWSTR copyName,
                            IFileOperationProgressSink* sink) override
    {
        return m_inner->CopyItem(item, destination, copyName, sink);
    }
    IFACEMETHODIMP CopyItems(IUnknown* items, IShellItem* destination) override
    {
        return m_inner->CopyItems(items, destination);
    }
    IFACEMETHODIMP DeleteItem(IShellItem* item, IFileOperationProgressSink* sink) override
    {
        return m_inner->DeleteItem(item, sink);
    }
    IFACEMETHODIMP DeleteItems(IUnknown* items) override
    {
        return m_inner->DeleteItems(items);
    }
    IFACEMETHODIMP NewItem(IShellItem* destination, DWORD attributes, LPCWSTR name, LPCWSTR templateName,
                           IFileOperationProgressSink* sink) override
    {
        return m_inner->NewItem(destination, attributes, name, templateName, sink);
    }
    IFACEMETHODIMP PerformOperations() override
    {
        return m_inner->PerformOperations();
    }
    IFACEMETHODIMP GetAnyOperationsAborted(BOOL* aborted) override
    {
        return m_inner->GetAnyOperationsAborted(aborted);
    }

  private:
    wil::com_ptr<IFileOperation> m_inner;
    std::shared_ptr<Observed> m_observed;
    bool m_quiet;
};

// Denies creating files and folders in `folder` to the current user, and undoes it.
class DenyWrite
{
  public:
    explicit DenyWrite(fs::path folder) : m_folder(std::move(folder))
    {
        wil::unique_handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return;
        }
        DWORD size = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
        std::vector<BYTE> buffer(size);
        if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size))
        {
            return;
        }
        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());

        EXPLICIT_ACCESSW deny{};
        deny.grfAccessPermissions = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
        deny.grfAccessMode = DENY_ACCESS;
        deny.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        deny.Trustee.TrusteeType = TRUSTEE_IS_USER;
        deny.Trustee.ptstrName = static_cast<LPWSTR>(user->User.Sid);

        PACL existing = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (GetNamedSecurityInfoW(m_folder.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                  nullptr, &existing, nullptr, &descriptor) != ERROR_SUCCESS)
        {
            return;
        }
        const wil::unique_hlocal descriptorOwner(descriptor);
        PACL updated = nullptr;
        if (SetEntriesInAclW(1, &deny, existing, &updated) != ERROR_SUCCESS)
        {
            return;
        }
        const wil::unique_hlocal aclOwner(updated);
        m_applied = SetNamedSecurityInfoW(const_cast<LPWSTR>(m_folder.c_str()), SE_FILE_OBJECT,
                                          DACL_SECURITY_INFORMATION, nullptr, nullptr, updated,
                                          nullptr) == ERROR_SUCCESS;
    }

    ~DenyWrite()
    {
        if (!m_applied)
        {
            return;
        }
        // An empty explicit DACL that inherits again: only the parent's entries remain.
        ACL empty{};
        InitializeAcl(&empty, sizeof(empty), ACL_REVISION);
        SetNamedSecurityInfoW(const_cast<LPWSTR>(m_folder.c_str()), SE_FILE_OBJECT,
                              DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                              nullptr, &empty, nullptr);
    }

    DenyWrite(const DenyWrite&) = delete;
    DenyWrite& operator=(const DenyWrite&) = delete;

    [[nodiscard]] bool Applied() const noexcept
    {
        return m_applied;
    }

  private:
    fs::path m_folder;
    bool m_applied = false;
};

bool IsAccessDeniedClass(HRESULT hr)
{
    return hr == E_ACCESSDENIED || hr == COPYENGINE_E_ACCESS_DENIED_DEST ||
           hr == COPYENGINE_E_ACCESS_DENIED_SRC || hr == COPYENGINE_E_REQUIRES_ELEVATION ||
           hr == COPYENGINE_E_REQUIRES_EDP_CONSENT || hr == HRESULT_FROM_WIN32(ERROR_WRITE_PROTECT);
}

std::string ReadAll(const std::wstring& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

class FileOperationServiceTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        m_uninitialize = SUCCEEDED(hr);
        m_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        ASSERT_NE(m_window, nullptr);

        GUID guid{};
        ASSERT_HRESULT_SUCCEEDED(CoCreateGuid(&guid));
        wchar_t name[40]{};
        swprintf_s(name, L"te-fileop-%08lx", guid.Data1);
        m_root = fs::temp_directory_path() / name;
        fs::create_directories(m_root);
    }

    void TearDown() override
    {
        m_service.reset(); // joins the worker
        if (m_window)
        {
            te::MainWindow::DrainPendingMessages(m_window);
            DestroyWindow(m_window);
        }
        std::error_code ignored;
        fs::remove_all(Long(m_root), ignored);
        if (m_uninitialize)
        {
            CoUninitialize();
        }
    }

    // A service whose IFileOperation is wrapped for observation (and, if quiet, with the
    // Shell's error dialogs suppressed on the wrapped object only).
    void MakeService(bool quiet = false)
    {
        auto observed = m_observed;
        m_service = std::make_unique<te::FileOperationService>([observed, quiet](IFileOperation** operation) {
            wil::com_ptr<IFileOperation> real;
            RETURN_IF_FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&real)));
            auto wrapper = Microsoft::WRL::Make<RecordingFileOperation>(std::move(real), observed, quiet);
            RETURN_IF_NULL_ALLOC(wrapper);
            return wrapper.CopyTo(operation);
        });
    }

    static te::ShellLocation Location(const fs::path& path)
    {
        te::ShellLocation location;
        wil::unique_cotaskmem_ptr<ITEMIDLIST_ABSOLUTE> pidl;
        const HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, wil::out_param(pidl), 0, nullptr);
        EXPECT_HRESULT_SUCCEEDED(hr) << ::testing::Message() << path.wstring();
        if (pidl)
        {
            EXPECT_HRESULT_SUCCEEDED(te::ShellLocation::FromIdList(pidl.get(), &location));
        }
        return location;
    }

    static void WriteFile(const fs::path& path, const std::string& content)
    {
        std::ofstream(Long(path), std::ios::binary) << content;
    }

    struct Outcome
    {
        std::vector<std::unique_ptr<te::FileOpItem>> items;
        std::unique_ptr<te::FileOpDone> done;
        bool itemAfterDone = false;
    };

    // Submits and collects every WM_TE_FILEOP_ITEM up to WM_TE_FILEOP_DONE.
    Outcome Run(te::FileOpRequest request)
    {
        request.id = ++m_nextId;
        const std::uint64_t id = request.id;
        m_service->Submit(m_window, std::move(request));

        Outcome outcome;
        const ULONGLONG until = GetTickCount64() + kTimeoutMs;
        MSG msg{};
        while (GetTickCount64() < until)
        {
            while (PeekMessageW(&msg, m_window, te::WM_TE_FILEOP_ITEM, te::WM_TE_FILEOP_DONE, PM_REMOVE))
            {
                if (msg.message == te::WM_TE_FILEOP_ITEM)
                {
                    auto item = te::TakeOwned<te::FileOpItem>(msg.lParam);
                    outcome.itemAfterDone = outcome.itemAfterDone || outcome.done != nullptr;
                    EXPECT_EQ(item->opId, id);
                    outcome.items.push_back(std::move(item));
                }
                else
                {
                    outcome.done = te::TakeOwned<te::FileOpDone>(msg.lParam);
                    EXPECT_EQ(outcome.done->opId, id);
                }
            }
            if (outcome.done)
            {
                // Anything posted after the final message would be a contract violation.
                Sleep(50);
                if (PeekMessageW(&msg, m_window, te::WM_TE_FILEOP_ITEM, te::WM_TE_FILEOP_ITEM, PM_NOREMOVE))
                {
                    outcome.itemAfterDone = true;
                }
                return outcome;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        }
        ADD_FAILURE() << "no WM_TE_FILEOP_DONE within " << kTimeoutMs << " ms";
        return outcome;
    }

    // The service set exactly the production flags, once, and an owner window.
    void ExpectProductionFlags(te::FileOpKind kind) const
    {
        EXPECT_EQ(m_observed->flagCalls.load(), 1);
        EXPECT_EQ(m_observed->flags.load(), te::FileOperationService::FlagsFor(kind));
        EXPECT_TRUE(m_observed->ownerSet.load()) << "SetOwnerWindow keeps Shell UI modal to the window";
    }

    bool m_uninitialize = false;
    HWND m_window = nullptr;
    fs::path m_root;
    std::shared_ptr<Observed> m_observed = std::make_shared<Observed>();
    std::unique_ptr<te::FileOperationService> m_service;
    std::uint64_t m_nextId = 100;
};

// (d) Fixed flags (research R-08): the Shell keeps its confirmation, conflict and error UI.
TEST(FileOperationFlags, NeverSuppressTheShellsSafetyUi)
{
    using te::FileOpKind;
    for (const FileOpKind kind : {FileOpKind::Copy, FileOpKind::Move, FileOpKind::Rename, FileOpKind::Recycle,
                                  FileOpKind::DeletePermanent})
    {
        const DWORD flags = te::FileOperationService::FlagsFor(kind);
        SCOPED_TRACE(::testing::Message()
                     << "kind " << static_cast<int>(kind) << " flags 0x" << std::hex << flags);
        EXPECT_EQ(flags & FOF_NOCONFIRMATION, 0u);
        EXPECT_EQ(flags & FOF_NOERRORUI, 0u);
        EXPECT_EQ(flags & FOF_RENAMEONCOLLISION, 0u);
        EXPECT_NE(flags & FOFX_ADDUNDORECORD, 0u);
    }
    EXPECT_NE(te::FileOperationService::FlagsFor(FileOpKind::Recycle) & FOF_ALLOWUNDO, 0u)
        << "to the Recycle Bin";
    EXPECT_EQ(te::FileOperationService::FlagsFor(FileOpKind::DeletePermanent) & FOF_ALLOWUNDO, 0u)
        << "Shift+Delete: the Shell asks before deleting permanently";
}

// (b) Invalid names never reach IFileOperation.
TEST(FileOperationNames, ValidateNewNameRejectsReservedCharactersAndBlanks)
{
    EXPECT_FALSE(te::FileOperationService::ValidateNewName(L"bad:name"));
    for (const wchar_t reserved : std::wstring_view(LR"(\/:*?"<>|)"))
    {
        SCOPED_TRACE(::testing::Message() << "character U+" << std::hex << static_cast<int>(reserved));
        EXPECT_FALSE(te::FileOperationService::ValidateNewName(std::wstring(L"a") + reserved + L"b"));
    }
    EXPECT_FALSE(te::FileOperationService::ValidateNewName(L""));
    EXPECT_FALSE(te::FileOperationService::ValidateNewName(L"   "));
    EXPECT_TRUE(te::FileOperationService::ValidateNewName(L"b.txt"));
    EXPECT_TRUE(te::FileOperationService::ValidateNewName(L"  padded name.txt  ")) << "trimmed, then valid";
    EXPECT_TRUE(te::FileOperationService::ValidateNewName(L"résumé \U0001F680.txt"));
}

// (a) Rename a.txt -> b.txt: one successful item with the new name, then Succeeded.
TEST_F(FileOperationServiceTest, RenameReportsTheNewNameThenSucceeds)
{
    MakeService();
    WriteFile(m_root / L"a.txt", "rename me");

    te::FileOpRequest request;
    request.kind = te::FileOpKind::Rename;
    request.sources.push_back(Location(m_root / L"a.txt"));
    request.newName = L"b.txt";
    const Outcome outcome = Run(std::move(request));

    ASSERT_NE(outcome.done, nullptr);
    ASSERT_EQ(outcome.items.size(), 1u);
    EXPECT_EQ(outcome.items[0]->kind, te::FileOpKind::Rename);
    EXPECT_EQ(outcome.items[0]->hr, S_OK);
    EXPECT_EQ(outcome.items[0]->newName.value_or(L"<none>"), L"b.txt");
    EXPECT_FALSE(outcome.itemAfterDone) << "FileOpDone is the last message";
    EXPECT_EQ(outcome.done->state, te::FileOpFinalState::Succeeded);
    EXPECT_TRUE(outcome.done->errors.empty());
    EXPECT_FALSE(outcome.done->aborted);

    EXPECT_FALSE(fs::exists(m_root / L"a.txt"));
    EXPECT_EQ(ReadAll(Long(m_root / L"b.txt")), "rename me");
    ExpectProductionFlags(te::FileOpKind::Rename);
}

// (c) Copy into a folder that denies writing: the failure is reported per item, never as
// success.
TEST_F(FileOperationServiceTest, CopyIntoAFolderThatDeniesWritingReportsAccessDenied)
{
    MakeService(/*quiet=*/true);
    WriteFile(m_root / L"source.txt", "copy me");
    fs::create_directories(m_root / L"locked");
    const DenyWrite deny(m_root / L"locked");
    ASSERT_TRUE(deny.Applied());

    te::FileOpRequest request;
    request.kind = te::FileOpKind::Copy;
    request.sources.push_back(Location(m_root / L"source.txt"));
    request.destination = Location(m_root / L"locked");
    const Outcome outcome = Run(std::move(request));

    ASSERT_NE(outcome.done, nullptr);
    EXPECT_TRUE(outcome.done->state == te::FileOpFinalState::PartiallySucceeded ||
                outcome.done->state == te::FileOpFinalState::Failed)
        << "state " << static_cast<int>(outcome.done->state);
    bool accessDenied = false;
    for (const auto& item : outcome.items)
    {
        accessDenied = accessDenied || IsAccessDeniedClass(item->hr);
    }
    for (const te::Status& error : outcome.done->errors)
    {
        accessDenied = accessDenied || IsAccessDeniedClass(error.hr);
    }
    RecordProperty("final_state", static_cast<int>(outcome.done->state));
    RecordProperty("first_error",
                   outcome.done->errors.empty()
                       ? std::string("none")
                       : ::testing::PrintToString(static_cast<unsigned long>(outcome.done->errors[0].hr)));
    EXPECT_TRUE(accessDenied) << "an E_ACCESSDENIED-class HRESULT for the item";
    EXPECT_FALSE(outcome.done->errors.empty()) << "the errors are listed for the failure dialog (FR-013)";
    EXPECT_FALSE(fs::exists(m_root / L"locked" / L"source.txt"));
    ExpectProductionFlags(te::FileOpKind::Copy);
}

// Move (T068): two files into an empty folder; one item message each, then Succeeded.
// Not in T067's list; Recycle and permanent delete stay manual (V-4), because they touch
// the user's Recycle Bin or show the Shell's confirmation.
TEST_F(FileOperationServiceTest, MoveReportsEachItemThenSucceeds)
{
    MakeService();
    WriteFile(m_root / L"one.txt", "1");
    WriteFile(m_root / L"two.txt", "22");
    fs::create_directories(m_root / L"target");

    te::FileOpRequest request;
    request.kind = te::FileOpKind::Move;
    request.sources.push_back(Location(m_root / L"one.txt"));
    request.sources.push_back(Location(m_root / L"two.txt"));
    request.destination = Location(m_root / L"target");
    const Outcome outcome = Run(std::move(request));

    ASSERT_NE(outcome.done, nullptr);
    EXPECT_EQ(outcome.done->state, te::FileOpFinalState::Succeeded);
    ASSERT_EQ(outcome.items.size(), 2u);
    for (const auto& item : outcome.items)
    {
        EXPECT_EQ(item->kind, te::FileOpKind::Move);
        EXPECT_EQ(item->hr, S_OK);
        EXPECT_FALSE(item->newName.has_value()) << "only a rename reports a new name";
    }
    EXPECT_FALSE(fs::exists(m_root / L"one.txt"));
    EXPECT_EQ(ReadAll(Long(m_root / L"target" / L"two.txt")), "22");
    ExpectProductionFlags(te::FileOpKind::Move);
}

// (e) Unicode names (emoji, CJK, right-to-left) round-trip exactly through rename and copy.
TEST_F(FileOperationServiceTest, UnicodeNamesRoundTripThroughRenameAndCopy)
{
    MakeService();
    const std::wstring original = L"emoji-\U0001F4C1\U0001F680 cjk-文件 hebrew-קובץ.txt";
    const std::wstring renamed = L"arabic-ملف 文件名 \U0001F389 résumé.txt";
    WriteFile(m_root / original, "unicode content");
    fs::create_directories(m_root / L"second");

    te::FileOpRequest rename;
    rename.kind = te::FileOpKind::Rename;
    rename.sources.push_back(Location(m_root / original));
    rename.newName = renamed;
    const Outcome renameOutcome = Run(std::move(rename));
    ASSERT_NE(renameOutcome.done, nullptr);
    EXPECT_EQ(renameOutcome.done->state, te::FileOpFinalState::Succeeded);
    ASSERT_EQ(renameOutcome.items.size(), 1u);
    EXPECT_EQ(renameOutcome.items[0]->newName.value_or(L"<none>"), renamed);
    ASSERT_TRUE(fs::exists(m_root / renamed));
    EXPECT_FALSE(fs::exists(m_root / original));

    te::FileOpRequest copy;
    copy.kind = te::FileOpKind::Copy;
    copy.sources.push_back(Location(m_root / renamed));
    copy.destination = Location(m_root / L"second");
    const Outcome copyOutcome = Run(std::move(copy));
    ASSERT_NE(copyOutcome.done, nullptr);
    EXPECT_EQ(copyOutcome.done->state, te::FileOpFinalState::Succeeded);
    ASSERT_EQ(copyOutcome.items.size(), 1u);
    EXPECT_EQ(copyOutcome.items[0]->hr, S_OK);

    std::vector<std::wstring> names;
    for (const auto& entry : fs::directory_iterator(m_root / L"second"))
    {
        names.push_back(entry.path().filename().wstring());
    }
    EXPECT_EQ(names, std::vector<std::wstring>{renamed}) << "exact name, no substitution";
    EXPECT_EQ(ReadAll(Long(m_root / L"second" / renamed)), "unicode content");
}

// (e) Copying out of a folder deeper than MAX_PATH either succeeds with identical content or
// reports a failure for the item; it never truncates silently.
TEST_F(FileOperationServiceTest, CopyFromBeyondMaxPathIsExactOrReported)
{
    MakeService(/*quiet=*/true);
    fs::path deep = m_root / L"long";
    while (deep.wstring().size() <= 300)
    {
        deep /= L"segment-" + std::wstring(40, L'x');
    }
    ASSERT_TRUE(fs::create_directories(Long(deep)));
    const std::string content(64 * 1024 + 7, 'L'); // not a round size, so truncation shows
    WriteFile(deep / L"deep-file.txt", content);
    ASSERT_GT((deep / L"deep-file.txt").wstring().size(), static_cast<std::size_t>(MAX_PATH));
    fs::create_directories(m_root / L"out");

    te::FileOpRequest request;
    request.kind = te::FileOpKind::Copy;
    request.sources.push_back(Location(deep / L"deep-file.txt"));
    request.destination = Location(m_root / L"out");
    const Outcome outcome = Run(std::move(request));
    ASSERT_NE(outcome.done, nullptr);

    const fs::path copied = m_root / L"out" / L"deep-file.txt";
    RecordProperty("final_state", static_cast<int>(outcome.done->state));
    if (outcome.done->state == te::FileOpFinalState::Succeeded)
    {
        ASSERT_EQ(outcome.items.size(), 1u);
        EXPECT_EQ(outcome.items[0]->hr, S_OK);
        ASSERT_TRUE(fs::exists(copied));
        EXPECT_EQ(ReadAll(Long(copied)), content) << "identical content";
    }
    else
    {
        bool reported = !outcome.done->errors.empty();
        for (const auto& item : outcome.items)
        {
            reported = reported || FAILED(item->hr);
        }
        EXPECT_TRUE(reported) << "a failure must name its HRESULT";
        if (fs::exists(copied))
        {
            EXPECT_EQ(ReadAll(Long(copied)), content) << "no partial file left behind as if complete";
        }
    }
    ExpectProductionFlags(te::FileOpKind::Copy);
}

// V-4h (T073): renaming a file in a folder deeper than MAX_PATH succeeds with the exact
// new name, or reports a failure for the item; never a silent success without the rename.
TEST_F(FileOperationServiceTest, RenameBeyondMaxPathIsExactOrReported)
{
    MakeService(/*quiet=*/true);
    fs::path deep = m_root / L"long";
    while (deep.wstring().size() <= 300)
    {
        deep /= L"segment-" + std::wstring(40, L'x');
    }
    ASSERT_TRUE(fs::create_directories(Long(deep)));
    WriteFile(deep / L"deep-file.txt", "deep");

    te::FileOpRequest request;
    request.kind = te::FileOpKind::Rename;
    request.sources.push_back(Location(deep / L"deep-file.txt"));
    request.newName = L"renamed deep 文件.txt";
    const Outcome outcome = Run(std::move(request));
    ASSERT_NE(outcome.done, nullptr);
    RecordProperty("final_state", static_cast<int>(outcome.done->state));
    if (outcome.done->state == te::FileOpFinalState::Succeeded)
    {
        ASSERT_EQ(outcome.items.size(), 1u);
        EXPECT_EQ(outcome.items[0]->newName.value_or(L""), L"renamed deep 文件.txt");
        EXPECT_TRUE(fs::exists(Long(deep / L"renamed deep 文件.txt")));
        EXPECT_FALSE(fs::exists(Long(deep / L"deep-file.txt")));
    }
    else
    {
        EXPECT_FALSE(outcome.done->errors.empty()) << "a failure must name its HRESULT";
        EXPECT_TRUE(fs::exists(Long(deep / L"deep-file.txt"))) << "nothing lost";
    }
    ExpectProductionFlags(te::FileOpKind::Rename);
}

} // namespace
