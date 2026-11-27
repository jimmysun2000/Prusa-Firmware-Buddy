/// @file
#include "screen_menu_nozzle_wiper.hpp"
#include "screen_nozzle_wiper_setup.hpp"

#include <config_store/store_instance.hpp>
#include <marlin_client.hpp>
#include <common/marlin_vars.hpp>
#include <window_msgbox.hpp>
#include <filament.hpp>
#include <timing.h>

#include <array>
#include <cmath>
#include <optional>

using namespace screen_menu_nozzle_wiper;
namespace cw = nozzle_cleaner_lite::custom_wiper;

namespace {

/// How long the server has to stay idle before a menu wipe may start
constexpr int32_t settle_ms = 300;

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

/// \returns why a tool that can wipe \p geometry with \p old_width could not with \p new_width, if one could not.
/// Not inlined, so that its two reports are off the display task's stack before the message box that follows.
[[gnu::noinline]] std::optional<cw::Problem> tool_lost_to_width(const cw::Geometry &geometry, float old_width, float new_width) {
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

/// The print clearance check knows nothing of a part left on the bed after a restart or Z homing
bool confirm_bed_clear() {
    return MsgBoxWarning(_("The bed will rise to the wiper. Remove any print from the bed first. Continue?"), Responses_YesNo, 1) == Response::Yes;
}

} // namespace

const NumericInputConfig screen_menu_nozzle_wiper::wipe_width_config {
    .min_value = 0,
    .max_value = cw::width_max,
    .step = 0.5f,
    .max_decimal_places = 1,
    .unit = Unit::millimeter,
};

std::optional<float> screen_menu_nozzle_wiper::width_setting_for(float min_width) {
    if (!(min_width > 0.0f)) {
        return std::nullopt;
    }
    const float setting = std::ceil((min_width - cw::reach_tolerance) / wipe_width_config.step) * wipe_width_config.step;
    if (!(setting <= cw::width_max)) {
        return std::nullopt;
    }
    return setting;
}

string_view_utf8 screen_menu_nozzle_wiper::problem_text(const cw::Problem &problem, ProblemParams &params) {
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

void screen_menu_nozzle_wiper::show_problem(const cw::Problem &problem) {
    if (problem.ok()) {
        return;
    }
    ProblemParams params;
    MsgBoxWarning(problem_text(problem, params), Responses_Ok);
}

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

MI_WIPE_WIDTH::MI_WIPE_WIDTH()
    : WiSpin(config_store().nozzle_wiper_width.get(), wipe_width_config, _("Wipe Width")) {
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

    Item<MI_GUIDED_SETUP>().callback = [] {
        screen_nozzle_wiper_setup::open();
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

void ScreenMenuNozzleWiper::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    if (event == GUI_event_t::LOOP) {
        loop();
    }

    ScreenMenu::windowEvent(sender, event, param);
}

void ScreenMenuNozzleWiper::loop() {
    const uint32_t now = ticks_ms();
    if (marlin_vars().is_processing.get()) {
        idle_since_ms.reset();
    } else if (!idle_since_ms.has_value()) {
        idle_since_ms = now;
    }
    const bool settled = idle_since_ms.has_value() && ticks_diff(now, *idle_since_ms) >= settle_ms;

    const bool idle = settled && !marlin_client::is_printing();
    Item<MI_GUIDED_SETUP>().set_enabled(idle);

    const auto tool = PhysicalToolIndex::currently_selected_opt();
    const bool can_run = tool.has_value() && idle && nozzle_cleaner_lite::custom_geometry_valid();
    const bool custom_wiper_on = nozzle_cleaner_lite::is_available() && nozzle_cleaner_lite::uses_custom_geometry();
    const bool cold = tool.has_value() && marlin_vars().hotend(*tool).temp_nozzle.get() <= cold_nozzle_max_temp;
    Item<MI_GO_TO_WAITING_POINT>().set_enabled(can_run);
    Item<MI_TEST_WIPE>().set_enabled(can_run && custom_wiper_on && cold);
    Item<MI_WIPE_NOW>().set_enabled(can_run && custom_wiper_on);
    Item<MI_DEEP_CLEAN>().set_enabled(can_run && custom_wiper_on);

    Item<MI_WIPER_STATUS>().refresh();
}

void ScreenMenuNozzleWiper::go_to_waiting_point() {
    const auto point = nozzle_cleaner_lite::custom_waiting_point();
    if (!point.has_value()) {
        return;
    }
    // P1: absolute Z, lifted before the XY move or lowered after it
    marlin_client::gcode_printf("G27 X%.3f Y%.3f Z%.3f P1", static_cast<double>(point->x), static_cast<double>(point->y), static_cast<double>(point->z));
}
