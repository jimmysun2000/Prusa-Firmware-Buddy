#include "mesh_history_renderer.h"

#include <common/bed_mesh_history.hpp>
#include <segmented_json_macros.h>

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

using bed_mesh_history::Row;
using bed_mesh_history::Summary;

namespace nhttp::handler {

namespace {

    using NumberBuffer = std::array<char, 16>;

    /// Large enough for any token, so each one fits into an empty send buffer
    using TokenBuffer = std::array<char, 192>;

    static_assert(bed_mesh_history::points_x * sizeof(",-32.768") + sizeof("[]") <= sizeof(TokenBuffer));

    const char *format_mm(int16_t um, NumberBuffer &buffer) {
        if (um == bed_mesh_history::no_value_um) {
            return "null";
        }
        const int magnitude = std::abs(um);
        snprintf(buffer.data(), buffer.size(), "%s%d.%03d", um < 0 ? "-" : "", magnitude / 1000, magnitude % 1000);
        return buffer.data();
    }

    const char *format_time(uint32_t time_s, NumberBuffer &buffer) {
        if (time_s == 0) {
            return "null";
        }
        snprintf(buffer.data(), buffer.size(), "%" PRIu32, time_s);
        return buffer.data();
    }

    const char *format_decidegrees(int16_t decidegrees, NumberBuffer &buffer) {
        if (decidegrees == bed_mesh_history::no_temperature) {
            return "null";
        }
        const int magnitude = std::abs(decidegrees);
        snprintf(buffer.data(), buffer.size(), "%s%d.%d", decidegrees < 0 ? "-" : "", magnitude / 10, magnitude % 10);
        return buffer.data();
    }

    const char *format_tool(uint8_t tool, NumberBuffer &buffer) {
        if (tool == bed_mesh_history::no_tool) {
            return "null";
        }
        snprintf(buffer.data(), buffer.size(), "%u", static_cast<unsigned>(tool));
        return buffer.data();
    }

    const char *format_clock(const bed_mesh_history::Clock &clock, TokenBuffer &buffer) {
        NumberBuffer time;
        snprintf(buffer.data(), buffer.size(), "\"uptime\":%" PRIu32 ",\"time\":%s", clock.uptime_s, format_time(clock.time_s, time));
        return buffer.data();
    }

    /// The fields of an entry, without the braces around them
    const char *format_summary(const Summary &summary, TokenBuffer &buffer) {
        NumberBuffer time, temp, tool, z_min, z_max;
        snprintf(buffer.data(), buffer.size(),
            "\"id\":%u,\"uptime\":%" PRIu32 ",\"time\":%s,\"bed\":{\"target\":%d,\"temp\":%s},\"tool\":%s,\"printing\":%s,"
            "\"probed_points\":%u,\"z_min\":%s,\"z_max\":%s",
            static_cast<unsigned>(summary.id),
            summary.meta.uptime_s,
            format_time(summary.meta.time_s, time),
            static_cast<int>(summary.meta.bed_target),
            format_decidegrees(summary.meta.bed_temp, temp),
            format_tool(summary.meta.tool, tool),
            summary.meta.printing ? "true" : "false",
            static_cast<unsigned>(summary.probed_count),
            format_mm(summary.z_min_um, z_min),
            format_mm(summary.z_max_um, z_max));
        return buffer.data();
    }

    const char *format_z_row(const Row &row, TokenBuffer &buffer) {
        size_t used = snprintf(buffer.data(), buffer.size(), "[");
        for (uint8_t ix = 0; ix < bed_mesh_history::points_x; ix++) {
            NumberBuffer value;
            used += snprintf(buffer.data() + used, buffer.size() - used, "%s%s", ix > 0 ? "," : "", format_mm(row.z_um[ix], value));
        }
        snprintf(buffer.data() + used, buffer.size() - used, "]");
        return buffer.data();
    }

    const char *format_probed_row(const Row &row, TokenBuffer &buffer) {
        size_t used = snprintf(buffer.data(), buffer.size(), "[");
        for (uint8_t ix = 0; ix < bed_mesh_history::points_x; ix++) {
            used += snprintf(buffer.data() + used, buffer.size() - used, "%s%d", ix > 0 ? "," : "", (row.probed >> ix) & 1);
        }
        snprintf(buffer.data() + used, buffer.size() - used, "]");
        return buffer.data();
    }

} // namespace

json::JsonResult MeshHistoryRenderer::renderState(size_t resume_point, json::JsonOutput &output, MeshHistoryState &state) const {
    return state.id == 0 ? render_list(resume_point, output, state) : render_entry(resume_point, output, state);
}

json::JsonResult MeshHistoryRenderer::render_list(size_t resume_point, json::JsonOutput &output, MeshHistoryState &state) const {
    const bed_mesh_history::Store &store = bed_mesh_history::store();
    Summary summary;
    TokenBuffer buffer;

    // Every entry token reads its entry again, also after a resume. The ids of the held entries are
    // consecutive, so going down from the newest one never repeats an entry; it ends at the first id
    // that is not held, also when an entry is added during the response.

    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
    JSON_OBJ_START;
        JSON_CUSTOM("%s", format_clock(bed_mesh_history::now(), buffer)) JSON_COMMA;
        JSON_FIELD_INT("capacity", bed_mesh_history::capacity) JSON_COMMA;
        JSON_FIELD_INT("points", bed_mesh_history::points) JSON_COMMA;
        JSON_FIELD_ARR("entries");
            for (state.list_id = store.newest_id(); store.summary(state.list_id, summary); state.list_id--) {
                JSON_OUT(__COUNTER__, store.summary(state.list_id, summary), output, "%s{%s}", state.listed > 0 ? "," : "", format_summary(summary, buffer));
                state.listed++;
            }
        JSON_ARR_END;
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

json::JsonResult MeshHistoryRenderer::render_entry(size_t resume_point, json::JsonOutput &output, MeshHistoryState &state) const {
    const bed_mesh_history::Store &store = bed_mesh_history::store();
    const bed_mesh_view::Info info = bed_mesh_view::major_info(bed_mesh_view::info());
    Summary summary;
    Row row;
    TokenBuffer buffer;

    // Every token with data of the entry reads it again, also after a resume, and aborts the rendering
    // once the entry is no longer held.

    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
        JSON_OUT(__COUNTER__, store.summary(state.id, summary), output, "{%s", format_summary(summary, buffer)) JSON_COMMA;
        JSON_FIELD_OBJ("grid");
            JSON_FIELD_INT("x", info.nx) JSON_COMMA;
            JSON_FIELD_INT("y", info.ny);
        JSON_OBJ_END JSON_COMMA;
        JSON_FIELD_OBJ("min");
            JSON_FIELD_FFIXED("x", static_cast<double>(info.min_x), 3) JSON_COMMA;
            JSON_FIELD_FFIXED("y", static_cast<double>(info.min_y), 3);
        JSON_OBJ_END JSON_COMMA;
        JSON_FIELD_OBJ("max");
            JSON_FIELD_FFIXED("x", static_cast<double>(info.max_x), 3) JSON_COMMA;
            JSON_FIELD_FFIXED("y", static_cast<double>(info.max_y), 3);
        JSON_OBJ_END JSON_COMMA;
        JSON_FIELD_OBJ("major");
            JSON_FIELD_INT("border", info.border) JSON_COMMA;
            JSON_FIELD_INT("step", info.major_step);
        JSON_OBJ_END JSON_COMMA;
        // Only complete meshes are stored
        JSON_FIELD_STR("state", bed_mesh_view::to_str(bed_mesh_view::State::complete)) JSON_COMMA;
        JSON_FIELD_ARR("z");
            for (state.row = 0; state.row < bed_mesh_history::points_y; state.row++) {
                JSON_OUT(__COUNTER__, store.row(state.id, state.row, row), output, "%s%s", state.row > 0 ? "," : "", format_z_row(row, buffer));
            }
        JSON_ARR_END JSON_COMMA;
        JSON_FIELD_ARR("probed");
            for (state.row = 0; state.row < bed_mesh_history::points_y; state.row++) {
                JSON_OUT(__COUNTER__, store.row(state.id, state.row, row), output, "%s%s", state.row > 0 ? "," : "", format_probed_row(row, buffer));
            }
        JSON_ARR_END;
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

} // namespace nhttp::handler
