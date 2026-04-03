#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>

#include "renderer/renderer.hpp"
#include "network/http_client.hpp"

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(int width = 1280, int height = 720);
    void run();

private:
    void update();
    void render();
    void render_ui();

    // Downloads latest timestamp metadata then fetches zoom-0 tile
    bool fetch_latest_tile();

    // GLFW event callbacks (static, dispatch via user pointer)
    static void cb_resize      (GLFWwindow*, int w, int h);
    static void cb_scroll      (GLFWwindow*, double dx, double dy);
    static void cb_mouse_button(GLFWwindow*, int btn, int action, int mods);
    static void cb_cursor_pos  (GLFWwindow*, double x, double y);

    GLFWwindow* m_window{nullptr};
    Renderer    m_renderer;
    HttpClient  m_http;

    GLuint      m_tile_tex{0};
    std::string m_status{"Initializing..."};
    std::string m_timestamp_label;

    // Pan with left-click drag
    bool      m_dragging{false};
    glm::vec2 m_drag_start_screen{};
    glm::vec2 m_drag_start_pan{};

    bool m_running{false};
};
