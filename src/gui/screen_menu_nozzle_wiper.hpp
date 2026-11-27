/// @file
#pragma once

#include "screen_menu.hpp"
#include "MItem_hardware.hpp"
#include "MItem_tools.hpp"
#include <WindowMenuItems.hpp>
#include <window_menu_callback_item.hpp>
#include <meta_utils.hpp>

namespace screen_menu_nozzle_wiper {

/// Where the taught custom wiper stands: what is taught and whether it can be wiped
class MI_WIPER_STATUS : public WI_INFO_t {
public:
    MI_WIPER_STATUS();

    void refresh();
};

class MI_JOG_STEP : public MenuItemSwitch {
public:
    MI_JOG_STEP();

    bool fine() const { return current_item() == 1; }
};

/// Jogs one axis, in machine coordinates like Move Axis
class MI_JOG_AXIS : public NumericInputConfigHolder, public WiSpin {
public:
    MI_JOG_AXIS(AxisEnum axis);

    AxisEnum axis() const { return axis_; }

    void set_fine(bool fine);
    void set_min(float min);

private:
    AxisEnum axis_;
};

using MI_JOG_X = WithConstructorArgs<MI_JOG_AXIS, X_AXIS>;
using MI_JOG_Y = WithConstructorArgs<MI_JOG_AXIS, Y_AXIS>;
using MI_JOG_Z = WithConstructorArgs<MI_JOG_AXIS, Z_AXIS>;

using MI_COOLDOWN = WithConstructorArgs<WindowMenuCallbackItem, N_("Cooldown"), nullptr>;
using MI_SAVE_START = WithConstructorArgs<WindowMenuCallbackItem, N_("Save Start"), nullptr>;
using MI_SAVE_END = WithConstructorArgs<WindowMenuCallbackItem, N_("Save End"), nullptr>;
using MI_GO_TO_WAITING_POINT = WithConstructorArgs<WindowMenuCallbackItem, N_("Go to Waiting Point"), nullptr>;
using MI_TEST_WIPE = WithConstructorArgs<WindowMenuCallbackItem, N_("Test Wipe (cold, slow)"), nullptr>;
using MI_WIPE_NOW = WithConstructorArgs<WindowMenuCallbackItem, N_("Wipe Now"), nullptr>;
using MI_DEEP_CLEAN = WithConstructorArgs<WindowMenuCallbackItem, N_("Deep Clean"), nullptr>;

class MI_SOAK_TIME : public WiSpin {
public:
    MI_SOAK_TIME();

protected:
    void OnClick() override;
};

class MI_WIPE_WIDTH : public WiSpin {
public:
    MI_WIPE_WIDTH();

protected:
    void OnClick() override;
};

class MI_WIPE_SPEED : public WiSpin {
public:
    MI_WIPE_SPEED();

protected:
    void OnClick() override;
};

class MI_GUARDED_DESCENT : public WI_ICON_SWITCH_OFF_ON_t {
public:
    MI_GUARDED_DESCENT();

protected:
    void OnChange(size_t old_index) override;
};

class MI_WIPE_BEFORE_PROBING : public WI_ICON_SWITCH_OFF_ON_t {
public:
    MI_WIPE_BEFORE_PROBING();

protected:
    void OnChange(size_t old_index) override;
};

using ScreenMenuNozzleWiper_ = ScreenMenu<EFooter::On, MI_RETURN, MI_NOZZLE_CLEANER_LITE, MI_NOZZLE_WIPER_TYPE, MI_WIPER_STATUS,
    MI_PICK_PARK_TOOL, MI_AUTO_HOME, MI_COOLDOWN, MI_JOG_STEP, MI_JOG_X, MI_JOG_Y, MI_JOG_Z, MI_SAVE_START, MI_SAVE_END,
    MI_GO_TO_WAITING_POINT, MI_TEST_WIPE, MI_WIPE_NOW, MI_WIPE_WIDTH, MI_WIPE_SPEED, MI_GUARDED_DESCENT, MI_WIPE_BEFORE_PROBING,
    MI_SOAK_TIME, MI_DEEP_CLEAN>;

} // namespace screen_menu_nozzle_wiper

/// Settings > Hardware > Nozzle Wiper Setup: teach the custom wiper by jogging
/// the nozzle onto it, then test and tune it.
class ScreenMenuNozzleWiper : public screen_menu_nozzle_wiper::ScreenMenuNozzleWiper_ {
public:
    ScreenMenuNozzleWiper();
    ~ScreenMenuNozzleWiper();

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    void loop();
    void sync_jog_items();
    void plan_jog();

    void save_start();
    void save_end();
    void go_to_waiting_point();

    /// Tool, homing, temperature and printing allow jogging and saving
    bool jog_allowed() const;

    /// Machine position the jog items were last set to or last sent
    xyz_float_t queued_pos { { NAN, NAN, NAN } };

    /// Position the jog items were last synced from
    xyz_float_t synced_pos { { NAN, NAN, NAN } };

    /// Lowest Z sent while on this screen, to lift on leaving
    float lowest_jog_z = NAN;
    float entry_z = NAN;

    /// Since when the server has been idle, nullopt while it is busy
    std::optional<uint32_t> idle_since_ms;

    /// A jog was sent and the server has not settled since
    bool jogging = false;
};
