#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>

#include "renderer/renderer.hpp"
#include "network/http_client.hpp"
#include "tile_manager.hpp"
#include "ui/sidebar.hpp"

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(int width = 1400, int height = 900);
    void run();

private:
    void update();
    void render();

    // Fetch latest timestamp metadata for the current source.
    // On success: updates state.timestamp and state.timestamp_str.
    bool fetch_latest_timestamp();

    // Reload: clear tiles, set new source on TileManager, fetch timestamp.
    void reload_source();

    // GLFW callbacks
    static void cb_resize      (GLFWwindow*, int w, int h);
    static void cb_scroll      (GLFWwindow*, double dx, double dy);
    static void cb_mouse_button(GLFWwindow*, int btn, int action, int mods);
    static void cb_cursor_pos  (GLFWwindow*, double x, double y);
    static void cb_key         (GLFWwindow*, int key, int scancode, int action, int mods);

    GLFWwindow* m_window{nullptr};
    Renderer    m_renderer;
    HttpClient  m_http;
    TileManager m_tiles;

    ViewState   m_state;

    float       m_sidebar_w{290.0f};

    // Pan drag state
    bool      m_dragging{false};
    glm::vec2 m_drag_start_screen{};
    glm::vec2 m_drag_start_pan{};

    bool m_running{false};
};
