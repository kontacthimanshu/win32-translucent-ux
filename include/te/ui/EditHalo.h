#pragma once

// The native single-line edits (address, filter, rename) in Transparent mode. Their
// text gets the same white-with-halo look as the rest of the window: the edit draws its
// text in its colour key, so the text vanishes with the background, and the owner draws
// the same text into the swap chain underneath, with TextHalo, at the positions GDI
// gives each character (a GDI-compatible DirectWrite layout in the edit's own font,
// from the first visible character). The caret and the selection stay the edit's own.

#include <te/appearance/AppearanceSettings.h>

#include <windows.h>

#include <d2d1_1.h>

namespace te::EditHalo
{

// Draws `edit`'s text, white with a halo, where the edit would have drawn it (clipped to
// its client area). `owner` is the window whose client area `dc` covers, in DIPs at
// `dpi`. Does nothing without effective.textHalo, or while `edit` is hidden or empty.
void Render(ID2D1DeviceContext* dc, HWND edit, HWND owner, UINT dpi, const EffectiveAppearance& effective);

// For the edit's subclass procedure, after the edit handled `msg`: when the message can
// change the text, its scroll position or the selection, the owner repaints so the text
// it draws follows. `active`: the owner draws the text (effective.textHalo).
void AfterMessage(HWND edit, UINT msg, WPARAM wParam, bool active);

} // namespace te::EditHalo
