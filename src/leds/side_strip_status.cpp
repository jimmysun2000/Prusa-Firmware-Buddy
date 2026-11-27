#include <leds/side_strip_status.hpp>

#include <algorithm>

namespace leds::side_strip_status {

ColorRGBW progress_color(uint8_t percent_done) {
    const uint8_t step = std::min<uint8_t>(percent_done, 100) / 5 * 5;
    return printing_color.blend(finished_color, static_cast<float>(step) / 100.0f);
}

std::optional<Look> look(Status status, SideStripStatusMode mode, uint8_t percent_done) {
    if (mode != SideStripStatusMode::status && mode != SideStripStatusMode::status_and_progress) {
        return std::nullopt;
    }

    const auto steady = [](ColorRGBW color) {
        return Look { color, color, 0 };
    };
    const auto pulse = [status](ColorRGBW color, float dark_factor) {
        return Look { color, color.fade(dark_factor), static_cast<uint16_t>(pulse_period_ms(status)) };
    };

    switch (status) {
    case Status::idle:
    case Status::untinted:
        return std::nullopt;
    case Status::heating:
        return pulse(heating_color, slow_pulse_dark_factor);
    case Status::printing:
        return steady((mode == SideStripStatusMode::status_and_progress) ? progress_color(percent_done) : printing_color);
    case Status::finished:
        return steady(finished_color);
    case Status::warning:
        return pulse(warning_color, pulse_dark_factor);
    case Status::error:
        return pulse(error_color, pulse_dark_factor);
    }
    return std::nullopt;
}

std::optional<ColorRGBW> tint(Status status, SideStripStatusMode mode, uint8_t percent_done, uint32_t time_ms) {
    const auto status_look = look(status, mode, percent_done);
    if (!status_look.has_value()) {
        return std::nullopt;
    }

    if (const uint32_t period = status_look->period_ms; period > 0 && (time_ms / (period / 2)) % 2 == 1) {
        return status_look->second;
    }
    return status_look->first;
}

ColorRGBW apply_tint(ColorRGBW plain, ColorRGBW tint, uint8_t level) {
    const auto scale = [level](uint8_t channel) {
        return static_cast<uint8_t>((channel * level + 127) / 255);
    };
    return ColorRGBW(scale(tint.r), scale(tint.g), scale(tint.b), plain.w);
}

} // namespace leds::side_strip_status
