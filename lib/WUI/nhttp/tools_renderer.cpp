#include "tools_renderer.h"

#include <json_encode.h>
#include <segmented_json_macros.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace nhttp::handler {

namespace {

    using NumberBuffer = std::array<char, 16>;

    /// Large enough for the longest tool entry
    using ToolBuffer = std::array<char, 224>;

    const char *number_or_null(float value, int decimals, NumberBuffer &buffer) {
        // Anything this large is garbage, not a temperature or a length
        if (!std::isfinite(value) || std::fabs(value) >= 1.0e6f) {
            return "null";
        }
        snprintf(buffer.data(), buffer.size(), "%.*f", decimals, static_cast<double>(value));
        return buffer.data();
    }

    const char *format_active(std::optional<uint8_t> active, NumberBuffer &buffer) {
        if (!active.has_value()) {
            return "null";
        }
        snprintf(buffer.data(), buffer.size(), "%u", static_cast<unsigned>(*active));
        return buffer.data();
    }

    using FilamentName = decltype(tools_data::Tool::filament);

    /// Escaping at most doubles each character (the name holds no zero), plus quotes and terminating zero
    using FilamentBuffer = std::array<char, 2 * sizeof(FilamentName) + 3>;

    const char *format_filament(const FilamentName &raw, FilamentBuffer &buffer) {
        const std::string_view name(raw.data(), strnlen(raw.data(), raw.size()));
        if (name.empty()) {
            return "null";
        }
        if (jsonify_str_buffer(name) == 0) {
            snprintf(buffer.data(), buffer.size(), "\"%.*s\"", static_cast<int>(name.size()), name.data());
        } else {
            std::array<char, 2 * sizeof(FilamentName) + 1> escaped;
            jsonify_str(name, escaped.data());
            snprintf(buffer.data(), buffer.size(), "\"%s\"", escaped.data());
        }
        return buffer.data();
    }

    const char *format_tool(const tools_data::Tool &tool, ToolBuffer &buffer) {
        if (!tool.enabled) {
            return "{\"enabled\":false}";
        }

        FilamentBuffer filament;
        NumberBuffer temp, target, retracted, diameter;
        snprintf(buffer.data(), buffer.size(),
            "{\"enabled\":true,\"temp\":%s,\"target\":%s,\"filament\":%s,\"picked\":%s,\"parked\":%s,\"retracted\":%s,\"nozzle_diameter\":%s}",
            number_or_null(tool.temp, 1, temp),
            number_or_null(tool.target, 1, target),
            format_filament(tool.filament, filament),
            jsonify_bool(tool.picked),
            jsonify_bool(tool.parked),
            tool.retracted.has_value() ? number_or_null(*tool.retracted, 0, retracted) : "null",
            number_or_null(tool.nozzle_diameter, 2, diameter));
        return buffer.data();
    }

} // namespace

json::JsonResult ToolsRenderer::renderState(size_t resume_point, json::JsonOutput &output, ToolsState &state) const {
    NumberBuffer active_buffer;
    ToolBuffer tool_buffer;

    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
    JSON_OBJ_START;
        JSON_CUSTOM("\"active\":%s", format_active(tools_data::active(), active_buffer)) JSON_COMMA;
        JSON_FIELD_ARR("tools");
            for (state.tool = 0; state.tool < tools_data::count(); state.tool++) {
                JSON_CUSTOM("%s%s", state.tool > 0 ? "," : "", format_tool(tools_data::tool(state.tool), tool_buffer));
            }
        JSON_ARR_END;
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

} // namespace nhttp::handler
