#include "timeline.hpp"
#include <imgui.h>
#include <format>
#include <algorithm>
#include <cmath>

static std::string fmt_ts_long(int64_t ts) {
    std::string s = std::to_string(ts);
    if (s.size() < 12) return s;
    return std::format("{}-{}-{} {}:{} UTC",
        s.substr(0,4), s.substr(4,2), s.substr(6,2),
        s.substr(8,2), s.substr(10,2));
}

bool timeline_draw(float x, float y, float w, float h,
                   const std::vector<int64_t>& timestamps,
                   int&  current,
                   bool& playing) {
    if (timestamps.empty()) return false;

    bool changed = false;
    int  N       = int(timestamps.size());

    ImGui::SetNextWindowPos ({ x, y });
    ImGui::SetNextWindowSize({ w, h });
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.0f, 5.0f });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   { 4.0f, 3.0f });

    ImGui::Begin("##timeline", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      wpos = ImGui::GetWindowPos();
    float track_x0 = wpos.x + 8.0f;
    float track_x1 = wpos.x + w - 8.0f;
    float track_w  = track_x1 - track_x0;
    float track_y  = wpos.y + h - 14.0f;

    // ── Top row ───────────────────────────────────────────────────────────────
    ImGui::TextColored({ 0.20f, 0.85f, 0.40f, 1.0f },
        "%s", fmt_ts_long(timestamps[current]).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  frame %d / %d", current + 1, N);
    ImGui::SameLine(w - 130.0f);
    ImGui::TextDisabled("[Space] %s", playing ? "Pause" : "Play ");

    // ── Track background ──────────────────────────────────────────────────────
    float track_h = 8.0f;
    float bar_y0  = track_y - track_h * 0.5f;
    float bar_y1  = track_y + track_h * 0.5f;
    dl->AddRectFilled({ track_x0, bar_y0 }, { track_x1, bar_y1 },
                      IM_COL32(30, 30, 40, 255), 4.0f);

    // ── Fill ──────────────────────────────────────────────────────────────────
    float fill_pct = N > 1 ? float(current) / float(N - 1) : 0.0f;
    float fill_x   = track_x0 + fill_pct * track_w;
    dl->AddRectFilled({ track_x0, bar_y0 }, { fill_x, bar_y1 },
                      IM_COL32(51, 153, 255, 220), 4.0f);

    // ── Tick marks ────────────────────────────────────────────────────────────
    if (N <= 60) {
        for (int i = 0; i < N; ++i) {
            float fx = track_x0 + (N > 1 ? float(i) / float(N - 1) : 0.0f) * track_w;
            dl->AddLine({ fx, bar_y0 - 1.0f }, { fx, bar_y1 + 1.0f },
                        IM_COL32(100, 140, 200, 120), 1.0f);
        }
    }

    // ── Scrubber handle ───────────────────────────────────────────────────────
    float handle_x = track_x0 + fill_pct * track_w;
    float handle_r = 7.0f;
    dl->AddCircleFilled({ handle_x, track_y }, handle_r, IM_COL32(255, 255, 255, 240));
    dl->AddCircle      ({ handle_x, track_y }, handle_r, IM_COL32(51, 153, 255, 255), 0, 1.5f);

    // ── Invisible drag button over track ─────────────────────────────────────
    ImGui::SetCursorScreenPos({ track_x0, bar_y0 - handle_r });
    ImGui::InvisibleButton("##track", { track_w, track_h + handle_r * 2.0f });

    bool hovered  = ImGui::IsItemHovered();
    bool dragging = ImGui::IsItemActive();

    if (dragging) {
        float pct  = std::clamp((ImGui::GetIO().MousePos.x - track_x0) / track_w, 0.0f, 1.0f);
        int new_fr = std::clamp(int(std::round(pct * float(N - 1))), 0, N - 1);
        if (new_fr != current) { current = new_fr; changed = true; }
    }

    if (hovered && !dragging) {
        float pct = std::clamp((ImGui::GetIO().MousePos.x - track_x0) / track_w, 0.0f, 1.0f);
        int   fi  = std::clamp(int(std::round(pct * float(N - 1))), 0, N - 1);
        ImGui::SetTooltip("%s", fmt_ts_long(timestamps[fi]).c_str());
    }

    ImGui::End();
    ImGui::PopStyleVar(2);

    // Space = play/pause (only when imgui isn't eating keyboard)
    if (!ImGui::GetIO().WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Space))
        playing = !playing;

    return changed;
}
