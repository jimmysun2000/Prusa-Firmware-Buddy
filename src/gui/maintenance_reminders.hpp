#pragma once

#include "maintenance_reminder_table.hpp"

#include <optional>

class StringBuilder;

/// Maintenance reminders: a counter of the odometer (or of the clock) minus its value at the last reset,
/// against a user-set interval per table row. Information only, nothing is controlled.
/// GUI thread only.
namespace maintenance_reminders {

/// One counter of a reminder: the whole printer, or one tool of a per-tool reminder
struct Slot {
    /// Row of the reminder table
    uint8_t item;

    /// Set for per-tool reminders
    std::optional<PhysicalToolIndex> tool;

    /// @returns index of the stored baseline
    size_t index() const;
};

/// @returns the number of slots shown to the user: per-tool slots only for enabled tools
size_t visible_slot_count();

/// @returns the @p n-th visible slot, in table order
Slot visible_slot(size_t n);

/// @returns the translatable setting unit of @p metric
const char *unit_name(Metric metric);

/// @returns the interval of @p slot in its setting unit (h, m, picks, months), 0 = Off.
/// Nozzle wear has one per tool, the other rows one per row.
uint32_t interval(const Slot &slot);

/// Stores the interval of @p slot. Turning it from Off on starts its counting
/// from its current counter; for a calendar row while the clock is unknown, from the first
/// known date (prompt_due_items). Writes the config store if the value changed.
void set_interval(const Slot &slot, uint32_t interval);

/// Gives every printer row with a default interval that default, once per row: when a firmware
/// that has the row calls this for the first time, or after the settings were reset.
/// Only rows that are Off then get it, their counting starts from the current counters.
/// A row turned Off later stays Off; a row the user set keeps its interval and counting.
/// A nozzle wear interval an older firmware stored for all tools becomes the interval of each
/// tool without one, keeping their counting.
/// Writes the config store if anything changed.
void apply_defaults();

/// @returns the nozzle type of @p tool for its nozzle wear reminder: Custom for an unknown stored
/// value or for an interval set without a type, not_set if neither a type nor an interval is set
NozzleType nozzle_type(PhysicalToolIndex tool);

/// Stores the nozzle type of @p tool for its nozzle wear reminder. The preset of the type, if it
/// has one, becomes the tool's interval: turning it from Off on starts the counting.
/// @p reset_counter starts the counting anew in any case. Writes the config store.
void set_nozzle_type(PhysicalToolIndex tool, NozzleType type, bool reset_counter);

/// @returns the usage since the last reset, in the setting unit (h, m, picks, months);
/// 0 for a calendar row while the clock is unknown or its counting has not started
uint32_t usage(const Slot &slot);

/// @returns whether the usage reached the interval; never when the slot is Off,
/// nor for a calendar row while the clock is unknown or its counting has not started
bool is_due(const Slot &slot);

/// Starts the counting of @p slot anew from the current counter; for a calendar row while the
/// clock is unknown, from the first known date (prompt_due_items). Writes the config store.
void reset(const Slot &slot);

/// Appends the name, e.g. "Nozzle wear T2"
void format_name(StringBuilder &sb, const Slot &slot);

/// Appends the usage against the interval, e.g. "812/2000 m", or "Off";
/// "no clock" for a calendar row while the clock is unknown
void format_progress(StringBuilder &sb, const Slot &slot);

/// Applies the default intervals (apply_defaults), then shows a blocking message box for each due
/// reminder that was not postponed. DONE resets the counter, NOT NOW postpones it (RAM only)
/// until the usage grows by another 10 % of the interval. A box closes without an answer when a
/// print starts. Does nothing unless the printer is idle and no dialog is open.
/// A calendar row waiting for a known date starts its counting here once the clock is known.
/// @returns true if the check was done
bool prompt_due_items();

} // namespace maintenance_reminders
