#include "screen_toolhead_settings_hotend_pid.hpp"

#include <bit>
#include <cmath>
#include <utility>

#include <ScreenFactory.hpp>
#include <ScreenHandler.hpp>
#include <config_store/store_instance.hpp>
#include <marlin_client.hpp>
#include <numeric_input_config_common.hpp>
#include <printer_selftest.hpp>
#include <selftest/selftest_data.hpp>
#include <window_msgbox.hpp>

using namespace screen_toolhead_settings;

namespace {

using Gain = MI_HOTEND_PID_GAIN::Gain;

constexpr std::array<float HotendPIDGains::*, 3> gain_members {
    &HotendPIDGains::kp,
    &HotendPIDGains::ki,
    &HotendPIDGains::kd,
};

constexpr std::array<const char *, 3> gain_labels { "Kp", "Ki", "Kd" };

constexpr std::array<NumericInputConfig, 3> gain_configs {
    NumericInputConfig {
        .min_value = HotendPIDGains::kp_min,
        .max_value = HotendPIDGains::kp_max,
        .step = 0.1f,
        .max_decimal_places = 2,
    },
    NumericInputConfig {
        .min_value = HotendPIDGains::ki_min,
        .max_value = HotendPIDGains::ki_max,
        .step = 0.01f,
        .max_decimal_places = 3,
    },
    NumericInputConfig {
        .min_value = HotendPIDGains::kd_min,
        .max_value = HotendPIDGains::kd_max,
        .step = 1.0f,
        .max_decimal_places = 1,
    },
};

/// @returns the gains the tool gets at startup
HotendPIDGains stored_gains(PhysicalToolIndex tool) {
    const HotendPIDGains gains = config_store().hotend_pid_gains.get(tool.to_raw());
    return hotend_pid::is_valid(gains) ? gains : config_store_ns::defaults::hotend_pid_gains;
}

void store_and_apply(PhysicalToolIndex tool, const HotendPIDGains &gains) {
    config_store().hotend_pid_gains.set(tool.to_raw(), gains);
    marlin_client::gcode_printf("M301 E%d P%.4f I%.4f D%.4f", tool.to_raw(),
        static_cast<double>(gains.kp), static_cast<double>(gains.ki), static_cast<double>(gains.kd));
}

} // namespace

// * MI_HOTEND_PID_GAIN
MI_HOTEND_PID_GAIN::MI_HOTEND_PID_GAIN(Gain gain, Toolhead toolhead)
    : MI_TOOLHEAD_SPECIFIC_SPIN(toolhead, 0, gain_configs[std::to_underlying(gain)], string_view_utf8::MakeCPUFLASH(gain_labels[std::to_underlying(gain)]))
    , gain_(gain) //
{
    update();
}

float MI_HOTEND_PID_GAIN::read_value_impl(PhysicalToolIndex ix) {
    return stored_gains(ix).*gain_members[std::to_underlying(gain_)];
}

void MI_HOTEND_PID_GAIN::store_value_impl(PhysicalToolIndex ix, float set) {
    HotendPIDGains gains = stored_gains(ix);
    gains.*gain_members[std::to_underlying(gain_)] = set;

    // The items are disabled during a print, this catches a print started while editing
    if (marlin_client::is_printing() || !hotend_pid::is_valid(gains)) {
        update();
        return;
    }
    store_and_apply(ix, gains);
}

// * MI_HOTEND_PID_RESTORE_DEFAULTS
MI_HOTEND_PID_RESTORE_DEFAULTS::MI_HOTEND_PID_RESTORE_DEFAULTS(Toolhead toolhead)
    : MI_TOOLHEAD_SPECIFIC_BASE(toolhead, _("Restore Defaults")) {}

void MI_HOTEND_PID_RESTORE_DEFAULTS::click(IWindowMenu &) {
    const Toolhead selected = toolhead();
    const auto *tool = std::get_if<PhysicalToolIndex>(&selected);
    if (!tool) {
        return;
    }

    if (MsgBoxQuestion(_("Do you really want to restore the default hotend PID values of this tool?"), Responses_YesNo) != Response::Yes) {
        return;
    }

    if (marlin_client::is_printing()) {
        return;
    }

    store_and_apply(*tool, config_store_ns::defaults::hotend_pid_gains);
    Screens::Access()->WindowEvent(GUI_event_t::CHILD_CLICK, std::bit_cast<void *>(ScreenToolheadDetailHotendPid::Event::gains_changed));
}

// * MI_HOTEND_PID_TUNE_TOOL
MI_HOTEND_PID_TUNE_TOOL::MI_HOTEND_PID_TUNE_TOOL(uint8_t raw_tool)
    : MenuItemToggleSwitch(true, {})
    , tool_(PhysicalToolIndex::from_raw(raw_tool)) //
{
    SetLabel(tool_.display_name(label_params_));
}

// * MI_HOTEND_PID_TUNE_TEMP
MI_HOTEND_PID_TUNE_TEMP::MI_HOTEND_PID_TUNE_TEMP()
    : NumericInputConfigHolder { numeric_input_config::filament_nozzle_temperature(AllTools {}) }
    , WiSpin(215, NumericInputConfigHolder::owned_config, _("Tuning Temperature")) {}

// * MI_HOTEND_PID_TUNE_FAN
MI_HOTEND_PID_TUNE_FAN::MI_HOTEND_PID_TUNE_FAN()
    : WiSpin(0, numeric_input_config::percent_with_off, _("Tuning Print Fan")) {}

// * MI_HOTEND_PID_TUNE
MI_HOTEND_PID_TUNE::MI_HOTEND_PID_TUNE(Toolhead toolhead)
    : MI_TOOLHEAD_SPECIFIC_BASE(toolhead, _("Run PID Tuning")) {}

void MI_HOTEND_PID_TUNE::click(IWindowMenu &) {
    Screens::Access()->WindowEvent(GUI_event_t::CHILD_CLICK, std::bit_cast<void *>(ScreenToolheadDetailHotendPid::Event::start_tuning));
}

// * ScreenToolheadDetailHotendPid
ScreenToolheadDetailHotendPid::ScreenToolheadDetailHotendPid(Toolhead toolhead)
    : ScreenMenu(_("HOTEND PID"))
    , toolhead_(toolhead) //
{
    const bool all_tools = toolhead == all_toolheads;
    if (all_tools) {
        // Gains are per tool, so their items are hidden here and stay on the first tool
        Item<MI_HOTEND_PID_TUNE>().set_toolhead(toolhead);
    } else {
        menu_set_toolhead(container, toolhead);
    }

    Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::kp>>().set_is_hidden(all_tools);
    Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::ki>>().set_is_hidden(all_tools);
    Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::kd>>().set_is_hidden(all_tools);
    Item<MI_HOTEND_PID_RESTORE_DEFAULTS>().set_is_hidden(all_tools);
    stdext::visit_tuple(container.menu_items, [all_tools]<typename T>(T &item) {
        if constexpr (std::is_base_of_v<MI_HOTEND_PID_TUNE_TOOL, T>) {
            item.set_is_hidden(!all_tools || !item.tool().is_enabled());
        }
    });

    update_editable();
}

hotend_pid::ToolSet ScreenToolheadDetailHotendPid::chosen_tools() {
    if (const auto *tool = std::get_if<PhysicalToolIndex>(&toolhead_)) {
        return static_cast<hotend_pid::ToolSet>(1u << tool->to_raw());
    }

    hotend_pid::ToolSet tools = 0;
    stdext::visit_tuple(container.menu_items, [&tools]<typename T>(T &item) {
        if constexpr (std::is_base_of_v<MI_HOTEND_PID_TUNE_TOOL, T>) {
            if (item.value() == true && item.tool().is_enabled()) {
                tools |= static_cast<hotend_pid::ToolSet>(1u << item.tool().to_raw());
            }
        }
    });
    return tools;
}

void ScreenToolheadDetailHotendPid::update_editable() {
    editable_ = !marlin_client::is_printing();
    Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::kp>>().set_enabled(editable_);
    Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::ki>>().set_enabled(editable_);
    Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::kd>>().set_enabled(editable_);
    Item<MI_HOTEND_PID_RESTORE_DEFAULTS>().set_enabled(editable_);
    stdext::visit_tuple(container.menu_items, [this]<typename T>(T &item) {
        if constexpr (std::is_base_of_v<MI_HOTEND_PID_TUNE_TOOL, T>) {
            item.set_enabled(editable_);
        }
    });
    Item<MI_HOTEND_PID_TUNE_TEMP>().set_enabled(editable_);
    Item<MI_HOTEND_PID_TUNE_FAN>().set_enabled(editable_);
    Item<MI_HOTEND_PID_TUNE>().set_enabled(editable_ && chosen_tools() != 0);
}

void ScreenToolheadDetailHotendPid::start_tuning() {
    const hotend_pid::ToolSet tools = chosen_tools();
    if (tools == 0 || marlin_client::is_printing()) {
        return;
    }

    const selftest::HotendPidTuningData request {
        .tool_mask = tools,
        .target_temp = static_cast<int16_t>(Item<MI_HOTEND_PID_TUNE_TEMP>().value()),
        .print_fan_pwm = static_cast<uint8_t>(std::lround(Item<MI_HOTEND_PID_TUNE_FAN>().value() * 255 / 100)),
    };
    marlin_client::test_start_with_data(stmHotendPid, request);
}

void ScreenToolheadDetailHotendPid::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    if (event == GUI_event_t::LOOP) {
        if (const bool editable = !marlin_client::is_printing(); editable != editable_) {
            update_editable();
        }
        // A tool row may have been switched
        Item<MI_HOTEND_PID_TUNE>().set_enabled(editable_ && chosen_tools() != 0);
    }

    if (event == GUI_event_t::CHILD_CLICK && std::bit_cast<Event>(param) == Event::gains_changed) {
        Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::kp>>().update();
        Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::ki>>().update();
        Item<WithConstructorArgs<MI_HOTEND_PID_GAIN, Gain::kd>>().update();
        return;
    }

    if (event == GUI_event_t::CHILD_CLICK && std::bit_cast<Event>(param) == Event::start_tuning) {
        start_tuning();
        return;
    }

    ScreenMenu::windowEvent(sender, event, param);
}

// * MI_HOTEND_PID
MI_HOTEND_PID::MI_HOTEND_PID(Toolhead toolhead)
    : MI_TOOLHEAD_SPECIFIC_BASE(toolhead, _("Hotend PID"), nullptr, is_enabled_t::yes, is_hidden_t::no, expands_t::yes) {}

void MI_HOTEND_PID::click(IWindowMenu &) {
    Screens::Access()->Open(ScreenFactory::ScreenWithArg<ScreenToolheadDetailHotendPid>(toolhead()));
}
