/// @file
#include "screen_nozzle_wiper_setup.hpp"

#include "screen_menu_nozzle_wiper.hpp"
#include <ScreenHandler.hpp>
#include <ScreenFactory.hpp>
#include <DialogHandler.hpp>
#include <img_resources.hpp>
#include <window_msgbox.hpp>
#include <window_dlg_wait.hpp>
#include <gui/dialogs/dialog_tool_select.hpp>
#include <gui/menu_vars.h>
#include <config_store/store_instance.hpp>
#include <marlin_client.hpp>
#include <marlin_server.hpp>
#include <common/marlin_vars.hpp>
#include <cooling_fan_assist.hpp>
#include <module/motion.h>
#include <utils/string_builder.hpp>
#include <utils/variant_utils.hpp>
#include <timing.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <utility>

using namespace screen_nozzle_wiper_setup;
using nozzle_cleaner_lite::CustomResult;
using screen_menu_nozzle_wiper::cold_nozzle_max_temp;
namespace cw = nozzle_cleaner_lite::custom_wiper;

namespace {

/// [mm/min]
constexpr int jog_feedrate_xy = 10 * 60;
constexpr int jog_feedrate_z = 2 * 60;

constexpr float coarse_step_xy = 1.0f;
constexpr float coarse_step_z = 0.5f;
constexpr float fine_step = 0.1f;

/// How long the server has to stay idle before its reported position is final
constexpr int32_t settle_ms = 300;

/// A finished jog stops within 0.01 mm of its target; anything further moved on its own
constexpr float position_tolerance = 0.05f;

/// The end warns when the nozzle is this far off the height taught with the start
constexpr float end_height_tolerance = 1.0f;

/// The check warns when a point is this close to a front-strip spot that other routines use
constexpr float strip_warning_distance = 5.0f;

/// The nozzle lifts this far off the wiper after the stroke end, and on leaving if it was lowered
constexpr float lift_distance = 10.0f;

/// A queued test wipe that has not taken its trial once the queue stayed empty this long never will
constexpr int32_t trial_take_timeout_ms = 2000;

/// A line shift below this is shown as none
constexpr float shown_shift_min = 0.05f;

constexpr const char *jog_step_items[] = {
    N_("Coarse"),
    N_("Fine"),
};

constexpr PhaseResponses responses_abort = { Response::Abort, Response::_none, Response::_none, Response::_none };

constexpr const char *step_titles[] = {
    N_("WIPER SETUP 1/5"),
    N_("WIPER SETUP 2/5"),
    N_("WIPER SETUP 3/5"),
    N_("WIPER SETUP 4/5"),
    N_("WIPER SETUP 5/5"),
};

/// Fits the 21 characters of an info value
constexpr const char *step_names[] = {
    N_("1/5 Stroke start"),
    N_("2/5 Stroke end"),
    N_("3/5 Check"),
    N_("4/5 Wipe width"),
    N_("5/5 Test wipe"),
};

float coarse_step(AxisEnum axis) {
    return axis == Z_AXIS ? coarse_step_z : coarse_step_xy;
}

NumericInputConfig jog_config(AxisEnum axis) {
    const auto range = MenuVars::axis_range(axis);
    return {
        .min_value = static_cast<float>(range.first),
        .max_value = static_cast<float>(range.second),
        .step = coarse_step(axis),
        .max_decimal_places = 1,
        .unit = Unit::millimeter,
    };
}

/// Nozzle tip position, once the moves still running have finished
xyz_float_t tip_position() {
    window_dlg_wait_t::wait_for_gcodes_to_finish();
    const xyz_pos_t offset = hotend_currently_applied_offset;
    return { {
        marlin_vars().native_pos[X_AXIS].get() + offset.x,
        marlin_vars().native_pos[Y_AXIS].get() + offset.y,
        marlin_vars().native_pos[Z_AXIS].get() + offset.z,
    } };
}

bool near_front_strip(cw::Point p) {
    const float d = strip_warning_distance;
    // Bed-tap nozzle check of the start G-code: G29 P9 X0 Y-4 W32 H4
    const bool taps = p.x >= 0 - d && p.x <= 32 + d && p.y >= -4 - d && p.y <= 0 + d;
    // Filament change and failed-cleaning park
    const bool park = std::hypot(p.x - X_NOZZLE_PARK_POINT_M600, p.y - Y_NOZZLE_PARK_POINT_M600) <= d;
    // Purge and filament load/unload park, at any X
    const bool purge = std::abs(p.y - Y_AXIS_PURGE_POS) <= d;
    return taps || park || purge;
}

/// Asks a question whose Yes moves the nozzle, heats or stores, with No focused
bool ask(const string_view_utf8 &text) {
    return MsgBoxWarning(text, Responses_YesNo, 1) == Response::Yes;
}

void show_not_ready() {
    MsgBoxWarning(_("The setup needs a picked tool, homed axes, no print running and the nozzle at 50 °C or less."), Responses_Ok);
}

/// \returns whether to use \p p
bool confirm_above_sheet(cw::Point p) {
    const bool above_sheet = p.x >= 0 && p.x <= X_BED_SIZE && p.y >= 0 && p.y <= Y_BED_SIZE;
    return !above_sheet || ask(_("This position is above the print sheet. Is the wiper really there?"));
}

/// Moves the nozzle tip 10 mm above the stroke start of \p geometry with the picked tool,
/// inside the range the jog items allow
void go_above_start(const cw::Geometry &geometry) {
    const xyz_pos_t offset = hotend_currently_applied_offset;
    const auto x_range = MenuVars::axis_range(X_AXIS);
    const auto y_range = MenuVars::axis_range(Y_AXIS);
    const float x = std::clamp(geometry.start_x - offset.x, static_cast<float>(x_range.first), static_cast<float>(x_range.second));
    const float y = std::clamp(geometry.start_y - offset.y, static_cast<float>(y_range.first), static_cast<float>(y_range.second));
    const float z = geometry.wipe_z - offset.z + lift_distance;
    // P1: absolute Z, lifted before the XY move or lowered after it
    marlin_client::gcode_printf("G27 X%.3f Y%.3f Z%.3f P1", static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
}

/// Waits for the queued moves to finish. Waits for the server to stay idle for a
/// moment, so that a G-code it has not reported as busy yet cannot end the wait early.
void wait_for_moves() {
    std::optional<uint32_t> idle_since;
    window_dlg_wait_t::wait_until({}, [&] {
        // Lets the dialogs of the running G-code show on top
        DialogHandler::Access().Loop();
        if (marlin_vars().is_processing.get()) {
            idle_since.reset();
            return false;
        }
        const uint32_t now = ticks_ms();
        if (!idle_since.has_value()) {
            idle_since = now;
        }
        return ticks_diff(now, *idle_since) >= settle_ms;
    });
}

/// Shows the nozzle temperature while it cools, with Abort
class CoolingBox : public MsgBoxIconned {
public:
    CoolingBox()
        : MsgBoxIconned(GuiDefaults::DialogFrameRect, responses_abort, 0, string_view_utf8::MakeNULLSTR(), is_multiline::yes, &img::warning_48x48) {}

    void show_temp(int temp) {
        if (temp == shown_temp) {
            return;
        }
        shown_temp = temp;
        text.SetText(_("Cooling the nozzle: %d °C").formatted(params, temp));
        // Every value is formatted into the same buffer, so the text does not see the change
        text.Invalidate();
    }

private:
    StringViewUtf8Parameters<8> params;
    int shown_temp = INT_MIN;
};

/// The print fan value the server applies to the picked tool
struct PrintFanPort {
    uint8_t get() const {
        return marlin_vars().print_fan_speed.get();
    }
    void set(uint8_t pwm) const {
        marlin_client::set_fan_speed(pwm);
    }
};

/// Switches the heaters off and waits for the nozzle of \p tool to cool for jogging.
/// The print fan helps while the tool is picked and has its previous value again on return.
/// \returns false if aborted or a print started
bool cool_down(PhysicalToolIndex tool) {
    for (auto t : PhysicalToolIndex::all()) {
        marlin_client::set_target_nozzle(0, t);
    }
    marlin_client::set_target_bed(0);

    // One reference fits the loop callback
    struct Cooling {
        PhysicalToolIndex tool;
        CoolingBox box {};
        cooling_fan_assist::Guard<PrintFanPort> fan {};
        bool cold = false;
        bool closing = false;
    } cooling { .tool = tool };

    Screens::Access()->gui_loop_until_dialog_closed([&cooling] {
        const float temp = marlin_vars().hotend(cooling.tool).temp_nozzle.get();
        cooling.box.show_temp(static_cast<int>(std::lround(temp)));
        // The fan value reaches only the picked tool
        if (stdext::holds_value(PhysicalToolIndex::currently_selected(), cooling.tool)) {
            cooling.fan.update(temp, marlin_vars().hotend(cooling.tool).target_nozzle.get(), cold_nozzle_max_temp);
        } else {
            cooling.fan.release();
        }
        cooling.cold = temp <= cold_nozzle_max_temp;
        if ((cooling.cold || marlin_client::is_printing()) && !cooling.closing) {
            cooling.closing = true;
            cooling.box.close_dialog();
        }
    });
    return cooling.cold && !marlin_client::is_printing();
}

struct ShortTool {
    uint8_t tool;
    /// How far it falls short [mm]
    float by;
};
using ShortTools = std::array<ShortTool, PhysicalToolIndex::count>;

// The two below are not inlined, so that the reach report they work on is off the
// display task's stack before the message boxes that follow run the GUI loop again.

/// Collects the enabled tools that cannot wipe \p geometry with \p width, except those in \p skip.
/// \returns how many it put in \p shorts
[[gnu::noinline]] size_t find_short_tools(const cw::Geometry &geometry, float width, uint32_t skip, ShortTools &shorts) {
    size_t count = 0;
    const auto report = nozzle_cleaner_lite::custom_reach_report(geometry, width);
    for (const auto &entry : report.tools()) {
        if (!entry.plan.has_value() && !(skip & (1u << entry.tool))) {
            shorts[count++] = { .tool = entry.tool, .by = entry.plan.error().by };
        }
    }
    return count;
}

/// Lists how far from the taught line each tool's band starts where the travel limit cuts it
[[gnu::noinline]] void append_line_shifts(StringBuilder &builder, const cw::Geometry &geometry, float width) {
    const auto report = nozzle_cleaner_lite::custom_reach_report(geometry, width);
    bool any_shift = false;
    for (const auto &entry : report.tools()) {
        if (!entry.plan.has_value() || entry.plan->line_shift() < shown_shift_min) {
            continue;
        }
        if (!any_shift) {
            builder.append_char(' ');
            builder.append_string_view(_("At the travel limit the band starts off the taught line:"));
            any_shift = true;
        } else {
            builder.append_char(',');
        }
        builder.append_printf(" T%u %.1f", static_cast<unsigned>(entry.tool + 1), static_cast<double>(entry.plan->line_shift()));
    }
    if (any_shift) {
        builder.append_string(" mm.");
    }
}

const char *result_reason(CustomResult result) {
    switch (result) {
    case CustomResult::none:
        return N_("it did not start");
    case CustomResult::done:
        return N_("it finished");
    case CustomResult::no_tool:
        return N_("no tool is picked");
    case CustomResult::geometry:
        return N_("the position is out of reach");
    case CustomResult::printing:
        return N_("a print is running");
    case CustomResult::print_in_the_way:
        return N_("a printed part is in the way");
    case CustomResult::not_homed:
        return N_("the printer is not homed");
    case CustomResult::homing_failed:
        return N_("homing failed");
    case CustomResult::no_soak_temp:
        return N_("there is no soak temperature");
    case CustomResult::temperature_timeout:
        return N_("the nozzle did not cool down in time");
    case CustomResult::target_changed:
        return N_("the nozzle temperature was changed");
    case CustomResult::obstruction:
        return N_("the nozzle touched something above the wiper");
    case CustomResult::aborted:
        return N_("it was stopped");
    }
    return "";
}

} // namespace

void screen_nozzle_wiper_setup::open() {
    if (!ask(_("This guide teaches where your nozzle wiper is. The bed will rise close to the nozzle. Remove any print and anything else from the bed. Is the bed empty?"))
        || marlin_client::is_printing()) {
        return;
    }

    // Picked first: which nozzle has to be cold depends on it
    if (!PhysicalToolIndex::currently_selected_opt().has_value()) {
        const auto tool = select_tool_dialog({ .allow_return = true });
        if (!tool.has_value() || marlin_client::is_printing()) {
            return;
        }
        // Lifts Z if it is not high enough
        marlin_client::gcode("G27 P0 Z5");
        marlin_client::gcode_printf("T%d S1 L0 D0", tool->to_raw());
        wait_for_moves();
        if (!stdext::holds_value(VirtualToolIndex::currently_selected(), *tool)) {
            MsgBoxWarning(_("Failed to pick up the selected tool."), Responses_Ok);
            return;
        }
    }
    const auto tool = PhysicalToolIndex::currently_selected_opt();
    if (!tool.has_value()) {
        return;
    }

    if (marlin_vars().hotend(*tool).temp_nozzle.get() > cold_nozzle_max_temp) {
        if (!ask(_("The nozzle must be at 50 °C or less. Switch the heaters off and wait?")) || marlin_client::is_printing()) {
            return;
        }
        if (!cool_down(*tool)) {
            return;
        }
    }

    if (!marlin_server::all_axes_homed()) {
        if (!ask(_("The printer has to home first. Home now?")) || marlin_client::is_printing()) {
            return;
        }
        marlin_client::gcode("G28");
        wait_for_moves();
        if (!marlin_server::all_axes_homed()) {
            MsgBoxWarning(_("Homing did not finish. The setup needs homed axes."), Responses_Ok);
            return;
        }
    }

    auto &store = config_store();
    const cw::Geometry saved = store.nozzle_wiper_geometry.get();
    if (nozzle_cleaner_lite::plan_custom_wipe(saved, store.nozzle_wiper_width.get()).has_value()
        && ask(_("Start from the saved position? Yes moves the nozzle 10 mm above the saved start."))
        && !marlin_client::is_printing()) {
        go_above_start(saved);
    }

    Screens::Access()->Open(ScreenFactory::Screen<ScreenNozzleWiperSetup>);
}

MI_JOG_STEP::MI_JOG_STEP()
    : MenuItemSwitch(_("Jog Step"), jog_step_items, 0) {
    set_behavior(Behavior::quick_cycle);
}

MI_JOG_AXIS::MI_JOG_AXIS(AxisEnum axis)
    : NumericInputConfigHolder { jog_config(axis) }
    , WiSpin(0, owned_config, _(MenuVars::labels[axis]), nullptr, is_enabled_t::no, is_hidden_t::no)
    , axis_(axis) {
}

void MI_JOG_AXIS::set_fine(bool fine) {
    owned_config.step = fine ? fine_step : coarse_step(axis_);
}

void MI_JOG_AXIS::set_min(float min) {
    owned_config.min_value = min;
}

MI_SETUP_STEP::MI_SETUP_STEP()
    : WI_INFO_t(_("Step")) {
}

MI_SETUP_WIDTH::MI_SETUP_WIDTH()
    : WiSpin(config_store().nozzle_wiper_width.get(), screen_menu_nozzle_wiper::wipe_width_config, _("Wipe Width"), nullptr, is_enabled_t::yes, is_hidden_t::yes) {
}

MI_SETUP_USABLE::MI_SETUP_USABLE()
    : WI_INFO_t(_("Usable"), nullptr, is_enabled_t::yes, is_hidden_t::yes) {
}

ScreenNozzleWiperSetup::ScreenNozzleWiperSetup()
    : ScreenNozzleWiperSetup_(_(step_titles[0])) {
    // Timing out would leave the setup half way
    ClrMenuTimeoutClose();

    Item<MI_CANCEL_SETUP>().callback = [] {
        if (ask(_("Leave the setup? Nothing is saved. If the nozzle was lowered, it lifts 10 mm."))) {
            Screens::Access()->close_screen();
        }
    };
    Item<MI_SETUP_INSTRUCTIONS>().callback = [this] {
        show_instructions();
    };
    Item<MI_SETUP_CONTINUE>().callback = [this] {
        on_continue();
    };

    enter(Step::start);
    loop();
}

ScreenNozzleWiperSetup::~ScreenNozzleWiperSetup() {
    // The test wipe waits for its trial to finish, so none can be left; drop one anyway
    nozzle_cleaner_lite::withdraw_trial();
    (void)nozzle_cleaner_lite::collect_trial();

    // The nozzle may have been left down on the wiper
    if (lowest_jog_z < entry_z) {
        marlin_client::gcode_printf("G27 P2 Z%.1f", static_cast<double>(lift_distance));
    }
}

void ScreenNozzleWiperSetup::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    if (event == GUI_event_t::LOOP) {
        loop();
        if (instructions_pending_) {
            // Cleared first: the message box runs the GUI loop, which comes back here
            instructions_pending_ = false;
            show_instructions();
        }
    }

    ScreenMenu::windowEvent(sender, event, param);
}

bool ScreenNozzleWiperSetup::jog_allowed() const {
    const auto tool = PhysicalToolIndex::currently_selected_opt();
    return tool.has_value()
        && !marlin_client::is_printing()
        && marlin_server::all_axes_homed()
        && marlin_vars().hotend(*tool).temp_nozzle.get() <= cold_nozzle_max_temp;
}

void ScreenNozzleWiperSetup::loop() {
    const uint32_t now = ticks_ms();
    if (marlin_vars().is_processing.get()) {
        idle_since_ms.reset();
    } else if (!idle_since_ms.has_value()) {
        idle_since_ms = now;
    }
    const bool settled = idle_since_ms.has_value() && ticks_diff(now, *idle_since_ms) >= settle_ms;
    if (settled) {
        jogging = false;
    }

    auto &jog_x = Item<MI_JOG_X>();
    auto &jog_y = Item<MI_JOG_Y>();
    auto &jog_z = Item<MI_JOG_Z>();
    const bool editing = jog_x.is_edited() || jog_y.is_edited() || jog_z.is_edited();
    if (settled && !editing) {
        sync_jog_items();
    }

    const bool can_jog = jog_allowed();
    const bool fine = Item<MI_JOG_STEP>().fine();
    for (MI_JOG_AXIS *item : { static_cast<MI_JOG_AXIS *>(&jog_x), static_cast<MI_JOG_AXIS *>(&jog_y), static_cast<MI_JOG_AXIS *>(&jog_z) }) {
        item->set_fine(fine);
        if (!item->is_edited()) {
            // Another G-code (homing, parking, a wipe) is moving the head
            item->set_enabled(can_jog && !std::isnan(queued_pos.x) && (settled || jogging));
        }
    }
    plan_jog();

    bool can_continue = false;
    switch (step_) {
    case Step::start:
    case Step::end:
        can_continue = can_jog && settled;
        break;
    case Step::width:
        update_usable();
        can_continue = width_usable_ && settled && !marlin_client::is_printing();
        break;
    case Step::check:
    case Step::test:
        // Run by Continue itself, through message boxes
        break;
    }
    Item<MI_SETUP_CONTINUE>().set_enabled(can_continue);
}

void ScreenNozzleWiperSetup::sync_jog_items() {
    // The jog floor is the wipe height floor, in the machine frame of the tool picked now
    Item<MI_JOG_Z>().set_min(cw::wipe_z_min - hotend_currently_applied_offset.z);

    const xyz_float_t pos { {
        marlin_vars().native_pos[X_AXIS].get(),
        marlin_vars().native_pos[Y_AXIS].get(),
        marlin_vars().native_pos[Z_AXIS].get(),
    } };
    if (std::isnan(entry_z)) {
        entry_z = pos.z;
    }

    const bool moved = std::isnan(synced_pos.x)
        || std::abs(pos.x - synced_pos.x) > position_tolerance
        || std::abs(pos.y - synced_pos.y) > position_tolerance
        || std::abs(pos.z - synced_pos.z) > position_tolerance;
    if (!moved) {
        return;
    }
    synced_pos = pos;

    // The items snap to their step and clamp to their range; take what they show as
    // already reached, so that syncing never moves the head
    Item<MI_JOG_X>().set_value(pos.x);
    Item<MI_JOG_Y>().set_value(pos.y);
    Item<MI_JOG_Z>().set_value(pos.z);
    queued_pos = { { Item<MI_JOG_X>().value(), Item<MI_JOG_Y>().value(), Item<MI_JOG_Z>().value() } };
}

void ScreenNozzleWiperSetup::plan_jog() {
    if (std::isnan(queued_pos.x)) {
        return;
    }

    const xyz_float_t target { { Item<MI_JOG_X>().value(), Item<MI_JOG_Y>().value(), Item<MI_JOG_Z>().value() } };
    if (target == queued_pos) {
        return;
    }

    if (!jog_allowed()) {
        // Nothing turned while jogging was not allowed may move the head later
        Item<MI_JOG_X>().set_value(queued_pos.x);
        Item<MI_JOG_Y>().set_value(queued_pos.y);
        Item<MI_JOG_Z>().set_value(queued_pos.z);
        return;
    }

    const int feedrate = (target.z != queued_pos.z) ? jog_feedrate_z : jog_feedrate_xy;
    ArrayStringBuilder<MARLIN_MAX_REQUEST> gcode;
    gcode.append_printf("G123 X%.3f Y%.3f Z%.3f F%d", static_cast<double>(target.x), static_cast<double>(target.y), static_cast<double>(target.z), feedrate);
    if (marlin_client::gcode_try(gcode.str()) != marlin_client::GcodeTryResult::Submitted) {
        return;
    }

    queued_pos = target;
    jogging = true;
    if (!(lowest_jog_z <= target.z)) {
        lowest_jog_z = target.z;
    }
}

void ScreenNozzleWiperSetup::enter(Step step, bool with_instructions) {
    step_ = step;
    const size_t index = std::to_underlying(step);
    // Each title is its own text, so setting another one always redraws
    header.SetText(_(step_titles[index]));
    Item<MI_SETUP_STEP>().ChangeInformation(_(step_names[index]));

    const bool jog = step == Step::start || step == Step::end;
    Item<MI_JOG_STEP>().set_is_hidden(!jog);
    Item<MI_JOG_X>().set_is_hidden(!jog);
    Item<MI_JOG_Y>().set_is_hidden(!jog);
    Item<MI_JOG_Z>().set_is_hidden(!jog);

    const bool width = step == Step::width;
    Item<MI_SETUP_WIDTH>().set_is_hidden(!width);
    Item<MI_SETUP_USABLE>().set_is_hidden(!width);

    Item<MI_SETUP_INSTRUCTIONS>().set_is_hidden(!jog && !width);
    instructions_pending_ = with_instructions && (jog || width);
    usable_for_width_ = NAN;
}

void ScreenNozzleWiperSetup::show_instructions() {
    switch (step_) {
    case Step::start:
        MsgBoxInfo(_("Step 1 of 5: stroke start. Turn X, Y and Z to put the nozzle tip on one end of the wiper, at the height it should wipe at. Jog Step switches 1 mm / 0.1 mm. Then choose Continue."), Responses_Ok);
        break;
    case Step::end:
        MsgBoxInfo(_("Step 2 of 5: stroke end. Move the nozzle along the wiper to its other end. Keep the height. Then choose Continue."), Responses_Ok);
        break;
    case Step::width:
        MsgBoxInfo(_("Step 4 of 5: wipe width. The nozzle zigzags across the wiper within this width, centred on the taught line; beyond the travel limit it is cut off. 'Usable' shows what is left."), Responses_Ok);
        break;
    case Step::check:
    case Step::test:
        break;
    }
}

void ScreenNozzleWiperSetup::on_continue() {
    switch (step_) {
    case Step::start:
        continue_from_start();
        break;
    case Step::end:
        continue_from_end();
        break;
    case Step::width:
        continue_from_width();
        break;
    case Step::check:
    case Step::test:
        break;
    }
}

void ScreenNozzleWiperSetup::back_to_start() {
    candidate_ = {};
    enter(Step::start);
}

void ScreenNozzleWiperSetup::continue_from_start() {
    const xyz_float_t tip = tip_position();
    if (!jog_allowed()) {
        show_not_ready();
        return;
    }
    if (!(tip.z >= cw::wipe_z_min && tip.z <= cw::wipe_z_max)) {
        screen_menu_nozzle_wiper::show_problem({ .fault = cw::Fault::wipe_z });
        return;
    }
    if (!confirm_above_sheet({ tip.x, tip.y })) {
        return;
    }

    candidate_ = {};
    candidate_.start_x = tip.x;
    candidate_.start_y = tip.y;
    candidate_.wipe_z = tip.z;
    candidate_.taught = cw::Geometry::taught_start;
    enter(Step::end);
}

void ScreenNozzleWiperSetup::continue_from_end() {
    const xyz_float_t tip = tip_position();
    if (!jog_allowed()) {
        show_not_ready();
        return;
    }

    cw::Geometry candidate = candidate_;
    candidate.end_x = tip.x;
    candidate.end_y = tip.y;
    candidate.taught |= cw::Geometry::taught_end;
    if (const cw::Problem problem = cw::check_geometry(candidate, Item<MI_SETUP_WIDTH>().value()); !problem.ok()) {
        screen_menu_nozzle_wiper::show_problem(problem);
        return;
    }
    if (!(std::abs(tip.z - candidate.wipe_z) <= end_height_tolerance)
        && !ask(_("The nozzle is more than 1 mm off the height of the start. Only the start sets the wipe height. Use this end anyway?"))) {
        return;
    }
    if (!confirm_above_sheet({ tip.x, tip.y })) {
        return;
    }

    candidate_ = candidate;
    // Off the wiper while the position is checked and the width chosen
    marlin_client::gcode_printf("G27 P2 Z%.1f", static_cast<double>(lift_distance));
    lowest_jog_z = NAN;
    run_check();
}

bool ScreenNozzleWiperSetup::accept_short_tools(float width) {
    ShortTools shorts;
    const size_t count = find_short_tools(candidate_, width, accepted_short_, shorts);
    for (size_t i = 0; i < count; ++i) {
        StringViewUtf8Parameters<16> params;
        if (!ask(_("Tool %u cannot reach the wiper (%.1f mm short) and will not be wiped. Continue?")
                     .formatted(params, static_cast<unsigned>(shorts[i].tool + 1), static_cast<double>(shorts[i].by)))) {
            return false;
        }
        accepted_short_ |= 1u << shorts[i].tool;
    }
    return true;
}

void ScreenNozzleWiperSetup::run_check() {
    enter(Step::check);
    const float width = Item<MI_SETUP_WIDTH>().value();

    // The width is chosen in the next step, so here the picked tool only has to reach the wiper with some width
    const auto reach = nozzle_cleaner_lite::plan_custom_wipe(candidate_, cw::width_max);
    if (!reach.has_value()) {
        screen_menu_nozzle_wiper::ProblemParams params;
        if (MsgBoxWarning(screen_menu_nozzle_wiper::problem_text(reach.error(), params), Responses_RetryAbort) == Response::Retry) {
            back_to_start();
        } else {
            Screens::Access()->close_screen();
        }
        return;
    }

    accepted_short_ = 0;
    const auto picked = nozzle_cleaner_lite::plan_custom_wipe(candidate_, width);
    // Until the picked tool can wipe with the width, which tools are short has no meaning yet
    if (picked.has_value() && !accept_short_tools(width)) {
        back_to_start();
        return;
    }

    const cw::ToolPlan &plan = picked.has_value() ? *picked : *reach;
    const xyz_pos_t offset = hotend_currently_applied_offset;
    const std::array<cw::Point, 3> tip_points { {
        { candidate_.start_x, candidate_.start_y },
        { candidate_.end_x, candidate_.end_y },
        { plan.waiting_point.x + offset.x, plan.waiting_point.y + offset.y },
    } };
    if (std::ranges::any_of(tip_points, near_front_strip)
        && !ask(_("This position is near where the printer purges, checks the nozzle by tapping the bed or parks for a filament change. Make sure the wiper and its mount stay clear of those spots. Continue anyway?"))) {
        back_to_start();
        return;
    }

    std::array<char, 256> text;
    StringBuilder builder { text };
    StringViewUtf8Parameters<16> params;
    builder.append_string_view(_("Check passed. Stroke %.0f mm at height %.1f mm.").formatted(params, static_cast<double>(plan.frame.length), static_cast<double>(candidate_.wipe_z)));
    append_line_shifts(builder, candidate_, width);

    if (!picked.has_value()) {
        builder.append_char(' ');
        StringViewUtf8Parameters<8> width_params;
        if (const auto needed = screen_menu_nozzle_wiper::width_setting_for(picked.error().min_width)) {
            builder.append_string_view(_("In the next step, set Wipe Width to %.1f mm or more.").formatted(width_params, static_cast<double>(*needed)));
        } else {
            builder.append_string_view(_("In the next step, raise Wipe Width until Usable shows a value."));
        }
    }

    MsgBoxInfo(string_view_utf8::MakeRAM(text.data()), Responses_Ok);
    enter(Step::width);
}

void ScreenNozzleWiperSetup::update_usable() {
    const float width = Item<MI_SETUP_WIDTH>().value();
    if (width == usable_for_width_) {
        return;
    }
    usable_for_width_ = width;

    const auto picked = nozzle_cleaner_lite::plan_custom_wipe(candidate_, width);
    width_usable_ = picked.has_value();
    if (!width_usable_) {
        Item<MI_SETUP_USABLE>().ChangeInformation(_("none"));
        return;
    }

    const auto report = nozzle_cleaner_lite::custom_reach_report(candidate_, width);
    float smallest = picked->pad.width();
    unsigned short_count = 0;
    for (const auto &entry : report.tools()) {
        if (entry.plan.has_value()) {
            smallest = std::min(smallest, entry.plan->pad.width());
        } else {
            ++short_count;
        }
    }

    StringViewUtf8Parameters<16> params;
    if (short_count == 0) {
        Item<MI_SETUP_USABLE>().ChangeInformation(_("%.1f mm (all %.1f)").formatted(params, static_cast<double>(picked->pad.width()), static_cast<double>(smallest)));
    } else {
        Item<MI_SETUP_USABLE>().ChangeInformation(_("%.1f mm (%u short)").formatted(params, static_cast<double>(picked->pad.width()), short_count));
    }
}

void ScreenNozzleWiperSetup::continue_from_width() {
    const float width = Item<MI_SETUP_WIDTH>().value();
    if (!nozzle_cleaner_lite::plan_custom_wipe(candidate_, width).has_value()) {
        return;
    }
    if (!accept_short_tools(width)) {
        return;
    }
    run_test_wipe();
}

void ScreenNozzleWiperSetup::run_test_wipe() {
    const float width = Item<MI_SETUP_WIDTH>().value();
    enter(Step::test);

    while (true) {
        if (!ask(_("Step 5 of 5: test wipe, cold and slow. The nozzle goes beside the wiper, lowers onto it and makes two slow double strokes. Is the bed still empty? Start?"))) {
            enter(Step::width, false);
            return;
        }
        if (!jog_allowed()) {
            show_not_ready();
            enter(Step::width, false);
            return;
        }

        const std::optional<CustomResult> result = test_wipe(width);
        if (result == CustomResult::done) {
            break;
        }

        std::array<char, 64> reason;
        _(result_reason(result.value_or(CustomResult::none))).copyToRAM(reason.data(), reason.size());
        StringViewUtf8Parameters<64> params;
        if (MsgBoxWarning(_("The test wipe stopped: %s.").formatted(params, reason.data()), Responses_RetryAbort) != Response::Retry) {
            Screens::Access()->close_screen();
            return;
        }
    }

    if (MsgBoxQuestion(_("Did the nozzle run along the wiper as it should?"), Responses_YesNo, 1) == Response::Yes) {
        store_and_finish(width);
        return;
    }

    if (!ask(_("Adjust the position? Yes goes back to step 1 with the nozzle 10 mm above the taught start. No discards it."))) {
        Screens::Access()->close_screen();
        return;
    }
    if (!jog_allowed()) {
        show_not_ready();
        Screens::Access()->close_screen();
        return;
    }
    go_above_start(candidate_);
    back_to_start();
}

std::optional<CustomResult> ScreenNozzleWiperSetup::test_wipe(float width) {
    if (!nozzle_cleaner_lite::arm_trial({ .geometry = candidate_, .width = width })) {
        return std::nullopt;
    }
    // The trial is armed before the G-code is queued, so that its G12 finds it
    if (marlin_client::gcode_try("G12 D") != marlin_client::GcodeTryResult::Submitted) {
        nozzle_cleaner_lite::withdraw_trial();
        return std::nullopt;
    }

    const uint32_t queued_ms = ticks_ms();
    std::optional<CustomResult> result;
    window_dlg_wait_t::wait_until({}, [&] {
        // Lets the wait screen of the G12 show on top
        DialogHandler::Access().Loop();
        if (!result.has_value()) {
            result = nozzle_cleaner_lite::collect_trial();
        }
        if (result.has_value()) {
            return !marlin_vars().is_processing.get();
        }
        // The G12 has not taken the trial and nothing is queued any more, so none will
        if (!marlin_vars().is_processing.get() && ticks_diff(ticks_ms(), queued_ms) >= trial_take_timeout_ms && nozzle_cleaner_lite::withdraw_trial()) {
            result = CustomResult::none;
            return true;
        }
        return false;
    });
    return result;
}

void ScreenNozzleWiperSetup::store_and_finish(float width) {
    if (marlin_client::is_printing()) {
        MsgBoxWarning(_("A print has started. Nothing was saved."), Responses_Ok);
        Screens::Access()->close_screen();
        return;
    }

    auto &store = config_store();
    store.nozzle_wiper_geometry.set(candidate_);
    store.nozzle_wiper_width.set(width);
    // Custom first, so that the wiper is never on with Prusa's type in between
    store.nozzle_wiper_custom.set(true);
    store.nozzle_cleaner_lite_installed.set(true);

    if (store.nozzle_wiper_auto_probing.get()) {
        MsgBoxInfo(_("Done. Nozzle Wiper is On, type Custom. Wipe Now, Deep Clean and G12 use this position. Wipe Before Probing is On."), Responses_Ok);
    } else {
        MsgBoxInfo(_("Done. Nozzle Wiper is On, type Custom. Wipe Now, Deep Clean and G12 use this position. Wipe Before Probing is Off; change it in the menu."), Responses_Ok);
    }
    Screens::Access()->close_screen();
}
