#pragma once

#include <cstdint>

namespace leds {

/// What the RGB channels of the side strip show next to its white light
enum class SideStripStatusMode : uint8_t {
    /// No status colours, the strip behaves as without this setting
    off = 0,
    /// Tint by printer state
    status = 1,
    _cnt = 2,
};

/// @returns the mode a value read from the config store stands for.
/// 2 is the "State and progress" mode of older firmware, which settings backups can still bring
/// back; it reads as status. Unknown values read as off.
constexpr SideStripStatusMode side_strip_status_mode_from_stored(SideStripStatusMode stored) {
    constexpr uint8_t status_and_progress = 2;
    switch (static_cast<uint8_t>(stored)) {
    case static_cast<uint8_t>(SideStripStatusMode::status):
    case status_and_progress:
        return SideStripStatusMode::status;
    default:
        return SideStripStatusMode::off;
    }
}

} // namespace leds
