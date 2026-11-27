#pragma once

#include "user_macro.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

class string_view_utf8;
class StringViewUtf8ParamBase;

/// Actions of the configurable shortcuts on the home screen
namespace home_shortcut {

/// The first two shortcuts are a fourth column of the home screen, top and bottom; the other two a fifth
inline constexpr size_t slot_count = 4;

enum class Kind : uint8_t {
    none,
    auto_home,
    pick_park,
    move_axis,
    motors_off,
    wipe_nozzle,
    macro,
    temperature,
    move_z,
    bed_mesh,
    bed_tiles,
    tools_overview,
    statistics,
    maintenance,
    sensor_info,
    network_status,
    prusalink,
    lights,
    enclosure,
    wiper_setup,
    calibrations,
    filament_sensors,
    macros_screen,
    load_all,
    unload_all,
    change_all,
    tool_settings,
    input_shaper,
    cooldown,
    retract_all,
    load_filament,
    unload_filament,
    change_filament,
    purge_filament,
    shape_tip,
    cleaning_filament,
    set_ready,
    export_settings,
    cold_pull,
};

struct Action {
    /// Stored in the config store: never change or reuse one
    uint8_t id;
    Kind kind;
    /// Macro number, for Kind::macro
    uint8_t macro = 0;
};

/// In the order of the selection menu
/// IDs 16 to 31 are for macros, 32 to 63 for screens, 64 to 95 for other actions
inline constexpr std::array actions {
    Action { 0, Kind::none },
    Action { 1, Kind::auto_home },
    Action { 2, Kind::pick_park },
    Action { 3, Kind::move_axis },
    Action { 4, Kind::motors_off },
    Action { 5, Kind::wipe_nozzle },
    Action { 33, Kind::move_z },
    Action { 32, Kind::temperature },
    Action { 64, Kind::cooldown },
    Action { 66, Kind::load_filament },
    Action { 67, Kind::unload_filament },
    Action { 68, Kind::change_filament },
    Action { 69, Kind::purge_filament },
    Action { 65, Kind::retract_all },
    Action { 48, Kind::load_all },
    Action { 49, Kind::unload_all },
    Action { 50, Kind::change_all },
    Action { 70, Kind::shape_tip },
    Action { 71, Kind::cleaning_filament },
    Action { 74, Kind::cold_pull },
    Action { 34, Kind::bed_mesh },
    Action { 35, Kind::bed_tiles },
    Action { 36, Kind::tools_overview },
    Action { 37, Kind::statistics },
    Action { 38, Kind::maintenance },
    Action { 39, Kind::sensor_info },
    Action { 40, Kind::network_status },
    Action { 45, Kind::calibrations },
    Action { 44, Kind::wiper_setup },
    Action { 42, Kind::lights },
    Action { 43, Kind::enclosure },
    Action { 46, Kind::filament_sensors },
    Action { 51, Kind::tool_settings },
    Action { 52, Kind::input_shaper },
    Action { 41, Kind::prusalink },
    Action { 72, Kind::set_ready },
    Action { 73, Kind::export_settings },
    Action { 47, Kind::macros_screen },
    Action { 16, Kind::macro, 1 },
    Action { 17, Kind::macro, 2 },
    Action { 18, Kind::macro, 3 },
    Action { 19, Kind::macro, 4 },
    Action { 20, Kind::macro, 5 },
    Action { 21, Kind::macro, 6 },
    Action { 22, Kind::macro, 7 },
    Action { 23, Kind::macro, 8 },
};
static_assert(actions[0].kind == Kind::none);

/// @returns the index in actions of the action of @p id; that of None for an unknown ID
constexpr size_t index_of(uint8_t id) {
    for (size_t i = 0; i < actions.size(); ++i) {
        if (actions[i].id == id) {
            return i;
        }
    }
    return 0;
}

/// @returns the action of @p id; None for an unknown ID
constexpr const Action &find(uint8_t id) {
    return actions[index_of(id)];
}

/// Characters a home screen label fits in every layout; no fixed label is longer
inline constexpr size_t home_label_length = 10;

/// Size of the parameter buffer a label needs
inline constexpr size_t label_buffer_size = user_macro::label_buffer_size;

/// @returns the label of @p action, formatted into @p params if needed
/// @param max_length longest macro name shown; the fixed labels fit home_label_length
string_view_utf8 label(const Action &action, StringViewUtf8ParamBase &params, size_t max_length = user_macro::max_name_length);

/// Runs the action of @p id. Before anything heats or moves, it asks Yes/No with No focused or
/// shows the list of the tools with Return on top, and drops the action if a print started meanwhile.
/// The screens open without a question.
void run(uint8_t id);

} // namespace home_shortcut
