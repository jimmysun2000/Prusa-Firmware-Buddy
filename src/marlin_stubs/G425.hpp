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

/// Pin-based calibration that measures only the reference tool and one other tool
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

struct Failure {
    FailureReason reason = FailureReason::unknown;

    /// Tool the failed step was done with, if any
    std::optional<PhysicalToolIndex> tool = std::nullopt;

    /// out_of_bounds only: axis and value of the rejected offset component [mm]
    AxisEnum axis = X_AXIS;
    float value = 0;
};

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
