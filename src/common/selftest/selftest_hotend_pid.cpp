#include "selftest_hotend_pid.hpp"

#include "i_selftest.hpp"
#include <Marlin/src/gcode/gcode.h>
#include <Marlin/src/module/motion.h>
#include <Marlin/src/module/prusa/toolchanger.h>
#include <Marlin/src/module/temperature.h>
#include <Marlin/src/module/tool_change.h>
#include <config_store/store_instance.hpp>
#include <logging/log.hpp>
#include <mapi/parking.hpp>

LOG_COMPONENT_REF(Selftest);

using namespace selftest;
using hotend_pid::TuningFailure;

namespace {

/// Gains that make the regulator switch the heater fully on below the target and fully off above it
constexpr PID_t relay_pid { .Kp = 1000000, .Ki = 0, .Kd = 0 };

// The Dwarf regulator, like this board's, limits its output to 0..PID_MAX
static_assert(PID_MAX == 255);
constexpr float relay_half_output = PID_MAX / 2.0f;

static_assert(PhysicalToolIndex::count <= hotend_pid::TOOL_SET_CAPACITY);
constexpr hotend_pid::ToolSet all_tools = (1u << PhysicalToolIndex::count) - 1;

// A request below EXTRUDE_MINTEMP is refused, so no waiting tool is ever hotter than the tuning target
static_assert(hotend_pid::WAITING_TEMP <= EXTRUDE_MINTEMP);

/// [ms] Answers given earlier on a result page are ignored. Pages of the same kind follow each
/// other on the same phase, so a quick second tap would answer a page nobody has read yet.
constexpr uint32_t page_answer_delay_ms = 1000;

bool is_request_allowed(const HotendPidConfig_t &config) {
    const HotendPidTuningData &request = config.request;
    if (request.tool_mask == 0 || (request.tool_mask & ~all_tools) != 0 || config.heaters.size() != PhysicalToolIndex::count) {
        return false;
    }

    for (const PhysicalToolIndex tool : PhysicalToolIndex::all()) {
        if (!hotend_pid::contains(request.tool_mask, tool.to_raw())) {
            continue;
        }
        if (config.heaters[tool.to_raw()].tool_nr != tool || !tool.is_enabled()) {
            return false;
        }
        const Hotend::Config &hotend_config = Hotend::for_tool(tool).config();
        if (request.target_temp < EXTRUDE_MINTEMP || request.target_temp > hotend_config.max_nozzle_temp - HEATER_MAXTEMP_SAFETY_MARGIN) {
            return false;
        }
    }
    return true;
}

} // namespace

CSelftestPart_HotendPid::CSelftestPart_HotendPid(IPartHandler &state_machine, const HotendPidConfig_t &config, SelftestHotendPid_t &result)
    : state_machine(state_machine)
    , config(config)
    , result(result)
    , recorder(config.request.target_temp) {}

CSelftestPart_HotendPid::~CSelftestPart_HotendPid() {
    switch_session_heaters_off();
}

LoopResult CSelftestPart_HotendPid::state_check_request() {
    const HotendPidTuningData &request = config.request;
    result.tool_mask = request.tool_mask;
    result.tool = hotend_pid::first_tool(request.tool_mask).value_or(0);
    result.target_temp = request.target_temp;
    result.print_fan_pwm = request.print_fan_pwm;

    if (!is_request_allowed(config)) {
        log_error(Selftest, "Hotend PID: tuning tools 0x%02x at %d C refused", request.tool_mask, request.target_temp);
        result.failure = TuningFailure::invalid_request;
        IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_result_rejected);
        return LoopResult::RunNext;
    }

    session_tools = request.tool_mask;
    tool_in_turn = result.tool;
    IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_confirm_start);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_wait_confirm() {
    if (session_tools == 0) {
        return LoopResult::RunNext;
    }
    if (state_machine.GetButtonPressed() != Response::Continue) {
        return LoopResult::RunCurrent;
    }
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_start_session() {
    if (session_tools == 0) {
        return LoopResult::RunNext;
    }

    // The first tool in turn is not heated before it is at the front, the waiting ones are
    apply_waiting_targets();
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_mark_tool() {
    return LoopResult::MarkLoop0;
}

LoopResult CSelftestPart_HotendPid::state_announce_move() {
    if (is_session_over()) {
        return LoopResult::RunNext;
    }

    result.tool = tool_in_turn;
    result.cycle_count = 0;

    // The moves block the selftest loop, so the phase is sent before they start
    IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_moving_to_load);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_move_to_load() {
    if (is_session_over()) {
        return LoopResult::RunNext;
    }

    if (!move_to_load_position()) {
        log_error(Selftest, "Hotend PID: tool %d not brought to the load position, tuning not started", tool_in_turn + 1);
        outcomes[tool_in_turn].failure = TuningFailure::not_at_load_position;
        stop_session(tool_in_turn);
        return LoopResult::RunNext;
    }

    // Without a toolchanger the only tool stays on the head
    park_when_done = prusa_toolchanger.is_toolchanger_enabled();
    log_info(Selftest, "Hotend PID: tool %d at the load position", tool_in_turn + 1);
    return LoopResult::RunNext;
}

bool CSelftestPart_HotendPid::move_to_load_position() {
    const PhysicalToolIndex tool = PhysicalToolIndex::from_raw(tool_in_turn);
    const mapi::ParkingPosition load_position = mapi::get_parking_position(mapi::ParkPosition::load);

    // Lift before the tool change as the filament load does. Z is never homed here: the load
    // position Z only raises, and an unhomed Z is raised by a homing move relative to where it is.
    if (!mapi::park({ .z = load_position.z })) {
        return false;
    }

    // The tool change homes X and Y only if it changes the tool, the move to the load position needs them anyway
    if (!prusa_toolchanger.can_move_safely()
        && !GcodeSuite::G28_no_parser(true, true, false, { .only_if_needed = true, .z_raise = 0, .precise = true })) {
        return false;
    }

    // Parks another tool that is on the head
    if (!prusa_toolchanger.tool_change(tool, tool_return_t::no_return, current_position.xyz(), tool_change_lift_t::no_lift, false)) {
        return false;
    }

    if (!mapi::park(load_position)) {
        return false;
    }

    if (!stdext::holds_value(PhysicalToolIndex::currently_selected(), tool)) {
        return false;
    }
    // Without a toolchanger, the tool is fixed on the head
    return !prusa_toolchanger.is_toolchanger_enabled() || prusa_toolchanger.getTool(tool).is_picked();
}

LoopResult CSelftestPart_HotendPid::state_start_relay() {
    if (is_session_over()) {
        return LoopResult::RunNext;
    }

    const PhysicalToolIndex tool = PhysicalToolIndex::from_raw(tool_in_turn);
    const HeaterConfig_t &heater = config.heaters[tool_in_turn];
    ToolOutcome &outcome = outcomes[tool_in_turn];

    original_pid = heater.get_pid();
    outcome.old_gains = hotend_pid::from_config(original_pid, PID_dT);
    outcome.failure = TuningFailure::none;
    recorder = hotend_pid::RelayOscillationRecorder(config.request.target_temp);
    relay_tool = tool_in_turn;
    relay_active = true;

    CFanCtlCommon &print_fan = heater.print_fan_fnc(tool);
    print_fan.enter_selftest_mode();
    print_fan.selftest_set_pwm(config.request.print_fan_pwm);

    heater.set_pid(relay_pid);
    heater.setTargetTemp(config.request.target_temp);
    relay_start_ms = SelftestInstance().GetTime();

    log_info(Selftest, "Hotend PID: tool %d relay mode on, target %d C, print fan PWM %d",
        tool.display_index(), config.request.target_temp, config.request.print_fan_pwm);

    // Raises the next tool in line
    apply_waiting_targets();

    result.cycle_count = 0;
    IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_heating);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_record() {
    if (is_session_over()) {
        return LoopResult::RunNext;
    }

    const uint32_t now = SelftestInstance().GetTime();
    if (now - relay_start_ms > hotend_pid::RELAY_TIME_LIMIT_MS) {
        log_error(Selftest, "Hotend PID: tool %d oscillation not recorded in time, %d cycles", tool_in_turn + 1, static_cast<int>(recorder.cycle_count()));
        outcomes[tool_in_turn].failure = TuningFailure::timeout;
        return LoopResult::RunNext;
    }

    if (const Hotend::OptionalTemperature temp = config.heaters[tool_in_turn].getTemp(); temp.has_value()) {
        recorder.add_sample(now, temp.value());
    }
    result.cycle_count = static_cast<uint8_t>(recorder.cycle_count());

    if (recorder.has_reached_target()) {
        IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_measuring);
    }
    return recorder.is_complete() ? LoopResult::RunNext : LoopResult::RunCurrent;
}

LoopResult CSelftestPart_HotendPid::state_evaluate() {
    if (is_session_over()) {
        return LoopResult::RunNext;
    }

    end_relay();

    ToolOutcome &outcome = outcomes[tool_in_turn];
    if (outcome.failure == TuningFailure::none) {
        for (const hotend_pid::OscillationCycle &cycle : recorder.recorded_cycles()) {
            log_info(Selftest, "Hotend PID: cycle %u ms, %f..%f C", static_cast<unsigned>(cycle.period_ms),
                static_cast<double>(cycle.min_temp), static_cast<double>(cycle.max_temp));
        }

        const hotend_pid::TuningResult tuning = hotend_pid::compute_gains(recorder.recorded_cycles(), relay_half_output);
        outcome.failure = tuning.failure;
        outcome.new_gains = tuning.gains;
        log_info(Selftest, "Hotend PID: tool %d computed %f %f %f, failure %d", tool_in_turn + 1, static_cast<double>(tuning.gains.kp),
            static_cast<double>(tuning.gains.ki), static_cast<double>(tuning.gains.kd), static_cast<int>(tuning.failure));
    }
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_next_tool() {
    if (is_session_over()) {
        return LoopResult::RunNext;
    }

    // A failed measurement leaves the machine in a known state, so the session goes on
    if (const std::optional<uint8_t> next = hotend_pid::next_tool(session_tools, tool_in_turn)) {
        tool_in_turn = *next;
        apply_waiting_targets();
        return LoopResult::GoToMark0;
    }

    switch_session_heaters_off();
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_announce_park() {
    if (!park_when_done || stopped) {
        return LoopResult::RunNext;
    }

    // The tool change blocks the selftest loop, so the phase is sent before it starts
    result.tool = tool_in_turn;
    IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_parking);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_park() {
    if (!park_when_done || stopped) {
        return LoopResult::RunNext;
    }

    // No extra lift and no Z return: the load position is already high above the bed and any print on it
    if (!prusa_toolchanger.tool_change(NoTool {}, tool_return_t::no_return, current_position.xyz(), tool_change_lift_t::no_lift, false)) {
        log_error(Selftest, "Hotend PID: parking tool %d failed", tool_in_turn + 1);
        stop_session(tool_in_turn);
        return LoopResult::RunNext;
    }

    log_info(Selftest, "Hotend PID: tool %d parked", tool_in_turn + 1);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_show_stop() {
    if (!stopped) {
        return LoopResult::RunNext;
    }

    if (IPartHandler::GetFsmPhase() != PhasesSelftest::HotendPid_session_stopped) {
        result.tool = stopped_tool;
        IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_session_stopped);
        return LoopResult::RunCurrent;
    }
    return state_machine.GetButtonPressed() == Response::Continue ? LoopResult::RunNext : LoopResult::RunCurrent;
}

LoopResult CSelftestPart_HotendPid::state_wait_result() {
    const Response response = state_machine.GetButtonPressed();

    if (session_tools == 0) {
        // The refused request is the only page
        return response == Response::Continue ? LoopResult::RunNext : LoopResult::RunCurrent;
    }

    if (!page_tool.has_value()) {
        page_tool = hotend_pid::first_tool(session_tools);
        show_result_page();
        return LoopResult::RunCurrent;
    }

    if (SelftestInstance().GetTime() - page_shown_ms < page_answer_delay_ms) {
        return LoopResult::RunCurrent;
    }

    const uint8_t tool = *page_tool;
    const ToolOutcome &outcome = outcomes[tool];
    if (outcome.failure == TuningFailure::none) {
        switch (response) {

        case Response::Yes:
            if (!hotend_pid::is_valid(outcome.new_gains)) {
                log_error(Selftest, "Hotend PID: tool %d new gains invalid, not stored", tool + 1);
                break;
            }
            config_store().hotend_pid_gains.set(tool, outcome.new_gains);
            config.heaters[tool].set_pid(hotend_pid::to_config<PID_t>(outcome.new_gains, PID_dT));
            log_info(Selftest, "Hotend PID: tool %d new gains stored", tool + 1);
            passed = true;
            break;

        case Response::No:
            log_info(Selftest, "Hotend PID: tool %d new gains discarded", tool + 1);
            passed = true;
            break;

        default:
            return LoopResult::RunCurrent;
        }
    } else if (response != Response::Continue) {
        return LoopResult::RunCurrent;
    }

    page_tool = hotend_pid::next_tool(session_tools, tool);
    if (!page_tool.has_value()) {
        return LoopResult::RunNext;
    }
    show_result_page();
    return LoopResult::RunCurrent;
}

LoopResult CSelftestPart_HotendPid::state_report_result() {
    return passed ? LoopResult::RunNext : LoopResult::Fail;
}

void CSelftestPart_HotendPid::show_result_page() {
    const uint8_t tool = *page_tool;
    const ToolOutcome &outcome = outcomes[tool];
    result.tool = tool;
    result.failure = outcome.failure;
    result.old_gains = outcome.old_gains;
    result.new_gains = outcome.new_gains;
    page_shown_ms = SelftestInstance().GetTime();
    IPartHandler::SetFsmPhase(outcome.failure == TuningFailure::none ? PhasesSelftest::HotendPid_result_accepted : PhasesSelftest::HotendPid_result_rejected);
}

void CSelftestPart_HotendPid::end_relay() {
    if (!relay_active) {
        return;
    }
    relay_active = false;

    const PhysicalToolIndex tool = PhysicalToolIndex::from_raw(relay_tool);
    const HeaterConfig_t &heater = config.heaters[relay_tool];
    heater.setTargetTemp(0);
    heater.set_pid(original_pid);
    heater.print_fan_fnc(tool).exit_selftest_mode();

    // Out of selftest mode, the picked tool's fan follows the Marlin print fan speed again
    thermalManager.set_print_fan_speed(0);
    thermalManager.apply_print_fan_speed();

    log_info(Selftest, "Hotend PID: tool %d relay mode off, gains restored", tool.display_index());
}

void CSelftestPart_HotendPid::apply_waiting_targets() {
    const hotend_pid::TurnStage stage = relay_active ? hotend_pid::TurnStage::measuring : hotend_pid::TurnStage::moving_to_front;
    for (const PhysicalToolIndex tool : PhysicalToolIndex::all()) {
        const uint8_t raw_tool = tool.to_raw();
        if (!hotend_pid::contains(session_tools, raw_tool) || raw_tool == tool_in_turn) {
            continue;
        }
        const int16_t target = hotend_pid::waiting_target(session_tools, tool_in_turn, stage, raw_tool, config.request.target_temp);
        config.heaters[raw_tool].setTargetTemp(target);
        log_info(Selftest, "Hotend PID: tool %d target %d C while tool %d has its turn", tool.display_index(), target, tool_in_turn + 1);
    }
}

void CSelftestPart_HotendPid::switch_session_heaters_off() {
    end_relay();

    for (const PhysicalToolIndex tool : PhysicalToolIndex::all()) {
        if (hotend_pid::contains(session_tools, tool.to_raw())) {
            config.heaters[tool.to_raw()].setTargetTemp(0);
        }
    }
}

void CSelftestPart_HotendPid::stop_session(uint8_t tool) {
    switch_session_heaters_off();
    stopped = true;
    stopped_tool = tool;
    log_error(Selftest, "Hotend PID: session stopped at tool %d, heaters of the session off", tool + 1);
}
