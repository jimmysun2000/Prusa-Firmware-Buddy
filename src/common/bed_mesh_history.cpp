/// @file
#include "bed_mesh_history.hpp"

#include <Marlin/src/feature/bedlevel/bedlevel.h>
#include <Marlin/src/module/temperature.h>
#include <feature/gcode_exception/gcode_exception.hpp>
#include <marlin_server.hpp>
#include <timing.h>
#include <tool_index.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <optional>
#include <utility>

static_assert(GRID_MAJOR_POINTS_X == bed_mesh_history::points_x && GRID_MAJOR_POINTS_Y == bed_mesh_history::points_y);

namespace bed_mesh_history {

namespace {

    int16_t to_decidegrees(float celsius) {
        if (!std::isfinite(celsius)) {
            return no_temperature;
        }
        return static_cast<int16_t>(std::lround(std::clamp(celsius * 10.0f, -32767.0f, 32767.0f)));
    }

} // namespace

Clock now() {
    const time_t time_s = time(nullptr);
    return Clock {
        .uptime_s = ticks_s(),
        // Negative while the clock is not set
        .time_s = time_s > 0 && std::in_range<uint32_t>(time_s) ? static_cast<uint32_t>(time_s) : 0,
    };
}

void capture_if_measured() {
    // An abort ends the probing early, and the fills of the G29 commands nested in the same G-code
    // still run and complete the mesh from the points probed until then
    if (gcode_exceptions().is_unwinding()) {
        return;
    }

    const Clock clock = now();
    const std::optional<PhysicalToolIndex> tool = PhysicalToolIndex::currently_selected_opt();
    const Meta meta {
        .uptime_s = clock.uptime_s,
        .time_s = clock.time_s,
        .bed_target = thermalManager.degTargetBed(),
        .bed_temp = to_decidegrees(thermalManager.degBed()),
        .tool = tool.has_value() ? tool->to_raw() : no_tool,
        .printing = marlin_server::is_printing(),
    };
    store().push_if_measured(bed_mesh_view::info(), bed_mesh_view::z, bed_mesh_view::probed, meta);
}

} // namespace bed_mesh_history
