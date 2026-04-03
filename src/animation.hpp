#pragma once
#include <vector>
#include <cstdint>

class AnimationController {
public:
    enum class Mode { Loop, Rock, Once };

    void set_frames(const std::vector<int64_t>& timestamps);
    void clear();

    // Playback
    void play()        { if (frame_count() > 1) m_playing = true; }
    void pause()       { m_playing = false; }
    void toggle_play() { m_playing ? pause() : play(); }
    bool playing()     const { return m_playing; }

    // Navigation
    void step(int delta);   // +1 / -1, wraps per mode
    void jump(int frame);   // clamp to [0, N-1]

    // State
    int     current_frame()     const { return m_current; }
    int64_t current_timestamp() const;
    int     frame_count()       const { return int(m_timestamps.size()); }
    bool    empty()             const { return m_timestamps.empty(); }

    const std::vector<int64_t>& timestamps() const { return m_timestamps; }

    // Settings
    void  set_fps (float fps) { m_fps  = fps > 0 ? fps : 1.0f; }
    void  set_mode(Mode m)    { m_mode = m; }
    Mode  mode()  const { return m_mode; }
    float fps()   const { return m_fps; }

    // Call once per frame with wall-clock delta seconds
    void update(float dt);

private:
    std::vector<int64_t> m_timestamps;
    int   m_current {0};
    bool  m_playing {false};
    bool  m_forward {true};   // rock mode direction
    float m_accum   {0.0f};   // time accumulator for frame advance
    float m_fps     {8.0f};
    Mode  m_mode    {Mode::Loop};
};
