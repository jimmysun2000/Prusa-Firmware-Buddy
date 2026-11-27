#pragma once

#include <bit>
#include <cstdint>
#include <optional>

/// Tool sets of a hotend PID tuning session and the nozzle targets of the tools waiting in it.
/// A session tunes the chosen tools one after another in ascending order; the one being tuned is
/// the tool in turn, the chosen tools after it are waiting and the ones before it are finished.
namespace hotend_pid {

/// Chosen tools, bit n set for the tool with raw index n
using ToolSet = uint8_t;

/// Number of tools a ToolSet can hold
inline constexpr uint8_t TOOL_SET_CAPACITY = 8;

/// [°C] Target of the tools waiting in their docks: the stock filament preheat temperature, at
/// which a loaded nozzle does not ooze. A session never tunes below it.
inline constexpr int16_t WAITING_TEMP = 170;

/// How far the turn of the tool in turn is
enum class TurnStage : uint8_t {
    /// Being picked and moved to the front; its heater is not in relay mode yet
    moving_to_front,
    /// In relay mode at the front, heating up or being measured
    measuring,
};

constexpr bool contains(ToolSet set, uint8_t tool) {
    return tool < TOOL_SET_CAPACITY && ((set >> tool) & 1) != 0;
}

/// @returns the lowest tool of @p set, nullopt for an empty set
constexpr std::optional<uint8_t> first_tool(ToolSet set) {
    if (set == 0) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(std::countr_zero(set));
}

/// @returns the lowest tool of @p set above @p tool, nullopt if there is none
constexpr std::optional<uint8_t> next_tool(ToolSet set, uint8_t tool) {
    if (tool >= TOOL_SET_CAPACITY - 1) {
        return std::nullopt;
    }
    const unsigned up_to_tool = (2u << tool) - 1u;
    return first_tool(static_cast<ToolSet>(set & ~up_to_tool));
}

constexpr int tool_count(ToolSet set) {
    return std::popcount(set);
}

/// @returns 1 for the first tool of @p set, 2 for the second and so on; 0 if @p tool is not in it
constexpr int position_of(ToolSet set, uint8_t tool) {
    if (!contains(set, tool)) {
        return 0;
    }
    const unsigned up_to_tool = (2u << tool) - 1u;
    return std::popcount(static_cast<ToolSet>(set & up_to_tool));
}

/// Nozzle target of @p tool while @p tool_in_turn has its turn in a session of @p set:
/// - 0 for a tool that is not in the session, for the tool in turn and for the finished tools;
/// - @p tuning_temp for the next tool in line while the tool in turn is measured, so at most one
///   waiting tool is fully hot at a time;
/// - WAITING_TEMP for every other waiting tool.
/// The tool in turn is not driven by this: it keeps the target it had while waiting until its
/// relay starts at the front.
/// @pre @p tuning_temp >= WAITING_TEMP, so no waiting target exceeds the tuning temperature
constexpr int16_t waiting_target(ToolSet set, uint8_t tool_in_turn, TurnStage stage, uint8_t tool, int16_t tuning_temp) {
    if (!contains(set, tool) || tool <= tool_in_turn) {
        return 0;
    }
    if (stage == TurnStage::measuring && next_tool(set, tool_in_turn) == tool) {
        return tuning_temp;
    }
    return WAITING_TEMP;
}

} // namespace hotend_pid
