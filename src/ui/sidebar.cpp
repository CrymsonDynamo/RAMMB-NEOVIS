#include "sidebar.hpp"
#include "../satellite_config.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <format>

static constexpr float SIDEBAR_W = 290.0f;

// ── Color palette (RAMMB-inspired dark theme) ────────────────────────────────
static const ImVec4 COL_ACCENT    = { 0.20f, 0.60f, 1.00f, 1.00f }; // blue highlight
static const ImVec4 COL_ACTIVE_BG = { 0.15f, 0.45f, 0.80f, 1.00f }; // selected button bg
static const ImVec4 COL_DIM       = { 0.55f, 0.55f, 0.55f, 1.00f }; // dim text
static const ImVec4 COL_GREEN     = { 0.20f, 0.85f, 0.40f, 1.00f }; // live/status green

// ── Helper: compact radio-style button group ─────────────────────────────────
// Returns true if selection changed.
template<typename IdStr>
static bool button_group(const char* label_prefix,
                          const IdStr* ids, const char* const* names, int count,
                          std::string& current) {
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        bool sel = (current == ids[i]);
        if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACTIVE_BG);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACTIVE_BG);
        }
        if (ImGui::Button(names[i])) {
            current = ids[i];
            changed = true;
        }
        if (sel) ImGui::PopStyleColor(2);
        if (i < count - 1) ImGui::SameLine(0, 4);
    }
    return changed;
}

// ── Sidebar draw ─────────────────────────────────────────────────────────────

float sidebar_draw(ViewState& state, float window_height) {
    ImGui::SetNextWindowPos({ 0.0f, 0.0f });
    ImGui::SetNextWindowSize({ SIDEBAR_W, window_height });
    ImGui::SetNextWindowBgAlpha(0.95f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove           |
        ImGuiWindowFlags_NoResize         |
        ImGuiWindowFlags_NoCollapse       |
        ImGuiWindowFlags_NoTitleBar       |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 4.0f, 5.0f });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, { 0.09f, 0.09f, 0.12f, 1.0f });

    ImGui::Begin("##sidebar", nullptr, flags);

    // ── Header ────────────────────────────────────────────────────────────────
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    ImGui::TextColored(COL_ACCENT, "RAMMB NeoVis");
    ImGui::PopFont();
    ImGui::TextColored(COL_DIM, "Native Satellite Viewer");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Satellite selector ────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "SATELLITE");
    ImGui::Spacing();

    {
        const char* sat_ids[]   = { "goes-19",  "goes-18",  "himawari", "jpss"    };
        const char* sat_names[] = { "GOES-19",  "GOES-18",  "Himawari", "JPSS"    };
        std::string prev = state.satellite;

        // Two rows of 2 to keep it tidy
        for (int i = 0; i < 4; ++i) {
            bool sel = (state.satellite == sat_ids[i]);
            if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, COL_ACTIVE_BG);
                       ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACTIVE_BG); }
            ImGui::SetNextItemWidth(SIDEBAR_W * 0.48f);
            if (ImGui::Button(sat_names[i], { SIDEBAR_W * 0.48f - 6, 0 }))
                state.satellite = sat_ids[i];
            if (sel) ImGui::PopStyleColor(2);
            if (i % 2 == 0) ImGui::SameLine(0, 4);
        }
        if (state.satellite != prev) {
            // Reset sector to first available for this satellite
            const auto* sat = find_satellite(state.satellite);
            if (sat && !sat->sectors.empty())
                state.sector = sat->sectors[0].id;
            state.source_changed = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Sector selector ───────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "SECTOR");
    ImGui::Spacing();

    {
        const auto* sat = find_satellite(state.satellite);
        if (sat) {
            std::string prev = state.sector;
            for (int i = 0; i < int(sat->sectors.size()); ++i) {
                const auto& sec = sat->sectors[i];
                bool sel = (state.sector == sec.id);
                if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, COL_ACTIVE_BG);
                           ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACTIVE_BG); }
                if (ImGui::Button(sec.name, { SIDEBAR_W * 0.48f - 6, 0 }))
                    state.sector = sec.id;
                if (sel) ImGui::PopStyleColor(2);
                if (i % 2 == 0) ImGui::SameLine(0, 4);
            }
            if (state.sector != prev) state.source_changed = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Data resolution ───────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "DATA RESOLUTION");
    ImGui::Spacing();

    {
        const auto* sat = find_satellite(state.satellite);
        const auto* sec = sat ? find_sector(*sat, state.sector) : nullptr;
        int max_z = sec ? sec->max_zoom : 5;
        const auto* prod = find_product(state.product);
        if (prod) max_z = std::max(0, max_z - prod->zoom_level_adjust);

        ImGui::SetNextItemWidth(SIDEBAR_W - 16.0f);
        if (ImGui::BeginCombo("##res", ZOOM_LABELS[std::min(state.data_zoom, 7)])) {
            for (int z = 0; z <= max_z; ++z) {
                bool sel = (state.data_zoom == z);
                if (ImGui::Selectable(ZOOM_LABELS[z], sel)) {
                    state.data_zoom  = z;
                    state.zoom_changed = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        int n = 1 << state.data_zoom;
        ImGui::TextColored(COL_DIM, "  %d × %d tiles  (%d total)", n, n, n * n);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Product browser ───────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "PRODUCT");
    ImGui::Spacing();

    static char search[64] = {};
    ImGui::SetNextItemWidth(SIDEBAR_W - 16.0f);
    ImGui::InputTextWithHint("##search", "Search products...", search, sizeof(search));
    ImGui::Spacing();

    // Calculate available height for scrollable product list
    // Reserve space below for animation + status sections
    float list_h = window_height - ImGui::GetCursorPosY() - 205.0f;
    list_h = std::max(list_h, 80.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.06f, 0.06f, 0.09f, 1.0f });
    ImGui::BeginChild("##products", { SIDEBAR_W - 8.0f, list_h }, false);

    std::string filter(search);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    for (const auto& cat : PRODUCT_CATEGORIES) {
        // Check if any product in this category matches the filter
        bool any_match = filter.empty();
        if (!any_match) {
            for (const auto& p : cat.products) {
                std::string n(p.name);
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find(filter) != std::string::npos ||
                    std::string(p.id).find(filter) != std::string::npos) {
                    any_match = true; break;
                }
            }
        }
        if (!any_match) continue;

        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        bool open = ImGui::CollapsingHeader(cat.name);
        if (!open) continue;

        for (const auto& prod : cat.products) {
            if (!filter.empty()) {
                std::string n(prod.name);
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find(filter) == std::string::npos &&
                    std::string(prod.id).find(filter) == std::string::npos)
                    continue;
            }

            bool sel = (state.product == prod.id);
            ImGui::PushID(prod.id);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Header, COL_ACTIVE_BG);
            if (ImGui::Selectable(prod.name, sel, 0, { 0, 0 })) {
                if (state.product != prod.id) {
                    state.product        = prod.id;
                    state.source_changed = true;

                    // Auto-clamp zoom to product max
                    const auto* sat = find_satellite(state.satellite);
                    const auto* sec = sat ? find_sector(*sat, state.sector) : nullptr;
                    int max_z = sec ? std::max(0, sec->max_zoom - prod.zoom_level_adjust) : 0;
                    if (state.data_zoom > max_z) {
                        state.data_zoom    = max_z;
                        state.zoom_changed = true;
                    }
                }
            }
            if (sel) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::Spacing();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Animation controls (placeholder) ─────────────────────────────────────
    ImGui::TextColored(COL_DIM, "ANIMATION");
    ImGui::Spacing();

    // Frames selector
    ImGui::Text("Frames:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    const int frame_opts[] = { 6, 12, 18, 24, 36, 48 };
    if (ImGui::BeginCombo("##frames", std::to_string(state.num_frames).c_str())) {
        for (int f : frame_opts) {
            bool s = (state.num_frames == f);
            if (ImGui::Selectable(std::to_string(f).c_str(), s))
                state.num_frames = f;
            if (s) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Text("  Step:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    const int step_opts[] = { 1, 5, 10, 30, 60 };
    std::string step_lbl = std::to_string(state.time_step) + "m";
    if (ImGui::BeginCombo("##step", step_lbl.c_str())) {
        for (int s : step_opts) {
            bool sel = (state.time_step == s);
            if (ImGui::Selectable((std::to_string(s) + "m").c_str(), sel))
                state.time_step = s;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    // Playback buttons
    float btn_w = (SIDEBAR_W - 24.0f) / 5.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, { 0.15f, 0.15f, 0.20f, 1.0f });
    ImGui::Button("|<",  { btn_w, 0 }); ImGui::SameLine(0, 3);
    ImGui::Button("<",   { btn_w, 0 }); ImGui::SameLine(0, 3);

    // Play/pause
    bool& pl = state.playing;
    if (pl) ImGui::PushStyleColor(ImGuiCol_Button, COL_ACTIVE_BG);
    if (ImGui::Button(pl ? "||" : ">", { btn_w, 0 })) pl = !pl;
    if (pl) ImGui::PopStyleColor();
    ImGui::SameLine(0, 3);

    ImGui::Button(">",   { btn_w, 0 }); ImGui::SameLine(0, 3);
    ImGui::Button(">|",  { btn_w, 0 });
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::TextColored(COL_DIM, "  Speed:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(SIDEBAR_W - 90.0f);
    ImGui::SliderFloat("##speed", &state.speed, 0.25f, 4.0f, "%.2fx");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Refresh + timestamp ───────────────────────────────────────────────────
    if (ImGui::Button("Refresh Latest", { SIDEBAR_W - 16.0f, 0 }))
        state.refresh_request = true;

    ImGui::Spacing();
    if (!state.timestamp_str.empty()) {
        ImGui::TextColored(COL_GREEN, "  LIVE");
        ImGui::SameLine();
        ImGui::TextColored(COL_DIM, "%s", state.timestamp_str.c_str());
    }

    ImGui::End();
    ImGui::PopStyleColor();     // WindowBg
    ImGui::PopStyleVar(3);

    return SIDEBAR_W;
}
