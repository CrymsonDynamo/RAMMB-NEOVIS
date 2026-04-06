#include "overlay_manager.hpp"
#include <stb_image.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <format>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

// ── libcurl thread-local handle (same pattern as tile_manager.cpp) ────────────

static size_t ov_write(void* data, size_t sz, size_t nmemb, void* user) {
    auto* v = static_cast<std::vector<uint8_t>*>(user);
    auto* p = static_cast<const uint8_t*>(data);
    v->insert(v->end(), p, p + sz * nmemb);
    return sz * nmemb;
}

namespace {
    struct CurlGuard {
        CURL* h;
        CurlGuard() : h(curl_easy_init()) {
            if (!h) return;
            curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION,    1L);
            curl_easy_setopt(h, CURLOPT_USERAGENT,         "RAMMB-NEOVIS/0.1");
            curl_easy_setopt(h, CURLOPT_TIMEOUT,           20L);
            curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT,    8L);
            curl_easy_setopt(h, CURLOPT_TCP_KEEPALIVE,     1L);
            curl_easy_setopt(h, CURLOPT_DNS_CACHE_TIMEOUT, 120L);
        }
        ~CurlGuard() { if (h) curl_easy_cleanup(h); }
        CurlGuard(const CurlGuard&) = delete;
        CurlGuard& operator=(const CurlGuard&) = delete;
    };
}

static bool ov_http_get(const std::string& url, std::vector<uint8_t>& out) {
    thread_local CurlGuard tl;
    if (!tl.h) return false;
    out.clear();
    curl_easy_setopt(tl.h, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(tl.h, CURLOPT_WRITEFUNCTION, ov_write);
    curl_easy_setopt(tl.h, CURLOPT_WRITEDATA,     &out);
    CURLcode res = curl_easy_perform(tl.h);
    long code = 0;
    curl_easy_getinfo(tl.h, CURLINFO_RESPONSE_CODE, &code);
    return res == CURLE_OK && code == 200;
}

// ── OverlayManager ────────────────────────────────────────────────────────────

OverlayManager::OverlayManager(int n_threads) : m_pool(n_threads) {}
OverlayManager::~OverlayManager() = default;

void OverlayManager::set_source(const std::string& sat, const std::string& sec, int zoom) {
    m_satellite = sat;
    m_sector    = sec;
    m_zoom      = zoom;
    // clear() must be called first by App to free GPU resources before calling set_source()
}

void OverlayManager::clear(Renderer& renderer) {
    m_pool.clear_queue();
    m_generation.fetch_add(1);
    for (auto& [k, t] : m_tiles)
        if (t.tex) renderer.free_texture(t.tex);
    m_tiles.clear();
    for (auto& l : m_layers) {
        l.phase     = LayerPhase::IDLE;
        l.timestamp = 0;
        l.color_idx = -1;
    }
    std::lock_guard lock(m_mutex);
    m_tile_results.clear();
    m_ts_results.clear();
}

void OverlayManager::free_layer_tiles(int layer_idx, Renderer& renderer) {
    auto it = m_tiles.begin();
    while (it != m_tiles.end()) {
        if (it->first.layer == layer_idx) {
            if (it->second.tex) renderer.free_texture(it->second.tex);
            it = m_tiles.erase(it);
        } else {
            ++it;
        }
    }
    // Discard any queued results for this layer
    std::lock_guard lock(m_mutex);
    m_tile_results.erase(
        std::remove_if(m_tile_results.begin(), m_tile_results.end(),
                       [layer_idx](const TileResult& r){ return r.key.layer == layer_idx; }),
        m_tile_results.end());
}

TileQuad OverlayManager::tile_quad(int zoom, int row, int col) const {
    int   N     = 1 << zoom;
    float x_min = float(col)     / float(N) - 0.5f;
    float x_max = float(col + 1) / float(N) - 0.5f;
    float y_min = 0.5f - float(row + 1) / float(N);
    float y_max = 0.5f - float(row)     / float(N);
    return { x_min, x_max, y_min, y_max };
}

std::string OverlayManager::ts_url(int layer_idx, int color_idx) const {
    return std::format(
        "https://slider.cira.colostate.edu/data/json/{}/{}/maps/{}/{}/latest_times_all.json",
        m_satellite, m_sector,
        OVERLAY_DEFS[layer_idx].key, OVERLAY_COLORS[color_idx]);
}

std::string OverlayManager::tile_url(int layer_idx, int color_idx, int64_t ts,
                                     int zoom, int row, int col) const {
    return std::format(
        "https://slider.cira.colostate.edu/data/maps/{}/{}/{}/{}/{}/{:02d}/{:03d}_{:03d}.png",
        m_satellite, m_sector,
        OVERLAY_DEFS[layer_idx].key, OVERLAY_COLORS[color_idx],
        ts, zoom, row, col);
}

void OverlayManager::fetch_timestamp(int layer_idx, int color_idx) {
    std::string url = ts_url(layer_idx, color_idx);
    uint32_t    gen = m_generation.load();
    int         li  = layer_idx;
    int         ci  = color_idx;

    m_pool.enqueue([this, url, li, ci, gen]() {
        if (gen != m_generation.load()) return;

        TsResult res;
        res.layer_idx = li;
        res.color_idx = ci;
        res.gen       = gen;
        res.ok        = false;
        res.timestamp = 0;

        std::vector<uint8_t> buf;
        if (ov_http_get(url, buf)) {
            try {
                auto j = json::parse(buf.begin(), buf.end());
                auto& arr = j.at("timestamps_int");
                if (!arr.empty()) {
                    // timestamps_int is sorted descending — [0] is the newest
                    res.timestamp = arr[0].get<int64_t>();
                    res.ok        = true;
                }
            } catch (const std::exception& e) {
                std::cerr << "[Overlay] JSON parse error: " << e.what() << "\n";
            }
        }

        std::lock_guard lock(m_mutex);
        m_ts_results.push_back(res);
    });
}

void OverlayManager::queue_tile(int layer, int color_idx, int64_t ts,
                                int zoom, int row, int col) {
    OvKey       key{ layer, zoom, row, col };
    std::string url = tile_url(layer, color_idx, ts, zoom, row, col);
    uint32_t    gen = m_generation.load();

    m_pool.enqueue([this, url, key, gen]() {
        if (gen != m_generation.load()) return;

        TileResult res;
        res.key = key;
        res.gen = gen;
        res.ok  = false;

        std::vector<uint8_t> png_buf;
        if (ov_http_get(url, png_buf)) {
            int w = 0, h = 0, ch = 0;
            stbi_uc* px = stbi_load_from_memory(
                png_buf.data(), int(png_buf.size()), &w, &h, &ch, 4);
            if (px) {
                res.rgba.assign(px, px + w * h * 4);
                stbi_image_free(px);
                res.w  = w;
                res.h  = h;
                res.ok = true;
            }
        }

        std::lock_guard lock(m_mutex);
        m_tile_results.push_back(std::move(res));
    });
}

void OverlayManager::update(float vp_x_min, float vp_x_max,
                            float vp_y_min, float vp_y_max,
                            const std::array<OverlaySettings, OVERLAY_COUNT>& settings,
                            Renderer& renderer) {
    // ── Upload finished tile downloads ────────────────────────────────────────
    {
        std::vector<TileResult> ready;
        {
            std::lock_guard lock(m_mutex);
            std::swap(ready, m_tile_results);
        }
        for (auto& r : ready) {
            if (r.gen != m_generation.load()) continue;
            auto it = m_tiles.find(r.key);
            if (it == m_tiles.end()) continue;
            if (!r.ok) { it->second.failed = true; continue; }
            it->second.tex   = renderer.upload_texture(r.rgba.data(), r.w, r.h);
            it->second.ready = true;
        }
    }

    // ── Process timestamp fetch results ───────────────────────────────────────
    {
        std::vector<TsResult> ts_ready;
        {
            std::lock_guard lock(m_mutex);
            std::swap(ts_ready, m_ts_results);
        }
        for (auto& r : ts_ready) {
            if (r.gen != m_generation.load()) continue;
            auto& ld = m_layers[r.layer_idx];
            if (ld.phase != LayerPhase::FETCHING_TS) continue;
            if (!r.ok) {
                std::cerr << "[Overlay] Timestamp fetch failed for "
                          << OVERLAY_DEFS[r.layer_idx].key << "\n";
                ld.phase = LayerPhase::FAILED;
            } else {
                ld.timestamp = r.timestamp;
                ld.color_idx = r.color_idx;
                ld.phase     = LayerPhase::READY;
            }
        }
    }

    // ── Per-layer state machine ───────────────────────────────────────────────
    int T = 1 << m_zoom;

    for (int i = 0; i < OVERLAY_COUNT; ++i) {
        auto& ld      = m_layers[i];
        const auto& s = settings[i];

        if (!s.enabled) continue;

        // Color changed while enabled: reset layer so tiles are re-fetched
        if (ld.phase != LayerPhase::IDLE && ld.color_idx != s.color_idx) {
            free_layer_tiles(i, renderer);
            ld.phase     = LayerPhase::IDLE;
            ld.timestamp = 0;
            ld.color_idx = -1;
        }

        if (ld.phase == LayerPhase::IDLE) {
            ld.phase     = LayerPhase::FETCHING_TS;
            ld.color_idx = s.color_idx;
            fetch_timestamp(i, s.color_idx);
            continue;
        }

        if (ld.phase != LayerPhase::READY) continue;

        // Queue visible tiles that haven't been fetched yet
        for (int r = 0; r < T; ++r) {
            for (int c = 0; c < T; ++c) {
                float tx_min = float(c)     / float(T) - 0.5f;
                float tx_max = float(c + 1) / float(T) - 0.5f;
                float ty_min = 0.5f - float(r + 1) / float(T);
                float ty_max = 0.5f - float(r)     / float(T);

                if (tx_max < vp_x_min || tx_min > vp_x_max ||
                    ty_max < vp_y_min || ty_min > vp_y_max)
                    continue;

                OvKey key{ i, m_zoom, r, c };
                auto [it, inserted] = m_tiles.emplace(key, TileEntry{});
                if (inserted || (!it->second.ready && !it->second.queued && !it->second.failed)) {
                    it->second.queued = true;
                    queue_tile(i, s.color_idx, ld.timestamp, m_zoom, r, c);
                }
            }
        }
    }
}

void OverlayManager::draw(const std::array<OverlaySettings, OVERLAY_COUNT>& settings,
                          Renderer& renderer) {
    for (int i = 0; i < OVERLAY_COUNT; ++i) {
        if (!settings[i].enabled) continue;
        float opacity = settings[i].opacity;

        for (auto& [key, entry] : m_tiles) {
            if (key.layer != i || !entry.ready || !entry.tex) continue;
            renderer.draw_tile(entry.tex, tile_quad(key.zoom, key.row, key.col), opacity);
        }
    }
}
