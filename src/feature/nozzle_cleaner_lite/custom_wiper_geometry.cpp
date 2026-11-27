#include "custom_wiper_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace nozzle_cleaner_lite::custom_wiper {

namespace {
    // NaN fails both comparisons, so a corrupted value is never within
    bool within(float value, float min, float max) {
        return value >= min && value <= max;
    }

    /// Fills in the travel fields of \p problem if \p value lies outside [min, max].
    /// \returns whether it does
    bool outside(float value, float min, float max, Problem &problem) {
        if (within(value, min, max)) {
            return false;
        }
        problem.beyond_max = value > max;
        problem.limit = problem.beyond_max ? max : min;
        // Stays 0 for NaN, which is outside but by no meaningful distance
        problem.by = problem.beyond_max ? value - max : (value < min ? min - value : 0.0f);
        return true;
    }
} // namespace

const char *to_string(Fault fault) {
    switch (fault) {
    case Fault::none:
        return "ok";
    case Fault::not_taught:
        return "start or end not taught";
    case Fault::stroke_length:
        return "stroke length out of range";
    case Fault::width:
        return "width out of range";
    case Fault::wipe_z:
        return "wipe height out of range";
    case Fault::travel_xy:
        return "outside XY travel for a tool";
    case Fault::travel_z:
        return "below Z travel for a tool";
    }
    return "unknown";
}

const char *to_string(Spot spot) {
    switch (spot) {
    case Spot::stroke_start:
        return "stroke start";
    case Spot::stroke_end:
        return "stroke end";
    case Spot::waiting_point:
        return "waiting point";
    case Spot::height:
        return "wipe height";
    }
    return "unknown";
}

std::optional<Frame> make_frame(Point start, Point end) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::hypot(dx, dy);
    if (!(length > 0.0f) || !std::isfinite(length)) {
        return std::nullopt;
    }

    const Point along { dx / length, dy / length };
    return Frame {
        .origin = start,
        .along = along,
        .across = { -along.y, along.x },
        .length = length,
    };
}

Point pad_to_xy(const Frame &frame, float along, float across) {
    return {
        frame.origin.x + frame.along.x * along + frame.across.x * across,
        frame.origin.y + frame.along.y * along + frame.across.y * across,
    };
}

Pad make_pad(const Frame &frame, float width) {
    return {
        .along_near = 0.0f,
        .along_far = frame.length,
        .across_min = -width / 2.0f,
        .across_max = width / 2.0f,
    };
}

Point staging_point(const Frame &frame) {
    return pad_to_xy(frame, -stage_distance, 0.0f);
}

Point tip_to_machine(Point tip, const Offset &tool_offset) {
    return { tip.x - tool_offset.x, tip.y - tool_offset.y };
}

float tip_to_machine_z(float tip_z, const Offset &tool_offset) {
    return tip_z - tool_offset.z;
}

int16_t touch_max_temp(std::optional<int16_t> filament_nozzle_temp) {
    if (!filament_nozzle_temp.has_value()) {
        return touch_floor;
    }
    return std::max<int16_t>(*filament_nozzle_temp - touch_margin, touch_floor);
}

bool print_clearance_ok(float max_printed_z, float tallest_tool_offset_z, float wipe_z) {
    if (max_printed_z <= 0.0f) {
        return true;
    }
    return max_printed_z + tallest_tool_offset_z + print_clearance_margin <= wipe_z;
}

Problem validate(const Geometry &geometry, float width, std::span<const ToolOffset> tool_offsets, const TravelLimits &limits) {
    if (!geometry.is_taught()) {
        return { .fault = Fault::not_taught };
    }

    const auto frame = make_frame({ geometry.start_x, geometry.start_y }, { geometry.end_x, geometry.end_y });
    if (!frame.has_value() || !within(frame->length, stroke_length_min, stroke_length_max)) {
        return { .fault = Fault::stroke_length };
    }

    if (!within(width, 0.0f, width_max)) {
        return { .fault = Fault::width };
    }

    const float wipe_z = geometry.wipe_z;
    if (!within(wipe_z, wipe_z_min, wipe_z_max)) {
        return { .fault = Fault::wipe_z };
    }

    struct TipPoint {
        Spot spot;
        Point tip;
    };

    // The band is a rectangle and the limits a box, so its corners cover the start, the end and every stroke
    const float half_width = width / 2.0f;
    const std::array<TipPoint, 5> tip_points { {
        { Spot::stroke_start, pad_to_xy(*frame, 0.0f, -half_width) },
        { Spot::stroke_start, pad_to_xy(*frame, 0.0f, half_width) },
        { Spot::stroke_end, pad_to_xy(*frame, frame->length, -half_width) },
        { Spot::stroke_end, pad_to_xy(*frame, frame->length, half_width) },
        { Spot::waiting_point, staging_point(*frame) },
    } };

    for (const ToolOffset &tool : tool_offsets) {
        Problem problem { .fault = Fault::travel_xy, .tool = tool.tool };

        for (const TipPoint &point : tip_points) {
            problem.spot = point.spot;
            const Point machine = tip_to_machine(point.tip, tool.offset);

            problem.axis = Axis::x;
            if (outside(machine.x, limits.x_min, limits.x_max, problem)) {
                return problem;
            }
            problem.axis = Axis::y;
            if (outside(machine.y, limits.y_min, limits.y_max, problem)) {
                return problem;
            }
        }

        problem.fault = Fault::travel_z;
        problem.spot = Spot::height;
        problem.axis = Axis::z;
        if (outside(tip_to_machine_z(wipe_z, tool.offset), limits.z_min, std::numeric_limits<float>::infinity(), problem)) {
            return problem;
        }
    }

    return {};
}

} // namespace nozzle_cleaner_lite::custom_wiper
