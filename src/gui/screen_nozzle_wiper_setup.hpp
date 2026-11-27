/// @file
#pragma once

#include "screen_menu.hpp"
#include <WindowMenuItems.hpp>
#include <window_menu_callback_item.hpp>
#include <meta_utils.hpp>
#include <feature/nozzle_cleaner_lite/include/nozzle_cleaner_lite.hpp>

#include <cmath>
#include <cstdint>
#include <optional>

namespace screen_nozzle_wiper_setup {

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

using MI_CANCEL_SETUP = WithConstructorArgs<WindowMenuCallbackItem, N_("Cancel Setup"), nullptr>;

/// Which of the five steps the setup is in
class MI_SETUP_STEP : public WI_INFO_t {
public:
    MI_SETUP_STEP();
};

using MI_SETUP_INSTRUCTIONS = WithConstructorArgs<WindowMenuCallbackItem, N_("Instructions"), nullptr>;

/// The wipe width to test; stored only once the test wipe is confirmed
class MI_SETUP_WIDTH : public WiSpin {
public:
    MI_SETUP_WIDTH();
};

/// How much of the wipe width the picked tool, and the worst enabled tool, can use
class MI_SETUP_USABLE : public WI_INFO_t {
public:
    MI_SETUP_USABLE();
};

using MI_SETUP_CONTINUE = WithConstructorArgs<WindowMenuCallbackItem, N_("Continue"), nullptr>;

using ScreenNozzleWiperSetup_ = ScreenMenu<EFooter::On, MI_CANCEL_SETUP, MI_SETUP_STEP, MI_SETUP_INSTRUCTIONS, MI_JOG_STEP, MI_JOG_X, MI_JOG_Y, MI_JOG_Z,
    MI_SETUP_WIDTH, MI_SETUP_USABLE, MI_SETUP_CONTINUE>;

/// Starts the guided setup: asks for an empty bed, picks a tool, cools the
/// nozzle and homes if needed, offers to start above the saved position, then
/// opens the step screen. Every step that moves or heats asks first.
void open();

} // namespace screen_nozzle_wiper_setup

/// Guided custom nozzle wiper setup: teach the stroke start and end by
/// jogging, check them for every tool, choose the width and confirm a cold
/// test wipe. Nothing is stored and no switch changes before the test wipe is
/// confirmed; any other way out leaves the stored position as it was.
class ScreenNozzleWiperSetup : public screen_nozzle_wiper_setup::ScreenNozzleWiperSetup_ {
public:
    ScreenNozzleWiperSetup();
    ~ScreenNozzleWiperSetup();

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    enum class Step : uint8_t {
        start,
        end,
        check,
        width,
        test,
    };

    void loop();
    void sync_jog_items();
    void plan_jog();

    /// Tool, homing, temperature and printing allow jogging and teaching
    bool jog_allowed() const;

    /// Shows the rows of \p step and, if it has any and \p with_instructions, its instructions
    void enter(Step step, bool with_instructions = true);
    void show_instructions();
    void on_continue();

    void continue_from_start();
    void continue_from_end();
    void run_check();
    void continue_from_width();
    void run_test_wipe();

    /// Back to the first step, the nozzle where it is and nothing taught
    void back_to_start();

    /// Asks about every enabled tool that cannot wipe with \p width and was not
    /// asked about yet. \returns whether to go on
    bool accept_short_tools(float width);

    /// Arms the trial, queues the test wipe and waits for it.
    /// \returns its result, nullopt if it could not be started
    std::optional<nozzle_cleaner_lite::CustomResult> test_wipe(float width);

    void store_and_finish(float width);
    void update_usable();

    Step step_ = Step::start;

    /// The position being taught, in the nozzle tip frame
    nozzle_cleaner_lite::custom_wiper::Geometry candidate_ {};

    /// Tools the user accepted to be left unwiped, one bit per tool
    uint32_t accepted_short_ = 0;

    /// The step's instructions are still to be shown
    bool instructions_pending_ = true;

    /// The picked tool can use some of the width
    bool width_usable_ = false;

    /// Width the usable band was last worked out for
    float usable_for_width_ = NAN;

    /// Machine position the jog items were last set to or last sent
    xyz_float_t queued_pos { { NAN, NAN, NAN } };

    /// Position the jog items were last synced from
    xyz_float_t synced_pos { { NAN, NAN, NAN } };

    /// Lowest Z sent since the nozzle was last lifted, to lift on leaving
    float lowest_jog_z = NAN;
    float entry_z = NAN;

    /// Since when the server has been idle, nullopt while it is busy
    std::optional<uint32_t> idle_since_ms;

    /// A jog was sent and the server has not settled since
    bool jogging = false;
};
