#include <feature/chamber_filtration/overtemp_vent.hpp>

#include <catch2/catch_test_macros.hpp>

using buddy::OvertempVent;

TEST_CASE("overtemp_vent::starts_above_limit") {
    OvertempVent vent;
    CHECK(!vent.update(70, 75));
    CHECK(!vent.update(75, 75));
    CHECK(vent.update(75.1f, 75));
    CHECK(vent.is_venting());
}

TEST_CASE("overtemp_vent::hysteresis") {
    OvertempVent vent;
    REQUIRE(vent.update(76, 75));

    // Keeps venting down to 3 C below the limit
    CHECK(vent.update(75, 75));
    CHECK(vent.update(73, 75));
    CHECK(vent.update(72, 75));
    CHECK(!vent.update(71.9f, 75));

    // And does not restart until above the limit again
    CHECK(!vent.update(74, 75));
    CHECK(!vent.update(75, 75));
    CHECK(vent.update(76, 75));
}

TEST_CASE("overtemp_vent::unknown_stops") {
    OvertempVent vent;
    REQUIRE(vent.update(80, 75));
    CHECK(!vent.update(std::nullopt, 75));
    CHECK(!vent.update(74, 75));

    REQUIRE(vent.update(80, 75));
    CHECK(!vent.update(80, std::nullopt));
    CHECK(!vent.update(std::nullopt, std::nullopt));
}

TEST_CASE("overtemp_vent::limit_change") {
    OvertempVent vent;
    CHECK(!vent.update(60, 75));
    // A tool with a lower maximum comes into play
    CHECK(vent.update(60, 38));
    CHECK(vent.update(36, 38));
    CHECK(!vent.update(34, 38));
}
