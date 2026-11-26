#pragma once

#include <tool_index.hpp>
#include <i18n.h>

#include <array>
#include <cstddef>
#include <cstdint>

/// Owner-defined maintenance reminders. Light header, the config store sizes its items from it.
namespace maintenance_reminders {

enum class Metric : uint8_t {
    print_hours, ///< Printing time; counted in s, set in h
    filament_m, ///< Extruded filament; counted in mm, set in m
    tool_changes, ///< Tool picks; counted and set in picks
};

enum class Scope : uint8_t {
    printer, ///< One counter for the whole printer
    per_tool, ///< One counter for each physical tool
};

struct Item {
    /// Translatable name
    const char *name;
    Metric metric;
    Scope scope;
};

/// The row index identifies the stored interval and the row order the stored baselines:
/// append rows only, never reorder or remove them or change their scope.
/// Intervals default to 0 (Off), so a new row never shows a reminder by itself.
inline constexpr std::array items {
    Item { N_("Belt check"), Metric::print_hours, Scope::printer },
    Item { N_("Lubrication"), Metric::print_hours, Scope::printer },
    Item { N_("Dock check"), Metric::tool_changes, Scope::printer },
    Item { N_("Nozzle wear"), Metric::filament_m, Scope::per_tool },
};

inline constexpr size_t item_count = items.size();

constexpr size_t slot_span(Scope scope) {
    return (scope == Scope::per_tool) ? PhysicalToolIndex::count : 1;
}

/// @returns index of the first stored baseline of row @p item; for item_count, the number of baselines
constexpr size_t first_slot(size_t item) {
    size_t result = 0;
    for (size_t i = 0; i < item; ++i) {
        result += slot_span(items[i].scope);
    }
    return result;
}

inline constexpr size_t slot_count = first_slot(item_count);

// Limits of the config store items (their reserved id ranges)
static_assert(item_count <= 16);
static_assert(slot_count <= 32);

} // namespace maintenance_reminders
