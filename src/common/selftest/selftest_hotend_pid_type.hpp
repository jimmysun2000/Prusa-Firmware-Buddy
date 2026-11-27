#pragma once

#include <cstdint>

#include <hotend_pid_tuning.hpp>
#include <marlin_server_extended_fsm_types.hpp>

/// State of the hotend PID tuning, sent to the GUI as FSM extended data
struct SelftestHotendPid_t : public FSMExtendedData {
    uint8_t tool = 0; ///< Raw physical tool index
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
