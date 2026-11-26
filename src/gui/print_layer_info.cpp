#include "print_layer_info.hpp"

#include <common/marlin_vars.hpp>
#include <gcode_info.hpp>

std::optional<PrintLayerInfo> print_layer_info() {
    const auto &info = GCodeInfo::getInstance();
    const auto layer_height = info.get_layer_height();
    const auto max_layer_z = info.get_max_layer_z();
    if (!layer_height || !max_layer_z) {
        return std::nullopt;
    }

    return estimate_layers(*layer_height, *max_layer_z, marlin_vars().max_printed_z.get());
}
