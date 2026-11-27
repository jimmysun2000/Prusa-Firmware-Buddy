/// @file
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

/// Purge speed shared by the purge screen (GUI thread) and the purge (Marlin thread)
namespace purge_speed {

/// Purge speed of the running load or change [0.1 mm/s], before the halving for flexible filament.
///
/// The load sets it from the Purge Speed setting when it starts, the knob on the purge screen changes it
/// and the purge follows it. It is never stored, so a dialled speed lasts until the load is over.
/// Every reader clamps it to the range of the setting.
inline std::atomic<uint8_t> in_use { 0 };
static_assert(std::atomic<uint8_t>::is_always_lock_free);

/// @returns \p value clamped to \p lowest .. \p highest (as it is shown), moved by \p knob_diff steps of 0.1 mm/s
/// and clamped again
constexpr uint8_t adjusted(uint8_t value, int knob_diff, uint8_t lowest, uint8_t highest) {
    constexpr int max_diff = std::numeric_limits<uint8_t>::max();
    const int moved = std::clamp<int>(value, lowest, highest) + std::clamp(knob_diff, -max_diff, max_diff);
    return static_cast<uint8_t>(std::clamp<int>(moved, lowest, highest));
}

} // namespace purge_speed
