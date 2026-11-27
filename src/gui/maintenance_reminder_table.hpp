#pragma once

#include <tool_index.hpp>
#include <i18n.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

/// Owner-defined maintenance reminders. Light header, the config store sizes its items from it.
namespace maintenance_reminders {

enum class Metric : uint8_t {
    print_hours, ///< Printing time; counted in s, set in h
    filament_m, ///< Extruded filament; counted in mm, set in m
    tool_changes, ///< Tool picks; counted and set in picks
    months, ///< Calendar time; counted in s of the real-time clock, set in months
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

    /// Interval the row gets once, at the first start of a firmware that has the row; 0 = stays Off.
    /// Printer rows only.
    uint32_t default_interval;
};

/// The row index identifies the stored interval and the row order the stored baselines:
/// append rows only, never reorder or remove them or change their scope.
/// A default interval starts counting when it is applied, so a new row is never due by itself.
/// The defaults of 300 h, 500 h and 700 h are the intervals of Prusa's XL maintenance article;
/// the general inspection covers the belts, the fans and the tool changer.
/// The article asks for the linear rails every 3 to 6 months; 6 is the owner's choice.
inline constexpr std::array items {
    Item { N_("General inspection"), Metric::print_hours, Scope::printer, 300 },
    Item { N_("Z-axis lubrication"), Metric::print_hours, Scope::printer, 500 },
    Item { N_("Dock check"), Metric::tool_changes, Scope::printer, 0 },
    Item { N_("Nozzle wear"), Metric::filament_m, Scope::per_tool, 0 },
    Item { N_("Electronics check"), Metric::print_hours, Scope::printer, 700 },
    Item { N_("Rail lubrication"), Metric::months, Scope::printer, 6 },
};

inline constexpr size_t item_count = items.size();

static_assert(std::ranges::all_of(items, [](const Item &item) { return item.scope == Scope::printer || item.default_interval == 0; }));

/// The only per-tool row; its interval is stored per tool, apart from the row intervals
inline constexpr uint8_t nozzle_wear_item = 3;

static_assert(items[nozzle_wear_item].scope == Scope::per_tool);
static_assert(std::ranges::count(items, Scope::per_tool, &Item::scope) == 1);

/// Nozzle fitted on a tool, as the user picked it for the nozzle wear reminder.
/// Only the reminder uses it; it is unrelated to the nozzle settings of the tool.
/// Stored: append values only, never reorder or remove them.
enum class NozzleType : uint8_t {
    not_set,
    brass,
    brass_high_flow,
    stainless_steel,
    hardened_steel,
    obxidian,
    obxidian_high_flow,
    tungsten_carbide,
    diamond_tipped,
    ruby_tipped,
    custom, ///< No preset: the tool keeps its interval
};

struct NozzleTypeInfo {
    NozzleType type;

    /// Translatable name
    const char *name;

    /// Nozzle wear interval the type fills in [m]; 0 = none
    uint32_t preset;
};

/// Every type the user can pick, in the stored order.
/// The presets are the owner's rough estimates, not Prusa's figures and not measured.
inline constexpr std::array nozzle_types {
    NozzleTypeInfo { NozzleType::brass, N_("Brass"), 5'000 },
    NozzleTypeInfo { NozzleType::brass_high_flow, N_("Brass high-flow"), 5'000 },
    NozzleTypeInfo { NozzleType::stainless_steel, N_("Stainless steel"), 8'000 },
    NozzleTypeInfo { NozzleType::hardened_steel, N_("Hardened steel"), 30'000 },
    NozzleTypeInfo { NozzleType::obxidian, N_("ObXidian"), 30'000 },
    NozzleTypeInfo { NozzleType::obxidian_high_flow, N_("ObXidian high-flow"), 30'000 },
    NozzleTypeInfo { NozzleType::tungsten_carbide, N_("Tungsten carbide"), 100'000 },
    NozzleTypeInfo { NozzleType::diamond_tipped, N_("Diamond-tipped"), 100'000 },
    NozzleTypeInfo { NozzleType::ruby_tipped, N_("Ruby-tipped"), 100'000 },
    NozzleTypeInfo { NozzleType::custom, N_("Custom"), 0 },
};

static_assert([] {
    for (size_t i = 0; i < nozzle_types.size(); ++i) {
        if (nozzle_types[i].type != static_cast<NozzleType>(i + 1)) {
            return false;
        }
    }
    return true;
}());

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
