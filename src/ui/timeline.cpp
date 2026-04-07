#include "timeline.hpp"
#include <imgui.h>
#include <format>
#include <algorithm>
#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string fmt_ts_short(int64_t ts) {
    std::string s = std::to_string(ts);
    if (s.size() < 12) return s;
    // HH:MM
    return std::format("{}:{}", s.substr(8, 2), s.substr(10, 2));
}
static std::string fmt_ts_long(int64_t ts) {
    std::string s = std::to_string(ts);
    if (s.size() < 12) return s;
    return std::format("{}-{}-{} {}:{} UTC",
        s.substr(0,4), s.substr(4,2), s.substr(6,2),
        s.substr(8,2), s.substr(10,2));
}

// Colours (Blender-inspired)
static constexpr ImU32 COL_BG_RULER  = IM_COL32( 22,  22,  30, 255);
static constexpr ImU32 COL_BG_TRACK  = IM_COL32( 32,  32,  44, 255);
static constexpr ImU32 COL_TICK_MAJ  = IM_COL32(100, 100, 130, 180);
static constexpr ImU32 COL_TICK_MIN  = IM_COL32( 60,  60,  85, 130);
static constexpr ImU32 COL_LABEL     = IM_COL32(170, 170, 200, 220);
static constexpr ImU32 COL_PLAYHEAD  = IM_COL32(255, 200,  60, 240);
static constexpr ImU32 COL_PH_FILL   = IM_COL32(255, 200,  60, 200);
static constexpr ImU32 COL_EXP_BAND  = IM_COL32(255, 160,  30,  45);
static constexpr ImU32 COL_EXP_EDGE  = IM_COL32(255, 180,  50, 220);
static constexpr ImU32 COL_EXP_HDL   = IM_COL32(255, 200,  80, 255);
static constexpr ImU32 COL_FRAME_DOT = IM_COL32(100, 140, 200, 160);
static constexpr ImU32 COL_FRAME_CUR = IM_COL32(255, 255, 255, 200);

static constexpr float RULER_H = 20.0f;
static constexpr float HDL_R   =  5.0f;   // export handle radius
static constexpr float PH_W    =  1.5f;   // playhead line width

bool timeline_draw(float x, float y, float w, float h,
                   const std::vector<int64_t>& timestamps,
                   int&           current,
                   bool&          playing,
                   TimelineState& tl,
                   bool&          exp_use_range,
                   int&           exp_start,
                   int&           exp_end) {
    if (timestamps.empty()) return false;

    int N = int(timestamps.size());
    bool changed = false;

    // ── Window setup ──────────────────────────────────────────────────────────
    ImGui::SetNextWindowPos ({ x, y });
    ImGui::SetNextWindowSize({ w, h });
    ImGui::SetNextWindowBgAlpha(0.0f);   // we paint the background ourselves
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   { 0.0f, 0.0f });

    ImGui::Begin("##timeline", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImGuiIO&    io   = ImGui::GetIO();
    ImVec2      wpos = ImGui::GetWindowPos();

    // ── Track geometry ────────────────────────────────────────────────────────
    const float PAD     = 10.0f;
    float track_x0 = wpos.x + PAD;
    float track_x1 = wpos.x + w - PAD;
    float track_w  = track_x1 - track_x0;
    float ruler_y0 = wpos.y;
    float ruler_y1 = wpos.y + RULER_H;
    float track_y0 = ruler_y1;
    float track_y1 = wpos.y + h;

    // ── Resolve zoom/scroll ───────────────────────────────────────────────────
    float auto_ppf = (N > 1) ? track_w / float(N - 1) : track_w;
    if (tl.px_per_frame <= 0.0f) tl.px_per_frame = auto_ppf;

    // Clamp scroll so we never go completely off-screen
    float max_scroll = std::max(0.0f, float(N - 1) - track_w / tl.px_per_frame);
    tl.scroll = std::clamp(tl.scroll, 0.0f, max_scroll);

    // Helpers: frame index → screen x, and vice versa
    auto frame_x = [&](float fi) {
        return track_x0 + (fi - tl.scroll) * tl.px_per_frame;
    };
    auto x_to_frame = [&](float sx) -> float {
        return (sx - track_x0) / tl.px_per_frame + tl.scroll;
    };
    auto clamp_frame = [&](float fi) {
        return std::clamp(int(std::round(fi)), 0, N - 1);
    };

    // ── Clamp export range ────────────────────────────────────────────────────
    if (exp_use_range) {
        exp_start = std::clamp(exp_start, 0, N - 1);
        exp_end   = std::clamp(exp_end,   0, N - 1);
        if (exp_end < exp_start) std::swap(exp_start, exp_end);
    }

    // ── Background ───────────────────────────────────────────────────────────
    dl->AddRectFilled({ wpos.x, ruler_y0 }, { wpos.x+w, ruler_y1 }, COL_BG_RULER);
    dl->AddRectFilled({ wpos.x, track_y0 }, { wpos.x+w, track_y1 }, COL_BG_TRACK);

    // ── Choose a tick interval that keeps labels readable ────────────────────
    // Pick smallest interval from {1,2,5,10,20,50,...} where labels are ≥40px apart
    int tick_step = 1;
    {
        const int candidates[] = {1,2,5,10,20,50,100,200,500};
        for (int c : candidates) {
            if (c * tl.px_per_frame >= 40.0f) { tick_step = c; break; }
            tick_step = c;
        }
    }
    int label_step = tick_step;
    // Only draw labels if they'd be at least 50px apart
    if (label_step * tl.px_per_frame < 50.0f) label_step *= 2;

    // ── Tick marks + grid lines + labels ─────────────────────────────────────
    int first_tick = std::max(0, int(std::floor(tl.scroll / tick_step)) * tick_step);
    for (int fi = first_tick; fi < N; fi += tick_step) {
        float fx = frame_x(float(fi));
        if (fx < track_x0 - 1.0f) continue;
        if (fx > track_x1 + 1.0f) break;

        bool is_label = (fi % label_step == 0);
        ImU32 col = is_label ? COL_TICK_MAJ : COL_TICK_MIN;

        // Ruler tick
        float tick_y = is_label ? ruler_y0 + 4.0f : ruler_y0 + 8.0f;
        dl->AddLine({ fx, tick_y }, { fx, ruler_y1 }, col, 1.0f);

        // Vertical grid line through track
        if (is_label)
            dl->AddLine({ fx, track_y0 }, { fx, track_y1 }, IM_COL32(60,60,90,80), 1.0f);

        // Label
        if (is_label && fi < N) {
            std::string lbl = fmt_ts_short(timestamps[fi]);
            ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
            float lx = fx - ts.x * 0.5f;
            // Don't clip against left edge label
            lx = std::max(lx, track_x0);
            dl->AddText({ lx, ruler_y0 + 3.0f }, COL_LABEL, lbl.c_str());
        }
    }

    // ── Frame dots along track ────────────────────────────────────────────────
    float dot_y = track_y0 + (track_y1 - track_y0) * 0.78f;
    for (int fi = 0; fi < N; ++fi) {
        float fx = frame_x(float(fi));
        if (fx < track_x0 || fx > track_x1) continue;
        ImU32 dc = (fi == current) ? COL_FRAME_CUR : COL_FRAME_DOT;
        float dr = (fi == current) ? 3.0f : 2.0f;
        dl->AddCircleFilled({ fx, dot_y }, dr, dc);
    }

    // ── Export range band ─────────────────────────────────────────────────────
    if (exp_use_range) {
        float ex0 = frame_x(float(exp_start));
        float ex1 = frame_x(float(exp_end));
        ex0 = std::clamp(ex0, track_x0, track_x1);
        ex1 = std::clamp(ex1, track_x0, track_x1);

        dl->AddRectFilled({ ex0, track_y0 }, { ex1, track_y1 }, COL_EXP_BAND);
        dl->AddLine({ ex0, track_y0 }, { ex0, track_y1 }, COL_EXP_EDGE, 1.5f);
        dl->AddLine({ ex1, track_y0 }, { ex1, track_y1 }, COL_EXP_EDGE, 1.5f);

        // Handle circles at ruler level
        dl->AddCircleFilled({ ex0, ruler_y1 }, HDL_R, COL_EXP_HDL);
        dl->AddCircle      ({ ex0, ruler_y1 }, HDL_R, IM_COL32(255,255,255,180), 0, 1.2f);
        dl->AddCircleFilled({ ex1, ruler_y1 }, HDL_R, COL_EXP_HDL);
        dl->AddCircle      ({ ex1, ruler_y1 }, HDL_R, IM_COL32(255,255,255,180), 0, 1.2f);

        // Labels above the handles
        auto lbl_s = std::format("S:{}", exp_start + 1);
        auto lbl_e = std::format("E:{}", exp_end   + 1);
        dl->AddText({ ex0 + 6.0f, ruler_y0 + 2.0f }, COL_EXP_HDL, lbl_s.c_str());
        dl->AddText({ ex1 + 6.0f, ruler_y0 + 2.0f }, COL_EXP_HDL, lbl_e.c_str());
    }

    // ── Playhead ──────────────────────────────────────────────────────────────
    float ph_x = frame_x(float(current));
    if (ph_x >= track_x0 - 2.0f && ph_x <= track_x1 + 2.0f) {
        dl->AddLine({ ph_x, ruler_y0 }, { ph_x, track_y1 }, COL_PLAYHEAD, PH_W);

        // Diamond handle at top of ruler
        float dh = RULER_H * 0.55f;
        float dw = dh * 0.6f;
        ImVec2 diamond[4] = {
            { ph_x,       ruler_y0         },  // top
            { ph_x + dw,  ruler_y0 + dh    },  // right
            { ph_x,       ruler_y0 + dh * 2.0f }, // bottom
            { ph_x - dw,  ruler_y0 + dh    },  // left
        };
        dl->AddConvexPolyFilled(diamond, 4, COL_PH_FILL);
        dl->AddPolyline(diamond, 4, COL_PLAYHEAD, ImDrawFlags_Closed, 1.2f);
    }

    // ── Interaction ───────────────────────────────────────────────────────────
    // We use a single invisible button covering the entire timeline for event detection.
    ImGui::SetCursorScreenPos({ wpos.x, wpos.y });
    ImGui::InvisibleButton("##tl_area", { w, h });

    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    // Persistent drag target (0=none, 1=playhead, 2=exp_start, 3=exp_end, 4=scroll)
    static int s_drag_target = 0;

    if (ImGui::IsItemClicked(0)) {
        float mx = io.MousePos.x;
        float my = io.MousePos.y;

        // Decide what was clicked
        s_drag_target = 0;

        // Check export handles first (they're smaller than playhead)
        if (exp_use_range) {
            float ex0 = frame_x(float(exp_start));
            float ex1 = frame_x(float(exp_end));
            float dx0 = mx - ex0, dx1 = mx - ex1;
            float dy  = my - ruler_y1;
            if (dx0*dx0 + dy*dy <= (HDL_R+3)*(HDL_R+3)) s_drag_target = 2;
            else if (dx1*dx1 + dy*dy <= (HDL_R+3)*(HDL_R+3)) s_drag_target = 3;
        }

        if (s_drag_target == 0) {
            // Playhead drag if near
            float ph_dx = mx - frame_x(float(current));
            if (std::abs(ph_dx) <= 8.0f && my <= ruler_y1 + 6.0f)
                s_drag_target = 1;
        }

        if (s_drag_target == 0) {
            // Click anywhere on ruler → scroll drag; click on track → jump to frame
            if (my <= ruler_y1)
                s_drag_target = 4;  // ruler drag = scroll
            else {
                int nf = clamp_frame(x_to_frame(mx));
                if (nf != current) { current = nf; changed = true; }
                s_drag_target = 1;  // continue as playhead drag
            }
        }
    }

    if (active && io.MouseDelta.x != 0.0f) {
        float dmx = io.MouseDelta.x;
        float mx  = io.MousePos.x;

        if (s_drag_target == 1) {
            int nf = clamp_frame(x_to_frame(mx));
            if (nf != current) { current = nf; changed = true; }
        } else if (s_drag_target == 2 && exp_use_range) {
            exp_start = clamp_frame(x_to_frame(mx));
            if (exp_start > exp_end) exp_start = exp_end;
        } else if (s_drag_target == 3 && exp_use_range) {
            exp_end = clamp_frame(x_to_frame(mx));
            if (exp_end < exp_start) exp_end = exp_start;
        } else if (s_drag_target == 4) {
            tl.scroll -= dmx / tl.px_per_frame;
            tl.scroll  = std::clamp(tl.scroll, 0.0f, max_scroll);
        }
    }

    if (!active) s_drag_target = 0;

    // Scroll wheel: zoom around mouse position
    if (hovered && io.MouseWheel != 0.0f) {
        float mx        = io.MousePos.x;
        float fi_under  = x_to_frame(mx);   // frame index under mouse — keep stable
        float factor    = (io.MouseWheel > 0.0f) ? 1.18f : 1.0f / 1.18f;
        tl.px_per_frame = std::clamp(tl.px_per_frame * factor, 2.0f, track_w);
        // Re-anchor scroll so fi_under stays at mx
        tl.scroll = fi_under - (mx - track_x0) / tl.px_per_frame;
        tl.scroll = std::clamp(tl.scroll, 0.0f,
                               std::max(0.0f, float(N-1) - track_w / tl.px_per_frame));
    }

    // Tooltip on hover
    if (hovered && s_drag_target == 0) {
        int fi = clamp_frame(x_to_frame(io.MousePos.x));
        if (fi >= 0 && fi < N)
            ImGui::SetTooltip("%s", fmt_ts_long(timestamps[fi]).c_str());
    }

    // Right-click context menu: toggle export range
    if (hovered && ImGui::IsMouseClicked(1)) {
        ImGui::OpenPopup("##tl_ctx");
    }
    if (ImGui::BeginPopup("##tl_ctx")) {
        if (!exp_use_range) {
            if (ImGui::MenuItem("Set export range")) {
                exp_use_range = true;
                exp_start     = 0;
                exp_end       = N - 1;
            }
        } else {
            if (ImGui::MenuItem("Clear export range")) exp_use_range = false;
            ImGui::Separator();
            if (ImGui::MenuItem("Set start here")) {
                int fi = clamp_frame(x_to_frame(io.MousePos.x));
                exp_start = std::min(fi, exp_end);
            }
            if (ImGui::MenuItem("Set end here")) {
                int fi = clamp_frame(x_to_frame(io.MousePos.x));
                exp_end = std::max(fi, exp_start);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Fit all frames")) {
            tl.px_per_frame = auto_ppf;
            tl.scroll       = 0.0f;
        }
        ImGui::EndPopup();
    }

    // Thin separator line at top of timeline
    dl->AddLine({ wpos.x, wpos.y }, { wpos.x + w, wpos.y }, IM_COL32(80,80,110,200), 1.0f);

    // Current frame + timestamp label at right of ruler
    {
        std::string lbl = std::format("  fr {}  {}",
            current + 1, fmt_ts_long(timestamps[current]));
        ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
        float lx = wpos.x + w - ts.x - 8.0f;
        dl->AddText({ lx, ruler_y0 + 3.0f }, IM_COL32(220,220,255,220), lbl.c_str());
    }

    ImGui::End();
    ImGui::PopStyleVar(2);

    // Space = play/pause
    if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Space))
        playing = !playing;

    return changed;
}
