///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, David Kocík @kocikdav, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "TriangleSelectorMmGui.hpp"

#include "DSKY/GUI/3DScene.hpp" // for the glsafe() macro
#include "DSKY/GUI/OpenGLManager.hpp"

#if PREFLIGHT_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

namespace DSKY
{
using namespace Luminary;

void TriangleSelectorMmGui::render(ImGuiWrapper *imgui, const Transform3d &matrix, const Camera &camera)
{
    if (m_update_render_data)
        update_render_data();

    auto *shader = m_get_current_shader ? m_get_current_shader() : nullptr;
    if (!shader)
        return;

    assert(shader->get_name() == "mm_gouraud" || shader->get_name() == "mm_color_preview");

    for (size_t color_idx = 0; color_idx < m_gizmo_scene.triangle_indices.size(); ++color_idx)
    {
        if (m_gizmo_scene.has_VBOs(color_idx))
        {
            if (color_idx > m_colors.size()) // Seed fill VBO
                shader->set_uniform("uniform_color", TriangleSelectorGUI::get_seed_fill_color(
                                                         color_idx == (m_colors.size() + 1)
                                                             ? m_default_volume_color
                                                             : m_colors[color_idx - (m_colors.size() + 1) - 1]));
            else // Normal VBO
                shader->set_uniform("uniform_color", color_idx == 0 ? m_default_volume_color : m_colors[color_idx - 1]);

            m_gizmo_scene.render(color_idx, shader);
        }
    }

    render_paint_contour(matrix, camera);
    m_update_render_data = false;
}

void TriangleSelectorMmGui::update_render_data()
{
    m_gizmo_scene.release_geometry();
    m_vertices.reserve(m_vertices.size() * 3);
    for (const Vertex &vr : m_vertices)
    {
        m_gizmo_scene.vertices.emplace_back(vr.v.x());
        m_gizmo_scene.vertices.emplace_back(vr.v.y());
        m_gizmo_scene.vertices.emplace_back(vr.v.z());
    }
    m_gizmo_scene.finalize_vertices();

    for (const Triangle &tr : m_triangles)
        if (tr.valid() && !tr.is_split())
        {
            int color = int(tr.get_state()) <= int(m_colors.size()) ? int(tr.get_state()) : 0;
            assert(m_colors.size() + 1 + color < m_gizmo_scene.triangle_indices.size());
            std::vector<int> &iva =
                m_gizmo_scene.triangle_indices[color + tr.is_selected_by_seed_fill() * (m_colors.size() + 1)];

            if (iva.size() + 3 > iva.capacity())
                iva.reserve(next_highest_power_of_2(iva.size() + 3));

            iva.emplace_back(tr.verts_idxs[0]);
            iva.emplace_back(tr.verts_idxs[1]);
            iva.emplace_back(tr.verts_idxs[2]);
        }

    for (size_t color_idx = 0; color_idx < m_gizmo_scene.triangle_indices.size(); ++color_idx)
        m_gizmo_scene.triangle_indices_sizes[color_idx] = m_gizmo_scene.triangle_indices[color_idx].size();

    m_gizmo_scene.finalize_triangle_indices();
    update_paint_contour();
}

void GLMmSegmentationGizmo3DScene::release_geometry()
{
    if (this->vertices_VBO_id)
    {
        glsafe(::glDeleteBuffers(1, &this->vertices_VBO_id));
        this->vertices_VBO_id = 0;
    }
    for (auto &triangle_indices_VBO_id : triangle_indices_VBO_ids)
    {
        glsafe(::glDeleteBuffers(1, &triangle_indices_VBO_id));
        triangle_indices_VBO_id = 0;
    }
#if !PREFLIGHT_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile())
    {
#endif // !PREFLIGHT_OPENGL_ES
        if (this->vertices_VAO_id > 0)
        {
            glsafe(::glDeleteVertexArrays(1, &this->vertices_VAO_id));
            this->vertices_VAO_id = 0;
        }
#if !PREFLIGHT_OPENGL_ES
    }
#endif // !PREFLIGHT_OPENGL_ES

    this->clear();
}

void GLMmSegmentationGizmo3DScene::render(size_t triangle_indices_idx, GLShaderProgram *shader) const
{
    assert(triangle_indices_idx < this->triangle_indices_VBO_ids.size());
    assert(this->triangle_indices_sizes.size() == this->triangle_indices_VBO_ids.size());
#if !PREFLIGHT_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile())
    {
#endif // !PREFLIGHT_OPENGL_ES
        assert(this->vertices_VAO_id != 0);
#if !PREFLIGHT_OPENGL_ES
    }
#endif // !PREFLIGHT_OPENGL_ES
    assert(this->vertices_VBO_id != 0);
    assert(this->triangle_indices_VBO_ids[triangle_indices_idx] != 0);

    if (shader == nullptr)
        return;

#if !PREFLIGHT_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile())
    {
#endif // !PREFLIGHT_OPENGL_ES
        glsafe(::glBindVertexArray(this->vertices_VAO_id));
#if !PREFLIGHT_OPENGL_ES
    }
#endif // !PREFLIGHT_OPENGL_ES
    // the following binding is needed to set the vertex attributes
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_VBO_id));
    const GLint position_id = shader->get_attrib_location("v_position");
    if (position_id != -1)
    {
        glsafe(::glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (GLvoid *) nullptr));
        glsafe(::glEnableVertexAttribArray(position_id));
    }

    // Render using the Vertex Buffer Objects.
    if (this->triangle_indices_VBO_ids[triangle_indices_idx] != 0 &&
        this->triangle_indices_sizes[triangle_indices_idx] > 0)
    {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_ids[triangle_indices_idx]));
        glsafe(::glDrawElements(GL_TRIANGLES, GLsizei(this->triangle_indices_sizes[triangle_indices_idx]),
                                GL_UNSIGNED_INT, nullptr));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    if (position_id != -1)
        glsafe(::glDisableVertexAttribArray(position_id));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
#if !PREFLIGHT_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile())
    {
#endif // !PREFLIGHT_OPENGL_ES
        glsafe(::glBindVertexArray(0));
#if !PREFLIGHT_OPENGL_ES
    }
#endif // !PREFLIGHT_OPENGL_ES
}

void GLMmSegmentationGizmo3DScene::finalize_vertices()
{
#if !PREFLIGHT_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile())
    {
#endif // !PREFLIGHT_OPENGL_ES
        assert(this->vertices_VAO_id == 0);
#if !PREFLIGHT_OPENGL_ES
    }
#endif // !PREFLIGHT_OPENGL_ES
    assert(this->vertices_VBO_id == 0);
    if (!this->vertices.empty())
    {
#if !PREFLIGHT_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile())
        {
#endif // !PREFLIGHT_OPENGL_ES
            glsafe(::glGenVertexArrays(1, &this->vertices_VAO_id));
            glsafe(::glBindVertexArray(this->vertices_VAO_id));
#if !PREFLIGHT_OPENGL_ES
        }
#endif // !PREFLIGHT_OPENGL_ES

        glsafe(::glGenBuffers(1, &this->vertices_VBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_VBO_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, this->vertices.size() * sizeof(float), this->vertices.data(),
                              GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->vertices.clear();

#if !PREFLIGHT_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile())
        {
#endif // !PREFLIGHT_OPENGL_ES
            glsafe(::glBindVertexArray(0));
#if !PREFLIGHT_OPENGL_ES
        }
#endif // !PREFLIGHT_OPENGL_ES
    }
}

void GLMmSegmentationGizmo3DScene::finalize_triangle_indices()
{
    assert(std::all_of(triangle_indices_VBO_ids.cbegin(), triangle_indices_VBO_ids.cend(),
                       [](const auto &ti_VBO_id) { return ti_VBO_id == 0; }));

    assert(this->triangle_indices.size() == this->triangle_indices_VBO_ids.size());
    for (size_t buffer_idx = 0; buffer_idx < this->triangle_indices.size(); ++buffer_idx)
    {
        if (!this->triangle_indices[buffer_idx].empty())
        {
            glsafe(::glGenBuffers(1, &this->triangle_indices_VBO_ids[buffer_idx]));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_ids[buffer_idx]));
            glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices[buffer_idx].size() * sizeof(int),
                                  this->triangle_indices[buffer_idx].data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            this->triangle_indices[buffer_idx].clear();
        }
    }
}

} // namespace DSKY
