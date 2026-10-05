#include <te/render/SurfacePainter.h>

#include <te/appearance/Contrast.h>

#include <wil/com.h>
#include <wil/result_macros.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace te::SurfacePainter
{

namespace
{

D2D1_COLOR_F ToColor(Rgb rgb, float alpha)
{
    return D2D1::ColorF(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, alpha);
}

bool IsEmpty(const D2D1_RECT_F& r)
{
    return r.right <= r.left || r.bottom <= r.top;
}

void FillRegions(ID2D1DeviceContext* dc, const MainLayout& layout, ID2D1Brush* brush)
{
    for (const D2D1_RECT_F& region : {layout.caption, layout.toolbar, layout.navigationPane, layout.fileList,
                                      layout.statusBar, layout.slabLeft, layout.slabBottom, layout.slabRight})
    {
        if (!IsEmpty(region))
        {
            dc->FillRectangle(region, brush);
        }
    }
}

// A closed polygon as a path geometry; nullptr on failure.
wil::com_ptr<ID2D1PathGeometry> Polygon(ID2D1Factory* factory, std::initializer_list<D2D1_POINT_2F> points)
{
    wil::com_ptr<ID2D1PathGeometry> path;
    wil::com_ptr<ID2D1GeometrySink> sink;
    if (FAILED_LOG(factory->CreatePathGeometry(path.put())) || FAILED_LOG(path->Open(sink.put())))
    {
        return nullptr;
    }
    auto it = points.begin();
    sink->BeginFigure(*it, D2D1_FIGURE_BEGIN_FILLED);
    for (++it; it != points.end(); ++it)
    {
        sink->AddLine(*it);
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED_LOG(sink->Close()))
    {
        return nullptr;
    }
    return path;
}

// A rectangle with each corner rounded by its own radius (0 = square), clockwise from the
// top left; nullptr on failure.
wil::com_ptr<ID2D1PathGeometry> RoundedRectPath(ID2D1Factory* factory, const D2D1_RECT_F& r, float topLeft,
                                                float topRight, float bottomRight, float bottomLeft)
{
    wil::com_ptr<ID2D1PathGeometry> path;
    wil::com_ptr<ID2D1GeometrySink> sink;
    if (FAILED_LOG(factory->CreatePathGeometry(path.put())) || FAILED_LOG(path->Open(sink.put())))
    {
        return nullptr;
    }
    const auto arc = [&](D2D1_POINT_2F to, float radius) {
        sink->AddArc(D2D1::ArcSegment(to, D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                      D2D1_ARC_SIZE_SMALL));
    };
    sink->BeginFigure(D2D1::Point2F(r.left + topLeft, r.top), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(r.right - topRight, r.top));
    if (topRight > 0.0f)
    {
        arc(D2D1::Point2F(r.right, r.top + topRight), topRight);
    }
    sink->AddLine(D2D1::Point2F(r.right, r.bottom - bottomRight));
    if (bottomRight > 0.0f)
    {
        arc(D2D1::Point2F(r.right - bottomRight, r.bottom), bottomRight);
    }
    sink->AddLine(D2D1::Point2F(r.left + bottomLeft, r.bottom));
    if (bottomLeft > 0.0f)
    {
        arc(D2D1::Point2F(r.left, r.bottom - bottomLeft), bottomLeft);
    }
    sink->AddLine(D2D1::Point2F(r.left, r.top + topLeft));
    if (topLeft > 0.0f)
    {
        arc(D2D1::Point2F(r.left + topLeft, r.top), topLeft);
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED_LOG(sink->Close()))
    {
        return nullptr;
    }
    return path;
}

// What the frame decorations (bevel, slab faces) leave alone: the caption buttons while
// they show the DWM's own material. In Transparent the glass covers them like the rest of
// the caption (PaintSurfaces), so the decorations run across them too; anything else
// would leave the buttons as a darker block in the lit top edge.
D2D1_RECT_F DecorationExclusion(const D2D1_RECT_F& captionButtons, const EffectiveAppearance& effective)
{
    return effective.applied == BackdropMode::Transparent ? D2D1_RECT_F{} : captionButtons;
}

} // namespace

void PaintSurfaces(ID2D1DeviceContext* dc, const MainLayout& layout, const EffectiveAppearance& effective)
{
    // Region edges fall on whole DIPs; aliased fills keep neighbouring regions from
    // blending twice along a shared edge.
    const D2D1_ANTIALIAS_MODE previous = dc->GetAntialiasMode();
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    dc->Clear(D2D1::ColorF(0, 0.0f));

    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED_LOG(dc->CreateSolidColorBrush(ToColor(effective.base, 1.0f), &brush)))
    {
        if (effective.applied == BackdropMode::Solid)
        {
            // Opaque base; in high contrast this is COLOR_WINDOW and nothing else is drawn.
            FillRegions(dc, layout, brush.get());
        }
        else
        {
            // Two independent layers (Principle VII): surface opacity never depends on tint.
            if (effective.surfaceAlpha > 0.0f)
            {
                brush->SetColor(ToColor(effective.base, effective.surfaceAlpha));
                FillRegions(dc, layout, brush.get());
            }
            if (effective.tintAlpha > 0.0f)
            {
                brush->SetColor(ToColor(effective.tint, effective.tintAlpha));
                FillRegions(dc, layout, brush.get());
            }
        }
    }

    // The DWM draws the caption buttons in the extended frame behind the swap chain.
    if (!IsEmpty(layout.captionButtons) && effective.applied != BackdropMode::Transparent)
    {
        dc->PushAxisAlignedClip(layout.captionButtons, D2D1_ANTIALIAS_MODE_ALIASED);
        dc->Clear(D2D1::ColorF(0, 0.0f));
        dc->PopAxisAlignedClip();
    }
    dc->SetAntialiasMode(previous);
}

D2D1_COLOR_F GlassColor(const EffectiveAppearance& effective)
{
    // Tint over surface over nothing, premultiplied, then divided back by the alpha.
    const float s = effective.surfaceAlpha;
    const float t = effective.tintAlpha;
    const float alpha = t + s * (1.0f - t);
    if (alpha <= 0.0f)
    {
        return D2D1::ColorF(0, 0.0f);
    }
    const auto channel = [&](std::uint8_t base, std::uint8_t tint) {
        return (tint / 255.0f * t + base / 255.0f * s * (1.0f - t)) / alpha;
    };
    return D2D1::ColorF(channel(effective.base.r, effective.tint.r),
                        channel(effective.base.g, effective.tint.g),
                        channel(effective.base.b, effective.tint.b), alpha);
}

void PaintTextScrim(ID2D1DeviceContext* dc, D2D1_RECT_F area, const EffectiveAppearance& effective)
{
    if (effective.textScrimAlpha <= effective.surfaceAlpha || IsEmpty(area))
    {
        return;
    }

    wil::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED_LOG(dc->CreateSolidColorBrush(ToColor(effective.base, effective.textScrimAlpha), &brush)))
    {
        return;
    }

    const D2D1_ANTIALIAS_MODE previousAntialias = dc->GetAntialiasMode();
    const D2D1_PRIMITIVE_BLEND previousBlend = dc->GetPrimitiveBlend();
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    // Replace (not blend over) the surface layer with the floor opacity, then put the
    // tint layer back on top: backdrop -> surface at the floor -> tint.
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    dc->FillRectangle(area, brush.get());
    dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    if (effective.tintAlpha > 0.0f)
    {
        brush->SetColor(ToColor(effective.tint, effective.tintAlpha));
        dc->FillRectangle(area, brush.get());
    }

    dc->SetPrimitiveBlend(previousBlend);
    dc->SetAntialiasMode(previousAntialias);
}

void PaintDepth(ID2D1DeviceContext* dc, const MainLayout& layout, const EffectiveAppearance& effective)
{
    if (effective.reason == FallbackReason::HighContrast)
    {
        return; // system colors only: no decoration of our own
    }
    // Shadows need more weight over a dark base to show; highlights less.
    const bool dark = Contrast::RelativeLuminance(effective.base) < 0.5;
    const float shadowAlpha = dark ? 0.35f : 0.14f;
    const float highlightAlpha = dark ? 0.12f : 0.60f;

    const auto shadow = [&](D2D1_RECT_F strip, D2D1_POINT_2F from, D2D1_POINT_2F to) {
        if (IsEmpty(strip))
        {
            return;
        }
        const D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(0, shadowAlpha)},
            {1.0f, D2D1::ColorF(0, 0.0f)},
        };
        wil::com_ptr<ID2D1GradientStopCollection> collection;
        wil::com_ptr<ID2D1LinearGradientBrush> brush;
        if (SUCCEEDED_LOG(dc->CreateGradientStopCollection(stops, 2, collection.put())) &&
            SUCCEEDED_LOG(dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(from, to),
                                                        collection.get(), brush.put())))
        {
            dc->FillRectangle(strip, brush.get());
        }
    };

    // The toolbar over the navigation pane and the file list.
    const float below = layout.toolbar.bottom;
    shadow(D2D1::RectF(layout.navigationPane.left, below, layout.fileList.right, below + kShadowDip),
           D2D1::Point2F(0.0f, below), D2D1::Point2F(0.0f, below + kShadowDip));
    // The navigation pane over the file list.
    const float edge = layout.navigationPane.right;
    shadow(D2D1::RectF(edge, layout.fileList.top + kShadowDip, edge + kShadowDip, layout.fileList.bottom),
           D2D1::Point2F(edge, 0.0f), D2D1::Point2F(edge + kShadowDip, 0.0f));

    wil::com_ptr<ID2D1SolidColorBrush> light;
    if (FAILED_LOG(dc->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, highlightAlpha), light.put())))
    {
        return;
    }
    const D2D1_ANTIALIAS_MODE previous = dc->GetAntialiasMode();
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (const D2D1_RECT_F& region :
         {layout.toolbar, layout.navigationPane, layout.fileList, layout.statusBar})
    {
        if (!IsEmpty(region))
        {
            dc->FillRectangle(D2D1::RectF(region.left, region.top, region.right, region.top + 1.0f),
                              light.get());
        }
    }
    dc->SetAntialiasMode(previous);
}

void PaintFrameBevel(ID2D1DeviceContext* dc, D2D1_SIZE_F client, const D2D1_RECT_F& captionButtons,
                     bool maximized, const EffectiveAppearance& effective)
{
    if (effective.reason == FallbackReason::HighContrast || client.width <= 2.0f * kBevelDip ||
        client.height <= 2.0f * kBevelDip)
    {
        return;
    }
    const bool dark = Contrast::RelativeLuminance(effective.base) < 0.5;
    const float light = dark ? 0.30f : 0.85f;
    const float shade = dark ? 0.55f : 0.30f;

    // One diagonal gradient across the whole window: light at the top left, clear in the
    // middle, shade at the bottom right. Stroked around the rim, the top and left edges
    // catch the light and the bottom and right ones fall into shade.
    const auto rimBrush = [&](float scale) {
        const D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, light * scale)},
            {0.5f, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.0f)},
            {0.5f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)},
            {1.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, shade * scale)},
        };
        wil::com_ptr<ID2D1GradientStopCollection> collection;
        wil::com_ptr<ID2D1LinearGradientBrush> brush;
        if (FAILED_LOG(dc->CreateGradientStopCollection(stops, ARRAYSIZE(stops), collection.put())) ||
            FAILED_LOG(dc->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f),
                                                    D2D1::Point2F(client.width, client.height)),
                collection.get(), brush.put())))
        {
            return wil::com_ptr<ID2D1LinearGradientBrush>();
        }
        return brush;
    };

    // Everything except the caption buttons.
    wil::com_ptr<ID2D1Factory> factory;
    dc->GetFactory(factory.put());
    wil::com_ptr<ID2D1RectangleGeometry> all;
    wil::com_ptr<ID2D1RectangleGeometry> buttons;
    wil::com_ptr<ID2D1PathGeometry> mask;
    wil::com_ptr<ID2D1GeometrySink> sink;
    const D2D1_RECT_F whole = D2D1::RectF(0.0f, 0.0f, client.width, client.height);
    if (FAILED_LOG(factory->CreateRectangleGeometry(whole, all.put())) ||
        FAILED_LOG(factory->CreateRectangleGeometry(DecorationExclusion(captionButtons, effective), buttons.put())) ||
        FAILED_LOG(factory->CreatePathGeometry(mask.put())) || FAILED_LOG(mask->Open(sink.put())) ||
        FAILED_LOG(all->CombineWithGeometry(buttons.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.get())) ||
        FAILED_LOG(sink->Close()))
    {
        return;
    }
    dc->PushLayer(D2D1::LayerParameters1(whole, mask.get()), nullptr);

    const float radius = maximized ? 0.0f : kCornerRadiusDip;
    // The soft band: kBevelDip wide, centred kBevelDip / 2 in, at a third of the strength.
    if (const auto band = rimBrush(0.35f))
    {
        const float inset = kBevelDip / 2.0f;
        const D2D1_RECT_F r = D2D1::RectF(inset, inset, client.width - inset, client.height - inset);
        dc->DrawRoundedRectangle(
            D2D1::RoundedRect(r, std::max(0.0f, radius - inset), std::max(0.0f, radius - inset)), band.get(),
            kBevelDip);
    }
    // The crisp edge: 1 DIP, right inside the DWM border.
    if (const auto edge = rimBrush(1.0f))
    {
        const D2D1_RECT_F r = D2D1::RectF(0.5f, 0.5f, client.width - 0.5f, client.height - 0.5f);
        dc->DrawRoundedRectangle(
            D2D1::RoundedRect(r, std::max(0.0f, radius - 0.5f), std::max(0.0f, radius - 0.5f)), edge.get(),
            1.0f);
    }
    dc->PopLayer();
}

void PaintSlab(ID2D1DeviceContext* dc, D2D1_SIZE_F client, const MainLayout& layout,
               const EffectiveAppearance& effective)
{
    const float T = layout.slab.top;
    const float L = layout.slab.left;
    const float B = layout.slab.bottom;
    const float R = layout.slab.right;
    const float o = layout.slabOrigin;
    if ((T <= 0.0f && L <= 0.0f && B <= 0.0f && R <= 0.0f) || effective.reason == FallbackReason::HighContrast ||
        client.width <= L + R || client.height <= o + T + B)
    {
        return;
    }
    // Light from the top left, as for the frame bevel: the top face catches it, the left one
    // half of it, the right one is half in shade and the bottom one in shade. Over a dark
    // base light needs less weight to show and shade more.
    const bool dark = Contrast::RelativeLuminance(effective.base) < 0.5;
    const D2D1_COLOR_F topLight = D2D1::ColorF(1.0f, 1.0f, 1.0f, dark ? 0.16f : 0.50f);
    const D2D1_COLOR_F sideLight = dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f) : D2D1::ColorF(0, 0.05f);
    const D2D1_COLOR_F bottomShade = D2D1::ColorF(0, dark ? 0.40f : 0.20f);
    const D2D1_COLOR_F rightShade = D2D1::ColorF(0, dark ? 0.28f : 0.12f);
    const D2D1_COLOR_F edgeLight = D2D1::ColorF(1.0f, 1.0f, 1.0f, dark ? 0.30f : 0.85f);
    const D2D1_COLOR_F edgeShade = D2D1::ColorF(0, dark ? 0.60f : 0.30f);

    wil::com_ptr<ID2D1Factory> factory;
    dc->GetFactory(factory.put());

    // Back outline (the client edges) and front face (inset by each face that is on). The
    // front face's corners are rounded like the window's own (kCornerRadiusDip) where two
    // faces meet; a corner on an edge without a face stays square, flush with the window.
    const float w = client.width;
    const float h = client.height;
    const D2D1_RECT_F front = D2D1::RectF(L, o + T, w - R, h - B);
    const float radius =
        std::min(kCornerRadiusDip, std::min(front.right - front.left, front.bottom - front.top) / 4.0f);
    const float rTopLeft = T > 0.0f && L > 0.0f ? radius : 0.0f;
    const float rTopRight = T > 0.0f && R > 0.0f ? radius : 0.0f;
    const float rBottomRight = B > 0.0f && R > 0.0f ? radius : 0.0f;
    const float rBottomLeft = B > 0.0f && L > 0.0f ? radius : 0.0f;

    const D2D1_POINT_2F backTopLeft = D2D1::Point2F(0.0f, o);
    const D2D1_POINT_2F backTopRight = D2D1::Point2F(w, o);
    const D2D1_POINT_2F backBottomLeft = D2D1::Point2F(0.0f, h);
    const D2D1_POINT_2F backBottomRight = D2D1::Point2F(w, h);
    // Each mitre continued past the front corner, so the faces' regions also take the
    // pocket a rounded corner leaves, split between the two faces along the mitre.
    const auto beyond = [&](D2D1_POINT_2F frontCorner, D2D1_POINT_2F backCorner) {
        const float dx = frontCorner.x - backCorner.x;
        const float dy = frontCorner.y - backCorner.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f)
        {
            return frontCorner;
        }
        const float reach = 3.0f * std::max(radius, 1.0f) / length;
        return D2D1::Point2F(frontCorner.x + dx * reach, frontCorner.y + dy * reach);
    };
    const D2D1_POINT_2F innerTopLeft = beyond(D2D1::Point2F(front.left, front.top), backTopLeft);
    const D2D1_POINT_2F innerTopRight = beyond(D2D1::Point2F(front.right, front.top), backTopRight);
    const D2D1_POINT_2F innerBottomLeft = beyond(D2D1::Point2F(front.left, front.bottom), backBottomLeft);
    const D2D1_POINT_2F innerBottomRight = beyond(D2D1::Point2F(front.right, front.bottom), backBottomRight);

    // Each face's region: from its back edge to the continued mitres. The face shows where
    // the region is outside the front face; its edge line where the region meets it.
    struct Face
    {
        bool on;
        wil::com_ptr<ID2D1PathGeometry> region;
        D2D1_COLOR_F color;
        D2D1_COLOR_F edge;
        D2D1_POINT_2F back; // the gradient, from the back edge to the front one
        D2D1_POINT_2F front;
    };
    Face faces[] = {
        {T > 0.0f, Polygon(factory.get(), {backTopLeft, backTopRight, innerTopRight, innerTopLeft}), topLight,
         edgeLight, D2D1::Point2F(0.0f, o), D2D1::Point2F(0.0f, o + T)},
        {L > 0.0f, Polygon(factory.get(), {backTopLeft, innerTopLeft, innerBottomLeft, backBottomLeft}), sideLight,
         edgeLight, D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(L, 0.0f)},
        {B > 0.0f, Polygon(factory.get(), {backBottomLeft, innerBottomLeft, innerBottomRight, backBottomRight}),
         bottomShade, edgeShade, D2D1::Point2F(0.0f, h), D2D1::Point2F(0.0f, h - B)},
        {R > 0.0f, Polygon(factory.get(), {backTopRight, backBottomRight, innerBottomRight, innerTopRight}),
         rightShade, edgeShade, D2D1::Point2F(w, 0.0f), D2D1::Point2F(w - R, 0.0f)},
    };

    // Everything except the caption buttons while the DWM draws their background, and
    // within that, everything outside the front face (where the faces show).
    const D2D1_RECT_F whole = D2D1::RectF(0.0f, 0.0f, w, h);
    const auto frontPath = RoundedRectPath(factory.get(), front, rTopLeft, rTopRight, rBottomRight, rBottomLeft);
    wil::com_ptr<ID2D1RectangleGeometry> all;
    wil::com_ptr<ID2D1RectangleGeometry> buttons;
    wil::com_ptr<ID2D1PathGeometry> mask;
    wil::com_ptr<ID2D1PathGeometry> outside;
    wil::com_ptr<ID2D1GeometrySink> sink;
    wil::com_ptr<ID2D1GeometrySink> outsideSink;
    if (!frontPath || FAILED_LOG(factory->CreateRectangleGeometry(whole, all.put())) ||
        FAILED_LOG(
            factory->CreateRectangleGeometry(DecorationExclusion(layout.captionButtons, effective), buttons.put())) ||
        FAILED_LOG(factory->CreatePathGeometry(mask.put())) || FAILED_LOG(mask->Open(sink.put())) ||
        FAILED_LOG(all->CombineWithGeometry(buttons.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.get())) ||
        FAILED_LOG(sink->Close()) || FAILED_LOG(factory->CreatePathGeometry(outside.put())) ||
        FAILED_LOG(outside->Open(outsideSink.put())) ||
        FAILED_LOG(all->CombineWithGeometry(frontPath.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, outsideSink.get())) ||
        FAILED_LOG(outsideSink->Close()))
    {
        return;
    }
    dc->PushLayer(D2D1::LayerParameters1(whole, mask.get()), nullptr);

    // The faces: a gradient across each one's thickness, full strength at the front edge and
    // fading toward the back, so it reads as a surface turning away.
    dc->PushLayer(D2D1::LayerParameters1(whole, outside.get()), nullptr);
    for (const Face& face : faces)
    {
        if (!face.on || !face.region)
        {
            continue;
        }
        D2D1_COLOR_F faded = face.color;
        faded.a *= 0.55f;
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, faded}, {1.0f, face.color}};
        wil::com_ptr<ID2D1GradientStopCollection> collection;
        wil::com_ptr<ID2D1LinearGradientBrush> brush;
        if (SUCCEEDED_LOG(dc->CreateGradientStopCollection(stops, 2, collection.put())) &&
            SUCCEEDED_LOG(dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(face.back, face.front),
                                                        collection.get(), brush.put())))
        {
            dc->FillGeometry(face.region.get(), brush.get());
        }
    }

    // A fainter crease along each mitre where two faces meet, up to the front face's curve.
    wil::com_ptr<ID2D1SolidColorBrush> line;
    if (SUCCEEDED_LOG(dc->CreateSolidColorBrush(edgeLight, line.put())))
    {
        const auto crease = [&](bool on, D2D1_COLOR_F color, D2D1_POINT_2F back, D2D1_POINT_2F inner) {
            if (on)
            {
                color.a *= 0.5f;
                line->SetColor(color);
                dc->DrawLine(back, inner, line.get(), 1.0f);
            }
        };
        crease(T > 0.0f && L > 0.0f, edgeLight, backTopLeft, innerTopLeft);
        crease(B > 0.0f && L > 0.0f, edgeShade, backBottomLeft, innerBottomLeft);
        crease(T > 0.0f && R > 0.0f, edgeShade, backTopRight, innerTopRight);
        crease(B > 0.0f && R > 0.0f, edgeShade, backBottomRight, innerBottomRight);
    }
    dc->PopLayer();

    // The front face's edge, following its rounded corners: a line of light where it meets
    // the top and left faces, a line of shade where it meets the bottom and right ones -
    // each face's color inside that face's region, so the colors change along the mitres.
    const D2D1_RECT_F inset = D2D1::RectF(front.left + 0.5f, front.top + 0.5f, front.right - 0.5f, front.bottom - 0.5f);
    const auto less = [](float r) { return std::max(0.0f, r - 0.5f); };
    const auto edgePath = RoundedRectPath(factory.get(), inset, less(rTopLeft), less(rTopRight),
                                          less(rBottomRight), less(rBottomLeft));
    if (line && edgePath)
    {
        for (const Face& face : faces)
        {
            if (!face.on || !face.region)
            {
                continue;
            }
            dc->PushLayer(D2D1::LayerParameters1(whole, face.region.get()), nullptr);
            line->SetColor(face.edge);
            dc->DrawGeometry(edgePath.get(), line.get(), 1.0f);
            dc->PopLayer();
        }
    }
    dc->PopLayer();
}

} // namespace te::SurfacePainter
