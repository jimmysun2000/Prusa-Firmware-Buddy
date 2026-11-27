/// @file
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

/// Values of a preset filament type that the user changed, stored over its built-in values.
/// Dependency-free, so that it can be unit tested.
namespace filament_preset_override {

/// Stored value that reads as the built-in value
inline constexpr uint8_t as_built_in = 255;

/// \returns the value in effect: \p built_in for as_built_in, else \p stored
constexpr std::optional<uint8_t> decode(uint8_t stored, std::optional<uint8_t> built_in) {
    return stored == as_built_in ? built_in : std::optional<uint8_t>(stored);
}

/// \returns the value to store for \p value over \p built_in.
/// An empty value and the built-in value itself are stored as as_built_in, so a stored value
/// other than as_built_in always differs from the built-in one. A \p value of 255 reads back as \p built_in.
constexpr uint8_t encode(std::optional<uint8_t> value, std::optional<uint8_t> built_in) {
    return (!value.has_value() || value == built_in) ? as_built_in : *value;
}

/// The values of a filament type that the user can change for a preset
struct Adjustable {
    std::optional<uint8_t> chamber_min_temperature;
    std::optional<uint8_t> chamber_max_temperature;
    std::optional<uint8_t> chamber_target_temperature;

    /// Bed cool-down rate [0.1 °C/min], nullopt = the Cooldown Rate setting (also where filaments have no rate)
    std::optional<uint8_t> cooldown_rate;

    constexpr bool operator==(const Adjustable &) const = default;
};

/// \returns whether a filament copy loaded in a tool takes over an edit of a preset from \p before to \p after.
/// Loading a preset with a color makes such a copy, with the name and values of the preset.
/// The copy follows while it has the name of the preset and its values from before the edit,
/// so a copy whose values or name the user changed keeps them. Any other copy that happens to match follows as well.
constexpr bool copy_follows_edit(std::string_view copy_name, const Adjustable &copy, std::string_view preset_name, const Adjustable &before, const Adjustable &after) {
    return before != after && copy_name == preset_name && copy == before;
}

} // namespace filament_preset_override
