#include <marlin_stubs/PrusaGcodeSuite.hpp>

#include <config_store/store_instance.hpp>
#include <feature/chamber/bed_cooldown_ramp.hpp>
#include <feature/chamber_filtration/chamber_filtration.hpp>
#include <feature/print_status_message/print_status_message_guard.hpp>
#include <feature/safety_timer/safety_timer.hpp>
#include <marlin_server.hpp>
#include <marlin_stubs/skippable_gcode.hpp>
#include <Marlin/src/Marlin.h>
#include <module/planner.h>
#include <module/temperature.h>
#include <option/has_dwarf.h>
#include <option/has_remote_bed.h>
#include <raii/scope_guard.hpp>
#include <timing.h>
#include <xl_enclosure.hpp>

#include <algorithm>

using buddy::BedCooldownRamp;

namespace {

bool enclosure_heating_enabled() {
    return config_store().xl_enclosure_bed_heating.get() && xl_enclosure.is_active();
}

bool board_overheat_warning() {
    return marlin_server::is_warning_active(WarningType::BuddyMCUMaxTemp)
#if HAS_DWARF()
        || marlin_server::is_warning_active(WarningType::DwarfMCUMaxTemp)
#endif
#if HAS_REMOTE_BED()
        || marlin_server::is_warning_active(WarningType::BedMCUMaxTemp)
#endif
        ;
}

} // namespace

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M9160: Bed cool-down in the XL enclosure (internal)
 *
 * Enqueued by the firmware when a print that heated the enclosure through the
 * bed has finished. Steps the bed target down from the measured bed
 * temperature by whole degrees at the given rate and switches the bed off at
 * 50 °C. Then waits for the bed to get to 50 °C, at most 60 minutes from the
 * start. Holds the filtration fan off meanwhile.
 *
 * Does nothing unless the print is finished, Chamber Heating is on and the
 * enclosure is active. Ends with the bed off on Skip, when the print leaves the
 * Finished state, when anything else sets the bed target, when Chamber Heating
 * or the enclosure is switched off, and when an electronics overheat warning
 * is shown. Switches the nozzles off at the start.
 *
 *#### Usage
 *
 *    M9160 S [ R ]
 *
 *#### Parameters
 *
 * - `S` - Highest bed target to start at [°C]
 * - `R` - Rate [0.1 °C/min], clamped to 5-30; the Cooldown Rate setting if omitted, 0 does nothing
 */
void PrusaGcodeSuite::M9160() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    const auto start_limit = p.option<int16_t>('S');
    const uint8_t rate = p.option<uint8_t>('R').value_or(config_store().xl_enclosure_cooldown_rate.get());
    if (!start_limit.has_value() || rate == 0 || !marlin_server::finishing_or_finished() || !enclosure_heating_enabled()) {
        return;
    }

    // Also false on an invalid reading
    const float bed_temperature = thermalManager.degBed();
    if (!(bed_temperature > BedCooldownRamp::end_temperature)) {
        return;
    }

    // Created before switching the nozzles off: creating it restores nozzle targets the safety timer may hold
    buddy::SafetyTimerBlocker safety_timer_blocker;
    thermalManager.disable_hotend();

    buddy::chamber_filtration().set_post_print_hold(true);
    ScopeGuard release_filtration = [] {
        buddy::chamber_filtration().set_post_print_hold(false);
    };

    // Every way out leaves the bed off
    ScopeGuard bed_off = [] {
        thermalManager.setTargetBed(0);
    };

    SkippableGCode::Guard skippable_operation;
    PrintStatusMessageGuard status_guard;

    const BedCooldownRamp ramp(std::min(static_cast<int16_t>(bed_temperature), *start_limit), rate);
    const uint32_t start_ms = ticks_ms();
    int16_t bed_target = thermalManager.degTargetBed();

    while (!skippable_operation.is_skip_requested()
        && !planner.draining()
        && marlin_server::finishing_or_finished()
        && enclosure_heating_enabled()
        && !board_overheat_warning()
        // Anyone else setting the bed ends the cool-down
        && thermalManager.degTargetBed() == bed_target) {

        const uint32_t elapsed_s = ticks_diff(ticks_ms(), start_ms) / 1000;
        if (elapsed_s >= BedCooldownRamp::max_duration_s || !(thermalManager.degBed() > BedCooldownRamp::end_temperature)) {
            break;
        }

        if (const int16_t target = ramp.bed_target(elapsed_s); target != bed_target) {
            bed_target = target;
            thermalManager.setTargetBed(bed_target);
        }

        status_guard.update<PrintStatusMessage::bed_cooldown>({
            .progress = { .current = thermalManager.degBed(), .target = BedCooldownRamp::end_temperature },
            .minutes_left = ramp.minutes_left(elapsed_s),
        });

        idle(true);
    }
}

/** @}*/
