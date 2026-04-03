#pragma once
#include <string_view>
#include <span>
#include <array>

// ── Product definitions ───────────────────────────────────────────────────────

struct ProductDef {
    const char* id;
    const char* name;
    // zoom_level_adjust: subtract from sector max_zoom to get max tile zoom for this product
    // e.g. sector max_zoom=5, product adjust=2 → tiles available at zoom 0-3 (2 km max)
    int zoom_level_adjust;
};

struct ProductCategory {
    const char* name;
    std::span<const ProductDef> products;
};

// ── Sectors ───────────────────────────────────────────────────────────────────

struct SectorDef {
    const char* id;
    const char* name;
    int         tile_size_px;   // pixels per tile
    int         max_zoom;       // max zoom level available for this sector
    int         update_minutes; // nominal cadence
};

// ── Satellites ────────────────────────────────────────────────────────────────

struct SatelliteDef {
    const char*              id;
    const char*              name;
    std::span<const SectorDef> sectors;
};

// ─────────────────────────────────────────────────────────────────────────────
// Static data
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr std::array PRODUCTS_BANDS = {
    ProductDef{ "band_01", "Band 01 – 0.47 µm (Blue)",                  1 },
    ProductDef{ "band_02", "Band 02 – 0.64 µm (Red)",                   0 },
    ProductDef{ "band_03", "Band 03 – 0.86 µm (Veggie)",                1 },
    ProductDef{ "band_04", "Band 04 – 1.37 µm (Cirrus)",                2 },
    ProductDef{ "band_05", "Band 05 – 1.6 µm (Snow/Ice)",               1 },
    ProductDef{ "band_06", "Band 06 – 2.2 µm (Cloud Particle Size)",    2 },
    ProductDef{ "band_07", "Band 07 – 3.9 µm (Shortwave Window)",       2 },
    ProductDef{ "band_08", "Band 08 – 6.2 µm (Upper WV)",               2 },
    ProductDef{ "band_09", "Band 09 – 6.9 µm (Mid WV)",                 2 },
    ProductDef{ "band_10", "Band 10 – 7.3 µm (Lower WV)",               2 },
    ProductDef{ "band_11", "Band 11 – 8.4 µm (Cloud-Top Phase)",        2 },
    ProductDef{ "band_12", "Band 12 – 9.6 µm (Ozone)",                  2 },
    ProductDef{ "band_13", "Band 13 – 10.3 µm (Clean IR)",              2 },
    ProductDef{ "band_14", "Band 14 – 11.2 µm (IR Longwave)",           2 },
    ProductDef{ "band_15", "Band 15 – 12.3 µm (Dirty Longwave)",        2 },
    ProductDef{ "band_16", "Band 16 – 13.3 µm (CO₂ IR)",               2 },
};

inline constexpr std::array PRODUCTS_MULTI = {
    ProductDef{ "geocolor",                          "GeoColor (CIRA)",                    1 },
    ProductDef{ "natural_color",                     "Day Land Cloud (EUMETSAT)",           1 },
    ProductDef{ "cira_geosst",                       "GeoSST (CIRA)",                      1 },
    ProductDef{ "cira_proxy_visible",                "ProxyVis (CIRA)",                    0 },
    ProductDef{ "cira_geodebra",                     "GeoDEBRA – Dust (CIRA)",             1 },
    ProductDef{ "cira_geosnow",                      "GeoSnow (CIRA)",                     1 },
    ProductDef{ "cira_geofire",                      "GeoFire (CIRA)",                     1 },
    ProductDef{ "cira_cloud_snow_discriminator",     "Snow/Cloud Discriminator (CIRA)",    1 },
    ProductDef{ "cira_high_low_cloud_and_snow",      "Snow/Cloud Layers (CIRA)",           1 },
    ProductDef{ "split_window_difference_10_3-12_3", "Split Window Difference",            2 },
    ProductDef{ "shortwave_albedo_cira",             "Shortwave Albedo (CIRA)",            2 },
    ProductDef{ "cira_debra_dust",                   "Dust DEBRA (CIRA)",                  2 },
};

inline constexpr std::array PRODUCTS_RGB = {
    ProductDef{ "rgb_air_mass",                           "Airmass (EUMETSAT)",                     2 },
    ProductDef{ "jma_day_cloud_phase_distinction_rgb",    "Day Cloud Phase (JMA)",                   1 },
    ProductDef{ "day_cloud_phase_microphysics_rgb",       "Day Cloud Phase Microphysics (EUMETSAT)", 1 },
    ProductDef{ "eumetsat_nighttime_microphysics",        "Nighttime Microphysics (EUMETSAT)",       2 },
    ProductDef{ "day_snow_fog",                           "Day Snow/Fog (EUMETSAT)",                 1 },
    ProductDef{ "awips_dust",                             "Dust (EUMETSAT)",                         2 },
    ProductDef{ "fire_temperature",                       "Fire Temperature (CIRA)",                 2 },
    ProductDef{ "cira_natural_fire_color",                "Day Fire (CIRA)",                         1 },
    ProductDef{ "eumetsat_ash",                           "Ash (EUMETSAT)",                          2 },
    ProductDef{ "eumetsat_severe_storms_rgb",             "Severe Storms (EUMETSAT)",                2 },
    ProductDef{ "blowing_snow_rgb",                       "Blowing Snow (NOAA)",                     1 },
    ProductDef{ "sea_spray_rgb",                          "Sea Spray (NOAA)",                        1 },
};

inline constexpr std::array PRODUCTS_CLOUD = {
    ProductDef{ "cloud_top_height_cira_clavr-x",             "Cloud-Top Height (NOAA)",          2 },
    ProductDef{ "cloud_geometric_thickness_cira_clavr-x",    "Cloud Thickness (CIRA)",           2 },
    ProductDef{ "cloud_layers_cira_clavr-x",                 "Cloud Layers (CIRA)",              2 },
    ProductDef{ "cloud_optical_thickness_cira_clavr-x",      "Cloud Optical Depth (NOAA)",       2 },
    ProductDef{ "cloud_effective_radius_cira_clavr-x",       "Cloud Particle Size (NOAA)",       2 },
    ProductDef{ "cloud_phase_cira_clavr-x",                  "Cloud Phase (CIRA)",               2 },
    ProductDef{ "cloud_mask_cira_clavr-x",                   "Cloud Mask (NOAA)",                2 },
};

inline constexpr std::array PRODUCTS_GLM = {
    ProductDef{ "cira_glm_l2_group_energy", "Group Energy Density (CIRA)",        2 },
    ProductDef{ "cira_glm_l2_group_counts", "Group Flash Count Density (CIRA)",   2 },
};

inline constexpr std::array PRODUCTS_MICRO = {
    ProductDef{ "cira_blended_tpw",                                          "Blended TPW (CIRA)",             2 },
    ProductDef{ "cira_advected_layered_precipitable_water_surface-850hPa",   "ALPW Surface–850 hPa (CIRA)",    2 },
    ProductDef{ "cira_advected_layered_precipitable_water_850-700hPa",       "ALPW 850–700 hPa (CIRA)",        2 },
    ProductDef{ "cira_advected_layered_precipitable_water_700-500hPa",       "ALPW 700–500 hPa (CIRA)",        2 },
    ProductDef{ "cira_advected_layered_precipitable_water_500-300hPa",       "ALPW 500–300 hPa (CIRA)",        2 },
};

inline constexpr std::array PRODUCTS_MRMS = {
    ProductDef{ "mrms_merged_base_reflectivity_qc",                  "MRMS Base Reflectivity",        0 },
    ProductDef{ "mrms_reflectivity_at_lowest_altitude",              "MRMS Lowest-Alt Reflectivity",  0 },
    ProductDef{ "mrms_radar_precipitation_accumulation_01-hour",     "MRMS Precip Accum 1-hr",        0 },
    ProductDef{ "mrms_radar_precipitation_rate",                     "MRMS Precipitation Rate",       0 },
    ProductDef{ "mrms_lightning_probability_0-30-min_nldn",          "MRMS Lightning Probability",    0 },
};

inline const std::array PRODUCT_CATEGORIES = {
    ProductCategory{ "Multispectral",       PRODUCTS_MULTI  },
    ProductCategory{ "Individual ABI Bands",PRODUCTS_BANDS  },
    ProductCategory{ "RGB Composites",      PRODUCTS_RGB    },
    ProductCategory{ "Cloud Products",      PRODUCTS_CLOUD  },
    ProductCategory{ "Lightning (GLM)",     PRODUCTS_GLM    },
    ProductCategory{ "Microwave / TPW",     PRODUCTS_MICRO  },
    ProductCategory{ "MRMS Radar",          PRODUCTS_MRMS   },
};

// ── Sector data ───────────────────────────────────────────────────────────────

inline constexpr std::array GOES_SECTORS = {
    SectorDef{ "full_disk",    "Full Disk",  678, 5, 10 },
    SectorDef{ "conus",        "CONUS",      625, 5,  5 },
    SectorDef{ "mesoscale_01", "Meso 1",     500, 7,  1 },
    SectorDef{ "mesoscale_02", "Meso 2",     500, 7,  1 },
};

inline constexpr std::array HIMAWARI_SECTORS = {
    SectorDef{ "full_disk",    "Full Disk",  678, 5, 10 },
    SectorDef{ "japan",        "Japan",      500, 5,  2 },
    SectorDef{ "conus",        "Target N",   625, 5,  5 },
    SectorDef{ "mesoscale_01", "Meso 1",     500, 7,  2 },
};

inline constexpr std::array JPSS_SECTORS = {
    SectorDef{ "northern_hemisphere", "Northern Hemisphere", 625, 4, 102 },
    SectorDef{ "southern_hemisphere", "Southern Hemisphere", 625, 4, 102 },
};

inline const std::array SATELLITES = {
    SatelliteDef{ "goes-19",  "GOES-19 (East)",  GOES_SECTORS     },
    SatelliteDef{ "goes-18",  "GOES-18 (West)",  GOES_SECTORS     },
    SatelliteDef{ "himawari", "Himawari",         HIMAWARI_SECTORS },
    SatelliteDef{ "jpss",     "JPSS",             JPSS_SECTORS     },
};

// ── Zoom resolution labels ────────────────────────────────────────────────────

inline constexpr std::array<const char*, 8> ZOOM_LABELS = {
    "16 km", "8 km", "4 km", "2 km", "1 km", "0.5 km", "0.25 km", "0.125 km"
};

// ── Helpers ───────────────────────────────────────────────────────────────────

inline const SatelliteDef* find_satellite(std::string_view id) {
    for (auto& s : SATELLITES)
        if (s.id == id) return &s;
    return nullptr;
}

inline const SectorDef* find_sector(const SatelliteDef& sat, std::string_view id) {
    for (auto& s : sat.sectors)
        if (s.id == id) return &s;
    return nullptr;
}

inline const ProductDef* find_product(std::string_view id) {
    for (auto& cat : PRODUCT_CATEGORIES)
        for (auto& p : cat.products)
            if (p.id == id) return &p;
    return nullptr;
}
