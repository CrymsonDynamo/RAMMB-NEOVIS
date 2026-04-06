#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <array>
#include "../overlay_defs.hpp"

// Date range for archive queries
struct DateRange {
    int start_year{2026}, start_month{4}, start_day{1};
    int start_hour{0},    start_min{0};
    int end_year{2026},   end_month{4}, end_day{1};
    int end_hour{23},     end_min{59};
    bool use_range{false};  // false = just use latest N frames
};

// All mutable state the sidebar reads/writes
struct ViewState {
    // ── Source selection ──────────────────────────────────────────────────────
    std::string satellite = "goes-19";
    std::string sector    = "full_disk";
    std::string product   = "geocolor";

    // ── Data resolution (decoupled from view zoom) ────────────────────────────
    int data_zoom = 0;

    // ── Frame list (set by App after fetching metadata) ───────────────────────
    std::vector<int64_t> frame_timestamps;  // all available timestamps for current source
    std::string          timestamp_str;     // display string for current frame

    // ── Animation ────────────────────────────────────────────────────────────
    int   num_frames    = 12;    // how many frames to load (latest-N mode)
    int   time_step     = 10;   // minutes between frames
    float fps           = 8.0f; // playback speed in frames/sec
    bool  playing       = false;
    int   loop_mode     = 0;    // 0=loop 1=rock 2=once
    int   current_frame = 0;    // set by AnimationController, read by UI

    // ── Date range ────────────────────────────────────────────────────────────
    DateRange date_range;

    // ── Tile region selection ────────────────────────────────────────────────
    // 2D bitmask of which tiles (row,col) to download at the current data_zoom.
    // When empty, ALL tiles are downloaded (default behavior).
    // Indexed as tile_selection[row * (1<<data_zoom) + col].
    std::vector<bool> tile_selection;
    bool              tile_select_all  = true;  // true = download all, ignore mask
    bool              tools_panel_open = false;  // right-side panel visibility

    // ── Settings ──────────────────────────────────────────────────────────────
    int download_limit_kbps = 0;
    int download_threads    = 4;

    // ── Available timestamps for date-range time pickers ─────────────────────
    std::vector<int64_t> avail_start_times;   // all timestamps for the start date
    std::vector<int64_t> avail_end_times;     // all timestamps for the end date
    bool request_start_times = false;         // ask App to fetch avail_start_times
    bool request_end_times   = false;         // ask App to fetch avail_end_times
    int  start_time_sel      = 0;             // selected index in avail_start_times
    int  end_time_sel        = 0;             // selected index in avail_end_times

    // ── Overlays ──────────────────────────────────────────────────────────────
    std::array<OverlaySettings, OVERLAY_COUNT> overlays{};

    // ── Dirty flags (cleared by App after handling) ───────────────────────────
    bool source_changed   = false;
    bool zoom_changed     = false;
    bool refresh_request  = false;
    bool range_changed    = false;  // date range or num_frames changed
    bool export_requested = false;  // user clicked Export button
};

// Draws the left sidebar and updates state. Returns sidebar pixel width.
// y_offset: pixels from top of window to start (e.g. scene bar height).
float sidebar_draw(ViewState& state, float window_height, float y_offset = 0.0f);
