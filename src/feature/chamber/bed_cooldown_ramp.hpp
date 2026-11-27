#pragma once

#include <algorithm>
#include <cstdint>

namespace buddy {

/// Bed targets of the controlled bed cool-down after a print in the XL enclosure.
///
/// The target starts at the start temperature and drops by whole degrees at
/// a fixed rate. When it would reach end_temperature, the bed is switched off
/// and the cool-down waits for the bed to get there, up to max_duration_s
/// from the start.
class BedCooldownRamp {
public:
    /// The cool-down is over once the bed is down to this [°C]
    static constexpr int16_t end_temperature = 50;

    /// Longest the cool-down runs, ramp and wait together [s]
    static constexpr uint32_t max_duration_s = 60 * 60;

    /// Rate limits [0.1 °C/min]
    static constexpr uint8_t min_rate = 5;
    static constexpr uint8_t max_rate = 30;

    /// \param start_temperature first bed target [°C]
    /// \param rate how fast the target drops [0.1 °C/min], clamped to min_rate..max_rate
    constexpr BedCooldownRamp(int16_t start_temperature, uint8_t rate)
        : start_temperature_(start_temperature)
        , rate_(std::clamp(rate, min_rate, max_rate)) {}

    constexpr int16_t start_temperature() const {
        return start_temperature_;
    }

    constexpr uint8_t rate() const {
        return rate_;
    }

    /// \returns the bed target \p elapsed_s into the cool-down; 0 once it would be at or below end_temperature
    constexpr int16_t bed_target(uint32_t elapsed_s) const {
        const uint64_t drop = uint64_t { rate_ } * elapsed_s / 600;
        if (start_temperature_ <= end_temperature || drop >= static_cast<uint64_t>(start_temperature_ - end_temperature)) {
            return 0;
        }
        return static_cast<int16_t>(start_temperature_ - static_cast<int16_t>(drop));
    }

    /// \returns how long the bed target stays on [s], not limited by max_duration_s
    constexpr uint32_t ramp_duration_s() const {
        if (start_temperature_ <= end_temperature) {
            return 0;
        }
        const uint32_t degrees = start_temperature_ - end_temperature;
        return (degrees * 600 + rate_ - 1) / rate_;
    }

    /// \returns whole minutes, rounded up, until the bed target is switched off,
    /// counting the switch-off at max_duration_s; 0 from then on
    constexpr uint16_t minutes_left(uint32_t elapsed_s) const {
        const uint32_t end_s = std::min(ramp_duration_s(), max_duration_s);
        if (elapsed_s >= end_s) {
            return 0;
        }
        return static_cast<uint16_t>((end_s - elapsed_s + 59) / 60);
    }

private:
    int16_t start_temperature_;
    uint8_t rate_;
};

} // namespace buddy
