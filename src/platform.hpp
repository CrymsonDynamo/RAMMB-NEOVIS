#pragma once
#include <string>

struct GLFWwindow;

// Thin platform helpers — keep OS/GL differences out of app hot paths.

// GLFW context version hints (4.1+forward on Apple, 4.5 elsewhere).
void platform_gl_hints();

// Safe GLEW init for all platforms (sets glewExperimental on Apple).
bool platform_glew_init();

// GLSL version string for ImGui OpenGL3 backend (matches shared shader policy).
const char* platform_imgui_glsl_version();

// Directory containing the running executable (no trailing slash).
std::string executable_directory();

// Window content scale (framebuffer / window). Typically 1 on Linux, 2 on Retina.
struct ContentScale {
    float x{1.0f};
    float y{1.0f};
};
ContentScale window_content_scale(GLFWwindow* window);

// True if Control or Super/Cmd is held — shared accelerators on Linux and macOS.
bool platform_primary_mod(int glfw_mods);

// Native file dialogs (portable-file-dialogs: zenity/kdialog on Linux, osascript on Mac).
// Returns selected path, or "" if cancelled / unavailable.
std::string platform_open_file(const char* title,
                               const char* default_path = "",
                               const char* filter_desc = "All Files",
                               const char* filter_glob = "*");
std::string platform_save_file(const char* title,
                               const char* default_path = "",
                               const char* filter_desc = "All Files",
                               const char* filter_glob = "*");
std::string platform_pick_folder(const char* title,
                                 const char* default_path = "");
