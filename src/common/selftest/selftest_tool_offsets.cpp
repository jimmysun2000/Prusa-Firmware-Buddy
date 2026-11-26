#include "selftest_tool_offsets.hpp"
#include "Marlin/src/gcode/queue.h"
#include "Marlin/src/module/stepper.h"
#include "marlin_server.hpp"
#include "selftest_tool_helper.hpp"
#include "Marlin/src/module/temperature.h"
#include "fanctl.hpp"
#include <module/prusa/toolchanger.h>

#include <option/has_tool_offset_pin_calibration.h>
#if HAS_TOOL_OFFSET_PIN_CALIBRATION()
    #include <marlin_stubs/G425.hpp>
#endif
#include <tool_index.hpp>

using namespace selftest;
LOG_COMPONENT_REF(Selftest);

namespace {
/// @returns whether the calibration heats the tool: every enabled tool in full mode, only the
/// reference tool and the other tool otherwise
bool is_tool_involved(PhysicalToolIndex tool, const ToolOffsetsCalibrationData &request) {
    if (!is_tool_selftest_enabled(tool, AllTools {})) {
        return false;
    }
    return request.mode == ToolOffsetsMode::full || tool == TOOL_OFFSETS_REFERENCE_TOOL || tool == request.other_tool;
}

/// @brief Set temperature to the involved tools
void set_nozzle_temps(int16_t temp, const ToolOffsetsCalibrationData &involved) {
    for (auto tool : PhysicalToolIndex::all()) {
        if (is_tool_involved(tool, involved)) {
            thermalManager.setTargetHotend(temp, tool);
        }
    }
}

/// @brief Check temperature of the involved tools is at target
bool all_nozzles_at_target(const ToolOffsetsCalibrationData &involved) {
    for (auto tool : PhysicalToolIndex::all()) {
        if (is_tool_involved(tool, involved)) {
            if (!Hotend::for_tool(tool).is_nozzle_temp_reached()) {
                return false;
            }
        }
    }
    return true;
}
}; // namespace

CSelftestPart_ToolOffsets::CSelftestPart_ToolOffsets(IPartHandler &state_machine, const ToolOffsetsConfig_t &config, SelftestToolOffsets_t &result)
    : state_machine(state_machine)
    , result(result)
    , config(config) {}

CSelftestPart_ToolOffsets::~CSelftestPart_ToolOffsets() {
    if (config.request.mode != ToolOffsetsMode::full && !committed) {
        prusa_toolchanger.load_tool_offsets();
    }
}

LoopResult CSelftestPart_ToolOffsets::state_ask_user_confirm_start() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_user_confirm_start);
    result.mode = config.request.mode;
    result.other_tool = config.request.other_tool.to_raw();
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_clean_nozzle_start() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_move_away);
    disable_all_steppers(); // Let the user operate tools, pull out the filament if required
    set_nozzle_temps(0, ToolOffsetsCalibrationData::full());

    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_move_away() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_user_clean_nozzle_cold);
    // we'll ask user to clean nozzle and put on sheet - so give him some space
    do_z_clearance(100);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_clean_nozzle() {
    const auto button_pressed = state_machine.GetButtonPressed();

    if (button_pressed == Response::Continue) {
        set_nozzle_temps(SelftestToolOffsets_t::TOOL_CALIBRATION_TEMPERATURE, config.request);
        quick_cooling.set_active(true);
        return LoopResult::RunNext;
    }

    if (IPartHandler::GetFsmPhase() == PhasesSelftest::ToolOffsets_wait_user_clean_nozzle_hot) {
        // nozzle is hot or heating up
        if (button_pressed == Response::Cooldown) {
            IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_user_clean_nozzle_cold);
            set_nozzle_temps(SelftestToolOffsets_t::TOOL_CALIBRATION_TEMPERATURE, config.request);
            quick_cooling.set_active(true);
        }
    } else if (IPartHandler::GetFsmPhase() == PhasesSelftest::ToolOffsets_wait_user_clean_nozzle_cold) {
        // nozzle is cold or cooling down
        if (button_pressed == Response::Heatup) {
            IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_user_clean_nozzle_hot);
            set_nozzle_temps(SelftestToolOffsets_t::TOOL_OFFSET_CLEANING_TEMPERATURE, config.request);
            quick_cooling.set_active(false);
        }
    }

    return LoopResult::RunCurrent;
}

LoopResult CSelftestPart_ToolOffsets::state_ask_user_install_sheet() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_user_install_sheet);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_wait_user() {
    if (state_machine.GetButtonPressed() != Response::Continue) {
        return LoopResult::RunCurrent;
    }
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_home_park() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_pin_install_prepare);

    // We need to be Z homed, so that we can find the pin
    marlin_server::enqueue_gcode("G28 Z O");

    // Park the nozzle out of the way, the user is about to remove the steel sheet.
    marlin_server::enqueue_gcode("P0 L0 D0");

    // Ensure tool will not hit calibration pin once installed
    marlin_server::enqueue_gcode("G1 Z30");

    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_wait_moves_done() {
    if (queue.has_commands_queued() || planner.processing()) {
        return LoopResult::RunCurrent;
    }
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_ask_user_install_pin() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_user_install_pin);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_wait_stable_temp() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_stable_temp);
    if (all_nozzles_at_target(config.request)) {
        quick_cooling.set_active(false);
        return LoopResult::RunNext;
    }
    return LoopResult::RunCurrent;
}

LoopResult CSelftestPart_ToolOffsets::state_calibrate() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_calibrate);
    return LoopResult::RunNext;
}

/**
 * This state exists just because full_calibration() is a blocking call and we need to update FSM state
 * to let the user know that calibration is in progress.
 * The issue is that the fsm takes update after returning from a state function, so we cannot do it in one state.
 */
LoopResult CSelftestPart_ToolOffsets::state_finish_calibration() {
    if (config.request.mode == ToolOffsetsMode::full) {
        bool calibration_success = full_calibration();
        if (!calibration_success) {
            return LoopResult::Fail;
        }
        return LoopResult::RunNext;
    }

    auto measured = pin_tool_offset::measure_partial(config.request);
    if (measured.has_value()) {
        partial_result = std::move(measured.value());
    } else {
        failure = measured.error();
    }
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_review_result() {
    if (failure.has_value()) {
        result.failure = *failure;
        IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_result_rejected);
        return LoopResult::RunNext;
    }

    if (partial_result.has_value()) {
        pin_tool_offset::commit_partial(config.request, *partial_result);
        committed = true;
    }
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_wait_review() {
    if (!failure.has_value()) {
        return LoopResult::RunNext;
    }

    if (state_machine.GetButtonPressed() != Response::Continue) {
        return LoopResult::RunCurrent;
    }

    switch (failure->reason) {

    case pin_tool_offset::FailureReason::center_not_found:
    case pin_tool_offset::FailureReason::out_of_bounds:
        // The toolchanger is in a known state, park and guide the user through removing the pin
        return LoopResult::RunNext;

    case pin_tool_offset::FailureReason::unknown:
    case pin_tool_offset::FailureReason::not_homed:
    case pin_tool_offset::FailureReason::invalid_request:
    case pin_tool_offset::FailureReason::toolchange_failed:
        // Do not move the tools any further
        break;
    }
    return fail_with_nozzles_off();
}

LoopResult CSelftestPart_ToolOffsets::state_final_park() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_move_away);
    // Let user uninstall the pin and put the steel sheet back
    marlin_server::enqueue_gcode("P0"); // Park tool
    marlin_server::enqueue_gcode("G27 Z40 P1"); // Park head, high enough to get the sheet in
    marlin_server::enqueue_gcode("M18"); // Disable steppers
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_ask_user_remove_pin() {
    IPartHandler::SetFsmPhase(PhasesSelftest::ToolOffsets_wait_user_remove_pin);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_ToolOffsets::state_report_result() {
    if (committed) {
        return LoopResult::RunNext;
    }
    return fail_with_nozzles_off();
}

LoopResult CSelftestPart_ToolOffsets::fail_with_nozzles_off() {
    set_nozzle_temps(0, config.request);
    return LoopResult::Fail;
}
