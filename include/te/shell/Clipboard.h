#pragma once

// Copy, cut and paste of files through the OLE clipboard (T070; research R-08; FR-012).
// The data object is the Shell's own (SHCreateDataObject), so other apps, Explorer
// included, can paste what this app copies and the other way round.

#include <te/shell/IFileOperationService.h>
#include <te/shell/ShellTypes.h>

#include <objidl.h>

#include <wil/com.h>

#include <span>

namespace te
{

class Clipboard
{
  public:
    Clipboard() = default;
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;

    // The Shell data object for `items` (children of `folder`), with
    // CFSTR_PREFERREDDROPEFFECT set as Explorer sets it: DROPEFFECT_MOVE for cut,
    // DROPEFFECT_COPY | DROPEFFECT_LINK for copy. Touches no clipboard.
    static HRESULT CreateDataObject(const ShellLocation& folder, std::span<const ShellItemInfo* const> items,
                                    bool cut, IDataObject** result);

    // The paste request for `data` into `destination`: Move if the preferred effect is
    // move only (a cut), otherwise Copy. Fails with DV_E_FORMATETC, leaving `out`
    // unchanged, if `data` holds no files. Touches no clipboard.
    static HRESULT PasteRequestFrom(IDataObject* data, const ShellLocation& destination, FileOpRequest* out);

    // CreateDataObject, then OleSetClipboard. Requires OleInitialize on this thread.
    HRESULT CopyToClipboard(const ShellLocation& folder, std::span<const ShellItemInfo* const> items,
                            bool cut);

    // OleGetClipboard, then PasteRequestFrom.
    static HRESULT GetPasteRequest(const ShellLocation& destination, FileOpRequest* out);

    // True if the clipboard holds files to paste (enables Paste).
    [[nodiscard]] static bool CanPaste();

    // After a cut from this app was pasted (moved), empties the clipboard if it still holds
    // that cut, as Explorer does, so the moved files cannot be pasted again.
    void ClearIfCurrent();

    // At exit: if the clipboard still holds this app's data object, OleFlushClipboard
    // renders it so the files can still be pasted after the app has closed.
    void FlushOnExit();

  private:
    wil::com_ptr<IDataObject> m_placed; // the last object this app put on the clipboard
};

} // namespace te
