#pragma once

#include "cleaner_zigzag.hpp"

#include <cstdint>
#include <optional>
#include <span>

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

/// The routine waits for temperature this far before the stroke start, on the stroke line
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
/// Longest wait for the nozzle to reach the touch range [s]
inline constexpr uint16_t wait_timeout_s = 300;

enum class Fault : uint8_t {
    none,
    not_taught,
    stroke_length,
    width,
    wipe_z,
    travel_xy,
    travel_z,
};

/// Short English description for the log
const char *to_string(Fault fault);

/// \returns the frame of the stroke from \p start to \p end, nullopt if they coincide
std::optional<Frame> make_frame(Point start, Point end);

/// Converts frame coordinates to the frame's XY
Point pad_to_xy(const Frame &frame, float along, float across);

/// Pad for the zigzag: the whole stroke length, \p width centred on the stroke line
Pad make_pad(const Frame &frame, float width);

/// Where the nozzle waits for temperature and retreats to, stage_distance before the stroke start
Point staging_point(const Frame &frame);

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

/// Checks that the taught geometry can be wiped with every tool in \p tool_offsets:
/// stroke length and height within limits, and the stroke band of \p width plus
/// the staging point inside \p limits in the machine frame of each tool.
Fault validate(const Geometry &geometry, float width, std::span<const Offset> tool_offsets, const TravelLimits &limits);

} // namespace nozzle_cleaner_lite::custom_wiper
