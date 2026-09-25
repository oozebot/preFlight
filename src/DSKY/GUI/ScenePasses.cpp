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
#include "luminary/presets/app_config/AppConfig.hpp"

#if PREFLIGHT_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

#include <array>
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
// 1 = full-resolution AO (sharper fine/inter-line occlusion), 2 = half resolution.
static constexpr int AO_RESOLUTION_DIVISOR = 1;

// Material response of the Full tier composite.
static constexpr float PBR_ROUGHNESS = 0.55f;
static constexpr float PBR_METALLIC = 0.03f;

ScenePasses::~ScenePasses()
{
    release();
}

bool ScenePasses::wants_full(const AppConfig *config)
{
    return config != nullptr && config->get("canvas_lighting_quality") == "full";
}

#if PREFLIGHT_OPENGL_ES

// The Full tier is desktop-GL only; ES builds keep the class inert.
bool ScenePasses::capabilities_ok()
{
    m_caps = ECaps::Unavailable;
    return false;
}
void ScenePasses::ensure_targets(int, int) {}
void ScenePasses::release()
{
    m_active = false;
}
void ScenePasses::run(const GLVolumeCollection &, const Camera &, const Vec3d &, const std::function<void()> &, int,
                      int, const ExtraCasters *)
{
    m_active = false;
}
void ScenePasses::bind_scene_uniforms(GLShaderProgram &) const {}
void ScenePasses::render_bed_overlay(Bed3D &, const Camera &, const Vec3d &) {}

#else

bool ScenePasses::capabilities_ok()
{
    if (m_caps == ECaps::Unknown)
    {
        bool ok = OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 1) &&
                  OpenGLManager::are_framebuffers_supported() && m_get_shader != nullptr;
        if (ok)
        {
            // A pass shader that failed to compile resolves to its fallback under a
            // different name; any such substitution disables the whole tier.
            for (const char *name :
                 {"phong_full", "shadowmap", "gbuffer", "gbuffer_bed", "ssao", "ssao_blur", "bed_overlay"})
            {
                GLShaderProgram *shader = m_get_shader(name);
                if (shader == nullptr || shader->get_name() != name)
                {
                    ok = false;
                    break;
                }
            }
        }
        m_caps = ok ? ECaps::Ok : ECaps::Unavailable;
    }
    return m_caps == ECaps::Ok;
}

void ScenePasses::ensure_targets(int width, int height)
{
    if (m_shadow_fbo == 0)
    {
        glsafe(::glGenTextures(1, &m_shadow_tex));
        glsafe(::glBindTexture(GL_TEXTURE_2D, m_shadow_tex));
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0,
                              GL_DEPTH_COMPONENT, GL_FLOAT, nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
        const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glsafe(::glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL));

        glsafe(::glGenFramebuffers(1, &m_shadow_fbo));
        glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_fbo));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadow_tex, 0));
        glsafe(::glDrawBuffer(GL_NONE));
        glsafe(::glReadBuffer(GL_NONE));
        if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
            release();
            m_caps = ECaps::Unavailable;
            return;
        }
        glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    }

    if (width == m_width && height == m_height && m_gbuffer_fbo != 0)
        return;

    m_width = width;
    m_height = height;
    m_ao_width = std::max(1, width / AO_RESOLUTION_DIVISOR);
    m_ao_height = std::max(1, height / AO_RESOLUTION_DIVISOR);
    m_viewport_size = Vec2f(float(width), float(height));

    if (m_gbuffer_fbo == 0)
    {
        glsafe(::glGenFramebuffers(1, &m_gbuffer_fbo));
        glsafe(::glGenTextures(1, &m_gbuffer_tex));
        glsafe(::glGenRenderbuffers(1, &m_gbuffer_depth_rb));
        glsafe(::glGenFramebuffers(2, m_ao_fbo));
        glsafe(::glGenTextures(2, m_ao_tex));
    }

    glsafe(::glBindTexture(GL_TEXTURE_2D, m_gbuffer_tex));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, m_gbuffer_depth_rb));
    glsafe(::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height));
    glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, 0));

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_gbuffer_fbo));
    glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gbuffer_tex, 0));
    glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_gbuffer_depth_rb));
    bool complete = ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    for (int i = 0; i < 2 && complete; ++i)
    {
        glsafe(::glBindTexture(GL_TEXTURE_2D, m_ao_tex[i]));
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_ao_width, m_ao_height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_ao_fbo[i]));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ao_tex[i], 0));
        complete &= ::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    if (!complete)
    {
        release();
        m_caps = ECaps::Unavailable;
    }
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
    if (m_gbuffer_depth_rb != 0)
        glsafe(::glDeleteRenderbuffers(1, &m_gbuffer_depth_rb));
    if (m_ao_fbo[0] != 0)
        glsafe(::glDeleteFramebuffers(2, m_ao_fbo));
    if (m_ao_tex[0] != 0)
        glsafe(::glDeleteTextures(2, m_ao_tex));
    m_shadow_fbo = m_shadow_tex = 0;
    m_gbuffer_fbo = m_gbuffer_tex = m_gbuffer_depth_rb = 0;
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
                      const std::function<void()> &render_bed_geometry, int target_width, int target_height,
                      const ExtraCasters *extra_casters)
{
    m_active = false;

    const bool has_extra = extra_casters != nullptr && extra_casters->render != nullptr && extra_casters->bbox.defined;
    if ((volumes.volumes.empty() && !has_extra) || !capabilities_ok())
        return;

    const int width = target_width;
    const int height = target_height;
    if (width < 10 || height < 10)
        return;

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
        return;

    ensure_targets(width, height);
    if (m_caps != ECaps::Ok)
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

    // Pass 2: eye-space normal and linear depth into the G-buffer, objects plus bed.
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

    // Pass 3: SSAO at half resolution, then a separable blur.
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
    glsafe(::glActiveTexture(GL_TEXTURE0));

    GLShaderProgram *ssao_shader = m_get_shader("ssao");
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_ao_fbo[0]));
    glsafe(::glViewport(0, 0, m_ao_width, m_ao_height));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_gbuffer_tex));
    ssao_shader->start_using();
    ssao_shader->set_uniform("gbuffer_tex", 0);
    ssao_shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    ssao_shader->set_uniform("inv_projection_matrix", Matrix4d(camera.get_projection_matrix().matrix().inverse()));
    ssao_shader->set_uniform("ao_radius", AO_RADIUS);
    ssao_shader->set_uniform("ao_bias", AO_BIAS);
    ssao_shader->set_uniform("ao_intensity", AO_INTENSITY);
    m_fs_quad.render();
    ssao_shader->stop_using();

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

#endif // PREFLIGHT_OPENGL_ES

SceneSupersampler::~SceneSupersampler()
{
    release();
}

double SceneSupersampler::requested_scale(const AppConfig *config)
{
    if (config == nullptr)
        return 1.0;
    const std::string value = config->get("canvas_ssaa_scale");
    if (value == "1.5")
        return 1.5;
    if (value == "2")
        return 2.0;
    return 1.0;
}

#if PREFLIGHT_OPENGL_ES

bool SceneSupersampler::capabilities_ok()
{
    m_caps = ECaps::Unavailable;
    return false;
}
void SceneSupersampler::release()
{
    m_active = false;
}
bool SceneSupersampler::begin(int, int, double, bool)
{
    m_active = false;
    return false;
}
void SceneSupersampler::rebind() const {}
void SceneSupersampler::end(int, int, int, int) {}

#else

bool SceneSupersampler::capabilities_ok()
{
    if (m_caps == ECaps::Unknown)
    {
        bool ok = OpenGLManager::get_gl_info().is_version_greater_or_equal_to(3, 1) &&
                  OpenGLManager::are_framebuffers_supported() && m_get_shader != nullptr;
        if (ok)
        {
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
    if (m_depth_tex != 0)
        glsafe(::glDeleteTextures(1, &m_depth_tex));
    m_fbo = m_color_tex = m_depth_tex = 0;
    m_width = m_height = 0;
    m_active = false;
}

bool SceneSupersampler::begin(int native_width, int native_height, double scale, bool force_offscreen)
{
    m_active = false;

    if ((scale <= 1.0 && !force_offscreen) || native_width < 10 || native_height < 10 || !capabilities_ok())
        return false;

    GLint max_tex_size = 0;
    glsafe(::glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size));
    const double max_scale = double(max_tex_size) / double(std::max(native_width, native_height));
    const double effective_scale = std::max(1.0, std::min(scale, max_scale));
    if (effective_scale < 1.05 && !force_offscreen)
        return false;

    const int width = int(std::lround(native_width * effective_scale));
    const int height = int(std::lround(native_height * effective_scale));

    if (width != m_width || height != m_height || m_fbo == 0)
    {
        if (m_fbo == 0)
        {
            glsafe(::glGenFramebuffers(1, &m_fbo));
            glsafe(::glGenTextures(1, &m_color_tex));
            glsafe(::glGenTextures(1, &m_depth_tex));
        }
        m_width = width;
        m_height = height;

        glsafe(::glBindTexture(GL_TEXTURE_2D, m_color_tex));
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        glsafe(::glBindTexture(GL_TEXTURE_2D, m_depth_tex));
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                              nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

        glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color_tex, 0));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth_tex, 0));
        if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
            release();
            m_caps = ECaps::Unavailable;
            return false;
        }
    }
    else
        glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, m_fbo));

    glsafe(::glViewport(0, 0, m_width, m_height));
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

    const bool blend_was_enabled = ::glIsEnabled(GL_BLEND) != GL_FALSE;
    glsafe(::glDisable(GL_BLEND));
    // Depth writes require an enabled depth test; ALWAYS makes the copy unconditional.
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthFunc(GL_ALWAYS));
    glsafe(::glDepthMask(GL_TRUE));

    shader->start_using();
    shader->set_uniform("color_tex", 0);
    shader->set_uniform("depth_tex", 1);
    glsafe(::glActiveTexture(GL_TEXTURE1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_depth_tex));
    glsafe(::glActiveTexture(GL_TEXTURE0));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_color_tex));
    m_fs_quad.render();
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    glsafe(::glActiveTexture(GL_TEXTURE1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
    glsafe(::glActiveTexture(GL_TEXTURE0));
    shader->stop_using();

    glsafe(::glDepthFunc(GL_LESS));
    if (blend_was_enabled)
        glsafe(::glEnable(GL_BLEND));
}

#endif // PREFLIGHT_OPENGL_ES

} // namespace DSKY
