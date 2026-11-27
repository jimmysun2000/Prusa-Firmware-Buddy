#pragma once

#include <cstdint>

#include "screen_toolhead_settings_common.hpp"
#include <hotend_pid_gains.hpp>

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

using ScreenToolheadDetailHotendPid_ = ScreenMenu<EFooter::Off,
    MI_RETURN,
    WithConstructorArgs<MI_HOTEND_PID_GAIN, MI_HOTEND_PID_GAIN::Gain::kp>,
    WithConstructorArgs<MI_HOTEND_PID_GAIN, MI_HOTEND_PID_GAIN::Gain::ki>,
    WithConstructorArgs<MI_HOTEND_PID_GAIN, MI_HOTEND_PID_GAIN::Gain::kd>,
    MI_HOTEND_PID_RESTORE_DEFAULTS>;

/// Hotend PID gains of one tool; they can be changed only while no print is active
class ScreenToolheadDetailHotendPid : public ScreenToolheadDetailHotendPid_ {
public:
    ScreenToolheadDetailHotendPid(Toolhead toolhead = default_toolhead);

    /// Sent as the CHILD_CLICK parameter by items that changed the stored gains; not 0, which is nullptr
    enum class Event : uintptr_t {
        gains_changed = 1,
    };

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    void update_editable();

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
