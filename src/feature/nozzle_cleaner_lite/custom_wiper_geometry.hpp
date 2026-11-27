#pragma once

#include "cleaner_zigzag.hpp"

#include <cstdint>
#include <expected>
#include <optional>

/// Geometry and limits of a wiper the user mounted and taught themselves,
/// used instead of Prusa's wiper. Knows nothing about Marlin, so the caller
/// passes in the tool offsets and travel limits.
namespace nozzle_cleaner_lite::custom_wiper {

#pragma pack(push, 1)
/// Taught wiper position in the nozzle tip frame: the machine position plus
/// the tool offset applied while teaching. Subtracting any tool's offset puts
/// that tool's tip on the same spot.
struct Geometry {
    static constexpr uint8_t taught_start = 1 << 0;
    static constexpr uint8_t taught_end = 1 << 1;

    /// Where the strokes start
    float start_x = 0;
    float start_y = 0;

    /// Where the strokes turn back
    float end_x = 0;
    float end_y = 0;

    /// Nozzle tip height while wiping, taught together with the start
    float wipe_z = 0;

    /// Which points were taught, see taught_start and taught_end
    uint8_t taught = 0;

    constexpr bool is_taught() const {
        return (taught & (taught_start | taught_end)) == (taught_start | taught_end);
    }
};
#pragma pack(pop)

// Packed fields cannot bind to the references a defaulted comparison would use
constexpr bool operator==(const Geometry &a, const Geometry &b) {
    return a.start_x == b.start_x && a.start_y == b.start_y && a.end_x == b.end_x && a.end_y == b.end_y && a.wipe_z == b.wipe_z && a.taught == b.taught;
}

struct Point {
    float x = 0;
    float y = 0;
};

/// Tool offset; tip = machine + offset
struct Offset {
    float x = 0;
    float y = 0;
    float z = 0;
};

/// Machine travel area every point the routine visits has to stay inside
struct TravelLimits {
    float x_min = 0;
    float x_max = 0;
    float y_min = 0;
    float y_max = 0;
    float z_min = 0;
};

/// The stroke line's own frame: 'along' runs from the stroke start towards
/// its end, 'across' is perpendicular to it, positive to the left.
struct Frame {
    Point origin;
    Point along;
    Point across;
    float length = 0;
};

/// Lowest tip height accepted for wiping, above the sheet plane.
/// Kept apart so it can later become the firmware's Z floor.
inline constexpr float wipe_z_min = 0.3f;
inline constexpr float wipe_z_max = 50.0f;

inline constexpr float stroke_length_min = 10.0f;
inline constexpr float stroke_length_max = 100.0f;

inline constexpr float width_default = 3.0f;
inline constexpr float width_max = 8.0f;

/// Fast stroke speed [mm/s]
inline constexpr uint8_t speed_default = 50;
inline constexpr uint8_t speed_min = 10;
inline constexpr uint8_t speed_max = 130;
inline constexpr float slow_speed = 25.0f;
inline constexpr uint8_t fast_cycles = 5;
inline constexpr uint8_t slow_cycles = 2;

/// The routine waits for temperature this far beside the stroke, on the stroke line
inline constexpr float stage_distance = 10.0f;
/// Height above the wipe height at which the nozzle moves onto the wiper
inline constexpr float approach_clearance = 10.0f;
/// How far above the taught height the loadcell may stop the descent and still count as the wiper
inline constexpr float guard_allowance = 2.0f;
/// How far above the printed part the wipe height has to be
inline constexpr float print_clearance_margin = 2.0f;

/// Heater target while on the wiper
inline constexpr int16_t wiper_target_temp = 140;
/// Below this the nozzle is heated up to wiper_target_temp before touching
inline constexpr int16_t heat_up_below_temp = wiper_target_temp - 3;
/// How much colder than the filament's print temperature the nozzle has to be to touch the wiper
inline constexpr int16_t touch_margin = 50;
/// The touch limit never drops below this, so holding wiper_target_temp is always allowed
inline constexpr int16_t touch_floor = wiper_target_temp + 3;
/// Longest wait for the nozzle to reach the touch range or the soak temperature [s]
inline constexpr uint16_t wait_timeout_s = 300;

/// Deep clean: how long the nozzle soaks at print temperature [s]
inline constexpr uint16_t soak_time_default = 120;
inline constexpr uint16_t soak_time_min = 30;
inline constexpr uint16_t soak_time_max = 600;
/// Deep clean: the nozzle soaks this far above the wipe height, beside the wiper at the waiting point
inline constexpr float soak_clearance = 100.0f;

enum class Fault : uint8_t {
    none,
    /// No tool is picked to plan for
    no_tool,
    not_taught,
    stroke_length,
    width,
    wipe_z,
    travel_xy,
    travel_z,
};

/// Point of the wipe a Problem is about
enum class Spot : uint8_t {
    stroke_start,
    stroke_end,
    /// Both stroke ends, equally far beyond the same limit
    stroke,
    waiting_point,
    height,
};

enum class Axis : uint8_t {
    x,
    y,
    z,
};

/// Why a taught geometry cannot be wiped, precise enough to tell what to change
struct Problem {
    Fault fault = Fault::none;

    // The rest is only set for travel_xy and travel_z

    /// Tool the problem was found with, 0-based
    uint8_t tool = 0;
    Spot spot = Spot::stroke_start;
    Axis axis = Axis::x;
    /// Beyond the upper limit of \p axis rather than the lower one
    bool beyond_max = false;
    /// How far beyond the limit [mm]
    float by = 0;
    /// The limit, as a machine coordinate [mm]
    float limit = 0;
    /// Smallest wipe width with which the tool would reach, 0 if no width up to width_max would
    float min_width = 0;

    constexpr bool ok() const { return fault == Fault::none; }
};

/// How far outside the limits a point may be computed and still count as inside [mm], so
/// that float rounding of a point taught exactly on a limit does not reject it. Far below
/// any margin the caller keeps from the real travel limits.
inline constexpr float reach_tolerance = 0.01f;

/// Everything one tool wipes with, in that tool's machine frame
struct ToolPlan {
    /// The taught stroke line
    Frame frame;

    /// The part of the wipe band the tool reaches. along_near is the end the strokes
    /// start from and return to: the stroke start, or its end when the waiting point
    /// had to move past it.
    Pad pad;

    /// Where the nozzle waits for temperature and retreats to, stage_distance before
    /// along_near on the stroke line, at the band's across position nearest to that line
    Point waiting_point;

    /// Machine Z of the taught wipe height
    float wipe_z = 0;

    /// How far from the taught line the band starts, because the travel limit cuts it off [mm]
    float line_shift() const;
};

/// Short English description for the log
const char *to_string(Fault fault);
const char *to_string(Spot spot);

/// \returns the frame of the stroke from \p start to \p end, nullopt if they coincide
std::optional<Frame> make_frame(Point start, Point end);

/// Converts frame coordinates to the frame's XY
Point pad_to_xy(const Frame &frame, float along, float across);

/// Pad for the zigzag: the whole stroke length, between \p across_min and \p across_max
Pad make_pad(const Frame &frame, float across_min, float across_max);

/// Converts a tip-frame point to the machine position that puts the tip there
Point tip_to_machine(Point tip, const Offset &tool_offset);
float tip_to_machine_z(float tip_z, const Offset &tool_offset);

/// Hottest nozzle temperature at which the nozzle may touch the wiper
/// \param filament_nozzle_temp print temperature of the loaded filament, nullopt if unknown
int16_t touch_max_temp(std::optional<int16_t> filament_nozzle_temp);

/// Whether wiping at tip height \p wipe_z keeps clear of the printed part.
/// \param max_printed_z highest printed carriage Z; 0 means nothing printed, as it does for Z homing
/// \param tallest_tool_offset_z highest tool Z offset, standing in for the tool that printed the top
bool print_clearance_ok(float max_printed_z, float tallest_tool_offset_z, float wipe_z);

/// Checks what does not depend on the tool: both points taught, and the stroke
/// length, \p width and wipe height within their ranges.
Problem check_geometry(const Geometry &geometry, float width);

/// Plans the wipe of \p geometry for the tool with \p tool_offset, inside \p limits.
///
/// The band is \p width centred on the taught line, cut where the limits do not let
/// both stroke ends reach it. A tool that cannot reach the band at all is refused
/// with a travel_xy Problem. Every point of the plan lies inside \p limits, within
/// reach_tolerance, and so does every straight move between them, as the limits
/// are a box. The waiting point moves past the stroke end, and the strokes start
/// from there, when it does not fit before the start.
///
/// \param tool 0-based, only to name the tool in a Problem
std::expected<ToolPlan, Problem> plan_for_tool(const Geometry &geometry, float width, uint8_t tool, const Offset &tool_offset, const TravelLimits &limits);

} // namespace nozzle_cleaner_lite::custom_wiper
