#pragma once

#include <cstdint>

namespace leds {

/// What the RGB channels of the side strip show next to its white light
enum class SideStripStatusMode : uint8_t {
    /// No status colours, the strip behaves as without this setting
    off = 0,
    /// Tint by printer state
    status = 1,
    /// Tint by printer state; while printing, the tint moves from azure to green with the print progress
    status_and_progress = 2,
    _cnt = 3,
};

} // namespace leds
