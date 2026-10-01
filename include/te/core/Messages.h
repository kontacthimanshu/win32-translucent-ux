#pragma once

// Cross-thread message contract (contracts/component-interfaces.md, "Cross-thread
// message contract"; research R-07).
//
// Workers talk to the UI thread only by posting WM_TE_* messages whose LPARAM owns
// a heap payload. PostOwned() hands ownership to the message queue only when the
// post succeeds; the UI thread takes it back with TakeOwned(). On WM_DESTROY the
// remaining WM_TE_* messages are drained and their payloads freed.

#include <te/core/Types.h>
#include <te/shell/IFileOperationService.h>
#include <te/shell/ShellTypes.h>

#include <windows.h>

#include <wil/resource.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace te
{

// ---------------------------------------------------------------------------
// Message identifiers
// ---------------------------------------------------------------------------
inline constexpr UINT WM_TE_ENUM_BATCH = WM_APP + 1;       // LPARAM: EnumBatch*
inline constexpr UINT WM_TE_ENUM_DONE = WM_APP + 2;        // LPARAM: EnumDone*
inline constexpr UINT WM_TE_ICON_READY = WM_APP + 3;       // LPARAM: IconReady*
inline constexpr UINT WM_TE_FILEOP_ITEM = WM_APP + 4;      // LPARAM: FileOpItem*
inline constexpr UINT WM_TE_FILEOP_DONE = WM_APP + 5;      // LPARAM: FileOpDone*
inline constexpr UINT WM_TE_SETTINGS_CHANGED = WM_APP + 6; // no payload
inline constexpr UINT WM_TE_BACKDROP_FAILED = WM_APP + 7;  // WPARAM: HRESULT, no payload

// UI Automation asked to open an item (T077): WPARAM = the item's key, LPARAM = the
// listing generation. No payload; posted so the UIA call returns before anything opens.
inline constexpr UINT WM_TE_UIA_OPEN_ITEM = WM_APP + 8;
// UI Automation asked to navigate (T078): the address's SetValue or a navigation-pane
// item. The target is kept by the window; posted so the UIA call returns first.
inline constexpr UINT WM_TE_UIA_NAVIGATE = WM_APP + 9;
// UI Automation asked to expand (LPARAM 1) or collapse (0) a folder-tree node, WPARAM =
// the node id (T090). No payload; posted so the UIA call returns first.
inline constexpr UINT WM_TE_UIA_EXPAND = WM_APP + 11;
// A folder-tree node's subfolders (T090). LPARAM: FolderChildren*.
inline constexpr UINT WM_TE_TREE_CHILDREN = WM_APP + 10;

inline constexpr UINT WM_TE_FIRST = WM_TE_ENUM_BATCH;
inline constexpr UINT WM_TE_LAST = WM_TE_TREE_CHILDREN; // drained on WM_DESTROY

// ---------------------------------------------------------------------------
// Payloads. The generation check (discard if gen != current) is done by the UI.
// ---------------------------------------------------------------------------

// Counts the live instances of a payload type (T084), so tests can prove that every
// payload posted across threads is freed exactly once: after a shutdown and a drain,
// Live() is back where it started. One atomic increment per payload.
template <class T> class LiveCounted
{
  public:
    LiveCounted() noexcept
    {
        s_live.fetch_add(1, std::memory_order_relaxed);
    }
    LiveCounted(const LiveCounted&) noexcept
    {
        s_live.fetch_add(1, std::memory_order_relaxed);
    }
    LiveCounted(LiveCounted&&) noexcept
    {
        s_live.fetch_add(1, std::memory_order_relaxed);
    }
    LiveCounted& operator=(const LiveCounted&) noexcept = default;
    LiveCounted& operator=(LiveCounted&&) noexcept = default;
    ~LiveCounted()
    {
        s_live.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]] static std::ptrdiff_t Live() noexcept
    {
        return s_live.load(std::memory_order_relaxed);
    }

  private:
    inline static std::atomic<std::ptrdiff_t> s_live{0};
};

struct EnumBatch : LiveCounted<EnumBatch>
{
    Generation gen = 0;
    std::vector<ShellItemInfo> items;
};

struct EnumDone : LiveCounted<EnumDone>
{
    Generation gen = 0;
    HRESULT hr = S_OK;
    bool cancelled = false;
};

struct IconReady : LiveCounted<IconReady>
{
    Generation gen = 0;
    std::size_t itemKey = 0;
    wil::unique_hbitmap bmp;
    // T087: the item's system image-list index (-1 if unknown) and the requested size, the
    // key under which the UI keeps one bitmap per icon. `shared`: not extracted, because
    // this index was already sent at this size; the UI has it in its IconCache.
    int imageIndex = -1;
    int sizePx = 0;
    bool shared = false;
};

struct FileOpItem : LiveCounted<FileOpItem>
{
    std::uint64_t opId = 0;
    FileOpKind kind = FileOpKind::Copy;
    ShellLocation item;
    HRESULT hr = S_OK;
    std::optional<std::wstring> newName; // set for a successful rename
};

struct FileOpDone : LiveCounted<FileOpDone>
{
    std::uint64_t opId = 0;
    FileOpFinalState state = FileOpFinalState::Succeeded;
    std::vector<Status> errors;
    bool aborted = false;
};

struct FolderChildren : LiveCounted<FolderChildren>
{
    struct Child
    {
        ShellLocation location;
        bool hasSubfolders = false; // SFGAO_HASSUBFOLDER: the node shows an expander
        wil::unique_hbitmap icon;   // 32 bpp, premultiplied; converted on the UI thread
    };
    std::uint64_t nodeId = 0; // the navigation pane's node the children belong to
    HRESULT hr = S_OK;
    std::vector<Child> children;
};

// ---------------------------------------------------------------------------
// Ownership transfer
// ---------------------------------------------------------------------------

// Posts msg with payload as LPARAM. On success the message queue owns the payload
// and payload is released (null). On failure (e.g. the window was destroyed)
// payload keeps ownership, so the caller frees it. Returns whether the post
// succeeded.
template <class T> bool PostOwned(HWND hwnd, UINT msg, std::unique_ptr<T>& payload)
{
    if (!PostMessageW(hwnd, msg, 0, reinterpret_cast<LPARAM>(payload.get())))
    {
        return false;
    }
    static_cast<void>(payload.release());
    return true;
}

// Takes ownership of a payload received as LPARAM of a WM_TE_* message.
template <class T> [[nodiscard]] std::unique_ptr<T> TakeOwned(LPARAM lParam) noexcept
{
    return std::unique_ptr<T>(reinterpret_cast<T*>(lParam));
}

} // namespace te
