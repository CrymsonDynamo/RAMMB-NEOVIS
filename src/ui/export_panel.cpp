#include "export_panel.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <format>
#include <filesystem>

namespace fs = std::filesystem;

// Opens a native file/folder picker via zenity (Linux). Returns selected path or "".
static std::string run_file_picker(bool directory_only, const char* title) {
    std::string cmd = directory_only
        ? std::format("zenity --file-selection --directory --title=\"{}\" 2>/dev/null", title)
        : std::format("zenity --file-selection --save --confirm-overwrite --title=\"{}\" 2>/dev/null", title);
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    char buf[1024] = {};
    bool got = (fgets(buf, sizeof(buf), f) != nullptr);
    pclose(f);
    if (!got) return {};
    std::string result(buf);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

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

    // Start drag on click — only if ImGui isn't consuming the event (e.g. panel buttons)
    if (!state.dragging_body && !state.dragging_corner) {
        if (io.MouseClicked[0] && in_vp(mp) && !io.WantCaptureMouse) {
            for (int ci = 0; ci < 4; ++ci) {
                float dx = mp.x - corners[ci].x;
                float dy = mp.y - corners[ci].y;
                if (dx*dx + dy*dy <= (HANDLE_R+4)*(HANDLE_R+4)) {
                    state.dragging_corner = true;
                    state.drag_corner_idx = ci;
                    break;
                }
            }
            if (!state.dragging_corner &&
                mp.x >= sx0 && mp.x <= sx1 && mp.y >= sy0 && mp.y <= sy1) {
                state.dragging_body = true;
            }
        }
    }

    // Apply incremental delta — never snaps, immune to click-offset issues
    if (io.MouseDown[0] && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        glm::vec2 prev_s = { mp.x - io.MouseDelta.x, mp.y - io.MouseDelta.y };
        glm::vec2 curr_w = s2w(mp);
        glm::vec2 prev_w = s2w(prev_s);
        float dx = curr_w.x - prev_w.x;
        float dy = curr_w.y - prev_w.y;

        if (state.dragging_corner) {
            int ci = state.drag_corner_idx;
            if (ci == 0) { state.crop.x_min += dx; state.crop.y_max += dy; } // TL
            if (ci == 1) { state.crop.x_max += dx; state.crop.y_max += dy; } // TR
            if (ci == 2) { state.crop.x_min += dx; state.crop.y_min += dy; } // BL
            if (ci == 3) { state.crop.x_max += dx; state.crop.y_min += dy; } // BR
            // Clamp: don't let opposite corners cross
            const float MIN = 0.01f;
            if (state.crop.x_max - state.crop.x_min < MIN) {
                if (ci == 0 || ci == 2) state.crop.x_min = state.crop.x_max - MIN;
                else                    state.crop.x_max = state.crop.x_min + MIN;
            }
            if (state.crop.y_max - state.crop.y_min < MIN) {
                if (ci == 0 || ci == 1) state.crop.y_max = state.crop.y_min + MIN;
                else                    state.crop.y_min = state.crop.y_max - MIN;
            }
        } else if (state.dragging_body) {
            state.crop.x_min += dx; state.crop.x_max += dx;
            state.crop.y_min += dy; state.crop.y_max += dy;
        }
    }

    if (!io.MouseDown[0]) {
        state.dragging_corner = false;
        state.dragging_body   = false;
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
    {
        bool is_dir = state.settings.format == ExportFormat::PNG_SEQUENCE;
        ImGui::SetNextItemWidth(PANEL_W - 80.0f);
        ImGui::InputText("##path", state.path_buf, sizeof(state.path_buf));
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Browse...", { 64.0f, 0 })) {
            const char* title = is_dir ? "Select Output Folder" : "Save Output File";
            std::string picked = run_file_picker(is_dir, title);
            if (!picked.empty())
                strncpy(state.path_buf, picked.c_str(), sizeof(state.path_buf) - 1);
        }
        ImGui::TextColored({ 0.55f,0.55f,0.55f,1.0f },
            is_dir ? "  Folder path (created if needed)"
                   : "  File path  e.g. /home/user/out.mp4");
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Resolution ────────────────────────────────────────────────────────────
    // Output dimensions always match the crop aspect ratio.
    // Presets set the long-edge pixel count; Custom lets you override width.
    ImGui::TextColored(COL_ACCENT, "RESOLUTION");
    ImGui::Spacing();

    // Compute crop aspect (w/h). Guard against degenerate crops.
    float crop_w = state.crop.width();
    float crop_h = state.crop.height();
    float crop_ar = (crop_h > 0.001f) ? (crop_w / crop_h) : 1.0f;

    // Long-edge pixel counts for Low / Medium / High
    const char* preset_names[]  = { "Low", "Medium", "High", "Custom" };
    const int   preset_long[]   = { 720, 1280, 1920, 0 };   // 0 = custom
    float bw = (PANEL_W - 20.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        bool sel = (state.res_preset == i);
        if (sel) { ImGui::PushStyleColor(ImGuiCol_Button,        { 0.15f,0.45f,0.80f,1.0f });
                   ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.15f,0.45f,0.80f,1.0f }); }
        if (ImGui::Button(preset_names[i], { bw, 0 }))
            state.res_preset = i;
        if (sel) ImGui::PopStyleColor(2);
        if (i < 3) ImGui::SameLine(0, 4);
    }
    ImGui::Spacing();

    if (state.res_preset < 3) {
        // Derive W and H from long edge + crop AR
        int long_edge = preset_long[state.res_preset];
        if (crop_ar >= 1.0f) {
            state.settings.out_width  = long_edge;
            state.settings.out_height = std::max(2, int(float(long_edge) / crop_ar));
        } else {
            state.settings.out_height = long_edge;
            state.settings.out_width  = std::max(2, int(float(long_edge) * crop_ar));
        }
        // H.264 requires even dimensions
        state.settings.out_width  &= ~1;
        state.settings.out_height &= ~1;
        ImGui::TextColored({ 0.55f,0.55f,0.55f,1.0f }, "  %d x %d px  (%.3f:1)",
            state.settings.out_width, state.settings.out_height, crop_ar);
    } else {
        // Custom: user sets width, height is auto-derived from crop AR
        ImGui::Text("  Width:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputInt("##ow", &state.settings.out_width, 0);
        state.settings.out_width = (std::clamp(state.settings.out_width, 64, 7680) & ~1);
        state.settings.out_height = std::max(2, int(float(state.settings.out_width) / crop_ar)) & ~1;
        ImGui::TextColored({ 0.55f,0.55f,0.55f,1.0f }, "  → %d x %d px  (%.3f:1)",
            state.settings.out_width, state.settings.out_height, crop_ar);
    }

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
        // Determine required extension and whether the path already has it
        const char* req_ext = nullptr;
        bool is_seq = state.settings.format == ExportFormat::PNG_SEQUENCE;
        if (!is_seq) {
            req_ext = (state.settings.format == ExportFormat::MP4) ? ".mp4" : ".gif";
        }

        bool path_ok = strlen(state.path_buf) > 0;
        // Warn if file path is missing the right extension (not needed for PNG seq dirs)
        bool ext_ok = true;
        if (path_ok && req_ext) {
            std::string p(state.path_buf);
            std::string ext(req_ext);
            ext_ok = p.size() >= ext.size() &&
                     p.compare(p.size() - ext.size(), ext.size(), ext) == 0;
        }

        bool ok = path_ok && state.crop.valid() && total_frames > 0;
        if (!ok) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_Button,        { 0.15f,0.50f,0.15f,1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.20f,0.70f,0.20f,1.0f });
        if (ImGui::Button("Export", { PANEL_W - 16.0f, 28.0f })) {
            // Auto-append extension if missing so FFmpeg doesn't guess wrong format
            std::string fixed_path(state.path_buf);
            if (req_ext && !ext_ok) {
                fixed_path += req_ext;
                strncpy(state.path_buf, fixed_path.c_str(), sizeof(state.path_buf) - 1);
            }
            state.settings.output_path = fixed_path;
            state.settings.crop        = state.crop;
            export_triggered = true;
        }
        ImGui::PopStyleColor(2);
        if (!ok) ImGui::EndDisabled();

        if (!ok) {
            if (!path_ok)
                ImGui::TextColored(COL_WARN, "  Set an output path above");
            else if (!state.crop.valid())
                ImGui::TextColored(COL_WARN, "  Crop region is invalid");
            else if (total_frames == 0)
                ImGui::TextColored(COL_WARN, "  No frames loaded");
        } else if (!ext_ok && req_ext) {
            ImGui::TextColored(COL_WARN, "  Extension will be added: %s", req_ext);
        }
    }

    ImGui::End();

    if (!keep_open) state.open = false;
    return export_triggered;
}
