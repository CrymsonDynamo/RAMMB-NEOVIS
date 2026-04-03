#include "app.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <stb_image.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <format>
#include <algorithm>

using json = nlohmann::json;

// ── RAMMB Slider API constants ───────────────────────────────────────────────
static constexpr const char* BASE_DATA = "https://slider.cira.colostate.edu/data";
static constexpr const char* SAT       = "goes-19";
static constexpr const char* SECTOR    = "full_disk";
static constexpr const char* PRODUCT   = "geocolor";

// ── App lifecycle ────────────────────────────────────────────────────────────

App::~App() {
    if (m_tile_tex) m_renderer.free_texture(m_tile_tex);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool App::init(int width, int height) {
    if (!glfwInit()) {
        std::cerr << "[App] GLFW init failed\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    m_window = glfwCreateWindow(width, height, "StormView — RAMMB Slider Client", nullptr, nullptr);
    if (!m_window) {
        std::cerr << "[App] Window creation failed\n";
        glfwTerminate();
        return false;
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, cb_resize);
    glfwSetScrollCallback(m_window, cb_scroll);
    glfwSetMouseButtonCallback(m_window, cb_mouse_button);
    glfwSetCursorPosCallback(m_window, cb_cursor_pos);

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // vsync on

    GLenum glew_err = glewInit();
    if (glew_err != GLEW_OK) {
        std::cerr << "[App] GLEW init failed: " << glewGetErrorString(glew_err) << "\n";
        return false;
    }

    std::cout << "OpenGL : " << glGetString(GL_VERSION) << "\n";
    std::cout << "GPU    : " << glGetString(GL_RENDERER) << "\n";

    if (!m_renderer.init("shaders")) {
        std::cerr << "[App] Renderer init failed\n";
        return false;
    }
    m_renderer.resize(width, height);

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr; // don't write imgui.ini
    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    m_running = true;
    return true;
}

void App::run() {
    m_status = "Fetching latest image...";
    if (!fetch_latest_tile())
        std::cerr << "[App] Initial tile fetch failed: " << m_status << "\n";

    while (m_running && !glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        update();
        render();
        glfwSwapBuffers(m_window);
    }
}

// ── Per-frame logic ──────────────────────────────────────────────────────────

void App::update() {
    if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        m_running = false;

    // R = reload latest frame
    static bool r_was_down = false;
    bool r_down = glfwGetKey(m_window, GLFW_KEY_R) == GLFW_PRESS;
    if (r_down && !r_was_down) {
        m_status = "Refreshing...";
        fetch_latest_tile();
    }
    r_was_down = r_down;
}

void App::render() {
    m_renderer.begin_frame();

    if (m_tile_tex) {
        // Zoom-0 tile = entire image in world space [-0.5, 0.5]
        TileQuad full_disk{ -0.5f, 0.5f, -0.5f, 0.5f };
        m_renderer.draw_tile(m_tile_tex, full_disk);
    }

    render_ui();
    // Caller (run loop) swaps buffers
}

void App::render_ui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ── Status bar pinned to the bottom ──────────────────────────────────
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0.0f, io.DisplaySize.y - 30.0f});
    ImGui::SetNextWindowSize({io.DisplaySize.x, 30.0f});
    ImGui::SetNextWindowBgAlpha(0.80f);
    ImGui::Begin("##statusbar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove   |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::Text("  %s  |  zoom %.2fx  |  scroll=zoom  drag=pan  R=refresh  Esc=quit",
        m_status.c_str(), m_renderer.zoom());
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Data fetching ─────────────────────────────────────────────────────────────

bool App::fetch_latest_tile() {
    // ── Step 1: latest timestamp ─────────────────────────────────────────
    std::string meta_url = std::format(
        "{}/json/{}/{}/{}/latest_times.json",
        BASE_DATA, SAT, SECTOR, PRODUCT);

    std::cout << "[App] GET " << meta_url << "\n";
    std::vector<uint8_t> meta_buf;
    if (!m_http.get(meta_url, meta_buf)) {
        m_status = "Metadata error: " + m_http.last_error();
        return false;
    }

    json meta = json::parse(meta_buf, nullptr, /*exceptions=*/false);
    if (meta.is_discarded() || !meta.contains("timestamps_int")) {
        m_status = "Bad metadata JSON";
        return false;
    }

    int64_t ts = meta["timestamps_int"][0].get<int64_t>();
    std::string ts_str = std::to_string(ts); // 14-char YYYYMMDDHHMMSS

    std::string yyyy = ts_str.substr(0, 4);
    std::string mm   = ts_str.substr(4, 2);
    std::string dd   = ts_str.substr(6, 2);
    std::string hh   = ts_str.substr(8, 2);
    std::string mn   = ts_str.substr(10, 2);
    std::string ss   = ts_str.substr(12, 2);

    // ── Step 2: download zoom-0 tile ──────────────────────────────────────
    // URL pattern (confirmed working):
    //   /data/imagery/YYYY/MM/DD/{sat}---{sector}/{product}/{ts14}/00/000_000.png
    std::string tile_url = std::format(
        "{}/imagery/{}/{}/{}/{}---{}/{}/{}/00/000_000.png",
        BASE_DATA, yyyy, mm, dd, SAT, SECTOR, PRODUCT, ts_str);

    std::cout << "[App] GET " << tile_url << "\n";
    std::vector<uint8_t> tile_buf;
    if (!m_http.get(tile_url, tile_buf)) {
        m_status = "Tile error: " + m_http.last_error();
        return false;
    }

    // ── Step 3: decode PNG ───────────────────────────────────────────────
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels = stbi_load_from_memory(
        tile_buf.data(), static_cast<int>(tile_buf.size()),
        &w, &h, &ch, 4 /*force RGBA*/);

    if (!pixels) {
        m_status = std::string("PNG decode failed: ") + stbi_failure_reason();
        return false;
    }

    // ── Step 4: upload to GPU ────────────────────────────────────────────
    if (m_tile_tex) m_renderer.free_texture(m_tile_tex);
    m_tile_tex = m_renderer.upload_texture(pixels, w, h);
    stbi_image_free(pixels);

    m_status = std::format("GOES-19  GeoColor  Full Disk  |  {}-{}-{} {}:{}:{} UTC  |  {}×{}px",
        yyyy, mm, dd, hh, mn, ss, w, h);

    std::cout << "[App] Tile loaded " << w << "×" << h << " (" << tile_buf.size()/1024 << " KB)\n";
    return true;
}

// ── GLFW callbacks ─────────────────────────────────────────────────────────

void App::cb_resize(GLFWwindow* w, int width, int height) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    app->m_renderer.resize(width, height);
}

void App::cb_scroll(GLFWwindow* w, double /*dx*/, double dy) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureMouse) return;

    // Zoom toward the cursor position (world point under cursor stays fixed)
    double mx = 0, my = 0;
    glfwGetCursorPos(w, &mx, &my);
    glm::vec2 world_before = app->m_renderer.screen_to_world({float(mx), float(my)});

    float factor  = (dy > 0) ? 1.15f : (1.0f / 1.15f);
    float new_zoom = std::clamp(app->m_renderer.zoom() * factor, 0.5f, 128.0f);
    app->m_renderer.set_zoom(new_zoom);

    // Correct pan so the cursor-world point doesn't drift
    glm::vec2 world_after = app->m_renderer.screen_to_world({float(mx), float(my)});
    app->m_renderer.set_pan(app->m_renderer.pan() + (world_before - world_after));
}

void App::cb_mouse_button(GLFWwindow* w, int btn, int action, int /*mods*/) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (btn == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            double x = 0, y = 0;
            glfwGetCursorPos(w, &x, &y);
            app->m_dragging          = true;
            app->m_drag_start_screen = {float(x), float(y)};
            app->m_drag_start_pan    = app->m_renderer.pan();
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

    glm::vec2 delta   = glm::vec2{float(x), float(y)} - app->m_drag_start_screen;
    float     half_h  = 0.5f / app->m_renderer.zoom();
    float     half_w  = half_h * (float(win_w) / float(win_h));

    // Pixel delta → world delta (Y inverted: screen-down = world-down)
    glm::vec2 world_delta{
        -delta.x / float(win_w) * 2.0f * half_w,
         delta.y / float(win_h) * 2.0f * half_h,
    };

    app->m_renderer.set_pan(app->m_drag_start_pan + world_delta);
}
