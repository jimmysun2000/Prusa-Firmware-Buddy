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
    /// States shown without a tint (aborted, power panic, power up)
    untinted,
};

inline constexpr ColorRGBW heating_color { 255, 96, 0 };
inline constexpr ColorRGBW printing_color { 0, 150, 255 };
inline constexpr ColorRGBW finished_color { 0, 255, 0 };
inline constexpr ColorRGBW warning_color { 255, 255, 0 };
inline constexpr ColorRGBW error_color { 255, 0, 0 };

/// Brightness of the dark half of a pulse, relative to the tint
inline constexpr float pulse_dark_factor = 0.15f;

/// A heater counts as warming up while it is more than this below its target
inline constexpr float warm_up_margin = 3.0f;

/// @returns period of the pulsing states, 0 for the steady ones
constexpr uint32_t pulse_period_ms(Status status) {
    switch (status) {
    case Status::warning:
        return 1000;
    case Status::error:
        return 500;
    default:
        return 0;
    }
}

/// @returns whether the status keeps the full tint while the strip is dimmed
constexpr bool is_alert(Status status) {
    return status == Status::warning || status == Status::error;
}

/// @returns whether a heater with a target is still warming up.
/// Negative temperatures (sensor not read yet) never count.
constexpr bool is_warming_up(float temp, int target) {
    return target > 0 && temp >= 0.0f && temp + warm_up_margin < static_cast<float>(target);
}

/// @returns transition time to use when the tint changes
constexpr uint32_t transition_ms(Status status) {
    const uint32_t period = pulse_period_ms(status);
    return (period > 0 && period / 2 < 500) ? period / 2 : 500;
}

/// @returns printing tint for the progress: azure at 0 %, green at 100 %, in 5 % steps
ColorRGBW progress_color(uint8_t percent_done);

/// @returns RGB tint for the status at time_ms, std::nullopt when the strip shows no tint.
/// @p percent_done is used only while printing in status_and_progress mode.
std::optional<ColorRGBW> tint(Status status, SideStripStatusMode mode, uint8_t percent_done, uint32_t time_ms);

/// @returns colour with the RGB channels from @p tint scaled to @p level (0-255) and the white channel from @p plain
ColorRGBW apply_tint(ColorRGBW plain, ColorRGBW tint, uint8_t level);

} // namespace leds::side_strip_status
