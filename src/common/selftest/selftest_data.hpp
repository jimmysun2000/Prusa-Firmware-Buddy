#pragma once

#include <variant>
#include <cstdint>
#include <type_traits>

#include "selftest_types.hpp"
#include <option/has_indx.h>
#include <option/has_toolchanger.h>

namespace selftest {

struct FirstLayerCalibrationData {
    uint8_t previous_sheet;
};

/// Tool that the pin-based tool offsets are measured against
inline constexpr PhysicalToolIndex TOOL_OFFSETS_REFERENCE_TOOL = PhysicalToolIndex::from_raw(0);

/// Which tools the pin-based tool offset calibration measures and which offsets it changes
enum class ToolOffsetsMode : uint8_t {
    /// All enabled tools are measured, all offsets are recomputed
    full,
    /// The reference tool and the other tool are measured, only the other tool's offset changes
    single_tool,
    /// The reference tool has changed; it is measured with the other tool, which has not changed,
    /// and the offsets of all enabled tools except the reference tool are shifted by the same amount
    rebase,
};

struct ToolOffsetsCalibrationData {
    ToolOffsetsMode mode;
    /// Tool measured together with the reference tool; unused in full mode
    PhysicalToolIndex other_tool;

    static constexpr ToolOffsetsCalibrationData full() {
        return { .mode = ToolOffsetsMode::full, .other_tool = TOOL_OFFSETS_REFERENCE_TOOL };
    }
};

#if HAS_TOOLCHANGER() && !HAS_INDX()
/// What the hotend PID tuning of one tool runs with, chosen by the user for each run
struct HotendPidTuningData {
    PhysicalToolIndex tool;
    int16_t target_temp; ///< [°C]
    uint8_t print_fan_pwm;
};
#endif

using TestData = std::variant<std::monostate, ToolMask, FirstLayerCalibrationData, ToolOffsetsCalibrationData
#if HAS_TOOLCHANGER() && !HAS_INDX()
    ,
    HotendPidTuningData
#endif
    >;

} // namespace selftest
