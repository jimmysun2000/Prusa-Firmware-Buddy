#include <leds/side_strip_status.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace leds;
using namespace leds::side_strip_status;

namespace {

bool same(ColorRGBW a, ColorRGBW b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.w == b.w;
}

bool same(std::optional<ColorRGBW> a, ColorRGBW b) {
    return a.has_value() && same(*a, b);
}

} // namespace

TEST_CASE("side_strip_status: mode off never tints") {
    for (const auto status : { Status::idle, Status::heating, Status::printing, Status::finished, Status::warning, Status::error, Status::untinted }) {
        CHECK_FALSE(look(status, SideStripStatusMode::off, 50).has_value());
        CHECK_FALSE(tint(status, SideStripStatusMode::off, 50, 0).has_value());
        CHECK_FALSE(tint(status, SideStripStatusMode::off, 50, 700).has_value());
        CHECK_FALSE(tint(status, SideStripStatusMode::off, 50, 2000).has_value());
    }
}

TEST_CASE("side_strip_status: invalid mode never tints") {
    CHECK_FALSE(tint(Status::printing, SideStripStatusMode::_cnt, 50, 0).has_value());
    CHECK_FALSE(tint(Status::error, static_cast<SideStripStatusMode>(200), 50, 0).has_value());
    CHECK_FALSE(look(Status::error, static_cast<SideStripStatusMode>(200), 50).has_value());
}

TEST_CASE("side_strip_status: status to colour") {
    const auto mode = SideStripStatusMode::status;
    CHECK_FALSE(tint(Status::idle, mode, 0, 0).has_value());
    CHECK_FALSE(tint(Status::untinted, mode, 0, 0).has_value());
    CHECK(same(tint(Status::heating, mode, 0, 0), ColorRGBW(255, 96, 0)));
    CHECK(same(tint(Status::printing, mode, 0, 0), ColorRGBW(0, 150, 255)));
    CHECK(same(tint(Status::finished, mode, 0, 0), ColorRGBW(0, 255, 0)));
    CHECK(same(tint(Status::warning, mode, 0, 0), ColorRGBW(255, 255, 0)));
    CHECK(same(tint(Status::error, mode, 0, 0), ColorRGBW(255, 0, 0)));
}

TEST_CASE("side_strip_status: printing ignores progress in status mode") {
    CHECK(same(tint(Status::printing, SideStripStatusMode::status, 80, 0), printing_color));
}

TEST_CASE("side_strip_status: progress blend") {
    const auto mode = SideStripStatusMode::status_and_progress;
    CHECK(same(tint(Status::printing, mode, 0, 0), ColorRGBW(0, 150, 255)));
    CHECK(same(tint(Status::printing, mode, 100, 0), ColorRGBW(0, 255, 0)));
    CHECK(same(tint(Status::printing, mode, 50, 0), ColorRGBW(0, 202, 127)));

    // 5 % steps
    CHECK(same(progress_color(50), progress_color(54)));
    CHECK_FALSE(same(progress_color(54), progress_color(55)));
    CHECK(same(progress_color(4), printing_color));

    // Out of range progress is clamped
    CHECK(same(progress_color(250), finished_color));

    // Green rises and blue falls monotonically
    for (uint8_t p = 5; p <= 100; p += 5) {
        CHECK(progress_color(p).g >= progress_color(p - 5).g);
        CHECK(progress_color(p).b <= progress_color(p - 5).b);
    }

    // Only printing follows progress
    CHECK(same(tint(Status::heating, mode, 50, 0), heating_color));
    CHECK(same(tint(Status::finished, mode, 50, 0), finished_color));
}

TEST_CASE("side_strip_status: pulse phase") {
    const auto mode = SideStripStatusMode::status;
    const ColorRGBW warning_dark = warning_color.fade(pulse_dark_factor);
    const ColorRGBW error_dark = error_color.fade(pulse_dark_factor);

    // Warning: 1 s period, bright for the first half
    CHECK(same(tint(Status::warning, mode, 0, 0), warning_color));
    CHECK(same(tint(Status::warning, mode, 0, 499), warning_color));
    CHECK(same(tint(Status::warning, mode, 0, 500), warning_dark));
    CHECK(same(tint(Status::warning, mode, 0, 999), warning_dark));
    CHECK(same(tint(Status::warning, mode, 0, 1000), warning_color));

    // Error: 0.5 s period
    CHECK(same(tint(Status::error, mode, 0, 0), error_color));
    CHECK(same(tint(Status::error, mode, 0, 249), error_color));
    CHECK(same(tint(Status::error, mode, 0, 250), error_dark));
    CHECK(same(tint(Status::error, mode, 0, 500), error_color));

    CHECK(warning_dark.r == 38);
    CHECK(warning_dark.g == 38);
    CHECK(warning_dark.b == 0);

    // Heating: 3 s period, 40 % for the second half
    const ColorRGBW heating_dark = heating_color.fade(slow_pulse_dark_factor);
    CHECK(same(heating_dark, ColorRGBW(102, 38, 0)));
    CHECK(same(tint(Status::heating, mode, 0, 0), heating_color));
    CHECK(same(tint(Status::heating, mode, 0, 1499), heating_color));
    CHECK(same(tint(Status::heating, mode, 0, 1500), heating_dark));
    CHECK(same(tint(Status::heating, mode, 0, 2999), heating_dark));
    CHECK(same(tint(Status::heating, mode, 0, 3000), heating_color));

    // Steady states do not pulse
    for (const auto status : { Status::printing, Status::finished }) {
        for (const uint32_t time_ms : { 250u, 750u, 1500u, 2000u }) {
            CHECK(same(tint(status, mode, 0, 0), *tint(status, mode, 0, time_ms)));
        }
    }

    CHECK(pulse_period_ms(Status::heating) == 3000);
    CHECK(pulse_period_ms(Status::warning) == 1000);
    CHECK(pulse_period_ms(Status::error) == 500);
    CHECK(pulse_period_ms(Status::printing) == 0);
    CHECK(pulse_period_ms(Status::finished) == 0);
    CHECK(transition_ms(Status::heating) == 500);
    CHECK(transition_ms(Status::warning) == 500);
    CHECK(transition_ms(Status::error) == 250);
    CHECK(transition_ms(Status::printing) == 500);
}

TEST_CASE("side_strip_status: looks") {
    struct Expected {
        Status status;
        ColorRGBW first;
        ColorRGBW second;
        uint16_t period_ms;
    };
    const Expected expected[] = {
        { Status::heating, heating_color, ColorRGBW(102, 38, 0), 3000 },
        { Status::printing, printing_color, printing_color, 0 },
        { Status::finished, finished_color, finished_color, 0 },
        { Status::warning, warning_color, ColorRGBW(38, 38, 0), 1000 },
        { Status::error, error_color, ColorRGBW(38, 0, 0), 500 },
    };

    for (const auto mode : { SideStripStatusMode::status, SideStripStatusMode::status_and_progress }) {
        for (const auto &e : expected) {
            CAPTURE(static_cast<int>(e.status), static_cast<int>(mode));
            const auto status_look = look(e.status, mode, 0);
            REQUIRE(status_look.has_value());
            CHECK(same(status_look->first, e.first));
            CHECK(same(status_look->second, e.second));
            CHECK(status_look->period_ms == e.period_ms);
            CHECK(status_look->period_ms == pulse_period_ms(e.status));

            // tint() shows the first tint for the first half of the period and the second one for the rest
            const uint32_t half = e.period_ms / 2;
            CHECK(same(tint(e.status, mode, 0, 0), e.first));
            if (half > 0) {
                CHECK(same(tint(e.status, mode, 0, half - 1), e.first));
                CHECK(same(tint(e.status, mode, 0, half), e.second));
                CHECK(same(tint(e.status, mode, 0, e.period_ms - 1), e.second));
                CHECK(same(tint(e.status, mode, 0, e.period_ms), e.first));
            }
        }

        CHECK_FALSE(look(Status::idle, mode, 0).has_value());
        CHECK_FALSE(look(Status::untinted, mode, 0).has_value());
    }

    // Looks never have a white of their own
    for (const auto &e : expected) {
        const auto status_look = look(e.status, SideStripStatusMode::status, 0);
        REQUIRE(status_look.has_value());
        CHECK(status_look->first.w == 0);
        CHECK(status_look->second.w == 0);
    }
}

TEST_CASE("side_strip_status: apply_tint") {
    const ColorRGBW plain(0, 0, 0, 200);

    // White channel is kept, RGB comes from the tint
    CHECK(same(apply_tint(plain, ColorRGBW(255, 96, 0), 255), ColorRGBW(255, 96, 0, 200)));

    // Level 0 turns the tint off
    CHECK(same(apply_tint(plain, ColorRGBW(255, 96, 0), 0), ColorRGBW(0, 0, 0, 200)));

    // Scaled with rounding
    CHECK(same(apply_tint(ColorRGBW(0, 0, 0, 102), ColorRGBW(255, 150, 1), 102), ColorRGBW(102, 60, 0, 102)));
}

TEST_CASE("side_strip_status: alerts") {
    CHECK(is_alert(Status::warning));
    CHECK(is_alert(Status::error));
    CHECK_FALSE(is_alert(Status::printing));
    CHECK_FALSE(is_alert(Status::heating));
    CHECK_FALSE(is_alert(Status::finished));
    CHECK_FALSE(is_alert(Status::idle));
}

TEST_CASE("side_strip_status: warming up") {
    CHECK(is_warming_up(25.0f, 215));
    CHECK(is_warming_up(211.9f, 215));
    CHECK_FALSE(is_warming_up(212.0f, 215));
    CHECK_FALSE(is_warming_up(215.0f, 215));
    CHECK_FALSE(is_warming_up(250.0f, 215)); // Cooling down
    CHECK_FALSE(is_warming_up(25.0f, 0));
    CHECK_FALSE(is_warming_up(-1.0f, 215)); // Not read yet
}
