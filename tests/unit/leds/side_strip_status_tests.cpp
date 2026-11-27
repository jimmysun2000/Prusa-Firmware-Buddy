#include <leds/side_strip_status.hpp>

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <iterator>
#include <utility>

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

constexpr Status all_statuses[] = {
    Status::error,
    Status::owner_present,
    Status::recovering,
    Status::waiting,
    Status::busy,
    Status::print_heating,
    Status::preheating,
    Status::printing,
    Status::finished_warm,
    Status::finished,
    Status::hot,
    Status::idle,
};

/// Statuses that leave the strip a plain work light
constexpr Status plain_statuses[] = { Status::owner_present, Status::printing, Status::idle };

TEST_CASE("side_strip_status: mode off never tints") {
    for (const auto status : all_statuses) {
        CHECK_FALSE(look(status, SideStripStatusMode::off).has_value());
        CHECK_FALSE(tint(status, SideStripStatusMode::off, 0).has_value());
        CHECK_FALSE(tint(status, SideStripStatusMode::off, 700).has_value());
        CHECK_FALSE(tint(status, SideStripStatusMode::off, 2000).has_value());
    }
}

TEST_CASE("side_strip_status: invalid mode never tints") {
    CHECK_FALSE(tint(Status::error, SideStripStatusMode::_cnt, 0).has_value());
    CHECK_FALSE(tint(Status::error, static_cast<SideStripStatusMode>(2), 0).has_value());
    CHECK_FALSE(tint(Status::error, static_cast<SideStripStatusMode>(200), 0).has_value());
    CHECK_FALSE(look(Status::error, static_cast<SideStripStatusMode>(200)).has_value());
}

TEST_CASE("side_strip_status: stored mode") {
    const auto from_stored = [](uint8_t stored) {
        return side_strip_status_mode_from_stored(static_cast<SideStripStatusMode>(stored));
    };
    CHECK(from_stored(0) == SideStripStatusMode::off);
    CHECK(from_stored(1) == SideStripStatusMode::status);
    // The State and progress mode of older firmware
    CHECK(from_stored(2) == SideStripStatusMode::status);
    CHECK(from_stored(3) == SideStripStatusMode::off);
    CHECK(from_stored(200) == SideStripStatusMode::off);
    CHECK(from_stored(255) == SideStripStatusMode::off);

    // Every value the store can hold reads as a mode the menu offers
    for (unsigned stored = 0; stored <= 255; ++stored) {
        CHECK(static_cast<uint8_t>(from_stored(static_cast<uint8_t>(stored))) < static_cast<uint8_t>(SideStripStatusMode::_cnt));
    }
}

TEST_CASE("side_strip_status: colours") {
    CHECK(same(red_color, ColorRGBW(255, 0, 0)));
    CHECK(same(yellow_color, ColorRGBW(255, 255, 0)));
    CHECK(same(green_color, ColorRGBW(0, 255, 0)));
    CHECK(same(blue_color, ColorRGBW(0, 150, 255)));
    CHECK(same(purple_color, ColorRGBW(160, 0, 255)));
    CHECK(same(orange_color, ColorRGBW(255, 96, 0)));
}

TEST_CASE("side_strip_status: looks") {
    struct Expected {
        Status status;
        ColorRGBW first;
        ColorRGBW second;
        uint16_t period_ms;
    };
    const Expected expected[] = {
        { Status::error, red_color, ColorRGBW(38, 0, 0), 500 },
        { Status::recovering, orange_color, orange_color, 0 },
        { Status::waiting, yellow_color, ColorRGBW(38, 38, 0), 1000 },
        { Status::busy, purple_color, ColorRGBW(16, 0, 25), 3000 },
        { Status::print_heating, blue_color, blue_color, 0 },
        { Status::preheating, blue_color, ColorRGBW(0, 15, 25), 3000 },
        { Status::finished_warm, green_color, ColorRGBW(0, 25, 0), 3000 },
        { Status::finished, green_color, green_color, 0 },
        { Status::hot, purple_color, purple_color, 0 },
    };

    const auto mode = SideStripStatusMode::status;
    for (const auto &e : expected) {
        CAPTURE(static_cast<int>(e.status));
        const auto status_look = look(e.status, mode);
        REQUIRE(status_look.has_value());
        CHECK(same(status_look->first, e.first));
        CHECK(same(status_look->second, e.second));
        CHECK(status_look->period_ms == e.period_ms);
        CHECK(status_look->period_ms == pulse_period_ms(e.status));

        // tint() shows the first tint for the first half of the period and the second one for the rest
        const uint32_t half = e.period_ms / 2;
        CHECK(same(tint(e.status, mode, 0), e.first));
        if (half > 0) {
            CHECK(same(tint(e.status, mode, half - 1), e.first));
            CHECK(same(tint(e.status, mode, half), e.second));
            CHECK(same(tint(e.status, mode, e.period_ms - 1), e.second));
            CHECK(same(tint(e.status, mode, e.period_ms), e.first));
        } else {
            for (const uint32_t time_ms : { 250u, 750u, 1500u, 2000u }) {
                CHECK(same(tint(e.status, mode, time_ms), e.first));
            }
        }

        // Looks never have a white of their own
        CHECK(status_look->first.w == 0);
        CHECK(status_look->second.w == 0);
    }

    for (const auto status : plain_statuses) {
        CAPTURE(static_cast<int>(status));
        CHECK_FALSE(look(status, mode).has_value());
        CHECK_FALSE(tint(status, mode, 0).has_value());
        CHECK(pulse_period_ms(status) == 0);
    }

    // Every status is in one of the two lists
    CHECK(std::size(expected) + std::size(plain_statuses) == std::size(all_statuses));
}

TEST_CASE("side_strip_status: pulse phase") {
    const auto mode = SideStripStatusMode::status;

    // The fast pulses fade to 15 %, the slow ones to 10 %
    CHECK(same(yellow_color.fade(pulse_dark_factor), ColorRGBW(38, 38, 0)));
    CHECK(same(blue_color.fade(slow_pulse_dark_factor), ColorRGBW(0, 15, 25)));

    // Waiting: 1 s period, bright for the first half
    CHECK(same(tint(Status::waiting, mode, 0), yellow_color));
    CHECK(same(tint(Status::waiting, mode, 499), yellow_color));
    CHECK(same(tint(Status::waiting, mode, 500), yellow_color.fade(pulse_dark_factor)));
    CHECK(same(tint(Status::waiting, mode, 999), yellow_color.fade(pulse_dark_factor)));
    CHECK(same(tint(Status::waiting, mode, 1000), yellow_color));

    // Error: 0.5 s period
    CHECK(same(tint(Status::error, mode, 0), red_color));
    CHECK(same(tint(Status::error, mode, 249), red_color));
    CHECK(same(tint(Status::error, mode, 250), red_color.fade(pulse_dark_factor)));
    CHECK(same(tint(Status::error, mode, 500), red_color));

    // Preheating: 3 s period
    CHECK(same(tint(Status::preheating, mode, 0), blue_color));
    CHECK(same(tint(Status::preheating, mode, 1499), blue_color));
    CHECK(same(tint(Status::preheating, mode, 1500), blue_color.fade(slow_pulse_dark_factor)));
    CHECK(same(tint(Status::preheating, mode, 2999), blue_color.fade(slow_pulse_dark_factor)));
    CHECK(same(tint(Status::preheating, mode, 3000), blue_color));

    CHECK(pulse_period_ms(Status::error) == 500);
    CHECK(pulse_period_ms(Status::waiting) == 1000);
    CHECK(pulse_period_ms(Status::busy) == 3000);
    CHECK(pulse_period_ms(Status::preheating) == 3000);
    CHECK(pulse_period_ms(Status::finished_warm) == 3000);
    for (const auto status : { Status::recovering, Status::print_heating, Status::finished, Status::hot }) {
        CHECK(pulse_period_ms(status) == 0);
    }

    CHECK(transition_ms(Status::error) == 250);
    for (const auto status : all_statuses) {
        if (status != Status::error) {
            CHECK(transition_ms(status) == 500);
        }
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
    for (const auto status : all_statuses) {
        CAPTURE(static_cast<int>(status));
        CHECK(is_alert(status) == (status == Status::waiting || status == Status::error));
    }
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

TEST_CASE("side_strip_status: hysteresis") {
    // Outside the band the state follows the temperature
    CHECK(is_above(50.1f, hot_temp, hot_off_temp, false));
    CHECK(is_above(50.1f, hot_temp, hot_off_temp, true));
    CHECK_FALSE(is_above(44.9f, hot_temp, hot_off_temp, false));
    CHECK_FALSE(is_above(44.9f, hot_temp, hot_off_temp, true));

    // Inside the band and on its edges the state is kept
    for (const float temp : { 45.0f, 47.0f, 50.0f }) {
        CHECK(is_above(temp, hot_temp, hot_off_temp, true));
        CHECK_FALSE(is_above(temp, hot_temp, hot_off_temp, false));
    }

    // Not read yet
    CHECK_FALSE(is_above(-1.0f, hot_temp, hot_off_temp, true));

    // A bed cooling down after a print and warming up again
    bool warm = false;
    for (const auto &[temp, expected] : std::initializer_list<std::pair<float, bool>> {
             { 25.0f, false }, { 41.0f, false }, { 42.5f, true }, { 100.0f, true }, { 41.0f, true }, { 40.0f, true }, { 39.9f, false }, { 41.9f, false }, { 42.1f, true } }) {
        warm = is_above(temp, bed_warm_temp, bed_cool_temp, warm);
        CAPTURE(temp);
        CHECK(warm == expected);
    }

    CHECK(hot_temp == 50.0f);
    CHECK(hot_off_temp == 45.0f);
    CHECK(bed_cool_temp == 40.0f);
}

namespace {

constexpr Activity all_activities[] = { Activity::idle, Activity::printing, Activity::finished, Activity::recovering };

/// The documented priority, highest first
Status expected_status(const Inputs &in) {
    const bool printing = in.activity == Activity::printing;
    const std::pair<bool, Status> order[] = {
        { in.error, Status::error },
        { in.user_present, Status::owner_present },
        { in.activity == Activity::recovering, Status::recovering },
        { in.waits_for_owner, Status::waiting },
        { in.busy, Status::busy },
        { in.heating && printing && !in.first_layer_started, Status::print_heating },
        { in.heating && !printing, Status::preheating },
        { printing, Status::printing },
        { in.activity == Activity::finished && in.bed_warm, Status::finished_warm },
        { in.activity == Activity::finished, Status::finished },
        { in.activity == Activity::idle && in.hot, Status::hot },
    };
    for (const auto &[applies, status] : order) {
        if (applies) {
            return status;
        }
    }
    return Status::idle;
}

Inputs make(Activity activity) {
    Inputs in;
    in.activity = activity;
    return in;
}

} // namespace

TEST_CASE("side_strip_status: resolve priority") {
    // Every combination of the inputs
    for (const auto activity : all_activities) {
        for (unsigned flags = 0; flags < (1u << 8); ++flags) {
            Inputs in;
            in.activity = activity;
            in.error = flags & (1u << 0);
            in.user_present = flags & (1u << 1);
            in.waits_for_owner = flags & (1u << 2);
            in.busy = flags & (1u << 3);
            in.heating = flags & (1u << 4);
            in.first_layer_started = flags & (1u << 5);
            in.bed_warm = flags & (1u << 6);
            in.hot = flags & (1u << 7);
            CAPTURE(static_cast<int>(activity), flags);
            CHECK(resolve(in) == expected_status(in));
        }
    }
}

TEST_CASE("side_strip_status: resolve cases") {
    // Nothing runs
    CHECK(resolve(make(Activity::idle)) == Status::idle);
    CHECK(resolve(Inputs {}) == Status::idle);

    // Idle with a hot bed, then heating up again
    auto in = make(Activity::idle);
    in.hot = true;
    CHECK(resolve(in) == Status::hot);
    in.heating = true;
    CHECK(resolve(in) == Status::preheating);

    // A warm bed matters only after a print
    in = make(Activity::idle);
    in.bed_warm = true;
    CHECK(resolve(in) == Status::idle);

    // Printing ignores the hot bed; heating at the print start, then a prompt
    in = make(Activity::printing);
    in.hot = true;
    in.bed_warm = true;
    CHECK(resolve(in) == Status::printing);
    in.heating = true;
    CHECK(resolve(in) == Status::print_heating);
    in.waits_for_owner = true;
    CHECK(resolve(in) == Status::waiting);

    // Finished: the bed cools down, then finished even with a hot nozzle
    in = make(Activity::finished);
    in.bed_warm = true;
    in.hot = true;
    CHECK(resolve(in) == Status::finished_warm);
    in.bed_warm = false;
    CHECK(resolve(in) == Status::finished);
    // A heater warming up on the end screen
    in.heating = true;
    CHECK(resolve(in) == Status::preheating);

    // Busy outside a print, a prompt on top of it
    in = make(Activity::idle);
    in.busy = true;
    in.heating = true;
    in.hot = true;
    CHECK(resolve(in) == Status::busy);
    in.waits_for_owner = true;
    CHECK(resolve(in) == Status::waiting);

    // Power panic recovery is above prompts, the owner and an error are above it
    in = make(Activity::recovering);
    in.waits_for_owner = true;
    in.busy = true;
    in.heating = true;
    in.hot = true;
    CHECK(resolve(in) == Status::recovering);
    in.user_present = true;
    CHECK(resolve(in) == Status::owner_present);
    in.error = true;
    CHECK(resolve(in) == Status::error);

    // An error beats everything
    for (const auto activity : all_activities) {
        in = make(activity);
        in.error = true;
        in.waits_for_owner = true;
        in.user_present = true;
        CHECK(resolve(in) == Status::error);
    }

    // The owner at the printer gets a plain work light whatever runs, except on an error
    for (const auto activity : all_activities) {
        in = make(activity);
        in.user_present = true;
        in.waits_for_owner = true;
        in.busy = true;
        in.heating = true;
        in.bed_warm = true;
        in.hot = true;
        CHECK(resolve(in) == Status::owner_present);
        CHECK_FALSE(tint(resolve(in), SideStripStatusMode::status, 0).has_value());
    }
}

TEST_CASE("side_strip_status: first layer") {
    // A wait for temperature is blue until the first layer begins, then plain white like printing
    auto in = make(Activity::printing);
    in.heating = true;
    CHECK(resolve(in) == Status::print_heating);
    in.first_layer_started = true;
    CHECK(resolve(in) == Status::printing);
    in.heating = false;
    CHECK(resolve(in) == Status::printing);

    // Only a print has a first layer
    for (const auto activity : { Activity::idle, Activity::finished }) {
        in = make(activity);
        in.heating = true;
        in.first_layer_started = true;
        CHECK(resolve(in) == Status::preheating);
    }

    // A filament change waiting for the nozzle is still a prompt
    in = make(Activity::printing);
    in.heating = true;
    in.waits_for_owner = true;
    CHECK(resolve(in) == Status::waiting);
    in.first_layer_started = true;
    CHECK(resolve(in) == Status::waiting);
}

TEST_CASE("side_strip_status: white under alerts") {
    CHECK(alert_white == 63);

    for (const auto status : { Status::waiting, Status::error }) {
        CHECK(white_under(status, 255) == alert_white);
        CHECK(white_under(status, 102) == alert_white);
        // Never brighter than without the status colours
        CHECK(white_under(status, 40) == 40);
        CHECK(white_under(status, 0) == 0);
    }

    for (const auto status : all_statuses) {
        if (is_alert(status)) {
            continue;
        }
        for (const uint8_t white : { 0, 40, 102, 255 }) {
            CHECK(white_under(status, white) == white);
        }
    }

    // The white under an alert is the same at both pulse phases, so only the RGB driver word changes
    const ColorRGBW plain(0, 0, 0, white_under(Status::waiting, 255));
    const auto bright = apply_tint(plain, *tint(Status::waiting, SideStripStatusMode::status, 0), 255);
    const auto dark = apply_tint(plain, *tint(Status::waiting, SideStripStatusMode::status, 500), 255);
    CHECK(bright.w == dark.w);
    CHECK_FALSE(same(bright, dark));
}
