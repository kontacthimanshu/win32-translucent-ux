#include <te/render/SurfacePainter.h>

#include <te/appearance/Contrast.h>

#include <wil/com.h>
#include <wil/result_macros.h>

#include <algorithm>
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
                                      layout.statusBar, layout.slabLeft, layout.slabBottom})
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
        FAILED_LOG(factory->CreateRectangleGeometry(captionButtons, buttons.put())) ||
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
    const float o = layout.slabOrigin;
    if ((T <= 0.0f && L <= 0.0f && B <= 0.0f) || effective.reason == FallbackReason::HighContrast ||
        client.width <= L || client.height <= o + T + B)
    {
        return;
    }
    // Light from the top left, as for the frame bevel: the top face catches it, the left one
    // half of it, the bottom one is in shade. Over a dark base light needs less weight to
    // show and shade more.
    const bool dark = Contrast::RelativeLuminance(effective.base) < 0.5;
    const D2D1_COLOR_F topLight = D2D1::ColorF(1.0f, 1.0f, 1.0f, dark ? 0.16f : 0.50f);
    const D2D1_COLOR_F sideLight = dark ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f) : D2D1::ColorF(0, 0.05f);
    const D2D1_COLOR_F bottomShade = D2D1::ColorF(0, dark ? 0.40f : 0.20f);
    const D2D1_COLOR_F edgeLight = D2D1::ColorF(1.0f, 1.0f, 1.0f, dark ? 0.30f : 0.85f);
    const D2D1_COLOR_F edgeShade = D2D1::ColorF(0, dark ? 0.60f : 0.30f);

    wil::com_ptr<ID2D1Factory> factory;
    dc->GetFactory(factory.put());

    // Back outline (the client edges) and front face (inset by each face that is on); the
    // faces join them, mitred where two meet. The right side has no face: the front face
    // runs to that edge.
    const float w = client.width;
    const float h = client.height;
    const D2D1_POINT_2F backTopLeft = D2D1::Point2F(0.0f, o);
    const D2D1_POINT_2F backTopRight = D2D1::Point2F(w, o);
    const D2D1_POINT_2F backBottomLeft = D2D1::Point2F(0.0f, h);
    const D2D1_POINT_2F backBottomRight = D2D1::Point2F(w, h);
    const D2D1_POINT_2F frontTopLeft = D2D1::Point2F(L, o + T);
    const D2D1_POINT_2F frontTopRight = D2D1::Point2F(w, o + T);
    const D2D1_POINT_2F frontBottomLeft = D2D1::Point2F(L, h - B);
    const D2D1_POINT_2F frontBottomRight = D2D1::Point2F(w, h - B);

    // Each face: a gradient across its thickness, full strength at the front edge and
    // fading toward the back, so it reads as a surface turning away.
    const auto face = [&](std::initializer_list<D2D1_POINT_2F> points, D2D1_COLOR_F color, D2D1_POINT_2F back,
                          D2D1_POINT_2F front) {
        const auto geometry = Polygon(factory.get(), points);
        D2D1_COLOR_F faded = color;
        faded.a *= 0.55f;
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, faded}, {1.0f, color}};
        wil::com_ptr<ID2D1GradientStopCollection> collection;
        wil::com_ptr<ID2D1LinearGradientBrush> brush;
        if (geometry && SUCCEEDED_LOG(dc->CreateGradientStopCollection(stops, 2, collection.put())) &&
            SUCCEEDED_LOG(dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(back, front),
                                                        collection.get(), brush.put())))
        {
            dc->FillGeometry(geometry.get(), brush.get());
        }
    };

    // Everything except the caption buttons, which the DWM draws.
    const D2D1_RECT_F whole = D2D1::RectF(0.0f, 0.0f, w, h);
    wil::com_ptr<ID2D1RectangleGeometry> all;
    wil::com_ptr<ID2D1RectangleGeometry> buttons;
    wil::com_ptr<ID2D1PathGeometry> mask;
    wil::com_ptr<ID2D1GeometrySink> sink;
    if (FAILED_LOG(factory->CreateRectangleGeometry(whole, all.put())) ||
        FAILED_LOG(factory->CreateRectangleGeometry(layout.captionButtons, buttons.put())) ||
        FAILED_LOG(factory->CreatePathGeometry(mask.put())) || FAILED_LOG(mask->Open(sink.put())) ||
        FAILED_LOG(all->CombineWithGeometry(buttons.get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.get())) ||
        FAILED_LOG(sink->Close()))
    {
        return;
    }
    dc->PushLayer(D2D1::LayerParameters1(whole, mask.get()), nullptr);

    if (T > 0.0f)
    {
        face({backTopLeft, backTopRight, frontTopRight, frontTopLeft}, topLight, D2D1::Point2F(0.0f, o),
             D2D1::Point2F(0.0f, o + T));
    }
    if (L > 0.0f)
    {
        face({backTopLeft, frontTopLeft, frontBottomLeft, backBottomLeft}, sideLight, D2D1::Point2F(0.0f, 0.0f),
             D2D1::Point2F(L, 0.0f));
    }
    if (B > 0.0f)
    {
        face({backBottomLeft, frontBottomLeft, frontBottomRight, backBottomRight}, bottomShade,
             D2D1::Point2F(0.0f, h), D2D1::Point2F(0.0f, h - B));
    }

    // The front face's edges: a line of light where it meets the top and left faces, a line
    // of shade where it meets the bottom one, and a fainter crease along each mitre.
    wil::com_ptr<ID2D1SolidColorBrush> line;
    if (SUCCEEDED_LOG(dc->CreateSolidColorBrush(edgeLight, line.put())))
    {
        if (T > 0.0f)
        {
            dc->DrawLine(D2D1::Point2F(L, o + T + 0.5f), D2D1::Point2F(w, o + T + 0.5f), line.get(), 1.0f);
        }
        if (L > 0.0f)
        {
            dc->DrawLine(D2D1::Point2F(L + 0.5f, o + T), D2D1::Point2F(L + 0.5f, h - B), line.get(), 1.0f);
        }
        if (B > 0.0f)
        {
            line->SetColor(edgeShade);
            dc->DrawLine(D2D1::Point2F(L, h - B - 0.5f), D2D1::Point2F(w, h - B - 0.5f), line.get(), 1.0f);
        }
        D2D1_COLOR_F crease = edgeLight;
        crease.a *= 0.5f;
        if (T > 0.0f && L > 0.0f)
        {
            line->SetColor(crease);
            dc->DrawLine(backTopLeft, frontTopLeft, line.get(), 1.0f);
        }
        if (B > 0.0f && L > 0.0f)
        {
            crease = edgeShade;
            crease.a *= 0.5f;
            line->SetColor(crease);
            dc->DrawLine(backBottomLeft, frontBottomLeft, line.get(), 1.0f);
        }
    }
    dc->PopLayer();
}

} // namespace te::SurfacePainter
