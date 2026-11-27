/// @file
#include "screen_menu_nozzle_wiper.hpp"

#include <feature/nozzle_cleaner_lite/include/nozzle_cleaner_lite.hpp>
#include <config_store/store_instance.hpp>
#include <marlin_client.hpp>
#include <marlin_server.hpp>
#include <common/marlin_vars.hpp>
#include <module/motion.h>
#include <gui/menu_vars.h>
#include <window_msgbox.hpp>
#include <window_dlg_wait.hpp>
#include <filament.hpp>
#include <utils/string_builder.hpp>
#include <timing.h>

#include <array>
#include <cmath>
#include <optional>

using namespace screen_menu_nozzle_wiper;
namespace cw = nozzle_cleaner_lite::custom_wiper;

namespace {

/// Jogging and saving only with a cold nozzle, so that the wiper is never touched hot while teaching
constexpr float jog_max_temp = 50.0f;

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

/// Save End warns when the nozzle is this far off the height saved with the start
constexpr float end_height_tolerance = 1.0f;

/// Saving warns when a point is this close to a front-strip spot that other routines use
constexpr float strip_warning_distance = 5.0f;

constexpr float exit_lift = 10.0f;

constexpr const char *jog_step_items[] = {
    N_("Coarse"),
    N_("Fine"),
};

const NumericInputConfig width_config {
    .min_value = 0,
    .max_value = cw::width_max,
    .step = 0.5f,
    .max_decimal_places = 1,
    .unit = Unit::millimeter,
};

const NumericInputConfig speed_config {
    .min_value = cw::speed_min,
    .max_value = cw::speed_max,
    .step = 5,
};

const NumericInputConfig soak_time_config {
    .min_value = cw::soak_time_min,
    .max_value = cw::soak_time_max,
    .step = 10,
    .unit = Unit::second,
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

/// Translated text in RAM, to be passed as a %s parameter of a formatted text
class Fragment {
public:
    explicit Fragment(const char *text) {
        _(text).copyToRAM(buffer.data(), buffer.size());
    }

    const char *c_str() const { return buffer.data(); }

private:
    std::array<char, 24> buffer;
};

const char *axis_name(cw::Axis axis) {
    switch (axis) {
    case cw::Axis::x:
        return "X";
    case cw::Axis::y:
        return "Y";
    case cw::Axis::z:
        return "Z";
    }
    return "";
}

const char *spot_name(cw::Spot spot) {
    switch (spot) {
    case cw::Spot::stroke_start:
        return N_("stroke start");
    case cw::Spot::stroke_end:
        return N_("stroke end");
    case cw::Spot::stroke:
        return N_("stroke");
    case cw::Spot::waiting_point:
        return N_("waiting point");
    case cw::Spot::height:
        return N_("wipe height");
    }
    return "";
}

/// Fits the 21 characters of the status line
const char *spot_short_name(cw::Spot spot) {
    switch (spot) {
    case cw::Spot::stroke_start:
        return N_("start");
    case cw::Spot::stroke_end:
        return N_("end");
    case cw::Spot::stroke:
        return N_("line");
    case cw::Spot::waiting_point:
        return N_("wait");
    case cw::Spot::height:
        return N_("height");
    }
    return "";
}

/// The X and Y limits as seen from the front of the printer
const char *limit_side_name(const cw::Problem &problem) {
    if (problem.axis == cw::Axis::x) {
        return problem.beyond_max ? N_("right") : N_("left");
    }
    return problem.beyond_max ? N_("rear") : N_("front");
}

/// Which way the taught point has to move to get inside the limit
const char *fix_direction(const cw::Problem &problem) {
    if (problem.axis == cw::Axis::x) {
        return problem.beyond_max ? N_("left") : N_("right");
    }
    return problem.beyond_max ? N_("forward") : N_("back");
}

/// The smallest Wipe Width setting that covers \p min_width, nullopt if there is none
std::optional<float> width_setting_for(float min_width) {
    if (!(min_width > 0.0f)) {
        return std::nullopt;
    }
    const float setting = std::ceil((min_width - cw::reach_tolerance) / width_config.step) * width_config.step;
    if (!(setting <= cw::width_max)) {
        return std::nullopt;
    }
    return setting;
}

using StatusParams = StringViewUtf8Parameters<32>;

/// Fits the 21 characters of the status line
string_view_utf8 short_problem_text(const cw::Problem &problem, StatusParams &params) {
    switch (problem.fault) {
    case cw::Fault::none:
        return _("Ready");
    case cw::Fault::no_tool:
        return _("No tool picked");
    case cw::Fault::not_taught:
        return _("Not taught");
    case cw::Fault::stroke_length:
        return _("Bad stroke length");
    case cw::Fault::width:
        return _("Bad width");
    case cw::Fault::wipe_z:
        return _("Bad height");
    case cw::Fault::travel_xy: {
        const Fragment spot { spot_short_name(problem.spot) };
        return _("T%u %s %s %.1f short").formatted(params, static_cast<unsigned>(problem.tool + 1), spot.c_str(), axis_name(problem.axis), static_cast<double>(problem.by));
    }
    case cw::Fault::travel_z:
        return _("T%u height %.1f short").formatted(params, static_cast<unsigned>(problem.tool + 1), static_cast<double>(problem.by));
    }
    return _("Not taught");
}

/// Ready as long as the picked tool can wipe; the tools that cannot are counted
string_view_utf8 status_text(const cw::Geometry &geometry, float width, StatusParams &params) {
    const bool start = geometry.taught & cw::Geometry::taught_start;
    const bool end = geometry.taught & cw::Geometry::taught_end;
    if (!start && !end) {
        return _("Not taught");
    } else if (!end) {
        return _("Start only");
    } else if (!start) {
        return _("End only");
    }

    if (const cw::Problem problem = cw::check_geometry(geometry, width); !problem.ok()) {
        return short_problem_text(problem, params);
    }

    const auto picked = PhysicalToolIndex::currently_selected_opt();
    const auto report = nozzle_cleaner_lite::custom_reach_report(geometry, width);
    std::optional<cw::Problem> first_short;
    unsigned short_count = 0;
    bool any_ready = false;
    for (const auto &entry : report.tools()) {
        if (entry.plan.has_value()) {
            any_ready = true;
            continue;
        }
        if (picked.has_value() && entry.tool == picked->to_raw()) {
            return short_problem_text(entry.plan.error(), params);
        }
        if (!first_short.has_value()) {
            first_short = entry.plan.error();
        }
        ++short_count;
    }

    if (!any_ready && first_short.has_value()) {
        return short_problem_text(*first_short, params);
    }
    if (short_count == 0) {
        return _("Ready");
    }
    return (short_count == 1 ? _("Ready, %u tool short") : _("Ready, %u tools short")).formatted(params, short_count);
}

using ProblemParams = StringViewUtf8Parameters<128>;

string_view_utf8 problem_text(const cw::Problem &problem, ProblemParams &params) {
    switch (problem.fault) {
    case cw::Fault::none:
        return string_view_utf8::MakeNULLSTR();
    case cw::Fault::no_tool:
        return _("Pick a tool first.");
    case cw::Fault::not_taught:
        return _("Save both the start and the end.");
    case cw::Fault::stroke_length:
        return _("The stroke from start to end has to be %.0f to %.0f mm long.").formatted(params, static_cast<double>(cw::stroke_length_min), static_cast<double>(cw::stroke_length_max));
    case cw::Fault::width:
        return _("The wipe width is out of range.");
    case cw::Fault::wipe_z:
        return _("The wipe height has to be %.1f to %.0f mm above the bed.").formatted(params, static_cast<double>(cw::wipe_z_min), static_cast<double>(cw::wipe_z_max));
    case cw::Fault::travel_xy: {
        const Fragment spot { spot_name(problem.spot) };
        const Fragment side { limit_side_name(problem) };
        const bool single_point = problem.spot == cw::Spot::stroke_start || problem.spot == cw::Spot::stroke_end;
        const Fragment what { single_point ? N_("that point") : N_("the stroke") };
        const Fragment direction { fix_direction(problem) };
        const unsigned tool = problem.tool + 1;
        const double by = problem.by;
        const double limit = problem.limit;
        if (const auto width = width_setting_for(problem.min_width)) {
            return _("Tool %u cannot reach the %s: %.1f mm beyond the %s limit (%s %.1f). Move %s %.1f mm %s or raise Wipe Width to %.1f mm.")
                .formatted(params, tool, spot.c_str(), by, side.c_str(), axis_name(problem.axis), limit, what.c_str(), by, direction.c_str(), static_cast<double>(*width));
        }
        return _("Tool %u cannot reach the %s: %.1f mm beyond the %s limit (%s %.1f). Move %s %.1f mm %s.")
            .formatted(params, tool, spot.c_str(), by, side.c_str(), axis_name(problem.axis), limit, what.c_str(), by, direction.c_str());
    }
    case cw::Fault::travel_z:
        return _("Tool %u cannot reach the wipe height: it is %.1f mm below the Z travel.").formatted(params, static_cast<unsigned>(problem.tool + 1), static_cast<double>(problem.by));
    }
    return string_view_utf8::MakeNULLSTR();
}

void show_problem(const cw::Problem &problem) {
    if (problem.ok()) {
        return;
    }
    ProblemParams params;
    MsgBoxWarning(problem_text(problem, params), Responses_Ok);
}

/// \returns why a tool that can wipe \p geometry with \p old_width could not with \p new_width, if one could not
std::optional<cw::Problem> tool_lost_to_width(const cw::Geometry &geometry, float old_width, float new_width) {
    uint32_t could_wipe = 0;
    const auto before = nozzle_cleaner_lite::custom_reach_report(geometry, old_width);
    for (const auto &entry : before.tools()) {
        if (entry.plan.has_value()) {
            could_wipe |= 1u << entry.tool;
        }
    }
    const auto after = nozzle_cleaner_lite::custom_reach_report(geometry, new_width);
    for (const auto &entry : after.tools()) {
        if (!entry.plan.has_value() && (could_wipe & (1u << entry.tool))) {
            return entry.plan.error();
        }
    }
    return std::nullopt;
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

/// The print clearance check knows nothing of a part left on the bed after a restart or Z homing
bool confirm_bed_clear() {
    return MsgBoxWarning(_("The bed will rise to the wiper. Remove any print from the bed first. Continue?"), Responses_YesNo, 1) == Response::Yes;
}

/// Asks before saving a point that looks misplaced. \returns whether to save.
bool confirm_points(std::span<const cw::Point> points) {
    for (const cw::Point &p : points) {
        if (p.x >= 0 && p.x <= X_BED_SIZE && p.y >= 0 && p.y <= Y_BED_SIZE) {
            if (MsgBoxWarning(_("This position is above the print sheet. Is the wiper really there?"), Responses_YesNo, 1) != Response::Yes) {
                return false;
            }
            break;
        }
    }

    for (const cw::Point &p : points) {
        if (near_front_strip(p)) {
            if (MsgBoxWarning(_("This position is near where the printer purges, checks the nozzle by tapping the bed or parks for a filament change. Make sure the wiper and its mount stay clear of those spots. Save anyway?"), Responses_YesNo, 1) != Response::Yes) {
                return false;
            }
            break;
        }
    }
    return true;
}

} // namespace

MI_WIPER_STATUS::MI_WIPER_STATUS()
    : WI_INFO_t(_("Wiper Position")) {
    refresh();
}

void MI_WIPER_STATUS::refresh() {
    auto &store = config_store();
    const cw::Geometry geometry = store.nozzle_wiper_geometry.get();
    StatusParams params;
    ChangeInformation(status_text(geometry, store.nozzle_wiper_width.get(), params));
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

MI_WIPE_WIDTH::MI_WIPE_WIDTH()
    : WiSpin(config_store().nozzle_wiper_width.get(), width_config, _("Wipe Width")) {
}

void MI_WIPE_WIDTH::OnClick() {
    auto &store = config_store();
    const cw::Geometry geometry = store.nozzle_wiper_geometry.get();
    const float old_width = store.nozzle_wiper_width.get();
    if (const auto lost = tool_lost_to_width(geometry, old_width, value())) {
        show_problem(*lost);
        set_value(old_width);
        return;
    }
    store.nozzle_wiper_width.set(value());
}

MI_WIPE_SPEED::MI_WIPE_SPEED()
    : WiSpin(config_store().nozzle_wiper_speed.get(), speed_config, _("Wipe Speed (mm/s)")) {
}

void MI_WIPE_SPEED::OnClick() {
    config_store().nozzle_wiper_speed.set(static_cast<uint8_t>(value()));
}

MI_GUARDED_DESCENT::MI_GUARDED_DESCENT()
    : WI_ICON_SWITCH_OFF_ON_t(config_store().nozzle_wiper_guarded_descent.get(), _("Guarded Descent")) {
}

void MI_GUARDED_DESCENT::OnChange([[maybe_unused]] size_t old_index) {
    config_store().nozzle_wiper_guarded_descent.set(value());
}

MI_SOAK_TIME::MI_SOAK_TIME()
    : WiSpin(config_store().nozzle_wiper_soak_time.get(), soak_time_config, _("Soak Time")) {
}

void MI_SOAK_TIME::OnClick() {
    config_store().nozzle_wiper_soak_time.set(static_cast<uint16_t>(value()));
}

MI_WIPE_BEFORE_PROBING::MI_WIPE_BEFORE_PROBING()
    : WI_ICON_SWITCH_OFF_ON_t(config_store().nozzle_wiper_auto_probing.get(), _("Wipe Before Probing")) {
}

void MI_WIPE_BEFORE_PROBING::OnChange([[maybe_unused]] size_t old_index) {
    config_store().nozzle_wiper_auto_probing.set(value());
}

ScreenMenuNozzleWiper::ScreenMenuNozzleWiper()
    : ScreenMenuNozzleWiper_(_("NOZZLE WIPER SETUP")) {
    ClrMenuTimeoutClose();

    Item<MI_COOLDOWN>().callback = [] {
        for (auto tool : PhysicalToolIndex::all()) {
            marlin_client::set_target_nozzle(0, tool);
        }
        marlin_client::set_target_bed(0);
    };
    Item<MI_SAVE_START>().callback = [this] {
        save_start();
    };
    Item<MI_SAVE_END>().callback = [this] {
        save_end();
    };
    Item<MI_GO_TO_WAITING_POINT>().callback = [this] {
        if (confirm_bed_clear()) {
            go_to_waiting_point();
        }
    };
    Item<MI_TEST_WIPE>().callback = [] {
        if (confirm_bed_clear()) {
            marlin_client::gcode("G12 D");
        }
    };
    Item<MI_WIPE_NOW>().callback = [] {
        if (confirm_bed_clear()) {
            marlin_client::gcode("G12");
        }
    };
    Item<MI_DEEP_CLEAN>().callback = [] {
        const auto tool = PhysicalToolIndex::currently_selected_opt();
        if (!tool.has_value()) {
            return;
        }
        if (FilamentType::for_tool(VirtualToolIndex::currently_selected()) == FilamentType::none && marlin_vars().hotend(*tool).target_nozzle.get() <= 0) {
            MsgBoxWarning(_("The deep clean soaks the nozzle at the loaded filament's temperature. Load a filament or set a nozzle temperature first."), Responses_Ok);
            return;
        }
        if (confirm_bed_clear()) {
            marlin_client::gcode("G12 H");
        }
    };

    loop();
}

ScreenMenuNozzleWiper::~ScreenMenuNozzleWiper() {
    // The nozzle may have been left down on the wiper
    if (lowest_jog_z < entry_z) {
        marlin_client::gcode_printf("G27 P2 Z%.1f", static_cast<double>(exit_lift));
    }
}

void ScreenMenuNozzleWiper::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    if (event == GUI_event_t::LOOP) {
        loop();
    }

    ScreenMenu::windowEvent(sender, event, param);
}

bool ScreenMenuNozzleWiper::jog_allowed() const {
    const auto tool = PhysicalToolIndex::currently_selected_opt();
    return tool.has_value()
        && !marlin_client::is_printing()
        && marlin_server::all_axes_homed()
        && marlin_vars().hotend(*tool).temp_nozzle.get() <= jog_max_temp;
}

void ScreenMenuNozzleWiper::loop() {
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

    Item<MI_SAVE_START>().set_enabled(can_jog && settled);
    Item<MI_SAVE_END>().set_enabled(can_jog && settled);

    const auto tool = PhysicalToolIndex::currently_selected_opt();
    const bool can_run = tool.has_value() && settled && !marlin_client::is_printing() && nozzle_cleaner_lite::custom_geometry_valid();
    const bool custom_wiper_on = nozzle_cleaner_lite::is_available() && nozzle_cleaner_lite::uses_custom_geometry();
    const bool cold = tool.has_value() && marlin_vars().hotend(*tool).temp_nozzle.get() <= jog_max_temp;
    Item<MI_GO_TO_WAITING_POINT>().set_enabled(can_run);
    Item<MI_TEST_WIPE>().set_enabled(can_run && custom_wiper_on && cold);
    Item<MI_WIPE_NOW>().set_enabled(can_run && custom_wiper_on);
    Item<MI_DEEP_CLEAN>().set_enabled(can_run && custom_wiper_on);

    Item<MI_WIPER_STATUS>().refresh();
}

void ScreenMenuNozzleWiper::sync_jog_items() {
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

void ScreenMenuNozzleWiper::plan_jog() {
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

void ScreenMenuNozzleWiper::save_start() {
    const xyz_float_t tip = tip_position();
    if (!jog_allowed()) {
        return;
    }
    if (!(tip.z >= cw::wipe_z_min && tip.z <= cw::wipe_z_max)) {
        show_problem({ .fault = cw::Fault::wipe_z });
        return;
    }

    auto &store = config_store();
    cw::Geometry candidate = store.nozzle_wiper_geometry.get();
    candidate.start_x = tip.x;
    candidate.start_y = tip.y;
    candidate.wipe_z = tip.z;
    candidate.taught |= cw::Geometry::taught_start;

    if (candidate.is_taught()) {
        const auto plan = nozzle_cleaner_lite::plan_custom_wipe(candidate, store.nozzle_wiper_width.get());
        switch (const cw::Problem problem = plan.has_value() ? cw::Problem {} : plan.error(); problem.fault) {
        case cw::Fault::none:
            break;
        case cw::Fault::wipe_z:
        case cw::Fault::travel_z:
            // The height comes from the start alone
            show_problem(problem);
            return;
        default:
            if (MsgBoxWarning(_("The saved end does not fit this start. Save the start and clear the end?"), Responses_YesNo, 1) != Response::Yes) {
                return;
            }
            candidate.taught = static_cast<uint8_t>(candidate.taught & ~cw::Geometry::taught_end);
            break;
        }
    }

    const cw::Point start { tip.x, tip.y };
    if (!confirm_points({ &start, 1 })) {
        return;
    }
    store.nozzle_wiper_geometry.set(candidate);
}

void ScreenMenuNozzleWiper::save_end() {
    const xyz_float_t tip = tip_position();
    if (!jog_allowed()) {
        return;
    }

    auto &store = config_store();
    cw::Geometry candidate = store.nozzle_wiper_geometry.get();
    candidate.end_x = tip.x;
    candidate.end_y = tip.y;
    candidate.taught |= cw::Geometry::taught_end;

    if (candidate.taught & cw::Geometry::taught_start) {
        if (!(std::abs(tip.z - candidate.wipe_z) <= end_height_tolerance)
            && MsgBoxWarning(_("The nozzle is more than 1 mm off the height saved with the start. Only the start sets the wipe height. Save the end anyway?"), Responses_YesNo, 1) != Response::Yes) {
            return;
        }
    }

    std::array<cw::Point, 2> points { cw::Point { tip.x, tip.y } };
    size_t point_count = 1;
    if (candidate.is_taught()) {
        const auto plan = nozzle_cleaner_lite::plan_custom_wipe(candidate, store.nozzle_wiper_width.get());
        if (!plan.has_value()) {
            show_problem(plan.error());
            return;
        }
        const xyz_pos_t offset = hotend_currently_applied_offset;
        points[point_count++] = { plan->waiting_point.x + offset.x, plan->waiting_point.y + offset.y };
    }
    if (!confirm_points({ points.data(), point_count })) {
        return;
    }
    store.nozzle_wiper_geometry.set(candidate);
}

void ScreenMenuNozzleWiper::go_to_waiting_point() {
    const auto point = nozzle_cleaner_lite::custom_waiting_point();
    if (!point.has_value()) {
        return;
    }
    // P1: absolute Z, lifted before the XY move or lowered after it
    marlin_client::gcode_printf("G27 X%.3f Y%.3f Z%.3f P1", static_cast<double>(point->x), static_cast<double>(point->y), static_cast<double>(point->z));
}
