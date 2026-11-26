#include "mesh_renderer.h"

#include <common/bed_mesh_view.hpp>
#include <segmented_json_macros.h>

namespace nhttp::handler {

json::JsonResult MeshRenderer::renderState(size_t resume_point, json::JsonOutput &output, MeshState &state) const {
    const auto info = bed_mesh_view::info();
    bed_mesh_view::JsonValueBuffer value_buffer;

    // Every value is read when it is output (also after a resume), so a NaN check can't go stale.

    // Keep the indentation of the JSON in here!
    // clang-format off
    JSON_START;
    JSON_OBJ_START;
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
        JSON_FIELD_STR("state", bed_mesh_view::to_str(bed_mesh_view::state())) JSON_COMMA;
        JSON_FIELD_ARR("z");
            for (state.y = 0; state.y < info.ny; state.y++) {
                JSON_CUSTOM("%s[", state.y > 0 ? "," : "");
                for (state.x = 0; state.x < info.nx; state.x++) {
                    JSON_CUSTOM("%s%s", state.x > 0 ? "," : "", bed_mesh_view::format_json_value(bed_mesh_view::z(state.x, state.y), value_buffer));
                }
                JSON_ARR_END;
            }
        JSON_ARR_END;
    JSON_OBJ_END;
    JSON_END;
    // clang-format on
}

} // namespace nhttp::handler
