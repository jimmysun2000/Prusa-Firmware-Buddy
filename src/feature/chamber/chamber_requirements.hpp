#pragma once

#include <cstdint>
#include <optional>

namespace buddy {

/// What the filaments of a print need from the chamber, aggregated over the used tools
struct ChamberRequirements {
    using OptionalTemperature = std::optional<uint8_t>;

    /// Highest chamber target temperature
    OptionalTemperature target;

    /// Highest chamber minimum temperature, which the enclosure soak heats up to
    OptionalTemperature soak_goal;

    /// Lowest chamber maximum temperature
    OptionalTemperature limit;

    /// Slowest bed cool-down rate set by a filament [0.1 °C/min]
    std::optional<uint8_t> cooldown_rate;

    /// Whether a filament leaves its bed cool-down rate to the Cooldown Rate setting
    bool uses_global_cooldown_rate = false;

    /// Takes the chamber parameters of one more used filament into account.
    /// A missing parameter, or 0, places no requirement.
    constexpr void add(OptionalTemperature min_temperature, OptionalTemperature max_temperature, OptionalTemperature target_temperature) {
        raise(target, target_temperature);
        raise(soak_goal, min_temperature);
        if (is_set(max_temperature) && (!limit.has_value() || *max_temperature < *limit)) {
            limit = max_temperature;
        }
    }

    /// \returns whether the chamber should be heated: the target is above \p threshold
    /// and no filament has its maximum below the soak goal of another one
    constexpr bool arms_heating(float threshold) const {
        if (!target.has_value() || *target <= threshold) {
            return false;
        }
        return !(limit.has_value() && soak_goal.has_value() && *limit < *soak_goal);
    }

    /// Takes the bed cool-down rate of one more used filament into account.
    /// nullopt or 0 leaves it to the Cooldown Rate setting.
    constexpr void add_cooldown_rate(std::optional<uint8_t> rate) {
        if (!rate.has_value() || *rate == 0) {
            uses_global_cooldown_rate = true;
        } else if (!cooldown_rate.has_value() || *rate < *cooldown_rate) {
            cooldown_rate = rate;
        }
    }

    /// \returns the bed cool-down rate of the print [0.1 °C/min]: the slowest of the used filaments,
    /// those without a rate of their own counting with \p global_rate (the Cooldown Rate setting).
    /// 0 when \p global_rate is 0: Cooldown Rate Off turns the cool-down off for every filament.
    constexpr uint8_t effective_cooldown_rate(uint8_t global_rate) const {
        if (global_rate == 0) {
            return 0;
        }
        if (!cooldown_rate.has_value()) {
            return global_rate;
        }
        return (uses_global_cooldown_rate && global_rate < *cooldown_rate) ? global_rate : *cooldown_rate;
    }

private:
    /// The filament detail screen saves an unset chamber temperature as 0
    static constexpr bool is_set(OptionalTemperature value) {
        return value.has_value() && *value > 0;
    }

    static constexpr void raise(OptionalTemperature &value, OptionalTemperature candidate) {
        if (is_set(candidate) && (!value.has_value() || *candidate > *value)) {
            value = candidate;
        }
    }
};

/// \returns the chamber requirements of the filaments in the tools the current print uses
/// Empty while the G-code info of the print is not loaded.
ChamberRequirements print_chamber_requirements();

} // namespace buddy
