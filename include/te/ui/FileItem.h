#pragma once

// A file-list row on the UI thread (data-model: FileItem): the Shell data plus
// icon, selection and generation state.

#include <te/core/Types.h>
#include <te/shell/ShellTypes.h>

#include <d2d1_1.h>

#include <wil/com.h>

#include <cstddef>

namespace te
{

struct IconSlot
{
    enum class State
    {
        NotRequested,
        Pending,
        Ready,
        Failed,
    };

    State state = State::NotRequested;
    wil::com_ptr<ID2D1Bitmap1> bitmap;
    // Ask the worker for the bitmap even if it already sent this icon (T087): the UI's
    // shared copy was lost.
    bool forceExtract = false;
};

struct FileItem
{
    ShellItemInfo info;
    std::size_t key = 0; // stable within one generation; used by SetIcon/ApplyRename
    IconSlot icon;
    bool selected = false;
    Generation generation = 0; // generation of the DirectoryRequest that produced it
};

} // namespace te
