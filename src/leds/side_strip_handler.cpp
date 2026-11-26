#include "leds/side_strip_handler.hpp"
#include <led_animation_controller/simple_transition_controller.hpp>
#include "marlin_server.hpp"

#if PRINTER_IS_PRUSA_XL()
    #include <config_store/store_instance.hpp>
    #include <leds/status_leds_handler.hpp>
    #include <common/marlin_vars.hpp>
    #include <feature/print_status_message/print_status_message_mgr.hpp>
    #include <tool_index.hpp>
#endif

namespace leds {

static SimpleTransitionController &controller_instance() {
    static SimpleTransitionController instance;
    return instance;
}

#if PRINTER_IS_PRUSA_XL()
namespace {

    bool any_heater_warming_up() {
        auto &vars = marlin_vars();
        if (side_strip_status::is_warming_up(vars.temp_bed, vars.target_bed)) {
            return true;
        }
        for (auto tool : PhysicalToolIndex::all().skip_all_disabled()) {
            auto &hotend = vars.hotend(tool);
            if (side_strip_status::is_warming_up(hotend.temp_nozzle, hotend.target_nozzle)) {
                return true;
            }
        }
        return false;
    }

    /// Takes the locks of the status LEDs and status messages
    side_strip_status::Status read_printer_status() {
        using side_strip_status::Status;

        switch (StatusLedsHandler::instance().current_animation()) {
        case StateAnimation::Error:
            // Possibly a redscreen: marlin_vars are not touched
            return Status::error;

        case StateAnimation::Warning:
            return Status::warning;

        case StateAnimation::Finished:
            return Status::finished;

        case StateAnimation::Printing: {
            const auto type = print_status_message().current_message().message.type;
            const bool waiting_for_heat = type == PrintStatusMessage::waiting_for_hotend_temp
                || type == PrintStatusMessage::waiting_for_bed_temp
                || type == PrintStatusMessage::absorbing_heat;
            return waiting_for_heat ? Status::heating : Status::printing;
        }

        case StateAnimation::Idle:
            return any_heater_warming_up() ? Status::heating : Status::idle;

        case StateAnimation::Aborted:
        case StateAnimation::PowerPanic:
        case StateAnimation::PowerUp:
            return Status::untinted;
        }
        return Status::untinted;
    }

} // namespace
#endif

SideStripHandler &SideStripHandler::instance() {
    static SideStripHandler instance;
    return instance;
}

SideStripHandler::SideStripHandler() {
    load_config();
}

void SideStripHandler::activity_ping() {
    std::lock_guard lock(mutex);
    active_timestamp_ms = ticks_ms();
}

void SideStripHandler::set_custom_color(ColorRGBW color, uint32_t duration_ms, uint32_t transition_ms) {
    std::lock_guard lock(mutex);
    custom_color.emplace(color, ticks_ms(), duration_ms, transition_ms);
    // Set state temporarily to off to force a change in case the state is already custom_color
    state = SideStripState::off;
}

void SideStripHandler::update() {
#if PRINTER_IS_PRUSA_XL()
    // Read before taking the mutex, reading the printer status takes other locks
    const auto mode = status_mode.load();
    std::optional<side_strip_status::Status> new_status;
    uint8_t new_percent_done = 0;
    if (const uint32_t now = ticks_ms(); mode != SideStripStatusMode::off && ticks_diff(now, status_read_ms) >= static_cast<int32_t>(status_read_interval_ms)) {
        status_read_ms = now;
        new_status = read_printer_status();
        if (*new_status == side_strip_status::Status::printing) {
            new_percent_done = marlin_vars().sd_percent_done;
        }
    }
#endif

    std::lock_guard lock(mutex);
    const uint32_t time_ms = ticks_ms();

#if PRINTER_IS_PRUSA_XL()
    if (new_status.has_value()) {
        printer_status = *new_status;
        percent_done = new_percent_done;
    }
    const bool tint_changed = update_tint(mode, time_ms);
#endif

    auto &controller = controller_instance();
    if (custom_color && time_ms - custom_color->start_ms < custom_color->duration_ms) {
        change_state(SideStripState::custom_color);
    } else {
        custom_color.reset();
        const bool dont_dim = dimming_enabled == DimmingEnabled::never || (dimming_enabled == DimmingEnabled::not_printing && marlin_server::is_printing());
        if (dont_dim || (active_timestamp_ms != 0 && time_ms - active_timestamp_ms < active_timeout_ms)) {
            change_state(SideStripState::active);
        } else {
            // Clear to prevent overflow bugs
            active_timestamp_ms = 0;
            change_state(SideStripState::dimmed);
        }
    }

#if PRINTER_IS_PRUSA_XL()
    // A state change above already used the new tint; this covers a tint change within the same state
    if (tint_changed && (state == SideStripState::active || state == SideStripState::dimmed)) {
        controller.set(get_color_for_state(state), side_strip_status::transition_ms(printer_status));
    }
#endif

    controller.update();
}

void SideStripHandler::load_config() {
    std::lock_guard lock(mutex);
    dimmed_brightness = config_store().side_leds_dimmed_brightness.get();
    max_brightness = config_store().side_leds_max_brightness.get();
    dimming_enabled = config_store().side_leds_dimming_enabled.get();
#if PRINTER_IS_PRUSA_XL()
    const auto mode = config_store().side_leds_status_mode.get();
    status_mode = (std::to_underlying(mode) < std::to_underlying(SideStripStatusMode::_cnt)) ? mode : SideStripStatusMode::off;
#endif
    // Set state to off to force a change of state that will transition to the new brightness
    state = SideStripState::off;
}

uint8_t SideStripHandler::get_max_brightness() const {
    std::lock_guard lock(mutex);
    return max_brightness;
}

void SideStripHandler::set_max_brightness(uint8_t value) {
    config_store().side_leds_max_brightness.set(value);
    std::lock_guard lock(mutex);
    if (max_brightness == value) {
        return;
    }

    max_brightness = value;
    // Set state to off to force a change of state that will transition to the new brightness
    state = SideStripState::off;
}

uint8_t SideStripHandler::get_dimmed_brightness() const {
    std::lock_guard lock(mutex);
    return dimmed_brightness;
}

void SideStripHandler::set_dimmed_brightness(uint8_t value) {
    config_store().side_leds_dimmed_brightness.set(value);
    std::lock_guard lock(mutex);
    if (dimmed_brightness == value) {
        return;
    }

    dimmed_brightness = value;
    // Set state to off to force a change of state that will transition to the new brightness
    state = SideStripState::off;
}

DimmingEnabled SideStripHandler::get_dimming_enabled() const {
    std::lock_guard lock(mutex);
    return dimming_enabled;
}

void SideStripHandler::set_dimming_enabled(DimmingEnabled value) {
    config_store().side_leds_dimming_enabled.set(value);
    std::lock_guard lock(mutex);
    dimming_enabled = value;
}

#if PRINTER_IS_PRUSA_XL()
SideStripStatusMode SideStripHandler::get_status_mode() const {
    return status_mode;
}

void SideStripHandler::set_status_mode(SideStripStatusMode value) {
    config_store().side_leds_status_mode.set(value);
    status_mode = value;
}

bool SideStripHandler::update_tint(SideStripStatusMode mode, uint32_t time_ms) {
    const auto new_tint = side_strip_status::tint(printer_status, mode, percent_done, time_ms);
    if (new_tint == tint) {
        return false;
    }
    tint = new_tint;
    return true;
}
#endif

leds::ColorRGBW SideStripHandler::color() const {
    std::lock_guard lock(mutex);
    return controller_instance().color();
}

bool SideStripHandler::is_dimmed() const {
    std::lock_guard lock(mutex);
    return state == SideStripState::dimmed;
}

void SideStripHandler::change_state(SideStripState state) {
    if (this->state != state) {
        this->state = state;

        uint32_t duration;
        if (state == SideStripState::dimmed) {
            duration = 5000;
        } else if (state == SideStripState::custom_color) {
            duration = custom_color->transition_ms;
        } else {
            duration = 500;
        }
        controller_instance().set(get_color_for_state(state), duration);
    }
}

ColorRGBW SideStripHandler::get_color_for_state(SideStripState state) {
    constexpr auto base_color = has_white_led() ? ColorRGBW(0, 0, 0, 255) : ColorRGBW(255, 255, 255);

    switch (state) {
    case SideStripState::off:
        return ColorRGBW();
    case SideStripState::dimmed:
        return with_tint(base_color.clamp(dimmed_brightness), dimmed_brightness);
    case SideStripState::active:
        return with_tint(base_color.clamp(max_brightness), max_brightness);
    case SideStripState::custom_color:
        return ColorRGBW(custom_color->color);
    }
    return {};
}

ColorRGBW SideStripHandler::with_tint(ColorRGBW plain, [[maybe_unused]] uint8_t level) const {
#if PRINTER_IS_PRUSA_XL()
    if (tint.has_value()) {
        // Alerts stay at full brightness while dimmed; no tint is ever brighter than max_brightness
        const uint8_t tint_level = side_strip_status::is_alert(printer_status) ? max_brightness : std::min(level, max_brightness);
        return side_strip_status::apply_tint(plain, *tint, tint_level);
    }
#endif
    return plain;
}

} // namespace leds
