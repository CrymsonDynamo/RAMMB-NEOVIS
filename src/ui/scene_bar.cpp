#include "scene_bar.hpp"
#include <imgui.h>
#include <algorithm>
#include <format>
#include <cmath>
#include <cstring>

static constexpr float BAR_H    = 30.0f;
static constexpr float PANEL_W  = 320.0f;

static const ImVec4 COL_ACCENT   = { 0.20f, 0.60f, 1.00f, 1.0f };
static const ImVec4 COL_DIM      = { 0.55f, 0.55f, 0.55f, 1.0f };
static const ImVec4 COL_WARN     = { 1.00f, 0.70f, 0.10f, 1.0f };

// Ensure at least one scene always exists.
static void ensure_one(SceneBar& bar) {
    if (bar.scenes.empty()) {
        bar.scenes.push_back({ "Scene 1", {} });
        bar.active = 0;
    }
    bar.active = std::clamp(bar.active, 0, int(bar.scenes.size()) - 1);
}

float scene_bar_draw(SceneBar& bar, ViewState& active_state, float win_w, float /*win_h*/) {
    ensure_one(bar);

    // Before drawing, save current active_state into the active scene slot
    // so switching tabs picks up live changes.
    bar.scenes[bar.active].state = active_state;

    ImGui::SetNextWindowPos({ 0.0f, 0.0f });
    ImGui::SetNextWindowSize({ win_w, BAR_H });
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,    { 0.07f, 0.07f, 0.10f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_Button,      { 0.12f, 0.12f, 0.17f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{ 0.20f,0.48f,0.85f,1.0f });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 4.0f, 3.0f });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   { 3.0f, 2.0f });
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  { 6.0f, 3.0f });

    ImGui::Begin("##scenebar", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove     |
        ImGuiWindowFlags_NoResize     | ImGuiWindowFlags_NoNav      |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ── Scene tabs ────────────────────────────────────────────────────────────
    int del_idx = -1;
    static int  s_rename_idx = -1;          // which tab is being renamed (-1 = none)
    static char s_rename_buf[64] = {};
    static bool s_rename_focus = false;     // true on the first frame the InputText opens

    for (int i = 0; i < int(bar.scenes.size()); ++i) {
        bool is_active = (i == bar.active);
        bool is_renaming = (s_rename_idx == i);

        ImGui::PushID(i);

        if (is_renaming) {
            // Inline rename input
            if (s_rename_focus) {
                ImGui::SetKeyboardFocusHere();
                s_rename_focus = false;
            }
            ImGui::SetNextItemWidth(std::max(60.0f,
                ImGui::CalcTextSize(s_rename_buf).x + 20.0f));
            if (ImGui::InputText("##rename", s_rename_buf, sizeof(s_rename_buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_AutoSelectAll)) {
                // Enter confirms
                if (s_rename_buf[0] != '\0')
                    bar.scenes[i].name = s_rename_buf;
                s_rename_idx = -1;
            }
            if (!ImGui::IsItemActive() && !ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(0)) {
                // Click outside cancels
                if (s_rename_buf[0] != '\0')
                    bar.scenes[i].name = s_rename_buf;
                s_rename_idx = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) s_rename_idx = -1;
        } else {
            if (is_active) {
                ImGui::PushStyleColor(ImGuiCol_Button,        { 0.15f,0.45f,0.80f,1.0f });
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.20f,0.60f,1.00f,1.0f });
            }

            if (ImGui::Button(bar.scenes[i].name.c_str(), { 0, BAR_H - 8.0f })) {
                if (i != bar.active) {
                    // Switching to a different tab — only reload if the source
                    // params actually differ from what's currently loaded.
                    const ViewState& nxt = bar.scenes[i].state;
                    bool src_diff =
                        nxt.satellite       != active_state.satellite ||
                        nxt.sector          != active_state.sector    ||
                        nxt.product         != active_state.product   ||
                        nxt.data_zoom       != active_state.data_zoom ||
                        nxt.num_frames      != active_state.num_frames||
                        nxt.time_step       != active_state.time_step ||
                        nxt.date_range.use_range  != active_state.date_range.use_range ||
                        nxt.date_range.start_year != active_state.date_range.start_year||
                        nxt.date_range.start_month!= active_state.date_range.start_month||
                        nxt.date_range.start_day  != active_state.date_range.start_day ||
                        nxt.date_range.end_year   != active_state.date_range.end_year  ||
                        nxt.date_range.end_month  != active_state.date_range.end_month ||
                        nxt.date_range.end_day    != active_state.date_range.end_day;
                    bar.active   = i;
                    active_state = bar.scenes[i].state;
                    active_state.source_changed = src_diff;
                }
                // Clicking the already-active tab does nothing.
            }

            // Double-click on the active tab → rename only, never reload.
            // Because clicking an already-active tab does nothing above,
            // source_changed is guaranteed false at this point.
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                s_rename_idx   = bar.active;  // always rename the active tab
                strncpy(s_rename_buf, bar.scenes[bar.active].name.c_str(),
                        sizeof(s_rename_buf) - 1);
                s_rename_buf[sizeof(s_rename_buf) - 1] = '\0';
                s_rename_focus = true;
            }

            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click to rename");

            if (is_active) ImGui::PopStyleColor(2);
        }

        // Close (×) button — only show if more than one scene
        if (bar.scenes.size() > 1) {
            ImGui::SameLine(0, 1);
            ImGui::PushStyleColor(ImGuiCol_Button,        { 0.0f,0.0f,0.0f,0.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.7f,0.2f,0.2f,1.0f });
            if (ImGui::SmallButton("x"))
                del_idx = i;
            ImGui::PopStyleColor(2);
        }

        ImGui::PopID();
        ImGui::SameLine(0, 4);
    }

    // ── + New Scene button ────────────────────────────────────────────────────
    if (ImGui::Button("+", { BAR_H - 8.0f, BAR_H - 8.0f })) {
        Scene s;
        s.name  = std::format("Scene {}", int(bar.scenes.size()) + 1);
        s.state = active_state;  // copy current state as starting point
        bar.scenes.push_back(std::move(s));
        bar.active   = int(bar.scenes.size()) - 1;
        active_state = bar.scenes[bar.active].state;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New scene");

    // ── Settings gear (anchored to far right) ─────────────────────────────────
    float gear_w = 28.0f;
    ImGui::SameLine(win_w - gear_w - 6.0f);
    bool gear_active = bar.settings_open;
    if (gear_active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        { 0.15f,0.45f,0.80f,1.0f });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.20f,0.60f,1.00f,1.0f });
    }
    if (ImGui::Button("##gear", { gear_w, BAR_H - 8.0f })) bar.settings_open = !bar.settings_open;
    if (gear_active) ImGui::PopStyleColor(2);
    // Draw a simple gear icon manually
    {
        ImVec2 p = ImGui::GetItemRectMin();
        ImVec2 sz = ImGui::GetItemRectSize();
        ImVec2 c  = { p.x + sz.x * 0.5f, p.y + sz.y * 0.5f };
        float  r  = std::min(sz.x, sz.y) * 0.35f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 col = IM_COL32(200, 200, 200, 220);
        dl->AddCircle(c, r, col, 12, 1.5f);
        dl->AddCircleFilled(c, r * 0.45f, col);
        // Teeth
        for (int t = 0; t < 8; ++t) {
            float a  = float(t) / 8.0f * 6.2832f;
            float ca = std::cos(a), sa = std::sin(a);
            ImVec2 p0 = { c.x + ca * r * 0.85f, c.y + sa * r * 0.85f };
            ImVec2 p1 = { c.x + ca * r * 1.35f, c.y + sa * r * 1.35f };
            dl->AddLine(p0, p1, col, 2.0f);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings");

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    // ── Delete scene (deferred to avoid mutating during iteration) ────────────
    if (del_idx >= 0) {
        bar.scenes.erase(bar.scenes.begin() + del_idx);
        if (bar.active >= int(bar.scenes.size())) bar.active = int(bar.scenes.size()) - 1;
        active_state = bar.scenes[bar.active].state;
        active_state.source_changed = true;
    }

    // ── Settings panel ────────────────────────────────────────────────────────
    if (bar.settings_open) {
        float panel_x = win_w - PANEL_W - 8.0f;
        float panel_y = BAR_H + 4.0f;
        ImGui::SetNextWindowPos({ panel_x, panel_y });
        ImGui::SetNextWindowSize({ PANEL_W, 0 });
        ImGui::SetNextWindowBgAlpha(0.97f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, { 0.08f, 0.08f, 0.12f, 1.0f });

        bool keep = true;
        ImGui::Begin("Settings", &keep,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::TextColored(COL_ACCENT, "DISPLAY");
        ImGui::Spacing();
        ImGui::Checkbox("VSync", &bar.vsync);
        ImGui::Checkbox("Dark UI", &bar.dark_ui);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "CACHE");
        ImGui::Spacing();
        ImGui::Text("Memory limit:");
        ImGui::SetNextItemWidth(PANEL_W - 16.0f);
        ImGui::SliderInt("##cache", &bar.cache_limit_mb, 64, 4096, "%d MB");
        ImGui::TextColored(COL_DIM, "  Tile textures kept in RAM. "
                                    "Older tiles evicted first.");

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "AUTO-RELOAD");
        ImGui::Spacing();
        ImGui::Checkbox("Reload on timer", &bar.auto_reload);
        if (bar.auto_reload) {
            ImGui::SetNextItemWidth(PANEL_W - 16.0f);
            ImGui::SliderInt("##ars", &bar.auto_reload_s, 30, 3600, "%d s");
            ImGui::TextColored(COL_DIM, "  Fetches new timestamps every %d s.",
                               bar.auto_reload_s);
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(COL_ACCENT, "DOWNLOADS");
        ImGui::Spacing();
        ImGui::Text("Speed limit:");
        bool unlimited = (bar.download_limit_kbps == 0);
        if (ImGui::Checkbox("Unlimited##dl", &unlimited))
            bar.download_limit_kbps = unlimited ? 0 : 5120;
        if (!unlimited) {
            ImGui::SetNextItemWidth(PANEL_W - 16.0f);
            ImGui::SliderInt("##throttle", &bar.download_limit_kbps,
                             128, 51200, "%d KB/s");
            ImGui::TextColored(COL_DIM, "  Presets:"); ImGui::SameLine();
            if (ImGui::SmallButton("1 MB/s"))  bar.download_limit_kbps = 1024;
            ImGui::SameLine();
            if (ImGui::SmallButton("5 MB/s"))  bar.download_limit_kbps = 5120;
            ImGui::SameLine();
            if (ImGui::SmallButton("20 MB/s")) bar.download_limit_kbps = 20480;
        }
        ImGui::Spacing();
        ImGui::Text("Concurrent downloads:");
        ImGui::SetNextItemWidth(PANEL_W - 16.0f);
        ImGui::SliderInt("##threads", &bar.download_threads, 1, 32, "%d threads");
        ImGui::TextColored(COL_DIM, "  (takes effect on next refresh)");

        ImGui::End();
        ImGui::PopStyleColor();

        if (!keep) bar.settings_open = false;
    }

    return BAR_H;
}
