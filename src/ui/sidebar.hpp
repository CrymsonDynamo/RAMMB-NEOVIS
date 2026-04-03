#pragma once
#include <string>
#include <cstdint>

// All mutable state the sidebar reads/writes
struct ViewState {
    // ── Source selection ──────────────────────────────────────────────────────
    std::string satellite = "goes-19";
    std::string sector    = "full_disk";
    std::string product   = "geocolor";

    // ── Data resolution (decoupled from view zoom) ────────────────────────────
    // data_zoom: which tile zoom level to download (0=16km … 5=0.5km)
    // Clamped to what the selected product supports
    int data_zoom = 0;

    // ── Current timestamp (set by app after fetching metadata) ───────────────
    int64_t     timestamp     = 0;
    std::string timestamp_str;         // "YYYY-MM-DD HH:MM:SS UTC"

    // ── Animation (UI placeholders, wired up in a later phase) ───────────────
    int   num_frames  = 12;
    int   time_step   = 10;  // minutes
    bool  playing     = false;
    float speed       = 1.0f; // playback speed multiplier

    // ── Settings ──────────────────────────────────────────────────────────────
    // Download speed limit in KB/s. 0 = unlimited.
    int download_limit_kbps = 0;
    // Number of concurrent download threads (1-32)
    int download_threads = 16;

    // ── Dirty flags (cleared by app after handling) ───────────────────────────
    bool source_changed   = false;  // sat/sector/product changed
    bool zoom_changed     = false;  // data_zoom changed
    bool refresh_request  = false;  // user hit "Refresh"
};

// Draws the left sidebar and updates state. Returns sidebar pixel width.
float sidebar_draw(ViewState& state, float window_height);
