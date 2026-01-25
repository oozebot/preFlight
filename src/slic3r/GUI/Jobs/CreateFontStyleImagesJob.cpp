///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Filip Sykala @Jony01
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "CreateFontStyleImagesJob.hpp"

#include "slic3r/GUI/3DScene.hpp" // ::glsafe

// ability to request new frame after finish rendering
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

using namespace Slic3r;
using namespace Slic3r::Emboss;
using namespace Slic3r::GUI;
using namespace Slic3r::GUI::Emboss;

CreateFontStyleImagesJob::CreateFontStyleImagesJob(StyleManager::StyleImagesData &&input)
    : m_input(std::move(input)), m_width(0), m_height(0)
{
    assert(m_input.result != nullptr);
    assert(!m_input.styles.empty());
    assert(!m_input.text.empty());
    assert(m_input.max_size.x() > 1);
    assert(m_input.max_size.y() > 1);
    assert(m_input.ppm > 1e-5);
}

void CreateFontStyleImagesJob::process(Ctl &ctl)
{
    // create shapes and calc size (bounding boxes)
    std::vector<ExPolygons> name_shapes(m_input.styles.size());
    std::vector<double> scales(m_input.styles.size());
    m_images = std::vector<StyleManager::StyleImage>(m_input.styles.size());

    auto was_canceled = []()
    {
        return false;
    };
    for (auto &item : m_input.styles)
    {
        size_t index = &item - &m_input.styles.front();
        ExPolygons &shapes = name_shapes[index];
        shapes = text2shapes(item.font, m_input.text.c_str(), item.prop, was_canceled);

        // create image description
        StyleManager::StyleImage &image = m_images[index];
        BoundingBox &bounding_box = image.bounding_box;
        for (const ExPolygon &shape : shapes)
            bounding_box.merge(BoundingBox(shape.contour.points));
        for (ExPolygon &shape : shapes)
            shape.translate(-bounding_box.min);

        // calculate conversion from FontPoint to screen pixels by size of font
        double scale = get_text_shape_scale(item.prop, *item.font.font_file) * m_input.ppm;
        scales[index] = scale;

        //double scale = font_prop.size_in_mm * SCALING_FACTOR;
        BoundingBoxf bb2(bounding_box.min.cast<double>(), bounding_box.max.cast<double>());
        bb2.scale(scale);
        image.tex_size.x = std::ceil(bb2.max.x() - bb2.min.x());
        image.tex_size.y = std::ceil(bb2.max.y() - bb2.min.y());

        // crop image width
        if (image.tex_size.x > m_input.max_size.x())
            image.tex_size.x = m_input.max_size.x();
        // crop image height
        if (image.tex_size.y > m_input.max_size.y())
            image.tex_size.y = m_input.max_size.y();
    }

    // arrange bounding boxes
    int offset_y = 0;
    m_width = 0;
    for (StyleManager::StyleImage &image : m_images)
    {
        image.offset.y() = offset_y;
        offset_y += image.tex_size.y + 1;
        if (m_width < image.tex_size.x)
            m_width = image.tex_size.x;
    }
    m_height = offset_y;
    for (StyleManager::StyleImage &image : m_images)
    {
        const Point &o = image.offset;
        const ImVec2 &s = image.tex_size;
        image.uv0 = ImVec2(o.x() / (double) m_width, o.y() / (double) m_height);
        image.uv1 = ImVec2((o.x() + s.x) / (double) m_width, (o.y() + s.y) / (double) m_height);
    }

    // Set up result
    m_pixels = std::vector<unsigned char>(4 * m_width * m_height, {255});

    // Font style preview images will not be generated
    (void) scales;      // suppress unused warning
    (void) name_shapes; // suppress unused warning
}

void CreateFontStyleImagesJob::finalize(bool canceled, std::exception_ptr &)
{
    if (canceled)
        return;
    // upload texture on GPU
    GLuint tex_id;
    GLenum target = GL_TEXTURE_2D, format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    GLint level = 0, border = 0;
    glsafe(::glGenTextures(1, &tex_id));
    glsafe(::glBindTexture(target, tex_id));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    GLint w = m_width, h = m_height;
    glsafe(::glTexImage2D(target, level, GL_RGBA, w, h, border, format, type, (const void *) m_pixels.data()));

    // set up texture id
    void *texture_id = (void *) (intptr_t) tex_id;
    for (StyleManager::StyleImage &image : m_images)
        image.texture_id = texture_id;

    // move to result
    m_input.result->styles = std::move(m_input.styles);
    m_input.result->images = std::move(m_images);

    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    // show rendered texture
    wxGetApp().plater()->canvas3D()->schedule_extra_frame(0);
}