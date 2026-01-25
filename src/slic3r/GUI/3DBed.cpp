///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2023 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) 2022 Michael Kirsch
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"

#include "3DBed.hpp"

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry/Circle.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/MultipleBeds.hpp"

#include "GUI_App.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"
#include "Camera.hpp"

#if SLIC3R_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>

#include <numeric>

static const float GROUND_Z = -0.01f;
static const Slic3r::ColorRGBA DEFAULT_MODEL_COLOR = {92.0f / 255.0f, 92.0f / 255.0f, 92.0f / 255.0f, 1.0f};
static const Slic3r::ColorRGBA PICKING_MODEL_COLOR = Slic3r::ColorRGBA::BLACK();
static const Slic3r::ColorRGBA DEFAULT_SOLID_GRID_COLOR = {0.5f, 0.5f, 0.5f, 0.66f};
static const Slic3r::ColorRGBA DEFAULT_TRANSPARENT_GRID_COLOR = {0.5f, 0.5f, 0.5f, 0.66f};
static const Slic3r::ColorRGBA DISABLED_MODEL_COLOR = {0.6f, 0.6f, 0.6f, 0.75f};

namespace Slic3r
{
namespace GUI
{

bool Bed3D::set_shape(const Pointfs &bed_shape, const double max_print_height, const std::string &custom_texture,
                      const std::string &custom_model, bool force_as_custom)
{
    auto check_texture = [](const std::string &texture)
    {
        boost::system::error_code ec; // so the exists call does not throw (e.g. after a permission problem)
        return !texture.empty() &&
               (boost::algorithm::iends_with(texture, ".png") || boost::algorithm::iends_with(texture, ".svg")) &&
               boost::filesystem::exists(texture, ec);
    };

    auto check_model = [](const std::string &model)
    {
        boost::system::error_code ec;
        return !model.empty() && boost::algorithm::iends_with(model, ".stl") && boost::filesystem::exists(model, ec);
    };

    Type type;
    std::string model;
    std::string texture;
    if (force_as_custom)
        type = Type::Custom;
    else
    {
        auto [new_type, system_model, system_texture] = detect_type(bed_shape);
        type = new_type;
        model = system_model;
        texture = system_texture;
    }

    std::string texture_filename = custom_texture.empty() ? texture : custom_texture;
    if (!texture_filename.empty() && !check_texture(texture_filename))
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to load bed texture: " << texture_filename;
        texture_filename.clear();
    }

    std::string model_filename = custom_model.empty() ? model : custom_model;
    if (!model_filename.empty() && !check_model(model_filename))
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to load bed model: " << model_filename;
        model_filename.clear();
    }

    if (m_build_volume.bed_shape() == bed_shape && m_build_volume.max_print_height() == max_print_height &&
        m_type == type && m_texture_filename == texture_filename && m_model_filename == model_filename)
        // No change, no need to update the UI.
        return false;

    m_type = type;
    m_build_volume = BuildVolume{bed_shape, max_print_height};
    m_texture_filename = texture_filename;
    m_model_filename = model_filename;
    m_extended_bounding_box = this->calc_extended_bounding_box();

    // Configurable corner radius in mm (easy to adjust)
    const double corner_radius_mm = 5.0; // Change this value to adjust roundness

    Pointfs rounded_bed_shape = bed_shape;

    // Only round corners for rectangular beds (4 corners)
    if (bed_shape.size() == 4)
    {
        rounded_bed_shape.clear();
        const int segments_per_corner = 16; // Number of points per rounded corner (higher = smoother)

        for (size_t i = 0; i < bed_shape.size(); ++i)
        {
            const Vec2d &prev = bed_shape[(i + bed_shape.size() - 1) % bed_shape.size()];
            const Vec2d &curr = bed_shape[i];
            const Vec2d &next = bed_shape[(i + 1) % bed_shape.size()];

            // Calculate direction vectors
            Vec2d v1 = (prev - curr).normalized();
            Vec2d v2 = (next - curr).normalized();

            // Calculate the points where the arc starts and ends
            Vec2d arc_start = curr + v1 * corner_radius_mm;
            Vec2d arc_end = curr + v2 * corner_radius_mm;

            // Add the arc start point
            rounded_bed_shape.push_back(arc_start);

            // Generate arc points
            for (int j = 1; j < segments_per_corner; ++j)
            {
                double t = double(j) / double(segments_per_corner);
                // Use quadratic bezier curve with corner as control point
                Vec2d p = (1.0 - t) * (1.0 - t) * arc_start + 2.0 * (1.0 - t) * t * curr + t * t * arc_end;
                rounded_bed_shape.push_back(p);
            }
        }
    }

    m_contour = ExPolygon(Polygon::new_scale(rounded_bed_shape));
    const BoundingBox bbox = m_contour.contour.bounding_box();
    if (!bbox.defined)
        throw RuntimeError(std::string("Invalid bed shape"));

    m_triangles.reset();
    m_gridlines.reset();
    m_contourlines.reset();
    m_texture.reset();
    m_model.reset();

    // unregister from picking
    wxGetApp().plater()->canvas3D()->remove_raycasters_for_picking(SceneRaycaster::EType::Bed);

    init_internal_model_from_file();
    init_triangles();

    s_multiple_beds.update_build_volume(m_build_volume.bounding_volume2d());

    m_models_overlap = false;
    if (!m_model_filename.empty())
    {
        // Calculate bb of the bed model and figure out if the models would overlap when rendered next to each other.
        const BoundingBoxf3 &mdl_bb3 = m_model.model.get_bounding_box();
        const BoundingBoxf model_bb(Vec2d(mdl_bb3.min.x(), mdl_bb3.min.y()), Vec2d(mdl_bb3.max.x(), mdl_bb3.max.y()));
        BoundingBoxf bed_bb = m_build_volume.bounding_volume2d();
        bed_bb.translate(-m_model_offset.x(), -m_model_offset.y());
        Vec2d gap = unscale(s_multiple_beds.get_bed_gap());
        m_models_overlap = (model_bb.size().x() - bed_bb.size().x() > 2 * gap.x() ||
                            model_bb.size().y() - bed_bb.size().y() > 2 * gap.y());
    }

    // Set the origin and size for rendering the coordinate system axes.
    m_axes.set_origin({0.0, 0.0, static_cast<double>(GROUND_Z)});
    m_axes.set_stem_length(0.1f * static_cast<float>(m_build_volume.bounding_volume().max_size()));

    // Let the calee to update the UI.
    return true;
}

void Bed3D::render(GLCanvas3D &canvas, const Transform3d &view_matrix, const Transform3d &projection_matrix,
                   bool bottom, float scale_factor, bool show_texture)
{
    bool is_thumbnail = s_multiple_beds.get_thumbnail_bed_idx() != -1;
    bool is_preview = wxGetApp().plater()->is_preview_shown();
    int bed_to_highlight = s_multiple_beds.get_active_bed();

    static std::vector<int> beds_to_render;
    beds_to_render.clear();
    if (is_thumbnail)
        beds_to_render.push_back(s_multiple_beds.get_thumbnail_bed_idx());
    else if (is_preview)
        beds_to_render.push_back(s_multiple_beds.get_active_bed());
    else
    {
        beds_to_render.resize(s_multiple_beds.get_number_of_beds() + int(s_multiple_beds.should_show_next_bed()));
        std::iota(beds_to_render.begin(), beds_to_render.end(), 0);
    }

    for (int i : beds_to_render)
    {
        Transform3d mat = view_matrix;
        mat.translate(s_multiple_beds.get_bed_translation(i));
        render_internal(canvas, mat, projection_matrix, bottom, scale_factor, show_texture, false,
                        is_thumbnail || i == bed_to_highlight);
    }

    if (m_digits_models.empty())
    {
        for (size_t i = 0; i < 10; ++i)
        {
            GLModel::Geometry g;
            g.format.vertex_layout = GLModel::Geometry::EVertexLayout::P3T2;
            const double digit_part = 94. / 1024.;
            g.add_vertex(Vec3f(0, 0, 0), Vec2f(digit_part * i, 1.));
            g.add_vertex(Vec3f(1, 0, 0), Vec2f(digit_part * (i + 1), 1.));
            g.add_vertex(Vec3f(1, 1, 0), Vec2f(digit_part * (i + 1), 0));
            g.add_vertex(Vec3f(0, 1, 0), Vec2f(digit_part * i, 0));
            g.add_triangle(0, 1, 3);
            g.add_triangle(3, 1, 2);
            m_digits_models.emplace_back(std::make_unique<GLModel>());
            m_digits_models.back()->init_from(std::move(g));
            m_digits_models.back()->set_color(ColorRGBA(0.5f, 0.5f, 0.5f, 0.5f));
        }
        m_digits_texture = std::make_unique<GLTexture>();
        m_digits_texture->load_from_file(resources_dir() + "/icons/numbers.png", true,
                                         GLTexture::ECompressionType::None, false);
        m_digits_texture->send_compressed_data_to_gpu();
    }
    if (!is_thumbnail && s_multiple_beds.get_number_of_beds() > 1)
    {
        GLShaderProgram *shader = wxGetApp().get_shader("flat_texture");
        shader->start_using();
        shader->set_uniform("projection_matrix", projection_matrix);
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glDepthMask(GL_FALSE));
        const bool old_cullface = ::glIsEnabled(GL_CULL_FACE);
        glsafe(::glDisable(GL_CULL_FACE));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        glsafe(::glBindTexture(GL_TEXTURE_2D, m_digits_texture->get_id()));

        const BoundingBoxf bb = this->build_volume().bounding_volume2d();

        for (int i : beds_to_render)
        {
            if (i + 1 >= m_digits_models.size())
                break;

            double size_x = std::max(10., std::min(bb.size().x(), bb.size().y()) * 0.11);
            double aspect = 1.2;
            Transform3d mat = view_matrix;
            mat.translate(Vec3d(bb.min.x(), bb.min.y(), 0.));
            mat.translate(s_multiple_beds.get_bed_translation(i));
            if (build_volume().type() != BuildVolume::Type::Circle)
                mat.translate(Vec3d(0.3 * size_x, 0.3 * size_x, 0.));
            mat.translate(Vec3d(0., 0., 0.5 * GROUND_Z));
            mat.scale(Vec3d(size_x, size_x * aspect, 1.));

            shader->set_uniform("view_model_matrix", mat);
            m_digits_models[i + 1]->render();
        }
        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
        if (old_cullface)
            glsafe(::glEnable(GL_CULL_FACE));
        glsafe(::glDepthMask(GL_TRUE));
        glsafe(::glDisable(GL_DEPTH_TEST));
        shader->stop_using();
    }
}

void Bed3D::render_for_picking(GLCanvas3D &canvas, const Transform3d &view_matrix, const Transform3d &projection_matrix,
                               bool bottom, float scale_factor)
{
    render_internal(canvas, view_matrix, projection_matrix, bottom, scale_factor, false, true, false);
}

void Bed3D::render_internal(GLCanvas3D &canvas, const Transform3d &view_matrix, const Transform3d &projection_matrix,
                            bool bottom, float scale_factor, bool show_texture, bool picking, bool active)
{
    m_scale_factor = scale_factor;

    glsafe(::glEnable(GL_DEPTH_TEST));

    m_model.model.set_color(picking ? PICKING_MODEL_COLOR : DEFAULT_MODEL_COLOR);
    m_triangles.set_color(picking ? PICKING_MODEL_COLOR : DEFAULT_MODEL_COLOR);
    if (!picking && !active)
    {
        m_model.model.set_color(DISABLED_MODEL_COLOR);
        m_triangles.set_color(DISABLED_MODEL_COLOR);
    }

    switch (m_type)
    {
    case Type::System:
    {
        render_system(canvas, view_matrix, projection_matrix, bottom, show_texture, active);
        break;
    }
    default:
    case Type::Custom:
    {
        render_custom(canvas, view_matrix, projection_matrix, bottom, show_texture, picking, active);
        break;
    }
    }

    glsafe(::glDisable(GL_DEPTH_TEST));
}

// Calculate an extended bounding box from axes and current model for visualization purposes.
BoundingBoxf3 Bed3D::calc_extended_bounding_box() const
{
    BoundingBoxf3 out{m_build_volume.bounding_volume()};
    const Vec3d size = out.size();
    // ensures that the bounding box is set as defined or the following calls to merge() will not work as intented
    if (size.x() > 0.0 && size.y() > 0.0 && !out.defined)
        out.defined = true;
    // Reset the build volume Z, we don't want to zoom to the top of the build volume if it is empty.
    out.min.z() = 0.0;
    out.max.z() = 0.0;
    // extend to origin in case origin is off bed
    out.merge(m_axes.get_origin());
    // extend to contain axes
    out.merge(m_axes.get_origin() + m_axes.get_total_length() * Vec3d::Ones());
    out.merge(out.min + Vec3d(-m_axes.get_tip_radius(), -m_axes.get_tip_radius(), out.max.z()));
    // extend to contain model, if any
    BoundingBoxf3 model_bb = m_model.model.get_bounding_box();
    if (model_bb.defined)
    {
        model_bb.translate(m_model_offset);
        out.merge(model_bb);
    }
    return out;
}

void Bed3D::init_triangles()
{
    if (m_triangles.is_initialized())
        return;

    if (m_contour.empty())
        return;

    const std::vector<Vec2f> triangles = triangulate_expolygon_2f(m_contour, NORMALS_UP);
    if (triangles.empty() || triangles.size() % 3 != 0)
        return;

    GLModel::Geometry init_data;
    init_data.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3T2};
    init_data.reserve_vertices(triangles.size());
    init_data.reserve_indices(triangles.size() / 3);

    Vec2f min = triangles.front();
    Vec2f max = min;
    for (const Vec2f &v : triangles)
    {
        min = min.cwiseMin(v).eval();
        max = max.cwiseMax(v).eval();
    }

    const Vec2f size = max - min;
    if (size.x() <= 0.0f || size.y() <= 0.0f)
        return;

    Vec2f inv_size = size.cwiseInverse();
    inv_size.y() *= -1.0f;

    // vertices + indices
    unsigned int vertices_counter = 0;
    for (const Vec2f &v : triangles)
    {
        const Vec3f p = {v.x(), v.y(), GROUND_Z};
        init_data.add_vertex(p, (Vec2f) (v - min).cwiseProduct(inv_size).eval());
        ++vertices_counter;
        if (vertices_counter % 3 == 0)
            init_data.add_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
    }

    if (m_model.model.get_filename().empty() && m_model.mesh_raycaster == nullptr)
        // register for picking
        register_raycasters_for_picking(init_data, Transform3d::Identity());

    m_triangles.init_from(std::move(init_data));
}

void Bed3D::init_gridlines()
{
    if (m_gridlines.is_initialized())
        return;

    if (m_contour.empty())
        return;

    const BoundingBox &bed_bbox = m_contour.contour.bounding_box();
    const coord_t step = scale_(20.0);

    Polylines axes_lines;
    for (coord_t x = bed_bbox.min.x(); x <= bed_bbox.max.x(); x += step)
    {
        Polyline line;
        line.append(Point(x, bed_bbox.min.y()));
        line.append(Point(x, bed_bbox.max.y()));
        axes_lines.push_back(line);
    }
    for (coord_t y = bed_bbox.min.y(); y <= bed_bbox.max.y(); y += step)
    {
        Polyline line;
        line.append(Point(bed_bbox.min.x(), y));
        line.append(Point(bed_bbox.max.x(), y));
        axes_lines.push_back(line);
    }

    // clip with a slightly grown expolygon because our lines lay on the contours and may get erroneously clipped
    Polygons offset_contour = offset(m_contour, float(SCALED_EPSILON));
    Polylines intersection_result = intersection_pl(axes_lines, offset_contour);
    Lines gridlines = to_lines(intersection_result);

    // Store contour lines separately - they'll be added later without dash offset
    Lines contour_lines = to_lines(m_contour);

    const float bed_min_x = unscale<float>(bed_bbox.min.x());
    const float bed_min_y = unscale<float>(bed_bbox.min.y());

    GLModel::Geometry init_data;
    init_data.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P4};
    init_data.reserve_vertices(2 * (gridlines.size() + contour_lines.size()));
    init_data.reserve_indices(2 * (gridlines.size() + contour_lines.size()));

    // Process grid lines with proper phase offset
    for (const Slic3r::Line &l : gridlines)
    {
        Vec3f start(unscale<float>(l.a.x()), unscale<float>(l.a.y()), GROUND_Z);
        Vec3f end(unscale<float>(l.b.x()), unscale<float>(l.b.y()), GROUND_Z);
        float distance = (end - start).norm();

        // Use coordinates relative to bed minimum so pattern is independent of origin setting
        bool is_vertical = std::abs(start.x() - end.x()) < 0.01f;

        if (is_vertical)
        {
            // Vertical line - use Y coordinate relative to bed minimum, offset by 5mm to center crosshairs in dashes
            float start_dist = start.y() - bed_min_y + 5.0f;
            float end_dist = end.y() - bed_min_y + 5.0f;
            init_data.add_vertex(Vec4f(start.x(), start.y(), start.z(), start_dist));
            init_data.add_vertex(Vec4f(end.x(), end.y(), end.z(), end_dist));
        }
        else
        {
            // Horizontal line - use X coordinate relative to bed minimum, offset by 5mm to center crosshairs in dashes
            float start_dist = start.x() - bed_min_x + 5.0f;
            float end_dist = end.x() - bed_min_x + 5.0f;
            init_data.add_vertex(Vec4f(start.x(), start.y(), start.z(), start_dist));
            init_data.add_vertex(Vec4f(end.x(), end.y(), end.z(), end_dist));
        }
        const unsigned int vertices_counter = (unsigned int) init_data.vertices_count();
        init_data.add_line(vertices_counter - 2, vertices_counter - 1);
    }

    // Add contour lines without offset
    for (const Slic3r::Line &l : contour_lines)
    {
        Vec3f start(unscale<float>(l.a.x()), unscale<float>(l.a.y()), GROUND_Z);
        Vec3f end(unscale<float>(l.b.x()), unscale<float>(l.b.y()), GROUND_Z);
        float distance = (end - start).norm();

        init_data.add_vertex(Vec4f(start.x(), start.y(), start.z(), 0.0f));
        init_data.add_vertex(Vec4f(end.x(), end.y(), end.z(), distance));
        const unsigned int vertices_counter = (unsigned int) init_data.vertices_count();
        init_data.add_line(vertices_counter - 2, vertices_counter - 1);
    }

    m_gridlines.init_from(std::move(init_data));
}

void Bed3D::init_contourlines()
{
    if (m_contourlines.is_initialized())
        return;

    if (m_contour.empty())
        return;

    const float line_width_mm = 1.0f;                // 1mm wide line
    const float inset_amount = line_width_mm * 0.5f; // Inset by half the line width
    ExPolygon inset_contour = offset_ex(m_contour, -scale_(inset_amount)).empty()
                                  ? m_contour
                                  : offset_ex(m_contour, -scale_(inset_amount)).front();
    const Lines contour_lines = to_lines(inset_contour);

    GLModel::Geometry init_data;
    init_data.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
    init_data.reserve_vertices(2 * contour_lines.size());
    init_data.reserve_indices(2 * contour_lines.size());

    for (const Slic3r::Line &l : contour_lines)
    {
        init_data.add_vertex(Vec3f(unscale<float>(l.a.x()), unscale<float>(l.a.y()), GROUND_Z));
        init_data.add_vertex(Vec3f(unscale<float>(l.b.x()), unscale<float>(l.b.y()), GROUND_Z));
        const unsigned int vertices_counter = (unsigned int) init_data.vertices_count();
        init_data.add_line(vertices_counter - 2, vertices_counter - 1);
    }

    m_contourlines.init_from(std::move(init_data));
    m_contourlines.set_color({0.5f, 0.5f, 0.5f, 0.66f}); // Same as grid
}

// Try to match the print bed shape with the shape of an active profile. If such a match exists,
// return the print bed model.
std::tuple<Bed3D::Type, std::string, std::string> Bed3D::detect_type(const Pointfs &shape)
{
    auto bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr)
    {
        const Preset *curr = &bundle->printers.get_selected_preset();
        while (curr != nullptr)
        {
            if (curr->config.has("bed_shape"))
            {
                if (shape == dynamic_cast<const ConfigOptionPoints *>(curr->config.option("bed_shape"))->values)
                {
                    std::string model_filename = PresetUtils::system_printer_bed_model(*curr);
                    std::string texture_filename = PresetUtils::system_printer_bed_texture(*curr);
                    if (!model_filename.empty() && !texture_filename.empty())
                        return {Type::System, model_filename, texture_filename};
                }
            }

            curr = bundle->printers.get_preset_parent(*curr);
        }
    }

    return {Type::Custom, {}, {}};
}

void Bed3D::render_axes()
{
    if (m_build_volume.valid())
        m_axes.render(Transform3d::Identity(), 0.25f);
}

void Bed3D::render_system(GLCanvas3D &canvas, const Transform3d &view_matrix, const Transform3d &projection_matrix,
                          bool bottom, bool show_texture, bool is_active)
{
    if (m_models_overlap && s_multiple_beds.get_number_of_beds() + int(s_multiple_beds.should_show_next_bed()) > 1)
    {
        render_default(bottom, false, show_texture, view_matrix, projection_matrix, canvas);
        return;
    }

    if (!bottom)
        render_model(view_matrix, projection_matrix);

    if (show_texture)
        render_texture(bottom, canvas, view_matrix, projection_matrix, is_active);
    else if (bottom)
        render_contour(view_matrix, projection_matrix);
}

void Bed3D::render_texture(bool bottom, GLCanvas3D &canvas, const Transform3d &view_matrix,
                           const Transform3d &projection_matrix, bool is_active)
{
    if (m_texture_filename.empty())
    {
        m_texture.reset();
        render_default(bottom, false, true, view_matrix, projection_matrix, canvas);
        return;
    }

    if (m_texture.get_id() == 0 || m_texture.get_source() != m_texture_filename)
    {
        m_texture.reset();

        if (boost::algorithm::iends_with(m_texture_filename, ".svg"))
        {
            // use higher resolution images if graphic card and opengl version allow
            GLint max_tex_size = OpenGLManager::get_gl_info().get_max_tex_size();
            if (m_temp_texture.get_id() == 0 || m_temp_texture.get_source() != m_texture_filename)
            {
                // generate a temporary lower resolution texture to show while no main texture levels have been compressed
                if (!m_temp_texture.load_from_svg_file(m_texture_filename, false, false, false, max_tex_size / 8))
                {
                    render_default(bottom, false, true, view_matrix, projection_matrix, canvas);
                    return;
                }
                canvas.request_extra_frame();
            }

            // starts generating the main texture, compression will run asynchronously
            if (!m_texture.load_from_svg_file(m_texture_filename, true, true, true, max_tex_size))
            {
                render_default(bottom, false, true, view_matrix, projection_matrix, canvas);
                return;
            }
        }
        else if (boost::algorithm::iends_with(m_texture_filename, ".png"))
        {
            // generate a temporary lower resolution texture to show while no main texture levels have been compressed
            if (m_temp_texture.get_id() == 0 || m_temp_texture.get_source() != m_texture_filename)
            {
                if (!m_temp_texture.load_from_file(m_texture_filename, false, GLTexture::None, false))
                {
                    render_default(bottom, false, true, view_matrix, projection_matrix, canvas);
                    return;
                }
                canvas.request_extra_frame();
            }

            // starts generating the main texture, compression will run asynchronously
            if (!m_texture.load_from_file(m_texture_filename, true, GLTexture::MultiThreaded, true))
            {
                render_default(bottom, false, true, view_matrix, projection_matrix, canvas);
                return;
            }
        }
        else
        {
            render_default(bottom, false, true, view_matrix, projection_matrix, canvas);
            return;
        }
    }
    else if (m_texture.unsent_compressed_data_available())
    {
        // sends to gpu the already available compressed levels of the main texture
        m_texture.send_compressed_data_to_gpu();
        wxQueueEvent(wxGetApp().plater(), new SimpleEvent(EVT_REGENERATE_BED_THUMBNAILS));

        // the temporary texture is not needed anymore, reset it
        if (m_temp_texture.get_id() != 0)
            m_temp_texture.reset();

        canvas.request_extra_frame();
    }

    init_triangles();

    GLShaderProgram *shader = wxGetApp().get_shader("printbed");
    if (shader != nullptr)
    {
        shader->start_using();
        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);
        shader->set_uniform("transparent_background", bottom || !is_active);
        shader->set_uniform("svg_source", boost::algorithm::iends_with(m_texture.get_source(), ".svg"));

        glsafe(::glEnable(GL_DEPTH_TEST));
        if (bottom)
            glsafe(::glDepthMask(GL_FALSE));

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        if (bottom)
            glsafe(::glFrontFace(GL_CW));

        // show the temporary texture while no compressed data is available
        GLuint tex_id = (GLuint) m_temp_texture.get_id();
        if (tex_id == 0)
            tex_id = (GLuint) m_texture.get_id();

        glsafe(::glBindTexture(GL_TEXTURE_2D, tex_id));
        m_triangles.render();
        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

        // Temporarily disabled - may use different approach
        /*
        if (!bottom) {
            render_logo(canvas, view_matrix, projection_matrix);
        }
        */

        if (bottom)
            glsafe(::glFrontFace(GL_CCW));

        glsafe(::glDisable(GL_BLEND));
        if (bottom)
            glsafe(::glDepthMask(GL_TRUE));

        shader->stop_using();
    }
}

void Bed3D::init_internal_model_from_file()
{
    if (m_model_filename.empty())
        return;

    if (m_model.model.get_filename() != m_model_filename && m_model.model.init_from_file(m_model_filename))
    {
        m_model.model.set_color(DEFAULT_MODEL_COLOR);

        // move the model so that its origin (0.0, 0.0, 0.0) goes into the bed shape center and a bit down to avoid z-fighting with the texture quad
        m_model_offset = to_3d(m_build_volume.bounding_volume2d().center(), -0.03);

        // register for picking
        const std::vector<std::shared_ptr<SceneRaycasterItem>> *const raycaster =
            wxGetApp().plater()->canvas3D()->get_raycasters_for_picking(SceneRaycaster::EType::Bed);
        if (!raycaster->empty())
        {
            // The raycaster may have been set by the call to init_triangles() made from render_texture() if the printbed was
            // changed while the camera was pointing upward.
            // In this case we need to remove it before creating a new using the model geometry
            wxGetApp().plater()->canvas3D()->remove_raycasters_for_picking(SceneRaycaster::EType::Bed);
            m_model.mesh_raycaster.reset();
        }
        register_raycasters_for_picking(m_model.model.get_geometry(), Geometry::translation_transform(m_model_offset));

        // update extended bounding box
        m_extended_bounding_box = this->calc_extended_bounding_box();
    }
}

void Bed3D::render_model(const Transform3d &view_matrix, const Transform3d &projection_matrix)
{
    if (m_model_filename.empty())
        return;

    init_internal_model_from_file();

    if (!m_model.model.get_filename().empty())
    {
        GLShaderProgram *shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr)
        {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.0f);
            const Transform3d model_matrix = Geometry::translation_transform(m_model_offset);
            shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) *
                                                model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
            shader->set_uniform("view_normal_matrix", view_normal_matrix);
            m_model.model.render();
            shader->stop_using();
        }
    }
}

void Bed3D::render_custom(GLCanvas3D &canvas, const Transform3d &view_matrix, const Transform3d &projection_matrix,
                          bool bottom, bool show_texture, bool picking, bool is_active)
{
    if ((m_texture_filename.empty() && m_model_filename.empty()) ||
        (m_models_overlap && s_multiple_beds.get_number_of_beds() + int(s_multiple_beds.should_show_next_bed()) > 1))
    {
        render_default(bottom, picking, show_texture, view_matrix, projection_matrix, canvas);
        return;
    }

    if (!bottom)
        render_model(view_matrix, projection_matrix);

    if (show_texture)
    {
        render_texture(bottom, canvas, view_matrix, projection_matrix, is_active);
    }
    else if (bottom)
        render_contour(view_matrix, projection_matrix);
}

void Bed3D::render_default(bool bottom, bool picking, bool show_texture, const Transform3d &view_matrix,
                           const Transform3d &projection_matrix, GLCanvas3D &canvas)
{
    m_texture.reset();

    init_gridlines();
    init_triangles();

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader != nullptr)
    {
        shader->start_using();

        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);

        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        const bool has_model = !m_model.model.get_filename().empty() && !m_models_overlap;
        if (!has_model && !bottom)
        {
            // draw background
            glsafe(::glDepthMask(GL_FALSE));
            m_triangles.render();
            glsafe(::glDepthMask(GL_TRUE));
        }

        // Temporarily disabled - may use different approach
        /*
        if (!bottom && show_texture) {
            shader->stop_using();  // Stop current shader before logo
            render_logo(canvas, view_matrix, projection_matrix);
            shader->start_using(); // Restart shader for grid
            shader->set_uniform("view_model_matrix", view_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
        }
        */

        if (show_texture)
        {
            // draw grid
            shader->stop_using(); // Stop flat shader

            GLShaderProgram *grid_shader = wxGetApp().get_shader("dashed_thick_lines");
            if (grid_shader != nullptr)
            {
                grid_shader->start_using();
                grid_shader->set_uniform("view_model_matrix", view_matrix);
                grid_shader->set_uniform("projection_matrix", projection_matrix);

                const std::array<int, 4> &viewport = wxGetApp().plater()->get_camera().get_viewport();
                grid_shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                grid_shader->set_uniform("width", 0.15f);
                grid_shader->set_uniform("dash_size", 10.0f); // 10mm dashes
                grid_shader->set_uniform("gap_size", 10.0f);  // 10mm gaps

                glsafe(::glEnable(GL_BLEND));
                glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

                m_gridlines.set_color(has_model && !bottom ? DEFAULT_SOLID_GRID_COLOR : DEFAULT_TRANSPARENT_GRID_COLOR);
                m_gridlines.render();

                grid_shader->stop_using();
            }

            shader->start_using(); // Restart flat shader
            shader->set_uniform("view_model_matrix", view_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
        }
        else
            render_contour(view_matrix, projection_matrix);

        glsafe(::glDisable(GL_BLEND));

        shader->stop_using();
    }
}

void Bed3D::render_contour(const Transform3d &view_matrix, const Transform3d &projection_matrix)
{
    init_contourlines();

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader != nullptr)
    {
        shader->start_using();
        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);

        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        // draw contour
#if !SLIC3R_OPENGL_ES
        if (!OpenGLManager::get_gl_info().is_core_profile())
            glsafe(::glLineWidth(1.5f * m_scale_factor));
#endif // !SLIC3R_OPENGL_ES
        m_contourlines.render();

        glsafe(::glDisable(GL_BLEND));

        shader->stop_using();
    }
}

void Bed3D::register_raycasters_for_picking(const GLModel::Geometry &geometry, const Transform3d &trafo)
{
    assert(m_model.mesh_raycaster == nullptr);

    indexed_triangle_set its;
    its.vertices.reserve(geometry.vertices_count());
    for (size_t i = 0; i < geometry.vertices_count(); ++i)
    {
        its.vertices.emplace_back(geometry.extract_position_3(i));
    }
    its.indices.reserve(geometry.indices_count() / 3);
    for (size_t i = 0; i < geometry.indices_count() / 3; ++i)
    {
        const size_t tri_id = i * 3;
        its.indices.emplace_back(geometry.extract_index(tri_id), geometry.extract_index(tri_id + 1),
                                 geometry.extract_index(tri_id + 2));
    }

    m_model.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    wxGetApp().plater()->canvas3D()->add_raycaster_for_picking(SceneRaycaster::EType::Bed, 0, *m_model.mesh_raycaster,
                                                               trafo);
}

void Bed3D::render_logo(GLCanvas3D &canvas, const Transform3d &view_matrix, const Transform3d &projection_matrix)
{
    // Load the logo texture if not already loaded
    if (m_logo_texture.get_id() == 0 || m_logo_texture.get_source().empty())
    {
        std::string logo_path = Slic3r::var("preFlight_platter.png");
        if (!m_logo_texture.load_from_file(logo_path, false, GLTexture::None, false))
        {
            // Failed to load logo, just return
            return;
        }

        glBindTexture(GL_TEXTURE_2D, m_logo_texture.get_id());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Get bed bounds to position the logo
    BoundingBoxf bed_bbox = m_build_volume.bounding_volume2d();
    Vec2d bed_center = bed_bbox.center();

    // Logo dimensions - 180mm tall, width based on texture aspect ratio
    float logo_height = 180.0f; // Logo height in mm

    // Calculate width based on actual texture dimensions
    float texture_width = (float) m_logo_texture.get_width();
    float texture_height = (float) m_logo_texture.get_height();
    float aspect_ratio = texture_width / texture_height;
    float logo_width = logo_height * aspect_ratio;

    Vec3d logo_pos;
    logo_pos(0) = bed_center(0) - (logo_width / 2.0f);  // X position (centered)
    logo_pos(1) = bed_center(1) - (logo_height / 2.0f); // Y position (centered)
    logo_pos(2) = 0.0f;                                 // Z position (on bed surface, grid renders above)

    // Create a quad for the logo
    GLModel logo_quad;
    GLModel::Geometry quad_data;
    quad_data.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3T2};

    // Define vertices with texture coordinates
    quad_data.reserve_vertices(4);
    quad_data.reserve_indices(6);

    // Bottom left
    quad_data.add_vertex(Vec3f(logo_pos.x(), logo_pos.y(), logo_pos.z()), Vec2f(0.0f, 1.0f));
    // Bottom right
    quad_data.add_vertex(Vec3f(logo_pos.x() + logo_width, logo_pos.y(), logo_pos.z()), Vec2f(1.0f, 1.0f));
    // Top right
    quad_data.add_vertex(Vec3f(logo_pos.x() + logo_width, logo_pos.y() + logo_height, logo_pos.z()), Vec2f(1.0f, 0.0f));
    // Top left
    quad_data.add_vertex(Vec3f(logo_pos.x(), logo_pos.y() + logo_height, logo_pos.z()), Vec2f(0.0f, 0.0f));

    // Add triangles
    quad_data.add_triangle(0, 1, 2);
    quad_data.add_triangle(0, 2, 3);

    logo_quad.init_from(std::move(quad_data));

    // Render the logo
    GLShaderProgram *shader = wxGetApp().get_shader("printbed");
    if (shader != nullptr)
    {
        shader->start_using();
        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);
        shader->set_uniform("transparent_background", false);
        shader->set_uniform("svg_source", false);

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        glsafe(::glDepthMask(GL_FALSE));

        glsafe(::glBindTexture(GL_TEXTURE_2D, m_logo_texture.get_id()));
        logo_quad.render();
        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

        glsafe(::glDepthMask(GL_TRUE));
        glsafe(::glDisable(GL_BLEND));

        shader->stop_using();
    }
}

} // namespace GUI
} // namespace Slic3r
