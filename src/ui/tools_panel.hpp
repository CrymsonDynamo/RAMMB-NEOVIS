#pragma once
#include "sidebar.hpp"
#include <functional>
#include <glm/glm.hpp>

using ScreenFromWorld = std::function<glm::vec2(glm::vec2)>;

// Draw the right-side tile tools panel and (when active) the tile grid overlay
// directly on the viewport imagery.
// svp_*: tile render viewport in screen coords (sidebar_w, 0, vp_w, render_h).
// w2s: converts world-space coords to screen-space.
void tools_panel_draw(ViewState& state,
                      float svp_x, float svp_y, float svp_w, float svp_h,
                      const ScreenFromWorld& w2s);
