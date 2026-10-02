///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "ScenePasses.hpp"

#include "3DBed.hpp"
#include "3DScene.hpp"
#include "Camera.hpp"
#include "GLShader.hpp"
#include "OpenGLManager.hpp"
#include "RenderPassTimer.hpp"
#include "luminary/core/diagnostics/DebugCounters.hpp"
#include "luminary/presets/app_config/AppConfig.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace DSKY
{
using namespace Luminary;

// Eye-space key light direction (toward the light), the same headlight the
// Enhanced tier and the toolpath shaders use. Transformed into world space each
// frame, so the shadow always falls away from the viewer and no side of the
// model stays permanently dark while orbiting.
static const Vec3d KEY_LIGHT_DIR_EYE = Vec3d(-0.4574957, 0.4574957, 0.7624929);

// SSAO tuning (scene units are millimeters).
static constexpr float AO_RADIUS = 14.0f;
static constexpr float AO_BIAS = 0.8f;
static constexpr float AO_INTENSITY = 1.0f;
// 1 = AO at the G-buffer resolution (sharper fine/inter-line occlusion), 2 = half resolution. The G-buffer is
// at the output size, also under SSAA. At 1 the AO target and the G-buffer share a size, so the toolpath
// normal's depth samples, a whole number of texels away, land on texel centres.
static constexpr int AO_RESOLUTION_DIVISOR = 1;
// Distance in output pixels (G-buffer texels) to each side at which the SSAO normal of toolpath pixels is rebuilt
// from depth (safe 5 to 8, whole numbers): wide enough to span several layers, so the normal follows the wall, not
// the bead ridges.
static constexpr float AO_TOOLPATH_NORMAL_STEP_PX = 6.0f;

// Material response of the Full tier composite.
static constexpr float PBR_ROUGHNESS = 0.55f;
static constexpr float PBR_METALLIC = 0.03f;

// Target allocations read GL errors themselves: glsafe compiles out of release builds and, in debug
// builds, would consume the error before the allocation could see it.

// Clears errors left by earlier work, so the check after an allocation sees only its own.
static void drain_gl_errors()
{
    for (int i = 0; i < 16 && ::glGetError() != GL_NO_ERROR; ++i)
    {
    }
}

// The first error raised since the last drain (GL_NO_ERROR when none), draining the rest.
static GLenum take_gl_error()
{
    const GLenum error = ::glGetError();
    if (error != GL_NO_ERROR)
        drain_gl_errors();
    return error;
}

static std::string size_text(int width, int height)
{
    return std::to_string(width) + "x" + std::to_string(height);
}

static std::string allocation_failure_reason(int width, int height, GLenum error, bool forced)
{
    std::string reason = "render target could not be created at " + size_text(width, height);
    if (forced)
        reason += " (forced by PREFLIGHT_RENDER_FAIL)";
    else if (error == GL_OUT_OF_MEMORY)
        reason += " (out of GPU memory)";
    else if (error != GL_NO_ERROR)
        reason += " (refused by the driver)";
    else
        reason += " (framebuffer incomplete)";
    return reason;
}

// Largest render target per axis: the texture, renderbuffer and viewport limits together.
static void query_target_limits(int &max_width, int &max_height)
{
    GLint max_texture = 0;
    GLint max_renderbuffer = 0;
    GLint max_viewport[2] = {0, 0};
    glsafe(::glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture));
    glsafe(::glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_renderbuffer));
    glsafe(::glGetIntegerv(GL_MAX_VIEWPORT_DIMS, max_viewport));
    const int common = std::min(int(max_texture), int(max_renderbuffer));
    max_width = std::min(common, int(max_viewport[0]));
    max_height = std::min(common, int(max_viewport[1]));
}

#ifdef PREFLIGHT_TEST_HOOKS
// PREFLIGHT_RENDER_FAIL=scene or gbuffer (comma separated for both) makes that target's allocation
// fail, to exercise the failure path. Read once per session.
static bool render_fail_forced(const std::string &target)
{
    static const std::string value = []()
    {
        const char *env = std::getenv("PREFLIGHT_RENDER_FAIL");
        return env != nullptr ? std::string(env) : std::string();
    }();
    size_t start = 0;
    while (start <= value.size())
    {
        const size_t end = std::min(value.find(',', start), value.size());
        if (value.compare(start, end - start, target) == 0)
            return true;
        start = end + 1;
    }
    return false;
}
#endif // PREFLIGHT_TEST_HOOKS

ScenePasses::~ScenePasses()
{
    release();
}

bool ScenePasses::wants_full(const AppConfig *config)
{
    return config != nullptr && config->get("canvas_lighting_quality") == "full";
}

void ScenePasses::release_targets()
{
    release();
    m_alloc_failures = 0;
    m_failed_width = m_failed_height = 0;
    m_refused_width = m_refused_height = 0;
    m_alloc_failure_reason.clear();
}

void ScenePasses::set_ao_toolpath_normals(bool enabled)
{
    m_ao_toolpath_normal_step = enabled ? AO_TOOLPATH_NORMAL_STEP_PX : 0.0f;
}

bool ScenePasses::capabilities_ok()
{
    if (m_caps == ECaps::Unknown)
    {
        bool ok = OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 1) &&
                  OpenGLManager::are_framebuffers_supported() && m_get_shader != nullptr;
        if (ok)
        {
            query_target_limits(m_max_target_width, m_max_target_height);
            // A pass shader that failed to compile resolves to its fallback under a
            // different name; any such substitution disables the whole tier.
            for (const char *name :
                 {"phong_full", "shadowmap", "gbuffer", "gbuffer_bed", "ssao", "ssao_blur", "bed_overlay"})
            {
                GLShaderProgram *shader = m_get_shader(name);
                if (shader == nullptr || shader->get_name() != name)
                {
                    ok = false;
                    m_missing_shader = name;
                    DBG_COUNT_LOAD("RENDER_FULL_SHADER_FALLBACK");
                    break;
                }
            }
        }
        m_caps = ok ? ECaps::Ok : ECaps::Unavailable;
    }
    return m_caps == ECaps::Ok;
}

size_t ScenePasses::bytes() const
{
    size_t total = m_shadow_tex != 0 ? size_t(SHADOW_MAP_SIZE) * SHADOW_MAP_SIZE * 4 : 0;
    // G-buffer normal (RGB10_A2) and 24-bit depth (stored as 4 bytes), and the two AO targets (R8)
    if (m_gbuffer_fbo != 0)
        total += size_t(m_width) * size_t(m_height) * (4 + 4) + size_t(m_ao_width) * size_t(m_ao_height) * 2;
    return total;
}

bool ScenePasses::ensure_targets(int width, int height)
{
    if (m_gbuffer_fbo != 0 && width == m_width && height == m_height)
        return true;

    // A size over the GPU's limits would give an incomplete framebuffer: refused before allocating
    const bool shadow_fits = SHADOW_MAP_SIZE <= std::min(m_max_target_width, m_max_target_height);
    if (!shadow_fits || width > m_max_target_width || height > m_max_target_height)
    {
        if (width != m_refused_width || height != m_refused_height)
        {
            m_refused_width = width;
            m_refused_height = height;
            DBG_COUNT_LOAD("RENDER_FULL_TARGET_OVER_LIMIT");
        }
        m_inactive_reason = (shadow_fits ? "render target " + size_text(width, height)
                                         : "shadow map " + size_text(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE)) +
                            " exceeds the GPU limit of " + size_text(m_max_target_width, m_max_target_height);
        return false;
    }

    // A failed size is not retried; after MAX_ALLOC_FAILURES in a row new sizes are not either
    if (m_alloc_failures >= MAX_ALLOC_FAILURES)
    {
        m_inactive_reason = m_alloc_failure_reason + ", stopped after " + std::to_string(MAX_ALLOC_FAILURES) +
                            " failures";
        return false;
    }
    if (m_alloc_failures > 0 && width == m_failed_width && height == m_failed_height)
    {
        m_inactive_reason = m_alloc_failure_reason;
        return false;
    }

    bool forced = false;
#ifdef PREFLIGHT_TEST_HOOKS
    forced = render_fail_forced("gbuffer");
#endif

    const bool new_shadow = m_shadow_fbo == 0;
    if (new_shadow)
    {
        glsafe(::glGenTextures(1, &m_shadow_tex));
        glsafe(::glGenFramebuffers(1, &m_shadow_fbo));
    }
    if (m_gbuffer_fbo == 0)
    {
        glsafe(::glGenFramebuffers(1, &m_gbuffer_fbo));
        glsafe(::glGenTextures(1, &m_gbuffer_tex));
        glsafe(::glGenTextures(1, &m_gbuffer_depth_tex));
        glsafe(::glGenFramebuffers(2, m_ao_fbo));
        glsafe(::glGenTextures(2, m_ao_tex));
    }

    m_width = width;
    m_height = height;
    m_ao_width = std::max(1, width / AO_RESOLUTION_DIVISOR);
    m_ao_height = std::max(1, height / AO_RESOLUTION_DIVISOR);

    drain_gl_errors();
    GLenum error = GL_NO_ERROR;
    bool complete = !forced;
    if (complete && new_shadow)
    {
        ::glBindTexture(GL_TEXTURE_2D, m_shadow_tex);
        ::glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0, GL_DEPTH_COMPONENT,
                       GL_FLOAT, nullptr);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        ::glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        ::glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_fbo);
        ::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadow_tex, 0);
        ::glDrawBuffer(GL_NONE);
        ::glReadBuffer(GL_NONE);
        complete = ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        error = take_gl_error();
        complete = complete && error == GL_NO_ERROR;
    }
    if (complete)
    {
        // Eye-space normal mapped to [0, 1]; alpha marks geometry
        ::glBindTexture(GL_TEXTURE_2D, m_gbuffer_tex);
        ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2, width, height, 0, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV,
                       nullptr);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // The depth attachment, sampled by SSAO to rebuild eye-space positions
        ::glBindTexture(GL_TEXTURE_2D, m_gbuffer_depth_tex);
        ::glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

        ::glBindFramebuffer(GL_FRAMEBUFFER, m_gbuffer_fbo);
        ::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gbuffer_tex, 0);
        ::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_gbuffer_depth_tex, 0);
        complete = ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        for (int i = 0; i < 2 && complete; ++i)
        {
            ::glBindTexture(GL_TEXTURE_2D, m_ao_tex[i]);
            ::glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_ao_width, m_ao_height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            ::glBindFramebuffer(GL_FRAMEBUFFER, m_ao_fbo[i]);
            ::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ao_tex[i], 0);
            complete = ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        error = take_gl_error();
        complete = complete && error == GL_NO_ERROR;
    }

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    if (!complete)
    {
        // Allocation failure at this size: the tier stays available and retries at the next size
        release();
        ++m_alloc_failures;
        m_failed_width = width;
        m_failed_height = height;
        m_alloc_failure_reason = allocation_failure_reason(width, height, error, forced);
        m_inactive_reason = m_alloc_failure_reason;
        DBG_COUNT_LOAD("RENDER_FULL_TARGET_FAILED");
        return false;
    }
    m_alloc_failures = 0;
    return true;
}

void ScenePasses::release()
{
    if (m_shadow_fbo != 0)
        glsafe(::glDeleteFramebuffers(1, &m_shadow_fbo));
    if (m_shadow_tex != 0)
        glsafe(::glDeleteTextures(1, &m_shadow_tex));
    if (m_gbuffer_fbo != 0)
        glsafe(::glDeleteFramebuffers(1, &m_gbuffer_fbo));
    if (m_gbuffer_tex != 0)
        glsafe(::glDeleteTextures(1, &m_gbuffer_tex));
    if (m_gbuffer_depth_tex != 0)
        glsafe(::glDeleteTextures(1, &m_gbuffer_depth_tex));
    if (m_ao_fbo[0] != 0)
        glsafe(::glDeleteFramebuffers(2, m_ao_fbo));
    if (m_ao_tex[0] != 0)
        glsafe(::glDeleteTextures(2, m_ao_tex));
    m_shadow_fbo = m_shadow_tex = 0;
    m_gbuffer_fbo = m_gbuffer_tex = m_gbuffer_depth_tex = 0;
    m_ao_fbo[0] = m_ao_fbo[1] = 0;
    m_ao_tex[0] = m_ao_tex[1] = 0;
    m_width = m_height = 0;
    m_active = false;
}

static void render_volume_geometry(const GLVolume &volume)
{
    GLModel &model = const_cast<GLModel &>(volume.model);
    if (volume.tverts_range == std::make_pair<size_t, size_t>(0, -1))
        model.render();
    else
        model.render(volume.tverts_range);
}

void ScenePasses::run(const GLVolumeCollection &volumes, const Camera &camera, const Vec3d &active_bed_offset,
                      const std::function<void()> &render_bed_geometry, int target_x, int target_y, int target_width,
                      int target_height, int pass_width, int pass_height, const ExtraCasters *extra_casters)
{
    m_active = false;
    m_inactive_reason.clear();
    // The visible pass looks the AO up at (gl_FragCoord - origin) / size of the rect it draws into, so an inset
    // viewport matches the origin-anchored AO target and a supersampled target reads the smaller AO texture.
    m_viewport_origin = Vec2f(float(target_x), float(target_y));
    m_viewport_size = Vec2f(float(target_width), float(target_height));

    const bool has_extra = extra_casters != nullptr && extra_casters->render != nullptr && extra_casters->bbox.defined;
    if (volumes.volumes.empty() && !has_extra)
    {
        m_inactive_reason = "nothing to shade";
        return;
    }
    if (!capabilities_ok())
    {
        m_inactive_reason = m_missing_shader.empty() ? "not supported by this OpenGL driver"
                                                     : "shader " + m_missing_shader + " failed to compile";
        return;
    }

    const int width = pass_width;
    const int height = pass_height;
    if (width < 10 || height < 10 || target_width < 10 || target_height < 10)
    {
        m_inactive_reason = "viewport too small";
        return;
    }

    // Shadow casters: active solids; modifiers stay out so they do not darken the
    // geometry they merely mark.
    std::vector<const GLVolume *> casters;
    BoundingBoxf3 casters_bb;
    for (const GLVolume *volume : volumes.volumes)
    {
        if (volume != nullptr && volume->is_active && !volume->is_modifier)
        {
            casters.emplace_back(volume);
            casters_bb.merge(volume->transformed_bounding_box());
        }
    }
    if (has_extra)
        casters_bb.merge(extra_casters->bbox);
    if ((casters.empty() && !has_extra) || !casters_bb.defined)
    {
        m_inactive_reason = "nothing to shade";
        return;
    }

    if (!ensure_targets(width, height))
        return;

    // Key light matrices: orthographic frustum fit around the casters, from the
    // camera-anchored light direction resolved into world space for this frame.
    const Matrix3d view_rotation = camera.get_view_matrix().matrix().block<3, 3>(0, 0);
    const Vec3d light_dir_world = (view_rotation.transpose() * KEY_LIGHT_DIR_EYE).normalized();
    const Vec3d center = casters_bb.center();
    const double radius = 0.5 * (casters_bb.max - casters_bb.min).norm() + 5.0;
    const Vec3d eye = center + light_dir_world * (radius + 5.0);
    const Vec3d forward = -light_dir_world;
    const Vec3d up_hint = std::abs(forward.z()) > 0.99 ? Vec3d(0.0, 1.0, 0.0) : Vec3d(0.0, 0.0, 1.0);
    const Vec3d side = forward.cross(up_hint).normalized();
    const Vec3d up = side.cross(forward);

    Matrix4d light_view = Matrix4d::Identity();
    light_view.block<1, 3>(0, 0) = side.transpose();
    light_view(0, 3) = -side.dot(eye);
    light_view.block<1, 3>(1, 0) = up.transpose();
    light_view(1, 3) = -up.dot(eye);
    light_view.block<1, 3>(2, 0) = (-forward).transpose();
    light_view(2, 3) = forward.dot(eye);

    const double z_near = 0.1;
    const double z_far = 2.0 * (radius + 5.0);
    Matrix4d light_proj = Matrix4d::Identity();
    light_proj(0, 0) = 1.0 / radius;
    light_proj(1, 1) = 1.0 / radius;
    light_proj(2, 2) = -2.0 / (z_far - z_near);
    light_proj(2, 3) = -(z_far + z_near) / (z_far - z_near);

    Matrix4d bias = Matrix4d::Identity();
    bias(0, 0) = bias(1, 1) = bias(2, 2) = 0.5;
    bias(0, 3) = bias(1, 3) = bias(2, 3) = 0.5;
    m_shadow_vp = bias * light_proj * light_view;
    // The orthographic frustum spans 2 * radius over the map's texels
    m_shadow_texel = float(2.0 * radius / double(SHADOW_MAP_SIZE));

    // Camera-anchored light: constant in eye space by construction.
    m_key_light_eye = KEY_LIGHT_DIR_EYE.normalized().cast<float>();

    // Save the pieces of state the passes disturb.
    const bool cull_was_enabled = ::glIsEnabled(GL_CULL_FACE) != GL_FALSE;
    const bool blend_was_enabled = ::glIsEnabled(GL_BLEND) != GL_FALSE;
    float prev_clear_color[4] = {0.f, 0.f, 0.f, 0.f};
    glsafe(::glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear_color));

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_TRUE));

    // Pass 1: depth from the light into the shadow map.
    GLShaderProgram *shadow_shader = m_get_shader("shadowmap");
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_fbo));
    glsafe(::glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    shadow_shader->start_using();
    shadow_shader->set_uniform("projection_matrix", light_proj);
    for (const GLVolume *volume : casters)
    {
        const Matrix4d view_model = light_view * volume->world_matrix().matrix();
        shadow_shader->set_uniform("view_model_matrix", view_model);
        render_volume_geometry(*volume);
    }
    shadow_shader->stop_using();
    if (has_extra)
        extra_casters->render(light_view, light_proj, eye, false);
    RENDER_PASS_MARK("full_shadow");

    // Pass 2: eye-space normal into the G-buffer and depth into its sampled attachment, objects plus bed, at the
    // pass size; the toolpath callback draws into this viewport too.
    const Transform3d &cam_view = camera.get_view_matrix();
    GLShaderProgram *gbuffer_shader = m_get_shader("gbuffer");
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_gbuffer_fbo));
    glsafe(::glViewport(0, 0, m_width, m_height));
    glsafe(::glClearColor(0.f, 0.f, 0.f, 0.f));
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    gbuffer_shader->start_using();
    gbuffer_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    for (const GLVolume *volume : casters)
    {
        const Transform3d world = volume->world_matrix();
        gbuffer_shader->set_uniform("view_model_matrix", cam_view * world);
        const Matrix3d view_normal = cam_view.matrix().block<3, 3>(0, 0) *
                                     world.matrix().block<3, 3>(0, 0).inverse().transpose();
        gbuffer_shader->set_uniform("view_normal_matrix", view_normal);
        render_volume_geometry(*volume);
    }
    gbuffer_shader->stop_using();
    if (has_extra)
        extra_casters->render(Matrix4d(cam_view.matrix()), Matrix4d(camera.get_projection_matrix().matrix()),
                              camera.get_position(), true);

    GLShaderProgram *gbuffer_bed_shader = m_get_shader("gbuffer_bed");
    gbuffer_bed_shader->start_using();
    const Transform3d bed_model = Transform3d(Eigen::Translation3d(active_bed_offset));
    gbuffer_bed_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    gbuffer_bed_shader->set_uniform("view_model_matrix", cam_view * bed_model);
    gbuffer_bed_shader->set_uniform("view_normal_matrix", Matrix3d(cam_view.matrix().block<3, 3>(0, 0)));
    if (render_bed_geometry)
        render_bed_geometry();
    gbuffer_bed_shader->stop_using();
    RENDER_PASS_MARK("full_gbuffer");

    // Pass 3: SSAO at the AO target resolution, then a separable blur in the same viewport.
    if (!m_fs_quad.is_initialized())
    {
        GLModel::Geometry quad;
        quad.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P2T2};
        quad.reserve_vertices(4);
        quad.reserve_indices(6);
        quad.add_vertex(Vec2f(-1.0f, -1.0f), Vec2f(0.0f, 0.0f));
        quad.add_vertex(Vec2f(1.0f, -1.0f), Vec2f(1.0f, 0.0f));
        quad.add_vertex(Vec2f(1.0f, 1.0f), Vec2f(1.0f, 1.0f));
        quad.add_vertex(Vec2f(-1.0f, 1.0f), Vec2f(0.0f, 1.0f));
        quad.add_triangle(0, 1, 2);
        quad.add_triangle(2, 3, 0);
        m_fs_quad.init_from(std::move(quad));
    }

    glsafe(::glDisable(GL_DEPTH_TEST));
    // Depth on unit 1 beside the normals on unit 0; unit 1 gets the shadow map back below
    glsafe(::glActiveTexture(GL_TEXTURE1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_gbuffer_depth_tex));
    glsafe(::glActiveTexture(GL_TEXTURE0));

    GLShaderProgram *ssao_shader = m_get_shader("ssao");
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_ao_fbo[0]));
    glsafe(::glViewport(0, 0, m_ao_width, m_ao_height));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_gbuffer_tex));
    ssao_shader->start_using();
    ssao_shader->set_uniform("gbuffer_tex", 0);
    ssao_shader->set_uniform("depth_tex", 1);
    ssao_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    ssao_shader->set_uniform("inv_projection_matrix", Matrix4d(camera.get_projection_matrix().matrix().inverse()));
    ssao_shader->set_uniform("ao_radius", AO_RADIUS);
    ssao_shader->set_uniform("ao_bias", AO_BIAS);
    ssao_shader->set_uniform("ao_intensity", AO_INTENSITY);
    ssao_shader->set_uniform("ao_toolpath_normal_step", m_ao_toolpath_normal_step);
    m_fs_quad.render();
    ssao_shader->stop_using();
    RENDER_PASS_MARK("full_ssao");

    GLShaderProgram *blur_shader = m_get_shader("ssao_blur");
    blur_shader->start_using();
    blur_shader->set_uniform("ao_tex", 0);

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_ao_fbo[1]));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_ao_tex[0]));
    blur_shader->set_uniform("blur_dir", Vec2f(1.0f / float(m_ao_width), 0.0f));
    m_fs_quad.render();

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_ao_fbo[0]));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_ao_tex[1]));
    blur_shader->set_uniform("blur_dir", Vec2f(0.0f, 1.0f / float(m_ao_height)));
    m_fs_quad.render();
    blur_shader->stop_using();
    RENDER_PASS_MARK("full_blur");

    // Back to the default framebuffer; expose the results on fixed texture units.
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    glsafe(::glActiveTexture(GL_TEXTURE1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_shadow_tex));
    glsafe(::glActiveTexture(GL_TEXTURE2));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_ao_tex[0]));
    glsafe(::glActiveTexture(GL_TEXTURE0));

    glsafe(::glClearColor(prev_clear_color[0], prev_clear_color[1], prev_clear_color[2], prev_clear_color[3]));
    glsafe(::glEnable(GL_DEPTH_TEST));
    if (cull_was_enabled)
        glsafe(::glEnable(GL_CULL_FACE));
    if (blend_was_enabled)
        glsafe(::glEnable(GL_BLEND));

    m_active = true;
}

void ScenePasses::bind_scene_uniforms(GLShaderProgram &shader) const
{
    shader.set_uniform("shadow_vp", m_shadow_vp);
    shader.set_uniform("shadow_tex", 1);
    shader.set_uniform("ao_tex", 2);
    shader.set_uniform("viewport_size", m_viewport_size);
    shader.set_uniform("viewport_origin", m_viewport_origin);
    shader.set_uniform("key_light_eye", m_key_light_eye);
    shader.set_uniform("pbr_roughness", PBR_ROUGHNESS);
    shader.set_uniform("pbr_metallic", PBR_METALLIC);
}

void ScenePasses::render_bed_overlay(Bed3D &bed, const Camera &camera, const Vec3d &active_bed_offset)
{
    if (!m_active)
        return;
    GLShaderProgram *shader = m_get_shader("bed_overlay");
    if (shader == nullptr || shader->get_name() != "bed_overlay")
        return;

    const bool blend_was_enabled = ::glIsEnabled(GL_BLEND) != GL_FALSE;

    shader->start_using();
    const Transform3d bed_model = Transform3d(Eigen::Translation3d(active_bed_offset));
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * bed_model);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    // The catcher's vertices are in bed-local space; the shadow map is looked up in world space, so the
    // active bed's translation goes into the matrix (the volumes' shader already works from world positions).
    shader->set_uniform("shadow_vp", Matrix4d(m_shadow_vp * bed_model.matrix()));
    shader->set_uniform("shadow_tex", 1);
    shader->set_uniform("ao_tex", 2);
    shader->set_uniform("viewport_size", m_viewport_size);
    shader->set_uniform("viewport_origin", m_viewport_origin);

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_FALSE));
    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glPolygonOffset(-1.0f, -1.0f));

    bed.render_shadow_catcher_geometry();

    glsafe(::glDisable(GL_POLYGON_OFFSET_FILL));
    glsafe(::glDepthMask(GL_TRUE));
    if (!blend_was_enabled)
        glsafe(::glDisable(GL_BLEND));
    shader->stop_using();
}

SceneSupersampler::~SceneSupersampler()
{
    release();
}

#ifdef PREFLIGHT_TEST_HOOKS
bool SceneSupersampler::s_test_force_offscreen = false;
#endif

double SceneSupersampler::requested_scale(const AppConfig *config)
{
    if (config == nullptr)
        return 1.0;
    const std::string value = config->get("canvas_ssaa_scale");
    if (value.empty() || value == "off")
        return 1.0;
    // The dropdown writes 1.5 and 2; a larger value up to 4 renders the reference frames AA is measured against
    char *end = nullptr;
    const double scale = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !(scale >= 1.0 && scale <= 4.0))
    {
        DBG_COUNT_LOAD("RENDER_SETTING_UNKNOWN_VALUE");
        return 1.0;
    }
    return scale;
}

bool SceneSupersampler::capabilities_ok()
{
    if (m_caps == ECaps::Unknown)
    {
        bool ok = OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 1) &&
                  OpenGLManager::are_framebuffers_supported() && m_get_shader != nullptr;
        if (ok)
        {
            query_target_limits(m_max_target_width, m_max_target_height);
            GLShaderProgram *shader = m_get_shader("ssaa_resolve");
            ok = shader != nullptr && shader->get_name() == "ssaa_resolve";
        }
        m_caps = ok ? ECaps::Ok : ECaps::Unavailable;
    }
    return m_caps == ECaps::Ok;
}

void SceneSupersampler::release()
{
    if (m_fbo != 0)
        glsafe(::glDeleteFramebuffers(1, &m_fbo));
    if (m_color_tex != 0)
        glsafe(::glDeleteTextures(1, &m_color_tex));
    if (m_depth_rb != 0)
        glsafe(::glDeleteRenderbuffers(1, &m_depth_rb));
    m_fbo = m_color_tex = m_depth_rb = 0;
    m_width = m_height = 0;
    m_active = false;
}

bool SceneSupersampler::begin(int native_width, int native_height, double scale)
{
    m_active = false;
    m_effective_scale = 1.0;
    m_reason.clear();

    bool force_offscreen = false;
#ifdef PREFLIGHT_TEST_HOOKS
    force_offscreen = s_test_force_offscreen;
#endif
    if (scale <= 1.0 && !force_offscreen)
    {
        // Supersampling is off: free the target, and let turning it on again retry failed allocations
        release();
        m_alloc_failures = 0;
        m_capped_scale = 0.0;
        return false;
    }
    if (native_width < 10 || native_height < 10)
        return false;
    if (!capabilities_ok())
    {
        m_reason = "not supported by this OpenGL driver";
        return false;
    }

    // The largest scale the GPU's texture, renderbuffer and viewport limits allow on both axes
    const double max_scale = std::min(double(m_max_target_width) / double(native_width),
                                      double(m_max_target_height) / double(native_height));
    const double effective_scale = std::max(1.0, std::min(scale, max_scale));
    if (effective_scale < scale)
    {
        m_reason = "limited by the GPU's maximum render target size of " +
                   size_text(m_max_target_width, m_max_target_height);
        if (effective_scale != m_capped_scale)
        {
            m_capped_scale = effective_scale;
            DBG_COUNT_LOAD("RENDER_SSAA_SCALE_CAPPED");
        }
    }
    else
        m_capped_scale = 0.0;
    if (effective_scale < 1.05 && !force_offscreen)
        return false;

    const int width = int(std::lround(native_width * effective_scale));
    const int height = int(std::lround(native_height * effective_scale));
    // Reached only by the forced offscreen path with a viewport already over the limits
    if (width > m_max_target_width || height > m_max_target_height)
    {
        m_reason = "viewport " + size_text(native_width, native_height) + " exceeds the GPU limit of " +
                   size_text(m_max_target_width, m_max_target_height);
        return false;
    }

    // A new requested scale is a setting change: failed allocations are retried
    if (m_alloc_failures > 0 && scale != m_failed_scale)
        m_alloc_failures = 0;

    if (width != m_width || height != m_height || m_fbo == 0)
    {
        // A failed size is not retried; after MAX_ALLOC_FAILURES in a row new sizes are not either
        if (m_alloc_failures >= MAX_ALLOC_FAILURES)
        {
            m_reason = m_alloc_failure_reason + ", stopped after " + std::to_string(MAX_ALLOC_FAILURES) + " failures";
            return false;
        }
        if (m_alloc_failures > 0 && width == m_failed_width && height == m_failed_height)
        {
            m_reason = m_alloc_failure_reason;
            return false;
        }

        if (m_fbo == 0)
        {
            glsafe(::glGenFramebuffers(1, &m_fbo));
            glsafe(::glGenTextures(1, &m_color_tex));
            glsafe(::glGenRenderbuffers(1, &m_depth_rb));
        }
        m_width = width;
        m_height = height;

        bool forced = false;
#ifdef PREFLIGHT_TEST_HOOKS
        forced = render_fail_forced("scene");
#endif
        drain_gl_errors();
        GLenum error = GL_NO_ERROR;
        bool complete = !forced;
        if (complete)
        {
            ::glBindTexture(GL_TEXTURE_2D, m_color_tex);
            ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            // The resolve reads texels directly
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            ::glBindTexture(GL_TEXTURE_2D, 0);

            // Depth serves the scene and the pivot read-back only, so it is never sampled
            ::glBindRenderbuffer(GL_RENDERBUFFER, m_depth_rb);
            ::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
            ::glBindRenderbuffer(GL_RENDERBUFFER, 0);

            ::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            ::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color_tex, 0);
            ::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth_rb);
            complete = ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            error = take_gl_error();
            complete = complete && error == GL_NO_ERROR;
        }
        if (!complete)
        {
            // Allocation failure at this size: supersampling stays available and retries at the next size
            glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
            release();
            ++m_alloc_failures;
            m_failed_width = width;
            m_failed_height = height;
            m_failed_scale = scale;
            m_alloc_failure_reason = allocation_failure_reason(width, height, error, forced);
            m_reason = m_alloc_failure_reason;
            DBG_COUNT_LOAD("RENDER_SSAA_TARGET_FAILED");
            return false;
        }
        m_alloc_failures = 0;
    }
    else
        glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));

    glsafe(::glViewport(0, 0, m_width, m_height));
    m_effective_scale = effective_scale;
    m_active = true;
    return true;
}

void SceneSupersampler::rebind() const
{
    if (!m_active)
        return;
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));
    glsafe(::glViewport(0, 0, m_width, m_height));
}

void SceneSupersampler::end(int native_x, int native_y, int native_width, int native_height)
{
    if (!m_active)
        return;
    m_active = false;

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
    glsafe(::glViewport(native_x, native_y, native_width, native_height));

    GLShaderProgram *shader = m_get_shader("ssaa_resolve");
    if (shader == nullptr || shader->get_name() != "ssaa_resolve")
        return;

    if (!m_fs_quad.is_initialized())
    {
        GLModel::Geometry quad;
        quad.format = {GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P2T2};
        quad.reserve_vertices(4);
        quad.reserve_indices(6);
        quad.add_vertex(Vec2f(-1.0f, -1.0f), Vec2f(0.0f, 0.0f));
        quad.add_vertex(Vec2f(1.0f, -1.0f), Vec2f(1.0f, 0.0f));
        quad.add_vertex(Vec2f(1.0f, 1.0f), Vec2f(1.0f, 1.0f));
        quad.add_vertex(Vec2f(-1.0f, 1.0f), Vec2f(0.0f, 1.0f));
        quad.add_triangle(0, 1, 2);
        quad.add_triangle(2, 3, 0);
        m_fs_quad.init_from(std::move(quad));
    }

    // Color only: nothing drawn after the resolve tests depth, so the quad neither tests nor writes it
    const bool blend_was_enabled = ::glIsEnabled(GL_BLEND) != GL_FALSE;
    const bool depth_test_was_enabled = ::glIsEnabled(GL_DEPTH_TEST) != GL_FALSE;
    GLboolean depth_mask_was = GL_TRUE;
    glsafe(::glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_was));
    glsafe(::glDisable(GL_BLEND));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_FALSE));

    shader->start_using();
    shader->set_uniform("color_tex", 0);
    shader->set_uniform("native_size", Vec2f(float(native_width), float(native_height)));
    shader->set_uniform("native_origin", std::array<int, 2>{native_x, native_y});
    glsafe(::glActiveTexture(GL_TEXTURE0));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_color_tex));
    m_fs_quad.render();
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    shader->stop_using();

    glsafe(::glDepthMask(depth_mask_was));
    if (depth_test_was_enabled)
        glsafe(::glEnable(GL_DEPTH_TEST));
    if (blend_was_enabled)
        glsafe(::glEnable(GL_BLEND));
}

} // namespace DSKY
