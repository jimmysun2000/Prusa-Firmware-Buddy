/// @file
/// Read-only view of the bed leveling mesh for the GUI and PrusaLink.
///
/// The header does not depend on Marlin, so the pure helpers can be used and tested without it.
#pragma once

#include <array>
#include <cstdint>

namespace bed_mesh_view {

/// Mesh geometry.
struct Info {
    /// Grid points along X
    uint8_t nx;

    /// Grid points along Y
    uint8_t ny;

    /// Grid rows/columns at each edge that are never probed
    uint8_t border;

    /// Distance between two major (probed) points, in grid points
    uint8_t major_step;

    /// Position of the first grid point [mm]
    float min_x, min_y;

    /// Position of the last grid point [mm]
    float max_x, max_y;
};

enum class State : uint8_t {
    /// All points are zero - nothing was measured since boot or the mesh was reset
    none,

    /// Some points are not measured (NaN); typically while probing
    partial,

    /// All points hold a value
    complete,
};

/// Reads a mesh point; NaN means not measured.
using ZGetter = float (*)(uint8_t ix, uint8_t iy);

/// Reads whether a mesh point holds a value measured by the probe.
using ProbedGetter = bool (*)(uint8_t ix, uint8_t iy);

// Live mesh access (implemented on top of the UBL mesh).
//
// The mesh is read directly, without a lock. While the printer is probing, a reader can see a mix
// of old and new points; the next read is consistent again.

Info info();

/// @returns height of the point [mm], NaN if not measured. @p ix grows to the right, @p iy to the back.
float z(uint8_t ix, uint8_t iy);

/// @returns whether the probe measured the point since the mesh was last cleared and the point still holds a value.
/// False for every point when this is not known, e.g. after a power panic resume.
bool probed(uint8_t ix, uint8_t iy);

State state();

/// @returns a value that changes whenever any mesh point or its probed mark changes (with high probability)
uint32_t revision();

// Pure helpers

/// @returns whether grid index @p i is a major point on an axis with @p n points
constexpr bool is_major(uint8_t i, uint8_t n, uint8_t border, uint8_t step) {
    return i >= border && i + border < n && (i - border) % step == 0;
}

/// Lower bound of the colour scale half-range [mm], so a flat bed is not shown as a rainbow of noise
inline constexpr float min_full_scale = 0.10f;

/// Number of colours of the deviation scale
inline constexpr uint8_t color_steps = 16;

/// Statistics over the major points that hold a value.
struct Stats {
    /// Number of major points with a value; the other members are meaningful only if nonzero
    uint16_t count = 0;

    float min = 0;
    float max = 0;
    float mean = 0;

    /// Half-range of the colour scale: max(min_full_scale, largest |point - mean|) [mm]
    float full_scale = min_full_scale;
};

Stats compute_stats(const Info &info, ZGetter z);

State compute_state(const Info &info, ZGetter z);

/// @returns number of points for which @p probed holds
uint16_t count_probed(const Info &info, ProbedGetter probed);

/// Hash of the raw bits of all points and, if @p probed is given, of their probed marks; position dependent
uint32_t compute_revision(const Info &info, ZGetter z, ProbedGetter probed = nullptr);

/// @returns colour step for a deviation from the mean, 0 (lowest, blue) to color_steps - 1 (highest, red).
/// Deviations beyond @p full_scale are clamped. The two middle steps are the palest.
uint8_t color_step(float deviation, float full_scale);

struct Rgb {
    uint8_t r, g, b;
};

/// @returns colour of a step returned by color_step(); steps out of range are clamped
Rgb color_of_step(uint8_t step);

/// Buffer large enough for any value from format_json_value()
using JsonValueBuffer = std::array<char, 16>;

/// Formats a mesh point for JSON: "null" if not finite, otherwise millimetres with 3 decimals.
/// @returns pointer to a static string or into @p buffer
const char *format_json_value(float z, JsonValueBuffer &buffer);

/// @returns "none", "partial" or "complete"
const char *to_str(State state);

} // namespace bed_mesh_view
