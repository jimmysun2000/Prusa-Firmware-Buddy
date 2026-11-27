#include <gui/maintenance_reminder_math.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace maintenance_reminders;

namespace {
constexpr uint32_t u32_max = std::numeric_limits<uint32_t>::max();
constexpr uint32_t seconds_per_hour = 3600;
constexpr uint32_t mm_per_m = 1000;
} // namespace

TEST_CASE("maintenance_reminders::usage") {
    CHECK(usage(1500, 1000) == 500);
    CHECK(usage(1000, 1000) == 0);
    CHECK(usage(0, 0) == 0);
    CHECK(usage(u32_max, 0) == u32_max);

    SECTION("counter below the baseline counts as no usage") {
        CHECK(usage(999, 1000) == 0);
        CHECK(usage(0, u32_max) == 0);
    }
}

TEST_CASE("maintenance_reminders::is_due") {
    SECTION("Off is never due") {
        CHECK_FALSE(is_due(0, 0, 1));
        CHECK_FALSE(is_due(u32_max, 0, 1));
        CHECK_FALSE(is_due(u32_max, 0, seconds_per_hour));
    }

    SECTION("due from the interval on") {
        CHECK_FALSE(is_due(1999, 2000, 1));
        CHECK(is_due(2000, 2000, 1));
        CHECK(is_due(2041, 2000, 1));
    }

    SECTION("interval in setting units, usage in counting units") {
        // 2000 m of filament, counted in mm
        CHECK_FALSE(is_due(1'999'999, 2000, mm_per_m));
        CHECK(is_due(2'000'000, 2000, mm_per_m));
        // 500 h of printing, counted in s
        CHECK_FALSE(is_due(500 * seconds_per_hour - 1, 500, seconds_per_hour));
        CHECK(is_due(500 * seconds_per_hour, 500, seconds_per_hour));
    }

    SECTION("intervals beyond the counter range saturate") {
        CHECK(interval_in_counts(2'000'000, seconds_per_hour) == u32_max);
        CHECK_FALSE(is_due(u32_max - 1, 2'000'000, seconds_per_hour));
        CHECK(is_due(u32_max, 2'000'000, seconds_per_hour));
    }

    SECTION("after a statistics reset below the baseline, nothing is due") {
        CHECK_FALSE(is_due(usage(100, 5000), 1, 1));
    }
}

TEST_CASE("maintenance_reminders::postponed_until") {
    CHECK(postponed_until(2041, 2000, 1) == 2241);
    CHECK(postponed_until(2'041'000, 2000, mm_per_m) == 2'241'000);

    SECTION("at least one counting unit") {
        CHECK(postponed_until(10, 5, 1) == 11);
    }

    SECTION("saturates at the counter range") {
        CHECK(postponed_until(u32_max - 5, 2000, 1) == u32_max);
        CHECK(postponed_until(u32_max, 1, 1) == u32_max);
    }
}

TEST_CASE("maintenance_reminders::starts_counting") {
    SECTION("Off to a value starts anew") {
        CHECK(starts_counting(0, 2000));

        // The baseline is then set to the current counter: nothing used, nothing due
        const uint32_t counter = 123'456;
        const uint32_t baseline = counter;
        CHECK(usage(counter, baseline) == 0);
        CHECK_FALSE(is_due(usage(counter, baseline), 2000, 1));
    }

    SECTION("other changes keep the counting") {
        CHECK_FALSE(starts_counting(2000, 3000));
        CHECK_FALSE(starts_counting(2000, 0));
        CHECK_FALSE(starts_counting(0, 0));
    }
}

TEST_CASE("maintenance_reminders::date_counter") {
    SECTION("a clock that was never set is unknown") {
        CHECK(date_counter(-1) == 0);
        CHECK(date_counter(0) == 0);
        CHECK(date_counter(first_valid_clock - 1) == 0);
    }

    SECTION("a known date is the counter") {
        CHECK(date_counter(first_valid_clock) == first_valid_clock);
        CHECK(date_counter(1'795'000'000) == 1'795'000'000);
        CHECK(date_counter(u32_max) == u32_max);
    }

    SECTION("beyond the counter range is unknown") {
        CHECK(date_counter(int64_t { u32_max } + 1) == 0);
    }
}

TEST_CASE("maintenance_reminders::date_usage") {
    constexpr uint32_t start = 1'780'000'000;

    SECTION("time since the baseline") {
        CHECK(date_usage(start + 100, start) == 100);
        CHECK(date_usage(start, start) == 0);
    }

    SECTION("unknown clock: no usage, never due") {
        CHECK(date_usage(0, start) == 0);
        CHECK_FALSE(is_due(date_usage(0, start), 6, seconds_per_month));
    }

    SECTION("pending baseline: no usage, never due") {
        CHECK(date_usage(start, 0) == 0);
        CHECK_FALSE(is_due(date_usage(start, 0), 6, seconds_per_month));
        CHECK_FALSE(is_due(date_usage(u32_max, 0), 1, seconds_per_month));
    }

    SECTION("clock set back before the baseline: no usage") {
        CHECK(date_usage(start - 1, start) == 0);
        CHECK_FALSE(is_due(date_usage(start - 86'400, start), 1, seconds_per_month));
    }

    SECTION("due after the interval in months") {
        CHECK_FALSE(is_due(date_usage(start + 6 * seconds_per_month - 1, start), 6, seconds_per_month));
        CHECK(is_due(date_usage(start + 6 * seconds_per_month, start), 6, seconds_per_month));
        // The longest interval the menu offers fits the counter
        CHECK(interval_in_counts(24, seconds_per_month) == 24 * seconds_per_month);
    }
}

TEST_CASE("maintenance_reminders::needs_date_stamp") {
    constexpr uint32_t now = 1'780'000'000;

    CHECK(needs_date_stamp(now, 0));
    CHECK_FALSE(needs_date_stamp(now, now - 100));

    SECTION("never while the clock is unknown") {
        CHECK_FALSE(needs_date_stamp(0, 0));
        CHECK_FALSE(needs_date_stamp(0, now));
    }

    SECTION("the stamped baseline counts from then on") {
        const uint32_t baseline = now;
        CHECK_FALSE(needs_date_stamp(now, baseline));
        CHECK(date_usage(now, baseline) == 0);
    }
}

TEST_CASE("maintenance_reminders::first_row_to_default") {
    SECTION("first start: every row") {
        CHECK(first_row_to_default(0, 5) == 0);
    }

    SECTION("a row appended by a later firmware") {
        CHECK(first_row_to_default(4, 5) == 4);
    }

    SECTION("all rows done") {
        CHECK(first_row_to_default(5, 5) == 5);
    }

    SECTION("stored by a firmware with more rows") {
        CHECK(first_row_to_default(7, 5) == 5);
        CHECK(first_row_to_default(255, 5) == 5);
    }
}
