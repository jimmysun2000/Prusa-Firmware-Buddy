#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

struct PrintLayerInfo {
    uint16_t current;
    uint16_t total;
    bool exact; ///< Reported by the G-code, not estimated
};

/// Layer progress the G-code reported with M73 L K.
///
/// @param current 1-based current layer, 0 = not reported
/// @param total layer count, 0 = not reported
/// @returns std::nullopt unless both are reported and current <= total
inline std::optional<PrintLayerInfo> reported_layers(uint16_t current, uint16_t total) {
    if (current == 0 || total == 0 || current > total) {
        return std::nullopt;
    }

    return PrintLayerInfo {
        .current = current,
        .total = total,
        .exact = true,
    };
}

/// Estimates the layer progress assuming all layers have the same height.
///
/// Off by up to one layer when the first layer height differs, wrong with
/// variable layer height, and stuck at the top in sequential printing.
///
/// @param layer_height slicer layer height [mm]
/// @param max_layer_z top of the last layer [mm]
/// @param printed_z highest Z printed so far [mm]
/// @returns std::nullopt if any input is not positive (nothing printed yet included)
///          or the layer count does not fit; otherwise current is clamped to [1, total]
inline std::optional<PrintLayerInfo> estimate_layers(float layer_height, float max_layer_z, float printed_z) {
    // Negated comparisons reject NaN too
    if (!(layer_height > 0) || !(max_layer_z > 0) || !(printed_z > 0)) {
        return std::nullopt;
    }

    const float total = std::round(max_layer_z / layer_height);
    if (!(total >= 1 && total <= std::numeric_limits<uint16_t>::max())) {
        return std::nullopt;
    }

    const float current = std::clamp(std::round(printed_z / layer_height), 1.0f, total);
    return PrintLayerInfo {
        .current = static_cast<uint16_t>(current),
        .total = static_cast<uint16_t>(total),
        .exact = false,
    };
}

/// Layer progress of the running print: as reported by M73 L K, otherwise
/// estimated from the G-code metadata (layer_height, max_layer_z) and the
/// highest printed Z.
///
/// @returns std::nullopt if nothing is reported and the metadata is missing
///          or nothing has been printed yet
std::optional<PrintLayerInfo> print_layer_info();
