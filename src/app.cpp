#include "app.hpp"
#include "ui/sidebar.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <format>
#include <algorithm>
#include <cmath>

using json = nlohmann::json;

static constexpr const char* BASE_DATA = "https://slider.cira.colostate.edu/data";

// ── App lifecycle ─────────────────────────────────────────────────────────────

App::~App() {
    m_tiles.clear(m_renderer);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool App::init(int width, int height) {
    if (!glfwInit()) { std::cerr << "[App] GLFW init failed\n"; return false; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    m_window = glfwCreateWindow(width, height, "RAMMB NeoVis", nullptr, nullptr);
    if (!m_window) { std::cerr << "[App] Window creation failed\n"; glfwTerminate(); return false; }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, cb_resize);
    glfwSetScrollCallback        (m_window, cb_scroll);
    glfwSetMouseButtonCallback   (m_window, cb_mouse_button);
    glfwSetCursorPosCallback     (m_window, cb_cursor_pos);
    glfwSetKeyCallback           (m_window, cb_key);

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) { std::cerr << "[App] GLEW failed\n"; return false; }

    std::cout << "OpenGL : " << glGetString(GL_VERSION) << "\n";
    std::cout << "GPU    : " << glGetString(GL_RENDERER) << "\n";

    if (!m_renderer.init("shaders")) { std::cerr << "[App] Renderer init failed\n"; return false; }
    m_renderer.resize(width, height);

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.WindowRounding    = 0.0f;
    style.ScrollbarRounding = 3.0f;
    style.ItemSpacing       = { 6.0f, 5.0f };
    style.FramePadding      = { 6.0f, 4.0f };

    ImVec4* c = style.Colors;
    c[ImGuiCol_FrameBg]         = { 0.14f, 0.14f, 0.18f, 1.0f };
    c[ImGuiCol_FrameBgHovered]  = { 0.20f, 0.20f, 0.26f, 1.0f };
    c[ImGuiCol_Header]          = { 0.14f, 0.14f, 0.18f, 1.0f };
    c[ImGuiCol_HeaderHovered]   = { 0.20f, 0.44f, 0.78f, 1.0f };
    c[ImGuiCol_Button]          = { 0.16f, 0.16f, 0.22f, 1.0f };
    c[ImGuiCol_ButtonHovered]   = { 0.24f, 0.48f, 0.85f, 1.0f };
    c[ImGuiCol_ButtonActive]    = { 0.15f, 0.40f, 0.75f, 1.0f };
    c[ImGuiCol_SliderGrab]      = { 0.20f, 0.60f, 1.00f, 1.0f };
    c[ImGuiCol_CheckMark]       = { 0.20f, 0.60f, 1.00f, 1.0f };
    c[ImGuiCol_SeparatorHovered]= { 0.20f, 0.60f, 1.00f, 1.0f };
    c[ImGuiCol_PopupBg]         = { 0.09f, 0.09f, 0.12f, 0.98f };

    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    m_running = true;
    reload_source();
    return true;
}

void App::run() {
    while (m_running && !glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        update();
        render();
        glfwSwapBuffers(m_window);
    }
}

// ── Per-frame ────────────────────────────────────────────────────────────────

void App::update() {
    // Handle sidebar state changes
    if (m_state.source_changed || m_state.zoom_changed || m_state.refresh_request) {
        if (m_state.source_changed || m_state.refresh_request) {
            reload_source();
        } else {
            // Just zoom changed: clear tiles and reset TileManager zoom
            m_tiles.clear(m_renderer);
            m_tiles.set_source(m_state.satellite, m_state.sector,
                               m_state.product,   m_state.timestamp,
                               m_state.data_zoom);
        }
        m_state.source_changed  = false;
        m_state.zoom_changed    = false;
        m_state.refresh_request = false;
    }

    // Apply throttle setting every frame (atomic, zero cost if unchanged)
    m_tiles.set_throttle(m_state.download_limit_kbps);

    // Update TileManager: compute visible viewport in world space
    int win_w = 1, win_h = 1;
    glfwGetFramebufferSize(m_window, &win_w, &win_h);
    float vp_w   = float(win_w) - m_sidebar_w;
    float aspect = vp_w / float(win_h);

    float half_h = 0.5f / m_renderer.zoom();
    float half_w = half_h * aspect;

    glm::vec2 pan = m_renderer.pan();
    m_tiles.update(pan.x - half_w, pan.x + half_w,
                   pan.y - half_h, pan.y + half_h,
                   m_renderer);
}

void App::render() {
    // ── ImGui frame ───────────────────────────────────────────────────────────
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int win_w = 1, win_h = 1;
    glfwGetFramebufferSize(m_window, &win_w, &win_h);

    m_sidebar_w = sidebar_draw(m_state, float(win_h));

    // ── Status / progress bar at bottom of viewport ───────────────────────────
    float vp_left   = m_sidebar_w;
    float vp_w      = float(win_w) - vp_left;
    float bar_h     = 38.0f;

    ImGui::SetNextWindowPos ({ vp_left, float(win_h) - bar_h });
    ImGui::SetNextWindowSize({ vp_w,    bar_h });
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.0f, 4.0f });
    ImGui::Begin("##status", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    int pending = m_tiles.pending_downloads();
    int loaded  = m_tiles.loaded_tiles();
    int total   = m_tiles.total_tiles();

    bool downloading = (pending > 0);
    float fraction   = (total > 0) ? std::clamp(float(loaded) / float(total), 0.0f, 1.0f) : 1.0f;

    // Left: info text
    if (downloading) {
        ImGui::TextColored({ 0.3f, 0.8f, 1.0f, 1.0f }, "Downloading");
        ImGui::SameLine();
        ImGui::Text("%d / %d tiles", loaded, total);
    } else {
        ImGui::TextColored({ 0.2f, 0.85f, 0.4f, 1.0f }, "Ready");
        ImGui::SameLine();
        ImGui::TextDisabled("%d tiles  |  scroll=zoom  drag=pan  Home=reset  R=refresh", loaded);
    }

    // Right: zoom + pan
    ImGui::SameLine(vp_w - 200.0f);
    ImGui::TextDisabled("zoom %.2fx  pan %.2f, %.2f",
        m_renderer.zoom(), m_renderer.pan().x, m_renderer.pan().y);

    // Progress bar (full width, slim, below the text)
    if (downloading || fraction < 1.0f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.20f, 0.60f, 1.00f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_FrameBg,       { 0.10f, 0.10f, 0.14f, 1.0f });
        ImGui::ProgressBar(fraction, { vp_w - 16.0f, 4.0f }, "");
        ImGui::PopStyleColor(2);
    }

    ImGui::End();
    ImGui::PopStyleVar();

    // ── 3D render pass ────────────────────────────────────────────────────────
    // Scissor/viewport restricted to right of sidebar
    glScissor(int(m_sidebar_w), 0, int(vp_w), win_h);
    glEnable(GL_SCISSOR_TEST);
    m_renderer.resize_viewport(int(vp_left), 0, int(vp_w), win_h);

    m_renderer.begin_frame();
    m_tiles.draw(m_renderer);

    glDisable(GL_SCISSOR_TEST);
    m_renderer.resize_viewport(0, 0, win_w, win_h);

    // ── ImGui render ──────────────────────────────────────────────────────────
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Data fetching ─────────────────────────────────────────────────────────────

bool App::fetch_latest_timestamp() {
    std::string url = std::format(
        "{}/json/{}/{}/{}/latest_times.json",
        BASE_DATA, m_state.satellite, m_state.sector, m_state.product);

    std::cout << "[App] GET " << url << "\n";
    std::vector<uint8_t> buf;
    if (!m_http.get(url, buf)) {
        std::cerr << "[App] Metadata fetch failed: " << m_http.last_error() << "\n";
        return false;
    }

    auto meta = json::parse(buf, nullptr, false);
    if (meta.is_discarded() || !meta.contains("timestamps_int")) {
        std::cerr << "[App] Bad metadata JSON\n";
        return false;
    }

    int64_t ts      = meta["timestamps_int"][0].get<int64_t>();
    std::string ts_s= std::to_string(ts);

    m_state.timestamp     = ts;
    m_state.timestamp_str = std::format("{}-{}-{} {}:{}:{} UTC",
        ts_s.substr(0,4), ts_s.substr(4,2), ts_s.substr(6,2),
        ts_s.substr(8,2), ts_s.substr(10,2), ts_s.substr(12,2));

    std::cout << "[App] Latest: " << m_state.timestamp_str << "\n";
    return true;
}

void App::reload_source() {
    m_tiles.clear(m_renderer);
    if (!fetch_latest_timestamp()) return;

    m_tiles.set_source(m_state.satellite, m_state.sector,
                       m_state.product,   m_state.timestamp,
                       m_state.data_zoom);
}

// ── GLFW callbacks ─────────────────────────────────────────────────────────────

void App::cb_resize(GLFWwindow* w, int width, int height) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    app->m_renderer.resize(width, height);
}

void App::cb_key(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    if (key == GLFW_KEY_ESCAPE) app->m_running = false;
    if (key == GLFW_KEY_R)      app->reload_source();
    if (key == GLFW_KEY_HOME) {
        app->m_renderer.set_pan({ 0.0f, 0.0f });
        app->m_renderer.set_zoom(1.0f);
    }
}

void App::cb_scroll(GLFWwindow* w, double /*dx*/, double dy) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureMouse) return;

    double mx = 0, my = 0;
    glfwGetCursorPos(w, &mx, &my);
    // Ignore scrolls inside sidebar
    if (mx < app->m_sidebar_w) return;

    glm::vec2 before = app->m_renderer.screen_to_world({ float(mx), float(my) });

    float factor   = (dy > 0) ? 1.15f : (1.0f / 1.15f);
    float new_zoom = std::clamp(app->m_renderer.zoom() * factor, 0.25f, 256.0f);
    app->m_renderer.set_zoom(new_zoom);

    // Anchor the world point under the cursor
    glm::vec2 after = app->m_renderer.screen_to_world({ float(mx), float(my) });
    app->m_renderer.set_pan(app->m_renderer.pan() + (before - after));
}

void App::cb_mouse_button(GLFWwindow* w, int btn, int action, int /*mods*/) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (btn == GLFW_MOUSE_BUTTON_LEFT) {
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        if (x < app->m_sidebar_w) return;

        if (action == GLFW_PRESS) {
            app->m_dragging           = true;
            app->m_drag_start_screen  = { float(x), float(y) };
            app->m_drag_start_pan     = app->m_renderer.pan();
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        } else {
            app->m_dragging = false;
            glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
}

void App::cb_cursor_pos(GLFWwindow* w, double x, double y) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (!app->m_dragging) return;

    int win_w = 1, win_h = 1;
    glfwGetWindowSize(w, &win_w, &win_h);

    float vp_w   = float(win_w) - app->m_sidebar_w;
    float aspect = vp_w / float(win_h);

    glm::vec2 delta = glm::vec2{ float(x), float(y) } - app->m_drag_start_screen;
    float half_h    = 0.5f / app->m_renderer.zoom();
    float half_w    = half_h * aspect;

    glm::vec2 world_delta{
        -delta.x / vp_w * 2.0f * half_w,
         delta.y / float(win_h) * 2.0f * half_h,
    };

    app->m_renderer.set_pan(app->m_drag_start_pan + world_delta);
}
