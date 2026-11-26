#pragma once

/// Feeds the per-tool print statistics of Odometer_s from the GUI thread.
///
/// Persisted counters are fed only in the Printing state, never while paused or
/// finished, because the print screen flushes changed counters on every GUI loop
/// in those states. Saving is not done here.
namespace print_stats_tracker {

/// Call on every GUI loop iteration, also when the print screen is not open.
/// Does its work at most once per second:
/// - adds the print duration elapsed between two samples that are both in the
///   Printing state to the currently picked tool (nothing when no tool is picked),
/// - counts a tool recovery on entering the tool crash recovery pickup state.
void gui_loop();

} // namespace print_stats_tracker
