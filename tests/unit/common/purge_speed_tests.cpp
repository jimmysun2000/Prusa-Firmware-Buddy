/// @file
#include <catch2/catch_test_macros.hpp>
#include <purge_speed.hpp>

#include <limits>

namespace {

constexpr uint8_t lowest = 30;
constexpr uint8_t highest = 80;

constexpr uint8_t adjusted(uint8_t value, int knob_diff) {
    return purge_speed::adjusted(value, knob_diff, lowest, highest);
}

static_assert(adjusted(45, 1) == 46, "usable at compile time");

} // namespace

TEST_CASE("purge_speed: one detent is 0.1 mm/s") {
    CHECK(adjusted(45, 1) == 46);
    CHECK(adjusted(45, -1) == 44);
    CHECK(adjusted(45, 0) == 45);
}

TEST_CASE("purge_speed: fast turns move by the whole diff") {
    CHECK(adjusted(45, 3) == 48);
    CHECK(adjusted(45, -3) == 42);
    CHECK(adjusted(45, 5) == 50);
    CHECK(adjusted(45, -5) == 40);
    CHECK(adjusted(45, 50) == highest);
    CHECK(adjusted(45, -50) == lowest);
}

TEST_CASE("purge_speed: stops at both ends") {
    CHECK(adjusted(highest, 1) == highest);
    CHECK(adjusted(lowest, -1) == lowest);
    CHECK(adjusted(79, 5) == highest);
    CHECK(adjusted(31, -5) == lowest);
    CHECK(adjusted(45, std::numeric_limits<int>::max()) == highest);
    CHECK(adjusted(45, std::numeric_limits<int>::min()) == lowest);
}

TEST_CASE("purge_speed: a start value outside the range moves from the end it is shown as") {
    CHECK(adjusted(0, 0) == lowest);
    CHECK(adjusted(0, 1) == lowest + 1);
    CHECK(adjusted(0, -1) == lowest);
    CHECK(adjusted(255, 0) == highest);
    CHECK(adjusted(255, -1) == highest - 1);
    CHECK(adjusted(255, 1) == highest);
}
