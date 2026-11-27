#pragma once

#include <option/has_nozzle_cleaner_lite.h>
#include <option/has_custom_nozzle_wiper.h>

#include <gcode/inject_queue_actions.hpp>
#include <optional>
#include <string_view>
#include <str_utils.hpp>

#include <core/types.h>
#include <mapi/parking.hpp>
#include <utils/badge.hpp>

#if HAS_CUSTOM_NOZZLE_WIPER()
    #include <feature/nozzle_cleaner_lite/custom_wiper_geometry.hpp>
    #include <tool_index.hpp>
    #include <array>
    #include <expected>
    #include <span>
#endif

class unified_bed_leveling;

namespace nozzle_cleaner_lite {

/// True when the running unit actually has a nozzle cleaner lite version enabled.
bool is_available();

#if HAS_CUSTOM_NOZZLE_WIPER()
/// True when clean() uses the taught custom wiper instead of Prusa's wiper.
/// The custom wiper is never probed, cooled down on or reconfigured by
/// CleanArgs: it heats or cools to its own touch range, wipes, and restores
/// the nozzle target and print fan.
bool uses_custom_geometry();

/// Plans a custom wipe of \p geometry with \p width for the tool picked now,
/// with the offset applied to it: what a wipe started now would do.
/// Only that tool counts: another tool that cannot reach is refused when it wipes.
std::expected<custom_wiper::ToolPlan, custom_wiper::Problem> plan_custom_wipe(const custom_wiper::Geometry &geometry, float width);

/// How each enabled tool would wipe, each planned with its stored offset
struct ReachReport {
    struct Entry {
        /// 0-based
        uint8_t tool = 0;
        std::expected<custom_wiper::ToolPlan, custom_wiper::Problem> plan;
    };

    std::array<Entry, PhysicalToolIndex::count> entries;
    size_t count = 0;

    std::span<const Entry> tools() const { return { entries.data(), count }; }
};

ReachReport custom_reach_report(const custom_wiper::Geometry &geometry, float width);

/// True when the picked tool can wipe the stored custom wiper geometry, see plan_custom_wipe()
bool custom_geometry_valid();

/// Machine position where the custom wipe waits for temperature with the
/// picked tool: the plan's waiting point at the approach height. nullopt while
/// the picked tool cannot wipe the stored geometry.
std::optional<xyz_pos_t> custom_waiting_point();

/// Wipes on the custom wiper before the bed-tap nozzle check, if that is
/// switched on and the picked tool can wipe it, then waits for the restored
/// nozzle target. Does nothing otherwise.
/// \returns false if a wipe was attempted and failed
bool wipe_before_probing();

/// Soaks the nozzle beside and well above the custom wiper at the loaded
/// filament's temperature (else the current target) for \p hold_s, cools it
/// to the wiper's touch range and wipes once. A skip request ends the soak
/// early, with the cool-down and the wipe still following. If anything else
/// changes the nozzle target before the wipe, the deep clean ends without
/// wiping and keeps that target. Refuses without a temperature to soak at.
bool deep_clean(uint16_t hold_s);
#else
constexpr bool uses_custom_geometry() { return false; }
#endif

/// Touchpoint cool-down temperature sits this much below the cleaning
/// temperature: no active ooze, yet as hot as possible so the nozzle
/// thermal expansion stays close to printing conditions. The probing tool
/// keeps it for the probing that follows.
constexpr int16_t cooldown_temp_diff = 20;
struct CleanArgs {
    int16_t cleaning_temp;
    // target to cool down to, defaulting to
    // cleaning_temp - cooldown_temp_diff. Must be <= cleaning_temp.
    std::optional<int16_t> probe_temp;
    bool cooldown;
    bool keep_target;
    /// Custom wiper only: heater off and only the slow strokes, for checking a taught position
    bool dry_run = false;
};

/// Home if needed, probe the cleaner's touchpoint reference point, run the
/// clean cycle and rest on the touchpoint until the nozzle cools down.
/// With the custom wiper, see uses_custom_geometry().
/// Caller must check is_available() first.
bool clean(CleanArgs args);

} // namespace nozzle_cleaner_lite
