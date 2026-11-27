#include "custom_wiper_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace nozzle_cleaner_lite::custom_wiper {

namespace {
    constexpr float infinity = std::numeric_limits<float>::infinity();

    // NaN fails both comparisons, so a corrupted value is never within
    bool within(float value, float min, float max) {
        return value >= min && value <= max;
    }

    /// Fills in the travel fields of \p problem if \p value lies outside [min, max]
    /// by more than \p tolerance.
    /// \returns whether it does
    bool outside(float value, float min, float max, Problem &problem, float tolerance = 0.0f) {
        if (within(value, min - tolerance, max + tolerance)) {
            return false;
        }
        problem.beyond_max = value > max;
        problem.limit = problem.beyond_max ? max : min;
        // Stays 0 for NaN, which is outside but by no meaningful distance
        problem.by = problem.beyond_max ? value - max : (value < min ? min - value : 0.0f);
        return true;
    }

    /// How far \p point lies outside the XY limits, on the worse axis if on both.
    /// \returns nullopt if it is inside, within reach_tolerance
    std::optional<Problem> xy_overshoot(Point point, const TravelLimits &limits, const Problem &base) {
        std::optional<Problem> worst;

        Problem problem = base;
        problem.axis = Axis::x;
        if (outside(point.x, limits.x_min, limits.x_max, problem, reach_tolerance)) {
            worst = problem;
        }

        problem = base;
        problem.axis = Axis::y;
        if (outside(point.y, limits.y_min, limits.y_max, problem, reach_tolerance) && (!worst.has_value() || problem.by > worst->by)) {
            worst = problem;
        }
        return worst;
    }

    /// Closed interval of across positions
    struct Interval {
        float lo = -infinity;
        float hi = infinity;

        bool empty() const { return !(lo <= hi); }
    };

    /// Narrows \p reach to the across positions a at which c + d * a lies in [min, max]
    void restrict_reach(Interval &reach, float c, float d, float min, float max) {
        // The stroke runs along this axis, so no across position changes it
        if (std::abs(d) < 1e-6f) {
            if (!within(c, min - reach_tolerance, max + reach_tolerance)) {
                reach = { infinity, -infinity };
            }
            return;
        }
        const float to_min = (min - c) / d;
        const float to_max = (max - c) / d;
        reach.lo = std::max(reach.lo, std::min(to_min, to_max));
        reach.hi = std::min(reach.hi, std::max(to_min, to_max));
    }

    /// Why the band of \p half_width on either side of the line holds no across position
    /// in \p reach. Measured at the band edge nearest to \p reach, or on the taught line
    /// if the tool reaches no across position at all.
    Problem band_problem(const Frame &frame, const TravelLimits &limits, Interval reach, float half_width, uint8_t tool) {
        float across = 0.0f;
        float min_width = 0.0f;
        if (!reach.empty()) {
            const bool band_below = half_width < reach.lo;
            across = band_below ? half_width : -half_width;
            const float needed = band_below ? 2.0f * reach.lo : -2.0f * reach.hi;
            min_width = needed <= width_max ? needed : 0.0f;
        }

        const Problem base { .fault = Fault::travel_xy, .tool = tool, .min_width = min_width };
        Problem start_base = base;
        start_base.spot = Spot::stroke_start;
        Problem end_base = base;
        end_base.spot = Spot::stroke_end;
        const std::optional<Problem> at_start = xy_overshoot(pad_to_xy(frame, 0.0f, across), limits, start_base);
        const std::optional<Problem> at_end = xy_overshoot(pad_to_xy(frame, frame.length, across), limits, end_base);

        if (at_start.has_value() && at_end.has_value()) {
            if (at_start->axis == at_end->axis && at_start->beyond_max == at_end->beyond_max && std::abs(at_start->by - at_end->by) <= reach_tolerance) {
                Problem both = *at_start;
                both.spot = Spot::stroke;
                both.by = std::max(at_start->by, at_end->by);
                return both;
            }
            return at_end->by > at_start->by ? *at_end : *at_start;
        }
        if (at_start.has_value()) {
            return *at_start;
        }
        if (at_end.has_value()) {
            return *at_end;
        }

        // Neither end is beyond a limit by more than the tolerance there, yet no position fits both
        Problem none_found = base;
        none_found.spot = Spot::stroke;
        return none_found;
    }
} // namespace

const char *to_string(Fault fault) {
    switch (fault) {
    case Fault::none:
        return "ok";
    case Fault::no_tool:
        return "no tool picked";
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
    case Spot::stroke:
        return "stroke";
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

Pad make_pad(const Frame &frame, float across_min, float across_max) {
    return {
        .along_near = 0.0f,
        .along_far = frame.length,
        .across_min = across_min,
        .across_max = across_max,
    };
}

float ToolPlan::line_shift() const {
    return std::abs(std::clamp(0.0f, pad.across_min, pad.across_max));
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

Problem check_geometry(const Geometry &geometry, float width) {
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

    if (!within(geometry.wipe_z, wipe_z_min, wipe_z_max)) {
        return { .fault = Fault::wipe_z };
    }

    return {};
}

std::expected<ToolPlan, Problem> plan_for_tool(const Geometry &geometry, float width, uint8_t tool, const Offset &tool_offset, const TravelLimits &limits) {
    if (Problem problem = check_geometry(geometry, width); !problem.ok()) {
        problem.tool = tool;
        return std::unexpected(problem);
    }

    const Point start = tip_to_machine({ geometry.start_x, geometry.start_y }, tool_offset);
    const Point end = tip_to_machine({ geometry.end_x, geometry.end_y }, tool_offset);
    const auto frame = make_frame(start, end);
    if (!frame.has_value()) {
        // The taught points passed, so the tool offset is corrupted
        return std::unexpected(Problem { .fault = Fault::travel_xy, .tool = tool });
    }

    const float wipe_z = tip_to_machine_z(geometry.wipe_z, tool_offset);
    if (Problem problem { .fault = Fault::travel_z, .tool = tool, .spot = Spot::height, .axis = Axis::z }; outside(wipe_z, limits.z_min, infinity, problem)) {
        return std::unexpected(problem);
    }

    // Across positions at which both stroke ends are inside the limits. The limits are
    // a box, so then the whole stroke is, and every stroke between two such positions.
    Interval reach;
    for (const Point &end_point : { start, end }) {
        restrict_reach(reach, end_point.x, frame->across.x, limits.x_min, limits.x_max);
        restrict_reach(reach, end_point.y, frame->across.y, limits.y_min, limits.y_max);
    }

    const float half_width = width / 2.0f;
    Interval band { std::max(reach.lo, -half_width), std::min(reach.hi, half_width) };
    if (band.empty()) {
        const bool band_below = half_width < reach.lo;
        const float gap = band_below ? reach.lo - half_width : -half_width - reach.hi;
        if (reach.empty() || !(gap <= reach_tolerance)) {
            return std::unexpected(band_problem(*frame, limits, reach, half_width, tool));
        }
        // A band edge on the limit, missed by rounding: wipe on the nearest line the tool reaches
        band.lo = band.hi = band_below ? reach.lo : reach.hi;
    }

    ToolPlan plan {
        .frame = *frame,
        .pad = make_pad(*frame, band.lo, band.hi),
        .waiting_point = {},
        .wipe_z = wipe_z,
    };

    const float wait_across = std::clamp(0.0f, band.lo, band.hi);
    const Problem wait_base { .fault = Fault::travel_xy, .tool = tool, .spot = Spot::waiting_point };
    plan.waiting_point = pad_to_xy(*frame, -stage_distance, wait_across);
    if (const auto before_start = xy_overshoot(plan.waiting_point, limits, wait_base)) {
        plan.waiting_point = pad_to_xy(*frame, frame->length + stage_distance, wait_across);
        if (const auto past_end = xy_overshoot(plan.waiting_point, limits, wait_base)) {
            return std::unexpected(past_end->by < before_start->by ? *past_end : *before_start);
        }
        std::swap(plan.pad.along_near, plan.pad.along_far);
    }

    return plan;
}

} // namespace nozzle_cleaner_lite::custom_wiper
