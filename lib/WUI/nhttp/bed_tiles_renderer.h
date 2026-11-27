#pragma once

#include "segmented_json.h"

#include <cstdint>

namespace nhttp::handler {

/// Reads of the heatbed tiles for the bed tiles route. They are called from the HTTP thread, so each read
/// is lock-free.
namespace bed_tiles_data {

    /// One heatbed tile, with the values of the Bed Tiles screen
    struct Tile {
        /// Number of the tile as printed on the bed, from 1
        uint8_t number = 0;

        /// [°C]
        float temp = 0;

        /// Target last written to the tile [°C], 0 if it is off
        float target = 0;

        /// Heater duty after the bed's own current limiting [%]
        uint8_t duty = 0;

        /// The tile belongs to the heated area while the bed heats
        bool enabled = false;
    };

    /// @returns the number of tiles along X
    uint8_t columns();

    /// @returns the number of tiles along Y
    uint8_t rows();

    /// @returns tile @p column (from the left, below columns()) in @p row (from the front, below rows())
    Tile tile(uint8_t column, uint8_t row);

    /// @returns the combined heater current of the bed [A]
    float heater_current();

} // namespace bed_tiles_data

class BedTilesState {
public:
    /// Next tile to render; kept here so the rendering can resume in another packet
    uint8_t column = 0;
    uint8_t row = 0;
};

/// Renders the heatbed tiles:
///
/// {"grid":{"x":4,"y":4},"heater_current":2.41,"tiles":[[row y=0, x=0..], [row y=1], ...]}
///
/// with each tile as {"number":7,"temp":60.1,"target":60.0,"duty":35,"enabled":true}. Row y=0 is the
/// front of the bed, x=0 its left side. temp and target are [°C], target is 0 for a tile that is off;
/// duty is the heater duty [%]; enabled tells the tile is part of the heated area while the bed heats;
/// heater_current is the combined heater current [A]. Every tile is read when it is output.
class BedTilesRenderer final : public json::JsonRenderer<BedTilesState> {
public:
    BedTilesRenderer()
        : JsonRenderer(BedTilesState {}) {}
    virtual json::JsonResult renderState(size_t resume_point, json::JsonOutput &output, BedTilesState &state) const override;
};

} // namespace nhttp::handler
