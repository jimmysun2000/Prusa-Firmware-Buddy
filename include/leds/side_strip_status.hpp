#pragma once

#include <leds/side_strip_status_mode.hpp>
#include <utils/led_color.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

/// Status colours of the side strip, as pure functions without any printer access
namespace leds::side_strip_status {

/// Printer status as shown by the side strip, highest priority first
enum class Status : uint8_t {
    /// A red screen or another error
    error,
    /// The owner has used the knob or the touch screen lately, the strip is a plain work light
    owner_present,
    /// Power panic recovery
    recovering,
    /// A warning dialog is open
    warning_dialog,
    /// The printer waits for the owner: a prompt or a paused print
    waiting,
    /// The printer works on its own outside a print: a wizard, a calibration, a filament load, G-code
    busy,
    /// A print waits for a temperature before its first layer
    print_heating,
    /// A heater warms up outside a print
    preheating,
    printing,
    /// A print is being stopped or is stopped, and its print screen is still open
    stopped,
    /// A print has finished and its bed is still warm
    finished_warm,
    /// A print has finished and its end screen is still open
    finished,
    /// Nothing runs and the bed or a nozzle is hot
    hot,
    idle,
};

inline constexpr ColorRGBW red_color { 255, 0, 0 };
inline constexpr ColorRGBW yellow_color { 255, 255, 0 };
inline constexpr ColorRGBW green_color { 0, 255, 0 };
inline constexpr ColorRGBW blue_color { 0, 150, 255 };
inline constexpr ColorRGBW purple_color { 160, 0, 255 };
inline constexpr ColorRGBW orange_color { 255, 96, 0 };

/// Above this the bed or a nozzle counts as hot, as for touching it
inline constexpr float hot_temp = 50.0f;
/// Below this a hot bed or nozzle no longer counts as hot
inline constexpr float hot_off_temp = 45.0f;

/// Below this the bed of a finished print counts as cool
inline constexpr float bed_cool_temp = 40.0f;
/// Above this a cool bed counts as warm again
inline constexpr float bed_warm_temp = 42.0f;

/// G-code running outside a print and without a dialog counts as busy once it has run this long
inline constexpr uint32_t gcode_busy_hold_ms = 1000;

/// Brightness of the dark half of an alert pulse, relative to the tint
inline constexpr float pulse_dark_factor = 0.15f;

/// Brightness of the dark half of a slow pulse, relative to the tint
inline constexpr float slow_pulse_dark_factor = 0.1f;

/// A heater counts as warming up while it is more than this below its target
inline constexpr float warm_up_margin = 3.0f;

/// @returns period of the pulsing states, 0 for the steady ones
constexpr uint32_t pulse_period_ms(Status status) {
    switch (status) {
    case Status::warning_dialog:
    case Status::busy:
    case Status::preheating:
    case Status::finished_warm:
        return 3000;
    case Status::waiting:
        return 1000;
    case Status::error:
        return 500;
    default:
        return 0;
    }
}

/// Two tints shown in turn, each for half of the period
struct Look {
    ColorRGBW first;
    ColorRGBW second;
    /// 0 shows only the first tint
    uint16_t period_ms;
};

/// @returns whether the status shows a colour; the others leave the strip a plain work light
constexpr bool shows_color(Status status) {
    switch (status) {
    case Status::owner_present:
    case Status::printing:
    case Status::idle:
        return false;
    case Status::error:
    case Status::recovering:
    case Status::warning_dialog:
    case Status::waiting:
    case Status::busy:
    case Status::print_heating:
    case Status::preheating:
    case Status::stopped:
    case Status::finished_warm:
    case Status::finished:
    case Status::hot:
        return true;
    }
    return false;
}

/// Highest white under a status colour, 25 % as the brightness settings count it
inline constexpr uint8_t color_white = 25 * 255 / 100;

/// @returns the white to show under the status, given the white @p plain_white the strip shows without status colours.
/// Under a status colour the white is lowered so that the colour stands out.
constexpr uint8_t white_under(Status status, uint8_t plain_white) {
    return shows_color(status) ? std::min(plain_white, color_white) : plain_white;
}

/// @returns whether a heater with a target is still warming up.
/// Negative temperatures (sensor not read yet) never count.
constexpr bool is_warming_up(float temp, int target) {
    return target > 0 && temp >= 0.0f && temp + warm_up_margin < static_cast<float>(target);
}

/// Hysteresis between two thresholds
/// @returns true above @p on, false below @p off and @p was_above in between
constexpr bool is_above(float temp, float on, float off, bool was_above) {
    if (temp > on) {
        return true;
    }
    if (temp < off) {
        return false;
    }
    return was_above;
}

/// What the printer does, as far as the side strip tells it apart
enum class Activity : uint8_t {
    /// Nothing runs, also while a print preview closes
    idle,
    printing,
    /// A print is being stopped or is stopped, and its print screen is still open
    stopped,
    /// A print has finished and its end screen is still open
    finished,
    /// Power panic recovery
    recovering,
};

/// Everything the side strip status depends on, as read from the printer
struct Inputs {
    Activity activity = Activity::idle;
    /// A red screen or another error
    bool error = false;
    /// The owner has used the knob or the touch screen lately, the strip is a plain work light
    bool user_present = false;
    /// A warning dialog is open
    bool warning_dialog = false;
    /// The printer waits for the owner: a prompt or a paused print
    bool waits_for_owner = false;
    /// The printer works on its own outside a print
    bool busy = false;
    /// A print waits for a temperature, or a heater warms up to a target outside a print
    bool heating = false;
    /// The print has extruded inside the print area, so its first layer has begun
    bool first_layer_started = false;
    /// The bed cool-down after a print runs or the bed is not cool yet
    bool bed_warm = false;
    /// The bed or a nozzle is hot
    bool hot = false;
};

/// @returns the status to show, the first of these that applies: error, owner_present, recovering,
/// warning_dialog, waiting, busy, print_heating or preheating, printing, stopped,
/// finished_warm or finished, hot, idle.
/// A print waiting for a temperature after its first layer has begun shows as printing.
Status resolve(const Inputs &inputs);

/// @returns transition time to use when the tint changes
constexpr uint32_t transition_ms(Status status) {
    const uint32_t period = pulse_period_ms(status);
    return (period > 0 && period / 2 < 500) ? period / 2 : 500;
}

/// @returns how the status shows, std::nullopt when the strip shows no tint
std::optional<Look> look(Status status, SideStripStatusMode mode);

/// @returns RGB tint for the status at time_ms, std::nullopt when the strip shows no tint
std::optional<ColorRGBW> tint(Status status, SideStripStatusMode mode, uint32_t time_ms);

/// @returns colour with the RGB channels from @p tint scaled to @p level (0-255) and the white channel from @p plain
ColorRGBW apply_tint(ColorRGBW plain, ColorRGBW tint, uint8_t level);

/// @returns colour to show while the printer starts: the power panic recovery look scaled to
/// @p level (0-255) without white, black when @p mode shows no status colours
ColorRGBW startup_color(SideStripStatusMode mode, uint8_t level);

} // namespace leds::side_strip_status
