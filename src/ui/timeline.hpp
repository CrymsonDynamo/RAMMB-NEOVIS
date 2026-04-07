#pragma once
#include <vector>
#include <cstdint>

// Persistent state for the Blender-style timeline strip.
// Owned by App and passed each frame.
struct TimelineState {
    float px_per_frame = 0.0f;  // pixels per frame; 0.0 = auto-fit all frames
    float scroll       = 0.0f;  // leftmost visible frame index (fractional ok)
};

// Draws the timeline strip.
//  x, y, w, h   : screen-space rect
//  timestamps    : full frame list
//  current       : current frame index (in/out — scrubbing updates it)
//  playing       : play state (in/out — space-bar toggles)
//  tl            : zoom/scroll state (in/out)
//  exp_use_range : whether an export range band is shown (in/out)
//  exp_start/end : export range frame indices, clamped to [0, N-1] (in/out)
//
// Returns true if the current frame index changed.
bool timeline_draw(float x, float y, float w, float h,
                   const std::vector<int64_t>& timestamps,
                   int&           current,
                   bool&          playing,
                   TimelineState& tl,
                   bool&          exp_use_range,
                   int&           exp_start,
                   int&           exp_end);
