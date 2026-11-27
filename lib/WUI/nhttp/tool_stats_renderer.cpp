#include "tool_stats_renderer.h"

#include <segmented_json_macros.h>

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdio>

namespace nhttp::handler {

namespace {

    /// Large enough for the longest entry
    using EntryBuffer = std::array<char, 96>;

    const char *format_tool(const tool_stats_data::ToolStats &stats, EntryBuffer &buffer) {
        std::array<char, 24> filament;
        // Bounded to fit the buffer; no real extruded length comes near it
        if (std::isfinite(stats.filament_used) && std::fabs(stats.filament_used) < 1.0e15f) {
            snprintf(filament.data(), filament.size(), "%.1f", static_cast<double>(stats.filament_used));
        } else {
            snprintf(filament.data(), filament.size(), "null");
        }
        snprintf(buffer.data(), buffer.size(), "{\"filament_used\":%s,\"picks\":%" PRIu32 ",\"print_time\":%" PRIu32 "}",
            filament.data(), stats.picks, stats.print_time);
        return buffer.data();
    }

} // namespace

json::JsonResult ToolStatsRenderer::renderState(size_t resume_point, json::JsonOutput &output, ToolStatsState &state) const {
    EntryBuffer buffer;

    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
    JSON_OBJ_START;
        JSON_FIELD_ARR("tools");
            for (state.tool = 0; state.tool < tool_stats_data::count(); state.tool++) {
                JSON_CUSTOM("%s%s", state.tool > 0 ? "," : "", format_tool(tool_stats_data::tool(state.tool), buffer));
            }
        JSON_ARR_END JSON_COMMA;
        JSON_FIELD_INT("tool_recoveries", tool_stats_data::tool_recoveries());
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

} // namespace nhttp::handler
