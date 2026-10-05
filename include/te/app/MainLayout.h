#pragma once

// The main window's regions (UI contract §1): caption strip, toolbar, navigation
// pane, file list and status bar. All rectangles are in DIPs, the unit the
// Direct2D device context and the UI components' SetBounds() use.

#include <windows.h>

#include <d2d1.h>

namespace te
{

struct MainLayout
{
    static constexpr float kToolbarHeightDip = 40.0f;
    static constexpr float kStatusBarHeightDip = 24.0f;
    static constexpr float kNavigationPaneWidthDip = 220.0f;
    // The pane never takes more than this share of the width, so the file list
    // keeps most of the space in narrow windows.
    static constexpr float kNavigationPaneMaxShare = 0.40f;

    D2D1_RECT_F caption{};        // full width, top to caption height
    D2D1_RECT_F toolbar{};        // full width, below the caption
    D2D1_RECT_F navigationPane{}; // left column between toolbar and status bar
    D2D1_RECT_F fileList{};       // right of the pane, between toolbar and status bar
    D2D1_RECT_F statusBar{};      // full width, at the bottom
    // The window as a slab of glass (AppearanceSettings::slabThicknessPx): its faces run
    // along the top (inside the caption strip, which is that much taller), the left and the
    // bottom edges, and the panes below the caption move in by the thickness. slabLeft and
    // slabBottom are the face strips outside every other region; all zero when slab is 0.
    float slab = 0.0f;              // thickness in DIPs
    D2D1_RECT_F slabLeft{};         // left edge, from the caption down to the bottom
    D2D1_RECT_F slabBottom{};       // bottom edge, right of slabLeft
    // Where the top face starts: the first visible row (below the off-screen rows of a
    // maximized window). Not set by Compute: the owner fills it from
    // CaptionLayout::contentTopPx (use ToDip).
    float slabTop = 0.0f;
    // The DWM caption buttons, kept at zero alpha by SurfacePainter. Not set by
    // Compute: the owner fills it from CaptionLayout::captionButtons (use
    // ToDip).
    D2D1_RECT_F captionButtons{};

    // clientSize and captionHeightPx are in physical pixels at dpi. Regions never
    // have negative sizes; in a window smaller than the minimum they shrink to zero
    // from the bottom (file list and pane first, then toolbar).
    // The toolbar and status bar grow with the Windows text size (`textScale`, 1 to 2.25;
    // T082), as their text does.
    // slabPx (physical pixels) is the slab thickness; captionHeightPx already includes it
    // (CaptionLayout::captionHeightPx).
    static MainLayout Compute(SIZE clientSize, UINT dpi, int captionHeightPx, float textScale = 1.0f,
                              int slabPx = 0);

    // Converts a client rectangle in physical pixels at dpi to DIPs.
    static D2D1_RECT_F ToDip(const RECT& px, UINT dpi);
};

} // namespace te
