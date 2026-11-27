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
    case Status::error:
        return pulse(red_color, pulse_dark_factor);
    case Status::owner_present:
    case Status::printing:
    case Status::idle:
        return std::nullopt;
    case Status::recovering:
        return steady(orange_color);
    case Status::warning_dialog:
        return pulse(red_color, slow_pulse_dark_factor);
    case Status::waiting:
        return pulse(yellow_color, pulse_dark_factor);
    case Status::busy:
        return pulse(purple_color, slow_pulse_dark_factor);
    case Status::print_heating:
        return steady(blue_color);
    case Status::preheating:
        return pulse(blue_color, slow_pulse_dark_factor);
    case Status::stopped:
        return steady(red_color);
    case Status::finished_warm:
        return pulse(green_color, slow_pulse_dark_factor);
    case Status::finished:
        return steady(green_color);
    case Status::hot:
        return steady(purple_color);
    }
    return std::nullopt;
}

Status resolve(const Inputs &inputs) {
    if (inputs.error) {
        return Status::error;
    }
    if (inputs.user_present) {
        return Status::owner_present;
    }
    if (inputs.activity == Activity::recovering) {
        return Status::recovering;
    }
    if (inputs.warning_dialog) {
        return Status::warning_dialog;
    }
    if (inputs.waits_for_owner) {
        return Status::waiting;
    }
    if (inputs.busy) {
        return Status::busy;
    }
    if (inputs.heating) {
        if (inputs.activity != Activity::printing) {
            return Status::preheating;
        }
        // Later waits, such as for a nozzle at a tool change, belong to printing
        if (!inputs.first_layer_started) {
            return Status::print_heating;
        }
    }

    switch (inputs.activity) {
    case Activity::printing:
        return Status::printing;
    case Activity::stopped:
        return Status::stopped;
    case Activity::finished:
        return inputs.bed_warm ? Status::finished_warm : Status::finished;
    case Activity::idle:
        return inputs.hot ? Status::hot : Status::idle;
    case Activity::recovering:
        return Status::recovering;
    }
    return Status::idle;
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

ColorRGBW startup_color(SideStripStatusMode mode, uint8_t level) {
    const auto startup_tint = tint(Status::recovering, mode, 0);
    return startup_tint.has_value() ? apply_tint(ColorRGBW(), *startup_tint, level) : ColorRGBW();
}

} // namespace leds::side_strip_status
