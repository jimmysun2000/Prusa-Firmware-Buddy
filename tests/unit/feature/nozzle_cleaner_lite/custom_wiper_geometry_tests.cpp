#include <feature/nozzle_cleaner_lite/custom_wiper_geometry.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

using namespace nozzle_cleaner_lite;
using namespace nozzle_cleaner_lite::custom_wiper;
using Catch::Approx;

namespace {

/// The XL's travel area as the firmware passes it in
constexpr TravelLimits xl_limits {
    .x_min = -8.0f + 0.4f,
    .x_max = 361.0f - 0.4f,
    .y_min = -9.0f + 0.4f,
    .y_max = 360.0f,
    .z_min = -1.45f,
};

constexpr std::array<ToolOffset, 1> no_offset { ToolOffset {} };

/// A front wiper running along X, the way the owner's is mounted
Geometry front_wiper() {
    return Geometry {
        .start_x = 100.0f,
        .start_y = -5.0f,
        .end_x = 140.0f,
        .end_y = -5.0f,
        .wipe_z = 2.0f,
        .taught = Geometry::taught_start | Geometry::taught_end,
    };
}

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

        const Point staging = staging_point(*frame);
        CHECK(staging.x == Approx(90));
        CHECK(staging.y == Approx(-5));
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

        const Point staging = staging_point(*frame);
        CHECK(staging.x == Approx(-6));
        CHECK(staging.y == Approx(-8));
    }

    SECTION("pad spans the stroke with the band centred on it") {
        const auto frame = make_frame({ 0, 0 }, { 0, 30 });
        REQUIRE(frame.has_value());
        const Pad pad = make_pad(*frame, 3);
        CHECK(pad.along_near == 0);
        CHECK(pad.along_far == Approx(30));
        CHECK(pad.across_min == Approx(-1.5));
        CHECK(pad.across_max == Approx(1.5));
    }

    SECTION("zigzag over the pad stays in the band and turns at the taught points") {
        const auto frame = make_frame({ 100, -5 }, { 140, -5 });
        REQUIRE(frame.has_value());
        const Pad pad = make_pad(*frame, 3);
        ZigZag zigzag { pad, 1.5f, 0.3f };

        for (int stroke = 0; stroke < 14; ++stroke) {
            const float along_to = (stroke % 2 == 0) ? pad.along_far : pad.along_near;
            zigzag.begin_stroke(along_to);
            std::optional<ZigZag::Waypoint> last;
            while (const auto waypoint = zigzag.next()) {
                const Point xy = pad_to_xy(*frame, waypoint->along, waypoint->across);
                CHECK(xy.x >= Approx(100).margin(1e-4));
                CHECK(xy.x <= Approx(140).margin(1e-4));
                CHECK(xy.y >= Approx(-6.5).margin(1e-4));
                CHECK(xy.y <= Approx(-3.5).margin(1e-4));
                last = waypoint;
            }
            REQUIRE(last.has_value());
            CHECK(pad_to_xy(*frame, last->along, 0).x == Approx(stroke % 2 == 0 ? 140 : 100));
        }
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

TEST_CASE("custom wiper validation") {
    SECTION("a taught front wiper is valid") {
        CHECK(validate(front_wiper(), 3, no_offset, xl_limits).ok());
    }

    SECTION("both points have to be taught") {
        Geometry g = front_wiper();
        g.taught = Geometry::taught_start;
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::not_taught);
        g.taught = Geometry::taught_end;
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::not_taught);
        g.taught = 0;
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::not_taught);
    }

    SECTION("stroke length") {
        Geometry g = front_wiper();
        g.end_x = g.start_x + 9.9f;
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::stroke_length);
        g.end_x = g.start_x + 10.0f;
        CHECK(validate(g, 3, no_offset, xl_limits).ok());
        g.end_x = g.start_x + 100.0f;
        CHECK(validate(g, 3, no_offset, xl_limits).ok());
        g.end_x = g.start_x + 100.5f;
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::stroke_length);
        g.end_x = g.start_x;
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::stroke_length);
    }

    SECTION("width") {
        Geometry g = front_wiper();
        g.start_y = g.end_y = 10.0f;
        CHECK(validate(g, 0, no_offset, xl_limits).ok());
        CHECK(validate(g, 8, no_offset, xl_limits).ok());
        CHECK(validate(g, 8.5f, no_offset, xl_limits).fault == Fault::width);
        CHECK(validate(g, -1, no_offset, xl_limits).fault == Fault::width);
        CHECK(validate(g, std::numeric_limits<float>::quiet_NaN(), no_offset, xl_limits).fault == Fault::width);
        // A wider band reaches further: at Y -5 an 8 mm band ends at Y -9
        CHECK(validate(front_wiper(), 8, no_offset, xl_limits).fault == Fault::travel_xy);
    }

    SECTION("wipe height") {
        Geometry g = front_wiper();
        g.wipe_z = 0.29f;
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::wipe_z);
        g.wipe_z = wipe_z_min;
        CHECK(validate(g, 3, no_offset, xl_limits).ok());
        g.wipe_z = 50.0f;
        CHECK(validate(g, 3, no_offset, xl_limits).ok());
        g.wipe_z = 50.1f;
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::wipe_z);
        g.wipe_z = std::numeric_limits<float>::quiet_NaN();
        CHECK(validate(g, 3, no_offset, xl_limits).fault == Fault::wipe_z);
    }

    SECTION("the band edges have to stay inside the travel") {
        Geometry g = front_wiper();
        // Band edge at -7.0 - 1.5 = -8.5 is inside -8.6, at -8.7 it is not
        g.start_y = g.end_y = -7.0f;
        CHECK(validate(g, 3, no_offset, xl_limits).ok());
        g.start_y = g.end_y = -7.2f;
        const Problem problem = validate(g, 3, no_offset, xl_limits);
        CHECK(problem.fault == Fault::travel_xy);
        CHECK(problem.spot == Spot::stroke_start);
        CHECK(problem.axis == Axis::y);
        CHECK_FALSE(problem.beyond_max);
        CHECK(problem.by == Approx(0.1));
        CHECK(problem.limit == Approx(-8.6));
    }

    SECTION("the staging point has to stay inside the travel") {
        Geometry g = front_wiper();
        g.start_x = 2.0f;
        g.end_x = 40.0f;
        // Staging at X -8 is outside -7.6
        const Problem problem = validate(g, 3, no_offset, xl_limits);
        CHECK(problem.fault == Fault::travel_xy);
        CHECK(problem.spot == Spot::waiting_point);
        CHECK(problem.axis == Axis::x);
        CHECK_FALSE(problem.beyond_max);
        CHECK(problem.by == Approx(0.4));
        g.start_x = 3.0f;
        CHECK(validate(g, 3, no_offset, xl_limits).ok());
    }

    SECTION("a stroke end beyond the right limit") {
        Geometry g = front_wiper();
        g.start_x = 300.0f;
        g.end_x = 361.0f;
        const Problem problem = validate(g, 3, no_offset, xl_limits);
        CHECK(problem.fault == Fault::travel_xy);
        CHECK(problem.spot == Spot::stroke_end);
        CHECK(problem.axis == Axis::x);
        CHECK(problem.beyond_max);
        CHECK(problem.by == Approx(0.4).margin(1e-4));
        CHECK(problem.limit == Approx(360.6));
    }

    SECTION("every tool's machine frame has to fit") {
        Geometry g = front_wiper();
        g.start_y = g.end_y = -7.0f;
        const std::array<ToolOffset, 2> offsets { ToolOffset { .tool = 0 }, ToolOffset { .tool = 3, .offset = { .y = 0.2f } } };
        // The second tool needs machine Y 0.2 lower than the tip, which is outside
        const Problem problem = validate(g, 3, offsets, xl_limits);
        CHECK(problem.fault == Fault::travel_xy);
        CHECK(problem.tool == 3);
        CHECK(problem.by == Approx(0.1));
    }

    SECTION("the owner's wiper at the front limit fails with the first tool") {
        const Geometry g {
            .start_x = 155.0f,
            .start_y = -9.0f,
            .end_x = 205.0f,
            .end_y = -9.0f,
            .wipe_z = 1.0f,
            .taught = Geometry::taught_start | Geometry::taught_end,
        };
        const Problem problem = validate(g, 3, no_offset, xl_limits);
        CHECK(problem.fault == Fault::travel_xy);
        CHECK(problem.tool == 0);
        CHECK(problem.spot == Spot::stroke_start);
        CHECK(problem.axis == Axis::y);
        CHECK_FALSE(problem.beyond_max);
        // The front band edge at -10.5 against -8.6
        CHECK(problem.by == Approx(1.9));
    }

    SECTION("the machine Z has to stay above the Z travel limit") {
        Geometry g = front_wiper();
        g.wipe_z = 0.5f;
        const std::array<ToolOffset, 2> offsets { ToolOffset {}, ToolOffset { .tool = 1, .offset = { .z = 2.0f } } };
        const Problem problem = validate(g, 3, offsets, xl_limits);
        CHECK(problem.fault == Fault::travel_z);
        CHECK(problem.tool == 1);
        CHECK(problem.spot == Spot::height);
        CHECK(problem.by == Approx(0.05));
        const std::array<ToolOffset, 2> fine_offsets { ToolOffset {}, ToolOffset { .tool = 1, .offset = { .z = 1.9f } } };
        CHECK(validate(g, 3, fine_offsets, xl_limits).ok());
    }

    SECTION("a corrupted start fails") {
        Geometry g = front_wiper();
        g.start_x = std::numeric_limits<float>::quiet_NaN();
        CHECK_FALSE(validate(g, 3, no_offset, xl_limits).ok());
    }

    SECTION("a corrupted offset fails") {
        const std::array<ToolOffset, 1> offsets { ToolOffset { .offset = { .x = std::numeric_limits<float>::quiet_NaN() } } };
        CHECK(validate(front_wiper(), 3, offsets, xl_limits).fault == Fault::travel_xy);
    }
}
