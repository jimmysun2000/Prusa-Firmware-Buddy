/**
 * @file pause_settings.cpp
 */

#include "pause_settings.hpp"
#include "config_features.h"
#include "../../../lib/Marlin/Marlin/src/core/types.h"
#include "../../../lib/Marlin/Marlin/src/feature/pause.h"
#include <option/has_mmu2.h>
#include <config_store/store_instance.hpp>

#include <algorithm>
#include <cmath>

// cannot be class member (externed in marlin)
StrongIndexArray<fil_change_settings_t, EXTRUDERS, VirtualToolIndex, VirtualToolIndex::to_raw_static, strong_index_array::AllowWeakIndexing::yes> fc_settings;

using namespace pause;

namespace {

/// Purge length from the Purge Length setting, before the high-flow adjustment
float configured_purge_length() {
    const uint8_t setting = config_store().purge_length.get();
    if (setting == config_store_ns::purge_length_unset) {
        return ADVANCED_PAUSE_PURGE_LENGTH;
    }
    return std::clamp(setting, config_store_ns::purge_length_min, config_store_ns::purge_length_max);
}

} // namespace

Settings::Settings()
    : unload_length(GetDefaultUnloadLength())
    , slow_load_length(GetDefaultSlowLoadLength())
    , fast_load_length(GetDefaultFastLoadLength())
    , retract(GetDefaultRetractLength())
    , resume_pos { NAN, NAN, NAN, NAN }
//
{
}

float Settings::GetDefaultFastLoadLength() {
    const VirtualToolIndex tool = VirtualToolIndex::currently_selected_opt()
                                      .value_or(VirtualToolIndex::from_raw(0));
    return fc_settings[tool].load_length;
}

float Settings::GetDefaultSlowLoadLength() {
    return FILAMENT_CHANGE_SLOW_LOAD_LENGTH;
}

float Settings::GetDefaultUnloadLength() {
    const VirtualToolIndex tool = VirtualToolIndex::currently_selected_opt()
                                      .value_or(VirtualToolIndex::from_raw(0));
    return fc_settings[tool].unload_length;
}

float Settings::GetDefaultRetractLength() {
    return -std::abs(STANDARD_RETRACT_LENGTH);
}

void Settings::SetUnloadLength(const std::optional<float> &len) {
    unload_length = -std::abs(len.has_value() ? len.value() : GetDefaultUnloadLength()); // it is negative value
}

void Settings::SetSlowLoadLength(const std::optional<float> &len) {
    slow_load_length = std::abs(len.has_value() ? len.value() : GetDefaultSlowLoadLength());
}

void Settings::SetFastLoadLength(const std::optional<float> &len) {
    fast_load_length = std::abs(len.has_value() ? len.value() : GetDefaultFastLoadLength());
}

void Settings::SetPurgeLength(const std::optional<float> &len) {
    if (len.has_value() && std::isfinite(*len)) {
        purge_length_ = std::min<float>(std::abs(*len), config_store_ns::purge_length_max);
    } else {
        purge_length_ = std::nullopt;
    }
}

void Settings::SetRetractLength(const std::optional<float> &len) {
    retract = -std::abs(len.has_value() ? len.value() : GetDefaultRetractLength()); // retract is negative
}

void Settings::SetParkPoint(const mapi::ParkingPosition &park_point) {
    this->park_point = park_point;
}

void Settings::SetResumePoint(const xyze_pos_t &resume_point) {
    resume_pos = resume_point; // TODO check limits
}

void Settings::SetResumeNozzleTemperature(int16_t temperature) {
    resume_nozzle_temperature = temperature;
}

float pause::Settings::purge_length() const {
    float r;

    if (purge_length_.has_value()) {
        r = *purge_length_;

    } else {
        // Double the purge length for HF nozzles
        r = configured_purge_length() * (config_store().get_nozzle_is_high_flow(physical_tool()) ? 2 : 1);
    }

    return std::max<float>(std::abs(r), minimal_purge);
}
