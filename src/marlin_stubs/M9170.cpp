#include "M9170.hpp"
#include "PrusaGcodeSuite.hpp"

#include <common/marlin_vars.hpp>
#include <feature/gcode_exception/gcode_exception.hpp>
#include <gcode/gcode_parser.hpp>
#include <logging/log.hpp>
#include <mapi/parking.hpp>
#include <marlin_server.hpp>
#include <marlin_stubs/skippable_gcode.hpp>
#include <Marlin/src/Marlin.h>
#include <Marlin/src/feature/bedlevel/bedlevel.h>
#include <module/motion.h>
#include <module/temperature.h>
#include <raii/scope_guard.hpp>
#include <timing.h>
#include <tool_index.hpp>
#include <utils/publisher.hpp>

#include <limits>
#include <optional>

LOG_COMPONENT_REF(MarlinServer);

namespace {

/// How far the head rises when the measurement is done [mm]
constexpr float final_lift = 10;

/// Sets the bed and the nozzle of @p tool to the measurement temperatures and waits until both
/// are reached, cooling down included. A click on the wait screen ends the waiting.
/// @returns whether both temperatures were reached
bool heat_up(int16_t bed_temperature, PhysicalToolIndex tool) {
    SkippableGCode::Guard skippable;

    // The stock waits end when wait_for_heatup is cleared, as M108 does
    Subscriber skip_watcher(marlin_server::idle_publisher, [&skippable] {
        if (skippable.is_skip_requested()) {
            wait_for_heatup = false;
        }
    });

    const auto interrupted = [&] {
        return skippable.is_skip_requested() || gcode_exceptions().is_unwinding();
    };

    thermalManager.setTargetBed(bed_temperature);
    thermalManager.setTargetHotend(bed_mesh_measurement::nozzle_temperature, tool.to_raw());

    if (bed_temperature > 0 && (!thermalManager.wait_for_bed(false) || interrupted())) {
        return false;
    }
    return thermalManager.wait_for_hotend(tool, { .no_wait_for_cooling = false, .fan_cooling = true }) && !interrupted();
}

constexpr int32_t soak_limit_ms = int32_t { bed_mesh_measurement::soak_limit_minutes } * 60 * 1000;

/// Waits as a print start does until the bed frame has absorbed the heat of the bed; the probing
/// would otherwise measure a bed that still changes its shape. A click on the wait screen or the
/// time limit ends the wait early, and the measurement goes on.
/// @returns false if the run is aborted
bool soak() {
    bool limit_reached = false;
    bool absorbed = false;
    {
        const uint32_t start_ms = ticks_ms();
        // Unbound as soon as the stock wait returns, so it cannot skip the homing or the probing
        Subscriber limit_watcher(marlin_server::idle_publisher, [&limit_reached, start_ms] {
            if (!limit_reached && ticks_diff(ticks_ms(), start_ms) >= soak_limit_ms) {
                limit_reached = true;
                skippable_gcode().request_skip();
            }
        });
        absorbed = thermalManager.wait_for_frame_heatup();
    }
    if (absorbed) {
        return true;
    }

    // The stock wait returns the same on a skip and on an abort
    if (gcode_exceptions().is_unwinding()) {
        return false;
    }

    log_info(MarlinServer, "M9170: heat absorption %s, measuring", limit_reached ? "time limit reached" : "skipped");
    return true;
}

} // namespace

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M9170: Measure the whole bed mesh at temperature (internal)
 *
 * Sent by the Bed Mesh screen. Does nothing unless the printer is idle and a
 * tool is picked; that tool does the measurement.
 *
 * Sets the bed to S and the nozzle to 170 °C and waits for both, cooling down
 * included. Then homes the axes that need it (G28 O), probes every major point
 * of the whole bed (G29 P1), fills in the rest of the mesh (G29 P3.2, G29 P3.13)
 * and lifts the head by 10 mm. Does not clean the nozzle, activate the mesh,
 * change or park the tool.
 *
 * While waiting for the nozzle, the print fan runs at full power if the nozzle
 * has to cool down and is off if it has to heat up; it has its previous value
 * again before homing.
 *
 * With S above 0, all bed tiles are enabled before the heating and stay
 * enabled afterwards, as they are when idle after a print. Between the
 * temperature waits and the homing, the run then waits as a print start does
 * until the bed frame has absorbed the heat (no wait at 25 °C or less). A click
 * during that wait ends it and the measurement starts at once, as it does after
 * one hour of waiting.
 *
 * Every way out sets the bed target and the target of the measuring nozzle to
 * 0; the other nozzles are left alone. A click on the wait screen ends the run
 * while waiting for the temperatures and ends the probing after the current
 * point, after which the mesh is filled in and the head lifted. "No" on the
 * probing failure prompt ends the run with the head where the prompt parked it.
 *
 *#### Usage
 *
 *    M9170 S
 *
 *#### Parameters
 *
 * - `S` - Bed temperature [°C], 0 leaves the bed off
 */
void PrusaGcodeSuite::M9170() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    static constexpr int16_t max_bed_temperature = BED_MAXTEMP - BED_MAXTEMP_SAFETY_MARGIN;
    const std::optional<int16_t> bed_temperature = p.option<int16_t>('S', static_cast<int16_t>(0), max_bed_temperature);
    if (!bed_temperature.has_value()) {
        SERIAL_ERROR_MSG("M9170: S (bed temperature) missing or out of range");
        return;
    }

    if (marlin_vars().print_state.get() != marlin_server::State::Idle) {
        SERIAL_ERROR_MSG("M9170: printer not idle");
        return;
    }

    const std::optional<PhysicalToolIndex> tool = PhysicalToolIndex::currently_selected_opt();
    if (!tool.has_value()) {
        SERIAL_ERROR_MSG("M9170: no tool picked");
        return;
    }

    marlin_server::FSM_Holder wait_screen { PhaseWait::print_status_message };

    ScopeGuard heaters_off = [measuring_tool = *tool] {
        thermalManager.setTargetBed(0);
        thermalManager.setTargetHotend(0, measuring_tool.to_raw());
    };

    // The whole bed is measured, so the whole bed is heated, whatever print area was set last
    if (*bed_temperature > 0) {
        thermalManager.setEnabledBedletMask(std::numeric_limits<uint16_t>::max());
    }

    if (!heat_up(*bed_temperature, *tool)) {
        return;
    }

    if (*bed_temperature > 0 && !soak()) {
        return;
    }

    GcodeSuite::process_subcommands_now_P("G28 O");
    if (gcode_exceptions().is_unwinding() || axes_need_homing()) {
        return;
    }

    GcodeSuite::process_subcommands_now_P("G29 P1 X0 Y0 W" STRINGIFY(X_BED_SIZE) " H" STRINGIFY(Y_BED_SIZE));
    // Still set after the probing when "No" was answered on the failure prompt
    if (gcode_exceptions().is_unwinding() || ubl.g29_probing_failed) {
        return;
    }

    GcodeSuite::process_subcommands_now_P("G29 P3.2");
    GcodeSuite::process_subcommands_now_P("G29 P3.13");

    mapi::park({ .z = mapi::ParkingPosition::Relative { final_lift } });
}

/** @}*/
