/// @file
#include "bed_mesh_view.hpp"

#include <Marlin/src/feature/bedlevel/bedlevel.h>

#include <cmath>

static_assert(ENABLED(AUTO_BED_LEVELING_UBL));
static_assert(GRID_MAX_POINTS_X <= UINT8_MAX && GRID_MAX_POINTS_Y <= UINT8_MAX);

namespace bed_mesh_view {

Info info() {
    return Info {
        .nx = GRID_MAX_POINTS_X,
        .ny = GRID_MAX_POINTS_Y,
        .border = GRID_BORDER,
        .major_step = GRID_MAJOR_STEP,
        .min_x = static_cast<float>(MESH_MIN_X),
        .min_y = static_cast<float>(MESH_MIN_Y),
        .max_x = static_cast<float>(MESH_MAX_X),
        .max_y = static_cast<float>(MESH_MAX_Y),
    };
}

float z(uint8_t ix, uint8_t iy) {
    if (ix >= GRID_MAX_POINTS_X || iy >= GRID_MAX_POINTS_Y) {
        return NAN;
    }
    // A 32-bit load does not tear, the writer is the Marlin thread
    return unified_bed_leveling::z_values[ix][iy];
}

bool probed(uint8_t ix, uint8_t iy) {
    if (ix >= GRID_MAX_POINTS_X || iy >= GRID_MAX_POINTS_Y) {
        return false;
    }
    // A probed point can be invalidated on its own later
    return unified_bed_leveling::is_probed(ix, iy) && std::isfinite(z(ix, iy));
}

State state() {
    return compute_state(info(), z);
}

uint32_t revision() {
    return compute_revision(info(), z, probed);
}

} // namespace bed_mesh_view
