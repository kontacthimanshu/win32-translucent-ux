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
    // The DWM caption buttons, kept at zero alpha by SurfacePainter. Not set by
    // Compute: the owner fills it from CaptionLayout::captionButtons (use
    // ToDip).
    D2D1_RECT_F captionButtons{};

    // clientSize and captionHeightPx are in physical pixels at dpi. Regions never
    // have negative sizes; in a window smaller than the minimum they shrink to zero
    // from the bottom (file list and pane first, then toolbar).
    // The toolbar and status bar grow with the Windows text size (`textScale`, 1 to 2.25;
    // T082), as their text does.
    static MainLayout Compute(SIZE clientSize, UINT dpi, int captionHeightPx, float textScale = 1.0f);

    // Converts a client rectangle in physical pixels at dpi to DIPs.
    static D2D1_RECT_F ToDip(const RECT& px, UINT dpi);
};

} // namespace te
