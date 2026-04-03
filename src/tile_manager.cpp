#include "tile_manager.hpp"
#include <stb_image.h>
#include <curl/curl.h>
#include <format>
#include <iostream>
#include <cmath>
#include <algorithm>

// ── libcurl helper (runs in worker thread) ────────────────────────────────────

static size_t write_cb(void* data, size_t size, size_t nmemb, void* user) {
    auto* vec = static_cast<std::vector<uint8_t>*>(user);
    auto* ptr = static_cast<const uint8_t*>(data);
    size_t n  = size * nmemb;
    vec->insert(vec->end(), ptr, ptr + n);
    return n;
}

static bool http_get(const std::string& url, std::vector<uint8_t>& out, int limit_kbps = 0) {
    CURL* c = curl_easy_init();
    if (!c) return false;

    out.clear();
    curl_easy_setopt(c, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,      &out);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        20L);
    curl_easy_setopt(c, CURLOPT_USERAGENT,      "StormView/0.1");
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING,"");

    if (limit_kbps > 0)
        curl_easy_setopt(c, CURLOPT_MAX_RECV_SPEED_LARGE,
                         curl_off_t(limit_kbps) * 1024);

    CURLcode res = curl_easy_perform(c);
    long     code= 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);

    return (res == CURLE_OK && code == 200);
}

// ── TileManager ───────────────────────────────────────────────────────────────

TileManager::TileManager(int n_threads)
    : m_pool(n_threads) {}

TileManager::~TileManager() = default;

void TileManager::set_source(const std::string& satellite,
                              const std::string& sector,
                              const std::string& product,
                              int64_t            timestamp,
                              int                data_zoom) {
    m_satellite   = satellite;
    m_sector      = sector;
    m_product     = product;
    m_timestamp   = timestamp;
    m_data_zoom   = data_zoom;
    int N         = 1 << data_zoom;
    m_total_tiles = N * N;
}

void TileManager::clear(Renderer& renderer) {
    for (auto& [k, e] : m_tiles)
        if (e.tex) renderer.free_texture(e.tex);
    m_tiles.clear();

    std::lock_guard lock(m_result_mutex);
    m_results.clear();
}

void TileManager::update(float vp_x_min, float vp_x_max,
                         float vp_y_min, float vp_y_max,
                         Renderer& renderer) {
    // ── Upload any finished downloads ─────────────────────────────────────────
    {
        std::vector<DownloadResult> ready;
        {
            std::lock_guard lock(m_result_mutex);
            std::swap(ready, m_results);
        }
        for (auto& r : ready) {
            auto it = m_tiles.find(r.key);
            if (it == m_tiles.end()) continue;
            if (!r.ok) {
                it->second.failed = true;
                continue;
            }
            it->second.tex   = renderer.upload_texture(r.rgba.data(), r.width, r.height);
            it->second.ready = true;
        }
    }

    // ── Cull tiles outside viewport + request visible ones ───────────────────
    if (m_timestamp == 0) return;

    int N = 1 << m_data_zoom; // tiles per side at this zoom

    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            // World-space bounds of this tile (image occupies [-0.5, 0.5]²)
            float tx_min = float(c)     / float(N) - 0.5f;
            float tx_max = float(c + 1) / float(N) - 0.5f;
            float ty_min = 0.5f - float(r + 1) / float(N);
            float ty_max = 0.5f - float(r)     / float(N);

            // AABB visibility test
            if (tx_max < vp_x_min || tx_min > vp_x_max ||
                ty_max < vp_y_min || ty_min > vp_y_max)
                continue;

            TileKey key{ m_data_zoom, r, c };
            auto [it, inserted] = m_tiles.emplace(key, TileEntry{});
            if (inserted || (!it->second.ready && !it->second.queued && !it->second.failed)) {
                it->second.queued = true;
                queue_tile(key);
            }
        }
    }
}

void TileManager::draw(Renderer& renderer) {
    for (auto& [key, entry] : m_tiles) {
        if (!entry.ready || !entry.tex) continue;
        renderer.draw_tile(entry.tex, tile_quad(key));
    }
}

void TileManager::queue_tile(const TileKey& key) {
    std::string url = build_url(key);

    // Capture everything needed by value into the lambda (no 'this' members via ref)
    TileKey captured_key = key;

    int limit = m_limit_kbps.load();
    m_pool.enqueue([this, url, captured_key, limit]() mutable {
        DownloadResult res;
        res.key = captured_key;

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
    // Tile URL pattern (confirmed working):
    // /data/imagery/YYYY/MM/DD/{sat}---{sector}/{product}/{ts14}/{zoom:02d}/{row:03d}_{col:03d}.png
    std::string ts = std::to_string(m_timestamp);
    std::string yyyy = ts.substr(0, 4);
    std::string mm   = ts.substr(4, 2);
    std::string dd   = ts.substr(6, 2);

    return std::format(
        "https://slider.cira.colostate.edu/data/imagery/{}/{}/{}/{}---{}/{}/{}/{:02d}/{:03d}_{:03d}.png",
        yyyy, mm, dd,
        m_satellite, m_sector,
        m_product,
        ts,
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
