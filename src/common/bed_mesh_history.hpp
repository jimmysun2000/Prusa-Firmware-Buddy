/// @file
/// History of the last measured bed meshes, kept in RAM for PrusaLink.
///
/// An entry holds the major (probed) points of a mesh with the printer state at the time of the
/// measurement. The history is lost on every restart.
///
/// The header does not depend on Marlin, so the store can be used and tested without it.
#pragma once

#include "bed_mesh_view.hpp"

#include <freertos/mutex.hpp>

#include <array>
#include <cstdint>

namespace bed_mesh_history {

/// Points of an entry along X: the major points of the mesh
inline constexpr uint8_t points_x = 12;

/// Points of an entry along Y: the major points of the mesh
inline constexpr uint8_t points_y = 12;

inline constexpr uint16_t points = points_x * points_y;

/// Number of entries kept; a new entry replaces the oldest one
inline constexpr uint8_t capacity = 8;

/// Height of a point without a value
inline constexpr int16_t no_value_um = INT16_MIN;

/// Bed temperature that is not known
inline constexpr int16_t no_temperature = INT16_MIN;

/// Tool of a measurement without a picked tool
inline constexpr uint8_t no_tool = 0xFF;

/// Printer state at the time of a measurement
struct Meta {
    /// Seconds since boot
    uint32_t uptime_s = 0;

    /// UTC [s since the epoch], 0 if the clock was not set
    uint32_t time_s = 0;

    /// [°C]
    int16_t bed_target = 0;

    /// [0.1 °C], no_temperature if not known
    int16_t bed_temp = no_temperature;

    /// 0-based, no_tool if none
    uint8_t tool = no_tool;

    bool printing = false;
};

/// Everything of an entry except its points
struct Summary {
    Meta meta;

    /// 1 for the first entry since boot, incremented by one for each next entry
    uint16_t id;

    /// Extremes over the points [µm], no_value_um if no point holds a value
    int16_t z_min_um;
    int16_t z_max_um;

    /// Points measured by the probe; the others were filled in by the printer
    uint8_t probed_count;
};

/// One row (one Y) of an entry
struct Row {
    /// Heights [µm] from left to right, no_value_um for a point without a value
    std::array<int16_t, points_x> z_um;

    /// Bit ix is set if point ix was measured by the probe
    uint16_t probed;
};

/// Time now, as stored in an entry
struct Clock {
    /// Seconds since boot
    uint32_t uptime_s;

    /// UTC [s since the epoch], 0 if the clock is not set
    uint32_t time_s;
};

/// @returns @p z_mm rounded to whole µm, clamped to ±32,767 µm; no_value_um for NaN
int16_t to_um(float z_mm);

/// The entries of the history.
///
/// One thread writes (the Marlin thread at the end of each G29), others read. Every read copies a
/// small part of one entry under a lock; an entry changes only by being replaced by a newer one,
/// which a reader notices by its id.
class Store {
public:
    /// Adds an entry with the major points of a mesh if the mesh is complete, at least one major
    /// point is measured by the probe and the points or their probed marks differ from the newest
    /// entry. The oldest entry is dropped if the history is full.
    ///
    /// Only reads the mesh. The lock is held only while the new entry is written.
    /// Must always be called from the same thread.
    ///
    /// @returns whether an entry was added
    bool push_if_measured(const bed_mesh_view::Info &info, bed_mesh_view::ZGetter z, bed_mesh_view::ProbedGetter probed, const Meta &meta);

    /// @returns id of the newest entry, 0 if there is none.
    /// The ids of the entries held are consecutive, down from this one.
    uint16_t newest_id() const;

    /// @returns whether entry @p id is held; @p out is written only if it is
    bool summary(uint16_t id, Summary &out) const;

    /// Reads row @p iy (0 = front) of entry @p id.
    /// @returns whether entry @p id is held and @p iy is a valid row; @p out is written only then
    bool row(uint16_t id, uint8_t iy, Row &out) const;

private:
    struct Entry {
        uint32_t uptime_s;
        uint32_t time_s;

        /// 0 for an empty slot
        uint16_t id;

        int16_t bed_target;
        int16_t bed_temp;
        int16_t z_min_um;
        int16_t z_max_um;
        uint8_t tool;
        uint8_t probed_count;
        bool printing;

        /// Bit iy * points_x + ix is set if the point was measured by the probe
        std::array<uint8_t, (points + 7) / 8> probed;

        /// Index iy * points_x + ix
        std::array<int16_t, points> z_um;
    };
    static_assert(sizeof(Entry) <= 336);

    /// Entry @p id lives in this slot while it is held
    static constexpr uint8_t slot_of(uint16_t id) {
        return (id - 1) % capacity;
    }

    mutable freertos::Mutex mutex;

    /// Written under the lock; the writer reads them without it
    std::array<Entry, capacity> entries {};
    uint16_t newest = 0;
};

/// @returns the history of the printer
Store &store();

/// @returns the time now
Clock now();

/// Adds the live mesh to the history of the printer if it is a new measurement (see Store::push_if_measured).
/// To be called on the Marlin thread at the end of every G29; does nothing while the G-code is being aborted.
void capture_if_measured();

} // namespace bed_mesh_history
