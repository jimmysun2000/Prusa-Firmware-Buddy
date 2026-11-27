#pragma once

#include "segmented_json.h"

#include <array>
#include <cstdint>
#include <optional>

namespace nhttp::handler {

/// Reads of the tool state for the tools route. They are called from the HTTP thread, so each read is
/// lock-free or goes through the config store.
namespace tools_data {

    /// One tool position, with the values of the Tools Overview menu
    struct Tool {
        /// A tool is installed in this position; the other members are meaningful only if true
        bool enabled = false;

        /// Nozzle temperature [°C], NaN if not known
        float temp = 0;

        /// Nozzle target temperature [°C]
        float target = 0;

        /// Name of the loaded filament type, empty if none is loaded
        std::array<char, 8> filament {};

        /// The tool is on the head according to its sensor
        bool picked = false;

        /// The tool is in its dock according to its sensor
        bool parked = false;

        /// Filament length pulled back from the nozzle [mm], nullopt if not known
        std::optional<float> retracted;

        /// [mm]
        float nozzle_diameter = 0;
    };

    /// @returns the number of tool positions
    uint8_t count();

    /// @returns the tool position picked by the printer, nullopt if none
    std::optional<uint8_t> active();

    /// @returns the state of tool position @p index, which is below count()
    Tool tool(uint8_t index);

} // namespace tools_data

class ToolsState {
public:
    /// Next tool to render; kept here so the rendering can resume in another packet
    uint8_t tool = 0;
};

/// Renders the state of every tool position:
///
/// {"active":0,"tools":[{"enabled":true,"temp":215.0,"target":215.0,"filament":"PLA","picked":true,
///  "parked":false,"retracted":0,"nozzle_diameter":0.40},{"enabled":false},...]}
///
/// "active" is the index of the picked tool, null if none. "tools" has one entry per tool position; a
/// position without a tool has only "enabled":false. temp and target are [°C], temp is null if not
/// known; filament is null if none is loaded; retracted is the filament length pulled back from the
/// nozzle [mm], null if not known; nozzle_diameter is [mm]. Every entry is read when it is output, all
/// of its values at once.
class ToolsRenderer final : public json::JsonRenderer<ToolsState> {
public:
    ToolsRenderer()
        : JsonRenderer(ToolsState {}) {}
    virtual json::JsonResult renderState(size_t resume_point, json::JsonOutput &output, ToolsState &state) const override;
};

} // namespace nhttp::handler
