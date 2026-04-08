#pragma once
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "renderer/renderer.hpp"
#include "network/http_client.hpp"
#include "tile_manager.hpp"
#include "overlay_manager.hpp"
#include "animation.hpp"
#include "ui/sidebar.hpp"
#include "ui/scene_bar.hpp"
#include "ui/export_panel.hpp"
#include "ui/timeline.hpp"
#include "export/exporter.hpp"

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

    // Fetch all available timestamps for a single day from RAMMB API.
    std::vector<int64_t> fetch_times_for_date(int year, int month, int day);

    // Full reload: fetch timestamps, load frames into TileManager + AnimController
    void reload_source();

    // Apply state.frame_timestamps → AnimationController + TileManager
    void apply_frames();

    // Scene file I/O (use zenity for file dialogs)
    void save_scene();
    void save_scene_as();
    void open_scene();
    void update_window_title();

    // Returns path chosen by zenity, or "" if cancelled.
    std::string dialog_save_path();
    std::string dialog_open_path();

    static void cb_resize      (GLFWwindow*, int, int);
    static void cb_scroll      (GLFWwindow*, double, double);
    static void cb_mouse_button(GLFWwindow*, int, int, int);
    static void cb_cursor_pos  (GLFWwindow*, double, double);
    static void cb_key         (GLFWwindow*, int, int, int, int);

    // world_to_screen: inverse of Renderer::screen_to_world, needed for export overlay
    glm::vec2 world_to_screen(glm::vec2 world) const;

    GLFWwindow*          m_window{nullptr};
    Renderer             m_renderer;
    HttpClient           m_http;
    TileManager          m_tiles;
    OverlayManager       m_overlays;
    AnimationController  m_anim;
    Exporter             m_exporter;

    ViewState     m_state;
    SceneBar      m_scene_bar;
    ExportState   m_export_state;
    TimelineState m_timeline;
    float        m_sidebar_w{300.0f};

    float     m_bar_h{30.0f};    // scene tab bar height, set each render()
    bool      m_dragging{false};
    glm::vec2 m_drag_start_screen{};
    glm::vec2 m_drag_start_pan{};

    bool m_running{false};
};
