
#include "MItem_enclosure.hpp"
#include "img_resources.hpp"
#include "WindowMenuSpin.hpp"
#include "xl_enclosure.hpp"
#include "ScreenHandler.hpp"
#include "screen_change_filter.hpp"
#include <screen_menu_enclosure.hpp>
#include <window_dlg_wait.hpp>
#include <config_store/store_instance.hpp>
#include <feature/chamber/bed_cooldown_ramp.hpp>

#include <cmath>

/* Once is Enclosure enabled in menu with ON/OFF switch (MI_ENCLOSURE_ENABLED), it tests the fan and after that Enclosure is declared Active */
/* If test was passed, MI_ENCLOSURE_ENABLE is swapped with MI_ENCLOSURE and enclosure settings can be accessed */
/* This hides enclosure settings for Users without enclosure */
MI_ENCLOSURE::MI_ENCLOSURE()
    : IWindowMenuItem(_(label), nullptr, is_enabled_t::yes, xl_enclosure.is_active() ? is_hidden_t::no : is_hidden_t::yes, expands_t::yes) {
}

void MI_ENCLOSURE::click(IWindowMenu & /*window_menu*/) {
    Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuEnclosure>);
}

MI_ENCLOSURE_ENABLE::MI_ENCLOSURE_ENABLE()
    : WI_ICON_SWITCH_OFF_ON_t(xl_enclosure.is_enabled(), _(label), nullptr, is_enabled_t::yes, xl_enclosure.is_active() ? is_hidden_t::yes : is_hidden_t::no) {
    /* Swapping of Menu Items MI_ENCLOSURE_ENABLE and MI_ENCLOSURE (See comment above) */
}

void MI_ENCLOSURE_ENABLE::OnChange([[maybe_unused]] size_t old_index) {
    xl_enclosure.set_enabled(value());
    if (value()) {
        /* Wait until enclosure is initialized & ready (test takes 3s). If initialization fails, the enclosure gets disabled internally. */
        window_dlg_wait_t::wait_until(_(wait_str), [] {
            return xl_enclosure.is_active() || !xl_enclosure.is_enabled();
        });

        if (!xl_enclosure.is_enabled()) {
            set_value(false);
        }
    }
}

MI_ENCLOSURE_BED_HEATING::MI_ENCLOSURE_BED_HEATING()
    : WI_ICON_SWITCH_OFF_ON_t(config_store().xl_enclosure_bed_heating.get(), _("Chamber Heating")) {
}

void MI_ENCLOSURE_BED_HEATING::OnChange([[maybe_unused]] size_t old_index) {
    config_store().xl_enclosure_bed_heating.set(value());
}

static constexpr NumericInputConfig soak_max_config {
    .min_value = 0,
    .max_value = 60,
    .special_value = 0,
    .unit = Unit::minute,
};

MI_ENCLOSURE_SOAK_MAX::MI_ENCLOSURE_SOAK_MAX()
    : WiSpin(config_store().xl_enclosure_soak_max_min.get(), soak_max_config, _("Max Soak Time")) {
}

void MI_ENCLOSURE_SOAK_MAX::OnClick() {
    config_store().xl_enclosure_soak_max_min.set(static_cast<uint8_t>(value()));
}

void MI_ENCLOSURE_SOAK_MAX::Loop() {
    set_enabled(config_store().xl_enclosure_bed_heating.get());
}

/// The setting is stored in 0.1 °C/min
static constexpr NumericInputConfig cooldown_rate_config {
    .min_value = buddy::BedCooldownRamp::min_rate / 10.0f,
    .max_value = buddy::BedCooldownRamp::max_rate / 10.0f,
    .step = 0.1f,
    .special_value = 0,
    .max_decimal_places = 1,
    .unit = Unit::celsius_per_minute,
};

MI_ENCLOSURE_COOLDOWN_RATE::MI_ENCLOSURE_COOLDOWN_RATE()
    : WiSpin(config_store().xl_enclosure_cooldown_rate.get() / 10.0f, cooldown_rate_config, _("Cooldown Rate")) {
}

void MI_ENCLOSURE_COOLDOWN_RATE::OnClick() {
    config_store().xl_enclosure_cooldown_rate.set(static_cast<uint8_t>(std::lround(value() * 10)));
}

void MI_ENCLOSURE_COOLDOWN_RATE::Loop() {
    set_enabled(config_store().xl_enclosure_bed_heating.get());
}

static constexpr NumericInputConfig soak_fan_config {
    .min_value = 0,
    .max_value = 100,
    .step = 5,
    .special_value = 0,
    .unit = Unit::percent,
};

MI_ENCLOSURE_SOAK_FAN::MI_ENCLOSURE_SOAK_FAN()
    : WiSpin(config_store().xl_enclosure_soak_fan_percent.get(), soak_fan_config, _("Soak Fan")) {
}

void MI_ENCLOSURE_SOAK_FAN::OnClick() {
    config_store().xl_enclosure_soak_fan_percent.set(static_cast<uint8_t>(value()));
}

void MI_ENCLOSURE_SOAK_FAN::Loop() {
    set_enabled(config_store().xl_enclosure_bed_heating.get());
}

MI_ENCLOSURE_OVERTEMP_VENTING::MI_ENCLOSURE_OVERTEMP_VENTING()
    : WI_ICON_SWITCH_OFF_ON_t(config_store().xl_enclosure_overtemp_venting.get(), _("Vent When Too Warm")) {
}

void MI_ENCLOSURE_OVERTEMP_VENTING::OnChange([[maybe_unused]] size_t old_index) {
    config_store().xl_enclosure_overtemp_venting.set(value());
}
