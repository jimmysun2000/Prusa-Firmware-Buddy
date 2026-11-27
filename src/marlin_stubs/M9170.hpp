/// @file
#pragma once

#include <cstdint>

namespace bed_mesh_measurement {

/// The nozzle temperature M9170 measures the bed mesh at, the one the stock start G-code probes at [°C]
inline constexpr int16_t nozzle_temperature = 170;

/// The longest M9170 waits for the bed frame to absorb heat; then it measures anyway [min]
inline constexpr uint16_t soak_limit_minutes = 60;

/// The longest M9170 can wait for the bed frame to absorb heat at @p bed_temperature, for telling
/// it before the run [min]
///
/// Copies the stock rule, so it goes stale if the rule changes: the frame estimate follows the bed
/// by at most 0.06 + (100 - bed) * 0.0015 °C/s (Temperature::updateTemperaturesFromRawValues()),
/// and Temperature::wait_for_frame_heatup() waits until it is within 1 °C of the target, not at all
/// for a target of 25 °C or less. The longest wait is for a frame at 25 °C under a bed already at
/// the target; a colder room makes it a little longer.
/// @returns 0 when there is no wait, otherwise at least 1 and at most soak_limit_minutes
constexpr uint16_t soak_minutes_max(int16_t bed_temperature) {
    constexpr int16_t room_temperature = 25; // [°C]
    constexpr float finish_threshold = 1; // [°C]
    if (bed_temperature <= room_temperature) {
        return 0;
    }

    const float rate = 0.06f + (100 - bed_temperature) * 0.0015f; // [°C/s]
    if (rate <= 0) {
        // The estimate would never rise
        return soak_limit_minutes;
    }

    const float minutes = (bed_temperature - finish_threshold - room_temperature) / rate / 60;
    if (minutes >= soak_limit_minutes) {
        return soak_limit_minutes;
    }

    const auto whole = static_cast<uint16_t>(minutes);
    const auto rounded_up = static_cast<uint16_t>((minutes > whole) ? whole + 1 : whole);
    // Even a wait that is about to end takes a moment
    return (rounded_up > 0) ? rounded_up : 1;
}

static_assert(soak_minutes_max(25) == 0);
static_assert(soak_minutes_max(26) == 1);
static_assert(soak_minutes_max(60) == 5);
static_assert(soak_minutes_max(85) == 12);
static_assert(soak_minutes_max(110) == 32);
static_assert(soak_minutes_max(115) == 40);
static_assert(soak_minutes_max(140) == soak_limit_minutes);

} // namespace bed_mesh_measurement
