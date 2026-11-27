/// @file
#include "bed_mesh_history.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace bed_mesh_history {

namespace {

    Store instance;

    template <size_t size>
    bool is_bit_set(const std::array<uint8_t, size> &bits, uint16_t i) {
        return (bits[i / 8] >> (i % 8)) & 1;
    }

} // namespace

int16_t to_um(float z_mm) {
    if (std::isnan(z_mm)) {
        return no_value_um;
    }
    // Same rounding as the mesh JSON, so both show the same value
    const double um = std::clamp(static_cast<double>(z_mm) * 1000.0, -32767.0, 32767.0);
    return static_cast<int16_t>(std::lround(um));
}

bool Store::push_if_measured(const bed_mesh_view::Info &info, bed_mesh_view::ZGetter z, bed_mesh_view::ProbedGetter probed, const Meta &meta) {
    if (bed_mesh_view::major_count(info.nx, info.border, info.major_step) != points_x
        || bed_mesh_view::major_count(info.ny, info.border, info.major_step) != points_y) {
        return false;
    }
    if (bed_mesh_view::compute_state(info, z) != bed_mesh_view::State::complete) {
        return false;
    }

    const auto grid_index = [&](uint8_t i) {
        return static_cast<uint8_t>(info.border + i * info.major_step);
    };

    bool any_probed = false;
    bool differs = newest == 0;
    for (uint8_t iy = 0; iy < points_y; iy++) {
        for (uint8_t ix = 0; ix < points_x; ix++) {
            const bool mark = probed(grid_index(ix), grid_index(iy));
            any_probed |= mark;
            if (!differs) {
                // This is the only thread that changes the entries, so it reads them without the lock
                const Entry &previous = entries[slot_of(newest)];
                const uint16_t i = iy * points_x + ix;
                differs = previous.z_um[i] != to_um(z(grid_index(ix), grid_index(iy))) || is_bit_set(previous.probed, i) != mark;
            }
        }
    }
    if (!any_probed || !differs) {
        return false;
    }

    // Ids are not reused before 65,535 entries since boot
    const uint16_t id = newest == UINT16_MAX ? 1 : newest + 1;

    // The mesh is not changed meanwhile: it is written by the same thread
    std::unique_lock lock(mutex);
    Entry &entry = entries[slot_of(id)];
    entry.uptime_s = meta.uptime_s;
    entry.time_s = meta.time_s;
    entry.id = id;
    entry.bed_target = meta.bed_target;
    entry.bed_temp = meta.bed_temp;
    entry.z_min_um = no_value_um;
    entry.z_max_um = no_value_um;
    entry.tool = meta.tool;
    entry.probed_count = 0;
    entry.printing = meta.printing;
    entry.probed = {};
    for (uint8_t iy = 0; iy < points_y; iy++) {
        for (uint8_t ix = 0; ix < points_x; ix++) {
            const uint16_t i = iy * points_x + ix;
            const int16_t um = to_um(z(grid_index(ix), grid_index(iy)));
            entry.z_um[i] = um;
            if (um != no_value_um) {
                const bool first = entry.z_min_um == no_value_um;
                entry.z_min_um = first ? um : std::min(entry.z_min_um, um);
                entry.z_max_um = first ? um : std::max(entry.z_max_um, um);
            }
            if (probed(grid_index(ix), grid_index(iy))) {
                entry.probed[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
                entry.probed_count++;
            }
        }
    }
    newest = id;
    return true;
}

uint16_t Store::newest_id() const {
    std::unique_lock lock(mutex);
    return newest;
}

bool Store::summary(uint16_t id, Summary &out) const {
    if (id == 0) {
        return false;
    }
    std::unique_lock lock(mutex);
    const Entry &entry = entries[slot_of(id)];
    if (entry.id != id) {
        return false;
    }
    out = Summary {
        .meta = Meta {
            .uptime_s = entry.uptime_s,
            .time_s = entry.time_s,
            .bed_target = entry.bed_target,
            .bed_temp = entry.bed_temp,
            .tool = entry.tool,
            .printing = entry.printing,
        },
        .id = entry.id,
        .z_min_um = entry.z_min_um,
        .z_max_um = entry.z_max_um,
        .probed_count = entry.probed_count,
    };
    return true;
}

bool Store::row(uint16_t id, uint8_t iy, Row &out) const {
    if (id == 0 || iy >= points_y) {
        return false;
    }
    std::unique_lock lock(mutex);
    const Entry &entry = entries[slot_of(id)];
    if (entry.id != id) {
        return false;
    }
    const uint16_t first = iy * points_x;
    std::copy_n(entry.z_um.begin() + first, points_x, out.z_um.begin());
    out.probed = 0;
    for (uint8_t ix = 0; ix < points_x; ix++) {
        if (is_bit_set(entry.probed, first + ix)) {
            out.probed |= static_cast<uint16_t>(1u << ix);
        }
    }
    return true;
}

Store &store() {
    return instance;
}

} // namespace bed_mesh_history
