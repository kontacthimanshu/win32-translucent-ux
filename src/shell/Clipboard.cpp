#include <te/shell/Clipboard.h>

#include <ole2.h>
#include <shlobj.h>

#include <wil/resource.h>
#include <wil/result.h>

#include <optional>
#include <vector>

namespace te
{

namespace
{

CLIPFORMAT Format(const wchar_t* name)
{
    return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name));
}

FORMATETC HGlobalFormat(CLIPFORMAT format)
{
    return FORMATETC{format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
}

HRESULT SetDword(IDataObject* data, CLIPFORMAT format, DWORD value)
{
    wil::unique_hglobal memory(GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD)));
    RETURN_IF_NULL_ALLOC(memory);
    {
        const wil::unique_hglobal_locked locked(memory.get());
        RETURN_IF_NULL_ALLOC(locked.get());
        *static_cast<DWORD*>(locked.get()) = value;
    }
    FORMATETC formatEtc = HGlobalFormat(format);
    STGMEDIUM medium{};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = memory.get();
    RETURN_IF_FAILED(data->SetData(&formatEtc, &medium, TRUE)); // the object now owns it
    static_cast<void>(memory.release());
    return S_OK;
}

std::optional<DWORD> GetDword(IDataObject* data, CLIPFORMAT format)
{
    FORMATETC formatEtc = HGlobalFormat(format);
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&formatEtc, &medium)))
    {
        return std::nullopt;
    }
    std::optional<DWORD> value;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal && GlobalSize(medium.hGlobal) >= sizeof(DWORD))
    {
        const wil::unique_hglobal_locked locked(medium.hGlobal);
        if (locked.get())
        {
            value = *static_cast<const DWORD*>(locked.get());
        }
    }
    ReleaseStgMedium(&medium);
    return value;
}

// Files, as the Shell ID list format or as a file-name list.
bool HoldsFiles(IDataObject* data)
{
    FORMATETC idList = HGlobalFormat(Format(CFSTR_SHELLIDLIST));
    FORMATETC drop = HGlobalFormat(CF_HDROP);
    return data->QueryGetData(&idList) == S_OK || data->QueryGetData(&drop) == S_OK;
}

} // namespace

HRESULT Clipboard::CreateDataObject(const ShellLocation& folder, std::span<const ShellItemInfo* const> items,
                                    bool cut, IDataObject** result)
{
    RETURN_HR_IF_NULL(E_POINTER, result);
    *result = nullptr;
    RETURN_HR_IF(E_INVALIDARG, !folder.IsValid() || items.empty());
    std::vector<PCUITEMID_CHILD> children;
    for (const ShellItemInfo* item : items)
    {
        RETURN_HR_IF(E_INVALIDARG, !item || !item->childPidl);
        children.push_back(item->childPidl.get());
    }
    wil::com_ptr<IDataObject> data;
    RETURN_IF_FAILED(SHCreateDataObject(folder.IdList(), static_cast<UINT>(children.size()), children.data(),
                                        nullptr, IID_PPV_ARGS(&data)));
    RETURN_IF_FAILED(SetDword(data.get(), Format(CFSTR_PREFERREDDROPEFFECT),
                              cut ? DROPEFFECT_MOVE : (DROPEFFECT_COPY | DROPEFFECT_LINK)));
    *result = data.detach();
    return S_OK;
}

HRESULT Clipboard::PasteRequestFrom(IDataObject* data, const ShellLocation& destination, FileOpRequest* out)
{
    RETURN_HR_IF_NULL(E_POINTER, out);
    RETURN_HR_IF(E_INVALIDARG, !data || !destination.IsValid());
    if (!HoldsFiles(data))
    {
        return DV_E_FORMATETC; // text or an image, not files
    }
    // A cut offers move only; a copy offers copy (and link). Without the format, copy:
    // never move files unless the source asked for it.
    const DWORD effect = GetDword(data, Format(CFSTR_PREFERREDDROPEFFECT)).value_or(DROPEFFECT_COPY);
    const bool move = (effect & DROPEFFECT_MOVE) != 0 && (effect & DROPEFFECT_COPY) == 0;

    FileOpRequest request;
    request.kind = move ? FileOpKind::Move : FileOpKind::Copy;
    request.dataObject = data;
    request.destination = destination;
    *out = std::move(request);
    return S_OK;
}

HRESULT Clipboard::CopyToClipboard(const ShellLocation& folder, std::span<const ShellItemInfo* const> items,
                                   bool cut)
{
    wil::com_ptr<IDataObject> data;
    RETURN_IF_FAILED(CreateDataObject(folder, items, cut, &data));
    RETURN_IF_FAILED(OleSetClipboard(data.get()));
    m_placed = std::move(data);
    return S_OK;
}

HRESULT Clipboard::GetPasteRequest(const ShellLocation& destination, FileOpRequest* out)
{
    wil::com_ptr<IDataObject> data;
    RETURN_IF_FAILED(OleGetClipboard(&data));
    return PasteRequestFrom(data.get(), destination, out);
}

bool Clipboard::CanPaste()
{
    return IsClipboardFormatAvailable(CF_HDROP) || IsClipboardFormatAvailable(Format(CFSTR_SHELLIDLIST));
}

void Clipboard::ClearIfCurrent()
{
    if (m_placed && OleIsCurrentClipboard(m_placed.get()) == S_OK)
    {
        LOG_IF_FAILED(OleSetClipboard(nullptr));
    }
    m_placed.reset();
}

void Clipboard::FlushOnExit()
{
    if (m_placed && OleIsCurrentClipboard(m_placed.get()) == S_OK)
    {
        LOG_IF_FAILED(OleFlushClipboard());
    }
    m_placed.reset();
}

} // namespace te
