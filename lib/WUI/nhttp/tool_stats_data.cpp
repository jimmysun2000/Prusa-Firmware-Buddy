#include "tool_stats_renderer.h"

#include <odometer.hpp>
#include <tool_index.hpp>

// These run on the HTTP thread. Each counter is the value saved in the config store, read under the
// store's lock, plus the unsaved part, an atomic that the Marlin thread adds to; the Print Statistics
// menu reads them the same way from the GUI thread. While the Marlin thread saves the counters, a read
// can count the unsaved part twice; the next read is right again.

namespace nhttp::handler::tool_stats_data {

uint8_t count() {
    return PhysicalToolIndex::count;
}

ToolStats tool(uint8_t index) {
    const PhysicalToolIndex physical_tool = PhysicalToolIndex::from_raw(index);
    Odometer_s &odometer = Odometer_s::instance();
    return ToolStats {
        .filament_used = odometer.get_extruded(physical_tool),
        .picks = odometer.get_toolpick(physical_tool),
        .print_time = odometer.get_tool_time(physical_tool),
    };
}

uint32_t tool_recoveries() {
    return Odometer_s::instance().get_tool_recoveries();
}

} // namespace nhttp::handler::tool_stats_data
