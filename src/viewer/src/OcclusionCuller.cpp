///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "OcclusionCuller.hpp"
#include "OpenGLUtils.hpp"
#include "Shaders.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <new>
#include <utility>

namespace libvgcode
{

namespace
{
const std::vector<uint8_t> NO_SUBCELLS;
} // namespace

const std::vector<uint8_t> &OcclusionCuller::visible_subcells(size_t view) const
{
    return view < OCCLUSION_VIEWS ? m_views[view].predicted : NO_SUBCELLS;
}

namespace
{

// The result targets' width: point i writes texel (i % width, i / width)
constexpr int OCCLUSION_RESULT_WIDTH = 1024;
// The texture units of the culler's textures, past the segment data (0 to 3), the scene-pass maps (4, 5) and the
// visibility probe's id unit (6): the depth target or the pyramid, the chunk results, the flags
constexpr int OCCLUSION_PYRAMID_UNIT = 8;
constexpr int OCCLUSION_CHUNK_RESULT_UNIT = 9;
constexpr int OCCLUSION_FLAGS_UNIT = 10;
constexpr int OCCLUSION_UNITS[3] = {OCCLUSION_PYRAMID_UNIT, OCCLUSION_CHUNK_RESULT_UNIT, OCCLUSION_FLAGS_UNIT};
// Texels per axis the faces test reads: chunk boxes, sub-cell boxes (the shader reads at most 8)
constexpr int OCCLUSION_CHUNK_TAPS = 4;
constexpr int OCCLUSION_SUBCELL_TAPS = 8;
static_assert(OCCLUSION_CHUNK_TAPS <= 8 && OCCLUSION_SUBCELL_TAPS <= 8, "the test shader reads at most 8 per axis");
// Refinement: a step stops when the sub-cells that passed and were neither predicted nor drawn in a slab hold at most
// the larger of this many enabled segments and the segments of the predicted set (whole, whatever the occluder cap
// drew of it) and of the slabs (a round costs about what shading that many does). Otherwise their slab nearest to the
// camera, OCCLUSION_FIRST_SLAB_SEGMENTS doubling each round, is drawn on top and the test runs again, at most
// OCCLUSION_MAX_ROUNDS tests a step.
constexpr size_t OCCLUSION_NEW_SEGMENTS_BUDGET = 1000000;
constexpr size_t OCCLUSION_FIRST_SLAB_SEGMENTS = 500000;
constexpr size_t OCCLUSION_MAX_ROUNDS = 8;
// The GPU test's own margins, wider than the CPU rule's so that its rounding (division, fused multiply-adds, the
// conversion of the 24-bit depth it samples) never makes it stricter than that rule; each only lets more boxes pass.
// The depth tolerance, also below the near plane and past the far plane (safe range 2 to 16 times the CPU's); the
// edge-on limit, as dropping a face only lowers the bound (safe range 1.5 to 8 times); the eye-plane limit; the
// window rectangle widened by this many pixels on every side, so a footprint at a texel edge takes the texels on both
// sides (safe range 1/256 to 1/4).
constexpr float OCCLUSION_GPU_DEPTH_TOLERANCE = 4.0f * OCCLUSION_DEPTH_TOLERANCE;
constexpr float OCCLUSION_GPU_FACE_MIN_AREA_FRACTION = 2.0f * OCCLUSION_FACE_MIN_AREA_FRACTION;
constexpr float OCCLUSION_GPU_FACE_MIN_AREA = 2.0f * OCCLUSION_FACE_MIN_AREA;
constexpr float OCCLUSION_GPU_MIN_CLIP_W = 2.0f * OCCLUSION_MIN_CLIP_W;
constexpr float OCCLUSION_GPU_WINDOW_MARGIN = 1.0f / 32.0f;

// Clears errors left by earlier work, so a check after a part of the step sees only its own
void drain_errors()
{
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i)
    {
    }
}

// The first error raised since the last drain (GL_NO_ERROR when none), draining the rest
GLenum take_error()
{
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
        drain_errors();
    return error;
}

// The rows a result target needs for `count` points
int result_rows(size_t count)
{
    return int((count + OCCLUSION_RESULT_WIDTH - 1) / OCCLUSION_RESULT_WIDTH);
}

// The store of a segment list of `bytes`: room to grow, within the largest texture buffer, so most writes keep its size
size_t list_capacity(size_t bytes, size_t max_texels)
{
    const size_t limit = max_texels * sizeof(uint32_t);
    return std::max<size_t>(std::min(bytes + bytes / 2, std::max(bytes, limit)), 4096);
}

// Whether the chunks' sub-cell ranges follow one another over every sub-cell, each sub-cell naming its chunk: a list
// written chunk by chunk from these ranges then holds each sub-cell it takes exactly once
bool chunk_ranges_cover(const PrintChunks &chunks)
{
    const size_t subcells = chunks.subcells();
    const size_t count = chunks.set.chunks.size();
    if (chunks.chunk.size() != subcells)
        return false;
    size_t next = 0;
    for (size_t c = 0; c < count; ++c)
    {
        const ViewChunk &chunk = chunks.set.chunks[c];
        const size_t end = next + chunk.subcell_count;
        if (chunk.first_subcell != next || end > subcells)
            return false;
        for (size_t s = next; s < end; ++s)
            if (chunks.chunk[s] != c)
                return false;
        next = end;
    }
    return next == subcells;
}

void delete_buffer(unsigned int &id)
{
    if (id != 0)
        glDeleteBuffers(1, &id);
    id = 0;
}

void delete_texture(unsigned int &id)
{
    if (id != 0)
        glDeleteTextures(1, &id);
    id = 0;
}

void delete_framebuffer(unsigned int &id)
{
    if (id != 0)
        glDeleteFramebuffers(1, &id);
    id = 0;
}

void delete_vertex_array(unsigned int &id)
{
    if (id != 0)
        glDeleteVertexArrays(1, &id);
    id = 0;
}

void delete_program(unsigned int &id)
{
    if (id != 0)
        glDeleteProgram(id);
    id = 0;
}

// Every GL state a step touches, saved when made and restored when destroyed, a throw included
class StateGuard
{
public:
    StateGuard()
    {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_read_fbo);
        glGetIntegerv(GL_VIEWPORT, m_viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &m_active_texture);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_vertex_array);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_array_buffer);
        glGetIntegerv(GL_TEXTURE_BUFFER_BINDING, &m_texture_buffer);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &m_pack_buffer);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &m_unpack_buffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &m_pack_alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &m_pack_row_length);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &m_pack_skip_pixels);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &m_pack_skip_rows);
        glGetIntegerv(GL_DEPTH_FUNC, &m_depth_func);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &m_depth_mask);
        glGetBooleanv(GL_COLOR_WRITEMASK, m_color_mask);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &m_offset_factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &m_offset_units);
        glGetFloatv(GL_POINT_SIZE, &m_point_size);
        for (size_t i = 0; i < CAPS_COUNT; ++i)
            m_caps[i] = glIsEnabled(CAPS[i]);
        for (size_t u = 0; u < 3; ++u)
        {
            glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_UNITS[u]));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_unit_2d[u]);
            glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &m_unit_buffer[u]);
        }
        glActiveTexture(GLenum(m_active_texture));
    }
    ~StateGuard()
    {
        for (size_t u = 0; u < 3; ++u)
        {
            glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_UNITS[u]));
            glBindTexture(GL_TEXTURE_2D, GLuint(m_unit_2d[u]));
            glBindTexture(GL_TEXTURE_BUFFER, GLuint(m_unit_buffer[u]));
        }
        glActiveTexture(GLenum(m_active_texture));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(m_draw_fbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(m_read_fbo));
        glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
        glUseProgram(GLuint(m_program));
        glBindVertexArray(GLuint(m_vertex_array));
        glBindBuffer(GL_ARRAY_BUFFER, GLuint(m_array_buffer));
        glBindBuffer(GL_TEXTURE_BUFFER, GLuint(m_texture_buffer));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(m_pack_buffer));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(m_unpack_buffer));
        glPixelStorei(GL_PACK_ALIGNMENT, m_pack_alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, m_pack_row_length);
        glPixelStorei(GL_PACK_SKIP_PIXELS, m_pack_skip_pixels);
        glPixelStorei(GL_PACK_SKIP_ROWS, m_pack_skip_rows);
        glDepthFunc(GLenum(m_depth_func));
        glDepthMask(m_depth_mask);
        glColorMask(m_color_mask[0], m_color_mask[1], m_color_mask[2], m_color_mask[3]);
        glPolygonOffset(m_offset_factor, m_offset_units);
        glPointSize(m_point_size);
        for (size_t i = 0; i < CAPS_COUNT; ++i)
        {
            if (m_caps[i])
                glEnable(CAPS[i]);
            else
                glDisable(CAPS[i]);
        }
    }
    StateGuard(const StateGuard &) = delete;
    StateGuard &operator=(const StateGuard &) = delete;

private:
    static constexpr size_t CAPS_COUNT = 9;
    static constexpr GLenum CAPS[CAPS_COUNT] = {GL_DEPTH_TEST, GL_SCISSOR_TEST,       GL_BLEND,
                                                GL_CULL_FACE,  GL_CLIP_DISTANCE0,     GL_POLYGON_OFFSET_FILL,
                                                GL_DITHER,     GL_RASTERIZER_DISCARD, GL_COLOR_LOGIC_OP};
    GLint m_draw_fbo{0};
    GLint m_read_fbo{0};
    GLint m_viewport[4]{0, 0, 0, 0};
    GLint m_program{0};
    GLint m_active_texture{GL_TEXTURE0};
    GLint m_vertex_array{0};
    GLint m_array_buffer{0};
    GLint m_texture_buffer{0};
    GLint m_pack_buffer{0};
    GLint m_unpack_buffer{0};
    GLint m_pack_alignment{4};
    GLint m_pack_row_length{0};
    GLint m_pack_skip_pixels{0};
    GLint m_pack_skip_rows{0};
    GLint m_depth_func{GL_LESS};
    GLboolean m_depth_mask{GL_TRUE};
    GLboolean m_color_mask[4]{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLfloat m_offset_factor{0.0f};
    GLfloat m_offset_units{0.0f};
    GLfloat m_point_size{1.0f};
    GLboolean m_caps[CAPS_COUNT]{};
    GLint m_unit_2d[3]{0, 0, 0};
    GLint m_unit_buffer[3]{0, 0, 0};
};

// The state the culler's passes share: no scissor, blending, culling, clipping, offset, dithering, discard or logic
// op; points of one pixel
void set_common_state()
{
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_CLIP_DISTANCE0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DITHER);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_COLOR_LOGIC_OP);
    glPointSize(1.0f);
}

// The shader of one stage, 0 with the compiler output in `log` when it failed
GLuint compile_shader(GLenum type, const char *source, const char *name, std::string &log)
{
    const GLuint shader = glCreateShader(type);
    if (shader == 0)
    {
        log = std::string("Occlusion culling: unable to create a shader of ") + name + "\n";
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string info(size_t(std::max(length, 1)), '\0');
        glGetShaderInfoLog(shader, length, nullptr, info.data());
        log = std::string("Occlusion culling: unable to compile ") +
              (type == GL_VERTEX_SHADER ? "the vertex shader of " : "the fragment shader of ") + name + ":\n" + info;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// A program with the given attributes at locations 0, 1, ... and the fragment output `result` at 0; 0 with the
// compiler or linker output in `log` when it failed
GLuint link_program(const char *name, const char *vertex, const char *fragment,
                    std::initializer_list<const char *> attributes, std::string &log)
{
    const GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex, name, log);
    if (vs == 0)
        return 0;
    const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment, name, log);
    if (fs == 0)
    {
        glDeleteShader(vs);
        return 0;
    }
    const GLuint program = glCreateProgram();
    if (program == 0)
    {
        glDeleteShader(vs);
        glDeleteShader(fs);
        log = std::string("Occlusion culling: unable to create the program ") + name + "\n";
        return 0;
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    GLuint location = 0;
    for (const char *attribute : attributes)
        glBindAttribLocation(program, location++, attribute);
    glBindFragDataLocation(program, 0, "result");
    glLinkProgram(program);
    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (status != GL_TRUE)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string info(size_t(std::max(length, 1)), '\0');
        glGetProgramInfoLog(program, length, nullptr, info.data());
        log = std::string("Occlusion culling: unable to link the program ") + name + ":\n" + info;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace

bool OcclusionCuller::build_programs()
{
    if (m_test_program != 0)
        return true;
    if (m_programs_failed)
        return false;
    std::string log;
    m_copy_program = link_program("occlusion_copy", Occlusion_Fullscreen_Vertex_Shader, Occlusion_Copy_Fragment_Shader,
                                  {"corner"}, log);
    if (m_copy_program != 0)
        m_reduce_program = link_program("occlusion_reduce", Occlusion_Fullscreen_Vertex_Shader,
                                        Occlusion_Reduce_Fragment_Shader, {"corner"}, log);
    if (m_reduce_program != 0)
        m_test_program = link_program("occlusion_test", Occlusion_Test_Vertex_Shader, Occlusion_Test_Fragment_Shader,
                                      {"box_min", "box_max", "chunk_index"}, log);
    drain_errors();
    if (m_test_program == 0)
    {
        delete_program(m_copy_program);
        delete_program(m_reduce_program);
        m_programs_failed = true;
        m_program_log = log.empty() ? std::string("Occlusion culling: unable to build its programs\n") : log;
        return false;
    }
    // The merge program: without it the culler works and merge_depth() refuses
    m_merge_program = link_program("occlusion_merge", Occlusion_Fullscreen_Vertex_Shader,
                                   Occlusion_Merge_Fragment_Shader, {"corner"}, log);
    drain_errors();
    if (m_merge_program == 0)
        m_program_log = log.empty() ? std::string("Occlusion culling: unable to build the merge program\n") : log;
    else
    {
        m_uni_merge_depth_tex = glGetUniformLocation(m_merge_program, "depth_tex");
        m_uni_merge_viewport_origin = glGetUniformLocation(m_merge_program, "viewport_origin");
    }
    m_uni_copy_depth_tex = glGetUniformLocation(m_copy_program, "depth_tex");
    m_uni_reduce_source_tex = glGetUniformLocation(m_reduce_program, "source_tex");
    m_uni_reduce_source_size = glGetUniformLocation(m_reduce_program, "source_size");
    m_uni_reduce_target_size = glGetUniformLocation(m_reduce_program, "target_size");
    const GLuint p = m_test_program;
    m_uni.view_proj = glGetUniformLocation(p, "view_proj");
    m_uni.viewport_size = glGetUniformLocation(p, "viewport_size");
    m_uni.viewport_texels = glGetUniformLocation(p, "viewport_texels");
    m_uni.orientation = glGetUniformLocation(p, "orientation");
    m_uni.last_level = glGetUniformLocation(p, "last_level");
    m_uni.taps = glGetUniformLocation(p, "taps");
    m_uni.stage = glGetUniformLocation(p, "stage");
    m_uni.result_width = glGetUniformLocation(p, "result_width");
    m_uni.result_size = glGetUniformLocation(p, "result_size");
    m_uni.chunk_flag_offset = glGetUniformLocation(p, "chunk_flag_offset");
    m_uni.min_clip_w = glGetUniformLocation(p, "min_clip_w");
    m_uni.depth_tolerance = glGetUniformLocation(p, "depth_tolerance");
    m_uni.face_min_area_fraction = glGetUniformLocation(p, "face_min_area_fraction");
    m_uni.face_min_area = glGetUniformLocation(p, "face_min_area");
    m_uni.window_margin = glGetUniformLocation(p, "window_margin");
    m_uni.unbounded = glGetUniformLocation(p, "unbounded");
    m_uni.box_faces = glGetUniformLocation(p, "box_faces");
    m_uni.pyramid_tex = glGetUniformLocation(p, "pyramid_tex");
    m_uni.chunk_result_tex = glGetUniformLocation(p, "chunk_result_tex");
    m_uni.flags_tex = glGetUniformLocation(p, "flags_tex");
    drain_errors();
    return true;
}

bool OcclusionCuller::upload_static(const OcclusionInputs &inputs, std::string &reason)
{
    if (m_static_valid && m_static_structure == inputs.structure_generation &&
        m_static_boxes == inputs.boxes_generation)
        return true;
    m_static_valid = false;
    const PrintChunks &chunks = *inputs.chunks;
    const size_t subcells = chunks.subcells();
    const size_t count = chunks.set.chunks.size();
    // The draw set is written chunk by chunk from the chunks' sub-cell ranges, so each sub-cell that passes is in
    // exactly one of them
    if (chunks.box.size() != 6 * subcells || chunks.chunk_box.size() != 6 * count || !chunk_ranges_cover(chunks))
    {
        reason = "no structure";
        return false;
    }
    if (m_subcell_box_buf == 0)
        glGenBuffers(1, &m_subcell_box_buf);
    if (m_subcell_chunk_buf == 0)
        glGenBuffers(1, &m_subcell_chunk_buf);
    if (m_chunk_box_buf == 0)
        glGenBuffers(1, &m_chunk_box_buf);
    if (m_fullscreen_buf == 0)
        glGenBuffers(1, &m_fullscreen_buf);
    if (m_chunk_vao == 0)
        glGenVertexArrays(1, &m_chunk_vao);
    if (m_subcell_vao == 0)
        glGenVertexArrays(1, &m_subcell_vao);
    if (m_fullscreen_vao == 0)
        glGenVertexArrays(1, &m_fullscreen_vao);

    // The boxes as the structure holds them, min then max, 6 floats each; each sub-cell's chunk
    glBindBuffer(GL_ARRAY_BUFFER, m_subcell_box_buf);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(chunks.box.size() * sizeof(float)), chunks.box.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_subcell_chunk_buf);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(chunks.chunk.size() * sizeof(uint32_t)), chunks.chunk.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, m_chunk_box_buf);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(chunks.chunk_box.size() * sizeof(float)), chunks.chunk_box.data(),
                 GL_STATIC_DRAW);
    // A triangle that covers the viewport
    static const float corners[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    glBindBuffer(GL_ARRAY_BUFFER, m_fullscreen_buf);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof(corners)), corners, GL_STATIC_DRAW);

    // Attribute 0 the box's min, 1 its max, 2 a chunk index (the chunk pass reads it without using it: every chunk has
    // a sub-cell, so the sub-cell chunk buffer holds an entry for each)
    const auto box_array = [](GLuint vao, GLuint box_buf, GLuint chunk_buf)
    {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, box_buf);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, GLsizei(6 * sizeof(float)), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, GLsizei(6 * sizeof(float)),
                              reinterpret_cast<const void *>(3 * sizeof(float)));
        glBindBuffer(GL_ARRAY_BUFFER, chunk_buf);
        glEnableVertexAttribArray(2);
        glVertexAttribIPointer(2, 1, GL_INT, GLsizei(sizeof(uint32_t)), nullptr);
    };
    box_array(m_chunk_vao, m_chunk_box_buf, m_subcell_chunk_buf);
    box_array(m_subcell_vao, m_subcell_box_buf, m_subcell_chunk_buf);
    glBindVertexArray(m_fullscreen_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_fullscreen_buf);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    if (take_error() != GL_NO_ERROR)
    {
        reason = "upload failed";
        return false;
    }
    m_static_bytes = (chunks.box.size() + chunks.chunk_box.size() + 6) * sizeof(float) +
                     chunks.chunk.size() * sizeof(uint32_t);
    m_static_structure = inputs.structure_generation;
    m_static_boxes = inputs.boxes_generation;
    m_static_valid = true;
    return true;
}

bool OcclusionCuller::upload_flags(const OcclusionInputs &inputs, std::string &reason)
{
    if (m_flags_valid && m_flags_structure == inputs.structure_generation && m_flags_filter == inputs.filter_generation)
        return true;
    m_flags_valid = false;
    const PrintChunks &chunks = *inputs.chunks;
    const size_t subcells = chunks.subcells();
    const size_t count = chunks.set.chunks.size();
    if (!chunks.applied || chunks.enabled.size() != subcells)
    {
        reason = "no structure";
        return false;
    }
    GLint max_texels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max_texels);
    m_max_texels = size_t(std::max(max_texels, 0));
    if (subcells + count > m_max_texels)
    {
        reason = "too many sub-cells";
        return false;
    }
    // A byte per sub-cell that holds an enabled segment, then a byte per chunk that holds such a sub-cell
    m_flag_bytes.assign(subcells + count, 0);
    m_chunk_flagged.assign(count, 0);
    for (size_t s = 0; s < subcells; ++s)
    {
        if (chunks.enabled[s] == 0)
            continue;
        m_flag_bytes[s] = 1;
        const uint32_t c = chunks.chunk[s];
        if (c < count)
        {
            m_flag_bytes[subcells + c] = 1;
            ++m_chunk_flagged[c];
        }
    }
    m_flagged_chunks = size_t(std::count(m_flag_bytes.begin() + std::ptrdiff_t(subcells), m_flag_bytes.end(), 1));
    if (m_flags_buf == 0)
        glGenBuffers(1, &m_flags_buf);
    if (m_flags_tex == 0)
        glGenTextures(1, &m_flags_tex);
    glBindBuffer(GL_TEXTURE_BUFFER, m_flags_buf);
    glBufferData(GL_TEXTURE_BUFFER, GLsizeiptr(m_flag_bytes.size()), m_flag_bytes.data(), GL_STATIC_DRAW);
    glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_FLAGS_UNIT));
    glBindTexture(GL_TEXTURE_BUFFER, m_flags_tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R8UI, m_flags_buf);
    if (take_error() != GL_NO_ERROR)
    {
        reason = "upload failed";
        return false;
    }
    m_flags_structure = inputs.structure_generation;
    m_flags_filter = inputs.filter_generation;
    m_flags_valid = true;
    return true;
}

void OcclusionCuller::release_targets()
{
    delete_framebuffer(m_depth_fbo);
    delete_framebuffer(m_pyramid_fbo);
    delete_framebuffer(m_chunk_result_fbo);
    delete_framebuffer(m_subcell_result_fbo);
    delete_texture(m_depth_tex);
    delete_texture(m_pyramid_tex);
    delete_texture(m_chunk_result_tex);
    delete_texture(m_subcell_result_tex);
    m_target_width = 0;
    m_target_height = 0;
    m_target_levels = 0;
    m_chunk_rows = 0;
    m_subcell_rows = 0;
    m_depth_owner = OCCLUSION_VIEWS;
}

bool OcclusionCuller::ensure_targets(int width, int height, size_t chunks, size_t subcells, std::string &reason)
{
    GLint max_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    const int chunk_rows = result_rows(chunks);
    const int subcell_rows = result_rows(subcells);
    if (width > max_size || height > max_size)
    {
        reason = "target failed";
        return false;
    }
    if (chunk_rows > max_size || subcell_rows > max_size)
    {
        reason = "too many sub-cells";
        return false;
    }
    // Allocations read no client memory
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_PYRAMID_UNIT));
    const auto nearest_texture = [](GLuint id, int max_level)
    {
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, max_level);
    };
    bool complete = true;
    // The depth target and the pyramid, grown to the largest view: a view uses their lower left part
    if (m_depth_tex == 0 || width > m_target_width || height > m_target_height)
    {
        // The reallocated target holds no step's depth
        m_depth_owner = OCCLUSION_VIEWS;
        const int target_width = std::max(width, m_target_width);
        const int target_height = std::max(height, m_target_height);
        const int levels = depth_pyramid_levels(target_width, target_height);
        if (m_depth_tex == 0)
            glGenTextures(1, &m_depth_tex);
        if (m_pyramid_tex == 0)
            glGenTextures(1, &m_pyramid_tex);
        if (m_depth_fbo == 0)
            glGenFramebuffers(1, &m_depth_fbo);
        if (m_pyramid_fbo == 0)
            glGenFramebuffers(1, &m_pyramid_fbo);
        nearest_texture(m_depth_tex, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, target_width, target_height, 0, GL_DEPTH_COMPONENT,
                     GL_UNSIGNED_INT, nullptr);
        nearest_texture(m_pyramid_tex, levels - 1);
        for (int l = 0; l < levels; ++l)
            glTexImage2D(GL_TEXTURE_2D, l, GL_R32F, std::max(1, target_width >> l), std::max(1, target_height >> l), 0,
                         GL_RED, GL_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, m_depth_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth_tex, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, m_pyramid_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pyramid_tex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        complete = complete && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        m_target_width = target_width;
        m_target_height = target_height;
        m_target_levels = levels;
    }
    // The result targets, a byte per point, their rows grown to the counts
    const auto result_target = [&](GLuint &tex, GLuint &fbo, int &rows, int needed)
    {
        if (tex != 0 && rows >= needed)
            return;
        if (tex == 0)
            glGenTextures(1, &tex);
        if (fbo == 0)
            glGenFramebuffers(1, &fbo);
        rows = std::max(needed, rows);
        nearest_texture(tex, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, OCCLUSION_RESULT_WIDTH, rows, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        complete = complete && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };
    result_target(m_chunk_result_tex, m_chunk_result_fbo, m_chunk_rows, chunk_rows);
    result_target(m_subcell_result_tex, m_subcell_result_fbo, m_subcell_rows, subcell_rows);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (take_error() != GL_NO_ERROR || !complete)
    {
        // A target that could not be made is made again on the next step
        release_targets();
        reason = "target failed";
        return false;
    }
    return true;
}

bool OcclusionCuller::upload_list(GpuList &list, const std::vector<uint32_t> &segments)
{
    if (list.buf == 0)
        glGenBuffers(1, &list.buf);
    if (list.tex == 0)
        glGenTextures(1, &list.tex);
    const size_t bytes = segments.size() * sizeof(uint32_t);
    if (bytes > list.capacity_bytes)
        list.capacity_bytes = list_capacity(bytes, m_max_texels);
    glBindBuffer(GL_TEXTURE_BUFFER, list.buf);
    // A fresh store: the draws still reading the old one keep it, so the upload does not wait for them
    glBufferData(GL_TEXTURE_BUFFER, GLsizeiptr(list.capacity_bytes), nullptr, GL_STREAM_DRAW);
    if (bytes > 0)
        glBufferSubData(GL_TEXTURE_BUFFER, 0, GLsizeiptr(bytes), segments.data());
    list.count = segments.size();
    if (take_error() != GL_NO_ERROR)
    {
        list.count = 0;
        list.capacity_bytes = 0;
        return false;
    }
    return true;
}

bool OcclusionCuller::write_list(GpuList &list, size_t count, const std::function<size_t(uint32_t *)> &fill)
{
    const size_t bytes = count * sizeof(uint32_t);
    if (bytes > 0)
    {
        if (list.buf == 0)
            glGenBuffers(1, &list.buf);
        if (list.tex == 0)
            glGenTextures(1, &list.tex);
        glBindBuffer(GL_TEXTURE_BUFFER, list.buf);
        if (bytes > list.capacity_bytes)
            list.capacity_bytes = list_capacity(bytes, m_max_texels);
        // A fresh store: the draws still reading the old one keep it and none reads this one yet, so the mapping needs
        // no synchronization
        glBufferData(GL_TEXTURE_BUFFER, GLsizeiptr(list.capacity_bytes), nullptr, GL_STREAM_DRAW);
        void *mapped = glMapBufferRange(GL_TEXTURE_BUFFER, 0, GLsizeiptr(bytes),
                                        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (mapped != nullptr)
        {
            const size_t written = fill(static_cast<uint32_t *>(mapped));
            // False when the store was lost while mapped, its contents then undefined
            const bool unmapped = glUnmapBuffer(GL_TEXTURE_BUFFER) == GL_TRUE;
            if (unmapped && take_error() == GL_NO_ERROR)
            {
                list.count = written;
                return true;
            }
            m_map_fallback_reason = "unmap failed";
        }
        else
            m_map_fallback_reason = "map failed";
        ++m_map_fallbacks;
        drain_errors();
    }
    // The list built in memory and uploaded into a fresh store
    m_list_scratch.resize(count);
    m_list_scratch.resize(count > 0 ? fill(m_list_scratch.data()) : 0);
    return upload_list(list, m_list_scratch);
}

void OcclusionCuller::release_list(GpuList &list)
{
    delete_buffer(list.buf);
    delete_texture(list.tex);
    list = GpuList();
}

bool OcclusionCuller::draw_occluders(const GpuList &list, size_t count, const OcclusionViewParams &params,
                                     std::string &reason)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_depth_fbo);
    glViewport(0, 0, params.width, params.height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    count = std::min(count, list.count);
    if (count > 0)
    {
        if (!m_draw_depth || count >= size_t(std::numeric_limits<GLsizei>::max()))
        {
            reason = "draw failed";
            return false;
        }
        m_draw_depth(list.tex, list.buf, count, params.view, params.projection, params.camera);
    }
    if (take_error() != GL_NO_ERROR)
    {
        reason = "draw failed";
        return false;
    }
    return true;
}

bool OcclusionCuller::draw_first_occluders(const ViewState &state, const OcclusionInputs &inputs,
                                           const OcclusionViewParams &params, size_t &drawn, size_t &predicted,
                                           StepTimes &times, std::string &reason)
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    drawn = 0;
    predicted = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, m_depth_fbo);
    glDepthMask(GL_TRUE);
    const GLfloat clear_depth = 1.0f;
    glClearBufferfv(GL_DEPTH, 0, &clear_depth);
    const size_t cap = m_occluder_cap > 0 ? m_occluder_cap : std::numeric_limits<size_t>::max();
    // The view's draw set holds the predicted set's enabled segments under the filter it was made with, nearest first
    const bool same_filter = state.valid && state.structure_generation == inputs.structure_generation &&
                             state.filter_generation == inputs.filter_generation;
    if (same_filter)
        predicted = state.list.count;
    else
    {
        // Under another filter, written again in the order of the chunks the draw set was written in
        const PrintChunks &chunks = *inputs.chunks;
        predicted = write_applied_print_chunks(chunks, state.chunk_order, state.predicted,
                                               std::numeric_limits<size_t>::max(), nullptr);
        const auto fill = [&](uint32_t *out)
        {
            return write_applied_print_chunks(chunks, state.chunk_order, state.predicted, cap, out);
        };
        if (!write_list(m_occluder_list, std::min(predicted, cap), fill))
        {
            reason = "upload failed";
            return false;
        }
    }
    const auto drawing = Clock::now();
    times.emit += std::chrono::duration<double, std::milli>(drawing - start).count();
    const GpuList &list = same_filter ? state.list : m_occluder_list;
    drawn = std::min(list.count, cap);
    const bool ok = draw_occluders(list, drawn, params, reason);
    times.depth += std::chrono::duration<double, std::milli>(Clock::now() - drawing).count();
    return ok;
}

void OcclusionCuller::build_pyramid(int width, int height, int last_level)
{
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindVertexArray(m_fullscreen_vao);
    glBindFramebuffer(GL_FRAMEBUFFER, m_pyramid_fbo);
    glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_PYRAMID_UNIT));
    // Level 0: the depth target's values
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pyramid_tex, 0);
    glViewport(0, 0, width, height);
    glUseProgram(m_copy_program);
    glUniform1i(m_uni_copy_depth_tex, OCCLUSION_PYRAMID_UNIT);
    glBindTexture(GL_TEXTURE_2D, m_depth_tex);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // Level l + 1 from level l, the only level the texture's base and maximum level leave to sample, so the level
    // written is never one that can be read
    glBindTexture(GL_TEXTURE_2D, m_pyramid_tex);
    glUseProgram(m_reduce_program);
    glUniform1i(m_uni_reduce_source_tex, OCCLUSION_PYRAMID_UNIT);
    for (int l = 0; l < last_level; ++l)
    {
        const int source_width = std::max(1, width >> l);
        const int source_height = std::max(1, height >> l);
        const int target_width = std::max(1, width >> (l + 1));
        const int target_height = std::max(1, height >> (l + 1));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, l);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, l);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pyramid_tex, l + 1);
        glViewport(0, 0, target_width, target_height);
        glUniform2i(m_uni_reduce_source_size, source_width, source_height);
        glUniform2i(m_uni_reduce_target_size, target_width, target_height);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // Every level readable again for the test, level 0 attached as allocated
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, m_target_levels - 1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pyramid_tex, 0);
}

bool OcclusionCuller::draw_tests(const OcclusionViewParams &params, const float view_proj[16], float orientation,
                                 int last_level, size_t chunks, size_t subcells, std::string &reason)
{
    const int chunk_rows = result_rows(chunks);
    const int subcell_rows = result_rows(subcells);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(m_test_program);
    glUniformMatrix4fv(m_uni.view_proj, 1, GL_FALSE, view_proj);
    glUniform2f(m_uni.viewport_size, float(params.width), float(params.height));
    glUniform2i(m_uni.viewport_texels, params.width, params.height);
    glUniform1f(m_uni.orientation, orientation);
    glUniform1i(m_uni.last_level, last_level);
    glUniform1i(m_uni.result_width, OCCLUSION_RESULT_WIDTH);
    glUniform1i(m_uni.chunk_flag_offset, GLint(subcells));
    glUniform1f(m_uni.min_clip_w, OCCLUSION_GPU_MIN_CLIP_W);
    glUniform1f(m_uni.depth_tolerance, OCCLUSION_GPU_DEPTH_TOLERANCE);
    glUniform1f(m_uni.face_min_area_fraction, OCCLUSION_GPU_FACE_MIN_AREA_FRACTION);
    glUniform1f(m_uni.face_min_area, OCCLUSION_GPU_FACE_MIN_AREA);
    glUniform1f(m_uni.window_margin, OCCLUSION_GPU_WINDOW_MARGIN);
    glUniform1f(m_uni.unbounded, std::numeric_limits<float>::max());
    glUniform4iv(m_uni.box_faces, 6, &OCCLUSION_BOX_FACES[0][0]);
    glUniform1i(m_uni.pyramid_tex, OCCLUSION_PYRAMID_UNIT);
    glUniform1i(m_uni.chunk_result_tex, OCCLUSION_CHUNK_RESULT_UNIT);
    glUniform1i(m_uni.flags_tex, OCCLUSION_FLAGS_UNIT);
    glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_FLAGS_UNIT));
    glBindTexture(GL_TEXTURE_BUFFER, m_flags_tex);
    glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_PYRAMID_UNIT));
    glBindTexture(GL_TEXTURE_2D, m_pyramid_tex);

    // The chunk boxes; the chunk result target is not bound for reading while written
    glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_CHUNK_RESULT_UNIT));
#ifdef __APPLE__
    // This stage does not read chunk_result_tex. The macOS driver reports a sampler whose unit holds no texture
    // with contents, so the pyramid stands in on its unit
    glBindTexture(GL_TEXTURE_2D, m_pyramid_tex);
#else
    glBindTexture(GL_TEXTURE_2D, 0);
#endif // __APPLE__
    glBindFramebuffer(GL_FRAMEBUFFER, m_chunk_result_fbo);
    glViewport(0, 0, OCCLUSION_RESULT_WIDTH, chunk_rows);
    glUniform1i(m_uni.stage, 0);
    glUniform1i(m_uni.taps, OCCLUSION_CHUNK_TAPS);
    glUniform2f(m_uni.result_size, float(OCCLUSION_RESULT_WIDTH), float(chunk_rows));
    glBindVertexArray(m_chunk_vao);
    glDrawArrays(GL_POINTS, 0, GLsizei(chunks));

    // The sub-cell boxes, each read with its chunk's result
    glBindTexture(GL_TEXTURE_2D, m_chunk_result_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, m_subcell_result_fbo);
    glViewport(0, 0, OCCLUSION_RESULT_WIDTH, subcell_rows);
    glUniform1i(m_uni.stage, 1);
    glUniform1i(m_uni.taps, OCCLUSION_SUBCELL_TAPS);
    glUniform2f(m_uni.result_size, float(OCCLUSION_RESULT_WIDTH), float(subcell_rows));
    glBindVertexArray(m_subcell_vao);
    glDrawArrays(GL_POINTS, 0, GLsizei(subcells));
    glBindTexture(GL_TEXTURE_2D, 0);
    if (take_error() != GL_NO_ERROR)
    {
        reason = "test failed";
        return false;
    }
    return true;
}

bool OcclusionCuller::read_results(size_t chunks, size_t subcells, std::string &reason)
{
    const int chunk_rows = result_rows(chunks);
    const int subcell_rows = result_rows(subcells);
    m_chunk_readback.resize(size_t(OCCLUSION_RESULT_WIDTH) * size_t(chunk_rows));
    m_readback.resize(size_t(OCCLUSION_RESULT_WIDTH) * size_t(subcell_rows));
    // Both results, a byte per point in point order
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_chunk_result_fbo);
    glReadPixels(0, 0, OCCLUSION_RESULT_WIDTH, chunk_rows, GL_RED, GL_UNSIGNED_BYTE, m_chunk_readback.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_subcell_result_fbo);
    glReadPixels(0, 0, OCCLUSION_RESULT_WIDTH, subcell_rows, GL_RED, GL_UNSIGNED_BYTE, m_readback.data());
    if (take_error() != GL_NO_ERROR)
    {
        reason = "readback failed";
        return false;
    }
    return true;
}

bool OcclusionCuller::run_step(ViewState &state, const OcclusionInputs &inputs, const OcclusionViewParams &params,
                               bool want_residual, size_t &residual_chunks, const char *&residual_reason,
                               OcclusionViewStats &stats, StepTimes &times)
{
    residual_chunks = 0;
    residual_reason = "";
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    // Adds the time since the last mark to a part of the step
    auto mark = start;
    const auto lap = [&mark](double &part)
    {
        const auto now = Clock::now();
        part += std::chrono::duration<double, std::milli>(now - mark).count();
        mark = now;
    };
    const PrintChunks &chunks = *inputs.chunks;
    const size_t subcells = chunks.subcells();
    const size_t chunk_count = chunks.set.chunks.size();
    // The prediction: kept through a filter change, none for a new structure
    if (state.predicted_structure != inputs.structure_generation || state.predicted.size() != subcells)
    {
        state.predicted.assign(subcells, 0);
        state.chunk_order.clear();
        state.predicted_structure = inputs.structure_generation;
    }
    m_map_fallbacks = 0;
    m_map_fallback_reason = "";
    float view_proj[16];
    occlusion_view_proj(params.projection.data(), params.view.data(), view_proj);
    const float orientation = box_face_orientation(view_proj);
    const int last_level = depth_pyramid_levels(params.width, params.height) - 1;
    // The slab's distance and the draw order's: from the eye (perspective), along the view direction, minus the view
    // matrix's third row (orthographic)
    const bool perspective = params.projection[15] == 0.0f;
    const float forward[3] = {-params.view[2], -params.view[6], -params.view[10]};
    const auto distance_of = [&](size_t s)
    {
        const float *box = &chunks.box[6 * s];
        float centre[3];
        for (int a = 0; a < 3; ++a)
            centre[a] = 0.5f * box[a] + 0.5f * box[3 + a];
        float d = 0.0f;
        if (perspective)
            for (int a = 0; a < 3; ++a)
                d += (centre[a] - params.camera[a]) * (centre[a] - params.camera[a]);
        else
            for (int a = 0; a < 3; ++a)
                d += centre[a] * forward[a];
        return std::isfinite(d) ? d : std::numeric_limits<float>::max();
    };

    // The sub-cells drawn as occluders so far: the predicted set, copied to m_drawn once a round adds to it
    bool drawn_added = false;
    std::string reason;
    size_t occluder_segments = 0;
    size_t new_segments = 0;
    size_t rounds = 0;
    size_t chunks_tested = 0;
    size_t subcells_tested = 0;
    size_t visible = 0;
    size_t chunks_drawn = 0;
    // The residual is written when asked and step 1 drew the whole predicted set
    bool residual = false;
    size_t residual_segments = 0;
    bool ok = true;
    {
        StateGuard guard;
        drain_errors();
        ok = upload_static(inputs, reason) && upload_flags(inputs, reason) &&
             ensure_targets(params.width, params.height, chunk_count, subcells, reason);
        size_t predicted_segments = 0;
        if (ok)
        {
            set_common_state();
            size_t drawn = 0;
            ok = draw_first_occluders(state, inputs, params, drawn, predicted_segments, times, reason);
            occluder_segments += drawn;
            if (want_residual)
            {
                residual = drawn == predicted_segments;
                if (!residual)
                    residual_reason = "occluder cap";
            }
        }
        // The occluder segments the budget weighs the new ones against: the predicted set whole, whatever part of it
        // the cap left undrawn, so the cap alone never makes a step refine, then the slabs drawn
        size_t weighed_segments = predicted_segments;
        mark = Clock::now();
        while (ok)
        {
            build_pyramid(params.width, params.height, last_level);
            lap(times.pyramid);
            ok = draw_tests(params, view_proj, orientation, last_level, chunk_count, subcells, reason);
            lap(times.test);
            if (!ok)
                break;
            ok = read_results(chunk_count, subcells, reason);
            lap(times.readback);
            if (!ok)
                break;
            ++rounds;
            chunks_tested += m_flagged_chunks;
            for (size_t c = 0; c < chunk_count; ++c)
                if (m_chunk_readback[c] != 0)
                    subcells_tested += m_chunk_flagged[c];
            // The enabled segments of the sub-cells that passed and were neither predicted nor drawn in a slab: done
            // when they fit the budget, or after the last round
            const uint8_t *passed = m_readback.data();
            const uint8_t *drawn = drawn_added ? m_drawn.data() : state.predicted.data();
            size_t fresh_segments = 0;
            for (size_t s = 0; s < subcells; ++s)
                if (passed[s] != 0 && drawn[s] == 0)
                    fresh_segments += chunks.enabled[s];
            if (rounds == 1)
                new_segments = fresh_segments;
            const size_t budget = std::max(OCCLUSION_NEW_SEGMENTS_BUDGET, weighed_segments);
            if (rounds >= OCCLUSION_MAX_ROUNDS || fresh_segments <= budget)
            {
                lap(times.emit);
                break;
            }
            // Their slab nearest to the camera, weighed by the enabled segments each holds, drawn on top of the depth
            // so far
            if (!drawn_added)
            {
                m_drawn.assign(state.predicted.begin(), state.predicted.end());
                drawn_added = true;
            }
            m_candidates.clear();
            for (size_t s = 0; s < subcells; ++s)
                if (passed[s] != 0 && m_drawn[s] == 0 && chunks.enabled[s] != 0)
                {
                    SlabCandidate candidate;
                    candidate.distance = distance_of(s);
                    candidate.subcell = uint32_t(s);
                    candidate.segments = chunks.enabled[s];
                    m_candidates.push_back(candidate);
                }
            const size_t slab = select_nearest_slab(m_candidates, OCCLUSION_FIRST_SLAB_SEGMENTS << (rounds - 1));
            size_t slab_segments = 0;
            for (size_t i = 0; i < slab; ++i)
            {
                slab_segments += m_candidates[i].segments;
                m_drawn[m_candidates[i].subcell] = 1;
            }
            // Each slab sub-cell's slot prefix
            const auto fill_slab = [&](uint32_t *out)
            {
                size_t written = 0;
                for (size_t i = 0; i < slab; ++i)
                {
                    const uint32_t s = m_candidates[i].subcell;
                    const uint32_t *slot = chunks.set.order.data() + chunks.set.subcell_first[s];
                    std::copy(slot, slot + chunks.enabled[s], out + written);
                    written += chunks.enabled[s];
                }
                return written;
            };
            ok = write_list(m_occluder_list, slab_segments, fill_slab);
            lap(times.emit);
            if (!ok)
            {
                reason = "upload failed";
                break;
            }
            ok = draw_occluders(m_occluder_list, m_occluder_list.count, params, reason);
            lap(times.depth);
            occluder_segments += m_occluder_list.count;
            weighed_segments += m_occluder_list.count;
        }
        // The residual's sub-cells: passed the last test, and neither in the prediction this step started from (read
        // before the draw set replaces it) nor in a slab; into m_drawn, which held the drawn ones
        if (ok && residual)
        {
            if (!drawn_added)
                m_drawn.resize(subcells);
            const uint8_t *drawn = drawn_added ? m_drawn.data() : state.predicted.data();
            uint8_t *rest = m_drawn.data();
            uint32_t last_chunk = std::numeric_limits<uint32_t>::max();
            for (size_t s = 0; s < subcells; ++s)
            {
                const bool in_rest = m_readback[s] != 0 && drawn[s] == 0 && chunks.enabled[s] != 0;
                rest[s] = in_rest ? 1 : 0;
                if (!in_rest)
                    continue;
                residual_segments += chunks.enabled[s];
                if (chunks.chunk[s] != last_chunk)
                {
                    last_chunk = chunks.chunk[s];
                    ++residual_chunks;
                }
            }
        }
        // The draw set: the enabled segments of the sub-cells that passed the last test, chunk by chunk nearest first.
        // Written again only when the sub-cells, the filter or the chunk order changed: the same sub-cells under the
        // same filter seen from the same view keep the order, and an order equal to the last one gives the same list.
        if (ok)
        {
            const bool same_set = state.valid && state.structure_generation == inputs.structure_generation &&
                                  state.filter_generation == inputs.filter_generation &&
                                  std::memcmp(state.predicted.data(), m_readback.data(), subcells) == 0;
            // A whole enabled set keeps no view, and its list is in structure order
            const bool same_view = same_set && !state.all && state.boxes_generation == inputs.boxes_generation &&
                                   state.params.view == params.view && state.params.projection == params.projection &&
                                   state.params.camera == params.camera;
            if (same_view)
            {
                visible = state.result.subcells_visible;
                chunks_drawn = state.chunks;
            }
            else
            {
                // The chunks that hold a sub-cell that passed (sub-cells are in chunk order) and the segments they hold
                m_chunk_order.clear();
                size_t total = 0;
                uint32_t last_chunk = std::numeric_limits<uint32_t>::max();
                for (size_t s = 0; s < subcells; ++s)
                {
                    if (m_readback[s] == 0)
                        continue;
                    ++visible;
                    total += chunks.enabled[s];
                    if (chunks.chunk[s] != last_chunk)
                    {
                        last_chunk = chunks.chunk[s];
                        m_chunk_order.push_back(last_chunk);
                    }
                }
                order_chunks_front_to_back(chunks.chunk_box, perspective, params.camera.data(), forward, m_chunk_order,
                                           m_chunk_distances);
                chunks_drawn = m_chunk_order.size();
                if (!same_set || m_chunk_order != state.chunk_order)
                {
                    const auto fill = [&](uint32_t *out)
                    {
                        return write_applied_print_chunks(chunks, m_chunk_order, m_readback, total, out);
                    };
                    ok = write_list(state.list, total, fill);
                    if (ok)
                    {
                        std::memcpy(state.predicted.data(), m_readback.data(), subcells);
                        state.chunk_order.swap(m_chunk_order);
                    }
                    else
                        reason = "upload failed";
                }
            }
            lap(times.list);
        }
        // The residual, chunk by chunk in the draw set's order, which holds every chunk of a sub-cell that passed; a
        // failure leaves the step's result standing
        if (ok && residual)
        {
            if (residual_segments == 0)
                state.residual.count = 0;
            else
            {
                const auto fill = [&](uint32_t *out)
                {
                    return write_applied_print_chunks(chunks, state.chunk_order, m_drawn, residual_segments, out);
                };
                if (!write_list(state.residual, residual_segments, fill))
                {
                    residual = false;
                    residual_reason = "upload failed";
                    drain_errors();
                }
                else if (state.residual.count != residual_segments)
                {
                    residual = false;
                    residual_reason = "list short";
                }
            }
            lap(times.list);
        }
        if (!ok)
            drain_errors();
    }
    drain_errors();
    if (!residual)
        residual_chunks = 0;
    if (!ok)
    {
        state.valid = false;
        stats = OcclusionViewStats();
        stats.reason = reason;
        return false;
    }

    // The result's figures; the readback is the next prediction
    state.valid = true;
    state.all = false;
    state.params = params;
    state.structure_generation = inputs.structure_generation;
    state.boxes_generation = inputs.boxes_generation;
    state.filter_generation = inputs.filter_generation;
    state.occluder_cap = m_occluder_cap;
    state.chunks = chunks_drawn;
    stats = OcclusionViewStats();
    stats.active = true;
    stats.reason.clear();
    stats.rounds = rounds;
    stats.chunks_tested = chunks_tested;
    stats.subcells_tested = subcells_tested;
    stats.subcells_visible = visible;
    stats.occluder_segments = occluder_segments;
    stats.new_segments = new_segments;
    stats.segments_drawn = state.list.count;
    stats.map_fallbacks = m_map_fallbacks;
    stats.map_fallback_reason = m_map_fallback_reason;
    stats.ms = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
    state.result = stats;
    return true;
}

bool OcclusionCuller::request(size_t view, const OcclusionInputs &inputs, const OcclusionViewParams &params,
                              bool want_residual, OcclusionDrawList &list, bool &cached, OcclusionViewStats &stats,
                              OcclusionBenchStats &bench)
{
    cached = false;
    list.depth_ready = false;
    list.residual_reason = "";
    // Only a step of this call leaves the view a depth to merge
    if (m_depth_owner == view)
        m_depth_owner = OCCLUSION_VIEWS;
    const auto fail = [&stats](const char *reason)
    {
        stats = OcclusionViewStats();
        stats.reason = reason;
        return false;
    };
    if (view >= OCCLUSION_VIEWS || inputs.chunks == nullptr || !inputs.chunks->valid || !inputs.chunks->applied ||
        inputs.chunks->subcells() == 0 || inputs.chunks->enabled.size() != inputs.chunks->subcells() ||
        inputs.chunks->subcells() >= size_t(std::numeric_limits<GLint>::max()))
        return fail("no structure");
    if (params.width <= 0 || params.height <= 0)
        return fail("no viewport");
    ViewState &state = m_views[view];
    // The last step's result, for the same view under the same generations and occluder cap; within its frame, the
    // camera's at any viewport size (the G-buffer and the visible pass may differ in size)
    const OcclusionViewParams &last = state.params;
    if (state.valid && !state.all && last.view == params.view && last.projection == params.projection &&
        last.camera == params.camera && state.structure_generation == inputs.structure_generation &&
        state.boxes_generation == inputs.boxes_generation && state.filter_generation == inputs.filter_generation &&
        state.occluder_cap == m_occluder_cap &&
        ((last.width == params.width && last.height == params.height) ||
         (view == OCCLUSION_VIEW_CAMERA && last.frame == params.frame)))
    {
        state.params.frame = params.frame;
        cached = true;
        stats = state.result;
        stats.ms = 0.0f;
        ++bench.cached;
        list.tex_id = state.list.tex;
        list.buf_id = state.list.buf;
        list.count = state.list.count;
        list.chunks = state.chunks;
        return true;
    }
    StepTimes times;
    size_t residual_chunks = 0;
    const char *residual_reason = "";
    // The step replaces the depth target's contents
    m_depth_owner = OCCLUSION_VIEWS;
    try
    {
        if (!build_programs())
            return fail("program failed");
        if (!run_step(state, inputs, params, want_residual, residual_chunks, residual_reason, stats, times))
            return false;
    }
    catch (...)
    {
        // An allocation failed; the guard restored the GL state
        state.valid = false;
        drain_errors();
        return fail("out of memory");
    }
    m_depth_owner = view;
    m_depth_width = params.width;
    m_depth_height = params.height;
    // Asked for, the residual was written unless the step said why not
    if (want_residual && *residual_reason == '\0')
    {
        list.residual_tex_id = state.residual.tex;
        list.residual_buf_id = state.residual.buf;
        list.residual_count = state.residual.count;
        list.residual_chunks = residual_chunks;
        list.depth_ready = true;
        stats.residual_segments = state.residual.count;
    }
    else
        list.residual_reason = residual_reason;
    ++bench.steps;
    bench.rounds += stats.rounds;
    bench.points += stats.chunks_tested + stats.subcells_tested;
    bench.segments += stats.segments_drawn;
    bench.occluders += stats.occluder_segments;
    bench.visible += stats.subcells_visible;
    bench.new_segments += stats.new_segments;
    bench.ms += stats.ms;
    bench.emit_ms += times.emit;
    bench.depth_ms += times.depth;
    bench.pyramid_ms += times.pyramid;
    bench.test_ms += times.test;
    bench.readback_ms += times.readback;
    bench.list_ms += times.list;
    list.tex_id = state.list.tex;
    list.buf_id = state.list.buf;
    list.count = state.list.count;
    list.chunks = state.chunks;
    return true;
}

bool OcclusionCuller::request_all(size_t view, const OcclusionInputs &inputs, OcclusionDrawList &list, bool &cached,
                                  OcclusionViewStats &stats, OcclusionBenchStats &bench)
{
    cached = false;
    list.depth_ready = false;
    list.residual_reason = "";
    const auto fail = [&stats](const char *reason)
    {
        stats = OcclusionViewStats();
        stats.reason = reason;
        return false;
    };
    if (view >= OCCLUSION_VIEWS || inputs.chunks == nullptr || !inputs.chunks->valid || !inputs.chunks->applied ||
        inputs.chunks->subcells() == 0 || inputs.chunks->enabled.size() != inputs.chunks->subcells() ||
        inputs.chunks->subcells() >= size_t(std::numeric_limits<GLint>::max()))
        return fail("no structure");
    // The view's result is no step's, so it has no depth to merge
    if (m_depth_owner == view)
        m_depth_owner = OCCLUSION_VIEWS;
    ViewState &state = m_views[view];
    const auto take = [&list, &state]()
    {
        list.tex_id = state.list.tex;
        list.buf_id = state.list.buf;
        list.count = state.list.count;
        list.chunks = state.chunks;
    };
    // The whole enabled set written under the same structure and filter holds for any view
    if (state.valid && state.all && state.structure_generation == inputs.structure_generation &&
        state.filter_generation == inputs.filter_generation)
    {
        cached = true;
        stats = state.result;
        stats.ms = 0.0f;
        ++bench.all;
        take();
        return true;
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const PrintChunks &chunks = *inputs.chunks;
    const size_t subcells = chunks.subcells();
    size_t visible = 0;
    size_t total = 0;
    bool ok = false;
    try
    {
        if (!chunk_ranges_cover(chunks))
            return fail("no structure");
        // Every sub-cell that holds an enabled segment, and the chunks that hold one in structure order (sub-cells are
        // in chunk order); made aside, so a failed write leaves the view's prediction as it was
        m_drawn.assign(subcells, 0);
        m_chunk_order.clear();
        uint32_t last_chunk = std::numeric_limits<uint32_t>::max();
        for (size_t s = 0; s < subcells; ++s)
        {
            if (chunks.enabled[s] == 0)
                continue;
            m_drawn[s] = 1;
            ++visible;
            total += chunks.enabled[s];
            if (chunks.chunk[s] != last_chunk)
            {
                last_chunk = chunks.chunk[s];
                m_chunk_order.push_back(last_chunk);
            }
        }
        m_map_fallbacks = 0;
        m_map_fallback_reason = "";
        {
            StateGuard guard;
            drain_errors();
            const auto fill = [&](uint32_t *out)
            {
                return write_applied_print_chunks(chunks, m_chunk_order, m_drawn, total, out);
            };
            ok = write_list(state.list, total, fill);
            if (!ok)
                drain_errors();
        }
        drain_errors();
    }
    catch (...)
    {
        // An allocation failed; the guard restored the GL state
        state.valid = false;
        drain_errors();
        return fail("out of memory");
    }
    // The write replaced the list's store whatever came of it; a count short of the enabled total means the chunk
    // ranges did not take every enabled sub-cell
    if (!ok || state.list.count != total)
    {
        state.valid = false;
        return fail(ok ? "no structure" : "upload failed");
    }

    // No view and no test: every sub-cell that holds an enabled segment is the next step's prediction
    state.valid = true;
    state.all = true;
    state.params = OcclusionViewParams();
    state.structure_generation = inputs.structure_generation;
    state.boxes_generation = inputs.boxes_generation;
    state.filter_generation = inputs.filter_generation;
    state.occluder_cap = m_occluder_cap;
    state.chunks = m_chunk_order.size();
    state.predicted.swap(m_drawn);
    state.chunk_order.swap(m_chunk_order);
    state.predicted_structure = inputs.structure_generation;
    stats = OcclusionViewStats();
    stats.active = true;
    stats.all = true;
    stats.reason.clear();
    stats.subcells_visible = visible;
    stats.segments_drawn = state.list.count;
    stats.map_fallbacks = m_map_fallbacks;
    stats.map_fallback_reason = m_map_fallback_reason;
    stats.ms = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
    state.result = stats;
    ++bench.all;
    take();
    return true;
}

bool OcclusionCuller::merge_depth(size_t view, const int viewport[4], std::string &reason)
{
    if (view >= OCCLUSION_VIEWS || m_depth_owner != view || m_depth_tex == 0 || m_fullscreen_vao == 0)
    {
        reason = "no step depth";
        return false;
    }
    if (viewport[2] != m_depth_width || viewport[3] != m_depth_height)
    {
        reason = "viewport size";
        return false;
    }
    if (m_merge_program == 0)
    {
        reason = "merge program failed";
        return false;
    }
    GLenum error = GL_NO_ERROR;
    {
        // The guard reads the bindings without changing them, so the draw goes to the caller's framebuffer
        StateGuard guard;
        drain_errors();
        set_common_state();
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glUseProgram(m_merge_program);
        glUniform1i(m_uni_merge_depth_tex, OCCLUSION_PYRAMID_UNIT);
        glUniform2i(m_uni_merge_viewport_origin, viewport[0], viewport[1]);
        glActiveTexture(GLenum(GL_TEXTURE0 + OCCLUSION_PYRAMID_UNIT));
        glBindTexture(GL_TEXTURE_2D, m_depth_tex);
        glBindVertexArray(m_fullscreen_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        error = take_error();
    }
    drain_errors();
    if (error != GL_NO_ERROR)
    {
        reason = "merge failed";
        return false;
    }
    return true;
}

void OcclusionCuller::release(bool programs)
{
    for (ViewState &state : m_views)
    {
        release_list(state.list);
        release_list(state.residual);
        state = ViewState();
    }
    delete_vertex_array(m_chunk_vao);
    delete_vertex_array(m_subcell_vao);
    delete_vertex_array(m_fullscreen_vao);
    delete_buffer(m_subcell_box_buf);
    delete_buffer(m_subcell_chunk_buf);
    delete_buffer(m_chunk_box_buf);
    delete_buffer(m_fullscreen_buf);
    m_static_valid = false;
    m_static_bytes = 0;
    delete_buffer(m_flags_buf);
    delete_texture(m_flags_tex);
    m_flags_valid = false;
    std::vector<uint8_t>().swap(m_flag_bytes);
    std::vector<uint32_t>().swap(m_chunk_flagged);
    m_flagged_chunks = 0;
    release_targets();
    release_list(m_occluder_list);
    std::vector<uint8_t>().swap(m_readback);
    std::vector<uint8_t>().swap(m_chunk_readback);
    std::vector<uint8_t>().swap(m_drawn);
    std::vector<SlabCandidate>().swap(m_candidates);
    std::vector<uint32_t>().swap(m_chunk_order);
    std::vector<ChunkDistance>().swap(m_chunk_distances);
    std::vector<uint32_t>().swap(m_list_scratch);
    if (programs)
    {
        delete_program(m_copy_program);
        delete_program(m_reduce_program);
        delete_program(m_test_program);
        delete_program(m_merge_program);
        m_programs_failed = false;
        m_program_log.clear();
    }
}

bool OcclusionCuller::holds_gl_objects() const
{
    for (const ViewState &state : m_views)
        if (state.list.buf != 0 || state.list.tex != 0 || state.residual.buf != 0 || state.residual.tex != 0)
            return true;
    return m_chunk_vao != 0 || m_subcell_vao != 0 || m_fullscreen_vao != 0 || m_subcell_box_buf != 0 ||
           m_subcell_chunk_buf != 0 || m_chunk_box_buf != 0 || m_fullscreen_buf != 0 || m_flags_buf != 0 ||
           m_flags_tex != 0 || m_depth_tex != 0 || m_depth_fbo != 0 || m_pyramid_tex != 0 || m_pyramid_fbo != 0 ||
           m_chunk_result_tex != 0 || m_chunk_result_fbo != 0 || m_subcell_result_tex != 0 ||
           m_subcell_result_fbo != 0 || m_occluder_list.buf != 0 || m_occluder_list.tex != 0;
}

size_t OcclusionCuller::gpu_bytes() const
{
    size_t bytes = m_static_bytes + (m_flags_buf != 0 ? m_flag_bytes.size() : 0) + m_occluder_list.capacity_bytes;
    for (const ViewState &state : m_views)
        bytes += state.list.capacity_bytes + state.residual.capacity_bytes;
    // The depth target (24 bits, which drivers store in 4 bytes) and the pyramid's float levels
    if (m_depth_tex != 0)
        bytes += size_t(m_target_width) * size_t(m_target_height) * sizeof(uint32_t);
    for (int l = 0; l < m_target_levels; ++l)
        bytes += size_t(std::max(1, m_target_width >> l)) * size_t(std::max(1, m_target_height >> l)) * sizeof(float);
    bytes += size_t(OCCLUSION_RESULT_WIDTH) * size_t(m_chunk_rows + m_subcell_rows);
    return bytes;
}

size_t OcclusionCuller::cpu_bytes() const
{
    size_t bytes = m_flag_bytes.capacity() + m_chunk_flagged.capacity() * sizeof(uint32_t) + m_readback.capacity() +
                   m_chunk_readback.capacity() + m_drawn.capacity() + m_candidates.capacity() * sizeof(SlabCandidate) +
                   m_chunk_order.capacity() * sizeof(uint32_t) + m_chunk_distances.capacity() * sizeof(ChunkDistance) +
                   m_list_scratch.capacity() * sizeof(uint32_t);
    for (const ViewState &state : m_views)
        bytes += state.predicted.capacity() + state.chunk_order.capacity() * sizeof(uint32_t);
    return bytes;
}

#ifdef PREFLIGHT_TEST_HOOKS
bool OcclusionCuller::cross_check(const OcclusionInputs &inputs, OcclusionCrossCheck &out, std::string &error)
{
    out = OcclusionCrossCheck();
    // It draws into the depth target
    m_depth_owner = OCCLUSION_VIEWS;
    const ViewState &state = m_views[OCCLUSION_VIEW_CAMERA];
    if (!state.valid || inputs.chunks == nullptr || !inputs.chunks->applied ||
        state.structure_generation != inputs.structure_generation ||
        state.boxes_generation != inputs.boxes_generation || state.filter_generation != inputs.filter_generation ||
        m_test_program == 0)
    {
        error = "no camera result";
        return false;
    }
    const PrintChunks &chunks = *inputs.chunks;
    const size_t subcells = chunks.subcells();
    const size_t chunk_count = chunks.set.chunks.size();
    const OcclusionViewParams params = state.params;
    float view_proj[16];
    occlusion_view_proj(params.projection.data(), params.view.data(), view_proj);
    const float orientation = box_face_orientation(view_proj);
    const int last_level = depth_pyramid_levels(params.width, params.height) - 1;
    std::vector<float> depth(size_t(params.width) * size_t(params.height), 1.0f);

    // One round from the view's draw set, then the depth it tested against
    std::string reason;
    bool ok = true;
    {
        StateGuard guard;
        drain_errors();
        ok = upload_static(inputs, reason) && upload_flags(inputs, reason) &&
             ensure_targets(params.width, params.height, chunk_count, subcells, reason);
        if (ok)
        {
            set_common_state();
            size_t drawn = 0;
            size_t predicted = 0;
            StepTimes times;
            ok = draw_first_occluders(state, inputs, params, drawn, predicted, times, reason);
        }
        if (ok)
        {
            build_pyramid(params.width, params.height, last_level);
            ok = draw_tests(params, view_proj, orientation, last_level, chunk_count, subcells, reason) &&
                 read_results(chunk_count, subcells, reason);
        }
        if (ok)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_depth_fbo);
            glReadPixels(0, 0, params.width, params.height, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
            if (take_error() != GL_NO_ERROR)
            {
                ok = false;
                reason = "depth readback failed";
            }
        }
        if (!ok)
            drain_errors();
    }
    drain_errors();
    if (!ok)
    {
        error = reason;
        return false;
    }

    // The CPU rule on the same depth: 4 taps on every chunk that holds an enabled sub-cell, 8 on every enabled sub-cell
    // of the chunks that pass on the CPU
    const DepthPyramid pyramid = build_depth_pyramid(params.width, params.height, std::move(depth));
    std::vector<uint8_t> cpu_chunk(chunk_count, 0);
    for (size_t c = 0; c < chunk_count; ++c)
    {
        if (m_flag_bytes[subcells + c] == 0)
            continue;
        const float *box = &chunks.chunk_box[6 * c];
        const bool cpu = !occlusion_box_bounded(box) ||
                         test_box_occlusion_faces(view_proj, box, box + 3, pyramid, OCCLUSION_CHUNK_TAPS) ==
                             BoxVisibility::Visible;
        const bool gpu = m_chunk_readback[c] != 0;
        cpu_chunk[c] = cpu ? 1 : 0;
        ++out.chunks;
        out.chunk_gpu_only += gpu && !cpu ? 1 : 0;
        out.chunk_cpu_only += cpu && !gpu ? 1 : 0;
    }
    for (size_t s = 0; s < subcells; ++s)
    {
        if (m_flag_bytes[s] == 0)
            continue;
        const float *box = &chunks.box[6 * s];
        const bool cpu = cpu_chunk[chunks.chunk[s]] != 0 &&
                         (!occlusion_box_bounded(box) ||
                          test_box_occlusion_faces(view_proj, box, box + 3, pyramid, OCCLUSION_SUBCELL_TAPS) ==
                              BoxVisibility::Visible);
        const bool gpu = m_readback[s] != 0;
        ++out.subcells;
        out.subcell_gpu_only += gpu && !cpu ? 1 : 0;
        out.subcell_cpu_only += cpu && !gpu ? 1 : 0;
    }
    return true;
}
#endif // PREFLIGHT_TEST_HOOKS

} // namespace libvgcode
