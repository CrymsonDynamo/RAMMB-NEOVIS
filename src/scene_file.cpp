#include "scene_file.hpp"
#include "overlay_defs.hpp"
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <string>
#include <cstring>

using json = nlohmann::json;

// ── ViewState JSON serialization ──────────────────────────────────────────────

static json state_to_json(const ViewState& s) {
    json j;
    j["satellite"]      = s.satellite;
    j["sector"]         = s.sector;
    j["product"]        = s.product;
    j["data_zoom"]      = s.data_zoom;
    j["num_frames"]     = s.num_frames;
    j["time_step"]      = s.time_step;
    j["fps"]            = s.fps;
    j["loop_mode"]      = s.loop_mode;
    j["tile_select_all"]= s.tile_select_all;

    // tile_selection: vector<bool> → JSON int array
    json ts = json::array();
    for (bool b : s.tile_selection) ts.push_back(b ? 1 : 0);
    j["tile_selection"] = ts;

    // date_range
    const auto& dr = s.date_range;
    j["dr_use_range"]   = dr.use_range;
    j["dr_start_year"]  = dr.start_year;
    j["dr_start_month"] = dr.start_month;
    j["dr_start_day"]   = dr.start_day;
    j["dr_start_hour"]  = dr.start_hour;
    j["dr_start_min"]   = dr.start_min;
    j["dr_end_year"]    = dr.end_year;
    j["dr_end_month"]   = dr.end_month;
    j["dr_end_day"]     = dr.end_day;
    j["dr_end_hour"]    = dr.end_hour;
    j["dr_end_min"]     = dr.end_min;

    // overlays
    json ov = json::array();
    for (const auto& o : s.overlays)
        ov.push_back({ {"enabled", o.enabled}, {"color_idx", o.color_idx}, {"opacity", o.opacity} });
    j["overlays"] = ov;

    return j;
}

static ViewState state_from_json(const json& j) {
    ViewState s;
    s.satellite       = j.value("satellite",       s.satellite);
    s.sector          = j.value("sector",           s.sector);
    s.product         = j.value("product",          s.product);
    s.data_zoom       = j.value("data_zoom",        s.data_zoom);
    s.num_frames      = j.value("num_frames",       s.num_frames);
    s.time_step       = j.value("time_step",        s.time_step);
    s.fps             = j.value("fps",              s.fps);
    s.loop_mode       = j.value("loop_mode",        s.loop_mode);
    s.tile_select_all = j.value("tile_select_all",  s.tile_select_all);

    if (j.contains("tile_selection") && j["tile_selection"].is_array()) {
        s.tile_selection.clear();
        for (auto& b : j["tile_selection"])
            s.tile_selection.push_back(b.get<int>() != 0);
    }

    auto& dr = s.date_range;
    dr.use_range   = j.value("dr_use_range",   dr.use_range);
    dr.start_year  = j.value("dr_start_year",  dr.start_year);
    dr.start_month = j.value("dr_start_month", dr.start_month);
    dr.start_day   = j.value("dr_start_day",   dr.start_day);
    dr.start_hour  = j.value("dr_start_hour",  dr.start_hour);
    dr.start_min   = j.value("dr_start_min",   dr.start_min);
    dr.end_year    = j.value("dr_end_year",    dr.end_year);
    dr.end_month   = j.value("dr_end_month",   dr.end_month);
    dr.end_day     = j.value("dr_end_day",     dr.end_day);
    dr.end_hour    = j.value("dr_end_hour",    dr.end_hour);
    dr.end_min     = j.value("dr_end_min",     dr.end_min);

    if (j.contains("overlays") && j["overlays"].is_array()) {
        int i = 0;
        for (auto& oj : j["overlays"]) {
            if (i >= OVERLAY_COUNT) break;
            s.overlays[i].enabled   = oj.value("enabled",   false);
            s.overlays[i].color_idx = oj.value("color_idx", 0);
            s.overlays[i].opacity   = oj.value("opacity",   0.85f);
            ++i;
        }
    }

    // Signal the app to reload data after restoring this state.
    s.source_changed = true;
    return s;
}

// ── SQLite helpers ────────────────────────────────────────────────────────────

struct Db {
    sqlite3* db = nullptr;
    ~Db() { if (db) sqlite3_close(db); }
    bool ok() const { return db != nullptr; }
    const char* errmsg() const { return sqlite3_errmsg(db); }
};

static bool exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    bool ok = sqlite3_exec(db, sql, nullptr, nullptr, &err) == SQLITE_OK;
    if (err) sqlite3_free(err);
    return ok;
}

// ── Public API ────────────────────────────────────────────────────────────────

std::string scene_save(const std::string& path, const SceneBar& bar) {
    Db d;
    if (sqlite3_open(path.c_str(), &d.db) != SQLITE_OK)
        return std::string("Cannot open file: ") + d.errmsg();

    // Recreate schema
    if (!exec(d.db, R"(
        DROP TABLE IF EXISTS meta;
        DROP TABLE IF EXISTS scenes;
        CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE scenes(sort_order INTEGER PRIMARY KEY,
                            name TEXT NOT NULL, state_json TEXT NOT NULL);
    )"))
        return std::string("Schema error: ") + d.errmsg();

    // Meta rows
    sqlite3_stmt* stmt = nullptr;
    const char* ins_meta = "INSERT INTO meta(key,value) VALUES(?,?);";
    sqlite3_prepare_v2(d.db, ins_meta, -1, &stmt, nullptr);

    auto put_meta = [&](const char* k, const std::string& v) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, k, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, v.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    };

    exec(d.db, "BEGIN;");
    put_meta("version",              "1");
    put_meta("active",               std::to_string(bar.active));
    put_meta("vsync",                std::to_string(int(bar.vsync)));
    put_meta("dark_ui",              std::to_string(int(bar.dark_ui)));
    put_meta("cache_limit_mb",       std::to_string(bar.cache_limit_mb));
    put_meta("auto_reload",          std::to_string(int(bar.auto_reload)));
    put_meta("auto_reload_s",        std::to_string(bar.auto_reload_s));
    put_meta("download_limit_kbps",  std::to_string(bar.download_limit_kbps));
    put_meta("download_threads",     std::to_string(bar.download_threads));
    sqlite3_finalize(stmt);

    // Scene rows
    const char* ins_scene =
        "INSERT INTO scenes(sort_order,name,state_json) VALUES(?,?,?);";
    sqlite3_prepare_v2(d.db, ins_scene, -1, &stmt, nullptr);

    for (int i = 0; i < int(bar.scenes.size()); ++i) {
        std::string js = state_to_json(bar.scenes[i].state).dump();
        sqlite3_reset(stmt);
        sqlite3_bind_int (stmt, 1, i);
        sqlite3_bind_text(stmt, 2, bar.scenes[i].name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, js.c_str(),                 -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
    exec(d.db, "COMMIT;");

    return "";
}

std::string scene_load(const std::string& path, SceneBar& bar) {
    Db d;
    if (sqlite3_open_v2(path.c_str(), &d.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        return std::string("Cannot open file: ") + d.errmsg();

    // ── Load meta ─────────────────────────────────────────────────────────────
    auto get_meta = [&](const char* key, int default_val) -> int {
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(d.db, "SELECT value FROM meta WHERE key=?;", -1, &s, nullptr);
        sqlite3_bind_text(s, 1, key, -1, SQLITE_STATIC);
        int v = default_val;
        if (sqlite3_step(s) == SQLITE_ROW)
            v = std::stoi(reinterpret_cast<const char*>(sqlite3_column_text(s, 0)));
        sqlite3_finalize(s);
        return v;
    };

    int active         = get_meta("active", 0);
    bar.vsync              = bool(get_meta("vsync", 1));
    bar.dark_ui            = bool(get_meta("dark_ui", 1));
    bar.cache_limit_mb     = get_meta("cache_limit_mb", 512);
    bar.auto_reload        = bool(get_meta("auto_reload", 0));
    bar.auto_reload_s      = get_meta("auto_reload_s", 300);
    bar.download_limit_kbps= get_meta("download_limit_kbps", 0);
    bar.download_threads   = get_meta("download_threads", 4);

    // ── Load scenes ───────────────────────────────────────────────────────────
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(d.db,
        "SELECT name, state_json FROM scenes ORDER BY sort_order;", -1, &s, nullptr);

    bar.scenes.clear();
    while (sqlite3_step(s) == SQLITE_ROW) {
        Scene sc;
        sc.name = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
        const char* js = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
        auto j = json::parse(js, nullptr, false);
        if (!j.is_discarded())
            sc.state = state_from_json(j);
        bar.scenes.push_back(std::move(sc));
    }
    sqlite3_finalize(s);

    if (bar.scenes.empty())
        return "File contains no scenes.";

    bar.active = std::clamp(active, 0, int(bar.scenes.size()) - 1);
    return "";
}
