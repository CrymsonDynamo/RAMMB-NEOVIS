#pragma once
#include <GL/glew.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include "renderer/renderer.hpp"
#include "thread_pool.hpp"
#include "overlay_defs.hpp"

// Fetches and caches RAMMB Slider map overlay tiles (country borders, states, etc.)
// rendered in the same tile-coordinate system as the satellite imagery.
//
// Tile URL:  https://slider.cira.colostate.edu/data/maps/{sat}/{sec}/{overlay}/{color}/{ts}/{zoom:02d}/{row:03d}_{col:03d}.png
// Timestamp: https://slider.cira.colostate.edu/data/json/{sat}/{sec}/maps/{overlay}/{color}/latest_times_all.json
class OverlayManager {
public:
    explicit OverlayManager(int n_threads = 2);
    ~OverlayManager();
    OverlayManager(const OverlayManager&) = delete;
    OverlayManager& operator=(const OverlayManager&) = delete;

    // Call when satellite/sector/data_zoom changes (after clear()).
    void set_source(const std::string& satellite, const std::string& sector, int zoom);

    // Upload ready tiles, queue visible tiles for enabled layers.
    // Call every frame from App::update().
    void update(float vp_x_min, float vp_x_max,
                float vp_y_min, float vp_y_max,
                const std::array<OverlaySettings, OVERLAY_COUNT>& settings,
                Renderer& renderer);

    // Draw all enabled overlays on top of imagery. Call from App::render()
    // immediately after TileManager::draw().
    void draw(const std::array<OverlaySettings, OVERLAY_COUNT>& settings,
              Renderer& renderer);

    // Free all GPU textures and reset state. Call before set_source() on source changes.
    void clear(Renderer& renderer);

private:
    // Tile cache key — layer index + spatial coords (no frame_idx; overlays are static).
    struct OvKey {
        int layer, zoom, row, col;
        bool operator==(const OvKey& o) const {
            return layer == o.layer && zoom == o.zoom && row == o.row && col == o.col;
        }
    };
    struct OvKeyHash {
        size_t operator()(const OvKey& k) const noexcept {
            size_t h = size_t(k.layer) * 5000000029ULL;
            h ^= size_t(k.zoom) * 1000000007ULL;
            h ^= size_t(k.row)  * 2654435761ULL;
            h ^= size_t(k.col)  * 2246822519ULL;
            return h;
        }
    };

    struct TileEntry { GLuint tex = 0; bool ready = false; bool queued = false; bool failed = false; };
    struct TileResult { OvKey key; std::vector<uint8_t> rgba; int w{}, h{}; bool ok{false}; uint32_t gen{0}; };
    struct TsResult   { int layer_idx; int color_idx; int64_t timestamp; bool ok; uint32_t gen; };

    // Per-layer state machine:
    //   IDLE → FETCHING_TS → READY → tiles downloaded
    //   IDLE → FETCHING_TS → FAILED (retry not automatic; re-enabled will retry)
    enum class LayerPhase { IDLE, FETCHING_TS, READY, FAILED };
    struct LayerData {
        LayerPhase phase     = LayerPhase::IDLE;
        int64_t    timestamp = 0;
        int        color_idx = -1;   // -1 = unset; used to detect color changes
    };

    void        fetch_timestamp(int layer_idx, int color_idx);
    void        queue_tile(int layer, int color_idx, int64_t ts, int zoom, int row, int col);
    void        free_layer_tiles(int layer_idx, Renderer& renderer);
    TileQuad    tile_quad(int zoom, int row, int col) const;
    std::string ts_url(int layer_idx, int color_idx) const;
    std::string tile_url(int layer_idx, int color_idx, int64_t ts,
                         int zoom, int row, int col) const;

    std::string m_satellite, m_sector;
    int         m_zoom{0};
    std::atomic<uint32_t> m_generation{0};

    std::array<LayerData, OVERLAY_COUNT> m_layers{};
    std::unordered_map<OvKey, TileEntry, OvKeyHash> m_tiles;

    mutable std::mutex        m_mutex;   // protects m_tile_results + m_ts_results
    std::vector<TileResult>   m_tile_results;
    std::vector<TsResult>     m_ts_results;

    ThreadPool m_pool;
};
