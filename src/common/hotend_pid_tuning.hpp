#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

#include "hotend_pid_gains.hpp"

/// Relay (on/off) autotuning of the hotend PID, the method of upstream Marlin's M303:
/// the heater is switched fully on below the target and fully off above it, and the
/// gains are derived from the period and amplitude of the resulting oscillation.
namespace hotend_pid {

/// [°C] How far the temperature has to pass the target to count as crossing it.
/// The reported temperature has 1 °C steps, this keeps one step of noise from counting.
inline constexpr float CROSSING_HYSTERESIS = 1.0f;

/// [°C] Smallest mean peak-to-peak swing accepted. Each reported peak is up to 1 °C off,
/// so a smaller swing is not known well enough to compute gains from.
inline constexpr float MIN_PEAK_TO_PEAK = 4.0f;

/// Largest accepted spread (max - min) of the cycle periods, relative to their mean
inline constexpr float MAX_PERIOD_SPREAD = 0.2f;

/// Largest accepted spread (max - min) of the cycle swings, relative to their mean
inline constexpr float MAX_PEAK_TO_PEAK_SPREAD = 0.25f;

/// [°C] Swing spread always accepted: two readings of the same swing can differ by
/// almost 2 °C at 1 °C resolution
inline constexpr float PEAK_TO_PEAK_SPREAD_ALLOWANCE = 2.0f;

/// [ms] Longest time the heater may stay in relay mode in one run, heat-up included
inline constexpr uint32_t RELAY_TIME_LIMIT_MS = 10 * 60 * 1000;

/// One oscillation cycle, from one rise through the target to the next
struct OscillationCycle {
    uint32_t period_ms;
    float max_temp;
    float min_temp;

    constexpr float peak_to_peak() const { return max_temp - min_temp; }
};

/// Splits the temperature of a relay-controlled heater into oscillation cycles.
/// A cycle starts whenever the temperature rises to target + CROSSING_HYSTERESIS after
/// having fallen to target - CROSSING_HYSTERESIS. The first settle_cycles complete cycles
/// carry the heat-up overshoot and are discarded, the next record_cycles are recorded.
class RelayOscillationRecorder {
public:
    static constexpr size_t settle_cycles = 2;
    static constexpr size_t record_cycles = 5;
    static constexpr size_t total_cycles = settle_cycles + record_cycles;

    explicit constexpr RelayOscillationRecorder(float target)
        : target_(target) {}

    /// Feeds one temperature sample taken at @p time_ms; samples after completion are ignored
    constexpr void add_sample(uint32_t time_ms, float temp) {
        if (is_complete()) {
            return;
        }

        if (temp >= target_ + CROSSING_HYSTERESIS) {
            if (side_ == Side::below) {
                start_cycle(time_ms, temp);
            }
            side_ = Side::above;
        } else if (temp <= target_ - CROSSING_HYSTERESIS) {
            side_ = Side::below;
        }

        if (cycle_started_) {
            cycle_max_ = std::max(cycle_max_, temp);
            cycle_min_ = std::min(cycle_min_, temp);
        }
    }

    /// @returns whether the temperature has risen through the target at least once
    constexpr bool has_reached_target() const { return cycle_started_; }

    /// @returns the number of complete cycles, settling ones included
    constexpr size_t cycle_count() const { return cycle_count_; }

    constexpr bool is_complete() const { return cycle_count_ >= total_cycles; }

    /// @returns the cycles recorded so far, all record_cycles of them once complete
    constexpr std::span<const OscillationCycle> recorded_cycles() const {
        const size_t recorded = cycle_count_ > settle_cycles ? cycle_count_ - settle_cycles : 0;
        return std::span(recorded_).first(recorded);
    }

private:
    enum class Side : uint8_t {
        unknown,
        below,
        above,
    };

    constexpr void start_cycle(uint32_t time_ms, float temp) {
        if (cycle_started_) {
            if (cycle_count_ >= settle_cycles) {
                recorded_[cycle_count_ - settle_cycles] = {
                    .period_ms = time_ms - cycle_start_ms_,
                    .max_temp = cycle_max_,
                    .min_temp = cycle_min_,
                };
            }
            ++cycle_count_;
        }
        cycle_started_ = true;
        cycle_start_ms_ = time_ms;
        cycle_max_ = temp;
        cycle_min_ = temp;
    }

    float target_;
    Side side_ = Side::unknown;
    bool cycle_started_ = false;
    uint32_t cycle_start_ms_ = 0;
    float cycle_max_ = 0;
    float cycle_min_ = 0;
    size_t cycle_count_ = 0;
    std::array<OscillationCycle, record_cycles> recorded_ {};
};

/// Why a tuning run produced no gains
enum class TuningFailure : uint8_t {
    none,
    invalid_request, ///< The tool, target temperature or fan speed is not allowed
    timeout, ///< The oscillation was not recorded within RELAY_TIME_LIMIT_MS
    amplitude_too_small, ///< The swing is below MIN_PEAK_TO_PEAK
    cycles_disagree, ///< The cycles differ more in period or swing than accepted
    out_of_limits, ///< The computed gains fail is_valid
};

struct TuningResult {
    TuningFailure failure;

    /// Computed gains; set also with out_of_limits, so that they can be shown
    HotendPIDGains gains;
};

/// Computes the gains from the recorded cycles with the classic Ziegler-Nichols rule of
/// upstream Marlin's autotune: Ku = 4 d / (pi a), Kp = 0.6 Ku, Ki = 2 Kp / Tu, Kd = Kp Tu / 8,
/// where a is half the mean swing [°C] and Tu the mean period [s]. The rule assumes a
/// symmetric relay; an oscillation that heats faster than it cools is not corrected for.
/// @param relay_half_output d, half the difference between the heater output fully on and off
///        [PWM]; a heater switched between 0 and 255 has 127.5
constexpr TuningResult compute_gains(std::span<const OscillationCycle> cycles, float relay_half_output) {
    if (cycles.empty()) {
        return { .failure = TuningFailure::cycles_disagree, .gains = {} };
    }

    float swing_sum = 0;
    float smallest_swing = cycles.front().peak_to_peak();
    float largest_swing = smallest_swing;
    float period_sum_ms = 0;
    uint32_t shortest_period_ms = cycles.front().period_ms;
    uint32_t longest_period_ms = shortest_period_ms;
    for (const OscillationCycle &cycle : cycles) {
        swing_sum += cycle.peak_to_peak();
        smallest_swing = std::min(smallest_swing, cycle.peak_to_peak());
        largest_swing = std::max(largest_swing, cycle.peak_to_peak());
        period_sum_ms += static_cast<float>(cycle.period_ms);
        shortest_period_ms = std::min(shortest_period_ms, cycle.period_ms);
        longest_period_ms = std::max(longest_period_ms, cycle.period_ms);
    }
    const float count = static_cast<float>(cycles.size());
    const float mean_swing = swing_sum / count;
    const float mean_period_ms = period_sum_ms / count;

    if (!(mean_swing >= MIN_PEAK_TO_PEAK)) {
        return { .failure = TuningFailure::amplitude_too_small, .gains = {} };
    }

    if (shortest_period_ms == 0 || static_cast<float>(longest_period_ms - shortest_period_ms) > MAX_PERIOD_SPREAD * mean_period_ms) {
        return { .failure = TuningFailure::cycles_disagree, .gains = {} };
    }

    if (largest_swing - smallest_swing > std::max(PEAK_TO_PEAK_SPREAD_ALLOWANCE, MAX_PEAK_TO_PEAK_SPREAD * mean_swing)) {
        return { .failure = TuningFailure::cycles_disagree, .gains = {} };
    }

    const float amplitude = mean_swing / 2;
    const float period_s = mean_period_ms / 1000;
    const float ku = 4 * relay_half_output / (std::numbers::pi_v<float> * amplitude);
    const float kp = 0.6f * ku;
    const HotendPIDGains gains {
        .kp = kp,
        .ki = 2 * kp / period_s,
        .kd = kp * period_s / 8,
    };

    return {
        .failure = is_valid(gains) ? TuningFailure::none : TuningFailure::out_of_limits,
        .gains = gains,
    };
}

} // namespace hotend_pid
