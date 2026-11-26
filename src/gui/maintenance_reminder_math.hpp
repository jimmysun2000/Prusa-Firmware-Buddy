#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

/// Due arithmetic of the maintenance reminders.
/// Counters and baselines are in the counting unit (s, mm, picks), intervals in the setting unit (h, m, picks);
/// @p unit_size is the number of counting units in one setting unit.
namespace maintenance_reminders {

/// @returns how much the counter grew since the baseline; 0 if it is below the baseline (statistics reset)
constexpr uint32_t usage(uint32_t counter, uint32_t baseline) {
    return (counter > baseline) ? counter - baseline : 0;
}

/// @returns @p interval in the counting unit, saturated
constexpr uint32_t interval_in_counts(uint32_t interval, uint32_t unit_size) {
    const uint64_t result = uint64_t { interval } * unit_size;
    return static_cast<uint32_t>(std::min<uint64_t>(result, std::numeric_limits<uint32_t>::max()));
}

/// @returns whether @p usage reached @p interval; never when the interval is 0 (Off)
constexpr bool is_due(uint32_t usage, uint32_t interval, uint32_t unit_size) {
    return (interval != 0) && (usage >= interval_in_counts(interval, unit_size));
}

/// @returns the counter value from which a postponed reminder shows again: 10 % of the interval later
constexpr uint32_t postponed_until(uint32_t counter, uint32_t interval, uint32_t unit_size) {
    const uint32_t step = std::max<uint32_t>(interval_in_counts(interval, unit_size) / 10, 1);
    return (counter > std::numeric_limits<uint32_t>::max() - step) ? std::numeric_limits<uint32_t>::max() : counter + step;
}

/// @returns whether an interval change starts the counting anew (baseline = current counter): from Off to a value
constexpr bool starts_counting(uint32_t old_interval, uint32_t new_interval) {
    return (old_interval == 0) && (new_interval != 0);
}

} // namespace maintenance_reminders
