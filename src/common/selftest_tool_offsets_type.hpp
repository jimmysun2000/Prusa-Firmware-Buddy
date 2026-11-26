#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <utility>

#include <common/fsm_base_types.hpp>
#include "selftest_sub_state.hpp"
#include "inc/MarlinConfig.h"
#include <marlin_stubs/G425.hpp>
#include <selftest/selftest_data.hpp>

struct SelftestToolOffsets_t {
    uint8_t progress;
    SelftestSubtestState_t state;

    selftest::ToolOffsetsMode mode = selftest::ToolOffsetsMode::full;
    uint8_t other_tool = 0; ///< Raw index of the tool measured together with the reference tool

    /// Why the calibration was rejected, shown in the ToolOffsets_result_rejected phase
    pin_tool_offset::Failure failure {};

    static constexpr int16_t TOOL_OFFSET_CLEANING_TEMPERATURE = 200; //< Temperature to heat up towards for cleaning tools
    static constexpr int16_t TOOL_CALIBRATION_TEMPERATURE = 70; //< Temperature to maintain during calibration

    constexpr SelftestToolOffsets_t(uint8_t prog = 0, SelftestSubtestState_t st = SelftestSubtestState_t::undef)
        : progress(prog)
        , state(st) {}

    void Pass() {
        state = SelftestSubtestState_t::ok;
        progress = 100;
    }

    void Fail() {
        state = SelftestSubtestState_t::not_good;
        progress = 100;
    }

    void Abort() {}

    fsm::PhaseData Serialize() const {
        const int32_t value_centi_mm = std::isfinite(failure.value)
            ? static_cast<int32_t>(std::clamp(std::lround(failure.value * 100), SerializedData::VALUE_MIN, SerializedData::VALUE_MAX))
            : SerializedData::VALUE_MAX;
        const SerializedData serialized {
            .state = static_cast<uint32_t>(state),
            .mode = static_cast<uint32_t>(mode),
            .other_tool = other_tool,
            .failure_reason = static_cast<uint32_t>(failure.reason),
            .failure_tool = failure.tool.has_value() ? failure.tool->to_raw() : SerializedData::NO_TOOL,
            .failure_axis = failure.axis,
            .failure_value_centi_mm = value_centi_mm,
        };
        return fsm::serialize_data(serialized);
    }

    static SelftestToolOffsets_t Deserialize(fsm::PhaseData data) {
        const auto serialized = fsm::deserialize_data<SerializedData>(data);
        SelftestToolOffsets_t result;
        result.state = static_cast<SelftestSubtestState_t>(serialized.state);
        result.mode = static_cast<selftest::ToolOffsetsMode>(serialized.mode);
        result.other_tool = serialized.other_tool;
        result.failure = {
            .reason = static_cast<pin_tool_offset::FailureReason>(serialized.failure_reason),
            .tool = serialized.failure_tool < PhysicalToolIndex::count ? std::optional(PhysicalToolIndex::from_raw(serialized.failure_tool)) : std::nullopt,
            .axis = static_cast<AxisEnum>(serialized.failure_axis),
            .value = static_cast<float>(serialized.failure_value_centi_mm) / 100,
        };
        return result;
    }

private:
    struct __attribute__((packed)) SerializedData {
        static constexpr uint32_t NO_TOOL = 7;
        static constexpr long VALUE_MIN = -(1 << 16);
        static constexpr long VALUE_MAX = (1 << 16) - 1;

        uint32_t state : 2;
        uint32_t mode : 2;
        uint32_t other_tool : 3;
        uint32_t failure_reason : 3;
        uint32_t failure_tool : 3;
        uint32_t failure_axis : 2;
        int32_t failure_value_centi_mm : 17;
    };
    static_assert(sizeof(SerializedData) <= sizeof(fsm::PhaseData), "Selftest tool offsets data fits into Phase data");
    static_assert(PhysicalToolIndex::count <= SerializedData::NO_TOOL);
    static_assert(std::to_underlying(pin_tool_offset::FailureReason::out_of_bounds) < (1 << 3));
};
