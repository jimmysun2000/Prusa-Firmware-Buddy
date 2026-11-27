#include "bed_tiles_renderer.h"

#include <common/marlin_vars.hpp>
#include <puppies/modular_bed.hpp>

// These run on the HTTP thread. They use only the lock-free getters of the modular bed, which load
// atomics that the puppy task writes, and Marlin variables, which are atomics too; nothing takes the
// bed's mutex.

namespace nhttp::handler::bed_tiles_data {

using buddy::puppies::modular_bed;
using buddy::puppies::ModularBed;

uint8_t columns() {
    return ModularBed::BEDLET_MAX_X;
}

uint8_t rows() {
    return ModularBed::BEDLET_MAX_Y;
}

Tile tile(uint8_t column, uint8_t row) {
    // As on the Bed Tiles screen: the heated area matters only while the bed is heating
    const uint16_t enabled_mask = (marlin_vars().target_bed > 0) ? marlin_vars().enabled_bedlet_mask.get() : 0;
    const uint16_t index = modular_bed.idx(column, row);
    return Tile {
        .number = static_cast<uint8_t>(buddy::puppies::bedlet_idx_to_board_number(index)),
        .temp = modular_bed.get_temp(column, row),
        .target = modular_bed.get_written_target(column, row),
        .duty = modular_bed.get_heater_duty_percent(column, row),
        .enabled = (enabled_mask & (1 << index)) != 0,
    };
}

float heater_current() {
    return modular_bed.get_heater_current();
}

} // namespace nhttp::handler::bed_tiles_data
