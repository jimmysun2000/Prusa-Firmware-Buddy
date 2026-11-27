#pragma once

#include "segmented_json.h"

#include <cstdint>

namespace nhttp::handler {

class MeshHistoryState {
public:
    /// Entry to render, 0 for the list
    uint16_t id;

    /// Kept here so the rendering can resume in another packet: next entry of the list, next row of an entry
    uint16_t list_id = 0;
    uint8_t row = 0;

    /// Entries of the list rendered so far
    uint8_t listed = 0;
};

/// Renders the bed mesh history (see bed_mesh_history.hpp), newest entry first:
///
/// {"uptime":5630,"time":1795750935,"capacity":8,"points":144,"entries":[{"id":2,"uptime":5025,
///  "time":1795750330,"bed":{"target":60,"temp":59.8},"tool":0,"printing":true,"probed_points":36,
///  "z_min":-0.112,"z_max":0.094},...]}
///
/// or, for one entry, the same fields as an entry of the list followed by the fields of the mesh JSON
/// (MeshRenderer) for the stored points:
///
/// {"id":2,...,"z_max":0.094,"grid":{"x":12,"y":12},"min":{"x":10.286,"y":10.286},
///  "max":{"x":349.714,"y":349.714},"major":{"border":0,"step":1},"state":"complete",
///  "z":[[row y=0, x=0..11], ...],"probed":[[0|1, ...], ...]}
///
/// uptime is [s] since boot; time is UTC [s since the epoch], null if the printer clock is not set.
/// The list carries both for now, each entry for the end of its measurement. bed is [°C], temp is
/// null if not known; tool is 0-based, null if none was picked; z_min, z_max and z are [mm] with 3
/// decimals, null for a point without a value.
///
/// Every part of an entry is read when it is output. If the entry is replaced by a newer one during
/// the response, the rendering is aborted, so a response never mixes two measurements.
class MeshHistoryRenderer final : public json::JsonRenderer<MeshHistoryState> {
public:
    /// Renders entry @p id of the history of the printer, the list if 0
    MeshHistoryRenderer(uint16_t id = 0)
        : JsonRenderer(MeshHistoryState { .id = id }) {}
    virtual json::JsonResult renderState(size_t resume_point, json::JsonOutput &output, MeshHistoryState &state) const override;

private:
    json::JsonResult render_list(size_t resume_point, json::JsonOutput &output, MeshHistoryState &state) const;
    json::JsonResult render_entry(size_t resume_point, json::JsonOutput &output, MeshHistoryState &state) const;
};

} // namespace nhttp::handler
