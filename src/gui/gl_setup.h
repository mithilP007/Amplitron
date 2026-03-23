#pragma once

// =============================================================================
// Platform-specific OpenGL / GLSL configuration
//
// Centralises the GL context attributes and shader version string so that
// gui_manager.cpp (and any future rendering code) stays free of #ifdefs.
// =============================================================================

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL_opengl.h>
#endif

namespace GuitarAmp {
namespace GLSetup {

#ifdef __EMSCRIPTEN__
    inline constexpr int GL_CONTEXT_PROFILE = SDL_GL_CONTEXT_PROFILE_ES;
    inline constexpr int GL_MAJOR           = 3;
    inline constexpr int GL_MINOR           = 0;
    inline constexpr const char* GLSL_VERSION = "#version 300 es";
#else
    inline constexpr int GL_CONTEXT_PROFILE = SDL_GL_CONTEXT_PROFILE_CORE;
    inline constexpr int GL_MAJOR           = 3;
    inline constexpr int GL_MINOR           = 3;
    inline constexpr const char* GLSL_VERSION = "#version 330";
#endif

} // namespace GLSetup
} // namespace GuitarAmp
