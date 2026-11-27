#include "chamber_soak.hpp"

#include <config_store/store_instance.hpp>
#include <core/serial.h>
#include <feature/chamber/chamber.hpp>
#include <feature/chamber/chamber_requirements.hpp>
#include <feature/print_status_message/print_status_message_guard.hpp>
#include <feature/safety_timer/safety_timer.hpp>
#include <marlin_server.hpp>
#include <marlin_stubs/skippable_gcode.hpp>
#include <Marlin/src/Marlin.h>
#include <module/planner.h>
#include <module/temperature.h>
#include <pwm_utils.hpp>
#include <raii/scope_guard.hpp>
#include <timing.h>
#include <tool_index.hpp>

void PrusaGcodeSuite::chamber_soak() {
    using buddy::chamber;

    const uint32_t max_soak_ms = config_store().xl_enclosure_soak_max_min.get() * 60'000;
    const auto heating_through_bed = [] {
        return chamber().is_heating_through_bed() && thermalManager.degTargetBed() > 0;
    };

    if (max_soak_ms == 0 || !heating_through_bed() || DEBUGGING(DRYRUN) || !marlin_server::is_printing()) {
        return;
    }

    const auto soak_goal_opt = buddy::print_chamber_requirements().soak_goal;
    if (!soak_goal_opt.has_value()) {
        return;
    }
    const buddy::Temperature soak_goal = *soak_goal_opt;

    // The tolerance M191 waits with
    static constexpr buddy::Temperature tolerance = 3;

    // Keep everything heated up while soaking
    buddy::SafetyTimerBlocker safety_timer_blocker;
    SkippableGCode::Guard skippable_operation;
    PrintStatusMessageGuard status_guard;

    // Stir the chamber air with the print fan of the picked tool. Going through the
    // print fan value also lets the bed know a fan is blowing at it.
    const uint8_t soak_fan_percent = config_store().xl_enclosure_soak_fan_percent.get();
    const bool stir_air = soak_fan_percent > 0 && PhysicalToolIndex::currently_selected_opt().has_value();
    const uint8_t fan_speed_at_start = thermalManager.get_print_fan_speed();
    if (stir_air) {
        thermalManager.set_print_fan_speed(PWM255::from_percent(soak_fan_percent).value);
    }
    ScopeGuard fan_restore_guard = [&] {
        if (stir_air) {
            thermalManager.set_print_fan_speed(fan_speed_at_start);
        }
    };

    const uint32_t start_ms = ticks_ms();
    while (!skippable_operation.is_skip_requested() && !planner.draining() && heating_through_bed()) {
        const uint32_t elapsed_ms = ticks_diff(ticks_ms(), start_ms);
        if (elapsed_ms >= max_soak_ms) {
            break;
        }

        if (const auto current = chamber().current_temperature()) {
            if (*current >= soak_goal - tolerance) {
                break;
            }
            status_guard.update<PrintStatusMessage::waiting_for_chamber_temp>({ .current = *current, .target = soak_goal });
        } else {
            // No estimate yet, show how much of Max Soak Time has passed
            status_guard.update<PrintStatusMessage::absorbing_heat>({ .current = 100.0f * elapsed_ms / max_soak_ms, .target = 100 });
        }

        idle(true);
    }
}
