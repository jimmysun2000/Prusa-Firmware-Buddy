#include <array>
#include <cmath>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/types.h>

#include <tool_offset_partial.hpp>

using namespace tool_offset_partial;

namespace {

constexpr double ALLOWED_ERROR_MM { 0.0001 };

const xyz_pos_t NO_OFFSET { 0.0f, 0.0f, 0.0f };

void check_equal(const xyz_pos_t &actual, const xyz_pos_t &expected) {
    INFO("actual " << actual.x << " " << actual.y << " " << actual.z);
    INFO("expected " << expected.x << " " << expected.y << " " << expected.z);
    CHECK(actual.x == Catch::Approx(expected.x).margin(ALLOWED_ERROR_MM));
    CHECK(actual.y == Catch::Approx(expected.y).margin(ALLOWED_ERROR_MM));
    CHECK(actual.z == Catch::Approx(expected.z).margin(ALLOWED_ERROR_MM));
}

/// What the full calibration stores: -c_t for every tool, then the reference tool's value subtracted from all
xyz_pos_t full_calibration_offset(const MachinePosXYZ &c_ref, const MachinePosXYZ &c_tool) {
    const xyz_pos_t ref_raw { .x = -c_ref.x, .y = -c_ref.y, .z = -c_ref.z };
    const xyz_pos_t tool_raw { .x = -c_tool.x, .y = -c_tool.y, .z = -c_tool.z };
    return tool_raw - ref_raw;
}

constexpr size_t TOOL_COUNT = 5;
constexpr size_t REF = 0;

/// Pin centres measured with each tool in one session
using Centres = std::array<MachinePosXYZ, TOOL_COUNT>;
using Offsets = std::array<xyz_pos_t, TOOL_COUNT>;

Offsets full_calibration(const Centres &centres) {
    Offsets result;
    for (size_t t = 0; t < TOOL_COUNT; ++t) {
        result[t] = full_calibration_offset(centres[REF], centres[t]);
    }
    return result;
}

const Centres session_centres { {
    { .x = 180.00f, .y = 180.00f, .z = 9.00f },
    { .x = 180.31f, .y = 179.82f, .z = 8.64f },
    { .x = 179.55f, .y = 180.47f, .z = 9.21f },
    { .x = 180.12f, .y = 180.05f, .z = 8.93f },
    { .x = 179.91f, .y = 179.68f, .z = 9.38f },
} };

/// The same tools in a later session, with the pin screwed in at a slightly different place
Centres shifted(const Centres &centres, const MachinePosXYZ &shift) {
    Centres result;
    for (size_t t = 0; t < TOOL_COUNT; ++t) {
        result[t] = centres[t] + shift;
    }
    return result;
}

} // namespace

TEST_CASE("tool_offset_partial: sign convention matches the full calibration") {
    const MachinePosXYZ c_ref { .x = 180.0f, .y = 180.0f, .z = 9.0f };
    const MachinePosXYZ c_tool { .x = 180.25f, .y = 179.9f, .z = 8.7f };

    const xyz_pos_t offset = single_tool_offset(NO_OFFSET, c_ref, c_tool);

    check_equal(offset, { .x = -0.25f, .y = 0.1f, .z = 0.3f });
    check_equal(offset, full_calibration_offset(c_ref, c_tool));
}

TEST_CASE("tool_offset_partial: single tool reproduces an unchanged tool") {
    const Offsets stored = full_calibration(session_centres);
    const Centres later = shifted(session_centres, { .x = 0.4f, .y = -0.3f, .z = 0.05f });

    for (size_t t = 1; t < TOOL_COUNT; ++t) {
        INFO("tool " << t);
        check_equal(single_tool_offset(stored[REF], later[REF], later[t]), stored[t]);
    }

    // The reference tool measured against itself has no offset
    check_equal(single_tool_offset(NO_OFFSET, later[REF], later[REF]), NO_OFFSET);
}

TEST_CASE("tool_offset_partial: single tool picks up a changed tool") {
    const Offsets stored = full_calibration(session_centres);

    // Tool 2 got a new nozzle
    Centres after_change = session_centres;
    after_change[2] = { .x = 179.80f, .y = 180.10f, .z = 8.80f };
    const Centres later = shifted(after_change, { .x = -0.2f, .y = 0.1f, .z = 0.0f });

    const xyz_pos_t new_offset = single_tool_offset(stored[REF], later[REF], later[2]);
    check_equal(new_offset, full_calibration(after_change)[2]);
}

TEST_CASE("tool_offset_partial: rebase reconstructs a full calibration") {
    const Offsets stored = full_calibration(session_centres);

    // The reference tool got a new nozzle, the others are unchanged
    Centres after_change = session_centres;
    after_change[REF] = { .x = 180.42f, .y = 179.71f, .z = 9.33f };
    const Centres later = shifted(after_change, { .x = 0.15f, .y = 0.25f, .z = -0.1f });
    const Offsets expected = full_calibration(after_change);

    for (size_t kept = 1; kept < TOOL_COUNT; ++kept) {
        INFO("kept tool " << kept);
        const xyz_pos_t delta = rebase_delta(stored[REF], later[REF], later[kept], stored[kept]);

        for (size_t t = 1; t < TOOL_COUNT; ++t) {
            INFO("tool " << t);
            check_equal(stored[t] + delta, expected[t]);
        }

        // The kept tool ends up exactly at its fresh measurement
        check_equal(stored[kept] + delta, single_tool_offset(stored[REF], later[REF], later[kept]));
    }
}

TEST_CASE("tool_offset_partial: non-zero reference offset keeps the frame") {
    const xyz_pos_t ref_old { .x = 0.12f, .y = -0.05f, .z = 0.2f };
    Offsets stored = full_calibration(session_centres);
    for (auto &offset : stored) {
        offset += ref_old;
    }
    const Centres later = shifted(session_centres, { .x = 0.3f, .y = 0.0f, .z = -0.2f });

    SECTION("single tool") {
        const xyz_pos_t new_offset = single_tool_offset(stored[REF], later[REF], later[3]);
        check_equal(new_offset, stored[3]);
        check_equal(new_offset - ref_old, full_calibration_offset(later[REF], later[3]));
    }

    SECTION("rebase") {
        Centres after_change = later;
        after_change[REF] = later[REF] + MachinePosXYZ { .x = -0.3f, .y = 0.2f, .z = 0.1f };
        const xyz_pos_t delta = rebase_delta(stored[REF], after_change[REF], after_change[1], stored[1]);

        for (size_t t = 1; t < TOOL_COUNT; ++t) {
            INFO("tool " << t);
            check_equal(stored[t] + delta - ref_old, full_calibration_offset(after_change[REF], after_change[t]));
        }
    }
}

TEST_CASE("tool_offset_partial: limits are inclusive") {
    CHECK(within_limits(-1.0f, -1.0f, 1.0f));
    CHECK(within_limits(1.0f, -1.0f, 1.0f));
    CHECK(within_limits(0.0f, -1.0f, 1.0f));
    CHECK(within_limits(-2.0f, -2.0f, 1.45f));
    CHECK(within_limits(1.45f, -2.0f, 1.45f));

    CHECK_FALSE(within_limits(std::nextafter(1.0f, 2.0f), -1.0f, 1.0f));
    CHECK_FALSE(within_limits(std::nextafter(-1.0f, -2.0f), -1.0f, 1.0f));
    CHECK_FALSE(within_limits(std::nextafter(1.45f, 2.0f), -2.0f, 1.45f));
    CHECK_FALSE(within_limits(std::numeric_limits<float>::quiet_NaN(), -1.0f, 1.0f));
    CHECK_FALSE(within_limits(std::numeric_limits<float>::infinity(), -1.0f, 1.0f));
}
