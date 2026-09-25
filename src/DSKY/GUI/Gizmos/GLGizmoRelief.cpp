///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "GLGizmoRelief.hpp"

#include "DSKY/GUI/GUI_App.hpp"
#include "DSKY/GUI/EventTypes.hpp"
#include "DSKY/GUI/EventBridge.hpp"
#include "DSKY/GUI/MainFrame.hpp"
#include "DSKY/GUI/GLCanvas3D.hpp"
#include "DSKY/GUI/ImGuiPureWrap.hpp"
#include "DSKY/GUI/Camera.hpp"
#include "DSKY/GUI/3DScene.hpp"

#include "luminary/presets/bundle/PresetBundle.hpp"
#include "luminary/model/build_volume/BuildVolume.hpp"
#include "luminary/mesh/core/TriangleMesh.hpp"

#include <wx/image.h>
#include <wx/filedlg.h>

#include <cmath>

namespace DSKY
{
using namespace Luminary;

GLGizmoRelief::GLGizmoRelief(GLCanvas3D &parent) : GLGizmoBase(parent, "", -2) {}

bool GLGizmoRelief::on_init()
{
    m_shortcut_key = 0; // Ctrl+K reassigned to CounterboreBridge
    return true;
}

std::string GLGizmoRelief::on_get_name() const
{
    return _u8L("Relief from image");
}

bool GLGizmoRelief::on_is_activable() const
{
    return true;
}

void GLGizmoRelief::on_set_state()
{
    if (get_state() == Off)
    {
        // Restore all volume visibility unconditionally. Index-based restore is
        // fragile because generate_mesh/plater->update may rebuild the volume list.
        if (m_objects_hidden)
        {
            for (GLVolume *v : m_parent.get_volumes().volumes)
                v->is_active = true;
            m_objects_hidden = false;
        }
        m_image.reset();
        m_blurred_pixels.clear();
        m_preview.reset();
        m_preview_its = {};
        m_inverted = false;
        m_smoothing = 2.f;
        m_gamma = 1.f;
        m_min_thickness = 0.f;
        m_solid_base = false;
        m_parent.reload_scene(true);
    }
}

void GLGizmoRelief::on_render()
{
    if (!m_image.has_value())
        return;

    // Hide other objects after image is loaded (safe - render cycle is fully initialized).
    // Skip rendering the preview on the hide frame so objects disappear first.
    if (!m_objects_hidden)
    {
        for (GLVolume *v : m_parent.get_volumes().volumes)
            v->is_active = false;
        m_objects_hidden = true;
        m_parent.set_as_dirty();
        return; // Skip this frame - next frame renders with objects hidden + preview visible
    }

    if (m_preview_dirty)
        update_preview();

    if (m_preview.is_initialized())
    {
        GLShaderProgram *shader = m_parent.get_shader("gouraud_light");
        if (shader == nullptr)
            return;

        shader->start_using();

        const Camera &camera = m_parent.get_camera();

        // Position preview at bed center
        Vec3d bed_center = Vec3d::Zero();
        const DynamicPrintConfig &config = m_parent.preset_bundle()->printers.get_edited_preset().config;
        if (const ConfigOptionPoints *opt = config.option<ConfigOptionPoints>("bed_shape"))
        {
            BoundingBoxf bb;
            for (const Vec2d &pt : opt->values)
                bb.merge(pt);
            bed_center = Vec3d(bb.center().x(), bb.center().y(), 0.0);
        }

        // Center the preview mesh on the bed
        BoundingBoxf3 mesh_bb = m_preview.get_bounding_box();
        Vec3d mesh_center = mesh_bb.center();
        Transform3d trafo = Transform3d::Identity();
        trafo.translate(bed_center - Vec3d(mesh_center.x(), mesh_center.y(), 0.0));

        const Transform3d view_model_matrix = camera.get_view_matrix() * trafo;
        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        const Matrix3d view_normal_matrix = view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);

        // Semi-transparent teal color for preview
        m_preview.set_color({0.2f, 0.7f, 0.7f, 0.8f});

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        glsafe(::glEnable(GL_DEPTH_TEST));

        m_preview.render();

        glsafe(::glDisable(GL_BLEND));

        shader->stop_using();
    }
}

void GLGizmoRelief::on_render_input_window(float x, float y, float bottom_limit)
{
    // Handle deferred load request from menu
    if (m_load_requested)
    {
        m_load_requested = false;
        if (!load_image())
        {
            m_parent.get_gizmos_manager().open_gizmo(GLGizmosManager::Relief);
            return;
        }
    }

    ImGuiPureWrap::begin("Relief from Image###GizmoRelief",
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGuiWrapper &imgui = *m_imgui;

    if (!m_image.has_value())
    {
        ImGuiPureWrap::text("Load a grayscale image to generate a 3D relief mesh.");
        ImGuiPureWrap::text("White = full height, Black = zero height.");
        ImGui::Spacing();

        if (ImGui::Button("Load Image"))
            load_image();

        ImGui::SameLine();

        if (ImGui::Button("Close"))
            set_state(Off);

        ImGuiPureWrap::end();
        return;
    }

    // Image loaded - show controls
    ImGuiPureWrap::text("Image: " + m_image->filename);
    ImGuiPureWrap::text("Pixels: " + std::to_string(m_image->width) + " x " + std::to_string(m_image->height));

    ImGui::Separator();

    // Width
    ImGuiPureWrap::text("Width (mm)");
    ImGui::SameLine();
    float old_width = m_width_mm;
    if (imgui.slider_float("##width", &m_width_mm, 1.0f, 500.0f, "%.1f"))
    {
        if (m_lock_aspect && old_width > 0.f)
            m_height_mm = m_width_mm / m_aspect_ratio;
        m_preview_dirty = true;
    }

    // Height
    ImGuiPureWrap::text("Height (mm)");
    ImGui::SameLine();
    float old_height = m_height_mm;
    if (imgui.slider_float("##height", &m_height_mm, 1.0f, 500.0f, "%.1f"))
    {
        if (m_lock_aspect && old_height > 0.f)
            m_width_mm = m_height_mm * m_aspect_ratio;
        m_preview_dirty = true;
    }

    // Lock aspect ratio
    ImGui::Checkbox("Lock aspect ratio", &m_lock_aspect);

    // Depth
    ImGuiPureWrap::text("Max thickness (mm)");
    ImGui::SameLine();
    if (imgui.slider_float("##depth", &m_depth_mm, 0.1f, 50.0f, "%.1f"))
    {
        if (m_min_thickness > m_depth_mm)
            m_min_thickness = m_depth_mm;
        m_preview_dirty = true;
    }

    // Min thickness
    ImGuiPureWrap::text("Min thickness (mm)");
    ImGui::SameLine();
    if (imgui.slider_float("##min_thick", &m_min_thickness, 0.f, m_depth_mm, "%.2f"))
        m_preview_dirty = true;

    // Smoothing
    ImGuiPureWrap::text("Smoothing");
    ImGui::SameLine();
    if (imgui.slider_float("##smoothing", &m_smoothing, 0.f, 10.f, "%.1f"))
    {
        rebuild_blurred_pixels();
        m_preview_dirty = true;
    }

    // Gamma correction
    ImGuiPureWrap::text("Gamma");
    ImGui::SameLine();
    if (imgui.slider_float("##gamma", &m_gamma, 0.2f, 3.0f, "%.2f"))
        m_preview_dirty = true;

    // Solid base: black pixels get a solid base instead of holes
    if (ImGui::Checkbox("Solid base", &m_solid_base))
        m_preview_dirty = true;

    ImGui::Separator();

    // Flip / Mirror / Invert
    if (ImGui::Button("Flip Horizontal"))
    {
        auto &px = m_image->pixels;
        unsigned int w = m_image->width, h = m_image->height;
        for (unsigned int row = 0; row < h; ++row)
            for (unsigned int col = 0; col < w / 2; ++col)
                std::swap(px[row * w + col], px[row * w + (w - 1 - col)]);
        rebuild_blurred_pixels();
        m_preview_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Flip Vertical"))
    {
        auto &px = m_image->pixels;
        unsigned int w = m_image->width, h = m_image->height;
        for (unsigned int row = 0; row < h / 2; ++row)
            for (unsigned int col = 0; col < w; ++col)
                std::swap(px[row * w + col], px[(h - 1 - row) * w + col]);
        rebuild_blurred_pixels();
        m_preview_dirty = true;
    }
    ImGui::SameLine();
    bool was_inverted = m_inverted;
    if (was_inverted)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.63f, 0.20f, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.63f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    }
    if (ImGui::Button("Invert"))
    {
        m_inverted = !m_inverted;
        m_preview_dirty = true;
    }
    if (was_inverted)
        ImGui::PopStyleColor(3);

    ImGui::Separator();

    // Resolution control - max pixels per dimension
    unsigned int img_max = std::max(m_image->width, m_image->height);
    unsigned int cap = std::min(m_max_dim, img_max); // Can't exceed image resolution
    int cap_int = static_cast<int>(cap);
    ImGuiPureWrap::text("Detail (max pixels)");
    ImGui::SameLine();
    if (ImGui::SliderInt("##detail", &cap_int, 50, static_cast<int>(std::min(img_max, 2000u)), "%d"))
    {
        m_max_dim = static_cast<unsigned int>(cap_int);
        m_preview_dirty = true;
    }

    // Compute effective output dimensions after downsampling
    unsigned int w = m_image->width, h = m_image->height;
    unsigned int step = 1;
    while (w / step > m_max_dim || h / step > m_max_dim)
        ++step;
    unsigned int sw = (w + step - 1) / step;
    unsigned int sh = (h + step - 1) / step;

    unsigned int tri_count = (sw - 1) * (sh - 1) * 2; // relief surface
    tri_count += 2 * (sw + sh);                       // border strips (fans from corners)
    tri_count += 2 + 8;                               // bottom face + 4 side walls
    std::string tri_str;
    if (tri_count > 1000000)
        tri_str = std::to_string(tri_count / 1000000) + "." + std::to_string((tri_count / 100000) % 10) + "M";
    else if (tri_count > 1000)
        tri_str = std::to_string(tri_count / 1000) + "K";
    else
        tri_str = std::to_string(tri_count);
    ImGuiPureWrap::text("Output: " + std::to_string(sw) + " x " + std::to_string(sh) + "  (~" + tri_str +
                        " triangles)");

    ImGui::Separator();

    if (ImGui::Button("Done"))
    {
        generate_mesh();
        ImGuiPureWrap::end();
        m_parent.get_gizmos_manager().open_gizmo(GLGizmosManager::Relief); // Toggle off
        return;
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        ImGuiPureWrap::end();
        m_parent.get_gizmos_manager().open_gizmo(GLGizmosManager::Relief); // Toggle off
        return;
    }

    ImGuiPureWrap::end();
}

bool GLGizmoRelief::load_image()
{
    wxFileDialog dialog(m_parent.get_wxglcanvas_parent(), _L("Select grayscale image"), "", "",
                        "Image files (*.png;*.jpg;*.jpeg;*.bmp;*.tiff;*.tif)"
                        "|*.png;*.jpg;*.jpeg;*.bmp;*.tiff;*.tif",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() != wxID_OK)
        return false;

    wxString path = dialog.GetPath();
    wxImage wx_img;

    wxInitAllImageHandlers();

    if (!wx_img.LoadFile(path))
    {
        wxMessageBox(_L("Failed to load image file."), _L("Error"), wxOK | wxICON_ERROR);
        return false;
    }

    ImageData img;
    img.width = wx_img.GetWidth();
    img.height = wx_img.GetHeight();
    img.filename = dialog.GetFilename().ToStdString();
    img.bit_depth = 8;
    img.pixels.resize(img.width * img.height);

    // Convert to grayscale using ITU-R BT.709 luminance, scaled to 16-bit
    const unsigned char *rgb = wx_img.GetData();
    for (unsigned int i = 0; i < img.width * img.height; ++i)
    {
        unsigned char r = rgb[i * 3];
        unsigned char g = rgb[i * 3 + 1];
        unsigned char b = rgb[i * 3 + 2];
        img.pixels[i] = static_cast<uint16_t>((0.2126 * r + 0.7152 * g + 0.0722 * b) * 257.0);
    }

    m_aspect_ratio = static_cast<float>(img.width) / static_cast<float>(img.height);
    m_height_mm = m_width_mm / m_aspect_ratio;

    m_image = std::move(img);
    rebuild_blurred_pixels();
    m_preview_dirty = true;
    return true;
}

void GLGizmoRelief::rebuild_blurred_pixels()
{
    if (!m_image.has_value())
    {
        m_blurred_pixels.clear();
        return;
    }

    const auto &img = m_image.value();
    unsigned int w = img.width;
    unsigned int h = img.height;

    if (m_smoothing < 0.5f)
    {
        // No meaningful blur - just copy
        m_blurred_pixels = img.pixels;
        return;
    }

    // Gaussian blur via separable horizontal + vertical passes.
    // Build 1D kernel from the blur radius.
    float sigma = m_smoothing;
    int radius = static_cast<int>(std::ceil(sigma * 2.5f));
    std::vector<float> kernel(radius + 1);
    float sum = 0.f;
    for (int i = 0; i <= radius; ++i)
    {
        kernel[i] = std::exp(-0.5f * (static_cast<float>(i * i)) / (sigma * sigma));
        sum += kernel[i] * (i == 0 ? 1.f : 2.f);
    }
    for (int i = 0; i <= radius; ++i)
        kernel[i] /= sum;

    // Horizontal pass: source -> temp
    std::vector<float> temp(w * h);
    for (unsigned int row = 0; row < h; ++row)
    {
        for (unsigned int col = 0; col < w; ++col)
        {
            float val = static_cast<float>(img.pixels[row * w + col]) * kernel[0];
            for (int k = 1; k <= radius; ++k)
            {
                unsigned int l = col >= static_cast<unsigned int>(k) ? col - k : 0;
                unsigned int r = std::min(col + k, w - 1);
                val += (static_cast<float>(img.pixels[row * w + l]) + static_cast<float>(img.pixels[row * w + r])) *
                       kernel[k];
            }
            temp[row * w + col] = val;
        }
    }

    // Vertical pass: temp -> blurred
    m_blurred_pixels.resize(w * h);
    for (unsigned int col = 0; col < w; ++col)
    {
        for (unsigned int row = 0; row < h; ++row)
        {
            float val = temp[row * w + col] * kernel[0];
            for (int k = 1; k <= radius; ++k)
            {
                unsigned int t = row >= static_cast<unsigned int>(k) ? row - k : 0;
                unsigned int b = std::min(row + k, h - 1);
                val += (temp[t * w + col] + temp[b * w + col]) * kernel[k];
            }
            m_blurred_pixels[row * w + col] = static_cast<uint16_t>(std::clamp(val, 0.f, 65535.f));
        }
    }
}

indexed_triangle_set GLGizmoRelief::build_heightmap_its(unsigned int max_dim) const
{
    if (!m_image.has_value() || m_blurred_pixels.empty())
        return {};

    const ImageData &img = m_image.value();
    const auto &pixels = m_blurred_pixels;

    // Downsample if needed
    unsigned int w = img.width;
    unsigned int h = img.height;
    unsigned int step = 1;
    while (w / step > max_dim || h / step > max_dim)
        ++step;
    unsigned int sw = (w + step - 1) / step;
    unsigned int sh = (h + step - 1) / step;

    const float x_scale = m_width_mm / static_cast<float>(sw - 1);
    const float y_scale = m_height_mm / static_cast<float>(sh - 1);
    const float max_pixel = 65535.f;

    // Bilinear interpolation sampler for sub-pixel positions in the source image
    auto sample_bilinear = [&pixels, w, h](float row_f, float col_f) -> float
    {
        col_f = std::clamp(col_f, 0.f, static_cast<float>(w - 1));
        row_f = std::clamp(row_f, 0.f, static_cast<float>(h - 1));
        unsigned int c0 = static_cast<unsigned int>(col_f);
        unsigned int r0 = static_cast<unsigned int>(row_f);
        unsigned int c1 = std::min(c0 + 1, w - 1);
        unsigned int r1 = std::min(r0 + 1, h - 1);
        float fc = col_f - static_cast<float>(c0);
        float fr = row_f - static_cast<float>(r0);
        float v00 = static_cast<float>(pixels[r0 * w + c0]);
        float v10 = static_cast<float>(pixels[r0 * w + c1]);
        float v01 = static_cast<float>(pixels[r1 * w + c0]);
        float v11 = static_cast<float>(pixels[r1 * w + c1]);
        return v00 * (1.f - fc) * (1.f - fr) + v10 * fc * (1.f - fr) + v01 * (1.f - fc) * fr + v11 * fc * fr;
    };

    // Phase 1: Build solid/void mask.
    // All pixels are solid when min_thickness > 0 or solid base is on (always has material).
    // Otherwise black pixels produce no geometry (holes/cutouts).
    bool all_solid = m_solid_base || m_min_thickness > 0.f;
    std::vector<bool> mask(sw * sh, false);
    for (unsigned int row = 0; row < sh; ++row)
    {
        for (unsigned int col = 0; col < sw; ++col)
        {
            if (all_solid)
            {
                mask[row * sw + col] = true;
            }
            else
            {
                float src_row = std::min(static_cast<float>(row * step), static_cast<float>(h - 1));
                float src_col = std::min(static_cast<float>(col * step), static_cast<float>(w - 1));
                mask[row * sw + col] = (sample_bilinear(src_row, src_col) > 0.f);
            }
        }
    }

    // Phase 2: Classify cells - a cell is solid only when ALL four corners are non-black.
    // No mixed solid/void cells - this avoids bowtie vertices that break CGAL booleans.
    unsigned int cw = sw - 1, ch = sh - 1;
    std::vector<bool> cell_solid(cw * ch, false);
    for (unsigned int r = 0; r < ch; ++r)
        for (unsigned int c = 0; c < cw; ++c)
            cell_solid[r * cw + c] = mask[r * sw + c] && mask[r * sw + c + 1] && mask[(r + 1) * sw + c] &&
                                     mask[(r + 1) * sw + c + 1];

    // Phase 3: Build vertex remapping - only emit vertices used by solid cells
    std::vector<unsigned int> vremap(sw * sh, UINT_MAX);
    unsigned int vert_count = 0;
    for (unsigned int r = 0; r < sh; ++r)
    {
        for (unsigned int c = 0; c < sw; ++c)
        {
            bool used = false;
            if (r > 0 && c > 0 && cell_solid[(r - 1) * cw + (c - 1)])
                used = true;
            if (r > 0 && c < cw && cell_solid[(r - 1) * cw + c])
                used = true;
            if (r < ch && c > 0 && cell_solid[r * cw + (c - 1)])
                used = true;
            if (r < ch && c < cw && cell_solid[r * cw + c])
                used = true;
            if (used)
                vremap[r * sw + c] = vert_count++;
        }
    }

    if (vert_count == 0)
        return {};

    // Phase 4: Emit vertices - all vertices are solid (non-black), no void remapping needed
    indexed_triangle_set its;
    its.vertices.reserve(vert_count * 2);

    // Top vertices at heightmap z
    for (unsigned int r = 0; r < sh; ++r)
    {
        for (unsigned int c = 0; c < sw; ++c)
        {
            if (vremap[r * sw + c] == UINT_MAX)
                continue;
            float x = static_cast<float>(c) * x_scale;
            float y = static_cast<float>(sh - 1 - r) * y_scale;
            float src_row = std::min(static_cast<float>(r * step), static_cast<float>(h - 1));
            float src_col = std::min(static_cast<float>(c * step), static_cast<float>(w - 1));
            float normalized = sample_bilinear(src_row, src_col) / max_pixel;
            if (m_inverted)
                normalized = 1.f - normalized;
            if (m_gamma != 1.f && normalized > 0.f)
                normalized = std::pow(normalized, m_gamma);
            // Min thickness extends from the back - relief detail on front is untouched
            float z = m_min_thickness + normalized * m_depth_mm;
            its.vertices.emplace_back(x, y, z);
        }
    }

    // Bottom vertices (same XY, z=0)
    unsigned int base = vert_count;
    for (unsigned int r = 0; r < sh; ++r)
    {
        for (unsigned int c = 0; c < sw; ++c)
        {
            if (vremap[r * sw + c] == UINT_MAX)
                continue;
            float x = static_cast<float>(c) * x_scale;
            float y = static_cast<float>(sh - 1 - r) * y_scale;
            its.vertices.emplace_back(x, y, 0.f);
        }
    }

    // Phase 5: Emit top and bottom face triangles for solid cells
    for (unsigned int r = 0; r < ch; ++r)
    {
        for (unsigned int c = 0; c < cw; ++c)
        {
            if (!cell_solid[r * cw + c])
                continue;

            unsigned int tl = vremap[r * sw + c];
            unsigned int tr = vremap[r * sw + c + 1];
            unsigned int bl = vremap[(r + 1) * sw + c];
            unsigned int br = vremap[(r + 1) * sw + c + 1];

            // Top face (normal +Z)
            its.indices.emplace_back(tl, bl, tr);
            its.indices.emplace_back(tr, bl, br);

            // Bottom face (normal -Z)
            its.indices.emplace_back(base + tl, base + tr, base + bl);
            its.indices.emplace_back(base + tr, base + br, base + bl);
        }
    }

    // Phase 6: Emit boundary walls - clean quads, no degenerate cases possible
    // since all cell corners are solid (non-zero z on top, zero on bottom).
    auto emit_wall = [&](unsigned int r0, unsigned int c0, unsigned int r1, unsigned int c1)
    {
        unsigned int t0 = vremap[r0 * sw + c0];
        unsigned int t1 = vremap[r1 * sw + c1];
        unsigned int b0 = base + t0;
        unsigned int b1 = base + t1;
        its.indices.emplace_back(t0, b0, t1);
        its.indices.emplace_back(t1, b0, b1);
    };

    for (unsigned int r = 0; r < ch; ++r)
    {
        for (unsigned int c = 0; c < cw; ++c)
        {
            if (!cell_solid[r * cw + c])
                continue;

            // Top edge (row r): wall if no solid cell above
            if (r == 0 || !cell_solid[(r - 1) * cw + c])
                emit_wall(r, c + 1, r, c);

            // Bottom edge (row r+1): wall if no solid cell below
            if (r == ch - 1 || !cell_solid[(r + 1) * cw + c])
                emit_wall(r + 1, c, r + 1, c + 1);

            // Left edge (col c): wall if no solid cell to the left
            if (c == 0 || !cell_solid[r * cw + (c - 1)])
                emit_wall(r, c, r + 1, c);

            // Right edge (col c+1): wall if no solid cell to the right
            if (c == cw - 1 || !cell_solid[r * cw + (c + 1)])
                emit_wall(r + 1, c + 1, r, c + 1);
        }
    }

    return its;
}

void GLGizmoRelief::update_preview()
{
    m_preview.reset();
    m_preview_its = {};
    m_preview_dirty = false;

    if (!m_image.has_value())
        return;

    // Build at the user's selected detail level so the preview matches the output
    m_preview_its = build_heightmap_its(m_max_dim);
    if (m_preview_its.vertices.empty())
        return;

    m_preview.init_from(m_preview_its);
}

void GLGizmoRelief::generate_mesh()
{
    if (!m_image.has_value())
        return;

    // Use the cached preview mesh - it's already at the user's selected detail level
    if (m_preview_its.vertices.empty())
        m_preview_its = build_heightmap_its(m_max_dim);
    if (m_preview_its.vertices.empty())
        return;

    TriangleMesh relief_mesh(std::move(m_preview_its));

    Model *model_ptr = m_parent.get_model();
    if (!model_ptr)
        return;

    Model &model = *model_ptr;
    Vec2d bed_center = m_parent.build_volume().bed_center();
    size_t obj_idx = model.objects.size();

    // Build a bounding box for the alignment proxy
    BoundingBoxf3 relief_bb = relief_mesh.bounding_box();
    TriangleMesh box_mesh = make_cube(relief_bb.size().x(), relief_bb.size().y(), relief_bb.size().z());
    box_mesh.translate(static_cast<float>(relief_bb.min.x()), static_cast<float>(relief_bb.min.y()),
                       static_cast<float>(relief_bb.min.z()));

    // Undo returns to the plate as it was before the relief object existed.
    m_parent.take_gizmo_snapshot(_u8L("Add object"));
    ModelObject *obj = model.add_object();
    obj->name = "[Relief] " + m_image->filename;

    ModelVolume *relief_vol = obj->add_volume(std::move(relief_mesh));
    relief_vol->name = "Relief";

    // Store a low-res version for CSG preview (CGAL can't handle 100K+ triangles)
    indexed_triangle_set preview_its = build_heightmap_its(100);
    if (!preview_its.vertices.empty())
        relief_vol->preview_its = std::make_shared<const indexed_triangle_set>(std::move(preview_its));

    // Alignment proxy: semi-transparent modifier for face picking by the Align gizmo
    ModelVolume *box_vol = obj->add_volume(std::move(box_mesh), ModelVolumeType::PARAMETER_MODIFIER);
    box_vol->name = "Alignment Box";

    obj->center_around_origin();
    ModelInstance *instance = obj->add_instance();
    instance->set_offset(Luminary::to_3d(bed_center, -obj->origin_translation(2)));
    obj->ensure_on_bed();

    m_parent.event_poster()->postEvent(CanvasEventType::ObjectListAddObject, int(obj_idx));
    m_parent.event_poster()->postEvent(CanvasEventType::PlaterUpdate);
}

} // namespace DSKY
