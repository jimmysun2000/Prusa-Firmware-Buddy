#pragma once

#include <cstdint>
#include <utility>

#include "screen_toolhead_settings_common.hpp"
#include <hotend_pid_gains.hpp>
#include <hotend_pid_session.hpp>
#include <meta_utils.hpp>

namespace screen_toolhead_settings {

/// One of the stored hotend PID gains of the tool. A change is stored and applied with M301.
class MI_HOTEND_PID_GAIN : public MI_TOOLHEAD_SPECIFIC_SPIN {
public:
    enum class Gain : uint8_t {
        kp,
        ki,
        kd,
    };

    MI_HOTEND_PID_GAIN(Gain gain, Toolhead toolhead = default_toolhead);
    float read_value_impl(PhysicalToolIndex ix) final;
    void store_value_impl(PhysicalToolIndex ix, float set) final;

private:
    const Gain gain_;
};

/// Stores and applies the compiled default hotend PID gains of the tool
class MI_HOTEND_PID_RESTORE_DEFAULTS : public MI_TOOLHEAD_SPECIFIC_BASE<IWindowMenuItem> {
public:
    MI_HOTEND_PID_RESTORE_DEFAULTS(Toolhead toolhead = default_toolhead);
    void update() final {}

protected:
    void click(IWindowMenu &) override;
};

/// Whether the next tuning session of all tools tunes this tool; not stored, on when the screen opens
class MI_HOTEND_PID_TUNE_TOOL : public MenuItemToggleSwitch {
public:
    MI_HOTEND_PID_TUNE_TOOL(uint8_t raw_tool);

    PhysicalToolIndex tool() const { return tool_; }

protected:
    void toggled(Tristate) override {}

private:
    const PhysicalToolIndex tool_;
    PhysicalToolIndex::DisplayNameParams label_params_;
};

/// Target temperature of the next tuning session, the same for all its tools; not stored
class MI_HOTEND_PID_TUNE_TEMP : public NumericInputConfigHolder, public WiSpin {
public:
    MI_HOTEND_PID_TUNE_TEMP();
};

/// Print fan speed of the next tuning session [%], the same for all its tools; not stored
class MI_HOTEND_PID_TUNE_FAN : public WiSpin {
public:
    MI_HOTEND_PID_TUNE_FAN();
};

/// Starts the tuning session with the tools, target temperature and fan speed set above
class MI_HOTEND_PID_TUNE : public MI_TOOLHEAD_SPECIFIC_BASE<IWindowMenuItem> {
public:
    MI_HOTEND_PID_TUNE(Toolhead toolhead = default_toolhead);
    void update() final {}

protected:
    void click(IWindowMenu &) override;
};

template <typename>
struct ScreenToolheadDetailHotendPidMenu;

template <size_t... raw_tool>
struct ScreenToolheadDetailHotendPidMenu<std::index_sequence<raw_tool...>> {
    using Type = ScreenMenu<EFooter::Off,
        MI_RETURN,
        WithConstructorArgs<MI_HOTEND_PID_GAIN, MI_HOTEND_PID_GAIN::Gain::kp>,
        WithConstructorArgs<MI_HOTEND_PID_GAIN, MI_HOTEND_PID_GAIN::Gain::ki>,
        WithConstructorArgs<MI_HOTEND_PID_GAIN, MI_HOTEND_PID_GAIN::Gain::kd>,
        MI_HOTEND_PID_RESTORE_DEFAULTS,
        WithConstructorArgs<MI_HOTEND_PID_TUNE_TOOL, static_cast<uint8_t>(raw_tool)>...,
        MI_HOTEND_PID_TUNE_TEMP,
        MI_HOTEND_PID_TUNE_FAN,
        MI_HOTEND_PID_TUNE>;
};

using ScreenToolheadDetailHotendPid_ = ScreenToolheadDetailHotendPidMenu<std::make_index_sequence<PhysicalToolIndex::count>>::Type;

/// Hotend PID gains of one tool, or the tuning of several tools for all_toolheads. Gains can be
/// changed and tuned only while no print is active.
class ScreenToolheadDetailHotendPid : public ScreenToolheadDetailHotendPid_ {
public:
    ScreenToolheadDetailHotendPid(Toolhead toolhead = default_toolhead);

    /// Sent as the CHILD_CLICK parameter by the items of this screen; not 0, which is nullptr
    enum class Event : uintptr_t {
        gains_changed = 1,
        start_tuning,
    };

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    void update_editable();
    void start_tuning();

    /// @returns the tools the next tuning session would tune
    hotend_pid::ToolSet chosen_tools();

    const Toolhead toolhead_;
    bool editable_ = false;
};

/// Opens ScreenToolheadDetailHotendPid
class MI_HOTEND_PID : public MI_TOOLHEAD_SPECIFIC_BASE<IWindowMenuItem> {
public:
    MI_HOTEND_PID(Toolhead toolhead = default_toolhead);
    void click(IWindowMenu &) override;
    void update() final {}
};

} // namespace screen_toolhead_settings
