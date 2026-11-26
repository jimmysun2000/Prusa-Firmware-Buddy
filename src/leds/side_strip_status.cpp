#include <leds/side_strip_status.hpp>

#include <algorithm>

namespace leds::side_strip_status {

ColorRGBW progress_color(uint8_t percent_done) {
    const uint8_t step = std::min<uint8_t>(percent_done, 100) / 5 * 5;
    return printing_color.blend(finished_color, static_cast<float>(step) / 100.0f);
}

std::optional<ColorRGBW> tint(Status status, SideStripStatusMode mode, uint8_t percent_done, uint32_t time_ms) {
    if (mode != SideStripStatusMode::status && mode != SideStripStatusMode::status_and_progress) {
        return std::nullopt;
    }

    ColorRGBW color;
    switch (status) {
    case Status::idle:
    case Status::untinted:
        return std::nullopt;
    case Status::heating:
        color = heating_color;
        break;
    case Status::printing:
        color = (mode == SideStripStatusMode::status_and_progress) ? progress_color(percent_done) : printing_color;
        break;
    case Status::finished:
        color = finished_color;
        break;
    case Status::warning:
        color = warning_color;
        break;
    case Status::error:
        color = error_color;
        break;
    }

    if (const uint32_t period = pulse_period_ms(status); period > 0 && (time_ms / (period / 2)) % 2 == 1) {
        color = color.fade(pulse_dark_factor);
    }
    return color;
}

ColorRGBW apply_tint(ColorRGBW plain, ColorRGBW tint, uint8_t level) {
    const auto scale = [level](uint8_t channel) {
        return static_cast<uint8_t>((channel * level + 127) / 255);
    };
    return ColorRGBW(scale(tint.r), scale(tint.g), scale(tint.b), plain.w);
}

} // namespace leds::side_strip_status
