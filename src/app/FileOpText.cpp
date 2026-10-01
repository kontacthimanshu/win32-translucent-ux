#include <te/app/FileOpText.h>

#include <wil/resource.h>

#include <initializer_list>

namespace te
{

namespace
{

void LoadInto(HINSTANCE instance, UINT id, std::wstring& target)
{
    if (id == 0)
    {
        return;
    }
    const wchar_t* text = nullptr;
    // With a zero buffer size LoadStringW returns a read-only pointer into the
    // resource; it is not null-terminated, so use the returned length.
    const int length = LoadStringW(instance, id, reinterpret_cast<LPWSTR>(&text), 0);
    if (length > 0)
    {
        target.assign(text, static_cast<std::size_t>(length));
    }
}

// FormatMessageW over a template with inserts passed as an argument array.
std::wstring Format(const std::wstring& pattern, std::initializer_list<DWORD_PTR> args)
{
    LPWSTR raw = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY |
                                            FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                        pattern.c_str(), 0, 0, reinterpret_cast<LPWSTR>(&raw), 0,
                                        reinterpret_cast<va_list*>(const_cast<DWORD_PTR*>(args.begin())));
    const wil::unique_hlocal buffer(raw);
    return length > 0 ? std::wstring(raw, length) : pattern;
}

DWORD_PTR Arg(std::size_t value)
{
    return static_cast<DWORD_PTR>(value);
}

} // namespace

FileOpText FileOpText::Load(HINSTANCE instance, const StringIds& ids)
{
    FileOpText text;
    LoadInto(instance, ids.copyingFmt, text.copyingFmt);
    LoadInto(instance, ids.movingFmt, text.movingFmt);
    LoadInto(instance, ids.deletingFmt, text.deletingFmt);
    LoadInto(instance, ids.renaming, text.renaming);
    LoadInto(instance, ids.copiedFmt, text.copiedFmt);
    LoadInto(instance, ids.movedFmt, text.movedFmt);
    LoadInto(instance, ids.deletedFmt, text.deletedFmt);
    LoadInto(instance, ids.renamed, text.renamed);
    LoadInto(instance, ids.partialFmt, text.partialFmt);
    LoadInto(instance, ids.cancelledFmt, text.cancelledFmt);
    LoadInto(instance, ids.failedTitle, text.failedTitle);
    LoadInto(instance, ids.badName, text.badName);
    return text;
}

std::wstring FileOpText::Progress(FileOpKind kind, std::size_t total) const
{
    switch (kind)
    {
    case FileOpKind::Copy:
        return Format(copyingFmt, {Arg(total)});
    case FileOpKind::Move:
        return Format(movingFmt, {Arg(total)});
    case FileOpKind::Recycle:
    case FileOpKind::DeletePermanent:
        return Format(deletingFmt, {Arg(total)});
    case FileOpKind::Rename:
        return renaming;
    }
    return {};
}

std::wstring FileOpText::Result(FileOpKind kind, FileOpFinalState state, std::size_t completed,
                                std::size_t total) const
{
    if (state == FileOpFinalState::Cancelled)
    {
        return Format(cancelledFmt, {Arg(completed)});
    }
    // Anything short of every item (failures, or items skipped in a conflict) says so.
    if (state != FileOpFinalState::Succeeded || completed < total)
    {
        return Format(partialFmt, {Arg(completed), Arg(total)});
    }
    switch (kind)
    {
    case FileOpKind::Copy:
        return Format(copiedFmt, {Arg(completed)});
    case FileOpKind::Move:
        return Format(movedFmt, {Arg(completed)});
    case FileOpKind::Recycle:
    case FileOpKind::DeletePermanent:
        return Format(deletedFmt, {Arg(completed)});
    case FileOpKind::Rename:
        return renamed;
    }
    return {};
}

std::wstring FileOpText::FailureList(const std::vector<Status>& errors, std::size_t maxLines)
{
    std::wstring text;
    std::size_t lines = 0;
    for (const Status& error : errors)
    {
        if (lines == maxLines)
        {
            text += L"\n…";
            break;
        }
        if (!text.empty())
        {
            text += L'\n';
        }
        text += error.message;
        ++lines;
    }
    return text;
}

} // namespace te
