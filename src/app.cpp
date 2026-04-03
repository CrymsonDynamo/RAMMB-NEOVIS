#include "app.hpp"
#include "ui/sidebar.hpp"
#include "ui/timeline.hpp"

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

// ── Timestamp utilities ───────────────────────────────────────────────────────

static std::string ts_to_display(int64_t ts) {
    std::string s = std::to_string(ts);
    if (s.size() < 12) return s;
    return std::format("{}-{}-{} {}:{} UTC",
        s.substr(0,4), s.substr(4,2), s.substr(6,2),
        s.substr(8,2), s.substr(10,2));
}

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
    if (!m_window) { std::cerr << "[App] Window failed\n"; glfwTerminate(); return false; }

    glfwSetWindowUserPointer    (m_window, this);
    glfwSetFramebufferSizeCallback(m_window, cb_resize);
    glfwSetScrollCallback         (m_window, cb_scroll);
    glfwSetMouseButtonCallback    (m_window, cb_mouse_button);
    glfwSetCursorPosCallback      (m_window, cb_cursor_pos);
    glfwSetKeyCallback            (m_window, cb_key);

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) { std::cerr << "[App] GLEW failed\n"; return false; }
    std::cout << "OpenGL : " << glGetString(GL_VERSION) << "\n";
    std::cout << "GPU    : " << glGetString(GL_RENDERER) << "\n";

    if (!m_renderer.init("shaders")) return false;
    m_renderer.resize(width, height);

    // ── ImGui ─────────────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiStyle& sty = ImGui::GetStyle();
    sty.FrameRounding  = 3.0f;
    sty.GrabRounding   = 3.0f;
    sty.WindowRounding = 0.0f;
    sty.ItemSpacing    = { 6.0f, 5.0f };
    sty.FramePadding   = { 6.0f, 4.0f };

    ImVec4* c = sty.Colors;
    c[ImGuiCol_FrameBg]          = { 0.14f, 0.14f, 0.18f, 1.0f };
    c[ImGuiCol_FrameBgHovered]   = { 0.20f, 0.20f, 0.26f, 1.0f };
    c[ImGuiCol_Header]           = { 0.14f, 0.14f, 0.18f, 1.0f };
    c[ImGuiCol_HeaderHovered]    = { 0.20f, 0.44f, 0.78f, 1.0f };
    c[ImGuiCol_Button]           = { 0.16f, 0.16f, 0.22f, 1.0f };
    c[ImGuiCol_ButtonHovered]    = { 0.24f, 0.48f, 0.85f, 1.0f };
    c[ImGuiCol_ButtonActive]     = { 0.15f, 0.40f, 0.75f, 1.0f };
    c[ImGuiCol_SliderGrab]       = { 0.20f, 0.60f, 1.00f, 1.0f };
    c[ImGuiCol_CheckMark]        = { 0.20f, 0.60f, 1.00f, 1.0f };
    c[ImGuiCol_PopupBg]          = { 0.09f, 0.09f, 0.12f, 0.98f };

    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    m_running = true;
    reload_source();
    return true;
}

void App::run() {
    double prev_time = glfwGetTime();
    while (m_running && !glfwWindowShouldClose(m_window)) {
        double now = glfwGetTime();
        float  dt  = float(now - prev_time);
        prev_time  = now;
        dt = std::clamp(dt, 0.0f, 0.1f); // cap at 100ms to avoid spiral on lag

        glfwPollEvents();
        update(dt);
        render();
        glfwSwapBuffers(m_window);
    }
}

// ── Per-frame ────────────────────────────────────────────────────────────────

void App::update(float dt) {
    // ── Handle sidebar dirty flags ────────────────────────────────────────────
    if (m_state.source_changed || m_state.refresh_request || m_state.range_changed) {
        reload_source();
        m_state.source_changed = m_state.refresh_request = m_state.range_changed = false;
    } else if (m_state.zoom_changed) {
        m_tiles.clear(m_renderer);
        m_tiles.set_frames(m_state.satellite, m_state.sector, m_state.product,
                           m_state.frame_timestamps, m_state.data_zoom);
        m_state.zoom_changed = false;
    }

    // ── Sync animation settings from UI ──────────────────────────────────────
    m_anim.set_fps(m_state.fps);
    m_anim.set_mode(AnimationController::Mode(m_state.loop_mode));

    // Sync play state: sidebar play button
    if (m_state.playing != m_anim.playing()) {
        m_state.playing ? m_anim.play() : m_anim.pause();
    }

    // Sidebar can also jump frame (prev/next/first/last buttons)
    if (!m_state.playing && m_state.current_frame != m_anim.current_frame())
        m_anim.jump(m_state.current_frame);

    m_anim.update(dt);

    // Write back frame index + timestamp string to state for UI
    m_state.current_frame = m_anim.current_frame();
    m_state.playing        = m_anim.playing();
    if (!m_state.frame_timestamps.empty())
        m_state.timestamp_str = ts_to_display(m_anim.current_timestamp());

    // ── Throttle ──────────────────────────────────────────────────────────────
    m_tiles.set_throttle(m_state.download_limit_kbps);

    // ── Update TileManager with current viewport ──────────────────────────────
    int win_w = 1, win_h = 1;
    glfwGetFramebufferSize(m_window, &win_w, &win_h);
    float vp_w   = float(win_w) - m_sidebar_w;
    float aspect = vp_w / float(win_h);
    float half_h = 0.5f / m_renderer.zoom();
    float half_w = half_h * aspect;
    glm::vec2 pan = m_renderer.pan();

    m_tiles.update(pan.x - half_w, pan.x + half_w,
                   pan.y - half_h, pan.y + half_h,
                   m_state.current_frame,
                   m_renderer);
}

void App::render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int win_w = 1, win_h = 1;
    glfwGetFramebufferSize(m_window, &win_w, &win_h);

    m_sidebar_w = sidebar_draw(m_state, float(win_h));

    float vp_left  = m_sidebar_w;
    float vp_w     = float(win_w) - vp_left;

    // Heights of bottom UI panels
    static constexpr float TIMELINE_H  = 52.0f;
    static constexpr float STATUSBAR_H = 32.0f;
    float bottom_h  = TIMELINE_H + STATUSBAR_H;

    // ── Timeline scrubber ─────────────────────────────────────────────────────
    bool scrubbed = timeline_draw(
        vp_left, float(win_h) - bottom_h,
        vp_w,    TIMELINE_H,
        m_state.frame_timestamps,
        m_state.current_frame,
        m_state.playing);
    if (scrubbed) m_anim.jump(m_state.current_frame);

    // ── Status / progress bar ─────────────────────────────────────────────────
    ImGui::SetNextWindowPos ({ vp_left, float(win_h) - STATUSBAR_H });
    ImGui::SetNextWindowSize({ vp_w,    STATUSBAR_H });
    ImGui::SetNextWindowBgAlpha(0.82f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.0f, 5.0f });
    ImGui::Begin("##status", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    int pending = m_tiles.pending_downloads();
    int loaded  = m_tiles.loaded_tiles();
    int total   = m_tiles.total_tiles();
    float frac  = total > 0 ? std::clamp(float(loaded) / float(total), 0.0f, 1.0f) : 1.0f;

    if (pending > 0) {
        ImGui::TextColored({ 0.3f, 0.8f, 1.0f, 1.0f }, "Downloading");
        ImGui::SameLine();
        ImGui::Text("%d / %d tiles", loaded, total);
    } else {
        ImGui::TextColored({ 0.2f, 0.85f, 0.4f, 1.0f }, "Ready");
        ImGui::SameLine();
        ImGui::TextDisabled("%d frames  %d tiles  |  drag=pan  scroll=zoom  Home=reset",
            m_tiles.frame_count(), loaded);
    }
    ImGui::SameLine(vp_w - 210.0f);
    ImGui::TextDisabled("zoom %.2fx  pan %.2f,%.2f",
        m_renderer.zoom(), m_renderer.pan().x, m_renderer.pan().y);

    if (pending > 0 || frac < 1.0f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.20f, 0.60f, 1.00f, 1.0f });
        ImGui::PushStyleColor(ImGuiCol_FrameBg,       { 0.10f, 0.10f, 0.14f, 1.0f });
        ImGui::ProgressBar(frac, { vp_w - 16.0f, 4.0f }, "");
        ImGui::PopStyleColor(2);
    }

    ImGui::End();
    ImGui::PopStyleVar();

    // ── 3D tile render ────────────────────────────────────────────────────────
    int render_h = int(float(win_h) - bottom_h);
    glScissor(int(m_sidebar_w), int(bottom_h), int(vp_w), render_h);
    glEnable(GL_SCISSOR_TEST);
    m_renderer.resize_viewport(int(vp_left), int(bottom_h), int(vp_w), render_h);
    m_renderer.begin_frame();
    m_tiles.draw(m_state.current_frame, m_renderer);
    glDisable(GL_SCISSOR_TEST);
    m_renderer.resize_viewport(0, 0, win_w, win_h);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Data fetching ─────────────────────────────────────────────────────────────

bool App::fetch_timestamps() {
    auto& dr = m_state.date_range;

    if (!dr.use_range) {
        // ── Latest N frames ───────────────────────────────────────────────────
        std::string url = std::format("{}/json/{}/{}/{}/latest_times.json",
            BASE_DATA, m_state.satellite, m_state.sector, m_state.product);
        std::cout << "[App] GET " << url << "\n";

        std::vector<uint8_t> buf;
        if (!m_http.get(url, buf)) {
            std::cerr << "[App] Fetch failed: " << m_http.last_error() << "\n";
            return false;
        }

        auto j = json::parse(buf, nullptr, false);
        if (j.is_discarded() || !j.contains("timestamps_int")) {
            std::cerr << "[App] Bad JSON\n"; return false;
        }

        auto& arr = j["timestamps_int"];
        int   N   = std::min(m_state.num_frames, int(arr.size()));

        m_state.frame_timestamps.clear();
        // API returns newest-first; we want oldest-first for animation
        for (int i = N - 1; i >= 0; --i)
            m_state.frame_timestamps.push_back(arr[i].get<int64_t>());

    } else {
        // ── Date range: query each day in range ───────────────────────────────
        m_state.frame_timestamps.clear();

        // Encode start/end as comparable int: YYYYMMDD
        auto ymd = [](int y, int mo, int d) { return y*10000 + mo*100 + d; };
        int start_date = ymd(dr.start_year, dr.start_month, dr.start_day);
        int end_date   = ymd(dr.end_year,   dr.end_month,   dr.end_day);
        if (start_date > end_date) std::swap(start_date, end_date);

        // Iterate days (simple: increment day, handle month/year rollover crudely)
        // We'll query the _by_hour JSON for each day and filter by hour range.
        int64_t start_filter = int64_t(dr.start_year)*10000000000LL
                             + int64_t(dr.start_month)*100000000LL
                             + int64_t(dr.start_day)*1000000LL
                             + int64_t(dr.start_hour)*10000LL
                             + int64_t(dr.start_min)*100LL;
        int64_t end_filter   = int64_t(dr.end_year)*10000000000LL
                             + int64_t(dr.end_month)*100000000LL
                             + int64_t(dr.end_day)*1000000LL
                             + int64_t(dr.end_hour)*10000LL
                             + int64_t(dr.end_min)*100LL + 59LL;

        // Build list of YYYYMMDD strings in range (brute-force day iteration)
        std::vector<std::string> days;
        {
            // Simple day counter using tm
            struct tm t{};
            t.tm_year = dr.start_year - 1900;
            t.tm_mon  = dr.start_month - 1;
            t.tm_mday = dr.start_day;
            time_t cur = mktime(&t);
            time_t end_t;
            struct tm te{};
            te.tm_year = dr.end_year - 1900;
            te.tm_mon  = dr.end_month - 1;
            te.tm_mday = dr.end_day;
            end_t = mktime(&te);

            while (cur <= end_t) {
                struct tm* lt = localtime(&cur);
                char buf[32];
                snprintf(buf, sizeof(buf), "%04d%02d%02d",
                    lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday);
                days.push_back(buf);
                cur += 86400;
            }
        }

        std::cout << "[App] Fetching " << days.size() << " days of timestamps\n";
        for (const auto& day : days) {
            std::string url = std::format("{}/json/{}/{}/{}/{}_by_hour.json",
                BASE_DATA, m_state.satellite, m_state.sector, m_state.product, day);

            std::vector<uint8_t> buf;
            if (!m_http.get(url, buf)) continue;

            auto j = json::parse(buf, nullptr, false);
            if (j.is_discarded() || !j.contains("timestamps_int")) continue;

            auto& by_hour = j["timestamps_int"];
            for (auto& [hour_key, arr] : by_hour.items()) {
                for (auto& ts_val : arr) {
                    int64_t ts = ts_val.get<int64_t>();
                    // Filter to selected range (compare to 12-digit prefix)
                    int64_t ts12 = ts / 100; // drop seconds
                    if (ts12 >= start_filter/100 && ts12 <= end_filter/100)
                        m_state.frame_timestamps.push_back(ts);
                }
            }
        }

        // Sort oldest → newest, deduplicate
        std::sort(m_state.frame_timestamps.begin(), m_state.frame_timestamps.end());
        m_state.frame_timestamps.erase(
            std::unique(m_state.frame_timestamps.begin(), m_state.frame_timestamps.end()),
            m_state.frame_timestamps.end());

        // Apply time_step filter to thin the frame list if needed
        if (m_state.time_step > 1 && !m_state.frame_timestamps.empty()) {
            std::vector<int64_t> filtered;
            filtered.push_back(m_state.frame_timestamps.front());
            int64_t last = m_state.frame_timestamps.front();
            for (auto ts : m_state.frame_timestamps) {
                // ts is YYYYMMDDHHMMSS; extract HHMM
                int64_t diff_min = ((ts / 100) % 10000) - ((last / 100) % 10000);
                if (std::abs(diff_min) >= m_state.time_step) {
                    filtered.push_back(ts);
                    last = ts;
                }
            }
            m_state.frame_timestamps = std::move(filtered);
        }

        if (m_state.frame_timestamps.empty()) {
            std::cerr << "[App] No timestamps found in date range\n";
            return false;
        }

        std::cout << "[App] " << m_state.frame_timestamps.size()
                  << " frames in range\n";
    }

    return true;
}

void App::apply_frames() {
    if (m_state.frame_timestamps.empty()) return;

    m_tiles.set_frames(m_state.satellite, m_state.sector, m_state.product,
                       m_state.frame_timestamps, m_state.data_zoom);
    m_anim.set_frames(m_state.frame_timestamps);
    m_anim.set_fps(m_state.fps);
    m_anim.set_mode(AnimationController::Mode(m_state.loop_mode));

    m_state.current_frame = 0;
    m_state.timestamp_str = ts_to_display(m_anim.current_timestamp());
}

void App::reload_source() {
    m_tiles.clear(m_renderer);
    m_anim.clear();
    m_state.frame_timestamps.clear();

    if (!fetch_timestamps()) return;
    apply_frames();
}

// ── GLFW callbacks ─────────────────────────────────────────────────────────────

void App::cb_resize(GLFWwindow* w, int width, int height) {
    static_cast<App*>(glfwGetWindowUserPointer(w))->m_renderer.resize(width, height);
}

void App::cb_key(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    switch (key) {
        case GLFW_KEY_ESCAPE: app->m_running = false; break;
        case GLFW_KEY_R:      app->reload_source(); break;
        case GLFW_KEY_HOME:
            app->m_renderer.set_pan({ 0.0f, 0.0f });
            app->m_renderer.set_zoom(1.0f);
            break;
        case GLFW_KEY_LEFT:  app->m_anim.step(-1); break;
        case GLFW_KEY_RIGHT: app->m_anim.step(+1); break;
        case GLFW_KEY_SPACE: app->m_anim.toggle_play(); break;
        default: break;
    }
}

void App::cb_scroll(GLFWwindow* w, double /*dx*/, double dy) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureMouse) return;
    double mx = 0, my = 0;
    glfwGetCursorPos(w, &mx, &my);
    if (mx < app->m_sidebar_w) return;

    glm::vec2 before = app->m_renderer.screen_to_world({ float(mx), float(my) });
    float new_zoom = std::clamp(app->m_renderer.zoom() * (dy > 0 ? 1.15f : 1.0f/1.15f), 0.25f, 256.0f);
    app->m_renderer.set_zoom(new_zoom);
    glm::vec2 after = app->m_renderer.screen_to_world({ float(mx), float(my) });
    app->m_renderer.set_pan(app->m_renderer.pan() + (before - after));
}

void App::cb_mouse_button(GLFWwindow* w, int btn, int action, int /*mods*/) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (btn != GLFW_MOUSE_BUTTON_LEFT) return;
    double x = 0, y = 0;
    glfwGetCursorPos(w, &x, &y);
    if (x < app->m_sidebar_w) return;

    if (action == GLFW_PRESS) {
        app->m_dragging          = true;
        app->m_drag_start_screen = { float(x), float(y) };
        app->m_drag_start_pan    = app->m_renderer.pan();
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
        app->m_dragging = false;
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
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
    float half_h = 0.5f / app->m_renderer.zoom();
    float half_w = half_h * aspect;
    app->m_renderer.set_pan(app->m_drag_start_pan + glm::vec2{
        -delta.x / vp_w * 2.0f * half_w,
         delta.y / float(win_h) * 2.0f * half_h });
}
