#include "chamber_requirements.hpp"

#include <filament.hpp>
#include <gcode_info.hpp>

namespace buddy {

ChamberRequirements print_chamber_requirements() {
    ChamberRequirements result;

    auto &gcode_info = GCodeInfo::getInstance();
    if (!gcode_info.is_loaded()) {
        return result;
    }

    gcode_info.for_each_used_extruder([&]([[maybe_unused]] GcodeToolIndex, VirtualToolIndex virtual_tool, const GCodeInfo::ExtruderInfo &) {
        const auto parameters = FilamentType::for_tool_heuristic(virtual_tool).parameters();
        result.add(parameters.chamber_min_temperature, parameters.chamber_max_temperature, parameters.chamber_target_temperature);
#if HAS_FILAMENT_COOLDOWN_RATE_PARAM()
        result.add_cooldown_rate(parameters.cooldown_rate);
#endif
    });

    return result;
}

} // namespace buddy
