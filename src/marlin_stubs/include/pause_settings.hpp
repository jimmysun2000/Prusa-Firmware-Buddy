/**
 * @file pause_settings.hpp
 * @brief
 */

#pragma once
#include <optional>
#include "../../../lib/Marlin/Marlin/src/core/macros.h"
#include "../../../lib/Marlin/Marlin/src/core/types.h"
#include <tool_index.hpp>
#include <mapi/parking.hpp>

class Pause; // forward declaration, so Settings does not think Pause is member of pause namespace

namespace pause {

class Settings {
public:
    Settings();
    static constexpr const float minimal_purge = 1;

    // defaults
    static float GetDefaultFastLoadLength();
    static float GetDefaultSlowLoadLength();
    static float GetDefaultUnloadLength();
    static float GetDefaultRetractLength();

    void SetUnloadLength(const std::optional<float> &len);
    void SetSlowLoadLength(const std::optional<float> &len);
    void SetFastLoadLength(const std::optional<float> &len);

    /// Overrides the purge length (mm, sign ignored, capped at the Purge Length setting's maximum).
    /// nullopt or a non-finite value = no override.
    void SetPurgeLength(const std::optional<float> &len);

    void SetRetractLength(const std::optional<float> &len);
    void SetParkPoint(const mapi::ParkingPosition &park_point);
    void SetResumePoint(const xyze_pos_t &resume_point);
    void SetResumeNozzleTemperature(int16_t temperature);
    inline void SetExtruder(VirtualToolIndex target) { virtual_tool_ = target; }

    /// Marks a filament change as caused by the filament running out.
    /// The remnant then gets the short runout ramming instead of a proper tip.
    inline void SetFilamentRanOut(bool ran_out) { filament_ran_out = ran_out; }

    inline VirtualToolIndex virtual_tool() const {
        return virtual_tool_;
    }

    inline PhysicalToolIndex physical_tool() const {
        return virtual_tool().to_physical();
    }

    /// Length to purge after loading, at least minimal_purge.
    /// The override if set, used as given; otherwise the Purge Length setting (the printer's
    /// built-in length when set to Default), doubled for high-flow nozzles.
    float purge_length() const;

private:
    friend class ::Pause; // forward declaration of Pause is not enough, have to add scope resolution operator too

    // this values must be set before every load/unload
    float unload_length;
    float slow_load_length;
    float fast_load_length;
    std::optional<float> purge_length_;
    float retract;

    mapi::ParkingPosition park_point; // requested parking position, resolved against the live position at park time
    xyze_pos_t resume_pos;

    VirtualToolIndex virtual_tool_ = VirtualToolIndex::from_raw(0);

    // Target to restore after loading drops it to the new filament's default; empty = leave as-is.
    std::optional<int16_t> resume_nozzle_temperature;

    bool filament_ran_out = false;
};

} // namespace pause
