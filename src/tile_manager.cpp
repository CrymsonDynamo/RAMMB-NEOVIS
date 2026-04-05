#include "tile_manager.hpp"
#include <stb_image.h>
#include <curl/curl.h>
#include <format>
#include <iostream>
#include <algorithm>

// ── libcurl helper ────────────────────────────────────────────────────────────
//
// IMPORTANT: We use ONE persistent CURL handle per worker thread (thread_local).
// Previously the code called curl_easy_init()/curl_easy_cleanup() for every
// tile download, which:
//   1. Opened a brand-new TCP connection per tile (no HTTP keep-alive)
//   2. Left each closed socket in TIME_WAIT for ~60s
//   3. With 16 threads × many tiles, exhausted OS ephemeral ports and
//      overwhelmed the NIC — killing the host's internet connectivity.
//
// With thread-local handles, each worker thread reuses one long-lived connection
// to slider.cira.colostate.edu. libcurl handles HTTP keep-alive automatically.
// The total number of concurrent TCP connections equals the thread count (≤4 by
// default), instead of potentially hundreds of short-lived ones.

static size_t write_cb(void* data, size_t size, size_t nmemb, void* user) {
    auto* vec = static_cast<std::vector<uint8_t>*>(user);
    auto* ptr = static_cast<const uint8_t*>(data);
    size_t n  = size * nmemb;
    vec->insert(vec->end(), ptr, ptr + n);
    return n;
}

namespace {
    struct CurlHandleGuard {
        CURL* h;
        CurlHandleGuard() : h(curl_easy_init()) {
            if (!h) return;
            // Persistent options — survive between requests on this handle
            curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION,      1L);
            curl_easy_setopt(h, CURLOPT_USERAGENT,           "RAMMB-NEOVIS/0.1");
            curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING,     "");      // accept gzip
            curl_easy_setopt(h, CURLOPT_TIMEOUT,             20L);     // 20s total
            curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT,      8L);      // 8s connect
            curl_easy_setopt(h, CURLOPT_TCP_KEEPALIVE,       1L);      // keep-alive
            curl_easy_setopt(h, CURLOPT_TCP_KEEPIDLE,        30L);     // probe after 30s idle
            curl_easy_setopt(h, CURLOPT_DNS_CACHE_TIMEOUT,   120L);    // cache DNS 2 min
        }
        ~CurlHandleGuard() { if (h) curl_easy_cleanup(h); }
        CurlHandleGuard(const CurlHandleGuard&) = delete;
        CurlHandleGuard& operator=(const CurlHandleGuard&) = delete;
    };
} // namespace

static bool http_get(const std::string& url, std::vector<uint8_t>& out, int limit_kbps = 0) {
    thread_local CurlHandleGuard tl; // one handle per worker thread, lives until thread exits
    CURL* c = tl.h;
    if (!c) return false;

    out.clear();
    // Per-request options (only what changes between calls)
    curl_easy_setopt(c, CURLOPT_URL,                   url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,         write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,             &out);
    curl_easy_setopt(c, CURLOPT_MAX_RECV_SPEED_LARGE,
                     limit_kbps > 0 ? curl_off_t(limit_kbps) * 1024 : curl_off_t(0));

    CURLcode res  = curl_easy_perform(c);
    long     code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    // Do NOT call curl_easy_cleanup — handle is reused for next tile on this thread
    return (res == CURLE_OK && code == 200);
}

// ── TileManager ───────────────────────────────────────────────────────────────

TileManager::TileManager(int n_threads) : m_pool(n_threads) {}
TileManager::~TileManager() = default;

void TileManager::set_frames(const std::string& satellite,
                              const std::string& sector,
                              const std::string& product,
                              const std::vector<int64_t>& timestamps,
                              int data_zoom) {
    m_satellite  = satellite;
    m_sector     = sector;
    m_product    = product;
    m_timestamps = timestamps;
    m_data_zoom  = data_zoom;

    int N = 1 << data_zoom;
    // total_tiles is recomputed in update() once the selection mask is known;
    // initialise to full grid for now.
    m_total_tiles = N * N * std::max(1, int(timestamps.size()));
    m_loaded_count.store(0);
}

void TileManager::set_source(const std::string& satellite,
                              const std::string& sector,
                              const std::string& product,
                              int64_t timestamp,
                              int data_zoom) {
    set_frames(satellite, sector, product, { timestamp }, data_zoom);
}

void TileManager::clear(Renderer& renderer) {
    // Drop queued-but-not-started tile downloads immediately.
    // Without this, switching products leaves hundreds of stale tasks in the
    // pool that block new tiles from downloading → blank screen.
    m_pool.clear_queue();

    // Bump generation so any in-flight results from old source are discarded.
    m_generation.fetch_add(1);

    for (auto& [k, e] : m_tiles)
        if (e.tex) renderer.free_texture(e.tex);
    m_tiles.clear();
    m_loaded_count.store(0);
    m_failed_count.store(0);

    std::lock_guard lock(m_result_mutex);
    m_results.clear();
}

void TileManager::update(float vp_x_min, float vp_x_max,
                         float vp_y_min, float vp_y_max,
                         int   current_frame,
                         Renderer& renderer) {
    // ── Upload finished downloads ─────────────────────────────────────────────
    {
        std::vector<DownloadResult> ready;
        {
            std::lock_guard lock(m_result_mutex);
            std::swap(ready, m_results);
        }
        for (auto& r : ready) {
            // Discard results from a previous source generation
            if (r.generation != m_generation.load()) continue;
            auto it = m_tiles.find(r.key);
            if (it == m_tiles.end()) continue;
            if (!r.ok) { it->second.failed = true; m_failed_count.fetch_add(1); continue; }
            it->second.tex   = renderer.upload_texture(r.rgba.data(), r.width, r.height);
            it->second.ready = true;
            m_loaded_count.fetch_add(1);
        }
    }

    if (m_timestamps.empty()) return;

    int N = int(m_timestamps.size());

    // Recompute total_tiles to reflect only the selected tiles so the progress
    // bar shows "9/9 ready" instead of "9/20 ready" when a mask is active.
    {
        int T = 1 << m_data_zoom;
        bool has_mask = (int(m_tile_selection.size()) == T * T);
        int sel_per_frame = T * T;
        if (has_mask) {
            sel_per_frame = 0;
            for (int i = 0; i < T * T; ++i)
                if (m_tile_selection[i]) ++sel_per_frame;
        }
        m_total_tiles = std::max(1, sel_per_frame * N);
    }

    // Priority 1: current frame — enqueued first so its tiles download soonest
    request_frame(current_frame, vp_x_min, vp_x_max, vp_y_min, vp_y_max);

    // Priority 2: all remaining frames, in playback order.
    // Once a tile is marked queued it won't be re-enqueued, so this is cheap
    // on subsequent frames. Tiles stay queued until the source changes.
    for (int i = 1; i < N; ++i)
        request_frame((current_frame + i) % N, vp_x_min, vp_x_max, vp_y_min, vp_y_max);
}

void TileManager::request_frame(int frame_idx,
                                float vp_x_min, float vp_x_max,
                                float vp_y_min, float vp_y_max) {
    int T = 1 << m_data_zoom;
    bool has_mask = (int(m_tile_selection.size()) == T * T);

    for (int r = 0; r < T; ++r) {
        for (int c = 0; c < T; ++c) {
            // Skip tiles deselected in the region selector
            if (has_mask && !m_tile_selection[r * T + c])
                continue;

            float tx_min = float(c)     / float(T) - 0.5f;
            float tx_max = float(c + 1) / float(T) - 0.5f;
            float ty_min = 0.5f - float(r + 1) / float(T);
            float ty_max = 0.5f - float(r)     / float(T);

            if (tx_max < vp_x_min || tx_min > vp_x_max ||
                ty_max < vp_y_min || ty_min > vp_y_max)
                continue;

            TileKey key{ frame_idx, m_data_zoom, r, c };
            auto [it, inserted] = m_tiles.emplace(key, TileEntry{});
            if (inserted || (!it->second.ready && !it->second.queued && !it->second.failed)) {
                it->second.queued = true;
                queue_tile(key);
            }
        }
    }
}

void TileManager::prefetch_all() {
    int N = int(m_timestamps.size());
    // Use full-image world bounds so all tiles are queued regardless of viewport
    for (int fi = 0; fi < N; ++fi)
        request_frame(fi, -0.5f, 0.5f, -0.5f, 0.5f);
}

void TileManager::draw(int frame_idx, Renderer& renderer) {
    for (auto& [key, entry] : m_tiles) {
        if (key.frame_idx != frame_idx) continue;
        if (!entry.ready || !entry.tex) continue;
        renderer.draw_tile(entry.tex, tile_quad(key));
    }
}

int TileManager::loaded_for_frame(int frame_idx) const {
    int count = 0;
    std::lock_guard lock(m_result_mutex);
    for (auto& [key, entry] : m_tiles)
        if (key.frame_idx == frame_idx && entry.ready) ++count;
    return count;
}

std::vector<std::pair<GLuint, TileQuad>> TileManager::ready_tiles_for_frame(int frame_idx) const {
    std::vector<std::pair<GLuint, TileQuad>> out;
    std::lock_guard lock(m_result_mutex);
    for (auto& [key, entry] : m_tiles) {
        if (key.frame_idx == frame_idx && entry.ready && entry.tex)
            out.emplace_back(entry.tex, tile_quad(key));
    }
    return out;
}

void TileManager::queue_tile(const TileKey& key) {
    std::string url   = build_url(key);
    TileKey     ckey  = key;
    int         limit = m_limit_kbps.load();
    uint32_t    gen   = m_generation.load();

    m_pool.enqueue([this, url, ckey, limit, gen]() mutable {
        // Early-out: if generation changed, source was switched — discard work
        if (gen != m_generation.load()) return;

        DownloadResult res;
        res.key        = ckey;
        res.generation = gen;

        std::vector<uint8_t> png_buf;
        if (!http_get(url, png_buf, limit)) {
            res.ok = false;
            std::lock_guard lock(m_result_mutex);
            m_results.push_back(std::move(res));
            return;
        }

        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* px = stbi_load_from_memory(
            png_buf.data(), int(png_buf.size()), &w, &h, &ch, 4);

        if (!px) {
            res.ok = false;
        } else {
            res.ok     = true;
            res.width  = w;
            res.height = h;
            res.rgba.assign(px, px + w * h * 4);
            stbi_image_free(px);
        }

        std::lock_guard lock(m_result_mutex);
        m_results.push_back(std::move(res));
    });
}

std::string TileManager::build_url(const TileKey& key) const {
    int64_t     ts   = m_timestamps[key.frame_idx];
    std::string ts_s = std::to_string(ts);
    std::string yyyy = ts_s.substr(0, 4);
    std::string mm   = ts_s.substr(4, 2);
    std::string dd   = ts_s.substr(6, 2);

    return std::format(
        "https://slider.cira.colostate.edu/data/imagery/{}/{}/{}/{}---{}/{}/{}/{:02d}/{:03d}_{:03d}.png",
        yyyy, mm, dd,
        m_satellite, m_sector, m_product,
        ts_s,
        key.zoom, key.row, key.col);
}

TileQuad TileManager::tile_quad(const TileKey& key) const {
    int   N     = 1 << key.zoom;
    float x_min = float(key.col)     / float(N) - 0.5f;
    float x_max = float(key.col + 1) / float(N) - 0.5f;
    float y_min = 0.5f - float(key.row + 1) / float(N);
    float y_max = 0.5f - float(key.row)     / float(N);
    return { x_min, x_max, y_min, y_max };
}
