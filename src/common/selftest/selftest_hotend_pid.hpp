#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "i_selftest_part.hpp"
#include "selftest_data.hpp"
#include "selftest_heater_config.hpp"
#include "selftest_hotend_pid_type.hpp"
#include <hotend_pid_session.hpp>
#include <hotend_pid_tuning.hpp>

namespace selftest {

struct HotendPidConfig_t {
    using type_evaluation = SelftestHotendPid_t;
    static constexpr SelftestParts part_type = SelftestParts::HotendPid;

    /// Nozzle heaters of all tools, indexed by the raw tool index; empty refuses the request
    std::span<const HeaterConfig_t> heaters {};

    HotendPidTuningData request { .tool_mask = 0, .target_temp = 0, .print_fan_pwm = 0 };
};

/// Relay autotuning of the hotend PID, see hotend_pid_tuning.hpp, as a session that tunes the
/// chosen tools one after another in ascending order with one target temperature and fan speed.
/// Each tool in turn is picked and moved to the filament load position first; its heater is used
/// only once it is there. The heater is driven by gains that make the regulator an on/off relay,
/// as the heater selftest does, and gets its own gains back as soon as it is measured.
/// After the last tool, all heaters of the session are off and the tool is parked; then the
/// result of each tool is shown on a page of its own. Computed gains are stored only when the
/// user confirms them on their tool's page.
/// A failed measurement does not stop the session. A failed pick, park or move does: all heaters
/// of the session go off at once, nothing moves any more and the results so far are shown.
/// An abort keeps the stock behaviour: the motors are switched off and nothing moves any more.
/// However the part ends, all heaters of the session are off and the gains are restored.
class CSelftestPart_HotendPid {
public:
    CSelftestPart_HotendPid(IPartHandler &state_machine, const HotendPidConfig_t &config, SelftestHotendPid_t &result);

    /// Switches all heaters of the session off and restores the gains, also when the part is aborted
    ~CSelftestPart_HotendPid();

    LoopResult state_check_request();
    LoopResult state_wait_confirm();
    LoopResult state_mark_tool();
    LoopResult state_announce_move();
    LoopResult state_move_to_load();
    LoopResult state_start_relay();
    LoopResult state_record();
    LoopResult state_evaluate();
    LoopResult state_next_tool();
    LoopResult state_announce_park();
    LoopResult state_park();
    LoopResult state_show_stop();
    LoopResult state_wait_result();
    LoopResult state_report_result();

private:
    /// What the session found out about one tool, kept until its result page is answered
    struct ToolOutcome {
        hotend_pid::TuningFailure failure = hotend_pid::TuningFailure::not_run;

        /// Gains the tool had when its relay started
        HotendPIDGains old_gains {};

        /// Gains computed for the tool; also set when they are out of limits
        HotendPIDGains new_gains {};
    };

    /// @returns whether the request was refused or a pick, park or move failed
    bool is_session_over() const { return session_tools == 0 || stopped; }

    /// Picks the tool in turn and moves it to the filament load position, as the filament load does
    /// @returns whether the tool is picked and at the load position
    bool move_to_load_position();

    /// Switches the nozzle in relay mode off, restores its gains from before the relay and turns
    /// the print fan off
    void end_relay();

    /// Ends the relay mode and sets the nozzle target of every tool of the session to 0
    void switch_session_heaters_off();

    /// Stops the session after a failed pick, park or move of @p tool: all heaters of the session
    /// go off and no state moves the machine any more
    void stop_session(uint8_t tool);

    /// Sends the result page of page_tool
    void show_result_page();

    IPartHandler &state_machine;
    const HotendPidConfig_t &config;
    SelftestHotendPid_t &result;

    /// Tools of the session; empty until the request is accepted and when it is refused
    hotend_pid::ToolSet session_tools = 0;

    /// Raw index of the tool being tuned
    uint8_t tool_in_turn = 0;

    std::array<ToolOutcome, PhysicalToolIndex::count> outcomes {};

    hotend_pid::RelayOscillationRecorder recorder;
    PID_t original_pid {};
    uint32_t relay_start_ms = 0;
    bool relay_active = false;

    /// Raw index of the tool whose heater is or was last in relay mode
    uint8_t relay_tool = 0;

    /// A tool reached the load position, the one on the head goes back to its dock at the end
    bool park_when_done = false;

    /// A pick, park or move failed
    bool stopped = false;

    /// Raw index of the tool whose pick, park or move failed
    uint8_t stopped_tool = 0;

    /// Raw index of the tool whose result page is shown
    std::optional<uint8_t> page_tool;

    uint32_t page_shown_ms = 0;

    /// The user answered a valid result
    bool passed = false;
};

} // namespace selftest
