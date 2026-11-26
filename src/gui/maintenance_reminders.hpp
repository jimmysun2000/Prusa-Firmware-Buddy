#pragma once

#include "maintenance_reminder_table.hpp"

#include <optional>

class StringBuilder;

/// Maintenance reminders: a counter of the odometer minus its value at the last reset,
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

/// @returns the interval of row @p item in its setting unit (h, m, picks), 0 = Off
uint32_t interval(uint8_t item);

/// Stores the interval of row @p item. Turning a row from Off on starts its counting
/// from the current counters. Writes the config store if the value changed.
void set_interval(uint8_t item, uint32_t interval);

/// @returns the usage since the last reset, in the setting unit (h, m, picks)
uint32_t usage(const Slot &slot);

/// @returns whether the usage reached the interval; never when the row is Off
bool is_due(const Slot &slot);

/// Starts the counting of @p slot anew from the current counter. Writes the config store.
void reset(const Slot &slot);

/// Appends the name, e.g. "Nozzle wear T2"
void format_name(StringBuilder &sb, const Slot &slot);

/// Appends the usage against the interval, e.g. "812/2000 m", or "Off"
void format_progress(StringBuilder &sb, const Slot &slot);

/// Shows a blocking message box for each due reminder that was not postponed.
/// DONE resets the counter, NOT NOW postpones it (RAM only) until the usage grows
/// by another 10 % of the interval. A box closes without an answer when a print starts.
/// Does nothing unless the printer is idle and no dialog is open.
/// @returns true if the check was done
bool prompt_due_items();

} // namespace maintenance_reminders
