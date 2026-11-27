/// @file
#pragma once

#include <cstdint>
#include <optional>

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

} // namespace filament_preset_override
