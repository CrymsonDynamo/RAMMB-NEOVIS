#include "export_panel.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <format>
#include <filesystem>

namespace fs = std::filesystem;

static const ImVec4 COL_ACCENT = { 0.20f, 0.60f, 1.00f, 1.0f };
static const ImVec4 COL_WARN   = { 1.00f, 0.70f, 0.10f, 1.0f };
static const ImVec4 COL_OK     = { 0.20f, 0.85f, 0.40f, 1.0f };
static const ImVec4 COL_ERR    = { 1.00f, 0.30f, 0.20f, 1.0f };

static constexpr float HANDLE_R = 7.0f;
static constexpr float PANEL_W  = 340.0f;

// ── Crop overlay (drawn into the ImGui background drawlist) ───────────────────

static void draw_crop_overlay(ExportState&          state,
                               float vp_x, float vp_y, float vp_w, float vp_h,
                               const ScreenFromWorld& w2s,
                               const WorldFromScreen& s2w) {
    // Convert world-space crop to screen
    glm::vec2 tl_s = w2s({ state.crop.x_min, state.crop.y_max });
    glm::vec2 br_s = w2s({ state.crop.x_max, state.crop.y_min });

    // Clamp to viewport bounds for display
    float sx0 = std::max(vp_x, tl_s.x);
    float sy0 = std::max(vp_y, tl_s.y);
    float sx1 = std::min(vp_x + vp_w, br_s.x);
    float sy1 = std::min(vp_y + vp_h, br_s.y);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Darken everything outside the crop box
    ImU32 dim = IM_COL32(0, 0, 0, 140);
    dl->AddRectFilled({ vp_x,  vp_y  }, { vp_x + vp_w, sy0  }, dim); // top
    dl->AddRectFilled({ vp_x,  sy1   }, { vp_x + vp_w, vp_y + vp_h }, dim); // bottom
    dl->AddRectFilled({ vp_x,  sy0   }, { sx0,  sy1   }, dim); // left
    dl->AddRectFilled({ sx1,   sy0   }, { vp_x + vp_w, sy1  }, dim); // right

    // Crop border
    dl->AddRect({ sx0, sy0 }, { sx1, sy1 }, IM_COL32(51, 153, 255, 255), 0.0f, 0, 2.0f);

    // Rule-of-thirds grid lines
    for (int i = 1; i <= 2; ++i) {
        float gx = sx0 + (sx1 - sx0) * float(i) / 3.0f;
        float gy = sy0 + (sy1 - sy0) * float(i) / 3.0f;
        dl->AddLine({ gx, sy0 }, { gx, sy1 }, IM_COL32(255,255,255,40), 1.0f);
        dl->AddLine({ sx0, gy }, { sx1, gy }, IM_COL32(255,255,255,40), 1.0f);
    }

    // Corner handles (filled circles)
    glm::vec2 corners[4] = {
        { tl_s.x, tl_s.y }, { br_s.x, tl_s.y },
        { tl_s.x, br_s.y }, { br_s.x, br_s.y }
    };
    for (auto& c : corners) {
        dl->AddCircleFilled({ c.x, c.y }, HANDLE_R, IM_COL32(51,153,255,230));
        dl->AddCircle      ({ c.x, c.y }, HANDLE_R, IM_COL32(255,255,255,200), 0, 1.5f);
    }

    // Size label
    float crop_w_km = state.crop.width()  * 21504.0f; // rough: full disk ≈ 21504 km
    float crop_h_km = state.crop.height() * 21504.0f;
    std::string lbl = std::format("{:.0f} × {:.0f} km", crop_w_km, crop_h_km);
    dl->AddText({ sx0 + 4, sy0 + 4 }, IM_COL32(255,255,255,200), lbl.c_str());

    // ── Handle interaction ────────────────────────────────────────────────────
    ImGuiIO& io = ImGui::GetIO();
    glm::vec2 mp = { io.MousePos.x, io.MousePos.y };

    auto in_vp = [&](glm::vec2 p) {
        return p.x >= vp_x && p.x <= vp_x+vp_w && p.y >= vp_y && p.y <= vp_y+vp_h;
    };

    if (!state.dragging_body && !state.dragging_corner) {
        if (io.MouseClicked[0] && in_vp(mp)) {
            // Check corner handles first
            for (int ci = 0; ci < 4; ++ci) {
                float dx = mp.x - corners[ci].x;
                float dy = mp.y - corners[ci].y;
                if (dx*dx + dy*dy <= (HANDLE_R+4)*(HANDLE_R+4)) {
                    state.dragging_corner  = true;
                    state.drag_corner_idx  = ci;
                    break;
                }
            }
            // Then body drag
            if (!state.dragging_corner &&
                mp.x >= sx0 && mp.x <= sx1 && mp.y >= sy0 && mp.y <= sy1) {
                state.dragging_body = true;
                glm::vec2 wmp = s2w(mp);
                state.drag_ox = wmp.x - state.crop.x_min;
                state.drag_oy = wmp.y - state.crop.y_min;
            }
        }
    }

    if (state.dragging_corner && io.MouseDown[0]) {
        glm::vec2 wmp = s2w(mp);
        int ci = state.drag_corner_idx;
        // TL=0 TR=1 BL=2 BR=3
        if (ci == 0) { state.crop.x_min = wmp.x; state.crop.y_max = wmp.y; }
        if (ci == 1) { state.crop.x_max = wmp.x; state.crop.y_max = wmp.y; }
        if (ci == 2) { state.crop.x_min = wmp.x; state.crop.y_min = wmp.y; }
        if (ci == 3) { state.crop.x_max = wmp.x; state.crop.y_min = wmp.y; }
        // Keep min < max
        if (state.crop.x_min > state.crop.x_max) std::swap(state.crop.x_min, state.crop.x_max);
        if (state.crop.y_min > state.crop.y_max) std::swap(state.crop.y_min, state.crop.y_max);
    } else {
        state.dragging_corner = false;
    }

    if (state.dragging_body && io.MouseDown[0]) {
        glm::vec2 wmp = s2w(mp);
        float cw = state.crop.width(), ch = state.crop.height();
        state.crop.x_min = wmp.x - state.drag_ox;
        state.crop.y_min = wmp.y - state.drag_oy;
        state.crop.x_max = state.crop.x_min + cw;
        state.crop.y_max = state.crop.y_min + ch;
    } else {
        state.dragging_body = false;
    }
}

// ── Export panel window ───────────────────────────────────────────────────────

bool export_panel_draw(ExportState&          state,
                       float vp_x, float vp_y, float vp_w, float vp_h,
                       int   total_frames,
                       const WorldFromScreen& s2w,
                       const ScreenFromWorld& w2s) {
    if (!state.open) return false;

    // Always draw crop overlay when panel is open
    draw_crop_overlay(state, vp_x, vp_y, vp_w, vp_h, w2s, s2w);

    // Position panel: right side of viewport
    float panel_x = vp_x + vp_w - PANEL_W - 16.0f;
    float panel_y = vp_y + 16.0f;
    ImGui::SetNextWindowPos({ panel_x, panel_y }, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ PANEL_W, 0 });
    ImGui::SetNextWindowBgAlpha(0.95f);

    bool keep_open = true;
    ImGui::Begin("Export", &keep_open,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize);

    bool export_triggered = false;
    bool is_running = state.progress->running.load();

    // ── Format ───────────────────────────────────────────────────────────────
    ImGui::TextColored(COL_ACCENT, "FORMAT");
    ImGui::Spacing();

    const char* fmt_names[] = { "MP4 (H.264)", "GIF", "PNG Sequence" };
    int fmt = int(state.settings.format);
    for (int i = 0; i < 3; ++i) {
        bool sel = (fmt == i);
        if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, { 0.15f,0.45f,0.80f,1.0f });
                   ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.15f,0.45f,0.80f,1.0f }); }
        if (ImGui::Button(fmt_names[i], { (PANEL_W - 20.0f) / 3.0f, 0 }))
            state.settings.format = ExportFormat(i);
        if (sel) ImGui::PopStyleColor(2);
        if (i < 2) ImGui::SameLine(0, 4);
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Output path ───────────────────────────────────────────────────────────
    ImGui::TextColored(COL_ACCENT, "OUTPUT");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(PANEL_W - 16.0f);
    ImGui::InputText("##path", state.path_buf, sizeof(state.path_buf));
    ImGui::TextColored({ 0.55f,0.55f,0.55f,1.0f },
        state.settings.format == ExportFormat::PNG_SEQUENCE
            ? "  Folder path (created if needed)"
            : "  File path  e.g. /home/user/out.mp4");

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Resolution ────────────────────────────────────────────────────────────
    ImGui::TextColored(COL_ACCENT, "RESOLUTION");
    ImGui::Spacing();

    const char* res_presets[] = { "720p", "1080p", "1440p", "4K" };
    const int   res_w[]       = {  1280,   1920,    2560,   3840 };
    const int   res_h[]       = {   720,   1080,    1440,   2160 };
    for (int i = 0; i < 4; ++i) {
        bool sel = (state.settings.out_width == res_w[i] && state.settings.out_height == res_h[i]);
        if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, { 0.15f,0.45f,0.80f,1.0f });
                   ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.15f,0.45f,0.80f,1.0f }); }
        if (ImGui::Button(res_presets[i], { (PANEL_W - 20.0f) / 4.0f, 0 })) {
            state.settings.out_width  = res_w[i];
            state.settings.out_height = res_h[i];
        }
        if (sel) ImGui::PopStyleColor(2);
        if (i < 3) ImGui::SameLine(0, 4);
    }
    ImGui::Spacing();
    ImGui::Text("  W:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(65.0f);
    ImGui::InputInt("##ow", &state.settings.out_width, 0);
    ImGui::SameLine();
    ImGui::Text("H:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(65.0f);
    ImGui::InputInt("##oh", &state.settings.out_height, 0);
    state.settings.out_width  = std::clamp(state.settings.out_width,  64, 7680);
    state.settings.out_height = std::clamp(state.settings.out_height, 64, 4320);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Crop ─────────────────────────────────────────────────────────────────
    ImGui::TextColored(COL_ACCENT, "CROP REGION");
    ImGui::Spacing();
    ImGui::TextColored({ 0.55f,0.55f,0.55f,1.0f },
        "  Drag corners/body in viewport.");
    ImGui::Spacing();

    // Reset crop to full disk
    if (ImGui::Button("Full Image", { (PANEL_W-20.0f)*0.5f, 0 })) {
        state.crop = { -0.5f, 0.5f, -0.5f, 0.5f };
    }
    ImGui::SameLine(0, 4);
    // Set crop to current viewport
    if (ImGui::Button("Use Viewport", { (PANEL_W-20.0f)*0.5f, 0 })) {
        glm::vec2 tl = s2w({ vp_x,        vp_y });
        glm::vec2 br = s2w({ vp_x+vp_w,   vp_y+vp_h });
        state.crop.x_min = tl.x; state.crop.x_max = br.x;
        state.crop.y_max = tl.y; state.crop.y_min = br.y;
    }

    // Show numeric coords
    ImGui::Spacing();
    ImGui::TextColored({ 0.55f,0.55f,0.55f,1.0f },
        "  X: [%.3f, %.3f]  Y: [%.3f, %.3f]",
        state.crop.x_min, state.crop.x_max,
        state.crop.y_min, state.crop.y_max);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── FPS / Quality ────────────────────────────────────────────────────────
    if (state.settings.format != ExportFormat::PNG_SEQUENCE) {
        ImGui::TextColored(COL_ACCENT, "PLAYBACK");
        ImGui::Spacing();
        ImGui::Text("FPS:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(PANEL_W - 70.0f);
        ImGui::SliderFloat("##efps", &state.settings.fps, 1.0f, 30.0f, "%.0f fps");

        if (state.settings.format == ExportFormat::MP4) {
            ImGui::Text("Quality (CRF):"); ImGui::SameLine();
            ImGui::SetNextItemWidth(PANEL_W - 130.0f);
            ImGui::SliderInt("##crf", &state.settings.crf, 0, 51, "%d");
            ImGui::TextColored({ 0.55f,0.55f,0.55f,1.0f }, "  0=lossless  18=great  28=small");

            bool& nvenc = state.settings.use_nvenc;
            ImGui::Checkbox("Use NVENC (RTX 5070 Ti)", &nvenc);
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    }

    // ── Frames summary ────────────────────────────────────────────────────────
    ImGui::TextColored(COL_ACCENT, "FRAMES");
    ImGui::Spacing();
    ImGui::Text("  %d frames will be exported", total_frames);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Progress ──────────────────────────────────────────────────────────────
    if (is_running) {
        int done  = state.progress->frames_done.load();
        int total = state.progress->frames_total.load();
        float frac = total > 0 ? std::clamp(float(done)/float(total), 0.0f, 1.0f) : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.20f,0.60f,1.00f,1.0f });
        ImGui::ProgressBar(frac, { PANEL_W - 16.0f, 12.0f }, "");
        ImGui::PopStyleColor();
        ImGui::TextColored(COL_ACCENT, "  Exporting %d / %d frames...", done, total);
        if (ImGui::Button("Cancel", { PANEL_W - 16.0f, 0 }))
            state.progress->failed.store(true); // signal cancel to exporter
    } else if (state.progress->finished.load()) {
        if (state.progress->failed.load()) {
            ImGui::TextColored(COL_ERR, "  Failed: %s", state.progress->error_msg.c_str());
        } else {
            ImGui::TextColored(COL_OK, "  Done: %s", state.progress->output_path.c_str());
        }
        ImGui::Spacing();
    }

    // ── Export button ─────────────────────────────────────────────────────────
    if (!is_running) {
        ImGui::PushStyleColor(ImGuiCol_Button,        { 0.15f,0.50f,0.15f,1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.20f,0.70f,0.20f,1.0f });
        bool ok = strlen(state.path_buf) > 0 && state.crop.valid() && total_frames > 0;
        if (!ok) ImGui::BeginDisabled();
        if (ImGui::Button("Export", { PANEL_W - 16.0f, 28.0f })) {
            state.settings.output_path = state.path_buf;
            state.settings.crop        = state.crop;
            export_triggered = true;
        }
        if (!ok) ImGui::EndDisabled();
        ImGui::PopStyleColor(2);

        if (!ok) {
            if (strlen(state.path_buf) == 0)
                ImGui::TextColored(COL_WARN, "  Set an output path above");
            else if (!state.crop.valid())
                ImGui::TextColored(COL_WARN, "  Crop region is invalid");
            else if (total_frames == 0)
                ImGui::TextColored(COL_WARN, "  No frames loaded");
        }
    }

    ImGui::End();

    if (!keep_open) state.open = false;
    return export_triggered;
}
