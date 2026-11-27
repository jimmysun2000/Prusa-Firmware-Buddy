#include "enclosure_renderer.h"

#include <segmented_json_macros.h>

#include <array>
#include <cmath>
#include <cstdio>

namespace nhttp::handler {

namespace {

    using NumberBuffer = std::array<char, 16>;

    const char *format_temperature(std::optional<float> temperature, NumberBuffer &buffer) {
        // Anything this large is garbage, not a temperature
        if (!temperature.has_value() || !std::isfinite(*temperature) || std::fabs(*temperature) >= 1.0e6f) {
            return "null";
        }
        snprintf(buffer.data(), buffer.size(), "%.1f", static_cast<double>(*temperature));
        return buffer.data();
    }

} // namespace

json::JsonResult render_enclosure(size_t resume_point, json::JsonOutput &output) {
    NumberBuffer buffer;

    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
    JSON_OBJ_START;
        JSON_FIELD_BOOL("enabled", enclosure_data::enabled()) JSON_COMMA;
        JSON_CUSTOM("\"temp\":%s", format_temperature(enclosure_data::temperature(), buffer)) JSON_COMMA;
        JSON_FIELD_INT("fan_rpm", enclosure_data::fan_rpm());
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

} // namespace nhttp::handler
