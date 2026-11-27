#include "include/nozzle_cleaner_lite.hpp"
#include "cleaner_zigzag.hpp"
#include <printers.h>
#include "Marlin/src/gcode/gcode.h"
#include <Marlin/src/Marlin.h>
#include <Marlin/src/module/motion.h>
#include <Marlin/src/module/planner.h>
#include <Marlin/src/module/probe.h>
#include <Marlin/src/module/temperature.h>
#include <option/has_toolchanger.h>
#if HAS_TOOLCHANGER()
    #include <Marlin/src/module/tool_change.h>
    #include <module/prusa/toolchanger.h>
    #include <gcode/gcode_info.hpp>
#endif
#include <option/has_spool_join.h>
#if HAS_SPOOL_JOIN()
    #include <module/prusa/spool_join.hpp>
#endif
#include <tool/hotend/hotend.hpp>
#include <Marlin/src/feature/pressure_advance/pressure_advance_config.hpp>
#include "loadcell.hpp"
#include <feature/print_status_message/print_status_message_guard.hpp>
#include <logging/log.hpp>
#include <raii/scope_guard.hpp>
#include <config_store/store_definition.hpp>
#include <bsod/bsod.h>
#include <timing.h>

#if HAS_CUSTOM_NOZZLE_WIPER()
    #include "custom_wiper_geometry.hpp"
    #include <filament.hpp>
    #include <marlin_server.hpp>
    #include <option/has_crash_detection.h>
    #if HAS_CRASH_DETECTION()
        #include <feature/prusa/crash_recovery.hpp>
    #endif
#endif

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <limits>

LOG_COMPONENT_DEF(NozzleCleanerLite, logging::Severity::info);

namespace nozzle_cleaner_lite {

namespace {
    enum class CleanerAxis : uint8_t {
        x,
        y,
    };

#if PRINTER_IS_PRUSA_XL()
    // Touchpoint: fixed reference point on the cleaner
    constexpr xy_pos_t touchpoint_xy = { { { -6.45f, 70.0f } } };

    constexpr CleanerAxis cleaner_axis = CleanerAxis::y;
    constexpr float cleaner_distance = 10.0f;
    constexpr float cleaner_length = 30.0f;

    constexpr float across_offset = -1.0f;
#elif PRINTER_IS_PRUSA_COREONE()
    constexpr xy_pos_t touchpoint_xy = { { { 206.5f, -15.0f } } };

    constexpr CleanerAxis cleaner_axis = CleanerAxis::x;
    constexpr float cleaner_distance = -10.0f;
    constexpr float cleaner_length = -30.0f;

    constexpr float across_offset = -1.5f;
#elif PRINTER_IS_PRUSA_COREONEL()
    constexpr xy_pos_t touchpoint_xy = { { { 299.5f, -7.5f } } };

    constexpr CleanerAxis cleaner_axis = CleanerAxis::x;
    constexpr float cleaner_distance = -10.0f;
    constexpr float cleaner_length = -30.0f;

    constexpr float across_offset = 0.0f;
#else
    #error "nozzle_cleaner_lite sequence not defined for this printer variant"
#endif

    // The pad's length runs along cleaner_axis and its width across the other
    // axis. Everything below is expressed in those two, so nothing has to ask
    // again which machine axis is which.
    constexpr bool pad_length_runs_along_x = (cleaner_axis == CleanerAxis::x);

    constexpr float touchpoint_along = pad_length_runs_along_x ? touchpoint_xy.x : touchpoint_xy.y;
    constexpr float touchpoint_across = pad_length_runs_along_x ? touchpoint_xy.y : touchpoint_xy.x;

    constexpr float along_travel_min = pad_length_runs_along_x ? X_MIN_POS : Y_MIN_POS;
    constexpr float along_travel_max = pad_length_runs_along_x ? X_MAX_POS : Y_MAX_POS;
    constexpr float across_travel_min = pad_length_runs_along_x ? Y_MIN_POS : X_MIN_POS;
    constexpr float across_travel_max = pad_length_runs_along_x ? Y_MAX_POS : X_MAX_POS;

    // 3mm sits comfortably inside the 7mm pad on every printer; only where that
    // band sits differs, because the travel left around the centre line does.
    constexpr float used_pad_width = 3.0f;
    constexpr float pad_along_near = touchpoint_along + cleaner_distance;
    constexpr float pad_across_min = touchpoint_across + across_offset;
    constexpr Pad cleaner_pad {
        .along_near = pad_along_near,
        .along_far = pad_along_near + cleaner_length,
        .across_min = pad_across_min,
        .across_max = pad_across_min + used_pad_width,
    };

    // Fractional on purpose: consecutive strokes take different diagonals instead
    // of every stroke retracing one. At 1.5 a stroke holds two or three diagonal
    // segments and the pattern comes back around every fourth stroke.
    constexpr float crossings_per_stroke = 1.5f;
    // A non-positive value would make the zigzag drift walk away from the stroke end without ever terminating.
    static_assert(crossings_per_stroke > 0.0f);

    constexpr float travel_limit_margin = 0.4f;
    static_assert(std::min(cleaner_pad.along_near, cleaner_pad.along_far) - travel_limit_margin >= along_travel_min);
    static_assert(std::max(cleaner_pad.along_near, cleaner_pad.along_far) + travel_limit_margin <= along_travel_max);
    static_assert(cleaner_pad.across_min - travel_limit_margin >= across_travel_min);
    static_assert(cleaner_pad.across_max + travel_limit_margin <= across_travel_max);

    // Z targets relative to the probed touchpoint surface
    constexpr float safe_above_surface_mm = 2.0f;
    constexpr float wipe_z_offset_mm = -0.5f + 0.2f; // touchpoint is 0.2mm below the wiper top, we need to dive 0.5mm into wiper
    constexpr float travel_clearance_mm = 10.0f;
    constexpr float final_clearance_mm = 20.0f;

    constexpr feedRate_t approach_feedrate = MMM_TO_MMS(1200);
    constexpr feedRate_t leave_feedrate = MMM_TO_MMS(1200);
    constexpr feedRate_t dive_feedrate = MMM_TO_MMS(6000);
    constexpr feedRate_t rub_feedrate_fast = MMM_TO_MMS(8000);
    constexpr feedRate_t rub_feedrate_slow = MMM_TO_MMS(1500);

    constexpr uint8_t rub_cycles_fast = 5;
    constexpr uint8_t rub_cycles_slow = 2;
    constexpr float rub_acceleration = 5000.0f;

    // Move in raw machine coordinates (line_to_machine_pos), bypassing MBL.
    void move_to_machine_pos_xy(float x, float y, feedRate_t fr_mm_s) {
        line_to_machine_pos(current_machine_position().with_xy(x, y), fr_mm_s);
        planner.synchronize();
    }

    void move_to_machine_pos_z(float z, feedRate_t fr_mm_s) {
        line_to_machine_pos(current_machine_position().with_z(z), fr_mm_s);
        planner.synchronize();
    }

    /// machine Z where the nozzle touched the touchpoint, nan if it never did.
    float find_touchpoint_z() {
        pressure_advance::PressureAdvanceDisabler pa_disabler;
        Loadcell::HighPrecisionEnabler loadcell_high_precision_enabler(loadcell);

        if (!do_homing_move(Z_AXIS, Z_PROBE_LOW_POINT - current_machine_position().z)) {
            log_error(NozzleCleanerLite, "touchpoint not found above Z %.1fmm", static_cast<double>(Z_PROBE_LOW_POINT));
            return std::numeric_limits<float>::quiet_NaN();
        }

        return current_machine_position().z;
    }

    // Queue a move given in pad coordinates.
    void line_to_pad(float along, float across, feedRate_t fr_mm_s) {
        const auto target = pad_length_runs_along_x
            ? current_machine_position().with_xy(along, across)
            : current_machine_position().with_xy(across, along);
        line_to_machine_pos(target, fr_mm_s);
    }

    void move_to_pad(float along, float across, feedRate_t fr_mm_s) {
        line_to_pad(along, across, fr_mm_s);
        planner.synchronize();
    }

    // The waypoints are only queued, so the nozzle blends through the zigzag's
    // turns instead of stopping on the pad at each one.
    void rub_stroke(ZigZag &zigzag, float along_to, feedRate_t fr_mm_s) {
        zigzag.begin_stroke(along_to);
        while (const auto waypoint = zigzag.next()) {
            line_to_pad(waypoint->along, waypoint->across, fr_mm_s);
        }
        planner.synchronize();
    }

} // namespace

#if HAS_CUSTOM_NOZZLE_WIPER()
    #if !HAS_HOTEND_OFFSET
        #error "The custom wiper geometry is stored in the nozzle tip frame and needs tool offsets"
    #endif

namespace {
    namespace cw = custom_wiper;

    cw::Offset to_offset(const xyz_pos_t &offset) {
        return { offset.x, offset.y, offset.z };
    }

    /// The taught geometry in the machine frame of the tool picked now
    struct MachineGeometry {
        cw::Frame frame;
        cw::Point staging;
        float wipe_z;
        float approach_z;
    };

    std::optional<MachineGeometry> to_machine(const cw::Geometry &geometry) {
        const cw::Offset offset = to_offset(hotend_currently_applied_offset);
        const auto frame = cw::make_frame(
            cw::tip_to_machine({ geometry.start_x, geometry.start_y }, offset),
            cw::tip_to_machine({ geometry.end_x, geometry.end_y }, offset));
        if (!frame.has_value()) {
            return std::nullopt;
        }

        const float wipe_z = cw::tip_to_machine_z(geometry.wipe_z, offset);
        return MachineGeometry {
            .frame = *frame,
            .staging = cw::staging_point(*frame),
            .wipe_z = wipe_z,
            .approach_z = wipe_z + cw::approach_clearance,
        };
    }

    float tallest_tool_offset_z() {
        float tallest = hotend_currently_applied_offset.z;
        for (auto tool : PhysicalToolIndex::all().skip_all_disabled()) {
            tallest = std::max(tallest, hotend_offset[tool].z);
        }
        return tallest;
    }

    std::optional<int16_t> loaded_filament_nozzle_temp() {
        const FilamentType filament = FilamentType::for_tool(VirtualToolIndex::currently_selected());
        if (filament == FilamentType::none) {
            return std::nullopt;
        }
        return filament.parameters().nozzle_temperature;
    }

    void line_to_frame(const cw::Frame &frame, float along, float across, feedRate_t fr_mm_s) {
        const cw::Point xy = cw::pad_to_xy(frame, along, across);
        line_to_machine_pos(current_machine_position().with_xy(xy.x, xy.y), fr_mm_s);
    }

    void rub_stroke(ZigZag &zigzag, const cw::Frame &frame, float along_to, feedRate_t fr_mm_s) {
        zigzag.begin_stroke(along_to);
        while (const auto waypoint = zigzag.next()) {
            line_to_frame(frame, waypoint->along, waypoint->across, fr_mm_s);
        }
        planner.synchronize();
    }

    /// Lowers the nozzle to machine \p z with the loadcell armed.
    /// \returns machine Z where it touched something, nullopt if it got to \p z untouched
    std::optional<float> guarded_descent(float z) {
        pressure_advance::PressureAdvanceDisabler pa_disabler;
        Loadcell::HighPrecisionEnabler loadcell_high_precision_enabler(loadcell);

        if (!do_homing_move(Z_AXIS, z - current_machine_position().z)) {
            return std::nullopt;
        }
        return current_machine_position().z;
    }

    enum class WaitResult : uint8_t {
        reached,
        timed_out,
        aborted,
        target_changed,
    };

    /// Waits for the nozzle to get between \p min_temp and \p max_temp while
    /// its heater holds \p target. Runs the print fan once the nozzle is too hot.
    WaitResult wait_for_touch_range(PhysicalToolIndex tool, int16_t target, float min_temp, float max_temp, PrintStatusMessageGuard &status) {
        Hotend &hotend = Hotend::for_tool(tool);
        const uint32_t start_ms = ticks_ms();
        bool fan_on = false;

        while (true) {
            if (planner.draining()) {
                return WaitResult::aborted;
            }
            if (hotend.nozzle_target_temp() != target) {
                return WaitResult::target_changed;
            }

            if (const auto current = hotend.nozzle_temp(); current.has_value()) {
                const float temp = current.value();
                if (temp >= min_temp && temp <= max_temp) {
                    return WaitResult::reached;
                }

                const bool too_hot = temp > max_temp;
                if (too_hot && !fan_on) {
                    thermalManager.set_print_fan_speed(255);
                    fan_on = true;
                }
                status.update<PrintStatusMessage::waiting_for_hotend_temp>({
                    .progress = { .current = temp, .target = too_hot ? max_temp : static_cast<float>(target) },
                    .tool = tool.to_raw(),
                });
            }

            if (ticks_diff(ticks_ms(), start_ms) >= static_cast<int32_t>(cw::wait_timeout_s) * 1000) {
                return WaitResult::timed_out;
            }
            idle(true);
        }
    }

    bool clean_custom(const CleanArgs &args) {
        PrintStatusMessageGuard status_message;
        status_message.update<PrintStatusMessage::nozzle_cleaner_lite>({});

        const std::optional<PhysicalToolIndex> tool = PhysicalToolIndex::currently_selected_opt();
        if (!tool) {
            log_error(NozzleCleanerLite, "no tool selected");
            return false;
        }

        auto &store = config_store();
        const cw::Geometry geometry = store.nozzle_wiper_geometry.get();
        const float width = store.nozzle_wiper_width.get();
        if (const cw::Fault fault = validate_custom_geometry(geometry, width); fault != cw::Fault::none) {
            log_error(NozzleCleanerLite, "custom wiper unusable: %s", cw::to_string(fault));
            return false;
        }

        if (!cw::print_clearance_ok(planner.max_printed_z, tallest_tool_offset_z(), geometry.wipe_z)) {
            log_error(NozzleCleanerLite, "printed part reaches above the custom wiper");
            return false;
        }

        if (!all_axes_homed()) {
            // Homing Z mid-print would lower the nozzle onto the part
            if (marlin_server::is_printing()) {
                log_error(NozzleCleanerLite, "not homed");
                return false;
            }
            if (!GcodeSuite::G28_no_parser(true, true, true, G28Flags { .only_if_needed = true })) {
                log_error(NozzleCleanerLite, "homing failed");
                return false;
            }
        }

    #if HAS_CRASH_DETECTION()
        // The wipe moves to absolute positions itself: inhibit XYZ repositioning on replay
        crash_s.set_gcode_replay_flags(Crash_s::RECOVER_AXIS_STATE);
    #endif

        const std::optional<MachineGeometry> machine = to_machine(geometry);
        if (!machine.has_value()) {
            return false;
        }
        const cw::Frame &frame = machine->frame;
        const cw::Point &staging = machine->staging;
        const float wipe_z = machine->wipe_z;
        const float approach_z = machine->approach_z;
        const Pad pad = cw::make_pad(frame, width);
        const float entry_z = current_machine_position().z;

        Hotend &hotend = Hotend::for_tool(*tool);
        const int16_t saved_nozzle_target = hotend.nozzle_target_temp();
        ScopeGuard restore_nozzle_target([&] {
            hotend.set_nozzle_target_temp(saved_nozzle_target);
        });
        const uint8_t saved_fan_speed = thermalManager.get_print_fan_speed();
        ScopeGuard restore_fan_speed([&] {
            thermalManager.set_print_fan_speed(saved_fan_speed);
        });

        // Declared after the temperature guards so that the nozzle leaves before they restore
        ScopeGuard retreat([&] {
            if (planner.draining()) {
                return;
            }
            if (current_machine_position().z < approach_z) {
                move_to_machine_pos_z(approach_z, leave_feedrate);
            }
            move_to_machine_pos_xy(staging.x, staging.y, leave_feedrate);
            if (current_machine_position().z < entry_z) {
                move_to_machine_pos_z(entry_z, leave_feedrate);
            }
        });

        // Cross over to the wiper no lower than its approach height
        if (entry_z < approach_z) {
            move_to_machine_pos_z(approach_z, leave_feedrate);
        }
        move_to_machine_pos_xy(staging.x, staging.y, dive_feedrate);
        if (entry_z > approach_z) {
            move_to_machine_pos_z(approach_z, approach_feedrate);
        }
        if (planner.draining()) {
            return false;
        }

        const int16_t gate_target = args.dry_run ? 0 : cw::wiper_target_temp;
        const float gate_min_temp = args.dry_run ? std::numeric_limits<float>::lowest() : cw::heat_up_below_temp;
        const int16_t touch_limit = cw::touch_max_temp(loaded_filament_nozzle_temp());
        hotend.set_nozzle_target_temp(gate_target);
        switch (wait_for_touch_range(*tool, gate_target, gate_min_temp, touch_limit, status_message)) {
        case WaitResult::reached:
            break;
        case WaitResult::target_changed:
            // Whoever changed it (the user, the safety timer) decides the target from now on
            restore_nozzle_target.disarm();
            log_error(NozzleCleanerLite, "nozzle target changed while waiting");
            return false;
        case WaitResult::timed_out:
            log_error(NozzleCleanerLite, "nozzle did not reach the wiper touch range in time");
            return false;
        case WaitResult::aborted:
            return false;
        }
        status_message.update<PrintStatusMessage::nozzle_cleaner_lite>({});

        // Start the zigzag somewhere new each run so consecutive cleans do not retrace the same track.
        ZigZag zigzag { pad, crossings_per_stroke, static_cast<float>(ticks_ms() % 1000) / 2000.0f };
        const auto zigzag_start = zigzag.start();
        line_to_frame(frame, zigzag_start.along, zigzag_start.across, approach_feedrate);
        planner.synchronize();

        if (store.nozzle_wiper_guarded_descent.get()) {
            const std::optional<float> touched_z = guarded_descent(wipe_z);
            if (touched_z.has_value() && *touched_z > wipe_z + cw::guard_allowance) {
                log_error(NozzleCleanerLite, "obstruction at Z=%.2f, wiper expected at Z=%.2f", static_cast<double>(*touched_z), static_cast<double>(wipe_z));
                return false;
            }
        } else {
            move_to_machine_pos_z(wipe_z, homing_feedrate(Z_AXIS));
        }
        if (planner.draining()) {
            return false;
        }
        log_info(NozzleCleanerLite, "Custom wiper: wiping at Z=%.3f", static_cast<double>(current_machine_position().z));

        const float saved_travel_acceleration = planner.user_settings.travel_acceleration;
        {
            auto s = planner.user_settings;
            s.travel_acceleration = rub_acceleration;
            planner.apply_settings(s);
        }

        const feedRate_t fast_feedrate = std::clamp(store.nozzle_wiper_speed.get(), cw::speed_min, cw::speed_max);
        if (!args.dry_run) {
            for (uint8_t i = 0; i < cw::fast_cycles; ++i) {
                rub_stroke(zigzag, frame, pad.along_far, fast_feedrate);
                rub_stroke(zigzag, frame, pad.along_near, fast_feedrate);
            }
        }
        for (uint8_t i = 0; i < cw::slow_cycles; ++i) {
            rub_stroke(zigzag, frame, pad.along_far, cw::slow_speed);
            rub_stroke(zigzag, frame, pad.along_near, cw::slow_speed);
        }

        {
            auto s = planner.user_settings;
            s.travel_acceleration = saved_travel_acceleration;
            planner.apply_settings(s);
        }

        return !planner.draining();
    }

} // namespace

bool uses_custom_geometry() {
    return config_store().nozzle_wiper_custom.get();
}

cw::Fault validate_custom_geometry(const cw::Geometry &geometry, float width) {
    // The applied offset is what a wipe now moves with; the stored ones are what any later wipe will
    std::array<cw::Offset, PhysicalToolIndex::count + 1> offsets;
    size_t offset_count = 0;
    offsets[offset_count++] = to_offset(hotend_currently_applied_offset);
    for (auto tool : PhysicalToolIndex::all().skip_all_disabled()) {
        offsets[offset_count++] = to_offset(hotend_offset[tool]);
    }

    static constexpr cw::TravelLimits limits {
        .x_min = X_MIN_POS + travel_limit_margin,
        .x_max = X_MAX_POS - travel_limit_margin,
        .y_min = Y_MIN_POS + travel_limit_margin,
        // Stays out of the tool dock area behind the bed
        .y_max = Y_BED_SIZE,
        .z_min = Z_MIN_POS,
    };
    return cw::validate(geometry, width, std::span(offsets.data(), offset_count), limits);
}

bool custom_geometry_valid() {
    auto &store = config_store();
    return validate_custom_geometry(store.nozzle_wiper_geometry.get(), store.nozzle_wiper_width.get()) == cw::Fault::none;
}

std::optional<xyz_pos_t> custom_waiting_point() {
    if (!custom_geometry_valid()) {
        return std::nullopt;
    }
    const auto machine = to_machine(config_store().nozzle_wiper_geometry.get());
    if (!machine.has_value()) {
        return std::nullopt;
    }
    return xyz_pos_t { { { machine->staging.x, machine->staging.y, machine->approach_z } } };
}

bool wipe_before_probing() {
    if (!is_available() || !uses_custom_geometry() || !config_store().nozzle_wiper_auto_probing.get()) {
        return true;
    }
    if (!custom_geometry_valid()) {
        log_warning(NozzleCleanerLite, "custom wiper not usable, no wipe before probing");
        return true;
    }

    const bool cleaned = clean({ .cleaning_temp = 0, .probe_temp = std::nullopt, .cooldown = false, .keep_target = false });

    // The bed-tap check that follows runs at the target the start G-code set
    if (const auto tool = PhysicalToolIndex::currently_selected_opt(); tool.has_value() && !planner.draining()) {
        thermalManager.wait_for_hotend(*tool, { .no_wait_for_cooling = false });
    }
    return cleaned;
}
#endif

bool is_available() {
    return config_store().nozzle_cleaner_lite_installed.get();
}

bool clean(CleanArgs args) {
    release_assert(is_available());

#if HAS_CUSTOM_NOZZLE_WIPER()
    if (uses_custom_geometry()) {
        return clean_custom(args);
    }
#endif

    const int16_t rest_temp = args.probe_temp.value_or(args.cleaning_temp - cooldown_temp_diff);
    release_assert(!args.cooldown || rest_temp <= args.cleaning_temp);

    PrintStatusMessageGuard status_message;
    status_message.update<PrintStatusMessage::nozzle_cleaner_lite>({});

    const std::optional<PhysicalToolIndex> tool = PhysicalToolIndex::currently_selected_opt();
    if (!tool) {
        log_error(NozzleCleanerLite, "no tool selected");
        return false;
    }

    // Start heating used tool and restore target temp on exit
    const int16_t saved_nozzle_target = Hotend::for_tool(*tool).nozzle_target_temp();
    Hotend::for_tool(*tool).set_nozzle_target_temp(args.cleaning_temp);
    ScopeGuard restore_nozzle_target([&] {
        Hotend::for_tool(*tool).set_nozzle_target_temp(saved_nozzle_target);
    });

    if (!GcodeSuite::G28_no_parser(true, true, true, G28Flags { .only_if_needed = true })) {
        log_error(NozzleCleanerLite, "homing failed");
        return false;
    }

    move_to_machine_pos_z(current_machine_position().z + travel_clearance_mm, leave_feedrate);
    move_to_machine_pos_xy(touchpoint_xy.x, touchpoint_xy.y, dive_feedrate);

    // reach the cleaning temperature before touching down
    if (!thermalManager.wait_for_hotend(*tool, { .no_wait_for_cooling = false })) {
        log_error(NozzleCleanerLite, "temperature wait failed");
        return false;
    }

    const float probed_z = find_touchpoint_z();
    if (std::isnan(probed_z)) {
        return false;
    }
    log_info(NozzleCleanerLite, "Touchpoint surface at Z=%.3f", static_cast<double>(probed_z));

    move_to_machine_pos_z(probed_z + safe_above_surface_mm, approach_feedrate);

    // Start the zigzag somewhere new each run so consecutive cleans do not retrace the same track.
    ZigZag zigzag { cleaner_pad, crossings_per_stroke,
        static_cast<float>(ticks_ms() % 1000) / 2000.0f };

    // Safely move from the touchpoint onto the pad, at the zigzag's own starting point rather than the pad centre
    const auto zigzag_start = zigzag.start();
    move_to_pad(zigzag_start.along, zigzag_start.across, approach_feedrate);
    move_to_machine_pos_z(probed_z + wipe_z_offset_mm, dive_feedrate);

    const float saved_travel_acceleration = planner.user_settings.travel_acceleration;
    {
        auto s = planner.user_settings;
        s.travel_acceleration = rub_acceleration;
        planner.apply_settings(s);
    }

    // few fast cycles, then a couple of slower ones to finish cleanly.
    for (uint8_t i = 0; i < rub_cycles_fast; ++i) {
        rub_stroke(zigzag, cleaner_pad.along_far, rub_feedrate_fast);
        rub_stroke(zigzag, cleaner_pad.along_near, rub_feedrate_fast);
    }
    for (uint8_t i = 0; i < rub_cycles_slow; ++i) {
        rub_stroke(zigzag, cleaner_pad.along_far, rub_feedrate_slow);
        rub_stroke(zigzag, cleaner_pad.along_near, rub_feedrate_slow);
    }

    {
        auto s = planner.user_settings;
        s.travel_acceleration = saved_travel_acceleration;
        planner.apply_settings(s);
    }

    const uint8_t saved_fan_speed = thermalManager.get_print_fan_speed();
    ScopeGuard restore_fan_speed([&] {
        thermalManager.set_print_fan_speed(saved_fan_speed);
    });
    if (args.cooldown) {
        // Start cooling down already on the way back to the touchpoint
        Hotend::for_tool(*tool).set_nozzle_target_temp(rest_temp);
        thermalManager.set_print_fan_speed(255);
    }

    // Retreat back over the touchpoint
    move_to_machine_pos_z(probed_z + travel_clearance_mm, leave_feedrate);
    move_to_machine_pos_xy(touchpoint_xy.x, touchpoint_xy.y, leave_feedrate);

    if (!args.cooldown) {
        return true;
    }

    move_to_machine_pos_z(probed_z, dive_feedrate);

    ScopeGuard leave_touchpoint([&] {
        move_to_machine_pos_z(probed_z + final_clearance_mm, leave_feedrate);
    });

    if (!thermalManager.wait_for_hotend(*tool, { .no_wait_for_cooling = false })) {
        log_error(NozzleCleanerLite, "cooling failed");
        return false;
    }

    if (args.keep_target) {
        restore_nozzle_target.disarm();
    }
    return true;
}

} // namespace nozzle_cleaner_lite
