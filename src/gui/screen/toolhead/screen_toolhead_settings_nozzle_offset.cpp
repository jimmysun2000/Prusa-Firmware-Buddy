#include "screen_toolhead_settings_nozzle_offset.hpp"

#if HAS_SELFTEST() && HAS_TOOL_OFFSET_PIN_CALIBRATION()
    #include <screen_menu_selftest_snake.hpp>
    #include <selftest_action_helpers.hpp>
#endif

using namespace screen_toolhead_settings;

static constexpr std::array<NumericInputConfig, 3> offset_configs {
    NumericInputConfig {
        .min_value = X_MIN_OFFSET,
        .max_value = X_MAX_OFFSET,
        .step = 0.01f,
        .max_decimal_places = 2,
        .unit = Unit::millimeter,
    },
    NumericInputConfig {
        .min_value = Y_MIN_OFFSET,
        .max_value = Y_MAX_OFFSET,
        .step = 0.01f,
        .max_decimal_places = 2,
        .unit = Unit::millimeter,
    },
    NumericInputConfig {
        .min_value = Z_MIN_OFFSET,
        .max_value = Z_MAX_OFFSET,
        .step = 0.01f,
        .max_decimal_places = 2,
        .unit = Unit::millimeter,
    },
};

// * MI_NOZZLE_OFFSET_COMPONENT
MI_NOZZLE_OFFSET_COMPONENT::MI_NOZZLE_OFFSET_COMPONENT(uint8_t component, Toolhead toolhead)
    : MI_TOOLHEAD_SPECIFIC_SPIN(toolhead, 0, offset_configs[component], string_view_utf8())
    , component_(component) //
{
    SetLabel(_("Offset %c").formatted(label_params_, "XYZ"[component]));
    update();
}

float MI_NOZZLE_OFFSET_COMPONENT::read_value_impl(PhysicalToolIndex ix) {
    return hotend_offset[ix][component_];
}

void MI_NOZZLE_OFFSET_COMPONENT::store_value_impl(PhysicalToolIndex ix, float set) {
    hotend_offset[ix][component_] = set;
    prusa_toolchanger.save_tool_offsets();
}

#if HAS_SELFTEST() && HAS_TOOL_OFFSET_PIN_CALIBRATION()
// * MI_NOZZLE_OFFSET_CALIBRATE
MI_NOZZLE_OFFSET_CALIBRATE::MI_NOZZLE_OFFSET_CALIBRATE(Toolhead toolhead)
    : MI_TOOLHEAD_SPECIFIC_BASE(toolhead, _("Calibrate Nozzle Offset")) {
    update();
}

void MI_NOZZLE_OFFSET_CALIBRATE::update() {
    // Hidden when the contactless sensor calibrates the offsets or the toolchanger is disabled
    set_is_hidden(!std::holds_alternative<PhysicalToolIndex>(toolhead())
        || !SelftestSnake::has_tool_picker(SelftestSnake::Action::ToolOffsetsCalibration));
}

void MI_NOZZLE_OFFSET_CALIBRATE::click([[maybe_unused]] IWindowMenu &menu) {
    const Toolhead selected = toolhead();
    if (const auto *tool = std::get_if<PhysicalToolIndex>(&selected)) {
        SelftestSnake::start_single_tool_offset_calibration(*tool);
    }
}
#endif

// * ScreenToolheadDetailNozzleOffset
ScreenToolheadDetailNozzleOffset::ScreenToolheadDetailNozzleOffset(Toolhead toolhead)
    : ScreenMenu(_("NOZZLE OFFSET"))
    , toolhead(toolhead) //
{
    menu_set_toolhead(container, toolhead);
}
