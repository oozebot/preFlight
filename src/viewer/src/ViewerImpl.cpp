///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "ViewerImpl.hpp"
#include "../include/GCodeInputData.hpp"
#include "Shaders.hpp"
#include "OpenGLUtils.hpp"
#include "Utils.hpp"
#ifdef PREFLIGHT_TEST_HOOKS
#include "OcclusionTest.hpp"
#endif // PREFLIGHT_TEST_HOOKS

#include <map>
#include <assert.h>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <numeric>
#include <thread>

namespace libvgcode
{

static Mat4x4 inverse(const Mat4x4 &m)
{
    // ref: https://stackoverflow.com/questions/1148309/inverting-a-4x4-matrix

    Mat4x4 inv;

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];

    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];

    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];

    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];

    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];

    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];

    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];

    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];

    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];

    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    assert(det != 0.0f);

    det = 1.0f / det;

    std::array<float, 16> ret = {};
    for (int i = 0; i < 16; ++i)
    {
        ret[i] = inv[i] * det;
    }

    return ret;
}

std::string check_shader(GLuint handle)
{
    std::string ret;
    GLint params;
    glsafe(glGetShaderiv(handle, GL_COMPILE_STATUS, &params));
    if (params == GL_FALSE)
    {
        glsafe(glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &params));
        ret.resize(params);
        glsafe(glGetShaderInfoLog(handle, params, &params, ret.data()));
    }
    return ret;
}

std::string check_program(GLuint handle)
{
    std::string ret;
    GLint params;
    glsafe(glGetProgramiv(handle, GL_LINK_STATUS, &params));
    if (params == GL_FALSE)
    {
        glsafe(glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &params));
        ret.resize(params);
        glsafe(glGetProgramInfoLog(handle, params, &params, ret.data()));
    }
    return ret;
}

unsigned int init_shader(const std::string &shader_name, const char *vertex_shader, const char *fragment_shader)
{
    const GLuint vs_id = glCreateShader(GL_VERTEX_SHADER);
    glcheck();
    glsafe(glShaderSource(vs_id, 1, &vertex_shader, nullptr));
    glsafe(glCompileShader(vs_id));
    std::string res = check_shader(vs_id);
    if (!res.empty())
    {
        glsafe(glDeleteShader(vs_id));
        throw std::runtime_error("LibVGCode: Unable to compile vertex shader:\n" + shader_name + "\n" + res + "\n");
    }

    const GLuint fs_id = glCreateShader(GL_FRAGMENT_SHADER);
    glcheck();
    glsafe(glShaderSource(fs_id, 1, &fragment_shader, nullptr));
    glsafe(glCompileShader(fs_id));
    res = check_shader(fs_id);
    if (!res.empty())
    {
        glsafe(glDeleteShader(vs_id));
        glsafe(glDeleteShader(fs_id));
        throw std::runtime_error("LibVGCode: Unable to compile fragment shader:\n" + shader_name + "\n" + res + "\n");
    }

    const GLuint shader_id = glCreateProgram();
    glcheck();
    glsafe(glAttachShader(shader_id, vs_id));
    glsafe(glAttachShader(shader_id, fs_id));
    glsafe(glLinkProgram(shader_id));
    res = check_program(shader_id);
    if (!res.empty())
    {
        glsafe(glDetachShader(shader_id, vs_id));
        glsafe(glDetachShader(shader_id, fs_id));
        glsafe(glDeleteShader(vs_id));
        glsafe(glDeleteShader(fs_id));
        glsafe(glDeleteProgram(shader_id));
        throw std::runtime_error("LibVGCode: Unable to link shader program:\n" + shader_name + "\n" + res + "\n");
    }

    glsafe(glDetachShader(shader_id, vs_id));
    glsafe(glDetachShader(shader_id, fs_id));
    glsafe(glDeleteShader(vs_id));
    glsafe(glDeleteShader(fs_id));
    return shader_id;
}

static void delete_textures(unsigned int &id)
{
    if (id != 0)
    {
        glsafe(glDeleteTextures(1, &id));
        id = 0;
    }
}

static void delete_buffers(unsigned int &id)
{
    if (id != 0)
    {
        glsafe(glDeleteBuffers(1, &id));
        id = 0;
    }
}

// The list uploads and the probes read GL errors themselves: glsafe compiles out of release builds. Clears errors
// left by earlier work, so the check after a step sees only its own.
static void drain_gl_errors()
{
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i)
    {
    }
}

// The first error raised since the last drain (GL_NO_ERROR when none), draining the rest
static GLenum take_gl_error()
{
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
        drain_gl_errors();
    return error;
}

// Fills the buffer bound to GL_TEXTURE_BUFFER with the given bytes (none: an empty store) and checks that the buffer
// took them: no GL error raised and the buffer's size the bytes handed over. Returns why not, empty when it did.
static std::string upload_list_checked(const void *data, size_t bytes, GLenum usage)
{
    drain_gl_errors();
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(bytes), bytes > 0 ? data : nullptr, usage);
    // Stays -1 when the query writes nothing
    GLint64 size = -1;
    if (glGetBufferParameteri64v != nullptr)
        glGetBufferParameteri64v(GL_TEXTURE_BUFFER, GL_BUFFER_SIZE, &size);
    else
    {
        GLint size32 = -1;
        glGetBufferParameteriv(GL_TEXTURE_BUFFER, GL_BUFFER_SIZE, &size32);
        size = size32;
    }
    const GLenum error = take_gl_error();
    char reason[64];
    if (error != GL_NO_ERROR)
        std::snprintf(reason, sizeof(reason), "gl error 0x%04X", static_cast<unsigned int>(error));
    else if (size != static_cast<GLint64>(bytes))
        std::snprintf(reason, sizeof(reason), "size %lld, expected %llu", static_cast<long long>(size),
                      static_cast<unsigned long long>(bytes));
    else
        return std::string();
    return std::string(reason);
}

// Adds the wall time of its scope and the given bytes to a running load's GPU upload figures; does nothing outside a
// load (the enabled list and color uploads also run on every settings change)
class LoadUploadScope
{
public:
    LoadUploadScope(LoadPhaseStats &stats, bool load_running, size_t bytes) : m_stats(load_running ? &stats : nullptr)
    {
        if (m_stats == nullptr)
            return;
        m_stats->gl_upload_bytes += bytes;
        m_start = std::chrono::steady_clock::now();
    }
    ~LoadUploadScope()
    {
        if (m_stats != nullptr)
            m_stats->gl_upload_ms +=
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - m_start).count();
    }
    LoadUploadScope(const LoadUploadScope &) = delete;
    LoadUploadScope &operator=(const LoadUploadScope &) = delete;

private:
    LoadPhaseStats *m_stats;
    std::chrono::steady_clock::time_point m_start;
};

// Color array must match GCodeExtrusionRole enum order exactly (ExtrusionRole.hpp:122-144)
static const std::array<Color, size_t(EGCodeExtrusionRole::COUNT)> DEFAULT_EXTRUSION_ROLES_COLORS = {{
    {230, 179, 179}, // None
    {0, 255, 65},    // Serpentine (Matrix accent green #00FF41)
    {0, 102, 26},    // SerpentineOverhang (deep green, clearly darker than Serpentine)
    {255, 230, 77},  // Perimeter (yellow)
    {255, 125, 56},  // ExternalPerimeter (orange)
    {31, 31, 255},   // OverhangPerimeter (blue)
    {0, 255, 255},   // InterlockingPerimeter (cyan) - NEW!
    {176, 48, 41},   // InternalInfill (dark red)
    {150, 84, 204},  // SolidInfill (purple)
    {240, 64, 64},   // TopSolidInfill (red)
    {255, 140, 105}, // Ironing (salmon)
    {77, 128, 186},  // BridgeInfill (steel blue)
    {255, 255, 255}, // GapFill (white)
    {0, 135, 110},   // Skirt (teal)
    {0, 255, 0},     // SupportMaterial (bright green)
    {0, 128, 0},     // SupportMaterialInterface (dark green)
    {179, 227, 171}, // WipeTower (light green)
    {94, 209, 148}   // Custom (mint green)
}};

static const std::array<Color, size_t(EOptionType::COUNT)> DEFAULT_OPTIONS_COLORS{{
    {56, 72, 155},   // Travels
    {255, 255, 0},   // Wipes
    {205, 34, 214},  // Retractions
    {73, 173, 207},  // Unretractions
    {230, 230, 230}, // Seams
    {193, 190, 99},  // ToolChanges
    {218, 148, 139}, // ColorChanges
    {82, 240, 131},  // PausePrints
    {226, 210, 67}   // CustomGCodes
}};

const std::array<Color, size_t(EGCodeExtrusionRole::COUNT)> &ViewerImpl::default_extrusion_roles_colors()
{
    return DEFAULT_EXTRUSION_ROLES_COLORS;
}

// Occlusion culling: the shadow view draws the whole enabled set, with no step, while it holds at most this many
// segments. The key light turns with the camera, so the shadow view's step runs every frame of an orbit, and below
// this count drawing every enabled segment into the shadow map costs less than the step, is exact and holds for any
// light. 0 never draws it whole (safe range 1 to 4 million).
static constexpr size_t OCCLUSION_SHADOW_ALL_MAX_SEGMENTS = 3000000;

ViewerImpl::ViewerImpl() : m_occlusion_shadow_all_max(OCCLUSION_SHADOW_ALL_MAX_SEGMENTS)
{
    reset_default_extrusion_roles_colors();
    reset_default_options_colors();
    // The occlusion step's occluders: the depth program with the uniforms the shadow pass sets
    m_occlusion.set_draw_depth(
        [this](unsigned int segments_tex_id, unsigned int segments_buf_id, size_t count, const Mat4x4 &view_matrix,
               const Mat4x4 &projection_matrix, const Vec3 &camera_position)
        {
            glUseProgram(m_segments_depth_shader_id);
            glUniform1i(m_uni_depth_positions_tex_id, 0);
            glUniform1i(m_uni_depth_height_width_angle_tex_id, 1);
            glUniform1i(m_uni_depth_segment_index_tex_id, 3);
            glUniformMatrix4fv(m_uni_depth_view_matrix_id, 1, GL_FALSE, view_matrix.data());
            glUniformMatrix4fv(m_uni_depth_projection_matrix_id, 1, GL_FALSE, projection_matrix.data());
            glUniform3fv(m_uni_depth_camera_position_id, 1, camera_position.data());
            glUniform4fv(m_uni_depth_clipping_plane_id, 1, m_clipping_plane.data());
            draw_segment_list(segments_tex_id, segments_buf_id, count);
        });
}

void ViewerImpl::SegmentsUniforms::init(unsigned int shader_id)
{
    view_matrix = glGetUniformLocation(shader_id, "view_matrix");
    projection_matrix = glGetUniformLocation(shader_id, "projection_matrix");
    camera_position = glGetUniformLocation(shader_id, "camera_position");
    positions_tex = glGetUniformLocation(shader_id, "position_tex");
    height_width_angle_tex = glGetUniformLocation(shader_id, "height_width_angle_tex");
    colors_tex = glGetUniformLocation(shader_id, "color_tex");
    segment_index_tex = glGetUniformLocation(shader_id, "segment_index_tex");
    clipping_plane = glGetUniformLocation(shader_id, "clipping_plane");
    shadow_vp = glGetUniformLocation(shader_id, "shadow_vp");
    scene_passes = glGetUniformLocation(shader_id, "scene_passes");
    shadow_tex = glGetUniformLocation(shader_id, "shadow_tex");
    ao_tex = glGetUniformLocation(shader_id, "ao_tex");
    viewport_size = glGetUniformLocation(shader_id, "viewport_size");
    viewport_origin = glGetUniformLocation(shader_id, "viewport_origin");
    shadow_offset_margin = glGetUniformLocation(shader_id, "shadow_offset_margin");
    pf_viewport_px = glGetUniformLocation(shader_id, "pf_viewport_px");
    pf_width = glGetUniformLocation(shader_id, "pf_width");
    pf_fade_elevation = glGetUniformLocation(shader_id, "pf_fade_elevation");
    pf_fade_pitch = glGetUniformLocation(shader_id, "pf_fade_pitch");
    pf_fade_above = glGetUniformLocation(shader_id, "pf_fade_above");
    pf_top_z = glGetUniformLocation(shader_id, "pf_top_z");
    pf_bottom_z = glGetUniformLocation(shader_id, "pf_bottom_z");
}

void ViewerImpl::init(const std::string &opengl_context_version)
{
    if (m_initialized)
        return;

    if (!OpenGLWrapper::load_opengl(opengl_context_version))
    {
        if (OpenGLWrapper::is_valid_context())
            throw std::runtime_error("LibVGCode was unable to initialize the GLAD library.\n");
        else
        {
#if defined(__linux__) && defined(__aarch64__)
            throw std::runtime_error("LibVGCode requires an OpenGL context based on OpenGL 3.1 or higher.\n");
#else
            throw std::runtime_error("LibVGCode requires an OpenGL context based on OpenGL 3.2 or higher.\n");
#endif
        }
    }

    // segments shader
    m_segments_shader_id = init_shader("segments", Segments_Vertex_Shader, Segments_Fragment_Shader);

    // Uniforms of the visible shader, the scene-pass (Full lighting tier) shadow/AO ones included
    m_uni_segments.init(m_segments_shader_id);
    glcheck();
    assert(m_uni_segments.view_matrix != -1 && m_uni_segments.projection_matrix != -1 &&
           m_uni_segments.camera_position != -1 && m_uni_segments.positions_tex != -1 &&
           m_uni_segments.height_width_angle_tex != -1 && m_uni_segments.colors_tex != -1 &&
           m_uni_segments.segment_index_tex != -1 && m_uni_segments.clipping_plane != -1);

    // Scene passes (Full lighting tier): the G-buffer variant program.
    m_segments_gbuffer_shader_id = init_shader("segments_gbuffer", Segments_GBuffer_Vertex_Shader,
                                               Segments_GBuffer_Fragment_Shader);
    m_uni_gbuffer_view_matrix_id = glGetUniformLocation(m_segments_gbuffer_shader_id, "view_matrix");
    m_uni_gbuffer_projection_matrix_id = glGetUniformLocation(m_segments_gbuffer_shader_id, "projection_matrix");
    m_uni_gbuffer_camera_position_id = glGetUniformLocation(m_segments_gbuffer_shader_id, "camera_position");
    m_uni_gbuffer_positions_tex_id = glGetUniformLocation(m_segments_gbuffer_shader_id, "position_tex");
    m_uni_gbuffer_height_width_angle_tex_id = glGetUniformLocation(m_segments_gbuffer_shader_id,
                                                                   "height_width_angle_tex");
    m_uni_gbuffer_colors_tex_id = glGetUniformLocation(m_segments_gbuffer_shader_id, "color_tex");
    m_uni_gbuffer_segment_index_tex_id = glGetUniformLocation(m_segments_gbuffer_shader_id, "segment_index_tex");
    m_uni_gbuffer_clipping_plane_id = glGetUniformLocation(m_segments_gbuffer_shader_id, "clipping_plane");

    // Scene passes (Full lighting tier): the shadow map pass's depth-only program. A failure never stops the viewer:
    // the id stays 0, the compiler output is kept, and the shadow pass draws with the visible program
    m_segments_depth_shader_id = 0;
    m_segments_depth_shader_log.clear();
    try
    {
        m_segments_depth_shader_id = init_shader("segments_depth", Segments_Depth_Vertex_Shader,
                                                 Segments_Depth_Fragment_Shader);
    }
    catch (const std::exception &e)
    {
        m_segments_depth_shader_id = 0;
        m_segments_depth_shader_log = e.what();
    }
    catch (...)
    {
        m_segments_depth_shader_id = 0;
    }
    if (m_segments_depth_shader_id == 0)
    {
        if (m_segments_depth_shader_log.empty())
            m_segments_depth_shader_log = "LibVGCode: Unable to build shader program:\nsegments_depth\n";
    }
    else
    {
        m_uni_depth_view_matrix_id = glGetUniformLocation(m_segments_depth_shader_id, "view_matrix");
        m_uni_depth_projection_matrix_id = glGetUniformLocation(m_segments_depth_shader_id, "projection_matrix");
        m_uni_depth_camera_position_id = glGetUniformLocation(m_segments_depth_shader_id, "camera_position");
        m_uni_depth_positions_tex_id = glGetUniformLocation(m_segments_depth_shader_id, "position_tex");
        m_uni_depth_height_width_angle_tex_id = glGetUniformLocation(m_segments_depth_shader_id,
                                                                     "height_width_angle_tex");
        m_uni_depth_segment_index_tex_id = glGetUniformLocation(m_segments_depth_shader_id, "segment_index_tex");
        m_uni_depth_clipping_plane_id = glGetUniformLocation(m_segments_depth_shader_id, "clipping_plane");
        glcheck();
    }

#ifdef PREFLIGHT_TEST_HOOKS
    // Visibility probe: the ID program. A failure never stops the viewer: the id stays 0 and the probe does not run
    m_segments_id_shader_id = 0;
    try
    {
        m_segments_id_shader_id = init_shader("segments_id", Segments_Id_Vertex_Shader, Segments_Id_Fragment_Shader);
    }
    catch (...)
    {
        m_segments_id_shader_id = 0;
    }
    if (m_segments_id_shader_id != 0)
    {
        m_uni_id_view_matrix_id = glGetUniformLocation(m_segments_id_shader_id, "view_matrix");
        m_uni_id_projection_matrix_id = glGetUniformLocation(m_segments_id_shader_id, "projection_matrix");
        m_uni_id_camera_position_id = glGetUniformLocation(m_segments_id_shader_id, "camera_position");
        m_uni_id_positions_tex_id = glGetUniformLocation(m_segments_id_shader_id, "position_tex");
        m_uni_id_height_width_angle_tex_id = glGetUniformLocation(m_segments_id_shader_id, "height_width_angle_tex");
        m_uni_id_colors_tex_id = glGetUniformLocation(m_segments_id_shader_id, "color_tex");
        m_uni_id_segment_index_tex_id = glGetUniformLocation(m_segments_id_shader_id, "segment_index_tex");
        m_uni_id_clipping_plane_id = glGetUniformLocation(m_segments_id_shader_id, "clipping_plane");
        m_uni_id_instance_id_tex_id = glGetUniformLocation(m_segments_id_shader_id, "instance_id_tex");
        glcheck();
    }
#endif // PREFLIGHT_TEST_HOOKS

    // The visible segments program with the toolpath prefilter is built on the first frame that draws with it
    // (build_prefilter_program), so a driver that fails on it costs nothing while the preference is off
    m_prefilter_active = false;
    m_prefilter_reason = "no view in range";
    m_segments_pf_shader_id = 0;
    m_segments_pf_shader_failed = false;
    m_segments_pf_shader_log.clear();

    m_segment_template.init();

    // options shader
    m_options_shader_id = init_shader("options", Options_Vertex_Shader, Options_Fragment_Shader);

    m_uni_options_view_matrix_id = glGetUniformLocation(m_options_shader_id, "view_matrix");
    m_uni_options_projection_matrix_id = glGetUniformLocation(m_options_shader_id, "projection_matrix");
    m_uni_options_positions_tex_id = glGetUniformLocation(m_options_shader_id, "position_tex");
    m_uni_options_height_width_angle_tex_id = glGetUniformLocation(m_options_shader_id, "height_width_angle_tex");
    m_uni_options_colors_tex_id = glGetUniformLocation(m_options_shader_id, "color_tex");
    m_uni_options_segment_index_tex_id = glGetUniformLocation(m_options_shader_id, "segment_index_tex");
    m_uni_options_clipping_plane_id = glGetUniformLocation(m_options_shader_id, "clipping_plane");
    glcheck();
    assert(m_uni_options_view_matrix_id != -1 && m_uni_options_projection_matrix_id != -1 &&
           m_uni_options_positions_tex_id != -1 && m_uni_options_height_width_angle_tex_id != -1 &&
           m_uni_options_colors_tex_id != -1 && m_uni_options_segment_index_tex_id != -1 &&
           m_uni_options_clipping_plane_id != -1);

    m_option_template.init(16);

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    // cog marker shader
    m_cog_marker_shader_id = init_shader("cog_marker", Cog_Marker_Vertex_Shader, Cog_Marker_Fragment_Shader);

    m_uni_cog_marker_world_center_position = glGetUniformLocation(m_cog_marker_shader_id, "world_center_position");
    m_uni_cog_marker_scale_factor = glGetUniformLocation(m_cog_marker_shader_id, "scale_factor");
    m_uni_cog_marker_view_matrix = glGetUniformLocation(m_cog_marker_shader_id, "view_matrix");
    m_uni_cog_marker_projection_matrix = glGetUniformLocation(m_cog_marker_shader_id, "projection_matrix");
    glcheck();
    assert(m_uni_cog_marker_world_center_position != -1 && m_uni_cog_marker_scale_factor != -1 &&
           m_uni_cog_marker_view_matrix != -1 && m_uni_cog_marker_projection_matrix != -1);

    m_cog_marker.init(32, 1.0f);

    // tool marker shader
    m_tool_marker_shader_id = init_shader("tool_marker", Tool_Marker_Vertex_Shader, Tool_Marker_Fragment_Shader);

    m_uni_tool_marker_world_origin = glGetUniformLocation(m_tool_marker_shader_id, "world_origin");
    m_uni_tool_marker_scale_factor = glGetUniformLocation(m_tool_marker_shader_id, "scale_factor");
    m_uni_tool_marker_view_matrix = glGetUniformLocation(m_tool_marker_shader_id, "view_matrix");
    m_uni_tool_marker_projection_matrix = glGetUniformLocation(m_tool_marker_shader_id, "projection_matrix");
    m_uni_tool_marker_color_base = glGetUniformLocation(m_tool_marker_shader_id, "color_base");

    glcheck();
    assert(m_uni_tool_marker_world_origin != -1 && m_uni_tool_marker_scale_factor != -1 &&
           m_uni_tool_marker_view_matrix != -1 && m_uni_tool_marker_projection_matrix != -1 &&
           m_uni_tool_marker_color_base != -1);

    m_tool_marker.init(32, 2.0f, 4.0f, 1.0f, 8.0f);
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    m_initialized = true;
}

void ViewerImpl::shutdown()
{
    reset();
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    m_tool_marker.shutdown();
    m_cog_marker.shutdown();
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    m_option_template.shutdown();
    m_segment_template.shutdown();
    if (m_options_shader_id != 0)
    {
        glsafe(glDeleteProgram(m_options_shader_id));
        m_options_shader_id = 0;
    }
    if (m_segments_shader_id != 0)
    {
        glsafe(glDeleteProgram(m_segments_shader_id));
        m_segments_shader_id = 0;
    }
    if (m_segments_gbuffer_shader_id != 0)
    {
        glsafe(glDeleteProgram(m_segments_gbuffer_shader_id));
        m_segments_gbuffer_shader_id = 0;
    }
    if (m_segments_depth_shader_id != 0)
    {
        glsafe(glDeleteProgram(m_segments_depth_shader_id));
        m_segments_depth_shader_id = 0;
    }
#ifdef PREFLIGHT_TEST_HOOKS
    if (m_segments_id_shader_id != 0)
    {
        glsafe(glDeleteProgram(m_segments_id_shader_id));
        m_segments_id_shader_id = 0;
    }
#endif // PREFLIGHT_TEST_HOOKS
    if (m_segments_pf_shader_id != 0)
    {
        glsafe(glDeleteProgram(m_segments_pf_shader_id));
        m_segments_pf_shader_id = 0;
    }
#ifdef __APPLE__
    delete_textures(m_shadow_placeholder_tex_id);
    delete_textures(m_ao_placeholder_tex_id);
#endif // __APPLE__
    // The occlusion step's programs; reset() freed the rest of its objects
    m_occlusion.release(true);
    m_initialized = false;
    OpenGLWrapper::unload_opengl();
}

void ViewerImpl::reset()
{
    m_layers.reset();
    m_view_range.reset();
    m_extrusion_roles.reset();
    m_options.clear();
    m_used_extruders.clear();
    m_total_time = {0.0f, 0.0f};
    m_travels_time = {0.0f, 0.0f};
    m_vertices.clear();
    m_vertices_colors.clear();
    m_valid_lines_bitset.clear();
    m_layers_extent.clear();
    m_toolpaths_xy_range = {{FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX}};
    m_prefilter_neighbours = PrefilterNeighbourData();
    m_prefilter_stats = PrefilterNeighbourStats();
    m_sealed = SealedBeadData();
    m_sealed_stats = SealedBeadStats();
    m_view_chunks = ViewChunkSet();
    m_view_chunks_valid = false;
    m_view_chunk_stats = ViewChunkStats();
    reset_chunk_selection();
    m_view_index = ViewIndex();
    m_enabled_lists_deferred = false;
    m_deferred_segments = 0;
    m_sealed_total_pending = false;
    m_colors_upload_valid = false;
    m_colors_upload = ColorDarkening();
    m_print_chunks = PrintChunks();
    m_print_chunk_flags = std::vector<uint8_t>();
    m_print_chunk_stats = PrintChunkStats();
    // The occlusion step's objects and results go with the structure; a new structure starts with no prediction
    m_occlusion.release(false);
    m_occl_camera_selection = ChunkSelection();
    m_occl_shadow_selection = ChunkSelection();
    m_occlusion_filter = PrintChunkFilter();
    ++m_print_chunks_generation;
    m_print_chunks_applied_generation = 0;
    m_occlusion_stats = OcclusionStats();
    m_occlusion_stats.enabled = m_occlusion_enabled;
    // A new load counts its own failed list uploads, with no rebuild pending for an earlier one
    m_list_upload_stats = ListUploadStats();
    m_list_upload_retry = false;
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    m_cog_marker.reset();
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    m_enabled_segments_count = 0;
    m_enabled_options_count = 0;

    m_settings_used_for_ranges = std::nullopt;

    delete_textures(m_enabled_options_tex_id);
    delete_buffers(m_enabled_options_buf_id);
    delete_textures(m_enabled_segments_tex_id);
    delete_buffers(m_enabled_segments_buf_id);
    delete_textures(m_colors_tex_id);
    delete_buffers(m_colors_buf_id);
    delete_textures(m_heights_widths_angles_tex_id);
    delete_buffers(m_heights_widths_angles_buf_id);
    delete_textures(m_positions_tex_id);
    delete_buffers(m_positions_buf_id);
}

void ViewerImpl::load(GCodeInputData &&gcode_data)
{
    // A load that returns early leaves no figures of an earlier one
    m_load_stats = LoadPhaseStats();

    if (!m_initialized)
        return;

    if (gcode_data.vertices.empty())
        return;

    // The preparation reports the first half of the load's progress, the install ends it
    const std::function<void(float)> progress_callback = gcode_data.progress_callback;
    std::function<void(float)> prepare_progress;
    if (progress_callback)
        prepare_progress = [&progress_callback](float fraction)
        {
            progress_callback(0.5f * fraction);
        };
    load(prepare_load(std::move(gcode_data), get_prepare_settings(), prepare_progress));

    if (progress_callback)
    {
        progress_callback(1.0f);
    }
}

void ViewerImpl::load(PreparedLoad &&prepared)
{
    // A load that returns early leaves no figures of an earlier one
    m_load_stats = LoadPhaseStats();

    if (!m_initialized)
        return;

    // The install owns the prepared data from here: every table is moved into the viewer, the buffers' contents are
    // freed once uploaded
    std::unique_ptr<PreparedLoadData> data = prepared.take_data();
    if (data == nullptr || data->vertices.empty())
        return;

    // The install's wall time, of which the GPU uploads and the chunk build are timed apart
    const auto load_start = std::chrono::steady_clock::now();
    m_load_running = true;

    reset();

    m_vertices = std::move(data->vertices);
    m_tool_colors = std::move(data->tools_colors);
    m_color_print_colors = std::move(data->color_print_colors);

    m_settings.spiral_vase_mode = data->spiral_vase_mode;

    // The tables of the vertex scan
    m_layers = std::move(data->layers);
    m_layers_extent = std::move(data->layers_extent);
    m_toolpaths_xy_range = data->toolpaths_xy_range;
    m_total_time = data->total_time;
    m_travels_time = data->travels_time;
    m_options = std::move(data->options);
    m_extrusion_roles = std::move(data->extrusion_roles);
    m_used_extruders = std::move(data->used_extruders);
    m_valid_lines_bitset = std::move(data->valid_lines_bitset);
    // The vertices by layer and option type, which the view ranges, the deferred enabled lists and the colors read
    const auto index_start = std::chrono::steady_clock::now();
    m_view_index = build_view_index(m_vertices);
    m_load_stats.view_index_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - index_start).count();

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    // updates calculation for center of gravity
    for (size_t i = 1; i < m_vertices.size(); ++i)
    {
        const PathVertex &v = m_vertices[i];
        if (v.type == EMoveType::Extrude && v.role != EGCodeExtrusionRole::Skirt &&
            v.role != EGCodeExtrusionRole::SupportMaterial && v.role != EGCodeExtrusionRole::SupportMaterialInterface &&
            v.role != EGCodeExtrusionRole::WipeTower && v.role != EGCodeExtrusionRole::Custom)
        {
            m_cog_marker.update(0.5f * (v.position + m_vertices[i - 1].position), v.weight);
        }
    }
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    if (m_settings.time_mode != ETimeMode::Normal && m_total_time[static_cast<size_t>(m_settings.time_mode)] == 0.0f)
        m_settings.time_mode = ETimeMode::Normal;

    // The load-time passes; the per-vertex neighbour data only when kept (the buffers hold it)
    m_prefilter_neighbours = m_keep_prefilter_neighbours ? std::move(data->prefilter_neighbours)
                                                         : PrefilterNeighbourData();
    m_prefilter_stats = std::move(data->prefilter_stats);
    m_sealed = std::move(data->sealed);
    m_sealed_stats = std::move(data->sealed_stats);
    // The chunk structure of the whole print, its travel and wipe boxes rebuilt for radii changed since the preparation
    m_print_chunks = std::move(data->print_chunks);
    set_print_chunk_radii(m_print_chunks, m_vertices, m_travels_radius, m_wipes_radius);
    m_print_chunk_stats.valid = m_print_chunks.valid;
    m_print_chunk_stats.chunks = m_print_chunks.set.chunks.size();
    m_print_chunk_stats.subcells = m_print_chunks.subcells();
    m_print_chunk_stats.segments = m_print_chunks.segments();
    m_print_chunk_stats.bytes = m_print_chunks.bytes();
    m_print_chunk_stats.build_ms = data->print_chunks_ms;

    // A radius changed since the preparation: the heights and widths of travels and wipes are rewritten after the
    // upload, as a change of the radius on a loaded viewer does
    const bool radii_changed = data->settings.travels_radius != m_travels_radius ||
                               data->settings.wipes_radius != m_wipes_radius;
    const float prepare_ms = data->prepare_ms;
    m_load_stats.prepare_view_ms = data->view_ms;

    // The enabled lists and the colors the preparation built are taken when its view snapshot equals this viewer's
    // view settings (its time mode after the same fallback); what reads a setting that differs is built here, as a
    // load always did, and the difference is named in the statistics. Prepared tables not taken are freed now.
    bool lists_prepared = false;
    bool colors_prepared = false;
    if (data->lists_prepared && data->settings.view.has_value())
    {
        bool lists_differ = false;
        bool colors_differ = false;
        m_load_stats.view_settings_changed = view_settings_difference(*data->settings.view, get_view_settings(),
                                                                      lists_differ, colors_differ);
        lists_prepared = !lists_differ;
        colors_prepared = data->colors_prepared && !colors_differ;
    }
    else if (data->settings.view.has_value())
        m_load_stats.view_settings_changed = data->view_error;
    if (!lists_prepared)
    {
        data->enabled_segments = std::vector<uint32_t>();
        data->enabled_options = std::vector<uint32_t>();
        data->chunks = ListChunks();
    }
    if (!colors_prepared)
    {
        data->ranges = ColorRanges();
        data->vertices_colors = std::vector<float>();
        m_vertices_colors.resize(m_vertices.size());
    }
    m_load_stats.lists_prepared = lists_prepared;
    m_load_stats.colors_prepared = colors_prepared;

    // buffers to send to gpu
    // the last component complies with GL_RGBA32F format (both carry the toolpath prefilter's neighbour data)
    std::vector<Vec4> positions = std::move(data->positions);
    std::vector<Vec4> heights_widths_angles = std::move(data->heights_widths_angles);
    m_load_stats.install_swap_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - load_start).count();

    if (!positions.empty())
    {
        m_positions_tex_size = positions.size() * sizeof(Vec4);
        m_height_width_angle_tex_size = heights_widths_angles.size() * sizeof(Vec4);

        // The buffer creation and the positions and heights uploads below
        const LoadUploadScope upload(m_load_stats, m_load_running,
                                     m_positions_tex_size + m_height_width_angle_tex_size);

        int old_bound_texture = 0;
        glsafe(glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &old_bound_texture));

        // create and fill positions buffer (heavy, blocks the caller)
        glsafe(glGenBuffers(1, &m_positions_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_positions_buf_id));
        glsafe(glBufferData(GL_TEXTURE_BUFFER, positions.size() * sizeof(Vec4), positions.data(), GL_STATIC_DRAW));
        glsafe(glGenTextures(1, &m_positions_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_positions_tex_id));

        // create and fill height, width and angles buffer (heavy, blocks the caller)
        glsafe(glGenBuffers(1, &m_heights_widths_angles_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_heights_widths_angles_buf_id));
        glsafe(glBufferData(GL_TEXTURE_BUFFER, heights_widths_angles.size() * sizeof(Vec4),
                            heights_widths_angles.data(), GL_DYNAMIC_DRAW));
        glsafe(glGenTextures(1, &m_heights_widths_angles_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_heights_widths_angles_tex_id));

        // create (but do not fill) colors buffer (data is set in update_colors())
        glsafe(glGenBuffers(1, &m_colors_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_colors_buf_id));
        glsafe(glGenTextures(1, &m_colors_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_colors_tex_id));

        // create (but do not fill) enabled segments buffer (data is set in update_enabled_entities())
        glsafe(glGenBuffers(1, &m_enabled_segments_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_segments_buf_id));
        glsafe(glGenTextures(1, &m_enabled_segments_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_enabled_segments_tex_id));

        // create (but do not fill) enabled options buffer (data is set in update_enabled_entities())
        glsafe(glGenBuffers(1, &m_enabled_options_buf_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_options_buf_id));
        glsafe(glGenTextures(1, &m_enabled_options_tex_id));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_enabled_options_tex_id));

        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, old_bound_texture));
    }
    // The GPU holds the buffers now; their CPU copies go before the tables below allocate their own
    std::vector<Vec4>().swap(positions);
    std::vector<Vec4>().swap(heights_widths_angles);
    if (radii_changed)
        update_heights_widths();

    const auto enabled_start = std::chrono::steady_clock::now();
    if (lists_prepared)
    {
        // The view ranges of the full layer range and the enabled lists built with these settings
        m_view_range = data->view_range;
        m_settings.update_view_full_range = false;
        apply_enabled_lists(std::move(data->enabled_segments), std::move(data->enabled_options), data->segments_total,
                            data->cull, &data->chunks);
    }
    else
    {
        update_view_full_range();
        m_view_range.set_visible(m_view_range.get_enabled());
        update_enabled_entities();
    }
    const auto colors_start = std::chrono::steady_clock::now();
    m_load_stats.install_enabled_ms = std::chrono::duration<float, std::milli>(colors_start - enabled_start).count();
    if (colors_prepared)
    {
        // What update_colors() builds, from the ranges and colors built with these settings and palettes
        pad_tool_colors(m_tool_colors, m_used_extruders);
        m_ranges = std::move(data->ranges);
        m_settings_used_for_ranges = m_settings;
        m_vertices_colors = std::move(data->vertices_colors);
        // The buffer is written whole with these colors (reset() dropped what it held)
        m_settings.update_colors = false;
        update_colors_texture();
    }
    else
        update_colors();
    m_load_stats.install_colors_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - colors_start).count();

    // The passes' times are their own statistics' (the chunk build's is this load's enabled list); the CPU
    // preparation is the rest of the preparation's and the install's wall time
    m_load_running = false;
    const float load_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - load_start).count();
    m_load_stats.vertices = m_vertices.size();
    m_load_stats.sealed_ms = m_sealed_stats.ms;
    m_load_stats.pf_search_ms = m_prefilter_stats.search_ms;
    m_load_stats.chunk_build_ms = m_view_chunk_stats.build_ms;
    m_load_stats.print_chunks_ms = m_print_chunk_stats.build_ms;
    const float passes_ms = m_load_stats.sealed_ms + m_load_stats.pf_search_ms + m_load_stats.chunk_build_ms +
                            m_load_stats.print_chunks_ms;
    m_load_stats.viewer_cpu_ms = std::max(0.0f, prepare_ms + load_ms - m_load_stats.gl_upload_ms - passes_ms);
}

void ViewerImpl::update_enabled_entities()
{
    if (m_vertices.empty())
        return;

    // Sealed bead culling leaves out the extrusion beads that cannot be seen from outside the print in the displayed
    // layer range
    const bool cull = sealed_bead_culling_applies();
    // With occlusion culling on, the filter is applied to the whole print's structure first; while the occlusion step
    // serves every pass, the segment list waits for a reader that needs it
    if (m_occlusion_enabled && m_view_index.options_valid)
    {
        update_print_chunk_flags(cull);
        // The retry of a failed upload builds the segment list now: a deferred one would be built by a later pass,
        // outside the retry
        if (enabled_lists_deferrable() && !m_list_upload_retrying)
            defer_enabled_lists(cull);
        else
            build_enabled_lists(cull, false);
        return;
    }
    build_enabled_lists(cull, true);
}

void ViewerImpl::build_enabled_lists(bool cull, bool update_flags)
{
    const auto start = std::chrono::steady_clock::now();
    std::vector<uint32_t> enabled_segments;
    std::vector<uint32_t> enabled_options;
    const size_t segments_total = compute_enabled_lists(m_vertices, m_view_range, m_layers.get_view_range(), m_settings,
                                                        m_valid_lines_bitset, m_sealed, cull, enabled_segments,
                                                        enabled_options);
    m_view_update_stats.lists_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    apply_enabled_lists(std::move(enabled_segments), std::move(enabled_options), segments_total, cull, nullptr,
                        update_flags);
}

bool ViewerImpl::enabled_lists_deferrable() const
{
    return occlusion_inactive_reason() == nullptr && m_print_chunks.applied &&
           m_print_chunks_applied_generation == m_print_chunk_flags_generation;
}

void ViewerImpl::defer_enabled_lists(bool cull)
{
    // The option list with its upload is this update's list work
    const auto start = std::chrono::steady_clock::now();
    std::vector<uint32_t> enabled_options;
    enabled_options_from_index(m_view_index, m_vertices, m_view_range, m_layers.get_view_range(), m_settings,
                               enabled_options);
    ++m_view_update_stats.lists_deferred;

    // The list's statistics from the structure, which holds the same segments under the filter applied (with the sealed
    // test when culled): the count before culling is taken when the statistics are read
    m_sealed_stats.active = cull;
    m_sealed_stats.segments_drawn = m_print_chunks.enabled_total;
    m_sealed_stats.segments_total = m_print_chunks.enabled_total;
    m_sealed_total_pending = cull;
    m_deferred_segments = m_print_chunks.enabled_total;

    // No chunk set; the camera selection is made again
    m_view_chunks = ViewChunkSet();
    m_view_chunks_valid = false;
    m_camera_selection.valid = false;
    m_view_chunk_stats.chunks_total = 0;
    m_view_chunk_stats.subcells_total = 0;
    m_view_chunk_stats.build_ms = 0.0f;

    bool upload_failed = false;
    // The segment list's GPU copy is freed when the list is first deferred, and again while a rebuild for a failed
    // upload is pending
    if ((!m_enabled_lists_deferred || m_list_upload_retry) && m_enabled_segments_buf_id > 0)
    {
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_segments_buf_id));
        const std::string free_error = upload_list_checked(nullptr, 0, GL_STATIC_DRAW);
        if (!free_error.empty())
        {
            count_list_upload_failure(free_error);
            upload_failed = true;
        }
    }
    m_enabled_segments_count = 0;
    m_enabled_segments_tex_size = 0;
    m_enabled_options_count = enabled_options.size();
    m_enabled_options_tex_size = enabled_options.size() * sizeof(uint32_t);
    assert(m_enabled_options_buf_id > 0);
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_options_buf_id));
    const std::string options_error = upload_list_checked(enabled_options.data(), m_enabled_options_tex_size,
                                                          GL_STATIC_DRAW);
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));
    if (!options_error.empty())
    {
        // The options buffer holds no known list: none is drawn until the rebuild
        count_list_upload_failure(options_error);
        m_enabled_options_count = 0;
        m_enabled_options_tex_size = 0;
        upload_failed = true;
    }
    m_view_update_stats.lists_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    m_enabled_lists_deferred = true;
    m_settings.update_enabled_entities = end_enabled_list_update(upload_failed);
}

void ViewerImpl::ensure_enabled_lists()
{
    if (!m_enabled_lists_deferred)
        return;
    // A settings change since the deferral updates the lists itself, so the structure's flags are those of the current
    // settings
    build_enabled_lists(sealed_bead_culling_applies(), false);
}

size_t ViewerImpl::enabled_segments_count() const
{
    return m_enabled_lists_deferred ? m_deferred_segments : m_enabled_segments_count;
}

const SealedBeadStats &ViewerImpl::get_sealed_bead_stats() const
{
    // A deferred culled list's segments before culling: those its filter passes without the sealed test, in the
    // sub-cells flagged for that filter
    if (m_sealed_total_pending)
    {
        m_sealed_total_pending = false;
        PrintChunkFilter filter = m_occlusion_filter;
        filter.sealed = nullptr;
        try
        {
            m_sealed_stats.segments_total = count_print_chunks(m_print_chunks, m_vertices, filter, m_print_chunk_flags);
        }
        catch (...)
        {
            // Left at the culled count
        }
    }
    return m_sealed_stats;
}

void ViewerImpl::apply_enabled_lists(std::vector<uint32_t> &&enabled_segments, std::vector<uint32_t> &&enabled_options,
                                     size_t segments_total, bool cull, ListChunks *prepared_chunks, bool update_flags)
{
    m_enabled_lists_deferred = false;
    m_deferred_segments = 0;
    m_sealed_total_pending = false;
    m_sealed_stats.active = cull;
    m_sealed_stats.segments_total = segments_total;
    m_sealed_stats.segments_drawn = enabled_segments.size();

    // The chunks of this list for the per-frame selection; the full list is still uploaded below, drawn whenever
    // chunk culling does not apply
    const auto chunks_start = std::chrono::steady_clock::now();
    update_view_chunks(enabled_segments, prepared_chunks);
    m_view_update_stats.chunks_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - chunks_start).count();
    // The sub-cells of the whole print's structure that can hold a segment of this list's settings
    if (update_flags)
        update_print_chunk_flags(cull);

    const auto upload_start = std::chrono::steady_clock::now();
    bool upload_failed = false;
    m_enabled_segments_count = enabled_segments.size();
    m_enabled_options_count = enabled_options.size();

    m_enabled_segments_tex_size = enabled_segments.size() * sizeof(uint32_t);
    m_enabled_options_tex_size = enabled_options.size() * sizeof(uint32_t);
    m_view_update_stats.upload_bytes += m_enabled_segments_tex_size + m_enabled_options_tex_size;

    const LoadUploadScope upload(m_load_stats, m_load_running,
                                 m_enabled_segments_tex_size + m_enabled_options_tex_size);

    // update gpu buffer for enabled segments
    assert(m_enabled_segments_buf_id > 0);
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_segments_buf_id));
    const std::string segments_error = upload_list_checked(enabled_segments.data(), m_enabled_segments_tex_size,
                                                           GL_STATIC_DRAW);

    // update gpu buffer for enabled options
    assert(m_enabled_options_buf_id > 0);
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_enabled_options_buf_id));
    const std::string options_error = upload_list_checked(enabled_options.data(), m_enabled_options_tex_size,
                                                          GL_STATIC_DRAW);

    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));

    // A list whose buffer did not take it is not drawn: the buffer holds no known list until the rebuild
    if (!segments_error.empty())
    {
        count_list_upload_failure(segments_error);
        m_enabled_segments_count = 0;
        m_enabled_segments_tex_size = 0;
        upload_failed = true;
    }
    if (!options_error.empty())
    {
        count_list_upload_failure(options_error);
        m_enabled_options_count = 0;
        m_enabled_options_tex_size = 0;
        upload_failed = true;
    }
    m_view_update_stats.upload_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - upload_start).count();

    m_settings.update_enabled_entities = end_enabled_list_update(upload_failed);
}

void ViewerImpl::count_list_upload_failure(const std::string &reason)
{
    ++m_list_upload_stats.failures;
    m_list_upload_stats.reason = reason;
}

bool ViewerImpl::end_enabled_list_update(bool upload_failed)
{
    // The retry's frame: its first update counts it, and none of its updates leaves another rebuild pending
    if (m_list_upload_retrying)
    {
        if (m_list_upload_retry)
        {
            ++m_list_upload_stats.retries;
            m_list_upload_retry = false;
        }
        return false;
    }
    // Any other update: a failed upload leaves one rebuild pending, a good one leaves none
    m_list_upload_retry = upload_failed;
    return upload_failed;
}

void ViewerImpl::update_colors_texture()
{
    if (m_colors_buf_id == 0)
        return;

    const auto start = std::chrono::steady_clock::now();
    const auto add_time = [this, start]()
    {
        m_view_update_stats.colors_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    // Full render (all layers shown and all commands of the top layer visible) darkens nothing; otherwise the layers
    // below the top one are darkened
    const ColorDarkening darkening = color_darkening(m_layers, m_view_range, m_settings);
    // The buffer holds its last write while no color and nothing a color reads has changed since it was written whole:
    // such a change either drops it or leaves update_colors() pending, which writes it whole
    const bool known = m_colors_upload_valid && !m_settings.update_colors;
    if (known && darkening == m_colors_upload)
    {
        add_time();
        return;
    }

    assert(m_vertices_colors.size() == m_vertices.size());
    if (known && m_view_index.layers_valid && m_view_index.vertices == m_vertices.size())
    {
        // Only the vertices whose darkening changes are written, the layers between the two top layers
        const ColorInputs inputs = color_inputs();
        std::vector<float> values;
        size_t bytes = 0;
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_colors_buf_id));
        for (const std::array<size_t, 2> &span : darkening_change_spans(m_view_index, m_colors_upload, darkening))
        {
            values.resize(span[1] - span[0]);
            for (size_t i = span[0]; i < span[1]; ++i)
                values[i - span[0]] = vertex_darkened(darkening, m_vertices[i], i)
                                          ? encode_color_darkened(vertex_color(m_vertices[i], inputs),
                                                                  PREVIOUS_LAYER_DARKEN_FACTOR)
                                          : m_vertices_colors[i];
            glsafe(glBufferSubData(GL_TEXTURE_BUFFER, span[0] * sizeof(float), values.size() * sizeof(float),
                                   values.data()));
            bytes += values.size() * sizeof(float);
        }
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));
        m_colors_upload = darkening;
        m_view_update_stats.colors_bytes += bytes;
        add_time();
        return;
    }

    // A full render darkens no vertex: the uploaded colors are the vertex colors themselves, not a copy
    std::vector<float> darkened;
    if (darkening.top_layer > 0)
    {
        darkened.resize(m_vertices_colors.size());
        const ColorInputs inputs = color_inputs();
        for (size_t i = 0; i < m_vertices.size(); ++i)
        {
            if (vertex_darkened(darkening, m_vertices[i], i))
                darkened[i] = encode_color_darkened(vertex_color(m_vertices[i], inputs), PREVIOUS_LAYER_DARKEN_FACTOR);
            else
                darkened[i] = m_vertices_colors[i];
        }
    }
    const std::vector<float> &colors = darkening.top_layer > 0 ? darkened : m_vertices_colors;

    m_colors_tex_size = colors.size() * sizeof(float);

    const LoadUploadScope upload(m_load_stats, m_load_running, m_colors_tex_size);

    // update gpu buffer for colors
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_colors_buf_id));
    glsafe(glBufferData(GL_TEXTURE_BUFFER, colors.size() * sizeof(float), colors.data(), GL_STATIC_DRAW));
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));
    m_view_update_stats.colors_bytes += colors.size() * sizeof(float);
    // Written with the current colors unless an update of them is pending, which writes the buffer whole again
    m_colors_upload = darkening;
    m_colors_upload_valid = !m_settings.update_colors;
    add_time();
}

void ViewerImpl::update_colors()
{
    pad_tool_colors(m_tool_colors, m_used_extruders);

    update_color_ranges();

    // Recalculate "normal" colors of all the vertices for current view settings.
    // If some part of the preview should be rendered in dark grey, it is taken
    // care of in update_colors_texture. That is to avoid the need to recalculate
    // the "normal" color on every slider move.
    const ColorInputs inputs = color_inputs();
    for (size_t i = 0; i < m_vertices.size(); ++i)
        m_vertices_colors[i] = encode_color(vertex_color(m_vertices[i], inputs));

    // The buffer is written whole with the new colors
    m_colors_upload_valid = false;
    m_settings.update_colors = false;
    update_colors_texture();
}

void ViewerImpl::render(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix)
{
    // A rebuild left pending by a failed list upload: this frame's list updates are its one retry
    m_list_upload_retrying = m_list_upload_retry;

    if (m_settings.update_view_full_range)
        update_view_full_range();

    if (m_settings.update_enabled_entities)
        update_enabled_entities();

    if (m_settings.update_colors)
        update_colors();

    const Mat4x4 inv_view_matrix = inverse(view_matrix);
    const Vec3 camera_position = {inv_view_matrix[12], inv_view_matrix[13], inv_view_matrix[14]};
    render_segments(view_matrix, projection_matrix, camera_position);
    render_options(view_matrix, projection_matrix);

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    if (m_settings.options_visibility[size_t(EOptionType::ToolMarker)])
        render_tool_marker(view_matrix, projection_matrix);
    if (m_settings.options_visibility[size_t(EOptionType::CenterOfGravity)])
        render_cog_marker(view_matrix, projection_matrix);
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

    m_list_upload_retrying = false;
}

void ViewerImpl::set_view_type(EViewType type)
{
    m_settings.view_type = type;
    m_settings.update_colors = true;
}

void ViewerImpl::set_time_mode(ETimeMode mode)
{
    m_settings.time_mode = mode;
    m_settings.update_colors = true;
}

void ViewerImpl::set_layers_view_range(Interval::value_type min, Interval::value_type max)
{
    min = std::clamp<Interval::value_type>(min, 0, m_layers.count() - 1);
    max = std::clamp<Interval::value_type>(max, 0, m_layers.count() - 1);
    // The range in place, its view ranges current and the visible range the enabled one: the view ranges, the enabled
    // lists and the colors this call rebuilds would come out the same (every other input rebuilds them or flags the
    // rebuild itself when it changes)
    if (min == m_layers.get_view_range()[0] && max == m_layers.get_view_range()[1] &&
        !m_settings.update_view_full_range && m_view_range.get_visible() == m_view_range.get_enabled())
        return;
    m_layers.set_view_range(min, max);
    // force immediate update of the full range
    update_view_full_range();
    m_view_range.set_visible(m_view_range.get_enabled());
    m_settings.update_enabled_entities = true;
    //m_settings.update_colors = true;
    update_colors_texture();
}

void ViewerImpl::toggle_top_layer_only_view_range()
{
    m_settings.top_layer_only_view_range = !m_settings.top_layer_only_view_range;
    update_view_full_range();
    m_view_range.set_visible(m_view_range.get_enabled());
    m_settings.update_enabled_entities = true;
    //m_settings.update_colors = true;
    update_colors_texture();
}

std::vector<ETimeMode> ViewerImpl::get_time_modes() const
{
    std::vector<ETimeMode> ret;
    for (size_t i = 0; i < TIME_MODES_COUNT; ++i)
    {
        if (std::accumulate(m_vertices.begin(), m_vertices.end(), 0.0f,
                            [i](float a, const PathVertex &v) { return a + v.times[i]; }) > 0.0f)
            ret.push_back(static_cast<ETimeMode>(i));
    }
    return ret;
}

std::vector<uint8_t> ViewerImpl::get_used_extruders_ids() const
{
    std::vector<uint8_t> ret;
    ret.reserve(m_used_extruders.size());
    for (const auto &[id, colors] : m_used_extruders)
    {
        ret.emplace_back(id);
    }
    return ret;
}

size_t ViewerImpl::get_color_prints_count(uint8_t extruder_id) const
{
    const auto it = m_used_extruders.find(extruder_id);
    return (it == m_used_extruders.end()) ? 0 : it->second.size();
}

std::vector<ColorPrint> ViewerImpl::get_color_prints(uint8_t extruder_id) const
{
    const auto it = m_used_extruders.find(extruder_id);
    return (it == m_used_extruders.end()) ? std::vector<ColorPrint>() : it->second;
}

AABox ViewerImpl::get_bounding_box(const std::vector<EMoveType> &types) const
{
    return get_vertices_bounding_box(m_vertices, types);
}

AABox ViewerImpl::get_extrusion_bounding_box(const std::vector<EGCodeExtrusionRole> &roles) const
{
    return get_vertices_extrusion_bounding_box(m_vertices, roles);
}

bool ViewerImpl::is_option_visible(EOptionType type) const
{
    return m_settings.options_visibility[size_t(type)];
}

void ViewerImpl::toggle_option_visibility(EOptionType type)
{
    m_settings.options_visibility[size_t(type)] = !m_settings.options_visibility[size_t(type)];
    const Interval old_enabled_range = m_view_range.get_enabled();
    update_view_full_range();
    const Interval &new_enabled_range = m_view_range.get_enabled();
    if (old_enabled_range != new_enabled_range)
    {
        const Interval &visible_range = m_view_range.get_visible();
        if (old_enabled_range == visible_range)
            m_view_range.set_visible(new_enabled_range);
        else if (m_settings.top_layer_only_view_range && new_enabled_range[0] < visible_range[0])
            m_view_range.set_visible(new_enabled_range[0], visible_range[1]);
    }
    m_settings.update_enabled_entities = true;
    m_settings.update_colors = true;
}

bool ViewerImpl::is_extrusion_role_visible(EGCodeExtrusionRole role) const
{
    return m_settings.extrusion_roles_visibility[size_t(role)];
}

void ViewerImpl::toggle_extrusion_role_visibility(EGCodeExtrusionRole role)
{
    m_settings.extrusion_roles_visibility[size_t(role)] = !m_settings.extrusion_roles_visibility[size_t(role)];
    update_view_full_range();
    m_settings.update_enabled_entities = true;
    m_settings.update_colors = true;
}

void ViewerImpl::set_view_visible_range(Interval::value_type min, Interval::value_type max)
{
    // The view ranges current, no list rebuild pending and the requested range (clamped as set_visible() clamps it)
    // the visible one: the enabled lists and the colors this call rebuilds would come out the same
    if (!m_settings.update_view_full_range && !m_settings.update_enabled_entities)
    {
        ViewRange requested = m_view_range;
        requested.set_visible(min, max);
        if (requested.get_visible() == m_view_range.get_visible())
            return;
    }
    // force update of the full range, to avoid clamping the visible range with full old values
    // when calling m_view_range.set_visible()
    update_view_full_range();
    m_view_range.set_visible(min, max);
    update_enabled_entities();
    //m_settings.update_colors = true;
    update_colors_texture();
}

float ViewerImpl::get_estimated_time_at(size_t id) const
{
    return std::accumulate(m_vertices.begin(), m_vertices.begin() + id + 1, 0.0f, [this](float a, const PathVertex &v)
                           { return a + v.times[static_cast<size_t>(m_settings.time_mode)]; });
}

Color ViewerImpl::get_vertex_color(const PathVertex &v) const
{
    return vertex_color(v, color_inputs());
}

ViewSettings ViewerImpl::get_view_settings() const
{
    ViewSettings view;
    view.view_type = m_settings.view_type;
    view.time_mode = m_settings.time_mode;
    view.top_layer_only_view_range = m_settings.top_layer_only_view_range;
    view.options_visibility = m_settings.options_visibility;
    view.extrusion_roles_visibility = m_settings.extrusion_roles_visibility;
    view.sealed_bead_culling = m_sealed_enabled;
    view.chunk_culling = m_chunk_culling_enabled;
    view.clipping_plane = clipping_plane_active();
    view.extrusion_roles_colors = m_extrusion_roles_colors;
    view.options_colors = m_options_colors;
    for (size_t k = 0; k < COLOR_RANGE_VIEW_TYPES.size(); ++k)
        view.color_range_palettes[k] = get_color_range(COLOR_RANGE_VIEW_TYPES[k]).get_palette();
    view.tool_colors = m_tool_colors;
    view.color_print_colors = m_color_print_colors;
    return view;
}

void ViewerImpl::set_tool_colors(const Palette &colors)
{
    m_tool_colors = colors;
    m_settings.update_colors = true;
}

void ViewerImpl::set_color_print_colors(const Palette &colors)
{
    m_color_print_colors = colors;
    m_settings.update_colors = true;
}

const Color &ViewerImpl::get_extrusion_role_color(EGCodeExtrusionRole role) const
{
    return m_extrusion_roles_colors[size_t(role)];
}

void ViewerImpl::set_extrusion_role_color(EGCodeExtrusionRole role, const Color &color)
{
    m_extrusion_roles_colors[size_t(role)] = color;
    m_settings.update_colors = true;
}

void ViewerImpl::reset_default_extrusion_roles_colors()
{
    m_extrusion_roles_colors = DEFAULT_EXTRUSION_ROLES_COLORS;
    // The darkened colors read the palette: the next color update writes the buffer whole
    m_colors_upload_valid = false;
}

const Color &ViewerImpl::get_option_color(EOptionType type) const
{
    return m_options_colors[size_t(type)];
}

void ViewerImpl::set_option_color(EOptionType type, const Color &color)
{
    m_options_colors[size_t(type)] = color;
    m_settings.update_colors = true;
}

void ViewerImpl::reset_default_options_colors()
{
    m_options_colors = DEFAULT_OPTIONS_COLORS;
    // The darkened colors read the palette: the next color update writes the buffer whole
    m_colors_upload_valid = false;
}

const ColorRange &ViewerImpl::get_color_range(EViewType type) const
{
    switch (type)
    {
    case EViewType::Height:
    {
        return m_height_range;
    }
    case EViewType::Width:
    {
        return m_width_range;
    }
    case EViewType::Speed:
    {
        return m_speed_range;
    }
    case EViewType::ActualSpeed:
    {
        return m_actual_speed_range;
    }
    case EViewType::FanSpeed:
    {
        return m_fan_speed_range;
    }
    case EViewType::Temperature:
    {
        return m_temperature_range;
    }
    case EViewType::VolumetricFlowRate:
    {
        return m_volumetric_rate_range;
    }
    case EViewType::ActualVolumetricFlowRate:
    {
        return m_actual_volumetric_rate_range;
    }
    case EViewType::LayerTimeLinear:
    {
        return m_layer_time_range[0];
    }
    case EViewType::LayerTimeLogarithmic:
    {
        return m_layer_time_range[1];
    }
    default:
    {
        return ColorRange::DUMMY_COLOR_RANGE;
    }
    }
}

void ViewerImpl::set_color_range_palette(EViewType type, const Palette &palette)
{
    switch (type)
    {
    case EViewType::Height:
    {
        m_height_range.set_palette(palette);
        break;
    }
    case EViewType::Width:
    {
        m_width_range.set_palette(palette);
        break;
    }
    case EViewType::Speed:
    {
        m_speed_range.set_palette(palette);
        break;
    }
    case EViewType::ActualSpeed:
    {
        m_actual_speed_range.set_palette(palette);
        break;
    }
    case EViewType::FanSpeed:
    {
        m_fan_speed_range.set_palette(palette);
        break;
    }
    case EViewType::Temperature:
    {
        m_temperature_range.set_palette(palette);
        break;
    }
    case EViewType::VolumetricFlowRate:
    {
        m_volumetric_rate_range.set_palette(palette);
        break;
    }
    case EViewType::ActualVolumetricFlowRate:
    {
        m_actual_volumetric_rate_range.set_palette(palette);
        break;
    }
    case EViewType::LayerTimeLinear:
    {
        m_layer_time_range[0].set_palette(palette);
        break;
    }
    case EViewType::LayerTimeLogarithmic:
    {
        m_layer_time_range[1].set_palette(palette);
        break;
    }
    default:
    {
        break;
    }
    }
    m_settings.update_colors = true;
    // Force re-computation of color ranges (bands depend on palette size)
    m_settings_used_for_ranges.reset();
}

void ViewerImpl::set_travels_radius(float radius)
{
    m_travels_radius = std::clamp(radius, MIN_TRAVELS_RADIUS_MM, MAX_TRAVELS_RADIUS_MM);
    update_heights_widths();
    // The boxes of the whole print's structure bound the travels at the radius drawn; the occlusion step uploads them
    // again and makes its results again
    set_print_chunk_radii(m_print_chunks, m_vertices, m_travels_radius, m_wipes_radius);
    ++m_print_chunk_boxes_generation;
}

void ViewerImpl::set_wipes_radius(float radius)
{
    m_wipes_radius = std::clamp(radius, MIN_WIPES_RADIUS_MM, MAX_WIPES_RADIUS_MM);
    update_heights_widths();
    // The boxes of the whole print's structure bound the wipes at the radius drawn; the occlusion step uploads them
    // again and makes its results again
    set_print_chunk_radii(m_print_chunks, m_vertices, m_travels_radius, m_wipes_radius);
    ++m_print_chunk_boxes_generation;
}

size_t ViewerImpl::get_used_cpu_memory() const
{
    size_t ret = sizeof(*this);
    ret += m_layers.size_in_bytes_cpu();
    ret += STDVEC_MEMSIZE(m_options, EOptionType);
    ret += m_used_extruders.size() * sizeof(std::map<uint8_t, ColorPrint>::value_type);
    ret += sizeof(m_extrusion_roles_colors);
    ret += sizeof(m_options_colors);
    ret += STDVEC_MEMSIZE(m_vertices, PathVertex);
    ret += m_valid_lines_bitset.size_in_bytes_cpu();
    ret += m_height_range.size_in_bytes_cpu();
    ret += m_width_range.size_in_bytes_cpu();
    ret += m_speed_range.size_in_bytes_cpu();
    ret += m_actual_speed_range.size_in_bytes_cpu();
    ret += m_fan_speed_range.size_in_bytes_cpu();
    ret += m_temperature_range.size_in_bytes_cpu();
    ret += m_volumetric_rate_range.size_in_bytes_cpu();
    ret += m_actual_volumetric_rate_range.size_in_bytes_cpu();
    for (size_t i = 0; i < COLOR_RANGE_TYPES_COUNT; ++i)
    {
        ret += m_layer_time_range[i].size_in_bytes_cpu();
    }
    ret += STDVEC_MEMSIZE(m_tool_colors, Color);
    ret += STDVEC_MEMSIZE(m_color_print_colors, Color);
    ret += STDVEC_MEMSIZE(m_prefilter_neighbours.offset_x, float);
    ret += STDVEC_MEMSIZE(m_prefilter_neighbours.offset_y, float);
    ret += STDVEC_MEMSIZE(m_prefilter_neighbours.flags, uint8_t);
    ret += STDVEC_MEMSIZE(m_sealed.touches_outside, uint8_t);
    ret += STDVEC_MEMSIZE(m_sealed.cavity_lo, uint32_t);
    ret += STDVEC_MEMSIZE(m_sealed.cavity_hi, uint32_t);
    ret += STDVEC_MEMSIZE(m_view_chunks.order, uint32_t);
    ret += STDVEC_MEMSIZE(m_view_chunks.chunks, ViewChunk);
    ret += STDVEC_MEMSIZE(m_view_chunks.subcell_first, uint32_t);
    ret += STDVEC_MEMSIZE(m_camera_selection.segments, uint32_t);
    ret += m_print_chunks.bytes();
    ret += STDVEC_MEMSIZE(m_print_chunk_flags, uint8_t);
    ret += m_view_index.size_in_bytes();
    // The occlusion step's tables and scratch (none while it is off); its draw sets live on the GPU alone
    ret += m_occlusion.cpu_bytes();
    return ret;
}

size_t ViewerImpl::get_used_gpu_memory() const
{
    size_t ret = 0;
    ret += m_segment_template.size_in_bytes_gpu();
    ret += m_option_template.size_in_bytes_gpu();
#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    ret += m_tool_marker.size_in_bytes_gpu();
    ret += m_cog_marker.size_in_bytes_gpu();
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS
    ret += m_positions_tex_size;
    ret += m_height_width_angle_tex_size;
    ret += m_colors_tex_size;
    ret += m_enabled_segments_tex_size;
    ret += m_enabled_options_tex_size;
    ret += m_camera_selection.tex_size;
    // The occlusion step's boxes, flags, targets and lists (none while it is off)
    ret += m_occlusion.gpu_bytes();
    return ret;
}

void ViewerImpl::update_view_full_range()
{
    const auto start = std::chrono::steady_clock::now();
    compute_view_full_range(m_vertices, m_view_index, m_layers.get_view_range(), m_settings, m_view_range);
    m_settings.update_view_full_range = false;
    m_view_update_stats.view_range_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ViewerImpl::update_color_ranges()
{
    // Color ranges do not need to be recalculated that often. If the following settings are the same
    // as last time, the current ranges are still valid. The recalculation is quite expensive.
    if (m_settings_used_for_ranges.has_value() &&
        m_settings.extrusion_roles_visibility == m_settings_used_for_ranges->extrusion_roles_visibility &&
        m_settings.options_visibility == m_settings_used_for_ranges->options_visibility)
        return;

    m_ranges.build(m_vertices, m_layers, m_settings);
    m_settings_used_for_ranges = m_settings;
}

void ViewerImpl::update_heights_widths()
{
    if (m_heights_widths_angles_buf_id == 0)
        return;

    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, m_heights_widths_angles_buf_id));

    // One RGBA32F texel per vertex; only the height and width of travels and wipes change
    Vec4 *buffer = static_cast<Vec4 *>(glMapBuffer(GL_TEXTURE_BUFFER, GL_WRITE_ONLY));
    glcheck();

    for (size_t i = 0; i < m_vertices.size(); ++i)
    {
        const PathVertex &v = m_vertices[i];
        if (v.is_travel())
        {
            buffer[i][0] = m_travels_radius;
            buffer[i][1] = m_travels_radius;
        }
        else if (v.is_wipe())
        {
            buffer[i][0] = m_wipes_radius;
            buffer[i][1] = m_wipes_radius;
        }
    }

    glsafe(glUnmapBuffer(GL_TEXTURE_BUFFER));
    glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));
}

void ViewerImpl::draw_enabled_segments(const ChunkSelection *selection)
{
    // A chunk selection draws its own segment list in place of the enabled one, unless it kept the whole list
    if (selection != nullptr && !selection->use_base)
        draw_segment_list(selection->tex_id, selection->buf_id, selection->segment_count());
    else
        draw_segment_list(m_enabled_segments_tex_id, m_enabled_segments_buf_id, m_enabled_segments_count);
}

void ViewerImpl::draw_segment_list(unsigned int segments_tex_id, unsigned int segments_buf_id, size_t segments_count)
{
    if (segments_count == 0)
        return;

    std::array<int, 4> curr_bound_texture = {0, 0, 0, 0};
    for (int i = 0; i < curr_bound_texture.size(); ++i)
    {
        glsafe(glActiveTexture(GL_TEXTURE0 + i));
        glsafe(glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &curr_bound_texture[i]));
        //assert(curr_bound_texture[i] == 0);
    }

    glsafe(glActiveTexture(GL_TEXTURE0));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_positions_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_positions_buf_id));
    glsafe(glActiveTexture(GL_TEXTURE1));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_heights_widths_angles_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_heights_widths_angles_buf_id));
    glsafe(glActiveTexture(GL_TEXTURE2));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_colors_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, m_colors_buf_id));
    glsafe(glActiveTexture(GL_TEXTURE3));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, segments_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, segments_buf_id));

    // The programs write the plane's distance to clip distance 0; the rasterizer clips on it only while a plane is set
    const bool clipping = clipping_plane_active();
    const bool curr_clip_distance = glIsEnabled(GL_CLIP_DISTANCE0);
    if (clipping && !curr_clip_distance)
        glsafe(glEnable(GL_CLIP_DISTANCE0));

    m_segment_template.render(segments_count, clipping);

    if (clipping && !curr_clip_distance)
        glsafe(glDisable(GL_CLIP_DISTANCE0));

    for (int i = 0; i < curr_bound_texture.size(); ++i)
    {
        glsafe(glActiveTexture(GL_TEXTURE0 + i));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, curr_bound_texture[i]));
    }
}

// Per-sample shading is used only in frames where some visible toolpath's cross-section can project below this
// many pixels. Plain MSAA still aliases the per-vertex shading of 4 to 8 px sections measurably, and the bound
// is conservative, so 8 keeps the switch from showing when zooming in (safe range 4 to 12).
static constexpr float SAMPLE_SHADING_MAX_SECTION_PX = 8.0f;

// Per-sample shading is used only while the target's samples times its viewport pixels stay at or below this many
// sample shadings per frame. It multiplies the toolpath fragment work by the sample count, and above this bound the
// frame cost outweighs the smoother shading (safe range 8e6 to 24e6).
static constexpr double SAMPLE_SHADING_MAX_SAMPLES = 16e6;

// Toolpath prefilter: width, in output pixels, of the box across the line that the wall shading is averaged over
// (safe range 1.5 to 2.5). The strength is full up to the first bead view elevation (degrees) and off from the
// second, and likewise over the on-screen layer pitch (output pixels). Beads whose neighbour above or below was
// found use the higher elevations when the camera is above them.
static constexpr float PREFILTER_WIDTH_PX = 2.0f;
static constexpr std::array<float, 2> PREFILTER_FADE_ELEVATION_DEG = {6.0f, 12.0f};
static constexpr std::array<float, 2> PREFILTER_FADE_ABOVE_DEG = {45.0f, 55.0f};
static constexpr std::array<float, 2> PREFILTER_FADE_PITCH_PX = {8.0f, 12.0f};

// Full lighting tier: how far, in shadow-map texels, past a wall bead's side the toolpaths take their shadow lookup,
// so the lookup clears the copy of the bead that the shadow pass drew facing the light (safe range 1 to 2)
static constexpr float SHADOW_OFFSET_MARGIN_TEXELS = 1.5f;

void ViewerImpl::set_output_pixel_scale(float scale)
{
    // Anything but a finite positive scale means no supersampling
    m_output_pixel_scale = (std::isfinite(scale) && scale > 0.0f) ? scale : 1.0f;
}

std::optional<ViewerImpl::ToolpathsBox> ViewerImpl::visible_toolpaths_box() const
{
    // Smallest and largest tube size and the z range over the visible layers, with the travel and wipe tubes
    // when they are shown
    float min_size = FLT_MAX;
    float max_size = 0.0f;
    float min_z = FLT_MAX;
    float max_z = -FLT_MAX;
    const Interval &range = m_layers.get_view_range();
    for (size_t i = range[0]; i <= range[1] && i < m_layers_extent.size(); ++i)
    {
        const LayerExtent &extent = m_layers_extent[i];
        min_size = std::min(min_size, extent.min_size);
        max_size = std::max(max_size, extent.max_size);
        min_z = std::min(min_z, extent.min_z);
        max_z = std::max(max_z, extent.max_z);
    }
    if (m_settings.options_visibility[size_t(EOptionType::Travels)])
    {
        min_size = std::min(min_size, m_travels_radius);
        max_size = std::max(max_size, m_travels_radius);
    }
    if (m_settings.options_visibility[size_t(EOptionType::Wipes)])
    {
        min_size = std::min(min_size, m_wipes_radius);
        max_size = std::max(max_size, m_wipes_radius);
    }
    if (min_size == FLT_MAX || min_z > max_z || m_toolpaths_xy_range[0] > m_toolpaths_xy_range[2])
        return std::nullopt;

    // Corners of the box holding every visible tube
    const float pad = max_size;
    ToolpathsBox box;
    box.xs = {m_toolpaths_xy_range[0] - pad, m_toolpaths_xy_range[2] + pad};
    box.ys = {m_toolpaths_xy_range[1] - pad, m_toolpaths_xy_range[3] + pad};
    box.zs = {min_z - pad, max_z + pad};
    box.min_size = min_size;
    box.max_size = max_size;
    return box;
}

float ViewerImpl::min_cross_section_px(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix,
                                       const Vec3 &camera_position, int viewport_width, int viewport_height) const
{
    const std::optional<ToolpathsBox> box = visible_toolpaths_box();
    if (!box.has_value())
        return FLT_MAX;
    const float min_size = box->min_size;
    const float max_size = box->max_size;
    const std::array<float, 2> &xs = box->xs;
    const std::array<float, 2> &ys = box->ys;
    const std::array<float, 2> &zs = box->zs;

    // Pixels per unit at unit eye depth (perspective) or per unit (orthographic)
    const float focal = 0.5f * std::min(projection_matrix[0] * float(viewport_width),
                                        projection_matrix[5] * float(viewport_height));
    // Whatever the view angle, the imposter takes the configuration whose corners lie at least
    // h w / sqrt(h^2 + w^2) >= 0.707 min(h, w) apart across the ray to it, and perspective projects that span
    // at depth d to at least focal / d pixels
    if (projection_matrix[15] == 0.0f)
    {
        float depth = 0.0f;
        for (float x : xs)
            for (float y : ys)
                for (float z : zs)
                    depth = std::max(depth, -(view_matrix[2] * x + view_matrix[6] * y + view_matrix[10] * z +
                                              view_matrix[14]));
        const float near_z = projection_matrix[14] / (projection_matrix[10] - 1.0f);
        return 0.7f * min_size * focal / std::max(depth, near_z);
    }
    // Orthographic: the imposter still picks its configuration from the ray to the camera position, which
    // turns from the view direction by up to alpha over the box; the span across the view direction then
    // loses at most 4 max(h, w) sin(alpha / 2)
    const Vec3 view_dir = {-view_matrix[2], -view_matrix[6], -view_matrix[10]};
    float max_sin_half = 0.0f;
    for (float x : xs)
        for (float y : ys)
            for (float z : zs)
            {
                const Vec3 ray = {x - camera_position[0], y - camera_position[1], z - camera_position[2]};
                const float len = std::sqrt(dot(ray, ray));
                const float cos_alpha = len > 0.0f ? std::clamp(dot(ray, view_dir) / len, -1.0f, 1.0f) : -1.0f;
                max_sin_half = std::max(max_sin_half, std::sqrt(0.5f * (1.0f - cos_alpha)));
            }
    return focal * (0.7f * min_size - 4.0f * max_size * max_sin_half);
}

bool ViewerImpl::prefilter_can_fire(const Vec3 &camera_position) const
{
    const std::optional<ToolpathsBox> box = visible_toolpaths_box();
    if (!box.has_value())
        return false;

    // Height of the camera above or below the box; within its z range a toolpath can be level with the camera
    const float camera_z = camera_position[2];
    const float dz = camera_z < box->zs[0] ? box->zs[0] - camera_z
                                           : (camera_z > box->zs[1] ? camera_z - box->zs[1] : 0.0f);
    if (dz == 0.0f)
        return true;

    // Every point of the box lies at least dz above or below the camera and no farther than the farthest corner,
    // so the camera sees it at an elevation of at least asin(dz / farthest corner distance); the half degree
    // covers rounding. From above the box, beads with a found neighbour stay filtered up to the higher elevation;
    // from below, and when the search found none, every bead only near level.
    float max_dist_sq = 0.0f;
    for (float x : box->xs)
        for (float y : box->ys)
            for (float z : box->zs)
            {
                const Vec3 ray = {x - camera_position[0], y - camera_position[1], z - camera_position[2]};
                max_dist_sq = std::max(max_dist_sq, dot(ray, ray));
            }
    const bool camera_above = camera_z > box->zs[1];
    const float max_elevation = (camera_above && m_prefilter_stats.found_vertices > 0)
                                    ? PREFILTER_FADE_ABOVE_DEG[1]
                                    : PREFILTER_FADE_ELEVATION_DEG[1];
    const float sin_max_elevation = std::sin((max_elevation + 0.5f) * PI / 180.0f);
    return dz < std::sqrt(max_dist_sq) * sin_max_elevation;
}

std::array<float, 2> ViewerImpl::displayed_bead_tops() const
{
    std::array<float, 2> tops = {FLT_MAX, -FLT_MAX};
    const Interval &range = m_layers.get_view_range();
    if (range[0] > range[1] || m_layers_extent.empty())
        return tops;
    const size_t first = range[0];
    const size_t last = std::min<size_t>(range[1], m_layers_extent.size() - 1);
    for (size_t i = first; i <= last; ++i)
    {
        if (m_layers_extent[i].min_bead_z <= m_layers_extent[i].max_bead_z)
        {
            tops[0] = m_layers_extent[i].min_bead_z;
            break;
        }
    }
    for (size_t i = last + 1; i-- > first;)
    {
        if (m_layers_extent[i].min_bead_z <= m_layers_extent[i].max_bead_z)
        {
            tops[1] = m_layers_extent[i].max_bead_z;
            break;
        }
    }
    return tops;
}

bool ViewerImpl::sealed_bead_culling_applies() const
{
    return sealed_culling_applies(m_sealed_enabled, clipping_plane_active(), m_vertices.size(), m_sealed,
                                  m_extrusion_roles, m_settings);
}

void ViewerImpl::set_sealed_bead_culling(bool enable)
{
    if (m_sealed_enabled == enable)
        return;
    m_sealed_enabled = enable;
    m_settings.update_enabled_entities = true;
}

PrefilterVertexView ViewerImpl::vertex_view() const
{
    return path_vertex_view(m_vertices);
}

void ViewerImpl::set_chunk_culling(bool enable)
{
    if (m_chunk_culling_enabled == enable)
        return;
    m_chunk_culling_enabled = enable;
    // The chunk set is built with the enabled list
    m_settings.update_enabled_entities = true;
}

void ViewerImpl::update_view_chunks(const std::vector<uint32_t> &enabled_segments, ListChunks *prepared)
{
    m_view_chunks = ViewChunkSet();
    m_view_chunks_valid = false;
    m_camera_selection.valid = false;
    m_view_chunk_stats.chunks_total = 0;
    m_view_chunk_stats.subcells_total = 0;
    m_view_chunk_stats.build_ms = 0.0f;
    if (!m_chunk_culling_enabled || m_vertices.empty() || enabled_segments.empty())
        return;

    // A failed build leaves chunk culling off for this list: the full list is drawn
    ListChunks built = (prepared != nullptr && prepared->built) ? std::move(*prepared)
                                                                : build_list_chunks(m_vertices, enabled_segments);
    m_view_chunks = std::move(built.chunks);
    m_view_chunks_valid = built.valid;
    m_view_chunk_stats.chunks_total = m_view_chunks.chunks.size();
    m_view_chunk_stats.subcells_total = m_view_chunks.subcell_first.empty() ? 0
                                                                            : m_view_chunks.subcell_first.size() - 1;
    m_view_chunk_stats.build_ms = built.build_ms;
}

void ViewerImpl::reset_chunk_selection()
{
    delete_textures(m_camera_selection.tex_id);
    delete_buffers(m_camera_selection.buf_id);
    m_camera_selection = ChunkSelection();
}

PrintChunkFilter ViewerImpl::print_chunk_filter() const
{
    return make_print_chunk_filter(m_vertices, m_view_range, m_layers.get_view_range(), m_settings,
                                   m_valid_lines_bitset);
}

void ViewerImpl::update_print_chunk_flags(bool cull)
{
    m_print_chunk_stats.filter_ms = 0.0f;
    m_print_chunk_stats.candidate_subcells = 0;
    // A result of the occlusion step holds for the flags and the emission filter it was made with
    ++m_print_chunk_flags_generation;
    m_occlusion_filter = PrintChunkFilter();
    if (!m_print_chunks.valid)
    {
        m_print_chunk_flags = std::vector<uint8_t>();
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    const PrintChunkFilter filter = print_chunk_filter();
    // The occlusion step emits what this list holds: with the sealed bead test of the list when it was culled
    m_occlusion_filter = cull ? with_sealed_beads(filter, m_sealed, m_layers.get_view_range()) : filter;
    try
    {
        m_print_chunk_stats.candidate_subcells = print_chunk_flags(m_print_chunks, filter, m_print_chunk_flags);
    }
    catch (...)
    {
        // Out of memory: no flags, which the statistics show as no candidate
        m_print_chunk_flags = std::vector<uint8_t>();
    }
    m_print_chunk_stats.filter_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_view_update_stats.filter_ms += m_print_chunk_stats.filter_ms;
    // The structure takes the filter only while occlusion culling is on; switched on later, the first pass applies it
    if (m_occlusion_enabled)
        apply_occlusion_filter();
}

bool ViewerImpl::apply_occlusion_filter()
{
    if (!m_print_chunks.valid || m_occlusion_filter.valid_lines == nullptr)
        return false;
    if (m_print_chunks.applied && m_print_chunks_applied_generation == m_print_chunk_flags_generation)
        return true;
    const auto start = std::chrono::steady_clock::now();
    bool applied = false;
    try
    {
        m_print_chunk_stats.applied_subcells =
            apply_print_chunk_filter(m_print_chunks, m_vertices, m_occlusion_filter).subcells;
        m_print_chunks_applied_generation = m_print_chunk_flags_generation;
        applied = true;
    }
    catch (...)
    {
        // Out of memory: the structure is left not applied, and the passes draw as with occlusion culling off
        m_print_chunks_applied_generation = 0;
        m_print_chunk_stats.applied_subcells = 0;
    }
    m_print_chunk_stats.apply_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_print_chunk_stats.filter_ms += m_print_chunk_stats.apply_ms;
    m_view_update_stats.filter_ms += m_print_chunk_stats.apply_ms;
    return applied;
}

void ViewerImpl::reset_occlusion_bench()
{
    m_occlusion_stats.camera_bench = OcclusionBenchStats();
    m_occlusion_stats.shadow_bench = OcclusionBenchStats();
}

void ViewerImpl::set_occlusion_culling(bool enable)
{
    m_occlusion_enabled = enable;
    m_occlusion_stats.enabled = enable;
    if (enable)
        return;
    // A deferred segment list is built by the next render, before its passes, as with occlusion culling off throughout
    if (m_enabled_lists_deferred)
        m_settings.update_enabled_entities = true;
    // Off: the passes draw as before; the draw sets' lists go now, the culler's GPU objects on the next visible pass
    for (ChunkSelection *selection : {&m_occl_camera_selection, &m_occl_shadow_selection})
        *selection = ChunkSelection();
    m_occlusion_stats.camera = OcclusionViewStats();
    m_occlusion_stats.shadow = OcclusionViewStats();
}

const char *ViewerImpl::occlusion_inactive_reason() const
{
    if (!m_occlusion_enabled)
        return "off";
    if (!m_print_chunks.valid || m_print_chunks.subcells() == 0 ||
        m_print_chunk_flags.size() != m_print_chunks.subcells() || m_occlusion_filter.valid_lines == nullptr)
        return "no structure";
    // The caps a clipping plane draws lie outside the sub-cell boxes
    if (clipping_plane_active())
        return "clipping plane";
    if (m_segments_depth_shader_id == 0 || m_occlusion.programs_failed())
        return "program failed";
    return nullptr;
}

OcclusionInputs ViewerImpl::occlusion_inputs() const
{
    OcclusionInputs inputs;
    inputs.chunks = &m_print_chunks;
    inputs.structure_generation = m_print_chunks_generation;
    inputs.boxes_generation = m_print_chunk_boxes_generation;
    inputs.filter_generation = m_print_chunk_flags_generation;
    return inputs;
}

const ViewerImpl::ChunkSelection *ViewerImpl::occlusion_selection(size_t view, const Mat4x4 &view_matrix,
                                                                  const Mat4x4 &projection_matrix,
                                                                  const Vec3 &camera_position)
{
    OcclusionViewStats &stats = view == OCCLUSION_VIEW_CAMERA ? m_occlusion_stats.camera : m_occlusion_stats.shadow;
    ChunkSelection &selection = view == OCCLUSION_VIEW_CAMERA ? m_occl_camera_selection : m_occl_shadow_selection;
    m_occlusion_stats.enabled = m_occlusion_enabled;
    if (const char *reason = occlusion_inactive_reason(); reason != nullptr)
    {
        stats = OcclusionViewStats();
        stats.reason = reason;
        selection.valid = false;
        return nullptr;
    }
    // The structure partitioned for the current filter (a no-op unless the filter changed while the step was off)
    if (!apply_occlusion_filter())
    {
        stats = OcclusionViewStats();
        stats.reason = "out of memory";
        selection.valid = false;
        return nullptr;
    }
    // The view as the pass draws it, at the pass's viewport. The camera view's position is derived from the view
    // matrix as render() derives it, whatever position the pass was given, so the G-buffer and the visible pass of a
    // frame look the result up by the same view.
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    OcclusionViewParams params;
    params.view = view_matrix;
    params.projection = projection_matrix;
    if (view == OCCLUSION_VIEW_CAMERA)
    {
        const Mat4x4 inv_view_matrix = inverse(view_matrix);
        params.camera = {inv_view_matrix[12], inv_view_matrix[13], inv_view_matrix[14]};
    }
    else
        params.camera = camera_position;
    params.width = viewport[2];
    params.height = viewport[3];
    params.frame = m_occlusion_frame;
    OcclusionDrawList list;
    bool cached = false;
    OcclusionBenchStats &bench = view == OCCLUSION_VIEW_CAMERA ? m_occlusion_stats.camera_bench
                                                               : m_occlusion_stats.shadow_bench;
    const OcclusionInputs inputs = occlusion_inputs();
    // The shadow view draws the whole enabled set while it is small (the structure's count under the filter applied):
    // that costs less than the step the turning light would run every frame. A failed write falls back to the step,
    // and its reason stands only when the step fails too.
    const bool try_all = view == OCCLUSION_VIEW_SHADOW && m_occlusion_shadow_all_max > 0 &&
                         m_print_chunks.enabled_total <= m_occlusion_shadow_all_max;
    const bool all = try_all && m_occlusion.request_all(view, inputs, list, cached, stats, bench);
    // The shadow view's step also writes the residual its pass draws after merging the step's depth
    const bool want_residual = view == OCCLUSION_VIEW_SHADOW && m_occlusion_shadow_merge;
    if (!all)
    {
        const std::string all_reason = try_all ? stats.reason : std::string();
        if (!m_occlusion.request(view, inputs, params, want_residual, list, cached, stats, bench))
        {
            if (try_all)
                stats.reason = all_reason;
            // The pass draws as with occlusion culling off
            selection.valid = false;
            return nullptr;
        }
        ++(cached ? m_occlusion_stats.cached : m_occlusion_stats.steps);
    }
    if (!cached && stats.map_fallbacks > 0)
    {
        m_occlusion_stats.map_fallbacks += stats.map_fallbacks;
        m_occlusion_stats.map_fallback_reason = stats.map_fallback_reason;
    }
    selection.valid = true;
    selection.occlusion = true;
    selection.occlusion_segments = list.count;
    selection.use_base = false;
    selection.buf_id = list.buf_id;
    selection.tex_id = list.tex_id;
    selection.tex_size = 0;
    selection.chunks = list.chunks;
    selection.select_ms = stats.ms;
    // After a step of this call the shadow map takes the step's occluder depth and the pass draws only the residual.
    // When that fails the pass draws the draw set, which is exact whatever part of the depth the merge wrote: every
    // occluder is an enabled bead, never nearer than the nearest one, which the draw set holds.
    if (want_residual && !all && !cached)
    {
        std::string reason = list.residual_reason;
        if (list.depth_ready && m_occlusion.merge_depth(view, viewport, reason))
        {
            selection.occlusion_segments = list.residual_count;
            selection.buf_id = list.residual_buf_id;
            selection.tex_id = list.residual_tex_id;
            selection.chunks = list.residual_chunks;
            stats.merged = true;
            ++bench.merged;
            bench.residual_segments += list.residual_count;
        }
        else
        {
            ++m_occlusion_stats.shadow_merge_fallbacks;
            m_occlusion_stats.shadow_merge_reason = reason.empty() ? std::string("no residual") : reason;
        }
    }
    return &selection;
}

// Whether a selection made with a still holds for b: the same view-projection
static bool same_cull_view(const ViewCullParams &a, const ViewCullParams &b)
{
    for (int i = 0; i < 16; ++i)
        if (a.view_proj[i] != b.view_proj[i])
            return false;
    return true;
}

const ViewerImpl::ChunkSelection *ViewerImpl::select_chunks(ChunkSelection &selection, const Mat4x4 &view_matrix,
                                                            const Mat4x4 &projection_matrix)
{
    if (!m_chunk_culling_enabled || !m_view_chunks_valid)
        return nullptr;

    ViewCullParams params{};
    // Column-major projection * view
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += projection_matrix[k * 4 + r] * view_matrix[c * 4 + k];
            params.view_proj[c * 4 + r] = sum;
        }

    if (selection.valid && same_cull_view(selection.params, params))
        return &selection;

    // No selection: the pass draws the full enabled list
    const auto drop_selection = [&selection]()
    {
        selection.valid = false;
        selection.segments.clear();
        selection.chunks = 0;
        selection.use_base = false;
    };

    const auto start = std::chrono::steady_clock::now();
    try
    {
        selection.segments.clear();
        selection.chunks = select_view_chunks(m_view_chunks, params, selection.segments);
    }
    catch (...)
    {
        drop_selection();
        return nullptr;
    }

    // Every chunk kept: the selection holds the whole enabled list (in chunk order), which the enabled buffer already
    // holds, so the pass draws that and nothing is uploaded
    selection.use_base = selection.chunks == m_view_chunks.chunks.size() &&
                         selection.segments.size() == m_enabled_segments_count;
    if (!selection.use_base)
    {
        // Upload the selected segments, which the pass draws in place of the enabled list
        if (selection.buf_id == 0)
            glsafe(glGenBuffers(1, &selection.buf_id));
        if (selection.tex_id == 0)
            glsafe(glGenTextures(1, &selection.tex_id));
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, selection.buf_id));
        selection.tex_size = selection.segments.size() * sizeof(uint32_t);
        const std::string upload_error = upload_list_checked(selection.segments.data(), selection.tex_size,
                                                             GL_DYNAMIC_DRAW);
        glsafe(glBindBuffer(GL_TEXTURE_BUFFER, 0));
        if (!upload_error.empty())
        {
            // The buffer holds no known list: the next pass selects and uploads again
            count_list_upload_failure(upload_error);
            selection.tex_size = 0;
            drop_selection();
            return nullptr;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    selection.select_ms = std::chrono::duration<float, std::milli>(end - start).count();
    selection.params = params;
    selection.valid = true;
    return &selection;
}

#ifdef PREFLIGHT_TEST_HOOKS
// Visibility probe: the class of a pixel, the class of the sub-cell that owns it
static constexpr uint8_t PROBE_NO_TOOLPATH = 0;
static constexpr uint8_t PROBE_DRAWN = 1;
static constexpr uint8_t PROBE_CHUNK_CULLED = 2;
// Visibility probe: the texture unit the ID pass reads each drawn instance's id from (units 0 to 3 hold the segment
// data, 4 and 5 the scene-pass maps)
static constexpr int PROBE_ID_UNIT = 6;
// Shadow map check: a bead nearer than the map by more than this is missing (the two depth formats may differ)
static constexpr float PROBE_SHADOW_DEPTH_TOLERANCE = 1e-6f;

// Moves the shadow map check's fields from one probe's statistics to another's
static void move_shadow_check(VisibilityProbeStats &from, VisibilityProbeStats &to)
{
    to.shadow_ran = from.shadow_ran;
    to.shadow_error = std::move(from.shadow_error);
    to.shadow_texels = from.shadow_texels;
    to.shadow_missing_texels = from.shadow_missing_texels;
    to.shadow_max_gap = from.shadow_max_gap;
    to.shadow_segments_drawn = from.shadow_segments_drawn;
    to.shadow_list_segments = from.shadow_list_segments;
    to.shadow_ms = from.shadow_ms;
}

// The camera probe's fields start over; the shadow map check, run earlier in the frame, is kept
static void reset_camera_probe(VisibilityProbeStats &stats)
{
    VisibilityProbeStats fresh;
    move_shadow_check(stats, fresh);
    stats = std::move(fresh);
}

void ViewerImpl::request_visibility_probe()
{
    m_probe_requested = true;
    m_probe_stats = VisibilityProbeStats();
    m_probe_stats.error = "no visible pass";
    m_probe_mask.clear();
}

void ViewerImpl::run_visibility_probe(const ChunkSelection *selection, const Mat4x4 &view_matrix,
                                      const Mat4x4 &projection_matrix, const Vec3 &camera_position)
{
    reset_camera_probe(m_probe_stats);
    m_probe_mask.clear();
    // Allocations happen before the GL state is changed or after it is restored, so a throw leaves the state intact
    try
    {
        // The chunk set of the enabled list, which the probe draws whole: built now when the list was deferred, outside
        // the probe's time
        ensure_enabled_lists();
        const auto start = std::chrono::steady_clock::now();
        if (m_segments_id_shader_id == 0 || m_segments_depth_shader_id == 0)
        {
            m_probe_stats.error = "id program missing";
            return;
        }
        const size_t subcells = m_view_chunks.subcell_first.size() < 2 ? 0 : m_view_chunks.subcell_first.size() - 1;
        const size_t order_count = m_view_chunks.order.size();
        if (!m_view_chunks_valid || subcells == 0 || order_count == 0)
        {
            m_probe_stats.error = "no chunks";
            return;
        }
        if (order_count >= size_t(std::numeric_limits<int32_t>::max()))
        {
            m_probe_stats.error = "too many segments";
            return;
        }
        GLint viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const GLsizei width = viewport[2];
        const GLsizei height = viewport[3];
        if (width <= 0 || height <= 0)
        {
            m_probe_stats.error = "no viewport";
            return;
        }
        VisibilityProbeStats stats;
        stats.width = width;
        stats.height = height;
        stats.list_segments = order_count;
        stats.drawn_segments = selection != nullptr ? selection->segment_count() : m_enabled_segments_count;

        // The id of each order position, the position itself, which the ID program writes plus one; the sub-cell of
        // each position, the readback, and the positions that own a pixel
        std::vector<uint32_t> position_ids(order_count);
        std::iota(position_ids.begin(), position_ids.end(), uint32_t(0));
        std::vector<uint32_t> subcell_of = subcell_of_order(m_view_chunks);
        std::vector<uint32_t> ids(size_t(width) * size_t(height), 0);
        // The same pixels' window depth, for the occlusion box test
        std::vector<float> depth(size_t(width) * size_t(height), 1.0f);
        std::vector<uint8_t> visible(order_count, 0);
        if (subcell_of.size() != order_count)
        {
            m_probe_stats.error = "no chunks";
            return;
        }

        drain_gl_errors();

        // The state the probe changes
        GLint prev_draw_fbo = 0;
        GLint prev_read_fbo = 0;
        GLint prev_program = 0;
        GLint prev_active_texture = 0;
        GLint prev_renderbuffer = 0;
        GLint prev_pack_buffer = 0;
        GLint prev_pack_alignment = 4;
        GLint prev_pack_row_length = 0;
        GLint prev_pack_skip_pixels = 0;
        GLint prev_pack_skip_rows = 0;
        GLint prev_depth_func = GL_LESS;
        GLboolean prev_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        GLboolean prev_depth_mask = GL_TRUE;
        GLfloat prev_offset_factor = 0.0f;
        GLfloat prev_offset_units = 0.0f;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_texture);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_renderbuffer);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prev_pack_buffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack_alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &prev_pack_row_length);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &prev_pack_skip_pixels);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &prev_pack_skip_rows);
        glGetIntegerv(GL_DEPTH_FUNC, &prev_depth_func);
        glGetBooleanv(GL_COLOR_WRITEMASK, prev_color_mask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &prev_offset_factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &prev_offset_units);
        const GLboolean prev_depth_test = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
        const GLboolean prev_blend = glIsEnabled(GL_BLEND);
        const GLboolean prev_cull_face = glIsEnabled(GL_CULL_FACE);
        const GLboolean prev_clip0 = glIsEnabled(GL_CLIP_DISTANCE0);
        const GLboolean prev_offset_fill = glIsEnabled(GL_POLYGON_OFFSET_FILL);

        // The lists the pass draws: the chunk order, so an instance id is an order position, and each position's id
        GLuint order_buf = 0;
        GLuint order_tex = 0;
        GLuint id_buf = 0;
        GLuint id_tex = 0;
        glGenBuffers(1, &order_buf);
        glGenTextures(1, &order_tex);
        glBindBuffer(GL_TEXTURE_BUFFER, order_buf);
        glBufferData(GL_TEXTURE_BUFFER, order_count * sizeof(uint32_t), m_view_chunks.order.data(), GL_STREAM_DRAW);
        glGenBuffers(1, &id_buf);
        glGenTextures(1, &id_tex);
        glBindBuffer(GL_TEXTURE_BUFFER, id_buf);
        glBufferData(GL_TEXTURE_BUFFER, order_count * sizeof(uint32_t), position_ids.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
        // The buffer holds its own copy
        std::vector<uint32_t>().swap(position_ids);

        // The target: order position ids (0: none) and depth, single sample, at the viewport's size
        GLuint fbo = 0;
        GLuint color_rb = 0;
        GLuint depth_rb = 0;
        glGenRenderbuffers(1, &color_rb);
        glGenRenderbuffers(1, &depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, color_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_R32UI, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prev_renderbuffer));
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rb);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        // No allocation from here until the state is restored
        const char *reason = nullptr;
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            reason = "target incomplete";
        else if (take_gl_error() != GL_NO_ERROR)
            reason = "target allocation failed";

        if (reason == nullptr)
        {
            glViewport(0, 0, width, height);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glDisable(GL_CLIP_DISTANCE0);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glEnable(GL_DEPTH_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
            const GLuint clear_id[4] = {0, 0, 0, 0};
            const GLfloat clear_depth = 1.0f;
            glClearBufferuiv(GL_COLOR, 0, clear_id);
            glClearBufferfv(GL_DEPTH, 0, &clear_depth);

            // One pass: the true beads seen from the camera write depth and their order positions, so a pixel keeps
            // the nearest bead's (no offset: a bead behind the surface never owns a pixel). The id of each drawn
            // instance is read from its own unit, past the ones the segment draw binds.
            GLint prev_unit_buffer = 0;
            glActiveTexture(GL_TEXTURE0 + PROBE_ID_UNIT);
            glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &prev_unit_buffer);
            glBindTexture(GL_TEXTURE_BUFFER, id_tex);
            glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, id_buf);
            glUseProgram(m_segments_id_shader_id);
            glUniform1i(m_uni_id_instance_id_tex_id, PROBE_ID_UNIT);
            glUniform1i(m_uni_id_positions_tex_id, 0);
            glUniform1i(m_uni_id_height_width_angle_tex_id, 1);
            if (m_uni_id_colors_tex_id != -1)
                glUniform1i(m_uni_id_colors_tex_id, 2);
            glUniform1i(m_uni_id_segment_index_tex_id, 3);
            glUniformMatrix4fv(m_uni_id_view_matrix_id, 1, GL_FALSE, view_matrix.data());
            glUniformMatrix4fv(m_uni_id_projection_matrix_id, 1, GL_FALSE, projection_matrix.data());
            glUniform3fv(m_uni_id_camera_position_id, 1, camera_position.data());
            glUniform4fv(m_uni_id_clipping_plane_id, 1, m_clipping_plane.data());
            glDepthFunc(GL_LESS);
            draw_segment_list(order_tex, order_buf, order_count);

            if (take_gl_error() != GL_NO_ERROR)
                reason = "draw failed";
            else
            {
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
                glPixelStorei(GL_PACK_SKIP_ROWS, 0);
                glReadPixels(0, 0, width, height, GL_RED_INTEGER, GL_UNSIGNED_INT, ids.data());
                if (take_gl_error() != GL_NO_ERROR)
                    reason = "readback failed";
                else
                {
                    // The depth attachment of the bound target: the true depth of every enabled segment
                    glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
                    if (take_gl_error() != GL_NO_ERROR)
                        reason = "depth readback failed";
                }
            }
            glActiveTexture(GL_TEXTURE0 + PROBE_ID_UNIT);
            glBindTexture(GL_TEXTURE_BUFFER, static_cast<GLuint>(prev_unit_buffer));
        }

        // Restore
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw_fbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(prev_program));
        glActiveTexture(static_cast<GLenum>(prev_active_texture));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(prev_pack_buffer));
        glPixelStorei(GL_PACK_ALIGNMENT, prev_pack_alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, prev_pack_row_length);
        glPixelStorei(GL_PACK_SKIP_PIXELS, prev_pack_skip_pixels);
        glPixelStorei(GL_PACK_SKIP_ROWS, prev_pack_skip_rows);
        glDepthFunc(static_cast<GLenum>(prev_depth_func));
        glColorMask(prev_color_mask[0], prev_color_mask[1], prev_color_mask[2], prev_color_mask[3]);
        glDepthMask(prev_depth_mask);
        glPolygonOffset(prev_offset_factor, prev_offset_units);
        const auto set_enabled = [](GLenum cap, GLboolean enabled)
        {
            if (enabled)
                glEnable(cap);
            else
                glDisable(cap);
        };
        set_enabled(GL_DEPTH_TEST, prev_depth_test);
        set_enabled(GL_SCISSOR_TEST, prev_scissor);
        set_enabled(GL_BLEND, prev_blend);
        set_enabled(GL_CULL_FACE, prev_cull_face);
        set_enabled(GL_CLIP_DISTANCE0, prev_clip0);
        set_enabled(GL_POLYGON_OFFSET_FILL, prev_offset_fill);
        if (reason == nullptr && take_gl_error() != GL_NO_ERROR)
            reason = "state restore failed";

        // The probe's own objects, unbound by the restore
        glDeleteFramebuffers(1, &fbo);
        glDeleteRenderbuffers(1, &color_rb);
        glDeleteRenderbuffers(1, &depth_rb);
        glDeleteTextures(1, &order_tex);
        glDeleteTextures(1, &id_tex);
        glDeleteBuffers(1, &order_buf);
        glDeleteBuffers(1, &id_buf);
        drain_gl_errors();
        if (reason != nullptr)
        {
            m_probe_stats.error = reason;
            return;
        }

        // Pixels per sub-cell and the order positions that own one: an id is an order position plus one, 0 no toolpath
        std::vector<uint32_t> pixels(subcells, 0);
        for (const uint32_t id : ids)
            if (id != 0 && id <= order_count)
            {
                visible[id - 1] = 1;
                ++pixels[subcell_of[id - 1]];
                ++stats.view_pixels;
            }
        stats.visible_segments = size_t(std::count(visible.begin(), visible.end(), uint8_t(1)));

        // The chunks that hold a sub-cell that owns a pixel, and their segments
        for (const ViewChunk &c : m_view_chunks.chunks)
        {
            const size_t first = std::min<size_t>(c.first_subcell, subcells);
            const size_t last = std::min<size_t>(size_t(c.first_subcell) + c.subcell_count, subcells);
            if (std::any_of(pixels.begin() + first, pixels.begin() + last, [](uint32_t n) { return n != 0; }))
            {
                ++stats.view_chunks;
                stats.view_chunk_segments += c.count;
            }
        }

        // The sub-cells the chunk selection holds, through each segment's sub-cell; every one when the pass drew the
        // enabled list without a selection or the occlusion draw set, which no chunk selection made (its own losses are
        // counted below)
        const bool chunk_selection = selection != nullptr && !selection->occlusion;
        std::vector<uint8_t> held(subcells, uint8_t(chunk_selection ? 0 : 1));
        if (chunk_selection)
        {
            std::vector<uint32_t> chunk_segments;
            select_view_chunks(m_view_chunks, selection->params, chunk_segments);
            std::vector<uint32_t> subcell_of_segment(m_vertices.size(), std::numeric_limits<uint32_t>::max());
            for (size_t p = 0; p < order_count; ++p)
                if (m_view_chunks.order[p] < subcell_of_segment.size())
                    subcell_of_segment[m_view_chunks.order[p]] = subcell_of[p];
            for (const uint32_t segment : chunk_segments)
                if (segment < subcell_of_segment.size() && subcell_of_segment[segment] < subcells)
                    held[subcell_of_segment[segment]] = 1;
        }

        // A sub-cell's segments: order positions [first, last)
        const auto subcell_range = [this, order_count](size_t s)
        {
            const size_t first = std::min<size_t>(m_view_chunks.subcell_first[s], order_count);
            return std::make_pair(first, std::clamp<size_t>(m_view_chunks.subcell_first[s + 1], first, order_count));
        };

        // Each sub-cell that owns a pixel: dropped by the chunk selection, or drawn (the pass drew every sub-cell the
        // selection holds)
        std::vector<uint8_t> subcell_class(subcells, PROBE_NO_TOOLPATH);
        for (size_t s = 0; s < subcells; ++s)
        {
            if (pixels[s] == 0)
                continue;
            const auto [first, last] = subcell_range(s);
            ++stats.view_subcells;
            stats.view_segments += last - first;
            if (held[s] == 0)
            {
                subcell_class[s] = PROBE_CHUNK_CULLED;
                ++stats.chunk_culled_subcells;
                stats.chunk_culled_pixels += pixels[s];
                continue;
            }
            subcell_class[s] = PROBE_DRAWN;
        }

        std::vector<uint8_t> mask(ids.size(), PROBE_NO_TOOLPATH);
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] != 0 && ids[i] <= order_count)
                mask[i] = subcell_class[subcell_of[ids[i] - 1]];

        stats.ran = true;
        stats.ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();

        // The occlusion box test against this view's own depth: every chunk box, then the sub-cell boxes of the chunks
        // that pass, each against the depth pyramid and against the full-resolution depth
        const auto box_start = std::chrono::steady_clock::now();
        const DepthPyramid pyramid = build_depth_pyramid(width, height, std::move(depth));
        // Column-major projection * view, as the chunk selection forms it
        float view_proj[16];
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += projection_matrix[k * 4 + r] * view_matrix[c * 4 + k];
                view_proj[c * 4 + r] = sum;
            }
        // A chunk box at +/- FLT_MAX or not finite is unbounded
        const auto bounded = [](const float lo[3], const float hi[3])
        {
            for (int a = 0; a < 3; ++a)
                if (!(lo[a] > -std::numeric_limits<float>::max() && hi[a] < std::numeric_limits<float>::max()))
                    return false;
            return true;
        };
        // A sub-cell's box, built as the chunk boxes are: both ends of each segment (positions hold the bead top),
        // widened on every axis by the largest max(half width, height) of the segments' end vertices. False when the
        // sub-cell is empty or a segment has no end vertex or a non-finite end, width or height.
        const auto subcell_box = [this](size_t first, size_t last, float lo_out[3], float hi_out[3])
        {
            if (first >= last)
                return false;
            const double inf = std::numeric_limits<double>::infinity();
            double lo[3] = {inf, inf, inf};
            double hi[3] = {-inf, -inf, -inf};
            double pad = 0.0;
            for (size_t p = first; p < last; ++p)
            {
                const size_t k = m_view_chunks.order[p];
                if (k + 1 >= m_vertices.size())
                    return false;
                const PathVertex &start = m_vertices[k];
                const PathVertex &end = m_vertices[k + 1];
                for (const PathVertex *v : {&start, &end})
                    for (int a = 0; a < 3; ++a)
                    {
                        const double coordinate = v->position[a];
                        if (!std::isfinite(coordinate))
                            return false;
                        lo[a] = std::min(lo[a], coordinate);
                        hi[a] = std::max(hi[a], coordinate);
                    }
                const double w = end.width;
                const double h = end.height;
                if (!std::isfinite(w) || !std::isfinite(h))
                    return false;
                pad = std::max({pad, 0.5 * w, h});
            }
            for (int a = 0; a < 3; ++a)
            {
                lo_out[a] = static_cast<float>(lo[a] - pad);
                hi_out[a] = static_cast<float>(hi[a] + pad);
            }
            return true;
        };
        // The sub-cells tested (those of the chunks that pass), kept for the faces test
        struct TestedSubcell
        {
            float lo[3];
            float hi[3];
            size_t segments;
            bool has_box;
            bool owns_pixel;
        };
        std::vector<TestedSubcell> tested;
        for (const ViewChunk &c : m_view_chunks.chunks)
        {
            const size_t first_subcell = std::min<size_t>(c.first_subcell, subcells);
            const size_t last_subcell = std::min<size_t>(size_t(c.first_subcell) + c.subcell_count, subcells);
            ++stats.box_chunks_tested;
            // An unbounded chunk passes
            const BoxVisibility chunk_result = bounded(c.min, c.max)
                                                   ? test_box_occlusion(view_proj, c.min, c.max, pyramid)
                                                   : BoxVisibility::Visible;
            if (chunk_result != BoxVisibility::Visible)
            {
                if (chunk_result == BoxVisibility::Outside)
                    ++stats.box_chunks_outside;
                // None of its sub-cells is drawn
                for (size_t s = first_subcell; s < last_subcell; ++s)
                    if (pixels[s] != 0)
                        ++stats.box_misses;
                continue;
            }
            ++stats.box_chunks_visible;
            stats.box_chunk_segments += c.count;
            for (size_t s = first_subcell; s < last_subcell; ++s)
            {
                ++stats.box_subcells_tested;
                const auto [first, last] = subcell_range(s);
                float lo[3];
                float hi[3];
                // A sub-cell without a box passes
                const bool has_box = subcell_box(first, last, lo, hi);
                const BoxVisibility result = has_box ? test_box_occlusion(view_proj, lo, hi, pyramid)
                                                     : BoxVisibility::Visible;
                // The exact test passes nothing the pyramid test occludes or puts outside, so it runs only where the
                // pyramid test passes
                const BoxVisibility exact = has_box && result == BoxVisibility::Visible
                                                ? test_box_occlusion_exact(view_proj, lo, hi, pyramid)
                                                : result;
                if (result == BoxVisibility::Visible)
                {
                    ++stats.box_subcells_visible;
                    stats.box_segments += last - first;
                }
                else if (pixels[s] != 0)
                    ++stats.box_misses;
                if (exact == BoxVisibility::Visible)
                {
                    ++stats.box_exact_subcells_visible;
                    stats.box_exact_segments += last - first;
                }
                TestedSubcell &t = tested.emplace_back();
                if (has_box)
                {
                    std::copy(lo, lo + 3, t.lo);
                    std::copy(hi, hi + 3, t.hi);
                }
                t.segments = last - first;
                t.has_box = has_box;
                t.owns_pixel = pixels[s] != 0;
            }
        }
        stats.box_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - box_start).count();

        // The faces test: the tested sub-cells with 4 and with 8 taps, then every chunk box with 4 taps
        const auto faces_start = std::chrono::steady_clock::now();
        for (const TestedSubcell &t : tested)
        {
            // A sub-cell without a box passes
            const BoxVisibility four = t.has_box ? test_box_occlusion_faces(view_proj, t.lo, t.hi, pyramid, 4)
                                                 : BoxVisibility::Visible;
            const BoxVisibility eight = t.has_box ? test_box_occlusion_faces(view_proj, t.lo, t.hi, pyramid, 8)
                                                  : BoxVisibility::Visible;
            if (four == BoxVisibility::Visible)
            {
                ++stats.box4_subcells_visible;
                stats.box4_segments += t.segments;
            }
            else if (t.owns_pixel)
                ++stats.box4_misses;
            if (eight == BoxVisibility::Visible)
            {
                ++stats.box8_subcells_visible;
                stats.box8_segments += t.segments;
            }
            else if (t.owns_pixel)
                ++stats.box8_misses;
        }
        for (const ViewChunk &c : m_view_chunks.chunks)
        {
            // An unbounded chunk passes
            const BoxVisibility four = bounded(c.min, c.max)
                                           ? test_box_occlusion_faces(view_proj, c.min, c.max, pyramid, 4)
                                           : BoxVisibility::Visible;
            if (four == BoxVisibility::Visible)
            {
                ++stats.box4_chunks_visible;
                stats.box4_chunk_segments += c.count;
                continue;
            }
            const size_t first_subcell = std::min<size_t>(c.first_subcell, subcells);
            const size_t last_subcell = std::min<size_t>(size_t(c.first_subcell) + c.subcell_count, subcells);
            for (size_t s = first_subcell; s < last_subcell; ++s)
                if (pixels[s] != 0)
                    ++stats.box4_chunk_misses;
        }
        stats.box_faces_ms =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - faces_start).count();

        // The faces test on the chunk structure of the whole print with its tight boxes, under the current filter: 4
        // texels on the box of every chunk that holds a flagged sub-cell, then 8 on the flagged sub-cells of the chunks
        // that pass. A pixel's owner maps to the structure's sub-cell that holds its segment.
        const auto pc_start = std::chrono::steady_clock::now();
        // The segment test's time, which pc_ms leaves out
        std::chrono::steady_clock::duration seg_elapsed{0};
        const size_t pc_subcells = m_print_chunks.subcells();
        if (m_print_chunks.valid && m_print_chunk_flags.size() == pc_subcells)
        {
            // The segments that own a pixel, one bit per vertex index
            std::vector<uint64_t> owner_bits((m_vertices.size() + 63) / 64, 0);
            for (size_t p = 0; p < order_count; ++p)
                if (visible[p] != 0)
                {
                    const size_t k = m_view_chunks.order[p];
                    if (k < m_vertices.size())
                        owner_bits[k >> 6] |= uint64_t(1) << (k & 63);
                }
            const std::vector<uint32_t> &pc_order = m_print_chunks.set.order;
            const std::vector<uint32_t> &pc_first = m_print_chunks.set.subcell_first;
            std::vector<uint8_t> pc_owns(pc_subcells, 0);
            for (size_t s = 0; s < pc_subcells; ++s)
                for (size_t j = pc_first[s]; j < pc_first[s + 1]; ++j)
                {
                    const size_t k = pc_order[j];
                    if (((owner_bits[k >> 6] >> (k & 63)) & 1) != 0)
                    {
                        pc_owns[s] = 1;
                        break;
                    }
                }
            std::vector<uint8_t> pc_pass(pc_subcells, 0);
            for (size_t c = 0; c < m_print_chunks.set.chunks.size(); ++c)
            {
                const ViewChunk &chunk = m_print_chunks.set.chunks[c];
                const size_t first_subcell = std::min<size_t>(chunk.first_subcell, pc_subcells);
                const size_t last_subcell = std::min<size_t>(size_t(chunk.first_subcell) + chunk.subcell_count,
                                                             pc_subcells);
                if (std::none_of(m_print_chunk_flags.begin() + first_subcell,
                                 m_print_chunk_flags.begin() + last_subcell, [](uint8_t flag) { return flag != 0; }))
                    continue;
                ++stats.pc_chunks_tested;
                // An unbounded box passes
                const float *chunk_box = &m_print_chunks.chunk_box[6 * c];
                const BoxVisibility chunk_result = bounded(chunk_box, chunk_box + 3)
                                                       ? test_box_occlusion_faces(view_proj, chunk_box, chunk_box + 3,
                                                                                  pyramid, 4)
                                                       : BoxVisibility::Visible;
                if (chunk_result != BoxVisibility::Visible)
                    continue;
                ++stats.pc_chunks_visible;
                for (size_t s = first_subcell; s < last_subcell; ++s)
                {
                    if (m_print_chunk_flags[s] == 0)
                        continue;
                    ++stats.pc_subcells_tested;
                    const float *box = &m_print_chunks.box[6 * s];
                    const BoxVisibility result = bounded(box, box + 3)
                                                     ? test_box_occlusion_faces(view_proj, box, box + 3, pyramid, 8)
                                                     : BoxVisibility::Visible;
                    if (result == BoxVisibility::Visible)
                    {
                        pc_pass[s] = 1;
                        ++stats.pc_subcells_visible;
                    }
                }
            }
            for (size_t s = 0; s < pc_subcells; ++s)
                if (pc_owns[s] != 0 && pc_pass[s] == 0)
                    ++stats.pc_misses;
            const PrintChunkFilter filter = print_chunk_filter();
            stats.pc_segments = count_print_chunks(m_print_chunks, m_vertices, filter, pc_pass);

            // The segment test, 8 texels per axis, on the enabled segments of the sub-cells that pass. An end's ball
            // is centred where the position buffer puts its vertex (an extrusion vertex half its height below its top)
            // with max(half height, half width) of the heights/widths buffer's sizes (the travel or wipe radius for a
            // travel or wipe vertex): every vertex the segments shader places for the end lies in it.
            const auto seg_start = std::chrono::steady_clock::now();
            const auto shader_end = [this](const PathVertex &v, float p[3], float &radius)
            {
                p[0] = v.position[0];
                p[1] = v.position[1];
                p[2] = v.position[2];
                if (v.type == EMoveType::Extrude)
                    p[2] -= 0.5f * v.height;
                float height = v.height;
                float width = v.width;
                if (v.is_travel())
                    height = width = m_travels_radius;
                else if (v.is_wipe())
                    height = width = m_wipes_radius;
                radius = 0.5f * std::max(std::abs(height), std::abs(width));
            };
            std::vector<uint32_t> seg_candidates;
            seg_candidates.reserve(stats.pc_segments);
            emit_print_chunks(m_print_chunks, m_vertices, filter, pc_pass, seg_candidates);
            std::vector<uint64_t> seg_pass_bits(owner_bits.size(), 0);
            for (const uint32_t k : seg_candidates)
            {
                if (size_t(k) + 1 >= m_vertices.size())
                    continue;
                float a[3];
                float b[3];
                float radius_a = 0.0f;
                float radius_b = 0.0f;
                shader_end(m_vertices[k], a, radius_a);
                shader_end(m_vertices[size_t(k) + 1], b, radius_b);
                ++stats.seg_tested;
                if (test_segment_occlusion(view_proj, a, b, radius_a, radius_b, pyramid, 8) == BoxVisibility::Visible)
                {
                    ++stats.seg_visible;
                    seg_pass_bits[k >> 6] |= uint64_t(1) << (k & 63);
                }
            }
            // The segments that own a pixel, and those of them that do not pass (one outside the structure or not
            // enabled under its filter among them)
            for (size_t i = 0; i < owner_bits.size(); ++i)
            {
                for (uint64_t bits = owner_bits[i]; bits != 0; bits &= bits - 1)
                    ++stats.seg_owners;
                for (uint64_t bits = owner_bits[i] & ~seg_pass_bits[i]; bits != 0; bits &= bits - 1)
                    ++stats.seg_misses;
            }
            seg_elapsed = std::chrono::steady_clock::now() - seg_start;
            stats.seg_ms = std::chrono::duration<float, std::milli>(seg_elapsed).count();

            std::fill(pc_pass.begin(), pc_pass.end(), uint8_t(1));
            stats.pc_enabled_segments = count_print_chunks(m_print_chunks, m_vertices, filter, pc_pass);
        }
        stats.pc_ms =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - pc_start - seg_elapsed).count();

        // The occlusion draw set the pass drew: the whole print's sub-cells that own a pixel and are not in the camera
        // view's draw set, with their pixels (the chunk-culled class in the mask) and the segments of them that own
        // one; then the cross-check of the GPU test against the CPU rule
        if (selection != nullptr && selection->occlusion)
        {
            stats.occl = true;
            stats.occl_drawn_segments = selection->segment_count();
            const std::vector<uint8_t> &drawn = m_occlusion.visible_subcells(OCCLUSION_VIEW_CAMERA);
            if (!m_print_chunks.valid || drawn.size() != pc_subcells)
                stats.occl_error = "no draw set";
            else
            {
                const size_t words = (m_vertices.size() + 63) / 64;
                std::vector<uint64_t> owner_bits(words, 0);
                for (size_t p = 0; p < order_count; ++p)
                    if (visible[p] != 0 && m_view_chunks.order[p] < m_vertices.size())
                        owner_bits[m_view_chunks.order[p] >> 6] |= uint64_t(1) << (m_view_chunks.order[p] & 63);
                // The segments that own a pixel in a sub-cell the draw set left out
                std::vector<uint64_t> missing_bits(words, 0);
                const std::vector<uint32_t> &pc_order = m_print_chunks.set.order;
                const std::vector<uint32_t> &pc_first = m_print_chunks.set.subcell_first;
                for (size_t s = 0; s < pc_subcells; ++s)
                {
                    if (drawn[s] != 0)
                        continue;
                    bool owns = false;
                    for (size_t j = pc_first[s]; j < pc_first[s + 1]; ++j)
                    {
                        const size_t k = pc_order[j];
                        if (k >= m_vertices.size() || ((owner_bits[k >> 6] >> (k & 63)) & 1) == 0)
                            continue;
                        missing_bits[k >> 6] |= uint64_t(1) << (k & 63);
                        ++stats.occl_missing_segments;
                        owns = true;
                    }
                    stats.occl_missing_subcells += owns ? 1 : 0;
                }
                for (size_t i = 0; i < ids.size(); ++i)
                {
                    if (ids[i] == 0 || ids[i] > order_count)
                        continue;
                    const size_t k = m_view_chunks.order[ids[i] - 1];
                    if (k < m_vertices.size() && ((missing_bits[k >> 6] >> (k & 63)) & 1) != 0)
                    {
                        mask[i] = PROBE_CHUNK_CULLED;
                        ++stats.occl_missing_pixels;
                    }
                }
            }
            OcclusionCrossCheck check;
            std::string check_error;
            if (m_occlusion.cross_check(occlusion_inputs(), check, check_error))
            {
                stats.occl_xcheck_chunks = check.chunks;
                stats.occl_xcheck_chunk_gpu_only = check.chunk_gpu_only;
                stats.occl_xcheck_chunk_cpu_only = check.chunk_cpu_only;
                stats.occl_xcheck_subcells = check.subcells;
                stats.occl_xcheck_gpu_only = check.subcell_gpu_only;
                stats.occl_xcheck_cpu_only = check.subcell_cpu_only;
            }
            else
                stats.occl_xcheck_error = check_error;
        }

        move_shadow_check(m_probe_stats, stats);
        m_probe_stats = std::move(stats);
        m_probe_mask = std::move(mask);
    }
    catch (...)
    {
        // An allocation failed; the GL state was not changed or is already restored
        reset_camera_probe(m_probe_stats);
        m_probe_stats.error = "out of memory";
        m_probe_mask.clear();
    }
}

void ViewerImpl::run_shadow_probe(const ChunkSelection *selection)
{
    // A second shadow pass in the frame replaces the first one's figures
    VisibilityProbeStats none;
    move_shadow_check(none, m_probe_stats);
    // Allocations happen before the GL state is changed or after it is restored, so a throw leaves the state intact
    try
    {
        const size_t drawn = selection != nullptr ? selection->segment_count() : m_enabled_segments_count;
        // The whole enabled list, built now when it was deferred, outside the check's time
        ensure_enabled_lists();
        const auto start = std::chrono::steady_clock::now();
        const size_t list_count = m_enabled_segments_count;
        if (list_count == 0)
        {
            m_probe_stats.shadow_error = "no toolpaths";
            return;
        }
        GLint viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const GLsizei width = viewport[2];
        const GLsizei height = viewport[3];
        if (width <= 0 || height <= 0)
        {
            m_probe_stats.shadow_error = "no viewport";
            return;
        }
        // The window depth over the viewport: of the map, and of the whole list in the check's own target
        std::vector<float> map_depth(size_t(width) * size_t(height), 1.0f);
        std::vector<float> list_depth(map_depth.size(), 1.0f);

        drain_gl_errors();

        // The state the check changes; the pass's program, uniforms, polygon offset and clip distance draw the list
        GLint prev_draw_fbo = 0;
        GLint prev_read_fbo = 0;
        GLint prev_program = 0;
        GLint prev_active_texture = 0;
        GLint prev_renderbuffer = 0;
        GLint prev_pack_buffer = 0;
        GLint prev_pack_alignment = 4;
        GLint prev_pack_row_length = 0;
        GLint prev_pack_skip_pixels = 0;
        GLint prev_pack_skip_rows = 0;
        GLint prev_depth_func = GL_LESS;
        GLboolean prev_depth_mask = GL_TRUE;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_texture);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_renderbuffer);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prev_pack_buffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack_alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &prev_pack_row_length);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &prev_pack_skip_pixels);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &prev_pack_skip_rows);
        glGetIntegerv(GL_DEPTH_FUNC, &prev_depth_func);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);
        const GLboolean prev_depth_test = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);

        // No allocation from here until the state is restored
        const char *reason = nullptr;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        // The map as the pass left it: the volumes and the toolpaths it drew
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_draw_fbo));
        glReadPixels(viewport[0], viewport[1], width, height, GL_DEPTH_COMPONENT, GL_FLOAT, map_depth.data());
        if (take_gl_error() != GL_NO_ERROR)
            reason = "readback failed";

        // The target: depth only, single sample, at the viewport's size
        GLuint fbo = 0;
        GLuint depth_rb = 0;
        if (reason == nullptr)
        {
            glGenRenderbuffers(1, &depth_rb);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prev_renderbuffer));
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                reason = "target incomplete";
            else if (take_gl_error() != GL_NO_ERROR)
                reason = "target allocation failed";
        }

        if (reason == nullptr)
        {
            // Every enabled segment with the pass's program and uniforms; each texel keeps the nearest bead's depth
            glViewport(0, 0, width, height);
            glDisable(GL_SCISSOR_TEST);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            const GLfloat clear_depth = 1.0f;
            glClearBufferfv(GL_DEPTH, 0, &clear_depth);
            draw_enabled_segments(nullptr);
            if (take_gl_error() != GL_NO_ERROR)
                reason = "draw failed";
            else
            {
                glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, list_depth.data());
                if (take_gl_error() != GL_NO_ERROR)
                    reason = "readback failed";
            }
        }

        // Restore
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw_fbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glUseProgram(static_cast<GLuint>(prev_program));
        glActiveTexture(static_cast<GLenum>(prev_active_texture));
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prev_renderbuffer));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(prev_pack_buffer));
        glPixelStorei(GL_PACK_ALIGNMENT, prev_pack_alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, prev_pack_row_length);
        glPixelStorei(GL_PACK_SKIP_PIXELS, prev_pack_skip_pixels);
        glPixelStorei(GL_PACK_SKIP_ROWS, prev_pack_skip_rows);
        glDepthFunc(static_cast<GLenum>(prev_depth_func));
        glDepthMask(prev_depth_mask);
        if (prev_depth_test)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        if (prev_scissor)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
        if (reason == nullptr && take_gl_error() != GL_NO_ERROR)
            reason = "state restore failed";

        // The check's own objects, unbound by the restore
        glDeleteFramebuffers(1, &fbo);
        glDeleteRenderbuffers(1, &depth_rb);
        drain_gl_errors();
        if (reason != nullptr)
        {
            m_probe_stats.shadow_error = reason;
            return;
        }

        // A texel a bead covers is missing from the map when the bead lies nearer than the map there; the volumes in
        // the map only make it nearer
        size_t texels = 0;
        size_t missing = 0;
        float max_gap = 0.0f;
        for (size_t i = 0; i < list_depth.size(); ++i)
        {
            if (!(list_depth[i] < 1.0f))
                continue;
            ++texels;
            const float gap = map_depth[i] - list_depth[i];
            if (gap > PROBE_SHADOW_DEPTH_TOLERANCE)
            {
                ++missing;
                max_gap = std::max(max_gap, gap);
            }
        }
        m_probe_stats.shadow_ran = true;
        m_probe_stats.shadow_texels = texels;
        m_probe_stats.shadow_missing_texels = missing;
        m_probe_stats.shadow_max_gap = max_gap;
        m_probe_stats.shadow_segments_drawn = drawn;
        m_probe_stats.shadow_list_segments = list_count;
        m_probe_stats.shadow_ms =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    catch (...)
    {
        // An allocation failed; the GL state was not changed or is already restored
        VisibilityProbeStats failed;
        move_shadow_check(failed, m_probe_stats);
        m_probe_stats.shadow_error = "out of memory";
    }
}
#endif // PREFLIGHT_TEST_HOOKS

bool ViewerImpl::build_prefilter_program()
{
    if (m_segments_pf_shader_id != 0)
        return true;
    if (m_segments_pf_shader_failed)
        return false;
    // A failure never stops the viewer: the id stays 0, the compiler output is kept for the application to log, and
    // the plain program draws for the rest of this initialization
    try
    {
        m_segments_pf_shader_id = init_shader("segments_prefilter", Segments_PF_Vertex_Shader,
                                              Segments_PF_Fragment_Shader);
    }
    catch (const std::exception &e)
    {
        m_segments_pf_shader_id = 0;
        m_segments_pf_shader_log = e.what();
    }
    catch (...)
    {
        m_segments_pf_shader_id = 0;
    }
    if (m_segments_pf_shader_id == 0)
    {
        m_segments_pf_shader_failed = true;
        // A non-empty log is how the application learns of the failure
        if (m_segments_pf_shader_log.empty())
            m_segments_pf_shader_log = "LibVGCode: Unable to build shader program:\nsegments_prefilter\n";
        return false;
    }
    m_uni_segments_pf.init(m_segments_pf_shader_id);
    glcheck();
    return true;
}

void ViewerImpl::render_segments(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix,
                                 const Vec3 &camera_position)
{
    // Occlusion culling switched off: its GPU objects go on the first visible pass after
    if (!m_occlusion_enabled && m_occlusion.holds_gl_objects())
        m_occlusion.release(false);

#ifdef PREFLIGHT_TEST_HOOKS
    // A requested probe runs on this pass or not at all
    const bool probe = m_probe_requested;
    m_probe_requested = false;
    if (probe)
        m_probe_stats.error = "no toolpaths";
#endif // PREFLIGHT_TEST_HOOKS

    m_sample_shading_active = false;
    m_sample_shading_gated = false;
    m_sample_shading_reason.clear();
    // Why the prefilter program does not draw, unless the frame check below picks it
    m_prefilter_active = false;
    if (!m_prefilter_enabled)
        m_prefilter_reason = "disabled";
    else if (m_segments_pf_shader_failed)
        m_prefilter_reason = "shader failed to compile";
    else
        m_prefilter_reason = "no view in range";
    if (m_segments_shader_id == 0)
        return;

    // The enabled list's segments, built or deferred
    if (enabled_segments_count() == 0)
        return;

    int curr_active_texture = 0;
    glsafe(glGetIntegerv(GL_ACTIVE_TEXTURE, &curr_active_texture));
    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    glcheck();

    // The prefilter program draws only in frames where some visible toolpath can be seen within the prefilter's
    // view elevations; every other frame draws with the plain program. The first such frame builds it.
    if (m_prefilter_enabled && !m_segments_pf_shader_failed && prefilter_can_fire(camera_position))
    {
        if (build_prefilter_program())
        {
            m_prefilter_active = true;
            m_prefilter_reason.clear();
        }
        else
            m_prefilter_reason = "shader failed to compile";
    }
    const unsigned int shader_id = m_prefilter_active ? m_segments_pf_shader_id : m_segments_shader_id;
    const SegmentsUniforms &uni = m_prefilter_active ? m_uni_segments_pf : m_uni_segments;

    glsafe(glUseProgram(shader_id));

    glsafe(glUniform1i(uni.positions_tex, 0));
    glsafe(glUniform1i(uni.height_width_angle_tex, 1));
    glsafe(glUniform1i(uni.colors_tex, 2));
    glsafe(glUniform1i(uni.segment_index_tex, 3));
    glsafe(glUniformMatrix4fv(uni.view_matrix, 1, GL_FALSE, view_matrix.data()));
    glsafe(glUniformMatrix4fv(uni.projection_matrix, 1, GL_FALSE, projection_matrix.data()));
    glsafe(glUniform3fv(uni.camera_position, 1, camera_position.data()));
    glsafe(glUniform4fv(uni.clipping_plane, 1, m_clipping_plane.data()));

    // Full lighting tier: shadow map on unit 4, ambient occlusion on unit 5. The units are assigned on
    // every draw, tier on or off: a sampler left at its default unit 0 shares it with the samplerBuffer
    // there, and a program whose samplers of different types point to one unit fails validation, so the
    // draw is rejected.
    glsafe(glUniform1i(uni.shadow_tex, 4));
    glsafe(glUniform1i(uni.ao_tex, 5));
    int prev_tex2d_unit4 = 0;
    int prev_tex2d_unit5 = 0;
    const bool scene_passes_on = m_scene_pass_params.enabled && uni.scene_passes != -1;
    if (scene_passes_on)
    {
        glsafe(glUniform1i(uni.scene_passes, 1));
        glsafe(glUniformMatrix4fv(uni.shadow_vp, 1, GL_FALSE, m_scene_pass_params.shadow_vp.data()));
        glsafe(glUniform2fv(uni.viewport_size, 1, m_scene_pass_params.viewport_size.data()));
        glsafe(glUniform2fv(uni.viewport_origin, 1, m_scene_pass_params.viewport_origin.data()));
        glsafe(glUniform1f(uni.shadow_offset_margin, SHADOW_OFFSET_MARGIN_TEXELS * m_scene_pass_params.shadow_texel));
        glsafe(glActiveTexture(GL_TEXTURE4));
        glsafe(glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d_unit4));
        glsafe(glBindTexture(GL_TEXTURE_2D, m_scene_pass_params.shadow_tex_id));
        glsafe(glActiveTexture(GL_TEXTURE5));
        glsafe(glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d_unit5));
        glsafe(glBindTexture(GL_TEXTURE_2D, m_scene_pass_params.ao_tex_id));
    }
    else if (uni.scene_passes != -1)
        glsafe(glUniform1i(uni.scene_passes, 0));
    bool scene_units_bound = scene_passes_on;
#ifdef __APPLE__
    if (!scene_passes_on && uni.scene_passes != -1)
    {
        const bool create = m_shadow_placeholder_tex_id == 0;
        if (create)
        {
            glsafe(glGenTextures(1, &m_shadow_placeholder_tex_id));
            glsafe(glGenTextures(1, &m_ao_placeholder_tex_id));
        }
        glsafe(glActiveTexture(GL_TEXTURE4));
        glsafe(glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d_unit4));
        glsafe(glBindTexture(GL_TEXTURE_2D, m_shadow_placeholder_tex_id));
        if (create)
        {
            const float lit = 1.0f;
            glsafe(glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &lit));
            glsafe(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
            glsafe(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
            glsafe(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE));
            glsafe(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL));
        }
        glsafe(glActiveTexture(GL_TEXTURE5));
        glsafe(glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d_unit5));
        glsafe(glBindTexture(GL_TEXTURE_2D, m_ao_placeholder_tex_id));
        if (create)
        {
            const unsigned char unoccluded = 255;
            glsafe(glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &unoccluded));
            glsafe(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
            glsafe(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        }
        scene_units_bound = true;
    }
#endif // __APPLE__

    if (m_prefilter_active)
    {
        // The filter width and the layer pitch fade are in output pixels: the viewport over the supersampling scale
        GLint viewport[4] = {0, 0, 0, 0};
        glsafe(glGetIntegerv(GL_VIEWPORT, viewport));
        const std::array<float, 2> viewport_px = {float(viewport[2]) / m_output_pixel_scale,
                                                  float(viewport[3]) / m_output_pixel_scale};
        glsafe(glUniform2fv(uni.pf_viewport_px, 1, viewport_px.data()));
        glsafe(glUniform1f(uni.pf_width, PREFILTER_WIDTH_PX));
        glsafe(glUniform2fv(uni.pf_fade_elevation, 1, PREFILTER_FADE_ELEVATION_DEG.data()));
        glsafe(glUniform2fv(uni.pf_fade_pitch, 1, PREFILTER_FADE_PITCH_PX.data()));
        glsafe(glUniform2fv(uni.pf_fade_above, 1, PREFILTER_FADE_ABOVE_DEG.data()));
        // Beads of the top (bottom) displayed layer are not covered by a next layer
        const std::array<float, 2> bead_tops = displayed_bead_tops();
        glsafe(glUniform1f(uni.pf_bottom_z, bead_tops[0]));
        glsafe(glUniform1f(uni.pf_top_z, bead_tops[1]));
    }

    // Per-sample shading on a multisampled target: the toolpaths' Gouraud shading is evaluated at every
    // sample, so MSAA resolves it instead of repeating one value per pixel. GL 4.0 core; a 3.x context (the
    // Raspberry Pi's 3.1) leaves it off. The gate also keeps it off, cheapest check first, in frames where the
    // prefilter program draws (it already filters the wall shading), where samples times viewport pixels exceed
    // SAMPLE_SHADING_MAX_SAMPLES, or where every visible toolpath's cross-section projects to
    // SAMPLE_SHADING_MAX_SECTION_PX or more, which plain MSAA resolves.
    GLboolean prev_sample_shading = GL_FALSE;
    GLfloat prev_min_sample_shading = 0.0f;
    if (m_sample_shading && GLAD_GL_VERSION_4_0 != 0 && glMinSampleShading != nullptr)
    {
        GLint samples = 0;
        glsafe(glGetIntegerv(GL_SAMPLES, &samples));
        if (samples > 1)
        {
            GLint viewport[4] = {0, 0, 0, 0};
            glsafe(glGetIntegerv(GL_VIEWPORT, viewport));
            if (m_prefilter_active)
                m_sample_shading_reason = "prefilter active";
            else if (double(samples) * double(viewport[2]) * double(viewport[3]) > SAMPLE_SHADING_MAX_SAMPLES)
                m_sample_shading_reason = "pixel budget";
            else if (min_cross_section_px(view_matrix, projection_matrix, camera_position, viewport[2], viewport[3]) >=
                     SAMPLE_SHADING_MAX_SECTION_PX)
                m_sample_shading_reason = "sections resolvable";
            m_sample_shading_gated = !m_sample_shading_reason.empty();
            if (!m_sample_shading_gated)
            {
                prev_sample_shading = glIsEnabled(GL_SAMPLE_SHADING);
                glsafe(glGetFloatv(GL_MIN_SAMPLE_SHADING_VALUE, &prev_min_sample_shading));
                glsafe(glEnable(GL_SAMPLE_SHADING));
                glsafe(glMinSampleShading(1.0f));
                m_sample_shading_active = true;
            }
        }
    }

    glsafe(glDisable(GL_CULL_FACE));

    // With occlusion culling applying, the camera view's occlusion draw set; else the chunks of the enabled list in the
    // camera's frustum that can face it (the full list when chunk culling does not apply)
    const ChunkSelection *selection = occlusion_selection(OCCLUSION_VIEW_CAMERA, view_matrix, projection_matrix,
                                                          camera_position);
    if (selection == nullptr)
    {
        // Without the occlusion draw set the pass draws the enabled list, built now when it was deferred
        ensure_enabled_lists();
        selection = select_chunks(m_camera_selection, view_matrix, projection_matrix);
    }
    m_view_chunk_stats.active = selection != nullptr;
    m_view_chunk_stats.chunks_drawn = selection != nullptr ? selection->chunks : m_view_chunk_stats.chunks_total;
    m_view_chunk_stats.segments_drawn = selection != nullptr ? selection->segment_count() : m_enabled_segments_count;
    m_view_chunk_stats.select_ms = m_camera_selection.select_ms;

    draw_enabled_segments(selection);

    if (m_sample_shading_active)
    {
        glsafe(glMinSampleShading(prev_min_sample_shading));
        if (prev_sample_shading == GL_FALSE)
            glsafe(glDisable(GL_SAMPLE_SHADING));
    }
    if (scene_units_bound)
    {
        glsafe(glActiveTexture(GL_TEXTURE4));
        glsafe(glBindTexture(GL_TEXTURE_2D, prev_tex2d_unit4));
        glsafe(glActiveTexture(GL_TEXTURE5));
        glsafe(glBindTexture(GL_TEXTURE_2D, prev_tex2d_unit5));
    }

    if (curr_cull_face)
        glsafe(glEnable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
    glsafe(glActiveTexture(curr_active_texture));

#ifdef PREFLIGHT_TEST_HOOKS
    // After the draw (of the selection, or of the enabled list without one), in its own target and with every state
    // it touches restored, so the frame's pixels are unchanged
    if (probe)
        run_visibility_probe(selection, view_matrix, projection_matrix, camera_position);
#endif // PREFLIGHT_TEST_HOOKS
    // The visible pass ends the frame
    ++m_occlusion_frame;
}

void ViewerImpl::render_segments_pass(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix,
                                      const Vec3 &camera_position, bool gbuffer)
{
    // The shadow pass draws with the depth-only program, or with the visible program when that failed to build
    const bool depth_only = !gbuffer && m_segments_depth_shader_id != 0;
    const unsigned int shader_id = gbuffer ? m_segments_gbuffer_shader_id
                                           : (depth_only ? m_segments_depth_shader_id : m_segments_shader_id);
    if (shader_id == 0 || enabled_segments_count() == 0)
        return;

    int curr_active_texture = 0;
    glsafe(glGetIntegerv(GL_ACTIVE_TEXTURE, &curr_active_texture));
    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    glcheck();

    glsafe(glUseProgram(shader_id));

    if (gbuffer)
    {
        glsafe(glUniform1i(m_uni_gbuffer_positions_tex_id, 0));
        glsafe(glUniform1i(m_uni_gbuffer_height_width_angle_tex_id, 1));
        glsafe(glUniform1i(m_uni_gbuffer_colors_tex_id, 2));
        glsafe(glUniform1i(m_uni_gbuffer_segment_index_tex_id, 3));
        glsafe(glUniformMatrix4fv(m_uni_gbuffer_view_matrix_id, 1, GL_FALSE, view_matrix.data()));
        glsafe(glUniformMatrix4fv(m_uni_gbuffer_projection_matrix_id, 1, GL_FALSE, projection_matrix.data()));
        glsafe(glUniform3fv(m_uni_gbuffer_camera_position_id, 1, camera_position.data()));
        glsafe(glUniform4fv(m_uni_gbuffer_clipping_plane_id, 1, m_clipping_plane.data()));
    }
    else if (depth_only)
    {
        // Depth pass with no color attachment; the light's position orients the imposters toward the light
        glsafe(glUniform1i(m_uni_depth_positions_tex_id, 0));
        glsafe(glUniform1i(m_uni_depth_height_width_angle_tex_id, 1));
        glsafe(glUniform1i(m_uni_depth_segment_index_tex_id, 3));
        glsafe(glUniformMatrix4fv(m_uni_depth_view_matrix_id, 1, GL_FALSE, view_matrix.data()));
        glsafe(glUniformMatrix4fv(m_uni_depth_projection_matrix_id, 1, GL_FALSE, projection_matrix.data()));
        glsafe(glUniform3fv(m_uni_depth_camera_position_id, 1, camera_position.data()));
        glsafe(glUniform4fv(m_uni_depth_clipping_plane_id, 1, m_clipping_plane.data()));
    }
    else
    {
        // Depth pass with the visible segments program, the depth-only one having failed to
        // build. The scene-pass modulation must stay off so the shader never samples the
        // shadow map being rendered.
        glsafe(glUniform1i(m_uni_segments.positions_tex, 0));
        glsafe(glUniform1i(m_uni_segments.height_width_angle_tex, 1));
        glsafe(glUniform1i(m_uni_segments.colors_tex, 2));
        glsafe(glUniform1i(m_uni_segments.segment_index_tex, 3));
        glsafe(glUniformMatrix4fv(m_uni_segments.view_matrix, 1, GL_FALSE, view_matrix.data()));
        glsafe(glUniformMatrix4fv(m_uni_segments.projection_matrix, 1, GL_FALSE, projection_matrix.data()));
        glsafe(glUniform3fv(m_uni_segments.camera_position, 1, camera_position.data()));
        glsafe(glUniform4fv(m_uni_segments.clipping_plane, 1, m_clipping_plane.data()));
        if (m_uni_segments.scene_passes != -1)
            glsafe(glUniform1i(m_uni_segments.scene_passes, 0));
        // The shadow and AO samplers keep their own units so no two sampler types share unit 0.
        glsafe(glUniform1i(m_uni_segments.shadow_tex, 4));
        glsafe(glUniform1i(m_uni_segments.ao_tex, 5));
    }

    glsafe(glDisable(GL_CULL_FACE));

    // With occlusion culling applying, the view's occlusion draw set: the camera's for the G-buffer pass, the light's
    // for the shadow pass (its residual once the step's depth is merged into the map, which the segments drawn count
    // then). Else the G-buffer pass shares the camera's chunk selection, and the shadow pass draws the whole enabled
    // list: its frustum is fitted to the casters, so every chunk in the list lies in it.
    const ChunkSelection *selection = occlusion_selection(gbuffer ? OCCLUSION_VIEW_CAMERA : OCCLUSION_VIEW_SHADOW,
                                                          view_matrix, projection_matrix, camera_position);
    if (selection == nullptr)
    {
        // Without the occlusion draw set the pass draws the enabled list, built now when it was deferred
        ensure_enabled_lists();
        if (gbuffer)
            selection = select_chunks(m_camera_selection, view_matrix, projection_matrix);
    }
    if (!gbuffer)
    {
        m_view_chunk_stats.shadow_chunks_drawn = selection != nullptr ? selection->chunks
                                                                      : m_view_chunk_stats.chunks_total;
        m_view_chunk_stats.shadow_segments_drawn = selection != nullptr ? selection->segment_count()
                                                                        : m_enabled_segments_count;
    }
    m_view_chunk_stats.select_ms = m_camera_selection.select_ms;

    draw_enabled_segments(selection);

#ifdef PREFLIGHT_TEST_HOOKS
    // A requested probe checks the shadow map while the pass's program is bound; the request stays for the visible pass
    if (!gbuffer && m_probe_requested)
        run_shadow_probe(selection);
#endif // PREFLIGHT_TEST_HOOKS

    if (curr_cull_face)
        glsafe(glEnable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
    glsafe(glActiveTexture(curr_active_texture));
}

void ViewerImpl::render_options(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix)
{
    if (m_options_shader_id == 0)
        return;

    if (m_enabled_options_count == 0)
        return;

    int curr_active_texture = 0;
    glsafe(glGetIntegerv(GL_ACTIVE_TEXTURE, &curr_active_texture));
    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    glcheck();

    glsafe(glUseProgram(m_options_shader_id));

    glsafe(glUniform1i(m_uni_options_positions_tex_id, 0));
    glsafe(glUniform1i(m_uni_options_height_width_angle_tex_id, 1));
    glsafe(glUniform1i(m_uni_options_colors_tex_id, 2));
    glsafe(glUniform1i(m_uni_options_segment_index_tex_id, 3));
    glsafe(glUniformMatrix4fv(m_uni_options_view_matrix_id, 1, GL_FALSE, view_matrix.data()));
    glsafe(glUniformMatrix4fv(m_uni_options_projection_matrix_id, 1, GL_FALSE, projection_matrix.data()));
    glsafe(glUniform4fv(m_uni_options_clipping_plane_id, 1, m_clipping_plane.data()));

    glsafe(glEnable(GL_CULL_FACE));

    std::array<int, 4> curr_bound_texture = {0, 0, 0, 0};
    for (int i = 0; i < curr_bound_texture.size(); ++i)
    {
        glsafe(glActiveTexture(GL_TEXTURE0 + i));
        glsafe(glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &curr_bound_texture[i]));
        //assert(curr_bound_texture[i] == 0);
    }

    glsafe(glActiveTexture(GL_TEXTURE0));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_positions_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_positions_buf_id));
    glsafe(glActiveTexture(GL_TEXTURE1));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_heights_widths_angles_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_heights_widths_angles_buf_id));
    glsafe(glActiveTexture(GL_TEXTURE2));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_colors_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, m_colors_buf_id));
    glsafe(glActiveTexture(GL_TEXTURE3));
    glsafe(glBindTexture(GL_TEXTURE_BUFFER, m_enabled_options_tex_id));
    glsafe(glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, m_enabled_options_buf_id));

    // The program writes the plane's distance to clip distance 0; the rasterizer clips on it only while a plane is set
    const bool clipping = clipping_plane_active();
    const bool curr_clip_distance = glIsEnabled(GL_CLIP_DISTANCE0);
    if (clipping && !curr_clip_distance)
        glsafe(glEnable(GL_CLIP_DISTANCE0));

    m_option_template.render(m_enabled_options_count);

    if (clipping && !curr_clip_distance)
        glsafe(glDisable(GL_CLIP_DISTANCE0));

    if (!curr_cull_face)
        glsafe(glDisable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
    for (int i = 0; i < curr_bound_texture.size(); ++i)
    {
        glsafe(glActiveTexture(GL_TEXTURE0 + i));
        glsafe(glBindTexture(GL_TEXTURE_BUFFER, curr_bound_texture[i]));
    }
    glsafe(glActiveTexture(curr_active_texture));
}

#if VGCODE_ENABLE_COG_AND_TOOL_MARKERS
void ViewerImpl::render_cog_marker(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix)
{
    if (m_cog_marker_shader_id == 0)
        return;

    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    const bool curr_depth_test = glIsEnabled(GL_DEPTH_TEST);
    glcheck();

    glsafe(glEnable(GL_CULL_FACE));
    glsafe(glDisable(GL_DEPTH_TEST));

    glsafe(glUseProgram(m_cog_marker_shader_id));

    glsafe(glUniform3fv(m_uni_cog_marker_world_center_position, 1, m_cog_marker.get_position().data()));
    glsafe(glUniform1f(m_uni_cog_marker_scale_factor, m_cog_marker_scale_factor));
    glsafe(glUniformMatrix4fv(m_uni_cog_marker_view_matrix, 1, GL_FALSE, view_matrix.data()));
    glsafe(glUniformMatrix4fv(m_uni_cog_marker_projection_matrix, 1, GL_FALSE, projection_matrix.data()));

    m_cog_marker.render();

    if (curr_depth_test)
        glsafe(glEnable(GL_DEPTH_TEST));
    if (!curr_cull_face)
        glsafe(glDisable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
}

void ViewerImpl::render_tool_marker(const Mat4x4 &view_matrix, const Mat4x4 &projection_matrix)
{
    if (m_tool_marker_shader_id == 0)
        return;

    if (m_view_range.get_visible()[1] == m_view_range.get_enabled()[1])
        return;

    m_tool_marker.set_position(get_current_vertex().position);

    int curr_shader;
    glsafe(glGetIntegerv(GL_CURRENT_PROGRAM, &curr_shader));
    const bool curr_cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean curr_depth_mask;
    glsafe(glGetBooleanv(GL_DEPTH_WRITEMASK, &curr_depth_mask));
    const bool curr_blend = glIsEnabled(GL_BLEND);
    glcheck();
    int curr_blend_func;
    glsafe(glGetIntegerv(GL_BLEND_SRC_ALPHA, &curr_blend_func));

    glsafe(glDisable(GL_CULL_FACE));
    glsafe(glDepthMask(GL_FALSE));
    glsafe(glEnable(GL_BLEND));
    glsafe(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    glsafe(glUseProgram(m_tool_marker_shader_id));

    const Vec3 &origin = m_tool_marker.get_position();
    const Vec3 offset = {0.0f, 0.0f, m_tool_marker.get_offset_z()};
    const Vec3 position = origin + offset;
    glsafe(glUniform3fv(m_uni_tool_marker_world_origin, 1, position.data()));
    glsafe(glUniform1f(m_uni_tool_marker_scale_factor, m_tool_marker_scale_factor));
    glsafe(glUniformMatrix4fv(m_uni_tool_marker_view_matrix, 1, GL_FALSE, view_matrix.data()));
    glsafe(glUniformMatrix4fv(m_uni_tool_marker_projection_matrix, 1, GL_FALSE, projection_matrix.data()));
    const Color &color = m_tool_marker.get_color();
    glsafe(glUniform4f(m_uni_tool_marker_color_base, color[0], color[1], color[2], m_tool_marker.get_alpha()));

    m_tool_marker.render();

    glsafe(glBlendFunc(GL_SRC_ALPHA, curr_blend_func));
    if (!curr_blend)
        glsafe(glDisable(GL_BLEND));
    if (curr_depth_mask == GL_TRUE)
        glsafe(glDepthMask(GL_TRUE));
    if (curr_cull_face)
        glsafe(glEnable(GL_CULL_FACE));

    glsafe(glUseProgram(curr_shader));
}
#endif // VGCODE_ENABLE_COG_AND_TOOL_MARKERS

// Clipping plane for the preview
void ViewerImpl::set_clipping_plane(float nx, float ny, float nz, float offset)
{
    const bool was_active = clipping_plane_active();
    m_clipping_plane = {nx, ny, nz, offset};
    if (clipping_plane_active() != was_active)
        m_settings.update_enabled_entities = true;
}

void ViewerImpl::reset_clipping_plane()
{
    const bool was_active = clipping_plane_active();
    m_clipping_plane = {0.0f, 0.0f, 1.0f, std::numeric_limits<float>::max()};
    if (was_active)
        m_settings.update_enabled_entities = true;
}

} // namespace libvgcode
