///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "SegmentTemplate.hpp"
#include "OpenGLUtils.hpp"

#include <cstdint>
#include <array>

namespace libvgcode
{

//|     /1-------6\     |
//|    / |       | \    |
//|   2--0-------5--7   |
//|    \ |       | /    |
//|      3-------4      |
// clang-format off
static constexpr const std::array<uint8_t, 36> VERTEX_DATA = {
    0, 1, 2, // front spike
    0, 2, 3, // front spike
    0, 3, 4, // right/bottom body
    0, 4, 5, // right/bottom body
    0, 5, 6, // left/top body
    0, 6, 1, // left/top body
    5, 4, 7, // back spike
    5, 7, 6, // back spike
    // Cap triangles for clip plane cross-section fill
    // Full diamond: right(8/11), up(9/12), down(10/13), left(14/15)
    8, 9, 14,   // front cap upper half (right, up, left)
    8, 14, 10,  // front cap lower half (right, left, down)
    11, 12, 15, // back cap upper half (right, up, left)
    11, 15, 13, // back cap lower half (right, left, down)
};
// clang-format on

// The body's triangles lead VERTEX_DATA, the cap triangles follow them
static constexpr const size_t BODY_VERTEX_DATA_COUNT = 24;

// The 16 vertex ids, each stored once: VERTEX_DATA becomes the element list of an indexed draw, which gives every
// triangle corner the same vertex id as the array draw and lets the GPU reuse the shaded vertices.
static constexpr const std::array<uint8_t, 16> VERTEX_IDS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

void SegmentTemplate::init()
{
    if (m_vao_id != 0)
        return;

    int curr_vertex_array;
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));
    int curr_array_buffer;
    glsafe(glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &curr_array_buffer));

    glsafe(glGenVertexArrays(1, &m_vao_id));
    glsafe(glBindVertexArray(m_vao_id));

    glsafe(glGenBuffers(1, &m_vbo_id));
    glsafe(glBindBuffer(GL_ARRAY_BUFFER, m_vbo_id));
    m_size_in_bytes_gpu += VERTEX_IDS.size() * sizeof(uint8_t) + VERTEX_DATA.size() * sizeof(uint8_t);
    glsafe(glBufferData(GL_ARRAY_BUFFER, VERTEX_IDS.size() * sizeof(uint8_t), VERTEX_IDS.data(), GL_STATIC_DRAW));
    glsafe(glEnableVertexAttribArray(0));
    glsafe(glVertexAttribIPointer(0, 1, GL_UNSIGNED_BYTE, 0, (const void *) 0));
    // The element buffer binding is state of the bound vertex array: it stays with the template's vertex array, and
    // restoring the previous vertex array below restores that one's own element buffer
    glsafe(glGenBuffers(1, &m_ibo_id));
    glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo_id));
    glsafe(glBufferData(GL_ELEMENT_ARRAY_BUFFER, VERTEX_DATA.size() * sizeof(uint8_t), VERTEX_DATA.data(),
                        GL_STATIC_DRAW));

    glsafe(glBindBuffer(GL_ARRAY_BUFFER, curr_array_buffer));
    glsafe(glBindVertexArray(curr_vertex_array));
}

void SegmentTemplate::shutdown()
{
    if (m_ibo_id != 0)
    {
        glsafe(glDeleteBuffers(1, &m_ibo_id));
        m_ibo_id = 0;
    }
    if (m_vbo_id != 0)
    {
        glsafe(glDeleteBuffers(1, &m_vbo_id));
        m_vbo_id = 0;
    }
    if (m_vao_id != 0)
    {
        glsafe(glDeleteVertexArrays(1, &m_vao_id));
        m_vao_id = 0;
    }

    m_size_in_bytes_gpu = 0;
}

void SegmentTemplate::render(size_t count, bool with_caps)
{
    if (m_vao_id == 0 || m_vbo_id == 0 || count == 0)
        return;
    if (m_ibo_id == 0)
        return;

    int curr_vertex_array;
    glsafe(glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &curr_vertex_array));

    const GLsizei vertices_count = static_cast<GLsizei>(with_caps ? VERTEX_DATA.size() : BODY_VERTEX_DATA_COUNT);
    glsafe(glBindVertexArray(m_vao_id));
    glsafe(glDrawElementsInstanced(GL_TRIANGLES, vertices_count, GL_UNSIGNED_BYTE, (const void *) 0,
                                   static_cast<GLsizei>(count)));
    glsafe(glBindVertexArray(curr_vertex_array));
}

} // namespace libvgcode
