///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2020 Enrico Turri @enricoturri1966
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#pragma once

#include "GLShader.hpp"

#include <map>
#include <vector>
#include <string>
#include <memory>

namespace Luminary
{

class GLShadersManager
{
    std::vector<std::unique_ptr<GLShaderProgram>> m_shaders;
    // Maps shader names to fallback names when compilation fails
    std::map<std::string, std::string> m_fallback_map;

public:
    std::pair<bool, std::string> init(bool compile_phong_shaders = true, bool compile_full_shaders = false);
    // call this method before to release the OpenGL context
    void shutdown();

    // returns nullptr if not found
    GLShaderProgram *get_shader(const std::string &shader_name);

    // returns currently active shader, nullptr if none
    GLShaderProgram *get_current_shader();

    // Compile phong shaders on demand if they were skipped during init.
    // Returns true if phong shaders are now available.
    bool ensure_phong_shaders();

private:
    // Try to compile a shader; on failure, map its name to fallback_name so
    // get_shader(name) transparently returns the fallback program.
    bool try_compile_with_fallback(const std::string &name, const GLShaderProgram::ShaderFilenames &filenames,
                                   const std::string &fallback_name,
                                   const std::initializer_list<std::string_view> &defines = {});

    // Full lighting tier (shadow map + SSAO + PBR composite), GLSL 140 only.
    void register_full_tier_fallbacks();
    bool compile_full_tier_shaders(const std::string &prefix);
};

} // namespace Luminary

