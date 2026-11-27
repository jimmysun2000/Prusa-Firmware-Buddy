#pragma once

#include <cstdint>

#include "i_selftest_part.hpp"
#include "selftest_data.hpp"
#include "selftest_heater_config.hpp"
#include "selftest_hotend_pid_type.hpp"
#include <hotend_pid_tuning.hpp>

namespace selftest {

struct HotendPidConfig_t {
    using type_evaluation = SelftestHotendPid_t;
    static constexpr SelftestParts part_type = SelftestParts::HotendPid;

    /// Nozzle heater of the requested tool; nullptr refuses the request
    const HeaterConfig_t *heater = nullptr;

    HotendPidTuningData request { .tool = PhysicalToolIndex::from_raw(0), .target_temp = 0, .print_fan_pwm = 0 };
};

/// Relay autotuning of the hotend PID of one tool, see hotend_pid_tuning.hpp.
/// The tool is picked and moved to the filament load position first; the heater is used only
/// once it is there. The heater is driven by sending gains that make the regulator an on/off
/// relay, as the heater selftest does. The computed gains are stored only when the user confirms
/// them. After the user answers the result, the tool is parked in its dock.
/// An abort keeps the stock behaviour: the motors are switched off and nothing moves any more.
class CSelftestPart_HotendPid {
public:
    CSelftestPart_HotendPid(IPartHandler &state_machine, const HotendPidConfig_t &config, SelftestHotendPid_t &result);

    /// Ends the relay mode if it is still on, also when the part is aborted
    ~CSelftestPart_HotendPid();

    LoopResult state_check_request();
    LoopResult state_wait_confirm();
    LoopResult state_announce_move();
    LoopResult state_move_to_load();
    LoopResult state_start_relay();
    LoopResult state_record();
    LoopResult state_evaluate();
    LoopResult state_wait_result();
    LoopResult state_announce_park();
    LoopResult state_park();
    LoopResult state_report_result();

private:
    /// Picks the tool and moves it to the filament load position, as the filament load does
    /// @returns whether the tool is picked and at the load position
    bool move_to_load_position();

    /// Switches the nozzle off, restores the gains from before the run and turns the print fan off
    void end_relay();

    IPartHandler &state_machine;
    const HotendPidConfig_t &config;
    SelftestHotendPid_t &result;

    hotend_pid::RelayOscillationRecorder recorder;
    PID_t original_pid {};
    uint32_t relay_start_ms = 0;
    bool relay_active = false;

    /// The tool reached the load position and goes back to its dock at the end
    bool park_when_done = false;

    /// The user answered a valid result
    bool passed = false;
};

} // namespace selftest
