#include "bed_tiles_renderer.h"

#include <json_encode.h>
#include <segmented_json_macros.h>

#include <array>
#include <cstdio>

namespace nhttp::handler {

namespace {

    /// Large enough for the longest tile entry and for the heater current
    using TileBuffer = std::array<char, 96>;

    const char *format_tile(const bed_tiles_data::Tile &tile, TileBuffer &buffer) {
        snprintf(buffer.data(), buffer.size(), "{\"number\":%u,\"temp\":%.1f,\"target\":%.1f,\"duty\":%u,\"enabled\":%s}",
            static_cast<unsigned>(tile.number),
            static_cast<double>(tile.temp),
            static_cast<double>(tile.target),
            static_cast<unsigned>(tile.duty),
            jsonify_bool(tile.enabled));
        return buffer.data();
    }

    const char *format_current(float current, TileBuffer &buffer) {
        snprintf(buffer.data(), buffer.size(), "%.2f", static_cast<double>(current));
        return buffer.data();
    }

} // namespace

json::JsonResult BedTilesRenderer::renderState(size_t resume_point, json::JsonOutput &output, BedTilesState &state) const {
    TileBuffer buffer;

    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
    JSON_OBJ_START;
        JSON_FIELD_OBJ("grid");
            JSON_FIELD_INT("x", bed_tiles_data::columns()) JSON_COMMA;
            JSON_FIELD_INT("y", bed_tiles_data::rows());
        JSON_OBJ_END JSON_COMMA;
        JSON_CUSTOM("\"heater_current\":%s", format_current(bed_tiles_data::heater_current(), buffer)) JSON_COMMA;
        JSON_FIELD_ARR("tiles");
            for (state.row = 0; state.row < bed_tiles_data::rows(); state.row++) {
                JSON_CUSTOM("%s[", state.row > 0 ? "," : "");
                for (state.column = 0; state.column < bed_tiles_data::columns(); state.column++) {
                    JSON_CUSTOM("%s%s", state.column > 0 ? "," : "", format_tile(bed_tiles_data::tile(state.column, state.row), buffer));
                }
                JSON_ARR_END;
            }
        JSON_ARR_END;
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

} // namespace nhttp::handler
