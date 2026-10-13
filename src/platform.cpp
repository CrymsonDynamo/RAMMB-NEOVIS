#include "platform.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#ifndef RAMMB_GL_MAJOR
#ifdef __APPLE__
#define RAMMB_GL_MAJOR 4
#define RAMMB_GL_MINOR 1
#define RAMMB_GL_FORWARD_COMPAT 1
#else
#define RAMMB_GL_MAJOR 4
#define RAMMB_GL_MINOR 5
#define RAMMB_GL_FORWARD_COMPAT 0
#endif
#endif

void platform_gl_hints() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, RAMMB_GL_MAJOR);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, RAMMB_GL_MINOR);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if RAMMB_GL_FORWARD_COMPAT
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);
}

bool platform_glew_init() {
#ifdef __APPLE__
    // Required on macOS before glewInit(); harmless elsewhere if ever set always.
    glewExperimental = GL_TRUE;
#endif
    GLenum err = glewInit();
    // GLEW may report GL_INVALID_ENUM on core profiles after a successful init.
    glGetError();
    if (err != GLEW_OK) {
        std::cerr << "[platform] GLEW failed: " << glewGetErrorString(err) << "\n";
        return false;
    }
    return true;
}

const char* platform_imgui_glsl_version() {
    // Universal GLSL 410 — Linux 4.5 contexts accept it; macOS is capped at 4.1.
    return "#version 410";
}

std::string executable_directory() {
#ifdef __APPLE__
    char initial[1024];
    uint32_t size = sizeof(initial);
    std::string path;
    if (_NSGetExecutablePath(initial, &size) == 0) {
        path = initial;
    } else {
        std::vector<char> buf(size);
        if (_NSGetExecutablePath(buf.data(), &size) != 0)
            return {};
        path.assign(buf.data());
    }
    std::error_code ec;
    auto resolved = std::filesystem::canonical(path, ec);
    if (ec)
        resolved = std::filesystem::path(path);
    return resolved.parent_path().string();
#else
    char buf[4096] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path().string();
#endif
}
