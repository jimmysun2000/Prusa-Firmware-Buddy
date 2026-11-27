#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

/// Due arithmetic of the maintenance reminders.
/// Counters and baselines are in the counting unit (s, mm, picks), intervals in the setting unit (h, m, picks, months);
/// @p unit_size is the number of counting units in one setting unit.
/// Calendar rows count the real-time clock in s since 1970 (UTC). Their counter is 0 while the clock is unknown,
/// their baseline 0 until the counting could start (pending).
namespace maintenance_reminders {

/// Clock values before 2025-01-01 00:00 UTC are taken as a clock that was never set
inline constexpr int64_t first_valid_clock = 1'735'689'600;

/// Mean Gregorian month
inline constexpr uint32_t seconds_per_month = 2'629'746;

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

/// @param now the real-time clock in s since 1970, negative if it is not set
/// @returns the counter of a calendar row: @p now, or 0 (unknown) if it is not a date the counter can hold
constexpr uint32_t date_counter(int64_t now) {
    return (now >= first_valid_clock && now <= std::numeric_limits<uint32_t>::max()) ? static_cast<uint32_t>(now) : 0;
}

/// @returns how much time passed since the baseline of a calendar row; 0 while the clock is unknown,
/// the baseline is pending or the clock is before the baseline (set back)
constexpr uint32_t date_usage(uint32_t now, uint32_t baseline) {
    return (now == 0 || baseline == 0) ? 0 : usage(now, baseline);
}

/// @returns whether the pending baseline of a calendar row can start its counting now
constexpr bool needs_date_stamp(uint32_t now, uint32_t baseline) {
    return (now != 0) && (baseline == 0);
}

/// @param applied number of leading table rows that already got their default interval, as stored
/// @returns the first row that still has to get its default interval; @p item_count if none
constexpr size_t first_row_to_default(size_t applied, size_t item_count) {
    // A firmware with more rows may have stored a higher number
    return std::min(applied, item_count);
}

} // namespace maintenance_reminders
