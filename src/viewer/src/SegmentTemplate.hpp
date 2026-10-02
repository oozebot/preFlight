///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include <cstddef>

namespace libvgcode
{

class SegmentTemplate
{
public:
    SegmentTemplate() = default;
    ~SegmentTemplate() { shutdown(); }
    SegmentTemplate(const SegmentTemplate &other) = delete;
    SegmentTemplate(SegmentTemplate &&other) = delete;
    SegmentTemplate &operator=(const SegmentTemplate &other) = delete;
    SegmentTemplate &operator=(SegmentTemplate &&other) = delete;

    //
    // Initialize gpu buffers.
    //
    void init();
    //
    // Release gpu buffers.
    //
    void shutdown();
    //
    // Draw count instances. Without caps only the body's triangles are drawn: the cap triangles fill the
    // cross-section of a clipping plane and are degenerate while no plane can clip.
    //
    void render(size_t count, bool with_caps);

    //
    // Return the size of the data sent to gpu, in bytes.
    //
    size_t size_in_bytes_gpu() const { return m_size_in_bytes_gpu; }

private:
    //
    // gpu buffers ids.
    //
    unsigned int m_vao_id{0};
    unsigned int m_vbo_id{0};
    // Element buffer of the indexed draw, bound in the vertex array
    unsigned int m_ibo_id{0};
    //
    // Size of the data sent to gpu, in bytes.
    //
    size_t m_size_in_bytes_gpu{0};
};

} // namespace libvgcode
