#pragma once

// Subfolders for the navigation pane's folder tree (T090; FR-021, research R-07): a node's
// children are listed on this loader's own STA worker when the node is first expanded -
// folders only (SHCONTF_FOLDERS), without zip folders, and hidden ones only when Explorer
// shows hidden items - with their Shell icons, and posted back as WM_TE_TREE_CHILDREN.
// Its own worker, so expanding a node never waits behind the listing of a large folder.

#include <te/core/Messages.h>
#include <te/shell/ShellTypes.h>

#include <windows.h>

#include <cstdint>
#include <memory>

namespace te
{

class FolderTreeLoader
{
  public:
    FolderTreeLoader();
    ~FolderTreeLoader(); // Shutdown()

    FolderTreeLoader(const FolderTreeLoader&) = delete;
    FolderTreeLoader& operator=(const FolderTreeLoader&) = delete;

    // Thread-safe. The result (FolderChildren, tagged with nodeId) is posted to notifyHwnd,
    // children sorted as Explorer's tree shows them: folders by name (natural order), then
    // drive roots by name. iconPx: the icon size in pixels.
    void Request(HWND notifyHwnd, std::uint64_t nodeId, const ShellLocation& folder, int iconPx);
    // Requests still queued are dropped (a refresh or closing); a running one completes.
    void CancelAll();
    // Stops the worker and joins it; later requests are ignored.
    void Shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace te
