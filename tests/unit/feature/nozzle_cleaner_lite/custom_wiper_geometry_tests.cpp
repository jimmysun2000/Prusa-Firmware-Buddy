#include <feature/nozzle_cleaner_lite/custom_wiper_geometry.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace nozzle_cleaner_lite;
using namespace nozzle_cleaner_lite::custom_wiper;
using Catch::Approx;

namespace {

constexpr float margin = 0.2f;

/// The XL's travel area as the firmware passes it in
constexpr TravelLimits xl_limits {
    .x_min = -8.0f + margin,
    .x_max = 361.0f - margin,
    .y_min = -9.0f + margin,
    .y_max = 360.0f,
    .z_min = -1.45f,
};

constexpr TravelLimits xl_limits_no_margin {
    .x_min = -8.0f,
    .x_max = 361.0f,
    .y_min = -9.0f,
    .y_max = 360.0f,
    .z_min = -1.45f,
};

Geometry stroke(float start_x, float start_y, float end_x, float end_y, float wipe_z = 2.0f) {
    return Geometry {
        .start_x = start_x,
        .start_y = start_y,
        .end_x = end_x,
        .end_y = end_y,
        .wipe_z = wipe_z,
        .taught = Geometry::taught_start | Geometry::taught_end,
    };
}

/// A front wiper running along X, well inside the travel
Geometry front_wiper() {
    return stroke(100.0f, -5.0f, 140.0f, -5.0f);
}

/// The owner's wiper, taught with a tool of \p teaching_offset_y at machine Y -9
Geometry owners_wiper(float teaching_offset_y) {
    return stroke(155.0f, -9.0f + teaching_offset_y, 205.0f, -9.0f + teaching_offset_y, 1.0f);
}

bool inside(Point p, const TravelLimits &limits, float tolerance = reach_tolerance + 1e-4f) {
    return p.x >= limits.x_min - tolerance && p.x <= limits.x_max + tolerance
        && p.y >= limits.y_min - tolerance && p.y <= limits.y_max + tolerance;
}

/// Every point the plan visits: the pad corners, the waiting point and the whole zigzag
std::vector<Point> visited_points(const ToolPlan &plan, float start_phase) {
    const Pad &pad = plan.pad;
    std::vector<Point> points {
        pad_to_xy(plan.frame, pad.along_near, pad.across_min),
        pad_to_xy(plan.frame, pad.along_near, pad.across_max),
        pad_to_xy(plan.frame, pad.along_far, pad.across_min),
        pad_to_xy(plan.frame, pad.along_far, pad.across_max),
        plan.waiting_point,
    };

    ZigZag zigzag { pad, 1.5f, start_phase };
    const auto start = zigzag.start();
    points.push_back(pad_to_xy(plan.frame, start.along, start.across));
    for (int stroke = 0; stroke < 14; ++stroke) {
        zigzag.begin_stroke(stroke % 2 == 0 ? pad.along_far : pad.along_near);
        while (const auto waypoint = zigzag.next()) {
            points.push_back(pad_to_xy(plan.frame, waypoint->along, waypoint->across));
        }
    }
    return points;
}

/// Small deterministic generator, so that a failure can be reproduced
class Random {
public:
    float uniform(float min, float max) {
        state_ = state_ * 1664525u + 1013904223u;
        return min + (max - min) * static_cast<float>(state_ >> 8) / static_cast<float>(1u << 24);
    }

private:
    uint32_t state_ = 12345;
};

} // namespace

TEST_CASE("custom wiper frame") {
    SECTION("coincident points have no frame") {
        CHECK_FALSE(make_frame({ 10, 10 }, { 10, 10 }).has_value());
    }

    SECTION("NaN has no frame") {
        CHECK_FALSE(make_frame({ std::numeric_limits<float>::quiet_NaN(), 0 }, { 10, 10 }).has_value());
    }

    SECTION("stroke along +X") {
        const auto frame = make_frame({ 100, -5 }, { 140, -5 });
        REQUIRE(frame.has_value());
        CHECK(frame->length == Approx(40));

        const Point end = pad_to_xy(*frame, frame->length, 0);
        CHECK(end.x == Approx(140));
        CHECK(end.y == Approx(-5));

        // Across is to the left of the stroke direction
        const Point left = pad_to_xy(*frame, 0, 1.5f);
        CHECK(left.x == Approx(100));
        CHECK(left.y == Approx(-3.5));
    }

    SECTION("diagonal stroke") {
        const auto frame = make_frame({ 0, 0 }, { 30, 40 });
        REQUIRE(frame.has_value());
        CHECK(frame->length == Approx(50));

        const Point mid = pad_to_xy(*frame, 25, 0);
        CHECK(mid.x == Approx(15));
        CHECK(mid.y == Approx(20));

        const Point across = pad_to_xy(*frame, 0, 5);
        CHECK(across.x == Approx(-4));
        CHECK(across.y == Approx(3));
    }

    SECTION("pad spans the stroke between the given across positions") {
        const auto frame = make_frame({ 0, 0 }, { 0, 30 });
        REQUIRE(frame.has_value());
        const Pad pad = make_pad(*frame, -1.5f, 0.5f);
        CHECK(pad.along_near == 0);
        CHECK(pad.along_far == Approx(30));
        CHECK(pad.across_min == Approx(-1.5));
        CHECK(pad.across_max == Approx(0.5));
    }
}

TEST_CASE("custom wiper tip frame") {
    const Offset offset { .x = 0.5f, .y = -0.25f, .z = 1.0f };
    const Point machine = tip_to_machine({ 10, 20 }, offset);
    CHECK(machine.x == Approx(9.5));
    CHECK(machine.y == Approx(20.25));
    CHECK(tip_to_machine_z(2.0f, offset) == Approx(1.0));
}

TEST_CASE("custom wiper touch temperature") {
    CHECK(touch_max_temp(std::nullopt) == 143);
    CHECK(touch_max_temp(215) == 165);
    CHECK(touch_max_temp(240) == 190);
    CHECK(touch_max_temp(190) == 143);
    CHECK(touch_max_temp(0) == 143);
    CHECK(touch_max_temp(touch_floor) >= wiper_target_temp);
}

TEST_CASE("custom wiper print clearance") {
    // Nothing printed: any valid wipe height
    CHECK(print_clearance_ok(0.0f, 0.5f, wipe_z_min));

    CHECK(print_clearance_ok(0.2f, 0.0f, 2.2f));
    CHECK_FALSE(print_clearance_ok(0.2f, 0.0f, 2.1f));
    CHECK(print_clearance_ok(3.0f, 0.0f, 5.0f));
    CHECK_FALSE(print_clearance_ok(3.1f, 0.0f, 5.0f));

    // The tallest tool offset raises the part's top in the tip frame
    CHECK_FALSE(print_clearance_ok(3.0f, 0.5f, 5.0f));
    CHECK(print_clearance_ok(3.0f, -0.5f, 5.0f));

    CHECK_FALSE(print_clearance_ok(std::numeric_limits<float>::quiet_NaN(), 0.0f, 5.0f));
}

TEST_CASE("custom wiper geometry check") {
    SECTION("a taught front wiper is fine") {
        CHECK(check_geometry(front_wiper(), 3).ok());
    }

    SECTION("both points have to be taught") {
        Geometry g = front_wiper();
        g.taught = Geometry::taught_start;
        CHECK(check_geometry(g, 3).fault == Fault::not_taught);
        g.taught = Geometry::taught_end;
        CHECK(check_geometry(g, 3).fault == Fault::not_taught);
        g.taught = 0;
        CHECK(check_geometry(g, 3).fault == Fault::not_taught);
    }

    SECTION("stroke length") {
        Geometry g = front_wiper();
        g.end_x = g.start_x + 9.9f;
        CHECK(check_geometry(g, 3).fault == Fault::stroke_length);
        g.end_x = g.start_x + 10.0f;
        CHECK(check_geometry(g, 3).ok());
        g.end_x = g.start_x + 100.0f;
        CHECK(check_geometry(g, 3).ok());
        g.end_x = g.start_x + 100.5f;
        CHECK(check_geometry(g, 3).fault == Fault::stroke_length);
        g.end_x = g.start_x;
        CHECK(check_geometry(g, 3).fault == Fault::stroke_length);
    }

    SECTION("width") {
        CHECK(check_geometry(front_wiper(), 0).ok());
        CHECK(check_geometry(front_wiper(), 8).ok());
        CHECK(check_geometry(front_wiper(), 8.5f).fault == Fault::width);
        CHECK(check_geometry(front_wiper(), -1).fault == Fault::width);
        CHECK(check_geometry(front_wiper(), std::numeric_limits<float>::quiet_NaN()).fault == Fault::width);
    }

    SECTION("wipe height") {
        Geometry g = front_wiper();
        g.wipe_z = 0.29f;
        CHECK(check_geometry(g, 3).fault == Fault::wipe_z);
        g.wipe_z = wipe_z_min;
        CHECK(check_geometry(g, 3).ok());
        g.wipe_z = 50.0f;
        CHECK(check_geometry(g, 3).ok());
        g.wipe_z = 50.1f;
        CHECK(check_geometry(g, 3).fault == Fault::wipe_z);
        g.wipe_z = std::numeric_limits<float>::quiet_NaN();
        CHECK(check_geometry(g, 3).fault == Fault::wipe_z);
    }

    SECTION("a corrupted start fails") {
        Geometry g = front_wiper();
        g.start_x = std::numeric_limits<float>::quiet_NaN();
        CHECK(check_geometry(g, 3).fault == Fault::stroke_length);
    }

    SECTION("plan_for_tool refuses the same, naming the tool") {
        Geometry g = front_wiper();
        g.taught = Geometry::taught_start;
        const auto plan = plan_for_tool(g, 3, 2, {}, xl_limits);
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error().fault == Fault::not_taught);
        CHECK(plan.error().tool == 2);
    }
}

TEST_CASE("custom wiper plan") {
    SECTION("a wiper well inside the travel gets the whole centred band") {
        const auto plan = plan_for_tool(front_wiper(), 3, 0, { .z = 0.5f }, xl_limits);
        REQUIRE(plan.has_value());
        CHECK(plan->pad.along_near == 0);
        CHECK(plan->pad.along_far == Approx(40));
        CHECK(plan->pad.across_min == Approx(-1.5));
        CHECK(plan->pad.across_max == Approx(1.5));
        CHECK(plan->waiting_point.x == Approx(90));
        CHECK(plan->waiting_point.y == Approx(-5));
        CHECK(plan->wipe_z == Approx(1.5));
        CHECK(plan->line_shift() == 0);
    }

    SECTION("the plan is in the machine frame of the tool") {
        const auto plan = plan_for_tool(front_wiper(), 3, 0, { .x = 0.5f, .y = -0.25f }, xl_limits);
        REQUIRE(plan.has_value());
        CHECK(plan->frame.origin.x == Approx(99.5));
        CHECK(plan->frame.origin.y == Approx(-4.75));
        CHECK(plan->waiting_point.x == Approx(89.5));
    }

    SECTION("the owner's wiper: each tool wipes as close to the taught line as it reaches") {
        // Taught with a tool of Y offset t; the others differ by +0.3 and +1.4 mm
        for (const float t : { 0.0f, -0.5f, 0.4f }) {
            const Geometry g = owners_wiper(t);

            const auto teaching = plan_for_tool(g, 3, 0, { .y = t }, xl_limits);
            REQUIRE(teaching.has_value());
            CHECK(teaching->pad.across_min == Approx(0.2).margin(1e-4));
            CHECK(teaching->pad.across_max == Approx(1.5));
            CHECK(teaching->line_shift() == Approx(0.2).margin(1e-4));
            CHECK(teaching->waiting_point.x == Approx(145));
            CHECK(teaching->waiting_point.y == Approx(-8.8).margin(1e-4));
            CHECK(teaching->pad.along_near == 0);

            const auto further = plan_for_tool(g, 3, 1, { .y = t + 0.3f }, xl_limits);
            REQUIRE(further.has_value());
            CHECK(further->pad.across_min == Approx(0.5).margin(1e-4));
            CHECK(further->pad.across_max == Approx(1.5));
            CHECK(further->waiting_point.y == Approx(-8.8).margin(1e-4));

            const auto too_far = plan_for_tool(g, 3, 2, { .y = t + 1.4f }, xl_limits);
            REQUIRE_FALSE(too_far.has_value());
            const Problem &problem = too_far.error();
            CHECK(problem.fault == Fault::travel_xy);
            CHECK(problem.tool == 2);
            CHECK(problem.spot == Spot::stroke);
            CHECK(problem.axis == Axis::y);
            CHECK_FALSE(problem.beyond_max);
            CHECK(problem.by == Approx(0.1).margin(1e-4));
            CHECK(problem.limit == Approx(-8.8));
            CHECK(problem.min_width == Approx(3.2).margin(1e-4));
        }
    }

    SECTION("a stroke taught the other way round is cut on the other side of its line") {
        const auto plan = plan_for_tool(stroke(205.0f, -9.0f, 155.0f, -9.0f), 3, 0, {}, xl_limits);
        REQUIRE(plan.has_value());
        CHECK(plan->pad.across_min == Approx(-1.5));
        CHECK(plan->pad.across_max == Approx(-0.2).margin(1e-4));
        CHECK(plan->waiting_point.x == Approx(215));
        CHECK(plan->waiting_point.y == Approx(-8.8).margin(1e-4));
    }

    SECTION("width 0 on the limit line is refused, with the width that would reach") {
        const auto plan = plan_for_tool(owners_wiper(0), 0, 0, {}, xl_limits);
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error().spot == Spot::stroke);
        CHECK(plan.error().by == Approx(0.2).margin(1e-4));
        CHECK(plan.error().min_width == Approx(0.4).margin(1e-4));
    }

    SECTION("width 0 inside the travel strokes straight along the line") {
        const auto plan = plan_for_tool(front_wiper(), 0, 0, {}, xl_limits);
        REQUIRE(plan.has_value());
        CHECK(plan->pad.across_min == 0);
        CHECK(plan->pad.across_max == 0);
    }

    SECTION("without a margin the line on the limit is wiped as taught") {
        const auto narrow = plan_for_tool(owners_wiper(0), 0, 0, {}, xl_limits_no_margin);
        REQUIRE(narrow.has_value());
        CHECK(narrow->pad.across_min == 0);
        CHECK(narrow->pad.across_max == 0);

        const auto wide = plan_for_tool(owners_wiper(0), 3, 0, {}, xl_limits_no_margin);
        REQUIRE(wide.has_value());
        CHECK(wide->pad.across_min == 0);
        CHECK(wide->pad.across_max == Approx(1.5));
        CHECK(wide->waiting_point.y == Approx(-9));
    }

    SECTION("a line just beyond the limit by rounding is wiped on the nearest line reached") {
        const auto rounded = plan_for_tool(stroke(155.0f, -8.805f, 205.0f, -8.805f), 0, 0, {}, xl_limits);
        REQUIRE(rounded.has_value());
        CHECK(rounded->pad.across_min == Approx(0.005).margin(1e-4));
        CHECK(rounded->pad.across_max == rounded->pad.across_min);
        CHECK(inside(pad_to_xy(rounded->frame, 0, rounded->pad.across_min), xl_limits, 1e-4f));

        const auto beyond = plan_for_tool(stroke(155.0f, -8.82f, 205.0f, -8.82f), 0, 0, {}, xl_limits);
        CHECK_FALSE(beyond.has_value());
    }

    SECTION("a slightly rotated stroke at the front limit") {
        const auto plan = plan_for_tool(stroke(155.0f, -9.0f, 205.0f, -8.5f), 3, 0, {}, xl_limits);
        REQUIRE(plan.has_value());
        // The start is the end that limits it
        CHECK(plan->pad.across_min == Approx(0.2).margin(1e-3));
        CHECK(plan->pad.across_max == Approx(1.5));
        for (const Point &p : visited_points(*plan, 0.3f)) {
            CHECK(inside(p, xl_limits));
        }
    }

    SECTION("a diagonal stroke near the front left corner") {
        const auto plan = plan_for_tool(stroke(5.0f, -7.5f, 40.0f, 18.5f), 6, 0, {}, xl_limits);
        REQUIRE(plan.has_value());
        // The start limits the band to about -1.6 .. 3
        CHECK(plan->pad.across_min == Approx(-1.62).margin(0.01));
        CHECK(plan->pad.across_max == Approx(3));
        for (const Point &p : visited_points(*plan, 0.7f)) {
            CHECK(inside(p, xl_limits));
        }
    }

    SECTION("a stroke end beyond the right limit cannot be helped by the width") {
        const auto plan = plan_for_tool(stroke(300.0f, -5.0f, 361.0f, -5.0f), 3, 0, {}, xl_limits);
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error().fault == Fault::travel_xy);
        CHECK(plan.error().spot == Spot::stroke_end);
        CHECK(plan.error().axis == Axis::x);
        CHECK(plan.error().beyond_max);
        CHECK(plan.error().by == Approx(0.2).margin(1e-4));
        CHECK(plan.error().limit == Approx(360.8));
        CHECK(plan.error().min_width == 0);
    }

    SECTION("a start within 10 mm of X min moves the waiting point past the end") {
        const auto plan = plan_for_tool(stroke(0.0f, 100.0f, 40.0f, 100.0f), 3, 0, {}, xl_limits);
        REQUIRE(plan.has_value());
        CHECK(plan->waiting_point.x == Approx(50));
        CHECK(plan->waiting_point.y == Approx(100));
        // The strokes start from the end
        CHECK(plan->pad.along_near == Approx(40));
        CHECK(plan->pad.along_far == 0);

        const auto room_before = plan_for_tool(stroke(2.2f, 100.0f, 40.0f, 100.0f), 3, 0, {}, xl_limits);
        REQUIRE(room_before.has_value());
        CHECK(room_before->waiting_point.x == Approx(-7.8));
        CHECK(room_before->pad.along_near == 0);
    }

    SECTION("no room for the waiting point at either end") {
        // Diagonal from near the left edge down to near the front edge
        const auto plan = plan_for_tool(stroke(-5.0f, 30.0f, 22.0f, -6.0f), 3, 0, {}, xl_limits);
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error().spot == Spot::waiting_point);
        // The smaller shortfall: X -11 before the start against Y -14 past the end
        CHECK(plan.error().axis == Axis::x);
        CHECK(plan.error().by == Approx(3.2).margin(1e-3));
    }

    SECTION("the machine Z has to stay above the Z travel limit") {
        const Geometry g = stroke(100.0f, -5.0f, 140.0f, -5.0f, 0.5f);
        const auto low = plan_for_tool(g, 3, 1, { .z = 2.0f }, xl_limits);
        REQUIRE_FALSE(low.has_value());
        CHECK(low.error().fault == Fault::travel_z);
        CHECK(low.error().tool == 1);
        CHECK(low.error().spot == Spot::height);
        CHECK(low.error().by == Approx(0.05).margin(1e-4));
        CHECK(plan_for_tool(g, 3, 1, { .z = 1.9f }, xl_limits).has_value());
    }

    SECTION("a corrupted offset is refused") {
        CHECK_FALSE(plan_for_tool(front_wiper(), 3, 0, { .x = std::numeric_limits<float>::quiet_NaN() }, xl_limits).has_value());
        CHECK_FALSE(plan_for_tool(front_wiper(), 3, 0, { .z = std::numeric_limits<float>::quiet_NaN() }, xl_limits).has_value());
    }
}

TEST_CASE("custom wiper plan stays inside the travel for any tool offset") {
    const std::array<Geometry, 6> geometries {
        owners_wiper(0),
        stroke(205.0f, -8.6f, 155.0f, -8.4f),
        stroke(-8.0f, 50.0f, -8.0f, 120.0f),
        stroke(361.0f, 300.0f, 355.0f, 359.0f),
        stroke(-4.0f, 40.0f, 30.0f, -7.0f),
        stroke(1.0f, 200.0f, 60.0f, 200.0f),
    };

    Random random;
    int planned = 0;
    int refused = 0;
    for (const Geometry &g : geometries) {
        for (float width = 0; width <= width_max; width += 0.5f) {
            for (int i = 0; i < 40; ++i) {
                const Offset offset { .x = random.uniform(-1, 1), .y = random.uniform(-1, 1), .z = random.uniform(-2, 1.45f) };
                const auto plan = plan_for_tool(g, width, 0, offset, xl_limits);
                if (!plan.has_value()) {
                    ++refused;
                    CHECK((plan.error().fault == Fault::travel_xy || plan.error().fault == Fault::travel_z));
                    CHECK(plan.error().by >= 0);

                    // Refused only if no across position in the band lets both ends reach
                    if (plan.error().fault == Fault::travel_xy && plan.error().spot != Spot::waiting_point) {
                        const Point start = tip_to_machine({ g.start_x, g.start_y }, offset);
                        const Point end = tip_to_machine({ g.end_x, g.end_y }, offset);
                        const auto frame = make_frame(start, end);
                        REQUIRE(frame.has_value());
                        for (float a = -width / 2; a <= width / 2; a += 0.002f) {
                            const bool both = inside(pad_to_xy(*frame, 0, a), xl_limits, -1e-3f) && inside(pad_to_xy(*frame, frame->length, a), xl_limits, -1e-3f);
                            CHECK_FALSE(both);
                        }
                    }
                    continue;
                }

                ++planned;
                // Never outside the centred band
                CHECK(plan->pad.across_min >= -width / 2 - reach_tolerance);
                CHECK(plan->pad.across_max <= width / 2 + reach_tolerance);
                CHECK(plan->pad.across_min <= plan->pad.across_max);
                for (const Point &p : visited_points(*plan, random.uniform(0, 1))) {
                    CHECK(inside(p, xl_limits));
                }
            }
        }
    }
    // Both outcomes have to be covered for the checks above to mean anything
    CHECK(planned > 100);
    CHECK(refused > 100);
}
