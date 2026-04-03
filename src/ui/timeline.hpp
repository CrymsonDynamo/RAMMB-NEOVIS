#pragma once
#include <vector>
#include <cstdint>

// Draws a timeline scrubber bar across the bottom of the viewport.
// Returns true if the user scrubbed to a new frame (out_frame is updated).
// Also returns true if play/pause was toggled via spacebar click area.
//
//  x, y        : top-left position in screen pixels
//  w, h        : dimensions (h ~= 52)
//  timestamps  : full frame list
//  current     : current frame index (in/out)
//  playing     : current play state (in/out)
bool timeline_draw(float x, float y, float w, float h,
                   const std::vector<int64_t>& timestamps,
                   int&  current,
                   bool& playing);
