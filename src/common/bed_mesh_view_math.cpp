/// @file
#include "bed_mesh_view.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bed_mesh_view {

Stats compute_stats(const Info &info, ZGetter z) {
    Stats stats;
    float sum = 0;

    for (uint8_t iy = 0; iy < info.ny; iy++) {
        if (!is_major(iy, info.ny, info.border, info.major_step)) {
            continue;
        }
        for (uint8_t ix = 0; ix < info.nx; ix++) {
            if (!is_major(ix, info.nx, info.border, info.major_step)) {
                continue;
            }
            const float v = z(ix, iy);
            if (!std::isfinite(v)) {
                continue;
            }
            if (stats.count == 0) {
                stats.min = stats.max = v;
            } else {
                stats.min = std::min(stats.min, v);
                stats.max = std::max(stats.max, v);
            }
            sum += v;
            stats.count++;
        }
    }

    if (stats.count > 0) {
        stats.mean = sum / stats.count;
        stats.full_scale = std::max({ min_full_scale, stats.max - stats.mean, stats.mean - stats.min });
    }
    return stats;
}

State compute_state(const Info &info, ZGetter z) {
    bool all_zero = true;
    for (uint8_t ix = 0; ix < info.nx; ix++) {
        for (uint8_t iy = 0; iy < info.ny; iy++) {
            const float v = z(ix, iy);
            if (std::isnan(v)) {
                return State::partial;
            }
            all_zero &= (v == 0.0f);
        }
    }
    return all_zero ? State::none : State::complete;
}

uint32_t compute_revision(const Info &info, ZGetter z) {
    // FNV-1a over the 32-bit words; a plain XOR would not notice e.g. all points turning from 0 to NaN
    uint32_t hash = 2166136261u;
    for (uint8_t ix = 0; ix < info.nx; ix++) {
        for (uint8_t iy = 0; iy < info.ny; iy++) {
            hash = (hash ^ std::bit_cast<uint32_t>(z(ix, iy))) * 16777619u;
        }
    }
    return hash;
}

uint8_t color_step(float deviation, float full_scale) {
    constexpr int half = color_steps / 2;
    if (!std::isfinite(deviation) || !(full_scale > 0)) {
        return half;
    }
    const int step = static_cast<int>(std::floor(deviation / full_scale * half)) + half;
    return static_cast<uint8_t>(std::clamp(step, 0, color_steps - 1));
}

Rgb color_of_step(uint8_t step) {
    constexpr int half = color_steps / 2;
    constexpr Rgb low { 0, 64, 255 };
    constexpr Rgb high { 255, 32, 0 };

    step = std::min<uint8_t>(step, color_steps - 1);
    // Distance from white: 1/half for the two middle steps, 1 for the outermost ones
    const bool is_high = step >= half;
    const int weight = is_high ? step - half + 1 : half - step;
    const Rgb &target = is_high ? high : low;
    const auto mix = [&](uint8_t channel) {
        return static_cast<uint8_t>(255 - (255 - channel) * weight / half);
    };
    return Rgb { mix(target.r), mix(target.g), mix(target.b) };
}

const char *format_json_value(float z, JsonValueBuffer &buffer) {
    // Anything this large is garbage, not a bed height
    if (!std::isfinite(z) || std::fabs(z) >= 1.0e6f) {
        return "null";
    }
    const long micrometers = std::lround(static_cast<double>(z) * 1000.0);
    const long magnitude = std::labs(micrometers);
    snprintf(buffer.data(), buffer.size(), "%s%ld.%03ld", micrometers < 0 ? "-" : "", magnitude / 1000, magnitude % 1000);
    return buffer.data();
}

const char *to_str(State state) {
    switch (state) {
    case State::none:
        return "none";
    case State::partial:
        return "partial";
    case State::complete:
        return "complete";
    }
    return "none";
}

} // namespace bed_mesh_view
