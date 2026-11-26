#pragma once

#include "segmented_json.h"

#include <cstdint>

namespace nhttp::handler {

class MeshState {
public:
    /// Next point to render; kept here so the rendering can resume in another packet
    uint8_t x = 0;
    uint8_t y = 0;
};

/// Renders the bed leveling mesh:
///
/// {"grid":{"x":36,"y":36},"min":{"x":..,"y":..},"max":{"x":..,"y":..},"major":{"border":1,"step":3},
///  "state":"none|partial|complete","z":[[row y=0, x=0..], [row y=1], ...]}
///
/// min/max are the positions of the first/last grid point [mm]; z values are [mm] with 3 decimals,
/// null for points without a value. The mesh is read live, a response sent during probing can mix
/// old and new points.
class MeshRenderer final : public json::JsonRenderer<MeshState> {
public:
    MeshRenderer()
        : JsonRenderer(MeshState {}) {}
    virtual json::JsonResult renderState(size_t resume_point, json::JsonOutput &output, MeshState &state) const override;
};

} // namespace nhttp::handler
