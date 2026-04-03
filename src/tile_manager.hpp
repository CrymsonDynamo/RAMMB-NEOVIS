#pragma once
#include <GL/glew.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>
#include <atomic>
#include <utility>

#include "renderer/renderer.hpp"
#include "thread_pool.hpp"

// ── TileKey ───────────────────────────────────────────────────────────────────

struct TileKey {
    int frame_idx;          // index into the frame list
    int zoom, row, col;
    bool operator==(const TileKey& o) const {
        return frame_idx == o.frame_idx &&
               zoom == o.zoom && row == o.row && col == o.col;
    }
};

struct TileKeyHash {
    size_t operator()(const TileKey& k) const noexcept {
        size_t h = size_t(k.frame_idx) * 4000000007ULL;
        h ^= size_t(k.zoom) * 1000000007ULL;
        h ^= size_t(k.row)  * 2654435761ULL;
        h ^= size_t(k.col)  * 2246822519ULL;
        return h;
    }
};

// ── TileManager ───────────────────────────────────────────────────────────────

class TileManager {
public:
    explicit TileManager(int n_threads = 16);
    ~TileManager();

    TileManager(const TileManager&) = delete;
    TileManager& operator=(const TileManager&) = delete;

    // Set source and full frame list. Clears existing tiles.
    void set_frames(const std::string& satellite,
                    const std::string& sector,
                    const std::string& product,
                    const std::vector<int64_t>& timestamps,
                    int data_zoom);

    // Single-frame convenience (wraps set_frames with one timestamp)
    void set_source(const std::string& satellite,
                    const std::string& sector,
                    const std::string& product,
                    int64_t            timestamp,
                    int                data_zoom);

    // Call every frame: uploads ready tiles, queues visible tile fetches.
    // current_frame is prioritized; adjacent frames are background-loaded.
    void update(float vp_x_min, float vp_x_max,
                float vp_y_min, float vp_y_max,
                int   current_frame,
                Renderer& renderer);

    // Draw loaded tiles for the given frame
    void draw(int frame_idx, Renderer& renderer);

    // Free all GPU textures and clear tile map
    void clear(Renderer& renderer);

    // Download speed cap (0 = unlimited, hot-swappable)
    void set_throttle(int limit_kbps) { m_limit_kbps.store(limit_kbps); }

    // Stats
    int pending_downloads()  const { return m_pool.pending(); }
    int loaded_tiles()       const { return m_loaded_count.load(); }
    int total_tiles()        const { return m_total_tiles; }
    int frame_count()        const { return int(m_timestamps.size()); }

    // How many tiles are loaded for a specific frame (for per-frame progress)
    int loaded_for_frame(int frame_idx) const;

    // Return all ready (tex, quad) pairs for a frame — used by export
    std::vector<std::pair<GLuint, TileQuad>> ready_tiles_for_frame(int frame_idx) const;

private:
    struct TileEntry {
        GLuint tex    = 0;
        bool   ready  = false;
        bool   queued = false;
        bool   failed = false;
    };

    struct DownloadResult {
        TileKey              key;
        std::vector<uint8_t> rgba;
        int                  width{}, height{};
        bool                 ok{false};
    };

    void        queue_tile(const TileKey& key);
    std::string build_url (const TileKey& key) const;
    TileQuad    tile_quad (const TileKey& key) const;

    // Enqueue all visible tiles for a given frame at current viewport
    void request_frame(int frame_idx,
                       float vp_x_min, float vp_x_max,
                       float vp_y_min, float vp_y_max);

    // Source
    std::string            m_satellite;
    std::string            m_sector;
    std::string            m_product;
    std::vector<int64_t>   m_timestamps;
    int                    m_data_zoom{0};

    // Tile storage
    std::unordered_map<TileKey, TileEntry, TileKeyHash> m_tiles;

    // Thread-safe result queue
    mutable std::mutex           m_result_mutex;
    std::vector<DownloadResult>  m_results;

    std::atomic<int> m_limit_kbps  {0};
    std::atomic<int> m_loaded_count {0};
    int              m_total_tiles  {1};

    ThreadPool m_pool;
};
