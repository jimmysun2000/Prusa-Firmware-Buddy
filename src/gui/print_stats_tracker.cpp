#include "print_stats_tracker.hpp"

#include <common/marlin_vars.hpp>
#include <odometer.hpp>
#include <timing.h>
#include <tool_index.hpp>
#include <utils/timing/rate_limiter.hpp>
#include <option/has_tool_crash_recovery.h>

#include <optional>

namespace print_stats_tracker {

namespace {
    RateLimiter<uint32_t> update_limiter(1000);

    /// Print duration [s] at the previous update, if that update was in the Printing state
    std::optional<uint32_t> last_printing_duration;

#if HAS_TOOL_CRASH_RECOVERY()
    bool was_in_tool_recovery = false;
#endif
} // namespace

void gui_loop() {
    if (!update_limiter.check(ticks_ms())) {
        return;
    }

    const auto state = marlin_vars().print_state.get();

    if (state == marlin_server::State::Printing) {
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
    const bool in_tool_recovery = (state == marlin_server::State::CrashRecovery_Tool_Pickup);
    if (in_tool_recovery && !was_in_tool_recovery) {
        Odometer_s::instance().add_tool_recovery();
    }
    was_in_tool_recovery = in_tool_recovery;
#endif
}

} // namespace print_stats_tracker
