#include "tools_panel.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <format>

static constexpr float PANEL_W = 220.0f;

static const ImVec4 COL_ACCENT = { 0.20f, 0.60f, 1.00f, 1.0f };
static const ImVec4 COL_DIM    = { 0.55f, 0.55f, 0.55f, 1.0f };

void tools_panel_draw(ViewState& state,
                      float svp_x, float svp_y, float svp_w, float svp_h,
                      const ScreenFromWorld& w2s) {

    int T = 1 << state.data_zoom;   // T×T tile grid

    // Keep selection vector in sync with current zoom
    if (int(state.tile_selection.size()) != T * T) {
        state.tile_selection.assign(T * T, true);
        state.tile_select_all = true;
    }

    // ── Toggle tab on the right edge ─────────────────────────────────────────
    {
        float tab_w = 24.0f;
        float tab_h = 80.0f;
        float tab_x = svp_x + svp_w - tab_w;
        float tab_y = svp_y + svp_h * 0.5f - tab_h * 0.5f;

        ImGui::SetNextWindowPos({ tab_x, tab_y });
        ImGui::SetNextWindowSize({ tab_w, tab_h });
        ImGui::SetNextWindowBgAlpha(0.75f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 2.0f, 2.0f });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::Begin("##tools_tab", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize     | ImGuiWindowFlags_NoNav  |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (ImGui::Button(state.tools_panel_open ? ">" : "<", { 20.0f, 76.0f }))
            state.tools_panel_open = !state.tools_panel_open;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Tile region selector");

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    if (!state.tools_panel_open) return;

    // ── Tile grid overlay drawn on the viewport ───────────────────────────────
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImGuiIO& io    = ImGui::GetIO();

    static bool s_drag_active = false;
    static bool s_drag_value  = true;

    // Track which tile the mouse is hovering over (for highlight)
    int hov_r = -1, hov_c = -1;

    for (int r = 0; r < T; ++r) {
        for (int c = 0; c < T; ++c) {
            // Tile world-space corners
            float wx0 = float(c)     / float(T) - 0.5f;
            float wx1 = float(c + 1) / float(T) - 0.5f;
            float wy1 = 0.5f - float(r)     / float(T);   // top (higher y)
            float wy0 = 0.5f - float(r + 1) / float(T);   // bottom

            glm::vec2 tl = w2s({ wx0, wy1 });
            glm::vec2 br = w2s({ wx1, wy0 });

            // Skip tiles entirely off-screen
            if (br.x < svp_x || tl.x > svp_x + svp_w ||
                br.y < svp_y || tl.y > svp_y + svp_h)
                continue;

            int  idx = r * T + c;
            bool sel = state.tile_selection[idx];

            // Check hover
            bool hovered = io.MousePos.x >= tl.x && io.MousePos.x < br.x &&
                           io.MousePos.y >= tl.y && io.MousePos.y < br.y &&
                           io.MousePos.x >= svp_x && io.MousePos.x < svp_x + svp_w &&
                           io.MousePos.y >= svp_y && io.MousePos.y < svp_y + svp_h;
            if (hovered) { hov_r = r; hov_c = c; }

            // Fill: selected = faint blue tint, deselected = red-dark overlay
            ImU32 fill;
            if (hovered)
                fill = sel ? IM_COL32(51, 153, 255, 60) : IM_COL32(220, 60, 60, 90);
            else
                fill = sel ? IM_COL32(51, 153, 255, 25) : IM_COL32(0, 0, 0, 120);

            dl->AddRectFilled({ tl.x, tl.y }, { br.x, br.y }, fill);

            // Border
            ImU32 border = sel ? IM_COL32(51, 153, 255, 100) : IM_COL32(180, 60, 60, 80);
            dl->AddRect({ tl.x, tl.y }, { br.x, br.y }, border, 0.0f, 0, 1.0f);

            // Row,col label when tiles are large enough to read
            float cell_px = br.x - tl.x;
            if (cell_px >= 40.0f) {
                char lbl[16];
                snprintf(lbl, sizeof(lbl), "%d,%d", r, c);
                dl->AddText({ tl.x + 4, tl.y + 4 },
                            sel ? IM_COL32(180, 220, 255, 200) : IM_COL32(200, 100, 100, 200),
                            lbl);
            }
        }
    }

    // ── Click / drag interaction on the overlay ───────────────────────────────
    // Only respond when ImGui isn't consuming the mouse (e.g. panel buttons)
    if (hov_r >= 0 && !io.WantCaptureMouse) {
        int idx = hov_r * T + hov_c;
        if (io.MouseClicked[0]) {
            s_drag_active = true;
            s_drag_value  = !state.tile_selection[idx];
            state.tile_selection[idx] = s_drag_value;
        } else if (s_drag_active && io.MouseDown[0]) {
            state.tile_selection[idx] = s_drag_value;
        }
    }
    if (!io.MouseDown[0]) s_drag_active = false;

    // Update the "all selected" shortcut flag
    state.tile_select_all = true;
    for (int i = 0; i < T * T; ++i)
        if (!state.tile_selection[i]) { state.tile_select_all = false; break; }

    // ── Side panel (controls + stats) ────────────────────────────────────────
    float panel_x = svp_x + svp_w - PANEL_W - 28.0f;
    float panel_y = svp_y + 10.0f;

    ImGui::SetNextWindowPos({ panel_x, panel_y });
    ImGui::SetNextWindowSize({ PANEL_W, 0 });
    ImGui::SetNextWindowBgAlpha(0.90f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, { 0.08f, 0.08f, 0.11f, 1.0f });
    ImGui::Begin("Tile Selector", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(COL_ACCENT, "TILE REGION");
    ImGui::Spacing();
    ImGui::TextColored(COL_DIM, "  %d x %d grid  (%d total)", T, T, T * T);
    ImGui::Spacing();

    // Count selected
    int sel_count = 0;
    for (int i = 0; i < T * T; ++i)
        if (state.tile_selection[i]) ++sel_count;

    // Colour the count: green if all, yellow if partial, red if none
    ImVec4 cnt_col = (sel_count == T*T) ? ImVec4{0.2f,0.85f,0.4f,1}
                   : (sel_count == 0)   ? ImVec4{1,0.3f,0.2f,1}
                                        : ImVec4{1,0.75f,0.1f,1};
    ImGui::TextColored(cnt_col, "  %d / %d tiles selected", sel_count, T * T);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    float bw = (PANEL_W - 20.0f) / 3.0f;
    if (ImGui::Button("All", { bw, 0 })) {
        state.tile_selection.assign(T * T, true);
        state.tile_select_all = true;
    }
    ImGui::SameLine(0, 4);
    if (ImGui::Button("None", { bw, 0 })) {
        state.tile_selection.assign(T * T, false);
        state.tile_select_all = false;
    }
    ImGui::SameLine(0, 4);
    if (ImGui::Button("Invert", { bw, 0 })) {
        for (int i = 0; i < T*T; ++i)
            state.tile_selection[i] = !state.tile_selection[i];
    }

    ImGui::Spacing();
    ImGui::TextColored(COL_DIM, "Click or drag tiles to toggle.");
    if (hov_r >= 0) {
        ImGui::TextColored(COL_ACCENT, "  Hovering tile %d, %d", hov_r, hov_c);
    }

    ImGui::End();
    ImGui::PopStyleColor();
}
