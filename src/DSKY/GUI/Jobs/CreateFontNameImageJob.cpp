///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2022 Filip Sykala @Jony01
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "CreateFontNameImageJob.hpp"

#include "luminary/gizmo/emboss/Emboss.hpp"

#include "DSKY/Utils/WxFontUtils.hpp"
#include "DSKY/GUI/3DScene.hpp" // ::glsafe

// ability to request new frame after finish rendering
#include "DSKY/GUI/GUI_App.hpp"
#include "DSKY/GUI/Plater.hpp"
#include "DSKY/GUI/GLCanvas3D.hpp"

#include "wx/fontenum.h"

#include <boost/log/trivial.hpp>

using namespace Luminary;
using namespace DSKY;

const std::string CreateFontImageJob::default_text = "AaBbCc 123";

CreateFontImageJob::CreateFontImageJob(FontImageData &&input) : m_input(std::move(input))
{
    assert(wxFontEnumerator::IsValidFacename(m_input.font_name));
    assert(m_input.gray_level > 0 && m_input.gray_level < 255);
    assert(m_input.texture_id != 0);
}

void CreateFontImageJob::process(Ctl &ctl)
{
    if (!wxFontEnumerator::IsValidFacename(m_input.font_name))
        return;
    // Select font
    wxFont wx_font(wxFontInfo().FaceName(m_input.font_name).Encoding(m_input.encoding));
    if (!wx_font.IsOk())
        return;

    std::unique_ptr<Emboss::FontFile> font_file = WxFontUtils::create_font_file(wx_font);
    if (font_file == nullptr)
        return;

    Emboss::FontFileWithCache font_file_with_cache(std::move(font_file));
    // use only first line of text
    std::string &text = m_input.text;
    if (text.empty())
        text = default_text; // copy

    size_t enter_pos = text.find('\n');
    if (enter_pos < text.size())
    {
        // text start with enter
        if (enter_pos == 0)
            return;
        // exist enter, soo delete all after enter
        text = text.substr(0, enter_pos);
    }

    std::function<bool()> was_canceled = [&ctl, cancel = m_input.cancel]() -> bool
    {
        if (ctl.was_canceled())
            return true;
        if (cancel->load())
            return true;
        return false;
    };

    FontProp fp; // create default font parameters
    ExPolygons shapes = Emboss::text2shapes(font_file_with_cache, text.c_str(), fp, was_canceled);

    // select some character from font e.g. default text
    if (shapes.empty())
        shapes = Emboss::text2shapes(font_file_with_cache, default_text.c_str(), fp, was_canceled);
    if (shapes.empty())
    {
        m_input.cancel->store(true);
        return;
    }

    // normalize height of font
    BoundingBox bounding_box;
    for (const ExPolygon &shape : shapes)
        bounding_box.merge(BoundingBox(shape.contour.points));
    if (bounding_box.size().x() < 1 || bounding_box.size().y() < 1)
    {
        m_input.cancel->store(true);
        return;
    }
    double scale = m_input.size.y() / (double) bounding_box.size().y();
    BoundingBoxf bb2(bounding_box.min.cast<double>(), bounding_box.max.cast<double>());
    bb2.scale(scale);
    Vec2d size_f = bb2.size();
    m_tex_size = Point(std::ceil(size_f.x()), std::ceil(size_f.y()));
    // crop image width
    if (m_tex_size.x() > m_input.size.x())
        m_tex_size.x() = m_input.size.x();
    if (m_tex_size.y() > m_input.size.y())
        m_tex_size.y() = m_input.size.y();

    // Set up result - white pixels with alpha driven by rasterization
    unsigned bit_count = 4; // RGBA
    m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * bit_count, {255});

    for (ExPolygon &shape : shapes)
        shape.translate(-bounding_box.min);

    // Rasterize font shapes into the alpha channel via point-in-polygon testing
    int w = m_tex_size.x(), h = m_tex_size.y();
    unsigned char alpha = static_cast<unsigned char>(255 / m_input.gray_level);
    for (int py = 0; py < h; ++py)
    {
        for (int px = 0; px < w; ++px)
        {
            Point pt(static_cast<coord_t>(px / scale), static_cast<coord_t>((h - 1 - py) / scale));
            for (const ExPolygon &shape : shapes)
            {
                if (shape.contains(pt))
                {
                    m_result[3 + 4 * (py * w + px)] = alpha;
                    break;
                }
            }
        }
    }
}

void CreateFontImageJob::finalize(bool canceled, std::exception_ptr &)
{
    if (m_input.count_opened_font_files)
        --(*m_input.count_opened_font_files);
    if (canceled || m_input.cancel->load())
        return;

    *m_input.is_created = true;

    // Exist result bitmap with preview?
    // (not valid input. e.g. not loadable font)
    if (m_result.empty())
    {
        // No glyphs were rendered, so fill the preview row with opaque white.
        m_result = std::vector<unsigned char>(m_tex_size.x() * m_tex_size.y() * 4, {255});
    }

    // upload texture on GPU
    const GLenum target = GL_TEXTURE_2D;
    glsafe(::glBindTexture(target, m_input.texture_id));

    GLsizei w = m_tex_size.x(), h = m_tex_size.y();
    GLint xoffset = 0; // align to left
    // GLint xoffset = m_input.size.x() - m_tex_size.x(); // align right
    GLint yoffset = m_input.size.y() * m_input.index;
    glsafe(::glTexSubImage2D(target, m_input.level, xoffset, yoffset, w, h, m_input.format, m_input.type,
                             m_result.data()));

    // clear rest of texture
    std::vector<unsigned char> empty_data(xoffset * h * 4, {0});
    glsafe(::glTexSubImage2D(target, m_input.level, 0, yoffset, xoffset, h, m_input.format, m_input.type,
                             empty_data.data()));

    // bind default texture
    GLuint no_texture_id = 0;
    glsafe(::glBindTexture(target, no_texture_id));

    // show rendered texture
    wxGetApp().plater()->canvas3D()->schedule_extra_frame(0);

    BOOST_LOG_TRIVIAL(info) << "Generate Preview font('" << m_input.font_name << "' id:" << m_input.index << ") "
                            << "with text: '" << m_input.text << "' "
                            << "texture_size " << m_input.size.x() << " x " << m_input.size.y();
}