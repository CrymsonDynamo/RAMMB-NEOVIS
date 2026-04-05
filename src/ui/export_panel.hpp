#pragma once
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <string>
#include "../export/exporter.hpp"

// State owned by App, mutated by export_panel_draw()
struct ExportState {
    bool open         = false;
    bool crop_editing = false;   // true while crop box is shown/editable

    ExportSettings settings;
    CropRegion     crop;         // current crop in world space

    std::shared_ptr<ExportProgress> progress = std::make_shared<ExportProgress>();

    // Drag state for crop box handles
    bool  dragging_body    = false;
    bool  dragging_corner  = false;
    int   drag_corner_idx  = -1; // 0=TL 1=TR 2=BL 3=BR

    // Resolution preset: 0=Low 1=Medium 2=High 3=Custom
    int res_preset = 1;

    // Output directory / file path buffer for UI
    char  path_buf[512] = {};
};

using WorldFromScreen = std::function<glm::vec2(glm::vec2)>;
using ScreenFromWorld = std::function<glm::vec2(glm::vec2)>;

// Draw the export panel and crop overlay.
// Call each frame while state.open is true (panel opens/closes itself).
// vp_*: viewport screen rect (right of sidebar, above timeline+status)
// Returns true when an export was triggered.
bool export_panel_draw(ExportState&      state,
                       float vp_x, float vp_y, float vp_w, float vp_h,
                       int   total_frames,
                       const WorldFromScreen& s2w,
                       const ScreenFromWorld& w2s);
