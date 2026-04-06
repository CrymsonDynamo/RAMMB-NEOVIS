#pragma once
#include <array>

// Overlay types available from RAMMB Slider.
// Key matches the URL path component; name is shown in the UI.
struct OverlayDef {
    const char* key;
    const char* name;
};

inline constexpr std::array OVERLAY_DEFS = {
    OverlayDef{ "borders",    "Default Borders"  },
    OverlayDef{ "countries",  "Countries"        },
    OverlayDef{ "states",     "States/Provinces" },
    OverlayDef{ "counties",   "U.S. Counties"    },
    OverlayDef{ "coastlines", "Coastlines"       },
    OverlayDef{ "lat",        "Lat/Lon Grid"     },
    OverlayDef{ "cities",     "Cities"           },
    OverlayDef{ "rivers",     "Rivers"           },
    OverlayDef{ "lakes",      "Lakes"            },
    OverlayDef{ "roads",      "Roads"            },
};
inline constexpr int OVERLAY_COUNT = static_cast<int>(OVERLAY_DEFS.size());

inline const char* const OVERLAY_COLORS[] = {
    "white", "yellow", "black", "red", "orange", "green", "blue", "cyan"
};
inline constexpr int OVERLAY_COLOR_COUNT = 8;

// Per-overlay UI state (stored in ViewState, one per OVERLAY_DEFS entry).
struct OverlaySettings {
    bool  enabled   = false;
    int   color_idx = 0;    // index into OVERLAY_COLORS
    float opacity   = 0.85f;
};
