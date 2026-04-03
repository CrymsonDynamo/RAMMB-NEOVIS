#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "renderer/renderer.hpp"
#include "network/http_client.hpp"
#include "tile_manager.hpp"
#include "animation.hpp"
#include "ui/sidebar.hpp"

class App {
public:
    App() = default;
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(int width = 1440, int height = 900);
    void run();

private:
    void update(float dt);
    void render();

    // Fetch timestamps for the current source into state.frame_timestamps.
    // latest-N mode: uses latest_times.json
    // range mode:    uses YYYYMMDD_by_hour.json for each day in range
    bool fetch_timestamps();

    // Full reload: fetch timestamps, load frames into TileManager + AnimController
    void reload_source();

    // Apply state.frame_timestamps → AnimationController + TileManager
    void apply_frames();

    static void cb_resize      (GLFWwindow*, int, int);
    static void cb_scroll      (GLFWwindow*, double, double);
    static void cb_mouse_button(GLFWwindow*, int, int, int);
    static void cb_cursor_pos  (GLFWwindow*, double, double);
    static void cb_key         (GLFWwindow*, int, int, int, int);

    GLFWwindow*          m_window{nullptr};
    Renderer             m_renderer;
    HttpClient           m_http;
    TileManager          m_tiles;
    AnimationController  m_anim;

    ViewState   m_state;
    float       m_sidebar_w{300.0f};

    bool      m_dragging{false};
    glm::vec2 m_drag_start_screen{};
    glm::vec2 m_drag_start_pan{};

    bool m_running{false};
};
