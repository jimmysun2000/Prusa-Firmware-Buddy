#pragma once

#include <cstdint>

#include <hotend_pid_session.hpp>
#include <hotend_pid_tuning.hpp>
#include <marlin_server_extended_fsm_types.hpp>

/// State of the hotend PID tuning session, sent to the GUI as FSM extended data.
/// The data is limited to a few dozen bytes, so it describes one tool at a time.
struct SelftestHotendPid_t : public FSMExtendedData {
    /// Raw physical tool index of the tool in turn, of the tool whose result is shown, or of the
    /// tool that stopped the session
    uint8_t tool = 0;

    hotend_pid::ToolSet tool_mask = 0; ///< Tools of the session

    uint8_t print_fan_pwm = 0;
    int16_t target_temp = 0; ///< [°C]

    /// Complete oscillation cycles so far, settling ones included
    uint8_t cycle_count = 0;

    hotend_pid::TuningFailure failure = hotend_pid::TuningFailure::none;

    /// Gains the tool had before the run
    HotendPIDGains old_gains {};

    /// Gains computed by the run; also shown when they are out of limits
    HotendPIDGains new_gains {};

    SelftestHotendPid_t() = default;

    bool operator==(const SelftestHotendPid_t &) const = default;

    void Pass() {}
    void Fail() {}
    void Abort() {}
};
