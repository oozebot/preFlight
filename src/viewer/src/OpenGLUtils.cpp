///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2023 Enrico Turri @enricoturri1966, Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "OpenGLUtils.hpp"

#include <iostream>
#include <assert.h>
#include <cctype>
#include <stdio.h>

namespace libvgcode
{

#ifdef HAS_GLSAFE
void glAssertRecentCallImpl(const char *file_name, unsigned int line, const char *function_name)
{
    const GLenum err = glGetError();
    if (err == GL_NO_ERROR)
        return;
    const char *sErr = 0;
    switch (err)
    {
    case GL_INVALID_ENUM:
    {
        sErr = "Invalid Enum";
        break;
    }
    case GL_INVALID_VALUE:
    {
        sErr = "Invalid Value";
        break;
    }
    // be aware that GL_INVALID_OPERATION is generated if glGetError is executed between the execution of glBegin / glEnd
    case GL_INVALID_OPERATION:
    {
        sErr = "Invalid Operation";
        break;
    }
    case GL_OUT_OF_MEMORY:
    {
        sErr = "Out Of Memory";
        break;
    }
    case GL_INVALID_FRAMEBUFFER_OPERATION:
    {
        sErr = "Invalid framebuffer operation";
        break;
    }
    case GL_STACK_OVERFLOW:
    {
        sErr = "Stack Overflow";
        break;
    }
    case GL_STACK_UNDERFLOW:
    {
        sErr = "Stack Underflow";
        break;
    }
    default:
    {
        sErr = "Unknown";
        break;
    }
    }
    std::cout << "OpenGL error in " << file_name << ":" << line << ", function " << function_name
              << "() : " << (int) err << " - " << sErr << "\n";
    assert(false);
}
#endif // HAS_GLSAFE

bool OpenGLWrapper::s_valid_context = false;

bool OpenGLWrapper::load_opengl(const std::string &context_version)
{
    s_valid_context = false;

    const char *version = context_version.c_str();

    GLint major = 0;
    GLint minor = 0;
#ifdef _MSC_VER
    const int res = sscanf_s(version, "%d.%d", &major, &minor);
#else
    const int res = sscanf(version, "%d.%d", &major, &minor);
#endif // _MSC_VER
    if (res != 2)
        return false;

#if defined(__linux__) && defined(__aarch64__)
    // RPi 5 V3D GPU reports OpenGL 3.1 but supports the 3.2 extensions preFlight uses
    s_valid_context = major > 3 || (major == 3 && minor >= 1);
#else
    s_valid_context = major > 3 || (major == 3 && minor >= 2);
#endif
    const int glad_res = gladLoaderLoadGL();

    if (glad_res == 0)
        return false;

    return s_valid_context;
}

void OpenGLWrapper::unload_opengl()
{
    gladLoaderUnloadGL();
}

} // namespace libvgcode
