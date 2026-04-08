#pragma once
#include <string>
#include "ui/scene_bar.hpp"

// Serialize/deserialize SceneBar (all scenes + app settings) to/from a .rnvs file.
// .rnvs files are SQLite databases.

// Returns "" on success, error string on failure.
std::string scene_save(const std::string& path, const SceneBar& bar);
std::string scene_load(const std::string& path, SceneBar& bar);
