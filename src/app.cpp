#include "app.hpp"
#include "ui/sidebar.hpp"
#include "ui/scene_bar.hpp"
#include "ui/timeline.hpp"
#include "ui/tools_panel.hpp"
#include "ui/notifications.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <format>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unistd.h>

using json = nlohmann::json;

static constexpr const char* BASE_DATA = "https://slider.cira.colostate.edu/data";

// ── world_to_screen ───────────────────────────────────────────────────────────
// Inverse of Renderer::screen_to_world, using the current viewport.
// Used by the export crop overlay.
glm::vec2 App::world_to_screen(glm::vec2 world) const {
    // From renderer internals: nx = ((sx - vp_x)/vp_w)*2-1, ny = -((sy-vp_y)/vp_h)*2+1
    // world.x = pan.x + nx*half_w  →  nx = (world.x - pan.x) / half_w
    int win_w = 1, win_h = 1;
    glfwGetFramebufferSize(m_window, &win_w, &win_h);
    float vp_w   = float(win_w) - m_sidebar_w;
    float aspect = vp_w / float(win_h);
    float half_h = 0.5f / m_renderer.zoom();
    float half_w = half_h * aspect;

    glm::vec2 pan = m_renderer.pan();
    float nx =  (world.x - pan.x) / half_w;
    float ny = -(world.y - pan.y) / half_h;

    // vp_x is sidebar_w, vp_y is 0 (top of viewport, OpenGL flips handled)
    float sx = m_sidebar_w + (nx + 1.0f) * 0.5f * vp_w;
    float sy = (ny + 1.0f) * 0.5f * float(win_h);
    return { sx, sy };
}

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
    m_overlays.clear(m_renderer);
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

    m_window = glfwCreateWindow(width, height, "RAMMB-NEOVIS", nullptr, nullptr);
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

    // Resolve shader directory: prefer next to the executable, fall back to
    // the installed data path (/usr/share/rammb-neovis/shaders).
    auto find_shader_dir = []() -> std::string {
        // 1. Next to the executable (dev build or portable AppImage)
        char buf[4096] = {};
        ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n > 0) {
            std::filesystem::path exe_dir = std::filesystem::path(buf).parent_path();
            if (std::filesystem::exists(exe_dir / "shaders" / "tile.vert"))
                return (exe_dir / "shaders").string();
        }
        // 2. Installed data dir
        if (std::filesystem::exists("/usr/share/rammb-neovis/shaders/tile.vert"))
            return "/usr/share/rammb-neovis/shaders";
        // 3. Relative fallback (cwd)
        return "shaders";
    };
    if (!m_renderer.init(find_shader_dir().c_str())) return false;
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
    notifs_tick(dt);

    // ── Export button ─────────────────────────────────────────────────────────
    if (m_state.export_requested) {
        m_state.export_requested = false;
        m_export_state.open = true;
        m_export_state.crop = { -0.5f, 0.5f, -0.5f, 0.5f }; // default: full image
    }

    // ── Handle sidebar dirty flags ────────────────────────────────────────────
    if (m_state.source_changed || m_state.refresh_request || m_state.range_changed) {
        // When source changes, invalidate cached time lists
        if (m_state.source_changed) {
            m_state.avail_start_times.clear();
            m_state.avail_end_times.clear();
            if (m_state.date_range.use_range) {
                m_state.request_start_times = true;
                m_state.request_end_times   = true;
            }
        }
        reload_source();
        m_state.source_changed = m_state.refresh_request = m_state.range_changed = false;
    } else if (m_state.zoom_changed) {
        m_tiles.clear(m_renderer);
        m_tiles.set_frames(m_state.satellite, m_state.sector, m_state.product,
                           m_state.frame_timestamps, m_state.data_zoom);
        m_overlays.clear(m_renderer);
        m_overlays.set_source(m_state.satellite, m_state.sector, m_state.data_zoom);
        m_state.zoom_changed = false;
    }

    // ── Fetch available times for date-range dropdowns ───────────────────────
    if (m_state.request_start_times) {
        m_state.request_start_times = false;
        m_state.avail_start_times = fetch_times_for_date(
            m_state.date_range.start_year,
            m_state.date_range.start_month,
            m_state.date_range.start_day);
        m_state.start_time_sel = 0; // default to earliest
        if (!m_state.avail_start_times.empty()) {
            std::string s = std::to_string(m_state.avail_start_times.front());
            if (s.size() >= 12) {
                m_state.date_range.start_hour = (s[8]-'0')*10 + (s[9]-'0');
                m_state.date_range.start_min  = (s[10]-'0')*10 + (s[11]-'0');
            }
        }
    }
    if (m_state.request_end_times) {
        m_state.request_end_times = false;
        m_state.avail_end_times = fetch_times_for_date(
            m_state.date_range.end_year,
            m_state.date_range.end_month,
            m_state.date_range.end_day);
        m_state.end_time_sel = std::max(0, int(m_state.avail_end_times.size()) - 1); // default to latest
        if (!m_state.avail_end_times.empty()) {
            std::string s = std::to_string(m_state.avail_end_times.back());
            if (s.size() >= 12) {
                m_state.date_range.end_hour = (s[8]-'0')*10 + (s[9]-'0');
                m_state.date_range.end_min  = (s[10]-'0')*10 + (s[11]-'0');
            }
        }
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

    // ── Tile failure warnings ─────────────────────────────────────────────────
    {
        static int s_last_reported_fail = 0;
        int fails = m_tiles.failed_tiles();
        if (fails > 0 && fails != s_last_reported_fail && m_tiles.pending_downloads() == 0) {
            push_notif(std::format("{} tile(s) failed to download (404 or timeout)",
                       fails - s_last_reported_fail),
                       NotifLevel::Warn, 7.0f);
            s_last_reported_fail = fails;
        }
        if (fails == 0) s_last_reported_fail = 0; // reset on source change
    }

    // ── Update TileManager with current viewport ──────────────────────────────
    int win_w = 1, win_h = 1;
    glfwGetFramebufferSize(m_window, &win_w, &win_h);
    static constexpr float BOTTOM_H = 52.0f + 32.0f; // timeline + status bar
    float vp_w    = float(win_w) - m_sidebar_w;
    float render_h = std::max(1.0f, float(win_h) - BOTTOM_H - m_bar_h);
    float aspect  = vp_w / render_h;
    float half_h  = 0.5f / m_renderer.zoom();
    float half_w  = half_h * aspect;
    glm::vec2 pan = m_renderer.pan();

    // Pass tile region selection mask to TileManager
    if (m_state.tile_select_all)
        m_tiles.set_tile_selection({});  // empty = download all
    else
        m_tiles.set_tile_selection(m_state.tile_selection);

    m_tiles.update(pan.x - half_w, pan.x + half_w,
                   pan.y - half_h, pan.y + half_h,
                   m_state.current_frame,
                   m_renderer);

    m_overlays.update(pan.x - half_w, pan.x + half_w,
                      pan.y - half_h, pan.y + half_h,
                      m_state.overlays, m_renderer);
}

void App::render() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int win_w = 1, win_h = 1;
    glfwGetFramebufferSize(m_window, &win_w, &win_h);

    // ── Scene tabs + settings bar (top of window, full width) ────────────────
    float bar_h = scene_bar_draw(m_scene_bar, m_state, float(win_w), float(win_h));
    m_bar_h = bar_h;

    m_sidebar_w = sidebar_draw(m_state, float(win_h) - bar_h, bar_h);

    float vp_left  = m_sidebar_w;
    float vp_w     = float(win_w) - vp_left;

    // Heights of bottom UI panels
    static constexpr float TIMELINE_H  = 52.0f;
    static constexpr float STATUSBAR_H = 32.0f;
    float bottom_h  = TIMELINE_H + STATUSBAR_H;
    float render_h  = float(win_h) - bottom_h - bar_h;

    // ── Right-side tools panel ────────────────────────────────────────────────
    {
        float svp_x = vp_left, svp_y = bar_h, svp_w = vp_w, svp_h = render_h;
        auto w2s_tp = [this, svp_x, svp_y, svp_w, svp_h](glm::vec2 wp) -> glm::vec2 {
            float half_h = 0.5f / m_renderer.zoom();
            float half_w = half_h * (svp_w / svp_h);
            glm::vec2 pan = m_renderer.pan();
            float nx =  (wp.x - pan.x) / half_w;
            float ny =  (wp.y - pan.y) / half_h;
            return { svp_x + (nx + 1.0f) * 0.5f * svp_w,
                     svp_y + (-ny + 1.0f) * 0.5f * svp_h };
        };
        tools_panel_draw(m_state, svp_x, svp_y, svp_w, svp_h, w2s_tp);
    }

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

    // Pause / Resume button
    bool paused = m_tiles.downloads_paused();
    if (paused) {
        ImGui::PushStyleColor(ImGuiCol_Button,        { 0.55f,0.40f,0.05f,1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.75f,0.55f,0.10f,1.0f });
    }
    if (ImGui::SmallButton(paused ? "Resume" : "Pause")) {
        paused ? m_tiles.resume_downloads() : m_tiles.pause_downloads();
    }
    if (paused) ImGui::PopStyleColor(2);
    ImGui::SameLine(0, 8);

    if (pending > 0 || paused) {
        if (paused)
            ImGui::TextColored({ 1.0f, 0.65f, 0.1f, 1.0f }, "Paused");
        else
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

    // ── Export panel + crop overlay ───────────────────────────────────────────
    if (m_export_state.open) {
        // Screen-space tile render viewport (excludes sidebar + bottom bar).
        // Both s2w and w2s must use the SAME viewport as the tile render so
        // the crop overlay is pixel-aligned with the imagery — no parallax.
        float svp_x = m_sidebar_w;
        float svp_y = bar_h;        // render area starts below scene tab bar
        float svp_w = vp_w;
        float svp_h = render_h;

        auto s2w = [this, svp_x, svp_y, svp_w, svp_h](glm::vec2 sp) -> glm::vec2 {
            float nx =  ((sp.x - svp_x) / svp_w) * 2.0f - 1.0f;
            float ny = -((sp.y - svp_y) / svp_h) * 2.0f + 1.0f;
            float half_h = 0.5f / m_renderer.zoom();
            float half_w = half_h * (svp_w / svp_h);
            glm::vec2 pan = m_renderer.pan();
            return { pan.x + nx * half_w, pan.y + ny * half_h };
        };
        auto w2s = [this, svp_x, svp_y, svp_w, svp_h](glm::vec2 wp) -> glm::vec2 {
            float half_h = 0.5f / m_renderer.zoom();
            float half_w = half_h * (svp_w / svp_h);
            glm::vec2 pan = m_renderer.pan();
            float nx =  (wp.x - pan.x) / half_w;
            float ny =  (wp.y - pan.y) / half_h;
            return { svp_x + (nx + 1.0f) * 0.5f * svp_w,
                     svp_y + (-ny + 1.0f) * 0.5f * svp_h };
        };

        bool triggered = export_panel_draw(
            m_export_state,
            svp_x, svp_y, svp_w, svp_h,
            int(m_state.frame_timestamps.size()),
            s2w, w2s);

        if (triggered && !m_exporter.running()) {
            // Build frames: render each frame offscreen
            auto prog = std::make_shared<ExportProgress>();
            m_export_state.progress = prog;

            int N = int(m_state.frame_timestamps.size());
            int ow = m_export_state.settings.out_width;
            int oh = m_export_state.settings.out_height;
            CropRegion cr = m_export_state.crop;

            std::vector<ExportFrame> frames;
            frames.reserve(N);
            for (int fi = 0; fi < N; ++fi) {
                auto tile_list = m_tiles.ready_tiles_for_frame(fi);
                Renderer::OffscreenResult res;
                if (m_renderer.render_offscreen(
                        tile_list,
                        cr.x_min, cr.x_max, cr.y_min, cr.y_max,
                        ow, oh, res)) {
                    ExportFrame ef;
                    ef.rgba      = std::move(res.rgba);
                    ef.timestamp = m_state.frame_timestamps[fi];
                    frames.push_back(std::move(ef));
                }
            }

            m_exporter.start(m_export_state.settings, std::move(frames), prog);
        }
    }

    // ── 3D tile render ────────────────────────────────────────────────────────
    int render_h_i = int(render_h);
    glScissor(int(m_sidebar_w), int(bottom_h), int(vp_w), render_h_i);
    glEnable(GL_SCISSOR_TEST);
    m_renderer.resize_viewport(int(vp_left), int(bottom_h), int(vp_w), render_h_i);
    m_renderer.begin_frame();
    m_tiles.draw(m_state.current_frame, m_renderer);
    m_overlays.draw(m_state.overlays, m_renderer);
    glDisable(GL_SCISSOR_TEST);
    m_renderer.resize_viewport(0, 0, win_w, win_h);

    // ── Notification toasts (bottom-right) ───────────────────────────────────
    notifs_draw(float(win_w), float(win_h), bottom_h);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Data fetching ─────────────────────────────────────────────────────────────

std::vector<int64_t> App::fetch_times_for_date(int year, int month, int day) {
    char day_s[16];
    snprintf(day_s, sizeof(day_s), "%04d%02d%02d", year, month, day);

    std::string url = std::format("{}/json/{}/{}/{}/{}_by_hour.json",
        BASE_DATA, m_state.satellite, m_state.sector, m_state.product, day_s);

    std::vector<uint8_t> buf;
    if (!m_http.get(url, buf)) return {};

    auto j = json::parse(buf, nullptr, false);
    if (j.is_discarded() || !j.contains("timestamps_int")) return {};

    std::vector<int64_t> times;
    auto& by_hour = j["timestamps_int"];
    for (auto& [hour_key, arr] : by_hour.items()) {
        for (auto& ts_val : arr)
            times.push_back(ts_val.get<int64_t>());
    }

    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    return times;
}

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

        // Apply time_step filter to thin the frame list if needed.
        // Timestamps are YYYYMMDDHHMMSS — convert to real minutes for comparison.
        if (m_state.time_step > 1 && !m_state.frame_timestamps.empty()) {
            // Convert YYYYMMDDHHMMSS → total minutes since epoch-ish for spacing calc
            auto ts_to_minutes = [](int64_t ts) -> int64_t {
                ts /= 100; // skip seconds
                int mm = int(ts % 100);        ts /= 100;
                int hh = int(ts % 100);        ts /= 100;
                int dd = int(ts % 100);        ts /= 100;
                int mo = int(ts % 100);        ts /= 100;
                int yy = int(ts);
                // Approximate: good enough for spacing (not calendar-exact)
                return int64_t(yy) * 525960LL  // ~365.25 * 24 * 60
                     + int64_t(mo) * 43800LL   // ~30.4 * 24 * 60
                     + int64_t(dd) * 1440LL
                     + int64_t(hh) * 60LL
                     + int64_t(mm);
            };

            std::vector<int64_t> filtered;
            filtered.push_back(m_state.frame_timestamps.front());
            int64_t last_min = ts_to_minutes(m_state.frame_timestamps.front());
            for (size_t i = 1; i < m_state.frame_timestamps.size(); ++i) {
                int64_t cur_min = ts_to_minutes(m_state.frame_timestamps[i]);
                if (cur_min - last_min >= m_state.time_step) {
                    filtered.push_back(m_state.frame_timestamps[i]);
                    last_min = cur_min;
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
    // update() is called at the end of App::update() and will eagerly queue
    // all frames' visible tiles on the very first frame after reload.

    m_anim.set_frames(m_state.frame_timestamps);
    m_anim.set_fps(m_state.fps);
    m_anim.set_mode(AnimationController::Mode(m_state.loop_mode));

    m_state.current_frame = 0;
    m_state.timestamp_str = ts_to_display(m_anim.current_timestamp());
}

void App::reload_source() {
    m_tiles.clear(m_renderer);
    m_overlays.clear(m_renderer);
    m_overlays.set_source(m_state.satellite, m_state.sector, m_state.data_zoom);
    m_anim.clear();
    m_state.frame_timestamps.clear();
    m_state.current_frame = 0;
    m_state.playing       = false;  // stop playback so first update() sees frame 0

    if (!fetch_timestamps()) {
        push_notif("Failed to fetch timestamps. Check connection or product selection.",
                   NotifLevel::Error, 8.0f);
        return;
    }
    apply_frames();

    int N = int(m_state.frame_timestamps.size());
    push_notif(std::format("Loaded {} frames for {}/{}/{}",
               N, m_state.satellite, m_state.sector, m_state.product),
               NotifLevel::Info, 4.0f);
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

    // Use the correct tile render viewport (not the full window) so zoom-to-cursor
    // is pixel-accurate relative to the imagery.
    int win_w = 1, win_h = 1;
    glfwGetWindowSize(w, &win_w, &win_h);
    static constexpr float BOTTOM_H = 52.0f + 32.0f;
    float svp_x = app->m_sidebar_w;
    float svp_y = app->m_bar_h;
    float svp_w = float(win_w) - svp_x;
    float svp_h = std::max(1.0f, float(win_h) - BOTTOM_H - app->m_bar_h);

    auto s2w = [&](float sx, float sy) -> glm::vec2 {
        float nx =  ((sx - svp_x) / svp_w) * 2.0f - 1.0f;
        float ny = -((sy - svp_y) / svp_h) * 2.0f + 1.0f;
        float half_h = 0.5f / app->m_renderer.zoom();
        float half_w = half_h * (svp_w / svp_h);
        glm::vec2 pan = app->m_renderer.pan();
        return { pan.x + nx * half_w, pan.y + ny * half_h };
    };

    glm::vec2 before = s2w(float(mx), float(my));
    float new_zoom = std::clamp(app->m_renderer.zoom() * (dy > 0 ? 1.15f : 1.0f/1.15f), 0.25f, 256.0f);
    app->m_renderer.set_zoom(new_zoom);
    glm::vec2 after = s2w(float(mx), float(my));
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

    // If the export crop overlay or tile selector has taken ownership of this
    // drag, don't also pan the viewport.
    if (app->m_export_state.open &&
        (app->m_export_state.dragging_body || app->m_export_state.dragging_corner))
        return;
    if (app->m_state.tools_panel_open)
        return;

    int win_w = 1, win_h = 1;
    glfwGetWindowSize(w, &win_w, &win_h);
    static constexpr float BOTTOM_H = 52.0f + 32.0f; // timeline + status bar
    float vp_w    = float(win_w) - app->m_sidebar_w;
    float render_h = std::max(1.0f, float(win_h) - BOTTOM_H - app->m_bar_h);
    float aspect  = vp_w / render_h;
    glm::vec2 delta = glm::vec2{ float(x), float(y) } - app->m_drag_start_screen;
    float half_h = 0.5f / app->m_renderer.zoom();
    float half_w = half_h * aspect;
    app->m_renderer.set_pan(app->m_drag_start_pan + glm::vec2{
        -delta.x / vp_w     * 2.0f * half_w,
         delta.y / render_h * 2.0f * half_h });
}
