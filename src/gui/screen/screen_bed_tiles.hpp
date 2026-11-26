/// @file
#pragma once

#include <screen.hpp>
#include <window_header.hpp>
#include <window_text.hpp>

#include <option/has_modular_bed.h>
#include <module/modular_heatbed.h>

#include <array>
#include <cstdint>

static_assert(HAS_MODULAR_BED());

/// One heatbedlet tile. Display only.
class WindowBedTile final : public window_t {
public:
    struct Values {
        /// Measured temperature, degrees Celsius
        int16_t temp = 0;

        /// Target temperature, degrees Celsius; 0 = off
        int16_t target = 0;

        /// The bedlet is in the heated area of the current job
        bool enabled = false;

        bool operator==(const Values &) const = default;
    };

    WindowBedTile() = default;

    void set_number(uint8_t number) { number_ = number; }

    /// Redraws the tile if the values differ from the shown ones
    void set_values(const Values &values);

protected:
    void unconditionalDraw() override;

private:
    Values values_;
    uint8_t number_ = 0;
};

/// Temperatures, targets and enabled state of the individual heatbedlets, and the total heater current.
class ScreenBedTiles final : public screen_t {
public:
    ScreenBedTiles();

protected:
    void windowEvent(window_t *sender, GUI_event_t event, void *param) override;

private:
    void update();

    window_header_t header;

    /// Row-major, row 0 is the front of the bed
    std::array<WindowBedTile, X_HBL_COUNT * Y_HBL_COUNT> tiles;

    window_text_t current_text;
    std::array<char, 40> current_buffer;

    /// Shown heater current in tenths of an ampere, -1 before the first update
    int32_t shown_current_tenths = -1;

    uint32_t last_update_ms = 0;
};
