#include "print_stats_tracker.hpp"

#include <common/marlin_vars.hpp>
#include <odometer.hpp>
#include <timing.h>
#include <tool_index.hpp>
#include <utils/timing/rate_limiter.hpp>
#include <option/has_tool_crash_recovery.h>
#include <option/has_tool_mapping.h>

#include <optional>

namespace print_stats_tracker {

namespace {
    using marlin_server::State;

    RateLimiter<uint32_t> update_limiter(1000);

    /// Print duration [s] at the previous update, if that update was in the Printing state
    std::optional<uint32_t> last_printing_duration;

#if HAS_TOOL_CRASH_RECOVERY()
    bool was_in_tool_recovery = false;
#endif

    bool was_in_print = false;

    /// From confirming the print in the preview until it is finished or stopped.
    /// Aborting a preview and leaving the print screen do not belong to a print.
    bool is_in_print(State state) {
        switch (state) {
        case State::Idle:
        case State::PrintPreviewInit:
        case State::PrintPreviewImage:
#if HAS_TOOL_MAPPING()
        case State::PrintPreviewToolsMapping:
#endif
        case State::Aborting_Preview:
        case State::Aborted:
        case State::Finished:
        case State::Exit:
            return false;
        default:
            return true;
        }
    }

    /// States a print passes through when it starts from the beginning
    bool is_print_start_state(State state) {
        switch (state) {
        case State::PrintPreviewConfirmed:
        case State::PrintPreviewQuestions:
        case State::PrintInit:
        case State::SerialPrintInit:
        case State::Printing:
            return true;
        default:
            return false;
        }
    }
} // namespace

void gui_loop() {
    if (!update_limiter.check(ticks_ms())) {
        return;
    }

    const auto state = marlin_vars().print_state.get();

    if (state == State::Printing) {
        const uint32_t duration = marlin_vars().print_duration.get();
        if (last_printing_duration && duration > *last_printing_duration) {
            if (const auto tool = PhysicalToolIndex::currently_selected_opt()) {
                Odometer_s::instance().add_tool_time(*tool, duration - *last_printing_duration);
            }
        }
        last_printing_duration = duration;
    } else {
        // Time across a pause, recovery or the end of a print is not printing time
        last_printing_duration = std::nullopt;
    }

#if HAS_TOOL_CRASH_RECOVERY()
    const bool in_tool_recovery = (state == State::CrashRecovery_Tool_Pickup);
    if (in_tool_recovery && !was_in_tool_recovery) {
        Odometer_s::instance().add_tool_recovery();
    }
    was_in_tool_recovery = in_tool_recovery;
#endif

    const bool in_print = is_in_print(state);
    if (in_print && !was_in_print) {
        // A print first seen in any other state was already running (power-loss resume)
        Odometer_s::instance().begin_print_trip(is_print_start_state(state) ? Odometer_s::PrintTripState::whole_print : Odometer_s::PrintTripState::since_resume);
    } else if (!in_print && was_in_print) {
        Odometer_s::instance().end_print_trip();
    }
    was_in_print = in_print;
}

} // namespace print_stats_tracker
