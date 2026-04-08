#pragma once
#include <string>
#include <vector>
#include "sidebar.hpp"

// One "scene" = a saved ViewState snapshot the user can switch between.
struct Scene {
    std::string name;
    ViewState   state;
};

struct SceneBar {
    std::vector<Scene> scenes;
    int active = 0;        // index into scenes
    bool settings_open = false;

    // App-level settings that don't belong in per-scene ViewState
    bool  vsync              = true;
    bool  dark_ui            = true;
    int   cache_limit_mb     = 512;   // in-memory tile cache cap
    bool  auto_reload        = false; // reload source on timer
    int   auto_reload_s      = 300;   // seconds between auto-reloads
    int   download_limit_kbps = 0;
    int   download_threads    = 4;

    // File persistence
    std::string current_file;          // path to open .rnvs, empty = unsaved
    bool save_requested    = false;    // Ctrl+S or Save button
    bool save_as_requested = false;    // Ctrl+Shift+S or Save As button
    bool open_requested    = false;    // Ctrl+O or Open button
};

// Draws the scene tab bar + settings panel.
// bar_h: height of the bar in pixels (output — passed to caller to offset viewport).
// Returns the bar height so caller can push viewport down.
float scene_bar_draw(SceneBar& bar, ViewState& active_state, float win_w, float win_h);
