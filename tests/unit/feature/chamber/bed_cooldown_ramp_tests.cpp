#include <feature/chamber/bed_cooldown_ramp.hpp>

#include <catch2/catch_test_macros.hpp>

using buddy::BedCooldownRamp;

TEST_CASE("bed_cooldown_ramp::starts_at_start_temperature") {
    const BedCooldownRamp ramp(100, 10);
    CHECK(ramp.bed_target(0) == 100);
    CHECK(ramp.bed_target(59) == 100);
    CHECK(ramp.bed_target(60) == 99);
}

TEST_CASE("bed_cooldown_ramp::one_degree_per_minute") {
    const BedCooldownRamp ramp(100, 10);
    CHECK(ramp.bed_target(10 * 60) == 90);
    CHECK(ramp.bed_target(49 * 60) == 51);
    CHECK(ramp.bed_target(50 * 60 - 1) == 51);

    // Reaching the end temperature switches the bed off
    CHECK(ramp.bed_target(50 * 60) == 0);
    CHECK(ramp.bed_target(BedCooldownRamp::max_duration_s) == 0);
    CHECK(ramp.ramp_duration_s() == 50 * 60);
}

TEST_CASE("bed_cooldown_ramp::slow_and_fast_rates") {
    const BedCooldownRamp slow(100, 5);
    CHECK(slow.bed_target(120) == 99);
    CHECK(slow.bed_target(119) == 100);
    CHECK(slow.ramp_duration_s() == 100 * 60);
    // The time limit comes first, the bed is still at 70 C then
    CHECK(slow.bed_target(BedCooldownRamp::max_duration_s - 1) == 71);

    const BedCooldownRamp fast(100, 30);
    CHECK(fast.bed_target(20) == 99);
    CHECK(fast.bed_target(19) == 100);
    CHECK(fast.ramp_duration_s() == 1000);
    CHECK(fast.bed_target(999) == 51);
    CHECK(fast.bed_target(1000) == 0);
}

TEST_CASE("bed_cooldown_ramp::fractional_rate") {
    // 1.3 C/min: whole degrees, never ahead of the rate
    const BedCooldownRamp ramp(100, 13);
    CHECK(ramp.bed_target(46) == 100);
    CHECK(ramp.bed_target(47) == 99);
    CHECK(ramp.bed_target(600) == 87);
    CHECK(ramp.ramp_duration_s() == (50 * 600 + 12) / 13);
    CHECK(ramp.bed_target(ramp.ramp_duration_s() - 1) == 51);
    CHECK(ramp.bed_target(ramp.ramp_duration_s()) == 0);
}

TEST_CASE("bed_cooldown_ramp::never_rises") {
    for (uint8_t rate = BedCooldownRamp::min_rate; rate <= BedCooldownRamp::max_rate; rate++) {
        const BedCooldownRamp ramp(110, rate);
        int16_t previous = ramp.bed_target(0);
        CHECK(previous == 110);
        for (uint32_t t = 1; t <= BedCooldownRamp::max_duration_s; t++) {
            const int16_t target = ramp.bed_target(t);
            CHECK(target <= previous);
            CHECK((target == 0 || target > BedCooldownRamp::end_temperature));
            previous = target;
        }
    }
}

TEST_CASE("bed_cooldown_ramp::rate_is_clamped") {
    CHECK(BedCooldownRamp(100, 0).rate() == BedCooldownRamp::min_rate);
    CHECK(BedCooldownRamp(100, 1).rate() == BedCooldownRamp::min_rate);
    CHECK(BedCooldownRamp(100, 200).rate() == BedCooldownRamp::max_rate);
    CHECK(BedCooldownRamp(100, 17).rate() == 17);
}

TEST_CASE("bed_cooldown_ramp::start_at_or_below_end") {
    for (const int16_t start : { int16_t { 50 }, int16_t { 49 }, int16_t { 0 }, int16_t { -5 } }) {
        const BedCooldownRamp ramp(start, 10);
        CHECK(ramp.bed_target(0) == 0);
        CHECK(ramp.ramp_duration_s() == 0);
        CHECK(ramp.minutes_left(0) == 0);
    }

    const BedCooldownRamp just_above(51, 10);
    CHECK(just_above.bed_target(0) == 51);
    CHECK(just_above.bed_target(59) == 51);
    CHECK(just_above.bed_target(60) == 0);
}

TEST_CASE("bed_cooldown_ramp::minutes_left") {
    const BedCooldownRamp ramp(100, 10);
    CHECK(ramp.minutes_left(0) == 50);
    CHECK(ramp.minutes_left(1) == 50);
    CHECK(ramp.minutes_left(60) == 49);
    CHECK(ramp.minutes_left(50 * 60 - 1) == 1);
    CHECK(ramp.minutes_left(50 * 60) == 0);

    // Limited by the time limit
    const BedCooldownRamp slow(100, 5);
    CHECK(slow.minutes_left(0) == 60);
    CHECK(slow.minutes_left(BedCooldownRamp::max_duration_s - 1) == 1);
    CHECK(slow.minutes_left(BedCooldownRamp::max_duration_s) == 0);
}

TEST_CASE("bed_cooldown_ramp::huge_elapsed_time") {
    const BedCooldownRamp ramp(110, 30);
    CHECK(ramp.bed_target(UINT32_MAX) == 0);
    CHECK(ramp.minutes_left(UINT32_MAX) == 0);
}
