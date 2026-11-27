#include <leds/side_strip_status.hpp>

namespace leds::side_strip_status {

std::optional<Look> look(Status status, SideStripStatusMode mode) {
    if (mode != SideStripStatusMode::status) {
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
        return steady(printing_color);
    case Status::finished:
        return steady(finished_color);
    case Status::warning:
        return pulse(warning_color, pulse_dark_factor);
    case Status::error:
        return pulse(error_color, pulse_dark_factor);
    case Status::busy:
        return pulse(busy_color, slow_pulse_dark_factor);
    case Status::hot:
        return steady(heating_color);
    case Status::cooling:
        return Look { finished_color, heating_color, static_cast<uint16_t>(pulse_period_ms(status)) };
    }
    return std::nullopt;
}

Status resolve(const Inputs &inputs) {
    if (inputs.error) {
        return Status::error;
    }
    if (inputs.activity == Activity::untinted || inputs.user_present) {
        return Status::untinted;
    }
    if (inputs.waits_for_owner) {
        return Status::warning;
    }
    if (inputs.busy) {
        return Status::busy;
    }
    if (inputs.heating) {
        return Status::heating;
    }

    switch (inputs.activity) {
    case Activity::printing:
        return Status::printing;
    case Activity::finished:
        return inputs.bed_warm ? Status::cooling : Status::finished;
    case Activity::idle:
        return inputs.hot ? Status::hot : Status::idle;
    case Activity::untinted:
        break;
    }
    return Status::untinted;
}

std::optional<ColorRGBW> tint(Status status, SideStripStatusMode mode, uint32_t time_ms) {
    const auto status_look = look(status, mode);
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
