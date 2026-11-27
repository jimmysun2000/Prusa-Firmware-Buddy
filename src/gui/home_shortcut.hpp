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
};

struct Action {
    /// Stored in the config store: never change or reuse one
    uint8_t id;
    Kind kind;
    /// Macro number, for Kind::macro
    uint8_t macro = 0;
};

/// In the order of the selection menu
inline constexpr std::array actions {
    Action { 0, Kind::none },
    Action { 1, Kind::auto_home },
    Action { 2, Kind::pick_park },
    Action { 3, Kind::move_axis },
    Action { 4, Kind::motors_off },
    Action { 5, Kind::wipe_nozzle },
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

/// Runs the action of @p id. The ones that act at once ask Yes/No first.
void run(uint8_t id);

} // namespace home_shortcut
