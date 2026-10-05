#pragma once

// The app's own surface and tint layers over the DWM backdrop (T036; FR-003,
// research R-04, R-05). Drawing order everywhere: backdrop -> surface layer -> tint
// layer -> content.

#include <te/app/MainLayout.h>
#include <te/appearance/AppearanceSettings.h>

#include <d2d1_1.h>

namespace te::SurfacePainter
{

// Clears the target to transparent, then paints the caption, toolbar (with the address
// bar), navigation pane, file list and status bar:
// - Solid (including high contrast, whose base is COLOR_WINDOW): opaque base color, no
//   overlay.
// - Acrylic / Mica: the surface layer (base x surfaceAlpha), then the tint layer
//   (tint x tintAlpha).
// layout.captionButtons stays at zero alpha so the DWM buttons show, except in
// Transparent: there the glass color covers them too (they show through it), so the whole
// window is one evenly colored pane.
void PaintSurfaces(ID2D1DeviceContext* dc, const MainLayout& layout, const EffectiveAppearance& effective);

// The surface layer and tint layer together as one straight-alpha color: drawn with
// D2D1_PRIMITIVE_BLEND_COPY on a cleared pixel, it gives exactly the pixel PaintSurfaces
// leaves there in a translucent mode.
D2D1_COLOR_F GlassColor(const EffectiveAppearance& effective);

// Raises the surface layer inside one text area to the legibility floor
// max(surfaceAlpha, textScrimAlpha): it replaces the surface layer in `area` and
// redraws the tint layer there, exactly as if the region had been painted at the floor.
// Call it for each text area before drawing that area's content (it overwrites what is
// already there). Does nothing when textScrimAlpha <= surfaceAlpha.
void PaintTextScrim(ID2D1DeviceContext* dc, D2D1_RECT_F area, const EffectiveAppearance& effective);

// Depth: the panes read as stacked slabs of glass. The toolbar casts a soft shadow onto
// the content below it and the navigation pane onto the file list (kShadowDip, fading
// out), and a hairline of light runs along the top edge of the toolbar, the navigation
// pane, the file list and the status bar. Drawn after the content so no text-area floor
// paints over it; the shadows stay clear of text (the first row and column start more
// than kShadowDip from those edges). Nothing is drawn in high contrast.
inline constexpr float kShadowDip = 8.0f;
void PaintDepth(ID2D1DeviceContext* dc, const MainLayout& layout, const EffectiveAppearance& effective);

// The window's rim as a bevelled edge of glass, lit from the top left: just inside the
// DWM border, a bright 1-DIP edge and a softer kBevelDip band along the top and left,
// fading to shade along the bottom and right, following the DWM's rounded corners
// (kCornerRadiusDip; square when maximized). The caption buttons are left untouched, so
// the DWM-drawn buttons and their states show as before - except in Transparent, where the
// glass covers them and the rim runs across them. Nothing in high contrast.
inline constexpr float kBevelDip = 4.0f;
inline constexpr float kCornerRadiusDip = 8.0f; // DWMWCP_ROUND
void PaintFrameBevel(ID2D1DeviceContext* dc, D2D1_SIZE_F client, const D2D1_RECT_F& captionButtons,
                     bool maximized, const EffectiveAppearance& effective);

// The window as a slab of glass, seen along up to four of its sides (layout.slab): a face
// along the top (lit), the left (half lit), the right (half in shade) and the bottom (in
// shade), each drawn only when on, mitred where two meet and meeting the front face (the
// panes) at a crisp edge. The front face's corners are rounded like the window's own
// (kCornerRadiusDip) where two faces meet, and square on an edge without a face. The faces
// are the glass itself (PaintSurfaces fills layout.slabLeft, layout.slabBottom and
// layout.slabRight); this draws only their shading, after the content. The caption buttons
// are left untouched, except in Transparent, where the glass covers them and the top and
// right faces run across them. Nothing when every face is off or in high contrast.
void PaintSlab(ID2D1DeviceContext* dc, D2D1_SIZE_F client, const MainLayout& layout,
               const EffectiveAppearance& effective);

} // namespace te::SurfacePainter
