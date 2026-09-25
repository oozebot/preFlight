///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2018 - 2023 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include "luminary/core/Prelude.hpp"
#include "OpenGLManager.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "luminary/presets/app_config/AppConfig.hpp"
#if !PREFLIGHT_OPENGL_ES
#include "GUI_Init.hpp"
#endif // !PREFLIGHT_OPENGL_ES
#include "I18N.hpp"
#include "3DScene.hpp"
#include "format.hpp"

#include "luminary/platform/host/Platform.hpp"

#if PREFLIGHT_OPENGL_ES
#include <glad/gles2.h>
#else
#include <glad/gl.h>
#endif

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/log/trivial.hpp>

#include <wx/glcanvas.h>
#include <wx/msgdlg.h>

#ifdef __linux__
#include <GL/glx.h>
#endif

#ifdef __APPLE__
// Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
#include <wx/platinfo.h>

#include "../Utils/MacDarkMode.hpp"
#endif // __APPLE__

namespace DSKY
{
using namespace Luminary;

// A safe wrapper around glGetString to report a "N/A" string in case glGetString returns nullptr.
std::string gl_get_string_safe(GLenum param, const std::string &default_value)
{
    const char *value = (const char *) ::glGetString(param);
    glcheck();
    return std::string((value != nullptr) ? value : default_value);
}

const std::string &OpenGLManager::GLInfo::get_version_string() const
{
    if (!m_detected)
        detect();

    return m_version_string;
}

const std::string &OpenGLManager::GLInfo::get_glsl_version_string() const
{
    if (!m_detected)
        detect();

    return m_glsl_version_string;
}

const std::string &OpenGLManager::GLInfo::get_vendor() const
{
    if (!m_detected)
        detect();

    return m_vendor;
}

const std::string &OpenGLManager::GLInfo::get_renderer() const
{
    if (!m_detected)
        detect();

    return m_renderer;
}

bool OpenGLManager::GLInfo::is_mesa() const
{
    return m_version_is_mesa;
}

bool OpenGLManager::GLInfo::should_use_phong(const std::string &lighting_quality) const
{
    if (lighting_quality == "basic")
        return false;
    if (lighting_quality == "enhanced" || lighting_quality == "full")
        return true;
    // "auto": only disable for software renderers. Any hardware GPU that passes
    // the OpenGL 3.2 minimum version check can handle per-pixel lighting.
    if (!m_detected)
        detect();
    const std::string &r = m_renderer;
    if (r.find("llvmpipe") != std::string::npos || r.find("SwiftShader") != std::string::npos)
        return false;
    return true;
}

int OpenGLManager::GLInfo::get_max_tex_size() const
{
    if (!m_detected)
        detect();

    // clamp to avoid the texture generation become too slow and use too much GPU memory
#ifdef __APPLE__
    // and use smaller texture for non retina systems
    return (DSKY::mac_max_scaling_factor() > 1.0) ? std::min(m_max_tex_size, 8192) : std::min(m_max_tex_size / 2, 4096);
#else
    // and use smaller texture for older OpenGL versions
    return is_version_greater_or_equal_to(3, 0) ? std::min(m_max_tex_size, 8192) : std::min(m_max_tex_size / 2, 4096);
#endif // __APPLE__
}

float OpenGLManager::GLInfo::get_max_anisotropy() const
{
    if (!m_detected)
        detect();

    return m_max_anisotropy;
}

static Semver parse_version_string(const std::string &version);

void OpenGLManager::GLInfo::detect() const
{
    *const_cast<std::string *>(&m_version_string) = gl_get_string_safe(GL_VERSION, "N/A");
    *const_cast<std::string *>(&m_glsl_version_string) = gl_get_string_safe(GL_SHADING_LANGUAGE_VERSION, "N/A");
    *const_cast<std::string *>(&m_vendor) = gl_get_string_safe(GL_VENDOR, "N/A");
    *const_cast<std::string *>(&m_renderer) = gl_get_string_safe(GL_RENDERER, "N/A");

    *const_cast<Semver *>(&m_version) = parse_version_string(m_version_string);
    *const_cast<bool *>(&m_version_is_mesa) = boost::icontains(m_version_string, "mesa");
    *const_cast<Semver *>(&m_glsl_version) = parse_version_string(m_glsl_version_string);

    int *max_tex_size = const_cast<int *>(&m_max_tex_size);
    glsafe(::glGetIntegerv(GL_MAX_TEXTURE_SIZE, max_tex_size));

    *max_tex_size /= 2;

    if (Luminary::total_physical_memory() / (1024 * 1024 * 1024) < 6)
        *max_tex_size /= 2;

#if PREFLIGHT_OPENGL_ES
    // OpenGL ES doesn't have anisotropic filtering extension by default
    // Leave m_max_anisotropy at default value
#else
    if (GLAD_GL_EXT_texture_filter_anisotropic)
    {
        float *max_anisotropy = const_cast<float *>(&m_max_anisotropy);
        glsafe(::glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy));
    }
#endif

#if PREFLIGHT_OPENGL_ES
    // OpenGL ES is always "core-like" (no compatibility profile concept)
    *const_cast<bool *>(&m_core_profile) = true;
#else
    // For OpenGL 3.2+, check the context profile mask
    // GLAD core profile loader means we're targeting core, but verify via GL query
    GLint profileMask = 0;
    glsafe(::glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask));
    if (profileMask & GL_CONTEXT_CORE_PROFILE_BIT)
        *const_cast<bool *>(&m_core_profile) = true;
#endif

    int *samples = const_cast<int *>(&m_samples);
    glsafe(::glGetIntegerv(GL_SAMPLES, samples));

    *const_cast<bool *>(&m_detected) = true;
}

static Semver parse_version_string(const std::string &version)
{
    if (version == "N/A")
        return Semver::invalid();

    std::vector<std::string> tokens;
    boost::split(tokens, version, boost::is_any_of(" "), boost::token_compress_on);

    if (tokens.empty())
        return Semver::invalid();

#if PREFLIGHT_OPENGL_ES
    const std::string version_container = (tokens.size() > 1 && boost::istarts_with(tokens[1], "ES")) ? tokens[2]
                                                                                                      : tokens[0];
#endif // PREFLIGHT_OPENGL_ES

    std::vector<std::string> numbers;
#if PREFLIGHT_OPENGL_ES
    boost::split(numbers, version_container, boost::is_any_of("."), boost::token_compress_on);
#else
    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);
#endif // PREFLIGHT_OPENGL_ES

    unsigned int gl_major = 0;
    unsigned int gl_minor = 0;

    if (numbers.size() > 0)
        gl_major = ::atoi(numbers[0].c_str());

    if (numbers.size() > 1)
        gl_minor = ::atoi(numbers[1].c_str());

    return Semver(gl_major, gl_minor, 0);
}

bool OpenGLManager::GLInfo::is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (!m_detected)
        detect();

    return m_version >= Semver(major, minor, 0);
}

bool OpenGLManager::GLInfo::is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (!m_detected)
        detect();

    return m_glsl_version >= Semver(major, minor, 0);
}

// If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
// Otherwise HTML formatted for the system info dialog.
std::string OpenGLManager::GLInfo::to_string(bool for_github) const
{
    if (!m_detected)
        detect();

    std::stringstream out;

    const bool format_as_html = !for_github;
    std::string h2_start = format_as_html ? "<b>" : "";
    std::string h2_end = format_as_html ? "</b>" : "";
    std::string b_start = format_as_html ? "<b>" : "";
    std::string b_end = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    out << h2_start << "OpenGL installation" << h2_end << line_end;
    out << b_start << "GL version:   " << b_end << m_version << " (" << m_version_string << ")" << line_end;
#if !PREFLIGHT_OPENGL_ES
    out << b_start << "Profile:      " << b_end << (is_core_profile() ? "Core" : "Compatibility") << line_end;
#endif // !PREFLIGHT_OPENGL_ES
    out << b_start << "Vendor:       " << b_end << m_vendor << line_end;
    out << b_start << "Renderer:     " << b_end << m_renderer << line_end;
    out << b_start << "GLSL version: " << b_end << m_glsl_version << line_end;
    out << b_start << "Textures compression:       " << b_end
        << (are_compressed_textures_supported() ? "Enabled" : "Disabled") << line_end;
    out << b_start << "Multisampling: " << b_end
        << (can_multisample() ? "Enabled (" + std::to_string(m_samples) + " samples)" : "Disabled") << line_end;

    {
#if PREFLIGHT_OPENGL_ES
        const std::string extensions_str = gl_get_string_safe(GL_EXTENSIONS, "");
        std::vector<std::string> extensions_list;
        boost::split(extensions_list, extensions_str, boost::is_any_of(" "), boost::token_compress_on);
#else
        std::vector<std::string> extensions_list = get_extensions_list();
#endif // PREFLIGHT_OPENGL_ES

        if (!extensions_list.empty())
        {
            if (for_github)
                out << "<details>\n<summary>Installed extensions:</summary>\n";
            else
                out << h2_start << "Installed extensions:" << h2_end << line_end;

            std::sort(extensions_list.begin(), extensions_list.end());
            for (const std::string &ext : extensions_list)
                if (!ext.empty())
                    out << ext << line_end;

            if (for_github)
                out << "</details>\n";
        }
    }

    return out.str();
}

#if !PREFLIGHT_OPENGL_ES
std::vector<std::string> OpenGLManager::GLInfo::get_extensions_list() const
{
    std::vector<std::string> ret;

    if (is_core_profile())
    {
        GLint n = 0;
        glsafe(::glGetIntegerv(GL_NUM_EXTENSIONS, &n));
        ret.reserve(n);
        for (GLint i = 0; i < n; ++i)
        {
            const char *extension = (const char *) ::glGetStringi(GL_EXTENSIONS, i);
            glcheck();
            if (extension != nullptr)
                ret.emplace_back(extension);
        }
    }
    else
    {
        const std::string extensions_str = gl_get_string_safe(GL_EXTENSIONS, "");
        boost::split(ret, extensions_str, boost::is_any_of(" "), boost::token_compress_on);
    }

    return ret;
}
#endif // !PREFLIGHT_OPENGL_ES

OpenGLManager::GLInfo OpenGLManager::s_gl_info;
bool OpenGLManager::s_compressed_textures_supported = false;
bool OpenGLManager::s_force_power_of_two_textures = false;
OpenGLManager::EMultisampleState OpenGLManager::s_multisample = OpenGLManager::EMultisampleState::Unknown;
OpenGLManager::EFramebufferType OpenGLManager::s_framebuffers_type = OpenGLManager::EFramebufferType::Unknown;

#ifdef __APPLE__
// Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
OpenGLManager::OSInfo OpenGLManager::s_os_info;
#endif // __APPLE__

void OpenGLManager::compile_pending_shaders()
{
    if (!m_phong_shaders_requested)
        return;
    m_phong_shaders_requested = false;
    m_shaders_manager.ensure_phong_shaders();
}

OpenGLManager::~OpenGLManager()
{
    m_shaders_manager.shutdown();

#ifdef __APPLE__
    // On OSX 10.9.5 the wxGLContext destructor crashes when the application closes, so the context is left undeleted.
    if (s_os_info.major != 10 || s_os_info.minor != 9 || s_os_info.micro != 5)
    {
#endif //__APPLE__
        if (m_context != nullptr)
            delete m_context;
#ifdef __APPLE__
    }
#endif //__APPLE__
}

#if !PREFLIGHT_OPENGL_ES
#ifdef _WIN32
static void APIENTRY CustomGLDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity, GLsizei length,
                                         const char *message, const void *userParam)
#else
static void CustomGLDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity, GLsizei length,
                                const char *message, const void *userParam)
#endif // _WIN32
{
    if (severity != GL_DEBUG_SEVERITY_HIGH)
        return;

    std::string out = "OpenGL DEBUG message [";
    switch (type)
    {
    case GL_DEBUG_TYPE_ERROR:
        out += "Error";
        break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        out += "Deprecated Behaviour";
        break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        out += "Undefined Behaviour";
        break;
    case GL_DEBUG_TYPE_PORTABILITY:
        out += "Portability";
        break;
    case GL_DEBUG_TYPE_PERFORMANCE:
        out += "Performance";
        break;
    case GL_DEBUG_TYPE_MARKER:
        out += "Marker";
        break;
    case GL_DEBUG_TYPE_PUSH_GROUP:
        out += "Push Group";
        break;
    case GL_DEBUG_TYPE_POP_GROUP:
        out += "Pop Group";
        break;
    case GL_DEBUG_TYPE_OTHER:
        out += "Other";
        break;
    }
    out += "/";
    switch (source)
    {
    case GL_DEBUG_SOURCE_API:
        out += "API";
        break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
        out += "Window System";
        break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER:
        out += "Shader Compiler";
        break;
    case GL_DEBUG_SOURCE_THIRD_PARTY:
        out += "Third Party";
        break;
    case GL_DEBUG_SOURCE_APPLICATION:
        out += "Application";
        break;
    case GL_DEBUG_SOURCE_OTHER:
        out += "Other";
        break;
    }
    out += "/";
    switch (severity)
    {
    case GL_DEBUG_SEVERITY_HIGH:
        out += "high";
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        out += "medium";
        break;
    case GL_DEBUG_SEVERITY_LOW:
        out += "low";
        break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
        out += "notification";
        break;
    }
    out += "]:\n";
    std::cout << out << "(" << id << "): " << message << "\n\n";
}
#endif // !PREFLIGHT_OPENGL_ES

bool OpenGLManager::init_gl()
{
    if (!m_gl_initialized)
    {
#if PREFLIGHT_OPENGL_ES
        int version = gladLoaderLoadGLES2();
#else
        int version = gladLoaderLoadGL();
#endif
        if (version == 0)
        {
            BOOST_LOG_TRIVIAL(error) << "Unable to init GLAD OpenGL loader";
            return false;
        }
        BOOST_LOG_TRIVIAL(info) << "GLAD loaded OpenGL " << GLAD_VERSION_MAJOR(version) << "."
                                << GLAD_VERSION_MINOR(version);

        // Clear any errors that may have been generated during initialization
        GLenum err;
        do
        {
            err = ::glGetError();
        } while (err != GL_NO_ERROR);

        m_gl_initialized = true;

#if PREFLIGHT_OPENGL_ES
        // OpenGL ES 3.0 doesn't have S3TC compression by default
        s_compressed_textures_supported = false;
#else
        if (GLAD_GL_EXT_texture_compression_s3tc)
            s_compressed_textures_supported = true;
        else
            s_compressed_textures_supported = false;
#endif

        // ARB_framebuffer_object is core in OpenGL 3.0+, so always available for our minimum 3.2 requirement
        s_framebuffers_type = EFramebufferType::Arb;

#if PREFLIGHT_OPENGL_ES
        bool valid_version = s_gl_info.is_version_greater_or_equal_to(3, 0);
#elif defined(__linux__) && defined(__aarch64__)
        // RPi 5 V3D GPU reports OpenGL 3.1 but supports the 3.2 extensions preFlight uses
        const bool valid_version = s_gl_info.is_version_greater_or_equal_to(3, 1);
#else
        const bool valid_version = s_gl_info.is_version_greater_or_equal_to(3, 2);
#endif // PREFLIGHT_OPENGL_ES

        if (!valid_version)
        {
            // Complain about the OpenGL version.
            wxString message = format_wxstr(
#if PREFLIGHT_OPENGL_ES
                _L("preFlight requires OpenGL ES 3.0 capable graphics driver to run correctly, \n"
                   "while OpenGL version %s, renderer %s, vendor %s was detected."),
                s_gl_info.get_version_string(), s_gl_info.get_renderer(), s_gl_info.get_vendor());
#elif defined(__linux__) && defined(__aarch64__)
                _L("preFlight requires OpenGL 3.1 capable graphics driver to run correctly,\n"
                   "while OpenGL version %s, renderer %s, vendor %s was detected."),
                s_gl_info.get_version_string(), s_gl_info.get_renderer(), s_gl_info.get_vendor());
#else
                _L("preFlight requires OpenGL 3.2 capable graphics driver to run correctly,\n"
                   "while OpenGL version %s, renderer %s, vendor %s was detected."),
                s_gl_info.get_version_string(), s_gl_info.get_renderer(), s_gl_info.get_vendor());
#endif // PREFLIGHT_OPENGL_ES
            message += "\n";
            message += _L("You may need to update your graphics card driver.");
#ifdef _WIN32
            message += "\n";
            message += _L(
                "As a workaround, you may run preFlight with a software rendered 3D graphics by running preFlight.exe with the --sw-renderer parameter.");
#endif
            wxMessageBox(message, wxString("preFlight - ") + _L("Unsupported OpenGL version"), wxOK | wxICON_ERROR);
        }

        if (valid_version)
        {
            // Determine whether to compile phong shaders based on lighting quality setting.
            // Shader compilation is GPU-blocking; skip it for Basic lighting to reduce
            // GPU pressure during startup (prevents TDR on some configurations).
            bool compile_phong = true;
            bool compile_full = false;
            {
                const AppConfig *config = DSKY::wxGetApp().app_config;
                if (config)
                {
                    const std::string quality = config->get("canvas_lighting_quality");
                    compile_phong = s_gl_info.should_use_phong(quality);
                    compile_full = quality == "full";
                }
            }
            auto [result, error] = m_shaders_manager.init(compile_phong, compile_full);
            if (!result)
            {
                wxString message = format_wxstr(_L("Unable to load the following shaders:\n%s"), error);
                wxMessageBox(message, wxString("preFlight - ") + _L("Error loading shaders"), wxOK | wxICON_ERROR);
            }
#if !PREFLIGHT_OPENGL_ES
            if (m_debug_enabled && s_gl_info.is_version_greater_or_equal_to(4, 3) && GLAD_GL_KHR_debug)
            {
                ::glEnable(GL_DEBUG_OUTPUT);
                ::glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                ::glDebugMessageCallback(CustomGLDebugOutput, nullptr);
                ::glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
                std::cout << "Enabled OpenGL debug output\n";
            }
#endif // !PREFLIGHT_OPENGL_ES
        }

#ifdef _WIN32
        // Since AMD driver version 22.7.1, there is probably some bug in the driver that causes the issue with the missing
        // texture of the bed (fix for MSAA texture binding).
        // It seems that this issue only triggers when mipmaps are generated manually
        // (combined with a texture compression) with texture size not being power of two.
        // When mipmaps are generated through OpenGL function glGenerateMipmap() the driver works fine,
        // but the mipmap generation is quite slow on some machines.
        // There is no an easy way to detect the driver version without using Win32 API because the strings returned by OpenGL
        // have no standardized format, only some of them contain the driver version.
        // Until we do not know that driver will be fixed (if ever) we force the use of power of two textures on all cards
        // 1) containing the string 'Radeon' in the string returned by glGetString(GL_RENDERER)
        // 2) containing the string 'Custom' in the string returned by glGetString(GL_RENDERER)
        const auto &gl_info = OpenGLManager::get_gl_info();
        if (boost::contains(gl_info.get_vendor(), "ATI Technologies Inc.") &&
            (boost::contains(gl_info.get_renderer(), "Radeon") || boost::contains(gl_info.get_renderer(), "Custom")))
            s_force_power_of_two_textures = true;
#endif // _WIN32

        // Enable VSync to cap framerate and reduce idle GPU usage.
        // Without VSync, SwapBuffers() returns immediately and the render loop
        // can run at hundreds of FPS when the canvas is dirty, burning GPU.
#ifdef _WIN32
        {
            typedef BOOL(WINAPI * PFNWGLSWAPINTERVALEXTPROC)(int interval);
            auto wglSwapIntervalEXT = reinterpret_cast<PFNWGLSWAPINTERVALEXTPROC>(
                wglGetProcAddress("wglSwapIntervalEXT"));
            if (wglSwapIntervalEXT)
            {
                wglSwapIntervalEXT(1);
                BOOST_LOG_TRIVIAL(info) << "VSync enabled via wglSwapIntervalEXT";
            }
            else
            {
                BOOST_LOG_TRIVIAL(warning) << "VSync not available (wglSwapIntervalEXT not found)";
            }
        }
#elif defined(__linux__)
        {
            bool vsync_set = false;
            // Try Mesa extension first (Mesa, AMD open-source)
            typedef int (*PFNGLXSWAPINTERVALMESAPROC)(int interval);
            auto glXSwapIntervalMESA = reinterpret_cast<PFNGLXSWAPINTERVALMESAPROC>(
                glXGetProcAddressARB(reinterpret_cast<const GLubyte *>("glXSwapIntervalMESA")));
            if (glXSwapIntervalMESA)
            {
                glXSwapIntervalMESA(1);
                BOOST_LOG_TRIVIAL(info) << "VSync enabled via glXSwapIntervalMESA";
                vsync_set = true;
            }
            // Fallback to SGI extension (NVIDIA proprietary)
            if (!vsync_set)
            {
                typedef int (*PFNGLXSWAPINTERVALSGIPROC)(int interval);
                auto glXSwapIntervalSGI = reinterpret_cast<PFNGLXSWAPINTERVALSGIPROC>(
                    glXGetProcAddressARB(reinterpret_cast<const GLubyte *>("glXSwapIntervalSGI")));
                if (glXSwapIntervalSGI)
                {
                    glXSwapIntervalSGI(1);
                    BOOST_LOG_TRIVIAL(info) << "VSync enabled via glXSwapIntervalSGI";
                    vsync_set = true;
                }
            }
            if (!vsync_set)
                BOOST_LOG_TRIVIAL(warning) << "VSync not available on Linux";
        }
#endif
    }

    return true;
}

#if PREFLIGHT_OPENGL_ES
wxGLContext *OpenGLManager::init_glcontext(wxGLCanvas &canvas)
#else
wxGLContext *OpenGLManager::init_glcontext(wxGLCanvas &canvas, const std::pair<int, int> &required_opengl_version,
                                           bool enable_compatibility_profile, bool enable_debug)
#endif // PREFLIGHT_OPENGL_ES
{
    if (m_context == nullptr)
    {
#if PREFLIGHT_OPENGL_ES
        wxGLContextAttrs attrs;
        attrs.PlatformDefaults().ES2().MajorVersion(2).EndList();
        m_context = new wxGLContext(&canvas, nullptr, &attrs);
#else
        m_debug_enabled = enable_debug;

        const int gl_major = required_opengl_version.first;
        const int gl_minor = required_opengl_version.second;
        const bool supports_core_profile = std::find(OpenGLVersions::core.begin(), OpenGLVersions::core.end(),
                                                     std::make_pair(gl_major, gl_minor)) != OpenGLVersions::core.end();

        if (gl_major == 0 && !enable_compatibility_profile)
        {
            // search for highest supported core profile version
            // disable wxWidgets logging to avoid showing the log dialog in case the following code fails generating a valid gl context
            wxLogNull logNo;
            for (auto v = OpenGLVersions::core.rbegin(); v != OpenGLVersions::core.rend(); ++v)
            {
                wxGLContextAttrs attrs;
                attrs.PlatformDefaults().MajorVersion(v->first).MinorVersion(v->second).CoreProfile().ForwardCompatible();
                if (m_debug_enabled)
                    attrs.DebugCtx();
                attrs.EndList();
                m_context = new wxGLContext(&canvas, nullptr, &attrs);
                if (m_context->IsOK())
                    break;
                else
                {
                    delete m_context;
                    m_context = nullptr;
                }
            }
        }

        if (m_context == nullptr)
        {
            // search for requested compatibility profile version
            if (enable_compatibility_profile)
            {
                // disable wxWidgets logging to avoid showing the log dialog in case the following code fails generating a valid gl context
                wxLogNull logNo;
                wxGLContextAttrs attrs;
                attrs.PlatformDefaults().CompatibilityProfile();
                if (m_debug_enabled)
                    attrs.DebugCtx();
                attrs.EndList();
                m_context = new wxGLContext(&canvas, nullptr, &attrs);
                if (!m_context->IsOK())
                {
                    delete m_context;
                    m_context = nullptr;
                }
            }
            // search for requested core profile version
            else if (supports_core_profile)
            {
                // disable wxWidgets logging to avoid showing the log dialog in case the following code fails generating a valid gl context
                wxLogNull logNo;
                wxGLContextAttrs attrs;
                attrs.PlatformDefaults().MajorVersion(gl_major).MinorVersion(gl_minor).CoreProfile().ForwardCompatible();
                if (m_debug_enabled)
                    attrs.DebugCtx();
                attrs.EndList();
                m_context = new wxGLContext(&canvas, nullptr, &attrs);
                if (!m_context->IsOK())
                {
                    delete m_context;
                    m_context = nullptr;
                }
            }
        }

        if (m_context == nullptr)
        {
            wxGLContextAttrs attrs;
            attrs.PlatformDefaults();
            if (m_debug_enabled)
                attrs.DebugCtx();
            attrs.EndList();
            // if no valid context was created use the default one
            m_context = new wxGLContext(&canvas, nullptr, &attrs);
        }
#endif // PREFLIGHT_OPENGL_ES

#ifdef __APPLE__
        // Part of hack to remove crash when closing the application on OSX 10.9.5 when building against newer wxWidgets
        s_os_info.major = wxPlatformInfo::Get().GetOSMajorVersion();
        s_os_info.minor = wxPlatformInfo::Get().GetOSMinorVersion();
        s_os_info.micro = wxPlatformInfo::Get().GetOSMicroVersion();
#endif //__APPLE__
    }
    return m_context;
}

wxGLCanvas *OpenGLManager::create_wxglcanvas(wxWindow &parent, int msaa_samples)
{
    wxGLAttributes attribList;
    // Only probe MSAA once - subsequent calls reuse the cached result.
    // Each IsDisplaySupported call creates/destroys a temporary WGL context,
    // and redundant probing during startup can saturate the GPU command queue.
    static bool s_msaa_probed = false;
    static wxGLAttributes s_msaa_attribs;
    if (!s_msaa_probed)
    {
        s_multisample = EMultisampleState::Disabled;
        // Disable multi-sampling on ChromeOS, as the OpenGL virtualization swaps Red/Blue channels with multi-sampling
        // enabled, at least on some platforms.
        if (msaa_samples != 0 && platform_flavor() != PlatformFlavor::LinuxOnChromium)
        {
            if (msaa_samples < 0)
            {
                // Auto: try highest available, fall back gracefully
                for (int i = 16; i >= 2; i /= 2)
                {
                    attribList.Reset();
                    attribList.PlatformDefaults()
                        .RGBA()
                        .DoubleBuffer()
                        .MinRGBA(8, 8, 8, 8)
                        .Depth(24)
                        .Stencil(8)
                        .SampleBuffers(1)
                        .Samplers(i);
#ifdef __APPLE__
                    attribList.SetNeedsARB(true);
#endif // __APPLE__
                    attribList.EndList();
                    if (wxGLCanvas::IsDisplaySupported(attribList))
                    {
                        s_multisample = EMultisampleState::Enabled;
                        break;
                    }
                }
            }
            else
            {
                // Explicit sample count requested
                attribList.Reset();
                attribList.PlatformDefaults()
                    .RGBA()
                    .DoubleBuffer()
                    .MinRGBA(8, 8, 8, 8)
                    .Depth(24)
                    .Stencil(8)
                    .SampleBuffers(1)
                    .Samplers(msaa_samples);
#ifdef __APPLE__
                attribList.SetNeedsARB(true);
#endif // __APPLE__
                attribList.EndList();
                if (wxGLCanvas::IsDisplaySupported(attribList))
                    s_multisample = EMultisampleState::Enabled;
            }
        }
        s_msaa_probed = true;
        s_msaa_attribs = attribList;
    }
    else if (s_multisample == EMultisampleState::Enabled)
    {
        attribList = s_msaa_attribs;
    }

    if (s_multisample != EMultisampleState::Enabled)
    {
        attribList.Reset();
        attribList.PlatformDefaults().RGBA().DoubleBuffer().MinRGBA(8, 8, 8, 8).Depth(24).Stencil(8);
#ifdef __APPLE__
        attribList.SetNeedsARB(true);
#endif // __APPLE__
        attribList.EndList();
    }

    return new wxGLCanvas(&parent, attribList, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
}

} // namespace DSKY
