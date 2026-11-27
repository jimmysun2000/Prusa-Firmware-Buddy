/**
 * @file G425.hpp
 * @brief Some code that is used by G425 and selftest_tool_offsets
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include <core/types.h>
#include <selftest/selftest_data.hpp>
#include <tool_index.hpp>
#include <utils/storage/strong_index_array.hpp>

/**
 * Perform calibration of all toolheads.
 * @return true if calibration was successful, false otherwise.
 */
bool full_calibration();

/// Pin-based tool offset calibration as run by the selftest: the full calibration with its
/// failure reported, and the partial one that measures only the reference tool and one other tool
namespace pin_tool_offset {

/// Step at which a calibration stopped
enum class FailureReason : uint8_t {
    unknown,
    not_homed,
    invalid_request,
    toolchange_failed,
    center_not_found,
    out_of_bounds,
};

/// @returns whether the calibration can be measured again right after failing for this reason,
/// without homing again or touching the pin and the sheet, because the toolchanger is in a known
/// state. Not after a failed tool change, nor when the cause is unknown or nothing was measured.
constexpr bool can_rerun_after(FailureReason reason) {
    switch (reason) {

    case FailureReason::center_not_found:
    case FailureReason::out_of_bounds:
        return true;

    case FailureReason::unknown:
    case FailureReason::not_homed:
    case FailureReason::invalid_request:
    case FailureReason::toolchange_failed:
        return false;
    }
    return false;
}

static_assert(can_rerun_after(FailureReason::center_not_found));
static_assert(can_rerun_after(FailureReason::out_of_bounds));
static_assert(!can_rerun_after(FailureReason::unknown));
static_assert(!can_rerun_after(FailureReason::not_homed));
static_assert(!can_rerun_after(FailureReason::invalid_request));
static_assert(!can_rerun_after(FailureReason::toolchange_failed));

struct Failure {
    FailureReason reason = FailureReason::unknown;

    /// Tool the failed step was done with, if any
    std::optional<PhysicalToolIndex> tool = std::nullopt;

    /// out_of_bounds only: axis and value of the rejected offset component [mm]
    AxisEnum axis = X_AXIS;
    float value = 0;
};

/// Runs full_calibration().
/// On failure, the tool and step are reported where known (tool change failed, pin center not
/// found), otherwise the reason is unknown.
std::expected<void, Failure> run_full_calibration();

/// single_tool or rebase mode with the tool measured together with the reference tool
using PartialRequest = selftest::ToolOffsetsCalibrationData;

struct PartialResult {
    /// New offset of each tool whose stored offset changes, nullopt for the others
    StrongIndexArray<std::optional<xyz_pos_t>, PhysicalToolIndex::count, PhysicalToolIndex, PhysicalToolIndex::to_raw_static> new_offsets;
};

/// @returns whether a partial calibration can run for the request on this printer:
/// single_tool or rebase mode, toolchanger enabled, no tool offset sensor present, and both the
/// reference tool and the other tool enabled and distinct.
bool is_partial_request_valid(const PartialRequest &request);

/// Measures the reference tool and the other tool on the calibration pin and computes the new offsets.
///
/// single_tool: only the other tool's offset changes.
/// rebase: the other tool is the one that has not changed; the offsets of all enabled tools except
/// the reference tool are shifted so that the other tool's offset equals its fresh measurement.
///
/// Writes nothing to the config store. Tool offsets in RAM equal the stored ones on return,
/// whatever the outcome. A new offset outside the hard limits fails with out_of_bounds and is
/// never returned. On success, no tool is picked.
std::expected<PartialResult, Failure> measure_partial(const PartialRequest &request);

/// Applies the offsets returned by measure_partial() in RAM and stores them.
/// This is the only config store write of a partial calibration; a rebase is stored in one transaction.
void commit_partial(const PartialRequest &request, const PartialResult &result);

} // namespace pin_tool_offset
