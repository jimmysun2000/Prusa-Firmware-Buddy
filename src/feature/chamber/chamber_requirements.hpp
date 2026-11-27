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

    /// Takes the chamber parameters of one more used filament into account.
    /// A missing parameter places no requirement.
    constexpr void add(OptionalTemperature min_temperature, OptionalTemperature max_temperature, OptionalTemperature target_temperature) {
        raise(target, target_temperature);
        raise(soak_goal, min_temperature);
        if (max_temperature.has_value() && (!limit.has_value() || *max_temperature < *limit)) {
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

private:
    static constexpr void raise(OptionalTemperature &value, OptionalTemperature candidate) {
        if (candidate.has_value() && (!value.has_value() || *candidate > *value)) {
            value = candidate;
        }
    }
};

/// \returns the chamber requirements of the filaments in the tools the current print uses
/// Empty while the G-code info of the print is not loaded.
ChamberRequirements print_chamber_requirements();

} // namespace buddy
