#pragma once
#include <string>

// Thin platform helpers — keep OS/GL differences out of app hot paths.

// GLFW context version hints (4.1+forward on Apple, 4.5 elsewhere).
void platform_gl_hints();

// Safe GLEW init for all platforms (sets glewExperimental on Apple).
bool platform_glew_init();

// GLSL version string for ImGui OpenGL3 backend (matches shared shader policy).
const char* platform_imgui_glsl_version();

// Directory containing the running executable (no trailing slash).
std::string executable_directory();
