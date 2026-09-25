///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "GLModel.hpp"
#include "luminary/geometry/primitives/BoundingBox.hpp"
#include "luminary/geometry/primitives/Point.hpp"

#include <functional>
#include <string>

namespace Luminary
{
class GLVolumeCollection;
class GLShaderProgram;
class AppConfig;
} // namespace Luminary

namespace DSKY
{
using namespace Luminary;

struct Camera;
class Bed3D;

// Offscreen render passes for the "Full" lighting tier: a shadow map from a fixed
// world-space key light plus a screen-space ambient-occlusion chain over a scene
// G-buffer. Results are exposed as textures on fixed units (1: shadow map,
// 2: blurred AO) and as uniforms for the phong_full and bed_overlay shaders.
// When a capability or shader is missing the passes stay inactive and the frame
// renders exactly as the Enhanced tier. Toolkit-agnostic: shader access is
// injected, no windowing types appear here.
class ScenePasses
{
public:
    using ShaderGetter = std::function<GLShaderProgram *(const std::string &)>;

    explicit ScenePasses(ShaderGetter get_shader) : m_get_shader(std::move(get_shader)) {}
    ~ScenePasses();

    ScenePasses(const ScenePasses &) = delete;
    ScenePasses &operator=(const ScenePasses &) = delete;

    // The Full tier is a deliberate opt-in; "auto" never resolves to it.
    static bool wants_full(const AppConfig *config);

    // True when the passes ran for the current frame and their outputs are bound.
    bool active() const { return m_active; }
    void set_inactive() { m_active = false; }

    // Additional shadow casters and G-buffer contributors beyond the scene volumes
    // (the G-code preview toolpaths). The callback receives view and projection
    // matrices, the eye position the geometry should face, and whether this is the
    // G-buffer pass (false = shadow depth pass).
    struct ExtraCasters
    {
        std::function<void(const Matrix4d &view, const Matrix4d &projection, const Vec3d &eye, bool gbuffer)> render;
        BoundingBoxf3 bbox;
    };

    // Renders the shadow and AO passes. target_width/height name the render
    // target the scene will draw into (the viewport, or the supersampled target
    // when SSAA is active), so the G-buffer, AO chain and gl_FragCoord-based
    // uniforms match it. render_bed_geometry draws the bare bed surface so
    // contact occlusion lands on it. Leaves the default framebuffer bound; the
    // caller restores its own target and viewport afterwards.
    void run(const GLVolumeCollection &volumes, const Camera &camera, const Vec3d &active_bed_offset,
             const std::function<void()> &render_bed_geometry, int target_width, int target_height,
             const ExtraCasters *extra_casters = nullptr);

    // Pass outputs, for consumers outside the model-shader path (the G-code viewer).
    unsigned int shadow_texture_id() const { return m_shadow_tex; }
    unsigned int ao_texture_id() const { return m_ao_tex[0]; }
    const Matrix4d &shadow_vp() const { return m_shadow_vp; }
    const Vec2f &viewport_size() const { return m_viewport_size; }

    // Sets the Full-tier uniforms on an already started phong_full shader.
    void bind_scene_uniforms(GLShaderProgram &shader) const;

    // Shadow/AO darkening drawn over the already-rendered bed of any style.
    void render_bed_overlay(Bed3D &bed, const Camera &camera, const Vec3d &active_bed_offset);

private:
    enum class ECaps
    {
        Unknown,
        Ok,
        Unavailable
    };

    bool capabilities_ok();
    void ensure_targets(int width, int height);
    void release();

    ShaderGetter m_get_shader;

    unsigned int m_shadow_fbo{0};
    unsigned int m_shadow_tex{0};
    unsigned int m_gbuffer_fbo{0};
    unsigned int m_gbuffer_tex{0};
    unsigned int m_gbuffer_depth_rb{0};
    unsigned int m_ao_fbo[2]{0, 0};
    unsigned int m_ao_tex[2]{0, 0};

    int m_width{0};
    int m_height{0};
    int m_ao_width{0};
    int m_ao_height{0};

    Matrix4d m_shadow_vp{Matrix4d::Identity()};
    Vec3f m_key_light_eye{0.f, 0.f, 1.f};
    Vec2f m_viewport_size{0.f, 0.f};

    ECaps m_caps{ECaps::Unknown};
    bool m_active{false};

    GLModel m_fs_quad;

    static constexpr int SHADOW_MAP_SIZE = 2048;
};

// Supersampled scene rendering (SSAA): the 3D scene renders into an offscreen
// target at a multiple of the viewport size and resolves to the native
// framebuffer through a filtered downsample that also carries depth, so passes
// reading the depth buffer afterwards keep working. Independent of the lighting
// tier; while active, the window's MSAA no longer affects scene pixels.
class SceneSupersampler
{
public:
    using ShaderGetter = std::function<GLShaderProgram *(const std::string &)>;

    explicit SceneSupersampler(ShaderGetter get_shader) : m_get_shader(std::move(get_shader)) {}
    ~SceneSupersampler();

    SceneSupersampler(const SceneSupersampler &) = delete;
    SceneSupersampler &operator=(const SceneSupersampler &) = delete;

    // Requested render scale from the configuration: 1.0 when off.
    static double requested_scale(const AppConfig *config);

    bool active() const { return m_active; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Binds the offscreen target with the scaled viewport. Returns false and
    // stays inactive when capabilities or texture limits refuse the scale.
    // force_offscreen renders offscreen even at scale 1, so the screen-space
    // passes keep an origin viewport when the native one is offset.
    bool begin(int native_width, int native_height, double scale, bool force_offscreen = false);
    // Re-binds the target and viewport (after other passes changed them).
    void rebind() const;
    // Resolves to the native framebuffer and leaves it bound with the native
    // viewport; the frame's overlays render on top at native resolution.
    void end(int native_x, int native_y, int native_width, int native_height);

private:
    bool capabilities_ok();
    void release();

    ShaderGetter m_get_shader;

    unsigned int m_fbo{0};
    unsigned int m_color_tex{0};
    unsigned int m_depth_tex{0};

    int m_width{0};
    int m_height{0};

    enum class ECaps
    {
        Unknown,
        Ok,
        Unavailable
    };
    ECaps m_caps{ECaps::Unknown};
    bool m_active{false};

    GLModel m_fs_quad;
};

} // namespace DSKY

