/// @file
#pragma once

#include <cstdint>

namespace bed_mesh_measurement {

/// The nozzle temperature M9170 measures the bed mesh at, the one the stock start G-code probes at [°C]
inline constexpr int16_t nozzle_temperature = 170;

/// The longest M9170 waits for the bed frame to absorb heat; then it measures anyway [min]
inline constexpr uint16_t soak_limit_minutes = 60;

} // namespace bed_mesh_measurement
