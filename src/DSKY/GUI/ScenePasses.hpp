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
    // The frame renders without the passes; `reason` says why when the tier was asked for
    void set_inactive(const std::string &reason = std::string())
    {
        m_active = false;
        m_inactive_reason = reason;
    }
    // Why the passes did not run this frame, empty when they ran or were not asked for
    const std::string &inactive_reason() const { return m_inactive_reason; }
    // Bytes held by the pass targets (shadow map, G-buffer, ambient occlusion)
    size_t bytes() const;
    // Frees every pass target when the tier is turned off, and clears the record of failed
    // allocations so turning it on again retries them
    void release_targets();

    // The SSAO normal of toolpath pixels for the next run(): rebuilt from depth when `enabled`, else the G-buffer
    // normal. The G-buffer renders at the output size, so the step is a fixed number of output pixels.
    void set_ao_toolpath_normals(bool enabled);
    // That step in G-buffer texels, 0 when toolpath pixels keep the G-buffer normal
    float ao_toolpath_normal_step() const { return m_ao_toolpath_normal_step; }

    // Additional shadow casters and G-buffer contributors beyond the scene volumes
    // (the G-code preview toolpaths). The callback receives view and projection
    // matrices, the eye position the geometry should face, and whether this is the
    // G-buffer pass (false = shadow depth pass).
    struct ExtraCasters
    {
        std::function<void(const Matrix4d &view, const Matrix4d &projection, const Vec3d &eye, bool gbuffer)> render;
        BoundingBoxf3 bbox;
    };

    // Renders the shadow and AO passes. target_x/y/width/height name the rect the
    // scene will draw into (the viewport in the window, or the supersampled target
    // at its origin when SSAA is active); the gl_FragCoord-based uniforms normalize
    // by it. The G-buffer and AO chain render at pass_width x pass_height, the
    // output viewport size (equal to the target size without SSAA), and consumers
    // sample the AO texture with normalized coordinates. render_bed_geometry draws
    // the bare bed surface so contact occlusion lands on it. Leaves the default
    // framebuffer bound; the caller restores its own target and viewport afterwards.
    void run(const GLVolumeCollection &volumes, const Camera &camera, const Vec3d &active_bed_offset,
             const std::function<void()> &render_bed_geometry, int target_x, int target_y, int target_width,
             int target_height, int pass_width, int pass_height, const ExtraCasters *extra_casters = nullptr);

    // Pass outputs, for consumers outside the model-shader path (the G-code viewer).
    unsigned int shadow_texture_id() const { return m_shadow_tex; }
    unsigned int ao_texture_id() const { return m_ao_tex[0]; }
    const Matrix4d &shadow_vp() const { return m_shadow_vp; }
    // World size (mm) of one shadow-map texel in the last run()
    float shadow_texel() const { return m_shadow_texel; }
    const Vec2f &viewport_size() const { return m_viewport_size; }
    const Vec2f &viewport_origin() const { return m_viewport_origin; }

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
    // Creates or resizes the targets at the pass size; false with m_inactive_reason set when they are unusable
    bool ensure_targets(int width, int height);
    void release();

    ShaderGetter m_get_shader;

    unsigned int m_shadow_fbo{0};
    unsigned int m_shadow_tex{0};
    unsigned int m_gbuffer_fbo{0};
    // RGB10_A2 encoded normal, and the depth attachment SSAO samples to rebuild positions
    unsigned int m_gbuffer_tex{0};
    unsigned int m_gbuffer_depth_tex{0};
    unsigned int m_ao_fbo[2]{0, 0};
    unsigned int m_ao_tex[2]{0, 0};

    // G-buffer size: the pass size, the output viewport even under SSAA
    int m_width{0};
    int m_height{0};
    int m_ao_width{0};
    int m_ao_height{0};

    // Largest target the GPU accepts per axis (texture, renderbuffer and viewport limits)
    int m_max_target_width{0};
    int m_max_target_height{0};
    // A size over those limits, refused without allocating; counted once per size
    int m_refused_width{0};
    int m_refused_height{0};
    // Consecutive failed allocations and the size of the last one. A failed size is not
    // retried; after MAX_ALLOC_FAILURES only release_targets() re-enables allocation.
    int m_alloc_failures{0};
    int m_failed_width{0};
    int m_failed_height{0};
    std::string m_alloc_failure_reason;

    // The SSAO pass's ao_toolpath_normal_step uniform, in G-buffer texels
    float m_ao_toolpath_normal_step{0.0f};

    Matrix4d m_shadow_vp{Matrix4d::Identity()};
    float m_shadow_texel{0.0f};
    Vec3f m_key_light_eye{0.f, 0.f, 1.f};
    // Size of the framebuffer the consumers draw into (the scene target), not of the G-buffer
    Vec2f m_viewport_size{0.f, 0.f};
    Vec2f m_viewport_origin{0.f, 0.f};

    ECaps m_caps{ECaps::Unknown};
    bool m_active{false};
    std::string m_inactive_reason;
    // The pass shader that resolved to a fallback program, which disables the tier
    std::string m_missing_shader;

    GLModel m_fs_quad;

    static constexpr int SHADOW_MAP_SIZE = 2048;
    // Consecutive failed allocations after which resizes stop retrying
    static constexpr int MAX_ALLOC_FAILURES = 3;
};

// Supersampled scene rendering (SSAA): the 3D scene renders into an offscreen
// target at a multiple of the viewport size and resolves to the native
// framebuffer through a box-filtered downsample of color only; nothing drawn
// after the resolve tests depth. Independent of the lighting tier; while active,
// the window's MSAA no longer affects scene pixels.
class SceneSupersampler
{
public:
    using ShaderGetter = std::function<GLShaderProgram *(const std::string &)>;

    explicit SceneSupersampler(ShaderGetter get_shader) : m_get_shader(std::move(get_shader)) {}
    ~SceneSupersampler();

    SceneSupersampler(const SceneSupersampler &) = delete;
    SceneSupersampler &operator=(const SceneSupersampler &) = delete;

    // Requested render scale from the configuration: 1.0 when off, at most 4.
    static double requested_scale(const AppConfig *config);

    bool active() const { return m_active; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    // The scale the last begin() rendered at (1.0 when it did not), and why it is below the request
    double effective_scale() const { return m_effective_scale; }
    const std::string &reason() const { return m_reason; }
    // Bytes held by the target (RGBA8 color and 24-bit depth, stored as 4 bytes)
    size_t bytes() const { return m_fbo != 0 ? size_t(m_width) * size_t(m_height) * 8 : 0; }

#ifdef PREFLIGHT_TEST_HOOKS
    // Renders offscreen at scale 1 as well: the path inset viewports took before the scene rendered into the
    // window, kept as the pixel oracle for the direct path
    static bool s_test_force_offscreen;
#endif

    // Binds the offscreen target with the scaled viewport. Returns false and
    // stays inactive when supersampling is off (its target is then freed), or when
    // capabilities, GPU size limits or a failed allocation refuse the scale; the
    // scene then renders into the window's multisampled framebuffer.
    bool begin(int native_width, int native_height, double scale);
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
    // Depth for the scene while it renders; read back only by the pivot pick
    unsigned int m_depth_rb{0};

    int m_width{0};
    int m_height{0};
    double m_effective_scale{1.0};
    std::string m_reason;

    // Largest target the GPU accepts per axis (texture, renderbuffer and viewport limits)
    int m_max_target_width{0};
    int m_max_target_height{0};
    // The last scale the limits capped the request to, 0 when not capped; counted once per change
    double m_capped_scale{0.0};
    // Consecutive failed allocations, the target size of the last one and the requested scale
    // they happened at. A failed size is not retried; after MAX_ALLOC_FAILURES only a change of
    // the requested scale (turning supersampling off included) re-enables allocation.
    int m_alloc_failures{0};
    int m_failed_width{0};
    int m_failed_height{0};
    double m_failed_scale{0.0};
    std::string m_alloc_failure_reason;
    static constexpr int MAX_ALLOC_FAILURES = 3;

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
