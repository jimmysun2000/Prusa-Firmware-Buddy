#pragma once

#include <leds/side_strip_status_mode.hpp>
#include <utils/led_color.hpp>

#include <cstdint>
#include <optional>

/// Status colours of the side strip, as pure functions without any printer access
namespace leds::side_strip_status {

/// Printer status as shown by the side strip
enum class Status : uint8_t {
    idle,
    heating,
    printing,
    finished,
    warning,
    error,
    /// States shown without a tint (power panic, power up)
    untinted,
    /// The printer works on its own outside a print: a wizard, a calibration, a filament load, G-code
    busy,
    /// Nothing runs and the bed or a nozzle is hot
    hot,
    /// A print has finished and its bed is still warm
    cooling,
};

inline constexpr ColorRGBW heating_color { 255, 96, 0 };
inline constexpr ColorRGBW printing_color { 0, 150, 255 };
inline constexpr ColorRGBW finished_color { 0, 255, 0 };
inline constexpr ColorRGBW warning_color { 255, 255, 0 };
inline constexpr ColorRGBW error_color { 255, 0, 0 };
inline constexpr ColorRGBW busy_color { 160, 0, 255 };

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
inline constexpr float slow_pulse_dark_factor = 0.4f;

/// A heater counts as warming up while it is more than this below its target
inline constexpr float warm_up_margin = 3.0f;

/// @returns period of the pulsing states, 0 for the steady ones
constexpr uint32_t pulse_period_ms(Status status) {
    switch (status) {
    case Status::heating:
    case Status::busy:
        return 3000;
    case Status::cooling:
        return 4000;
    case Status::warning:
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

/// @returns whether the status keeps the full tint while the strip is dimmed
constexpr bool is_alert(Status status) {
    return status == Status::warning || status == Status::error;
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
    /// Nothing runs, also while and after a print is stopped
    idle,
    printing,
    /// A print has finished and its end screen is still open
    finished,
    /// Power panic recovery and power up, shown without a tint
    untinted,
};

/// Everything the side strip status depends on, as read from the printer
struct Inputs {
    Activity activity = Activity::idle;
    /// A red screen or another error
    bool error = false;
    /// The printer waits for the owner: a warning dialog, a paused print, a prompt
    bool waits_for_owner = false;
    /// The printer works on its own outside a print
    bool busy = false;
    /// A print waits for a temperature, or a heater warms up outside a print
    bool heating = false;
    /// The bed cool-down after a print runs or the bed is not cool yet
    bool bed_warm = false;
    /// The bed or a nozzle is hot
    bool hot = false;
};

/// @returns the status to show, the highest of the ones that apply:
/// error, warning, busy, heating, printing, cooling, finished, hot, idle.
/// Power panic recovery and power up show no tint whatever else applies, except an error.
Status resolve(const Inputs &inputs);

/// @returns transition time to use when the tint changes
constexpr uint32_t transition_ms(Status status) {
    const uint32_t period = pulse_period_ms(status);
    return (period > 0 && period / 2 < 500) ? period / 2 : 500;
}

/// @returns printing tint for the progress: azure at 0 %, green at 100 %, in 5 % steps
ColorRGBW progress_color(uint8_t percent_done);

/// @returns how the status shows, std::nullopt when the strip shows no tint.
/// @p percent_done is used only while printing in status_and_progress mode.
std::optional<Look> look(Status status, SideStripStatusMode mode, uint8_t percent_done);

/// @returns RGB tint for the status at time_ms, std::nullopt when the strip shows no tint.
/// @p percent_done is used only while printing in status_and_progress mode.
std::optional<ColorRGBW> tint(Status status, SideStripStatusMode mode, uint8_t percent_done, uint32_t time_ms);

/// @returns colour with the RGB channels from @p tint scaled to @p level (0-255) and the white channel from @p plain
ColorRGBW apply_tint(ColorRGBW plain, ColorRGBW tint, uint8_t level);

} // namespace leds::side_strip_status
