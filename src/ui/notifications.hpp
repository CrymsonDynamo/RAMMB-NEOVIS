#pragma once
// Header-only notification toast system.
// Worker threads push via push_notif(). Main thread calls notifs_draw() each frame.

#include <imgui.h>
#include <string>
#include <deque>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstdio>

enum class NotifLevel { Info, Warn, Error };

struct Notif {
    std::string msg;
    NotifLevel  level = NotifLevel::Info;
    float       ttl   = 6.0f;
    bool        dead  = false;
};

struct NotifSystem {
    // Worker threads: push to incoming (mutex protected)
    std::mutex        push_mx;
    std::deque<Notif> incoming;

    // Main thread only: no lock needed
    std::deque<Notif> active;

    // Thread-safe push — safe to call from any thread
    void push(std::string msg, NotifLevel lv = NotifLevel::Info, float ttl = 6.0f) {
        std::lock_guard lk(push_mx);
        // Dedup: skip if same message already pending
        for (auto& n : incoming)
            if (n.msg == msg) return;
        incoming.push_back({ std::move(msg), lv, ttl });
    }

    // Main thread: move incoming → active, age, purge dead
    void tick(float dt) {
        {
            std::lock_guard lk(push_mx);
            for (auto& n : incoming) {
                // Also dedup against active
                bool dup = false;
                for (auto& a : active)
                    if (!a.dead && a.msg == n.msg) { dup = true; break; }
                if (!dup) {
                    active.push_back(std::move(n));
                    if (active.size() > 5) active.front().dead = true; // cap at 5
                }
            }
            incoming.clear();
        }
        for (auto& n : active) {
            n.ttl -= dt;
            if (n.ttl <= 0.0f) n.dead = true;
        }
        active.erase(
            std::remove_if(active.begin(), active.end(), [](const Notif& n){ return n.dead; }),
            active.end());
    }
};

inline NotifSystem& g_notifs() {
    static NotifSystem inst;
    return inst;
}

// Call from any thread
inline void push_notif(std::string msg, NotifLevel lv = NotifLevel::Info, float ttl = 6.0f) {
    g_notifs().push(std::move(msg), lv, ttl);
}

// ── Rendering ─────────────────────────────────────────────────────────────────
// Call once per frame in update() to age notifications.
inline void notifs_tick(float dt) { g_notifs().tick(dt); }

// Call once per frame in render() after ImGui::NewFrame().
// bottom_offset: pixels to keep clear at the bottom (status bar + timeline).
inline void notifs_draw(float win_w, float win_h, float bottom_offset = 0.0f) {
    auto& active = g_notifs().active;
    if (active.empty()) return;

    constexpr float W      = 320.0f;
    constexpr float CARD_H = 44.0f;
    constexpr float GAP    =  6.0f;
    constexpr float PAD    = 14.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   { 9.0f, 7.0f });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);

    int N = int(active.size());
    for (int i = 0; i < N; ++i) {
        auto& n = active[i];

        // Slot: newest (highest index) goes at the bottom
        float slot = float(N - i);   // 1 = bottom-most, N = top-most
        float cy = win_h - bottom_offset - PAD - slot * (CARD_H + GAP);
        float cx = win_w - W - PAD;

        // Alpha: fade in during first 0.3s of TTL from 6→5.7, fade out last 1s
        float alpha = std::min(n.ttl, 1.0f);
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        ImVec4 bg, border_col, icon_col;
        const char* icon;
        switch (n.level) {
            case NotifLevel::Warn:
                bg         = { 0.16f, 0.13f, 0.04f, 0.96f * alpha };
                border_col = { 1.00f, 0.70f, 0.10f, 1.0f  * alpha };
                icon_col   = { 1.00f, 0.80f, 0.20f, 1.0f  * alpha };
                icon       = "(!)" ;
                break;
            case NotifLevel::Error:
                bg         = { 0.18f, 0.05f, 0.05f, 0.96f * alpha };
                border_col = { 1.00f, 0.30f, 0.20f, 1.0f  * alpha };
                icon_col   = { 1.00f, 0.40f, 0.30f, 1.0f  * alpha };
                icon       = "(X)";
                break;
            default:
                bg         = { 0.07f, 0.11f, 0.20f, 0.96f * alpha };
                border_col = { 0.20f, 0.60f, 1.00f, 1.0f  * alpha };
                icon_col   = { 0.30f, 0.70f, 1.00f, 1.0f  * alpha };
                icon       = "(i)";
                break;
        }

        ImGui::SetNextWindowPos ({ cx, cy }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ W, CARD_H }, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(bg.w);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Border,   border_col);

        ImGui::PushID(i);
        char wid[32]; snprintf(wid, sizeof(wid), "##notif%d", i);
        ImGui::Begin(wid, nullptr,
            ImGuiWindowFlags_NoDecoration    | ImGuiWindowFlags_NoMove        |
            ImGuiWindowFlags_NoNav           | ImGuiWindowFlags_NoScrollbar   |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoInputs);

        // Icon
        float base_y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(base_y + 2.0f);
        ImGui::TextColored(icon_col, "%s", icon);
        ImGui::SameLine(0, 8);

        // Message (two lines if needed, truncated)
        ImGui::SetCursorPosY(base_y);
        float msg_w = W - 68.0f; // leave room for icon + X button
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + msg_w);
        ImGui::TextColored({ 1.0f, 1.0f, 1.0f, alpha }, "%s", n.msg.c_str());
        ImGui::PopTextWrapPos();

        // X close button (right side, vertically centred)
        ImGui::SetCursorPos({ W - 28.0f, (CARD_H - ImGui::GetTextLineHeight() - 6.0f) * 0.5f });
        ImGui::PushStyleColor(ImGuiCol_Button,        { 0.3f,0.3f,0.3f,0.5f*alpha });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.6f,0.2f,0.2f,0.8f*alpha });
        if (ImGui::SmallButton("X")) n.dead = true;
        ImGui::PopStyleColor(2);

        ImGui::End();
        ImGui::PopID();
        ImGui::PopStyleColor(2);
    }

    ImGui::PopStyleVar(3);
}
