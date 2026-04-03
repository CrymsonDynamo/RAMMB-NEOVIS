#pragma once
#include <GL/glew.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <cstdint>

#include "renderer/renderer.hpp"
#include "thread_pool.hpp"
#include <atomic>

// ── TileKey ───────────────────────────────────────────────────────────────────

struct TileKey {
    int zoom, row, col;
    bool operator==(const TileKey& o) const {
        return zoom == o.zoom && row == o.row && col == o.col;
    }
};

struct TileKeyHash {
    size_t operator()(const TileKey& k) const noexcept {
        // Simple hash combine
        size_t h = size_t(k.zoom) * 1000000007ULL;
        h ^= size_t(k.row) * 2654435761ULL;
        h ^= size_t(k.col) * 2246822519ULL;
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

    // Call when satellite/sector/product/timestamp changes
    void set_source(const std::string& satellite,
                    const std::string& sector,
                    const std::string& product,
                    int64_t            timestamp,
                    int                data_zoom);

    // Call every frame: uploads ready tiles, queues visible tile fetches
    // viewport_* are in world-space (same coords as Renderer)
    void update(float vp_x_min, float vp_x_max,
                float vp_y_min, float vp_y_max,
                Renderer& renderer);

    // Draw all loaded tiles for the current source
    void draw(Renderer& renderer);

    // Clear all tiles (e.g. on source change)
    void clear(Renderer& renderer);

    // Set download speed cap. limit_kbps=0 means unlimited.
    void set_throttle(int limit_kbps) { m_limit_kbps.store(limit_kbps); }

    int pending_downloads() const { return m_pool.pending(); }
    int loaded_tiles()      const { return int(m_tiles.size()); }
    int total_tiles()       const { return m_total_tiles; }

private:
    struct TileEntry {
        GLuint tex   = 0;
        bool   ready = false;
        bool   queued= false;
        bool   failed= false;
    };

    struct DownloadResult {
        TileKey              key;
        std::vector<uint8_t> rgba;    // decoded pixels
        int                  width{}, height{};
        bool                 ok{false};
    };

    void queue_tile(const TileKey& key);
    std::string build_url(const TileKey& key) const;
    TileQuad    tile_quad(const TileKey& key) const;

    // Source description
    std::string m_satellite;
    std::string m_sector;
    std::string m_product;
    int64_t     m_timestamp{0};
    int         m_data_zoom{0};

    // Tile storage
    std::unordered_map<TileKey, TileEntry, TileKeyHash> m_tiles;

    // Thread-safe result queue posted by worker threads
    std::mutex                   m_result_mutex;
    std::vector<DownloadResult>  m_results;

    std::atomic<int> m_limit_kbps{0}; // 0 = unlimited
    int              m_total_tiles{1}; // expected tile count at current zoom

    ThreadPool m_pool;
};
