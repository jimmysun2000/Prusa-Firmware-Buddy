#pragma once

#include "segmented_json.h"

#include <cstdint>

namespace nhttp::handler {

/// Reads of the print statistics for the tool statistics route. They are called from the HTTP thread, so
/// each read is lock-free or goes through the config store.
namespace tool_stats_data {

    /// Statistics of one tool position, as the Print Statistics menu shows them
    struct ToolStats {
        /// Filament extruded by the tool [mm]
        float filament_used = 0;

        /// Number of times the tool was picked
        uint32_t picks = 0;

        /// Printing time with the tool picked [s]
        uint32_t print_time = 0;
    };

    /// @returns the number of tool positions
    uint8_t count();

    /// @returns the statistics of tool position @p index, which is below count()
    ToolStats tool(uint8_t index);

    /// @returns the number of times the printer asked to pick up a fallen or missing tool
    uint32_t tool_recoveries();

} // namespace tool_stats_data

class ToolStatsState {
public:
    /// Next tool to render; kept here so the rendering can resume in another packet
    uint8_t tool = 0;
};

/// Renders the print statistics of every tool position:
///
/// {"tools":[{"filament_used":123456.7,"picks":321,"print_time":36000},...],"tool_recoveries":2}
///
/// filament_used is [mm], null if not known; picks is the number of tool picks; print_time is the
/// printing time with the tool picked [s]. tool_recoveries counts the times the printer asked to pick
/// up a fallen or missing tool. The counters are kept since the statistics were last reset, including
/// positions without a tool. Every entry is read when it is output.
class ToolStatsRenderer final : public json::JsonRenderer<ToolStatsState> {
public:
    ToolStatsRenderer()
        : JsonRenderer(ToolStatsState {}) {}
    virtual json::JsonResult renderState(size_t resume_point, json::JsonOutput &output, ToolStatsState &state) const override;
};

} // namespace nhttp::handler
