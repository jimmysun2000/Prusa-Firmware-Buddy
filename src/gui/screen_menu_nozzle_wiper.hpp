/// @file
#pragma once

#include "screen_menu.hpp"
#include "MItem_hardware.hpp"
#include "MItem_tools.hpp"
#include <WindowMenuItems.hpp>
#include <window_menu_callback_item.hpp>
#include <meta_utils.hpp>
#include <numeric_input_config.hpp>
#include <string_view_utf8.hpp>
#include <feature/nozzle_cleaner_lite/include/nozzle_cleaner_lite.hpp>

#include <optional>

namespace screen_menu_nozzle_wiper {

/// Jogging, teaching and the test wipe only with a nozzle this cold, so that
/// the wiper is never touched hot while it is being taught
inline constexpr float cold_nozzle_max_temp = 50.0f;

/// Range and step of the Wipe Width setting
extern const NumericInputConfig wipe_width_config;

using ProblemParams = StringViewUtf8Parameters<128>;

/// What keeps a tool from wiping and what to change, for a message box
string_view_utf8 problem_text(const nozzle_cleaner_lite::custom_wiper::Problem &problem, ProblemParams &params);

void show_problem(const nozzle_cleaner_lite::custom_wiper::Problem &problem);

/// The smallest Wipe Width setting that covers \p min_width, nullopt if there is none
std::optional<float> width_setting_for(float min_width);

/// Where the taught custom wiper stands: what is taught and whether it can be wiped
class MI_WIPER_STATUS : public WI_INFO_t {
public:
    MI_WIPER_STATUS();

    void refresh();
};

using MI_GUIDED_SETUP = WithConstructorArgs<WindowMenuCallbackItem, N_("Guided Setup"), nullptr>;
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
    MI_GUIDED_SETUP, MI_PICK_PARK_TOOL, MI_GO_TO_WAITING_POINT, MI_TEST_WIPE, MI_WIPE_NOW, MI_DEEP_CLEAN, MI_WIPE_WIDTH, MI_WIPE_SPEED,
    MI_GUARDED_DESCENT, MI_WIPE_BEFORE_PROBING, MI_SOAK_TIME>;

} // namespace screen_menu_nozzle_wiper

/// Settings > Hardware > Nozzle Wiper Setup: switch and tune the custom wiper,
/// test it, and start the guided setup that teaches where it is.
class ScreenMenuNozzleWiper : public screen_menu_nozzle_wiper::ScreenMenuNozzleWiper_ {
public:
    ScreenMenuNozzleWiper();

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    void loop();
    void go_to_waiting_point();

    /// Since when the server has been idle, nullopt while it is busy
    std::optional<uint32_t> idle_since_ms;
};
