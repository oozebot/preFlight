///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "GLGizmoPainterBase.hpp"

namespace DSKY
{
using namespace Luminary;

// VBO holder for the multi-color triangle render. Holds one vertex buffer plus an index
// buffer per color slot. Used by the ColorMixing gizmo and the MMU-paint preview path.
class GLMmSegmentationGizmo3DScene
{
public:
    GLMmSegmentationGizmo3DScene() = delete;

    explicit GLMmSegmentationGizmo3DScene(size_t triangle_indices_buffers_count)
    {
        this->triangle_indices = std::vector<std::vector<int>>(triangle_indices_buffers_count);
        this->triangle_indices_sizes = std::vector<size_t>(triangle_indices_buffers_count);
        this->triangle_indices_VBO_ids = std::vector<unsigned int>(triangle_indices_buffers_count);
    }

    virtual ~GLMmSegmentationGizmo3DScene() { release_geometry(); }

    [[nodiscard]] inline bool has_VBOs(size_t triangle_indices_idx) const
    {
        assert(triangle_indices_idx < this->triangle_indices.size());
        return this->triangle_indices_VBO_ids[triangle_indices_idx] != 0;
    }

    // Release the geometry data, release OpenGL VBOs.
    void release_geometry();
    // Finalize the initialization of the geometry, upload the geometry to OpenGL VBO objects
    // and possibly releasing it if it has been loaded into the VBOs.
    void finalize_vertices();
    // Finalize the initialization of the indices, upload the indices to OpenGL VBO objects
    // and possibly releasing it if it has been loaded into the VBOs.
    void finalize_triangle_indices();

    void clear()
    {
        this->vertices.clear();
        for (std::vector<int> &ti : this->triangle_indices)
            ti.clear();

        for (size_t &triangle_indices_size : this->triangle_indices_sizes)
            triangle_indices_size = 0;
    }

    void render(size_t triangle_indices_idx, GLShaderProgram *shader) const;

    std::vector<float> vertices;
    std::vector<std::vector<int>> triangle_indices;

    // When the triangle indices are loaded into the graphics card as Vertex Buffer Objects,
    // the above mentioned std::vectors are cleared and the following variables keep their original length.
    std::vector<size_t> triangle_indices_sizes;

    // IDs of the Vertex Array Objects, into which the geometry has been loaded.
    // Zero if the VBOs are not sent to GPU yet.
    unsigned int vertices_VAO_id{0};
    unsigned int vertices_VBO_id{0};
    std::vector<unsigned int> triangle_indices_VBO_ids;
};

// GUI-side triangle selector that renders each painted state with a different color.
// Used by the ColorMixing gizmo and by the canvas/preview render path.
class TriangleSelectorMmGui : public TriangleSelectorGUI
{
public:
    TriangleSelectorMmGui() = delete;
    // Plus 1 in the initialization of m_gizmo_scene is because the first position is allocated
    // for non-painted triangles, and the indices above colors.size() are allocated for seed fill.
    explicit TriangleSelectorMmGui(const TriangleMesh &mesh, const std::vector<ColorRGBA> &colors,
                                   const ColorRGBA &default_volume_color)
        : TriangleSelectorGUI(mesh)
        , m_colors(colors)
        , m_default_volume_color(default_volume_color)
        , m_gizmo_scene(2 * (colors.size() + 1))
    {
    }

    ~TriangleSelectorMmGui() override = default;

    void render(ImGuiWrapper *imgui, const Transform3d &matrix, const Camera &camera) override;

private:
    void update_render_data();

    const std::vector<ColorRGBA> &m_colors;
    const ColorRGBA m_default_volume_color;
    GLMmSegmentationGizmo3DScene m_gizmo_scene;
};

} // namespace DSKY

