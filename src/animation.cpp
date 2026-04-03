#include "animation.hpp"
#include <algorithm>

void AnimationController::set_frames(const std::vector<int64_t>& timestamps) {
    m_timestamps = timestamps;
    m_current    = 0;
    m_accum      = 0.0f;
    m_forward    = true;
    if (m_timestamps.size() <= 1) m_playing = false;
}

void AnimationController::clear() {
    m_timestamps.clear();
    m_current = 0;
    m_playing = false;
    m_accum   = 0.0f;
}

int64_t AnimationController::current_timestamp() const {
    if (m_timestamps.empty()) return 0;
    return m_timestamps[std::clamp(m_current, 0, int(m_timestamps.size()) - 1)];
}

void AnimationController::step(int delta) {
    if (m_timestamps.empty()) return;
    int N = int(m_timestamps.size());

    switch (m_mode) {
        case Mode::Loop:
        case Mode::Once:
            m_current = (m_current + delta % N + N) % N;
            break;
        case Mode::Rock:
            m_current = std::clamp(m_current + delta, 0, N - 1);
            break;
    }
    m_accum = 0.0f;
}

void AnimationController::jump(int frame) {
    if (m_timestamps.empty()) return;
    m_current = std::clamp(frame, 0, int(m_timestamps.size()) - 1);
    m_accum   = 0.0f;
}

void AnimationController::update(float dt) {
    if (!m_playing || m_timestamps.empty()) return;

    m_accum += dt;
    float period = 1.0f / m_fps;

    while (m_accum >= period) {
        m_accum -= period;
        int N = int(m_timestamps.size());

        switch (m_mode) {
            case Mode::Loop:
                m_current = (m_current + 1) % N;
                break;

            case Mode::Once:
                if (m_current < N - 1) ++m_current;
                else m_playing = false;
                break;

            case Mode::Rock:
                if (m_forward) {
                    if (m_current < N - 1) ++m_current;
                    else { m_forward = false; --m_current; }
                } else {
                    if (m_current > 0) --m_current;
                    else { m_forward = true; ++m_current; }
                }
                break;
        }
    }
}
