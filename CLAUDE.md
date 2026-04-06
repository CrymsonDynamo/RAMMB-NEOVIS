# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (first time or after CMakeLists changes)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j$(nproc)

# Run from repo root (shaders are resolved relative to the executable)
./build/rammb-neovis

# Package .deb locally
cd build && cpack -G DEB

# Release: tag and push — CI builds .deb + AppImage automatically
./scripts/release.sh 0.2.0
```

There are no tests. The only validation is a successful build and runtime smoke test.

## Dependencies

System packages required (Ubuntu/Debian):
```bash
sudo apt install cmake libglfw3-dev libglew-dev libglm-dev \
  libcurl4-openssl-dev libavcodec-dev libavformat-dev libavutil-dev \
  libswscale-dev nlohmann-json3-dev libsqlite3-dev zenity
```

ImGui and stb are fetched automatically by CMake (FetchContent) on first configure.

`<format>` requires GCC 13+. Ubuntu 22.04 ships GCC 11 — use Ubuntu 24.04 or install `gcc-13`.

## Architecture

**Main loop:** `App::run()` drives a GLFW event loop. Each frame calls `update()` (state/download logic) then `render()` (OpenGL + ImGui draw).

**Coordinate systems — critical to get right:**
- World space: full image fits in `[-0.5, 0.5] × [-0.5, 0.5]`, Y-up
- Tile `(zoom, row, col)` maps to world bounds: `x = col/2^z - 0.5`, `y = 0.5 - row/2^z`
- Screen→world and world→screen conversions (`s2w`/`w2s` lambdas in `app.cpp`) **must** use the exact tile render viewport — not the full window — or overlays drift when panning (parallax bug)
- Tile render viewport excludes: left sidebar (`m_sidebar_w`), top scene bar (`m_bar_h`), bottom timeline

**Tile pipeline:**
1. `TileManager::request_frame(frame_idx)` enqueues tile downloads via `ThreadPool`
2. `HttpClient` (synchronous CURL) fetches PNG bytes; `stb_image` decodes to RGBA
3. Results tagged with a generation counter (`m_generation`) — stale results from before a `clear()` are silently discarded in `TileManager::update()`
4. GPU texture upload happens on the main thread in `update()`
5. Tile selection mask (`m_tile_selection`) skips unwanted tiles entirely

**Export pipeline:**
- `Renderer::render_offscreen(crop, w, h)` renders to an FBO and reads back RGBA
- `Exporter` runs in a background thread; frames are passed via a queue
- FFmpeg encodes MP4 (H.264 or NVENC) / GIF; raw PNG uses stb_image_write

**Scene tabs:** `SceneBar` stores a `std::vector<Scene>`, each holding a full `ViewState` snapshot. Switching tabs restores state and sets `source_changed = true` to trigger a reload.

**Shader resolution order** (see `App::init`):
1. Directory next to the executable (dev build, AppImage)
2. `/usr/share/rammb-neovis/shaders/` (installed .deb)
3. Relative `shaders/` fallback

## Key Files

| File | Role |
|------|------|
| `src/app.cpp` | Main loop, GLFW callbacks, viewport math, s2w/w2s lambdas |
| `src/tile_manager.cpp` | Download queue, generation counter, selection mask, progress stats |
| `src/thread_pool.hpp` | Work queue with `pause()`/`resume()` |
| `src/satellite_config.hpp` | Static list of all satellites, sectors, and products |
| `src/ui/sidebar.cpp` | Date range picker, API timestamp dropdowns, animation controls |
| `src/ui/export_panel.cpp` | Crop box overlay, resolution presets (aspect-ratio-aware) |
| `src/ui/tools_panel.cpp` | Interactive tile grid overlay (click/drag to select/deselect) |
| `src/ui/scene_bar.cpp` | Tab bar, scene state save/restore, settings panel |
| `shaders/tile.vert/.frag` | GLSL 4.5 — tile rendering with pan/zoom projection |

## API

Tile imagery is fetched from `https://slider.cira.colostate.edu/data`. Available timestamps per day are fetched from `YYYYMMDD_by_hour.json` endpoints. All network calls use `HttpClient` (synchronous CURL, `thread_local` handles for per-thread reuse).
