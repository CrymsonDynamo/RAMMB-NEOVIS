#include "sidebar.hpp"
#include "../satellite_config.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <map>
#include <format>

static constexpr float SIDEBAR_W = 300.0f;

static const ImVec4 COL_ACCENT    = { 0.20f, 0.60f, 1.00f, 1.00f };
static const ImVec4 COL_ACTIVE_BG = { 0.15f, 0.45f, 0.80f, 1.00f };
static const ImVec4 COL_DIM       = { 0.55f, 0.55f, 0.55f, 1.00f };
static const ImVec4 COL_GREEN     = { 0.20f, 0.85f, 0.40f, 1.00f };

static const char* MONTH_NAMES[] = {
    "Jan","Feb","Mar","Apr","May","Jun",
    "Jul","Aug","Sep","Oct","Nov","Dec"
};

// ── Calendar helpers ──────────────────────────────────────────────────────────

// Returns the day-of-week (0=Sun..6=Sat) for the 1st of year/month.
static int first_dow(int y, int m) {
    struct tm t{}; t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = 1;
    mktime(&t); return t.tm_wday;
}

static int days_in_month(int y, int m) {
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return d[m - 1];
}

// Per-popup calendar navigation state (keyed by id_prefix string).
struct CalNavState { int year, month; };
static std::map<std::string, CalNavState> s_cal_nav;

// Draws an inline calendar popup. Call while inside a PushID block.
// popup_id must match the ID passed to OpenPopup.
// Returns true when a day was selected (year/month/day updated).
static bool draw_calendar_popup(const char* popup_id, const char* map_key,
                                int& year, int& month, int& day) {
    bool changed = false;
    if (!ImGui::BeginPopup(popup_id)) return false;

    auto& nav = s_cal_nav[map_key];

    constexpr float CELL   = 28.0f;
    constexpr float BTN_H  = 20.0f;
    constexpr float GAP    =  2.0f;
    constexpr float GRID_W = CELL * 7.0f;

    // ── Month / Year navigation header ───────────────────────────────────────
    ImGui::SetNextItemWidth(GRID_W);

    if (ImGui::ArrowButton("##cprev", ImGuiDir_Left)) {
        if (--nav.month < 1) { nav.month = 12; --nav.year; }
    }
    ImGui::SameLine(0, 4);
    // Centered month+year label
    {
        char hdr[32]; snprintf(hdr, sizeof(hdr), "%s %d", MONTH_NAMES[nav.month - 1], nav.year);
        float tw = ImGui::CalcTextSize(hdr).x;
        float cx = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(cx + (GRID_W - 32.0f - tw) * 0.5f);
        ImGui::Text("%s", hdr);
    }
    ImGui::SameLine(0, 4);
    if (ImGui::ArrowButton("##cnext", ImGuiDir_Right)) {
        if (++nav.month > 12) { nav.month = 1; ++nav.year; }
    }

    ImGui::Spacing();

    // ── Day-of-week header ────────────────────────────────────────────────────
    static const char* dn[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    float hx = ImGui::GetCursorPosX();
    float hy = ImGui::GetCursorPosY();
    for (int i = 0; i < 7; ++i) {
        ImGui::SetCursorPos({ hx + i * CELL, hy });
        ImGui::TextColored({ 0.55f, 0.55f, 0.55f, 1.0f }, "%s", dn[i]);
    }
    ImGui::SetCursorPosY(hy + ImGui::GetTextLineHeight() + GAP);

    // ── Day grid ──────────────────────────────────────────────────────────────
    int fdow = first_dow(nav.year, nav.month);
    int dim  = days_in_month(nav.year, nav.month);
    float gx = ImGui::GetCursorPosX();
    float gy = ImGui::GetCursorPosY();

    for (int d = 1; d <= dim; ++d) {
        int cell_idx = fdow + d - 1;
        int row = cell_idx / 7;
        int col = cell_idx % 7;
        ImGui::SetCursorPos({ gx + col * CELL, gy + row * (BTN_H + GAP) });

        bool sel = (d == day && nav.month == month && nav.year == year);
        if (sel) {
            ImGui::PushStyleColor(ImGuiCol_Button,        { 0.15f, 0.45f, 0.80f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.20f, 0.60f, 1.00f, 1.0f });
        }
        ImGui::PushID(d);
        char lbl[4]; snprintf(lbl, sizeof(lbl), "%d", d);
        if (ImGui::Button(lbl, { CELL - GAP, BTN_H })) {
            year = nav.year; month = nav.month; day = d;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
        if (sel) ImGui::PopStyleColor(2);
    }

    // Advance cursor past the grid
    int total_rows = (fdow + dim + 6) / 7;
    ImGui::SetCursorPosY(gy + total_rows * (BTN_H + GAP) + GAP);
    ImGui::Spacing();

    ImGui::EndPopup();
    return changed;
}

// ── Other Helpers ─────────────────────────────────────────────────────────────

// Returns true if any product in the category name-matches the filter.
static bool category_matches(const ProductCategory& cat, const std::string& filter) {
    if (filter.empty()) return true;
    for (auto& p : cat.products) {
        std::string n(p.name); std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        std::string id(p.id);  std::transform(id.begin(), id.end(), id.begin(), ::tolower);
        if (n.find(filter) != std::string::npos || id.find(filter) != std::string::npos)
            return true;
    }
    return false;
}

static bool product_matches(const ProductDef& p, const std::string& filter) {
    if (filter.empty()) return true;
    std::string n(p.name); std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    std::string id(p.id);  std::transform(id.begin(), id.end(), id.begin(), ::tolower);
    return n.find(filter) != std::string::npos || id.find(filter) != std::string::npos;
}

// Date-only row: calendar button + month/day/year.
// Returns true if any date component changed (triggers time-list refetch).
static bool date_row(const char* id_prefix, int& year, int& month, int& day) {
    bool changed = false;
    ImGui::PushID(id_prefix);

    int prev_year = year, prev_month = month, prev_day = day;

    // Ensure nav state exists
    if (s_cal_nav.find(id_prefix) == s_cal_nav.end())
        s_cal_nav[id_prefix] = { year, month };

    // Calendar popup button
    if (ImGui::SmallButton("Cal")) {
        s_cal_nav[id_prefix] = { year, month };
        ImGui::OpenPopup("##calp");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open calendar picker");
    ImGui::SameLine(0, 4);

    // Month dropdown
    ImGui::SetNextItemWidth(50.0f);
    if (ImGui::BeginCombo("##mo", MONTH_NAMES[std::clamp(month,1,12)-1])) {
        for (int m = 1; m <= 12; ++m) {
            bool sel = (month == m);
            if (ImGui::Selectable(MONTH_NAMES[m-1], sel)) month = m;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 3);

    // Day
    ImGui::SetNextItemWidth(34.0f);
    ImGui::InputInt("##dy", &day, 0);
    day = std::clamp(day, 1, 31);
    ImGui::SameLine(0, 3);

    // Year
    ImGui::SetNextItemWidth(52.0f);
    ImGui::InputInt("##yr", &year, 0);
    year = std::clamp(year, 2017, 2099);

    // Calendar popup
    if (draw_calendar_popup("##calp", id_prefix, year, month, day))
        changed = true;

    if (year != prev_year || month != prev_month || day != prev_day)
        changed = true;

    ImGui::PopID();
    return changed;
}

// Time dropdown populated from available timestamps fetched from API.
// Falls back to manual HH:MM input when avail is empty (not yet fetched).
static void time_selector(const char* id,
                          const std::vector<int64_t>& avail,
                          int& sel_idx, int& hour, int& min) {
    ImGui::PushID(id);

    if (avail.empty()) {
        // Manual fallback
        ImGui::SetNextItemWidth(28.0f);
        ImGui::InputInt("##hh", &hour, 0);
        hour = std::clamp(hour, 0, 23);
        ImGui::SameLine(0, 2);
        ImGui::TextDisabled(":");
        ImGui::SameLine(0, 2);
        ImGui::SetNextItemWidth(28.0f);
        ImGui::InputInt("##mn", &min, 0);
        min = std::clamp(min, 0, 59);
        ImGui::SameLine(0, 4);
        ImGui::TextColored(COL_DIM, "(loading...)");
    } else {
        sel_idx = std::clamp(sel_idx, 0, int(avail.size()) - 1);

        // Format current selection label
        std::string ts_s = std::to_string(avail[sel_idx]);
        std::string cur_lbl = (ts_s.size() >= 12)
            ? ts_s.substr(8, 2) + ":" + ts_s.substr(10, 2) + " UTC"
            : "??:??";

        ImGui::SetNextItemWidth(SIDEBAR_W - 80.0f);
        if (ImGui::BeginCombo("##ts", cur_lbl.c_str())) {
            for (int i = 0; i < int(avail.size()); ++i) {
                std::string s = std::to_string(avail[i]);
                if (s.size() < 12) continue;
                char lbl[16];
                snprintf(lbl, sizeof(lbl), "%c%c:%c%c UTC",
                         s[8], s[9], s[10], s[11]);
                bool is_sel = (i == sel_idx);
                if (ImGui::Selectable(lbl, is_sel)) {
                    sel_idx = i;
                    hour = (s[8] - '0') * 10 + (s[9] - '0');
                    min  = (s[10] - '0') * 10 + (s[11] - '0');
                }
                if (is_sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::PopID();
}

// ── Main draw ─────────────────────────────────────────────────────────────────

float sidebar_draw(ViewState& state, float window_height, float y_offset) {
    ImGui::SetNextWindowPos({ 0.0f, y_offset });
    ImGui::SetNextWindowSize({ SIDEBAR_W, window_height });
    ImGui::SetNextWindowBgAlpha(0.95f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     { 4.0f, 5.0f });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, { 0.09f, 0.09f, 0.12f, 1.0f });

    ImGui::Begin("##sidebar", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ── Header ────────────────────────────────────────────────────────────────
    ImGui::TextColored(COL_ACCENT, "RAMMB-NEOVIS");
    ImGui::SameLine();
    ImGui::TextColored(COL_DIM, "Native Viewer");
    ImGui::Separator();
    ImGui::Spacing();

    // ── Satellite ─────────────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "SATELLITE");
    {
        const char* ids[]   = { "goes-19", "goes-18", "himawari", "jpss" };
        const char* names[] = { "GOES-19", "GOES-18", "Himawari", "JPSS" };
        std::string prev = state.satellite;
        for (int i = 0; i < 4; ++i) {
            bool sel = (state.satellite == ids[i]);
            if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, COL_ACTIVE_BG);
                       ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACTIVE_BG); }
            if (ImGui::Button(names[i], { (SIDEBAR_W - 16.0f) * 0.5f - 2.0f, 0 }))
                state.satellite = ids[i];
            if (sel) ImGui::PopStyleColor(2);
            if (i % 2 == 0) ImGui::SameLine(0, 4);
        }
        if (state.satellite != prev) {
            const auto* sat = find_satellite(state.satellite);
            if (sat && !sat->sectors.empty()) state.sector = sat->sectors[0].id;
            state.source_changed = true;
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Sector ────────────────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "SECTOR");
    {
        const auto* sat = find_satellite(state.satellite);
        if (sat) {
            std::string prev = state.sector;
            for (int i = 0; i < int(sat->sectors.size()); ++i) {
                auto& sec = sat->sectors[i];
                bool sel  = (state.sector == sec.id);
                if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, COL_ACTIVE_BG);
                           ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACTIVE_BG); }
                if (ImGui::Button(sec.name, { (SIDEBAR_W - 16.0f) * 0.5f - 2.0f, 0 }))
                    state.sector = sec.id;
                if (sel) ImGui::PopStyleColor(2);
                if (i % 2 == 0) ImGui::SameLine(0, 4);
            }
            if (state.sector != prev) state.source_changed = true;
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Data resolution ───────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "DATA RESOLUTION");
    {
        const auto* sat  = find_satellite(state.satellite);
        const auto* sec  = sat ? find_sector(*sat, state.sector) : nullptr;
        const auto* prod = find_product(state.product);
        int max_z = sec ? sec->max_zoom : 5;
        if (prod) max_z = std::max(0, max_z - prod->zoom_level_adjust);

        ImGui::SetNextItemWidth(SIDEBAR_W - 16.0f);
        if (ImGui::BeginCombo("##res", ZOOM_LABELS[std::min(state.data_zoom, 7)])) {
            for (int z = 0; z <= max_z; ++z) {
                bool sel = (state.data_zoom == z);
                if (ImGui::Selectable(ZOOM_LABELS[z], sel)) {
                    state.data_zoom   = z;
                    state.zoom_changed = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        int n = 1 << state.data_zoom;
        ImGui::TextColored(COL_DIM, "  %d×%d = %d tiles per frame", n, n, n * n);
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Product browser ───────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "PRODUCT");
    static char search[64] = {};
    ImGui::SetNextItemWidth(SIDEBAR_W - 16.0f);
    ImGui::InputTextWithHint("##search", "Search products...", search, sizeof(search));

    std::string filter(search);
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

    // Reserve space: window height minus sections below (animation + date + settings + status)
    float list_h = window_height - ImGui::GetCursorPosY() - 330.0f;
    list_h = std::max(list_h, 60.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.06f, 0.06f, 0.09f, 1.0f });
    ImGui::BeginChild("##products", { SIDEBAR_W - 8.0f, list_h }, false);

    for (const auto& cat : PRODUCT_CATEGORIES) {
        if (!category_matches(cat, filter)) continue;
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        bool open = ImGui::CollapsingHeader(cat.name);
        if (!open) continue;

        for (const auto& prod : cat.products) {
            if (!product_matches(prod, filter)) continue;
            bool sel = (state.product == prod.id);
            ImGui::PushID(prod.id);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Header, COL_ACTIVE_BG);
            if (ImGui::Selectable(prod.name, sel)) {
                if (state.product != prod.id) {
                    state.product        = prod.id;
                    state.source_changed = true;
                    // Auto-clamp zoom
                    const auto* sat = find_satellite(state.satellite);
                    const auto* sec = sat ? find_sector(*sat, state.sector) : nullptr;
                    int max_z = sec ? std::max(0, sec->max_zoom - prod.zoom_level_adjust) : 0;
                    if (state.data_zoom > max_z) { state.data_zoom = max_z; state.zoom_changed = true; }
                }
            }
            if (sel) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::Spacing();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Animation ─────────────────────────────────────────────────────────────
    ImGui::TextColored(COL_DIM, "ANIMATION");
    ImGui::Spacing();

    // Date range toggle
    bool& use_range = state.date_range.use_range;
    {
        bool prev = use_range;
        ImGui::RadioButton("Latest frames", !use_range);
        if (ImGui::IsItemClicked()) use_range = false;
        ImGui::SameLine();
        ImGui::RadioButton("Date range",    use_range);
        if (ImGui::IsItemClicked()) use_range = true;
        if (use_range != prev) state.range_changed = true;
    }

    ImGui::Spacing();

    if (!use_range) {
        // ── Latest N frames ───────────────────────────────────────────────────
        ImGui::Text("Frames:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(65.0f);
        const int fopts[] = { 6, 12, 18, 24, 36, 48, 60 };
        int prev_f = state.num_frames;
        if (ImGui::BeginCombo("##frames", std::to_string(state.num_frames).c_str())) {
            for (int f : fopts) {
                bool s = (state.num_frames == f);
                if (ImGui::Selectable(std::to_string(f).c_str(), s)) state.num_frames = f;
                if (s) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (state.num_frames != prev_f) state.range_changed = true;

        ImGui::SameLine();
        ImGui::Text("Step:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(55.0f);
        const int sopts[] = { 1, 5, 10, 30, 60 };
        int prev_s = state.time_step;
        if (ImGui::BeginCombo("##step", (std::to_string(state.time_step)+"m").c_str())) {
            for (int s : sopts) {
                bool sel = (state.time_step == s);
                if (ImGui::Selectable((std::to_string(s)+"m").c_str(), sel)) state.time_step = s;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (state.time_step != prev_s) state.range_changed = true;
    } else {
        // ── Date range picker ─────────────────────────────────────────────────
        ImGui::TextColored(COL_DIM, "  From:");
        if (date_row("rng_start",
                state.date_range.start_year, state.date_range.start_month,
                state.date_range.start_day))
            state.request_start_times = true;
        ImGui::TextColored(COL_DIM, "    Time:");
        ImGui::SameLine();
        time_selector("rng_start_t", state.avail_start_times,
                      state.start_time_sel,
                      state.date_range.start_hour, state.date_range.start_min);

        ImGui::Spacing();
        ImGui::TextColored(COL_DIM, "  To:");
        if (date_row("rng_end",
                state.date_range.end_year, state.date_range.end_month,
                state.date_range.end_day))
            state.request_end_times = true;
        ImGui::TextColored(COL_DIM, "    Time:");
        ImGui::SameLine();
        time_selector("rng_end_t", state.avail_end_times,
                      state.end_time_sel,
                      state.date_range.end_hour, state.date_range.end_min);

        ImGui::Spacing();

        // Time step (for date-range mode too)
        ImGui::Text("Step:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(55.0f);
        const int sopts_r[] = { 1, 5, 10, 30, 60 };
        if (ImGui::BeginCombo("##step_r", (std::to_string(state.time_step)+"m").c_str())) {
            for (int s : sopts_r) {
                bool sel = (state.time_step == s);
                if (ImGui::Selectable((std::to_string(s)+"m").c_str(), sel)) state.time_step = s;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Load Range", { SIDEBAR_W - 16.0f, 0 }))
            state.range_changed = true;
    }

    ImGui::Spacing();

    // Playback bar
    float btn_w = (SIDEBAR_W - 20.0f) / 5.0f;
    ImGui::PushStyleColor(ImGuiCol_Button, { 0.15f, 0.15f, 0.20f, 1.0f });

    if (ImGui::Button("|<", { btn_w, 0 })) { state.playing = false; state.current_frame = 0; }
    ImGui::SameLine(0, 3);
    if (ImGui::Button("<",  { btn_w, 0 })) {
        state.playing = false;
        state.current_frame = std::max(0, state.current_frame - 1);
    }
    ImGui::SameLine(0, 3);

    bool& pl = state.playing;
    if (pl) ImGui::PushStyleColor(ImGuiCol_Button, COL_ACTIVE_BG);
    if (ImGui::Button(pl ? "||" : " > ", { btn_w, 0 })) pl = !pl;
    if (pl) ImGui::PopStyleColor();
    ImGui::SameLine(0, 3);

    if (ImGui::Button(">",  { btn_w, 0 })) {
        state.playing = false;
        state.current_frame = std::min(int(state.frame_timestamps.size()) - 1, state.current_frame + 1);
    }
    ImGui::SameLine(0, 3);
    if (ImGui::Button(">|", { btn_w, 0 })) {
        state.playing       = false;
        state.current_frame = std::max(0, int(state.frame_timestamps.size()) - 1);
    }
    ImGui::PopStyleColor();

    // Loop mode
    ImGui::Spacing();
    ImGui::TextColored(COL_DIM, "  Mode:"); ImGui::SameLine();
    const char* modes[] = { "Loop", "Rock", "Once" };
    for (int i = 0; i < 3; ++i) {
        bool sel = (state.loop_mode == i);
        if (sel) { ImGui::PushStyleColor(ImGuiCol_Button, COL_ACTIVE_BG);
                   ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACTIVE_BG); }
        if (ImGui::SmallButton(modes[i])) state.loop_mode = i;
        if (sel) ImGui::PopStyleColor(2);
        if (i < 2) ImGui::SameLine(0, 4);
    }

    // FPS
    ImGui::Spacing();
    ImGui::TextColored(COL_DIM, "  FPS:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(SIDEBAR_W - 72.0f);
    ImGui::SliderFloat("##fps", &state.fps, 1.0f, 30.0f, "%.0f fps");

    // Frame info
    if (!state.frame_timestamps.empty()) {
        int N = int(state.frame_timestamps.size());
        ImGui::Spacing();
        ImGui::TextColored(COL_DIM, "  Frame %d / %d", state.current_frame + 1, N);
        if (!state.timestamp_str.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(COL_GREEN, "  %s", state.timestamp_str.c_str());
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Refresh ───────────────────────────────────────────────────────────────
    if (ImGui::Button("Refresh", { (SIDEBAR_W - 20.0f) * 0.5f, 0 }))
        state.refresh_request = true;
    ImGui::SameLine(0, 4);
    if (ImGui::Button("Reset View", { (SIDEBAR_W - 20.0f) * 0.5f, 0 }))
        state.refresh_request = false; // placeholder; Home key handles it

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button,        { 0.15f, 0.50f, 0.15f, 1.0f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.20f, 0.70f, 0.20f, 1.0f });
    if (ImGui::Button("Export...", { SIDEBAR_W - 16.0f, 24.0f }))
        state.export_requested = true;
    ImGui::PopStyleColor(2);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Settings (collapsible) ─────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Settings")) {
        ImGui::Spacing();
        ImGui::TextColored(COL_DIM, "Download Speed Limit");
        bool unlimited = (state.download_limit_kbps == 0);
        if (ImGui::Checkbox("Unlimited", &unlimited))
            state.download_limit_kbps = unlimited ? 0 : 5120;
        if (!unlimited) {
            ImGui::SetNextItemWidth(SIDEBAR_W - 16.0f);
            ImGui::SliderInt("##throttle", &state.download_limit_kbps, 128, 51200, "%d KB/s");
            ImGui::TextColored(COL_DIM, "  Presets:"); ImGui::SameLine();
            if (ImGui::SmallButton("1 MB/s"))  state.download_limit_kbps = 1024;
            ImGui::SameLine();
            if (ImGui::SmallButton("5 MB/s"))  state.download_limit_kbps = 5120;
            ImGui::SameLine();
            if (ImGui::SmallButton("20 MB/s")) state.download_limit_kbps = 20480;
        }
        ImGui::Spacing();
        ImGui::TextColored(COL_DIM, "Concurrent Downloads");
        ImGui::SetNextItemWidth(SIDEBAR_W - 16.0f);
        ImGui::SliderInt("##threads", &state.download_threads, 1, 32, "%d threads");
        ImGui::TextColored(COL_DIM, "  (takes effect on next refresh)");
        ImGui::Spacing();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    return SIDEBAR_W;
}
