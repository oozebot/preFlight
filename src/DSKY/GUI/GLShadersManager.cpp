///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 - 2022 Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "luminary/core/Prelude.hpp"
#include "luminary/platform/host/Platform.hpp"
#include "GLShadersManager.hpp"
#include "3DScene.hpp"
#include "GUI_App.hpp"
#if !PREFLIGHT_OPENGL_ES
#include "OpenGLManager.hpp"
#endif // !PREFLIGHT_OPENGL_ES

#include <cassert>
#include <algorithm>
#include <string_view>

#include <boost/log/trivial.hpp>
using namespace std::literals;

#if PREFLIGHT_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

namespace Luminary
{

std::pair<bool, std::string> GLShadersManager::init(bool compile_phong_shaders, bool compile_full_shaders)
{
    std::string error;

    auto append_shader = [this, &error](const std::string &name, const GLShaderProgram::ShaderFilenames &filenames,
                                        const std::initializer_list<std::string_view> &defines = {})
    {
        m_shaders.push_back(std::make_unique<GLShaderProgram>());
        if (!m_shaders.back()->init_from_files(name, filenames, defines))
        {
            error += name + "\n";
            // if any error happens while initializating the shader, we remove it from the list
            m_shaders.pop_back();
            return false;
        }
        return true;
    };

    assert(m_shaders.empty());

    bool valid = true;

#if PREFLIGHT_OPENGL_ES
    const std::string prefix = "ES/";
    // used to render wireframed triangles
    valid &= append_shader("wireframe", {prefix + "wireframe.vs", prefix + "wireframe.fs"});
#else
    const std::string prefix = DSKY::wxGetApp().is_gl_version_greater_or_equal_to(3, 1) ? "140/" : "110/";
#endif // PREFLIGHT_OPENGL_ES
    // imgui shader
    valid &= append_shader("imgui", {prefix + "imgui.vs", prefix + "imgui.fs"});
    // basic shader, used to render all what was previously rendered using the immediate mode
    valid &= append_shader("flat", {prefix + "flat.vs", prefix + "flat.fs"});
    // basic shader with plane clipping, used to render volumes in picking pass
    valid &= append_shader("flat_clip", {prefix + "flat_clip.vs", prefix + "flat_clip.fs"});
    // basic shader for textures, used to render textures
    valid &= append_shader("flat_texture", {prefix + "flat_texture.vs", prefix + "flat_texture.fs"});
    // used to render 3D scene background
    valid &= append_shader("background", {prefix + "background.vs", prefix + "background.fs"});
#if PREFLIGHT_OPENGL_ES
    // used to render dashed lines
    valid &= append_shader("dashed_lines", {prefix + "dashed_lines.vs", prefix + "dashed_lines.fs"});
#else
    if (DSKY::OpenGLManager::get_gl_info().is_core_profile())
        // used to render thick and/or dashed lines
        valid &= append_shader("dashed_thick_lines",
                               {prefix + "dashed_thick_lines.vs", prefix + "dashed_thick_lines.fs",
                                prefix + "dashed_thick_lines.gs"});
#endif // PREFLIGHT_OPENGL_ES
    // used to render toolpaths center of gravity
    valid &= append_shader("toolpaths_cog", {prefix + "toolpaths_cog.vs", prefix + "toolpaths_cog.fs"});
    // used to render tool marker
    valid &= append_shader("tool_marker", {prefix + "tool_marker.vs", prefix + "tool_marker.fs"});
    // used to render bed axes and model, selection hints, gcode sequential view marker model, preview shells, options in gcode preview
    valid &= append_shader("gouraud_light", {prefix + "gouraud_light.vs", prefix + "gouraud_light.fs"});
    // extend "gouraud_light" by adding clipping, used in sla gizmos
    valid &= append_shader("gouraud_light_clip", {prefix + "gouraud_light_clip.vs", prefix + "gouraud_light_clip.fs"});
    // used to render printbed
    valid &= append_shader("printbed", {prefix + "printbed.vs", prefix + "printbed.fs"});
    // used to render options in gcode preview
    if (DSKY::wxGetApp().is_gl_version_greater_or_equal_to(3, 3))
    {
        valid &= append_shader("gouraud_light_instanced",
                               {prefix + "gouraud_light_instanced.vs", prefix + "gouraud_light_instanced.fs"});
    }
    // used to render objects in 3d editor
    valid &= append_shader("gouraud", {prefix + "gouraud.vs", prefix + "gouraud.fs"}
#if ENABLE_ENVIRONMENT_MAP
                           ,
                           {"ENABLE_ENVIRONMENT_MAP"sv}
#endif // ENABLE_ENVIRONMENT_MAP
    );
    // Only compile phong shaders when Enhanced lighting is selected.
    // Shader compilation is GPU-blocking and happens during startup while the
    // splash screen's DWM compositor surface is active; compiling unnecessary
    // shaders can trigger NVIDIA TDR. Requires restart to change lighting quality.
    if (compile_phong_shaders)
    {
        try_compile_with_fallback("phong", {prefix + "phong.vs", prefix + "phong.fs"}, "gouraud"
#if ENABLE_ENVIRONMENT_MAP
                                  ,
                                  {"ENABLE_ENVIRONMENT_MAP"sv}
#endif // ENABLE_ENVIRONMENT_MAP
        );
        try_compile_with_fallback("phong_light", {prefix + "phong_light.vs", prefix + "phong_light.fs"},
                                  "gouraud_light");
    }
    else
    {
        m_fallback_map["phong"] = "gouraud";
        m_fallback_map["phong_light"] = "gouraud_light";
    }
    // Full lighting tier (shadow map + SSAO + PBR composite), GLSL 140 only. Same
    // deferred policy as phong: compiled at startup only when already selected,
    // otherwise registered as fallbacks and compiled on demand.
    register_full_tier_fallbacks();
#if !PREFLIGHT_OPENGL_ES
    // Supersampled-scene resolve (SSAA), tiny and tier-independent: always compiled.
    if (prefix == "140/")
        try_compile_with_fallback("ssaa_resolve", {prefix + "fullscreen.vs", prefix + "ssaa_resolve.fs"}, "flat");
    else
        m_fallback_map["ssaa_resolve"] = "flat";
#else
    m_fallback_map["ssaa_resolve"] = "flat";
#endif // !PREFLIGHT_OPENGL_ES
#if !PREFLIGHT_OPENGL_ES
    if (compile_full_shaders && prefix == "140/")
        compile_full_tier_shaders(prefix);
#else
    (void) compile_full_shaders;
#endif // !PREFLIGHT_OPENGL_ES
    // used to render variable layers heights in 3d editor
    valid &= append_shader("variable_layer_height",
                           {prefix + "variable_layer_height.vs", prefix + "variable_layer_height.fs"});
    // used to render highlight contour around selected triangles inside the multi-material gizmo
    valid &= append_shader("mm_contour", {prefix + "mm_contour.vs", prefix + "mm_contour.fs"});
    // Used to render painted triangles inside the multi-material gizmo. Triangle normals are computed inside fragment shader.
    // For Apple's on Arm CPU computed triangle normals inside fragment shader using dFdx and dFdy has the opposite direction.
    // Because of this, objects had darker colors inside the multi-material gizmo.
    // Based on https://stackoverflow.com/a/66206648, the similar behavior was also spotted on some other devices with Arm CPU.
    // Since macOS 12 (Monterey), this issue with the opposite direction on Apple's Arm CPU seems to be fixed, and computed
    // triangle normals inside fragment shader have the right direction.
    if (platform_flavor() == PlatformFlavor::OSXOnArm && wxPlatformInfo::Get().GetOSMajorVersion() < 12)
    {
        valid &= append_shader("mm_gouraud", {prefix + "mm_gouraud.vs", prefix + "mm_gouraud.fs"},
                               {"FLIP_TRIANGLE_NORMALS"sv});
        // Color preview shader is optional - falls back to mm_gouraud if unavailable
        append_shader("mm_color_preview", {prefix + "mm_color_preview.vs", prefix + "mm_color_preview.fs"},
                      {"FLIP_TRIANGLE_NORMALS"sv});
    }
    else
    {
        valid &= append_shader("mm_gouraud", {prefix + "mm_gouraud.vs", prefix + "mm_gouraud.fs"});
        // Color preview shader is optional - falls back to mm_gouraud if unavailable
        append_shader("mm_color_preview", {prefix + "mm_color_preview.vs", prefix + "mm_color_preview.fs"});
    }

    return {valid, error};
}

void GLShadersManager::shutdown()
{
    m_shaders.clear();
}

GLShaderProgram *GLShadersManager::get_shader(const std::string &shader_name)
{
    auto it = std::find_if(m_shaders.begin(), m_shaders.end(), [&shader_name](std::unique_ptr<GLShaderProgram> &p)
                           { return p->get_name() == shader_name; });
    if (it != m_shaders.end())
        return it->get();

    // Check fallback map: if this shader failed to compile, return its fallback
    auto fb = m_fallback_map.find(shader_name);
    if (fb != m_fallback_map.end())
        return get_shader(fb->second);

    return nullptr;
}

bool GLShadersManager::ensure_phong_shaders()
{
#if PREFLIGHT_OPENGL_ES
    const std::string prefix = "ES/";
#else
    const std::string prefix = DSKY::wxGetApp().is_gl_version_greater_or_equal_to(3, 1) ? "140/" : "110/";
#endif

    if (m_fallback_map.find("phong") != m_fallback_map.end())
    {
        // Remove fallback mappings so try_compile_with_fallback can re-register them on failure
        m_fallback_map.erase("phong");
        m_fallback_map.erase("phong_light");

        try_compile_with_fallback("phong", {prefix + "phong.vs", prefix + "phong.fs"}, "gouraud"
#if ENABLE_ENVIRONMENT_MAP
                                  ,
                                  {"ENABLE_ENVIRONMENT_MAP"sv}
#endif
        );
        try_compile_with_fallback("phong_light", {prefix + "phong_light.vs", prefix + "phong_light.fs"},
                                  "gouraud_light");
    }

#if !PREFLIGHT_OPENGL_ES
    // The Full tier compiles on demand as well (Preferences change to "full").
    if (prefix == "140/" && m_fallback_map.find("phong_full") != m_fallback_map.end())
        compile_full_tier_shaders(prefix);
#endif // !PREFLIGHT_OPENGL_ES

    // Return true if phong is now a real shader, not a fallback
    return m_fallback_map.find("phong") == m_fallback_map.end() && get_shader("phong") != nullptr;
}

void GLShadersManager::register_full_tier_fallbacks()
{
    m_fallback_map["phong_full"] = "phong";
    m_fallback_map["shadowmap"] = "flat";
    m_fallback_map["gbuffer"] = "flat";
    m_fallback_map["gbuffer_bed"] = "flat";
    m_fallback_map["ssao"] = "flat";
    m_fallback_map["ssao_blur"] = "flat";
    m_fallback_map["bed_overlay"] = "flat";
}

bool GLShadersManager::compile_full_tier_shaders(const std::string &prefix)
{
    using namespace std::literals;
    m_fallback_map.erase("phong_full");
    m_fallback_map.erase("shadowmap");
    m_fallback_map.erase("gbuffer");
    m_fallback_map.erase("gbuffer_bed");
    m_fallback_map.erase("ssao");
    m_fallback_map.erase("ssao_blur");
    m_fallback_map.erase("bed_overlay");

    bool ok = true;
    ok &= try_compile_with_fallback("phong_full", {prefix + "phong_full.vs", prefix + "phong_full.fs"}, "phong"
#if ENABLE_ENVIRONMENT_MAP
                                    ,
                                    {"ENABLE_ENVIRONMENT_MAP"sv}
#endif
    );
    ok &= try_compile_with_fallback("shadowmap", {prefix + "shadowmap.vs", prefix + "shadowmap.fs"}, "flat");
    ok &= try_compile_with_fallback("gbuffer", {prefix + "gbuffer.vs", prefix + "gbuffer.fs"}, "flat");
    ok &= try_compile_with_fallback("gbuffer_bed", {prefix + "gbuffer_bed.vs", prefix + "gbuffer_bed.fs"}, "flat");
    ok &= try_compile_with_fallback("ssao", {prefix + "fullscreen.vs", prefix + "ssao.fs"}, "flat");
    ok &= try_compile_with_fallback("ssao_blur", {prefix + "fullscreen.vs", prefix + "ssao_blur.fs"}, "flat");
    ok &= try_compile_with_fallback("bed_overlay", {prefix + "bed_overlay.vs", prefix + "bed_overlay.fs"}, "flat");
    return ok;
}

bool GLShadersManager::try_compile_with_fallback(const std::string &name,
                                                 const GLShaderProgram::ShaderFilenames &filenames,
                                                 const std::string &fallback_name,
                                                 const std::initializer_list<std::string_view> &defines)
{
    m_shaders.push_back(std::make_unique<GLShaderProgram>());
    if (m_shaders.back()->init_from_files(name, filenames, defines))
        return true;

    // Compilation failed - remove the broken shader and map to fallback
    m_shaders.pop_back();
    m_fallback_map[name] = fallback_name;
    BOOST_LOG_TRIVIAL(warning) << "Shader '" << name << "' failed to compile, falling back to '" << fallback_name
                               << "'";
    return false;
}

GLShaderProgram *GLShadersManager::get_current_shader()
{
    GLint id = 0;
    glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &id));
    if (id == 0)
        return nullptr;

    auto it = std::find_if(m_shaders.begin(), m_shaders.end(),
                           [id](std::unique_ptr<GLShaderProgram> &p) { return static_cast<GLint>(p->get_id()) == id; });
    return (it != m_shaders.end()) ? it->get() : nullptr;
}

} // namespace Luminary
