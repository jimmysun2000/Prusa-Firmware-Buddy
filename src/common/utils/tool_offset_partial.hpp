#pragma once

#include <cstdint>

#include <core/types.h>

/// Offset arithmetic of the pin-based tool offset calibration when only two tools are measured.
///
/// c_t is the pin centre measured with tool t. A full calibration stores
/// offset[t] = c_ref - c_t, so the reference tool's offset is zero there.
namespace tool_offset_partial {

/// @returns the offset of a tool measured against the reference tool, expressed in the frame
/// of the stored offsets (the reference tool keeps its stored offset).
/// @param ref_old stored offset of the reference tool, zero after a full calibration
/// @param c_ref pin centre measured with the reference tool
/// @param c_tool pin centre measured with the tool
constexpr xyz_pos_t single_tool_offset(const xyz_pos_t &ref_old, const MachinePosXYZ &c_ref, const MachinePosXYZ &c_tool) {
    const MachinePosXYZ diff = c_ref - c_tool;
    return ref_old + xyz_pos_t { .x = diff.x, .y = diff.y, .z = diff.z };
}

/// @returns the shift to add to the stored offset of every tool except the reference tool after
/// the reference tool has changed, so that the kept tool's offset equals its fresh measurement.
/// @param ref_old stored offset of the reference tool
/// @param c_ref pin centre measured with the reference tool
/// @param c_kept pin centre measured with the kept tool, which has not changed since its offset was stored
/// @param kept_old stored offset of the kept tool
constexpr xyz_pos_t rebase_delta(const xyz_pos_t &ref_old, const MachinePosXYZ &c_ref, const MachinePosXYZ &c_kept, const xyz_pos_t &kept_old) {
    return single_tool_offset(ref_old, c_ref, c_kept) - kept_old;
}

/// @returns whether min <= value <= max. NaN is never within the limits.
constexpr bool within_limits(float value, float min, float max) {
    return value >= min && value <= max;
}

} // namespace tool_offset_partial
