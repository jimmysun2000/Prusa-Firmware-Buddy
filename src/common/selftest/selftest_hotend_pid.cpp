#include "selftest_hotend_pid.hpp"

#include "i_selftest.hpp"
#include <Marlin/src/module/temperature.h>
#include <config_store/store_instance.hpp>
#include <logging/log.hpp>

LOG_COMPONENT_REF(Selftest);

using namespace selftest;
using hotend_pid::TuningFailure;

namespace {

/// Gains that make the regulator switch the heater fully on below the target and fully off above it
constexpr PID_t relay_pid { .Kp = 1000000, .Ki = 0, .Kd = 0 };

// The Dwarf regulator, like this board's, limits its output to 0..PID_MAX
static_assert(PID_MAX == 255);
constexpr float relay_half_output = PID_MAX / 2.0f;

bool is_request_allowed(const HotendPidConfig_t &config) {
    const HotendPidTuningData &request = config.request;
    if (!config.heater || config.heater->tool_nr != request.tool || !request.tool.is_enabled()) {
        return false;
    }

    const Hotend::Config &hotend_config = Hotend::for_tool(request.tool).config();
    return request.target_temp >= EXTRUDE_MINTEMP
        && request.target_temp <= hotend_config.max_nozzle_temp - HEATER_MAXTEMP_SAFETY_MARGIN;
}

} // namespace

CSelftestPart_HotendPid::CSelftestPart_HotendPid(IPartHandler &state_machine, const HotendPidConfig_t &config, SelftestHotendPid_t &result)
    : state_machine(state_machine)
    , config(config)
    , result(result)
    , recorder(config.request.target_temp) {}

CSelftestPart_HotendPid::~CSelftestPart_HotendPid() {
    end_relay();
}

LoopResult CSelftestPart_HotendPid::state_check_request() {
    const HotendPidTuningData &request = config.request;
    result.tool = request.tool.to_raw();
    result.target_temp = request.target_temp;
    result.print_fan_pwm = request.print_fan_pwm;

    if (!is_request_allowed(config)) {
        log_error(Selftest, "Hotend PID: tuning tool %d at %d C refused", request.tool.display_index(), request.target_temp);
        result.failure = TuningFailure::invalid_request;
        IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_result_rejected);
        return LoopResult::RunNext;
    }

    result.old_gains = hotend_pid::from_config(config.heater->get_pid(), PID_dT);
    IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_confirm_start);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_wait_confirm() {
    if (result.failure != TuningFailure::none) {
        return LoopResult::RunNext;
    }
    if (state_machine.GetButtonPressed() != Response::Continue) {
        return LoopResult::RunCurrent;
    }
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_start_relay() {
    if (result.failure != TuningFailure::none) {
        return LoopResult::RunNext;
    }

    const HeaterConfig_t &heater = *config.heater;
    original_pid = heater.get_pid();
    relay_active = true;

    CFanCtlCommon &print_fan = heater.print_fan_fnc(config.request.tool);
    print_fan.enter_selftest_mode();
    print_fan.selftest_set_pwm(config.request.print_fan_pwm);

    heater.set_pid(relay_pid);
    heater.setTargetTemp(config.request.target_temp);
    relay_start_ms = SelftestInstance().GetTime();

    log_info(Selftest, "Hotend PID: tool %d relay mode on, target %d C, print fan PWM %d",
        config.request.tool.display_index(), config.request.target_temp, config.request.print_fan_pwm);
    IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_heating);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_record() {
    if (result.failure != TuningFailure::none) {
        return LoopResult::RunNext;
    }

    const uint32_t now = SelftestInstance().GetTime();
    if (now - relay_start_ms > hotend_pid::RELAY_TIME_LIMIT_MS) {
        log_error(Selftest, "Hotend PID: oscillation not recorded in time, %d cycles", static_cast<int>(recorder.cycle_count()));
        result.failure = TuningFailure::timeout;
        return LoopResult::RunNext;
    }

    if (const Hotend::OptionalTemperature temp = config.heater->getTemp(); temp.has_value()) {
        recorder.add_sample(now, temp.value());
    }
    result.cycle_count = static_cast<uint8_t>(recorder.cycle_count());

    if (recorder.has_reached_target()) {
        IPartHandler::SetFsmPhase(PhasesSelftest::HotendPid_measuring);
    }
    return recorder.is_complete() ? LoopResult::RunNext : LoopResult::RunCurrent;
}

LoopResult CSelftestPart_HotendPid::state_evaluate() {
    end_relay();

    if (result.failure == TuningFailure::none) {
        for (const hotend_pid::OscillationCycle &cycle : recorder.recorded_cycles()) {
            log_info(Selftest, "Hotend PID: cycle %u ms, %f..%f C", static_cast<unsigned>(cycle.period_ms),
                static_cast<double>(cycle.min_temp), static_cast<double>(cycle.max_temp));
        }

        const hotend_pid::TuningResult tuning = hotend_pid::compute_gains(recorder.recorded_cycles(), relay_half_output);
        result.failure = tuning.failure;
        result.new_gains = tuning.gains;
        log_info(Selftest, "Hotend PID: computed %f %f %f, failure %d", static_cast<double>(tuning.gains.kp),
            static_cast<double>(tuning.gains.ki), static_cast<double>(tuning.gains.kd), static_cast<int>(tuning.failure));
    }

    IPartHandler::SetFsmPhase(result.failure == TuningFailure::none ? PhasesSelftest::HotendPid_result_accepted : PhasesSelftest::HotendPid_result_rejected);
    return LoopResult::RunNext;
}

LoopResult CSelftestPart_HotendPid::state_wait_result() {
    const Response response = state_machine.GetButtonPressed();

    if (result.failure != TuningFailure::none) {
        return response == Response::Continue ? LoopResult::Fail : LoopResult::RunCurrent;
    }

    switch (response) {

    case Response::Yes:
        if (!hotend_pid::is_valid(result.new_gains)) {
            return LoopResult::Fail;
        }
        config_store().hotend_pid_gains.set(result.tool, result.new_gains);
        config.heater->set_pid(hotend_pid::to_config<PID_t>(result.new_gains, PID_dT));
        log_info(Selftest, "Hotend PID: tool %d new gains stored", config.request.tool.display_index());
        return LoopResult::RunNext;

    case Response::No:
        log_info(Selftest, "Hotend PID: tool %d new gains discarded", config.request.tool.display_index());
        return LoopResult::RunNext;

    default:
        return LoopResult::RunCurrent;
    }
}

void CSelftestPart_HotendPid::end_relay() {
    if (!relay_active) {
        return;
    }
    relay_active = false;

    const HeaterConfig_t &heater = *config.heater;
    heater.setTargetTemp(0);
    heater.set_pid(original_pid);
    heater.print_fan_fnc(config.request.tool).exit_selftest_mode();

    // Out of selftest mode, the picked tool's fan follows the Marlin print fan speed again
    thermalManager.set_print_fan_speed(0);
    thermalManager.apply_print_fan_speed();

    log_info(Selftest, "Hotend PID: tool %d relay mode off, gains restored", config.request.tool.display_index());
}
