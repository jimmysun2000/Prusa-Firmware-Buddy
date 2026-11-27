#include "leds/side_strip_handler.hpp"
#include <led_animation_controller/simple_transition_controller.hpp>
#include "marlin_server.hpp"

#if PRINTER_IS_PRUSA_XL()
    #include <config_store/store_instance.hpp>
    #include <leds/status_leds_handler.hpp>
    #include <common/marlin_vars.hpp>
    #include <client_response.hpp>
    #include <feature/print_status_message/print_status_message_mgr.hpp>
    #include <fsm/filament_change_phases.hpp>
    #include <option/has_chamber_api.h>
    #include <option/has_extruder_fsensor.h>
    #include <option/has_loadcell.h>
    #include <option/has_side_fsensor.h>
    #include <option/xl_enclosure_support.h>
    #include <state/printer_state.hpp>
    #include <tool_index.hpp>

    #include <algorithm>
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

    /// @returns the temperature of the hottest of the bed and the nozzles
    float hottest_heater() {
        auto &vars = marlin_vars();
        float hottest = vars.temp_bed;
        for (auto tool : PhysicalToolIndex::all().skip_all_disabled()) {
            hottest = std::max<float>(hottest, vars.hotend(tool).temp_nozzle);
        }
        return hottest;
    }

    /// Takes the lock of the status messages
    bool print_waits_for_heat() {
        const auto type = print_status_message().current_message().message.type;
        return type == PrintStatusMessage::waiting_for_hotend_temp
            || type == PrintStatusMessage::waiting_for_bed_temp
    #if HAS_CHAMBER_API()
            // Enclosure soak and M191
            || type == PrintStatusMessage::waiting_for_chamber_temp
    #endif
            || type == PrintStatusMessage::absorbing_heat;
    }

    /// Takes the lock of the status messages
    bool bed_cooldown_runs() {
    #if XL_ENCLOSURE_SUPPORT()
        return print_status_message().current_message().message.type == PrintStatusMessage::bed_cooldown;
    #else
        return false;
    #endif
    }

    /// @returns whether the dialog of a busy printer waits for the owner
    bool waits_for_owner(const fsm::States::Top &top) {
        const PhaseUnderlyingType phase = top.data.GetPhase();

        if (top.fsm_type == ClientFSM::Load_unload) {
            // Loading and unloading also offer Stop and Retry, only these phases wait
            switch (GetEnumFromPhaseIndex<PhasesLoadUnload>(phase)) {
            case PhasesLoadUnload::IsFilamentUnloaded:
            case PhasesLoadUnload::FilamentNotInFS:
            case PhasesLoadUnload::ManualUnload_continuable:
            case PhasesLoadUnload::ManualUnload_uncontinuable:
            case PhasesLoadUnload::UserPush_stoppable:
            case PhasesLoadUnload::UserPush_unstoppable:
            case PhasesLoadUnload::MakeSureInserted_stoppable:
            case PhasesLoadUnload::MakeSureInserted_unstoppable:
            case PhasesLoadUnload::IsFilamentInGear:
            case PhasesLoadUnload::AwaitingFilament_stoppable:
            case PhasesLoadUnload::AwaitingFilament_unstoppable:
            case PhasesLoadUnload::IsColor:
            case PhasesLoadUnload::IsColorPurge:
    #if HAS_SIDE_FSENSOR() && HAS_EXTRUDER_FSENSOR()
            case PhasesLoadUnload::LoadingObstruction_stoppable:
            case PhasesLoadUnload::LoadingObstruction_unstoppable:
    #endif
    #if HAS_LOADCELL() && HAS_EXTRUDER_FSENSOR()
            case PhasesLoadUnload::FilamentStuck:
    #endif
                return true;
            default:
                return false;
            }
        }

        // A phase that runs on its own offers at most Abort
        const auto &responses = ClientResponses::get_available_responses(FSMAndPhase(top.fsm_type, phase));
        return std::ranges::any_of(responses, [](Response response) {
            return response != Response::_none && response != Response::Abort;
        });
    }

} // namespace

side_strip_status::Inputs SideStripHandler::read_inputs(uint32_t now_ms) {
    using printer_state::DeviceState;
    using side_strip_status::Activity;

    side_strip_status::Inputs inputs;

    const StateAnimation animation = StatusLedsHandler::instance().current_animation();
    if (animation == StateAnimation::Error) {
        // Possibly a redscreen: marlin_vars are not touched
        inputs.error = true;
        return inputs;
    }

    auto &vars = marlin_vars();
    std::optional<fsm::States::Top> top;
    vars.peek_fsm_states([&](const fsm::States &states) {
        top = states.get_top();
    });
    const DeviceState device_state = printer_state::get_state();

    was_hot = side_strip_status::is_above(hottest_heater(), side_strip_status::hot_temp, side_strip_status::hot_off_temp, was_hot);
    was_bed_warm = side_strip_status::is_above(vars.temp_bed, side_strip_status::bed_warm_temp, side_strip_status::bed_cool_temp, was_bed_warm);
    inputs.hot = was_hot;

    switch (animation) {
    case StateAnimation::Error:
        // Returned above
        break;

    case StateAnimation::Warning:
        inputs.waits_for_owner = true;
        break;

    case StateAnimation::Printing:
        inputs.activity = Activity::printing;
        inputs.heating = print_waits_for_heat();
        break;

    case StateAnimation::Finished:
        inputs.activity = Activity::finished;
        inputs.heating = any_heater_warming_up();
        inputs.bed_warm = was_bed_warm || bed_cooldown_runs();
        break;

    case StateAnimation::Idle:
    case StateAnimation::Aborted:
        inputs.activity = Activity::idle;
        inputs.heating = any_heater_warming_up();
        break;

    case StateAnimation::PowerPanic:
    case StateAnimation::PowerUp:
        inputs.activity = Activity::untinted;
        break;
    }

    // Outside a print every dialog runs on its own or waits for the owner, except the preheat menu.
    // A serial print also reports busy, it shows as printing.
    inputs.busy = device_state == DeviceState::Busy
        && inputs.activity != Activity::printing
        && !(top.has_value() && top->fsm_type == ClientFSM::Preheat);

    inputs.waits_for_owner = inputs.waits_for_owner
        || device_state == DeviceState::Attention
        || device_state == DeviceState::Paused
        || (inputs.busy && top.has_value() && waits_for_owner(*top));

    // G-code without a dialog, such as a macro. Short commands, also the printer's own, do not count.
    if (animation == StateAnimation::Idle && !top.has_value() && vars.is_processing) {
        if (!gcode_since_ms.has_value()) {
            gcode_since_ms = now_ms;
        }
    } else {
        gcode_since_ms.reset();
    }
    if (gcode_since_ms.has_value() && ticks_diff(now_ms, *gcode_since_ms) >= static_cast<int32_t>(side_strip_status::gcode_busy_hold_ms)) {
        inputs.busy = true;
    }

    return inputs;
}
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
    std::optional<side_strip_status::Inputs> new_inputs;
    uint8_t new_percent_done = 0;
    if (const uint32_t now = ticks_ms(); mode != SideStripStatusMode::off && ticks_diff(now, status_read_ms) >= static_cast<int32_t>(status_read_interval_ms)) {
        status_read_ms = now;
        new_inputs = read_inputs(now);
        if (new_inputs->activity == side_strip_status::Activity::printing) {
            new_percent_done = marlin_vars().sd_percent_done;
        }
    }
#endif

    std::lock_guard lock(mutex);
    const uint32_t time_ms = ticks_ms();

#if PRINTER_IS_PRUSA_XL()
    if (new_inputs.has_value()) {
        status_inputs = *new_inputs;
        percent_done = new_percent_done;
    }
    const bool user_present = active_timestamp_ms != 0 && time_ms - active_timestamp_ms < active_timeout_ms;
    const bool tint_changed = update_tint(mode, time_ms, user_present);
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
        fade_to(get_color_for_state(state), side_strip_status::transition_ms(printer_status));
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

bool SideStripHandler::update_tint(SideStripStatusMode mode, uint32_t time_ms, bool user_present) {
    side_strip_status::Inputs inputs = status_inputs;
    inputs.user_present = user_present;
    printer_status = side_strip_status::resolve(inputs);
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
    ColorRGBW color = controller_instance().color();
#if PRINTER_IS_PRUSA_XL()
    if (held_white.has_value()) {
        color.w = *held_white;
    }
#endif
    return color;
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
        fade_to(get_color_for_state(state), duration);
    }
}

void SideStripHandler::fade_to(ColorRGBW color, uint32_t duration_ms) {
    auto &controller = controller_instance();
#if PRINTER_IS_PRUSA_XL()
    // The controller's blend can round a channel that keeps its value one step down. Holding the
    // white keeps the second LED driver word, which also carries the enclosure fan, unchanged while
    // only the tint moves.
    const uint8_t shown_white = held_white.value_or(controller.color().w);
    const bool keeps_white = status_mode != SideStripStatusMode::off && shown_white == color.w;
    held_white = keeps_white ? std::optional<uint8_t>(color.w) : std::nullopt;
#endif
    controller.set(color, duration_ms);
}

ColorRGBW SideStripHandler::get_color_for_state(SideStripState state) {
    constexpr auto base_color = has_white_led() ? ColorRGBW(0, 0, 0, 255) : ColorRGBW(255, 255, 255);

    switch (state) {
    case SideStripState::off:
        return ColorRGBW();
    case SideStripState::dimmed:
        return with_tint(base_color.clamp(dimmed_brightness));
    case SideStripState::active:
        return with_tint(base_color.clamp(max_brightness));
    case SideStripState::custom_color:
        return ColorRGBW(custom_color->color);
    }
    return {};
}

ColorRGBW SideStripHandler::with_tint(ColorRGBW plain) const {
#if PRINTER_IS_PRUSA_XL()
    if (tint.has_value()) {
        // The dimming sets how much white is mixed in; the status colours keep the full brightness
        plain.w = side_strip_status::white_under(printer_status, plain.w);
        return side_strip_status::apply_tint(plain, *tint, max_brightness);
    }
#endif
    return plain;
}

} // namespace leds
