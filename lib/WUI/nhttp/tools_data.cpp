#include "tools_renderer.h"

#include <common/marlin_vars.hpp>
#include <config_store/store_instance.hpp>
#include <filament.hpp>
#include <module/prusa/toolchanger.h>
#include <module/temperature/temp_defines.hpp>
#include <tool_index.hpp>

#include <cmath>
#include <cstring>

// These run on the HTTP thread. Nothing here calls into the Marlin thread or waits for it: the values
// are atomics written by other threads, or config store items, which hold the store lock only for the copy.

namespace nhttp::handler::tools_data {

static_assert(sizeof(Tool::filament) == filament_name_buffer_size);

uint8_t count() {
    return PhysicalToolIndex::count;
}

std::optional<uint8_t> active() {
    // Marlin's active_extruder, an atomic
    return PhysicalToolIndex::currently_selected_opt().transform([](PhysicalToolIndex tool) { return tool.to_raw(); });
}

Tool tool(uint8_t index) {
    const PhysicalToolIndex physical_tool = PhysicalToolIndex::from_raw(index);
    // A flag of the tool's puppy, set when the puppies are detected; the status route reads it the same way
    if (!physical_tool.is_enabled()) {
        return Tool {};
    }

    Tool result;
    result.enabled = true;

    // Marlin variables are atomics, published by the Marlin thread
    const auto &hotend = marlin_vars().hotend(physical_tool);
    const float temp = hotend.temp_nozzle;
    result.temp = (temp == TempInfo::celsius_uninitialized) ? NAN : temp;
    result.target = hotend.target_nozzle.get();

    // From the config store, like the filament shown by /api/printer
    const FilamentType filament = FilamentType::for_tool(physical_tool.currently_selected_virtual_tool());
    if (filament != FilamentType::none) {
        strlcpy(result.filament.data(), filament.parameters().name.data(), result.filament.size());
    }

    // Atomics that the puppy task updates from the tool's sensors
    const auto &dwarf = prusa_toolchanger.getTool(physical_tool);
    result.picked = dwarf.is_picked();
    result.parked = dwarf.is_parked();

    result.retracted = config_store().get_filament_retracted_distance(physical_tool);
    result.nozzle_diameter = config_store().get_nozzle_diameter(physical_tool);
    return result;
}

} // namespace nhttp::handler::tools_data
