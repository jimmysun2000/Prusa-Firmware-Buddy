/// @file
#include "screen_bed_tiles.hpp"

#include <common/marlin_vars.hpp>
#include <display.hpp>
#include <display_helper.h>
#include <i18n.h>
#include <puppies/modular_bed.hpp>
#include <sound.hpp>
#include <timing.h>
#include <utils/string_builder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr uint16_t gap = 6;
constexpr int16_t grid_left = 15;
constexpr int16_t grid_top = GuiDefaults::HeaderHeight + 4;
constexpr uint16_t grid_width = GuiDefaults::ScreenWidth - 2 * grid_left;
constexpr uint16_t current_line_height = height(Font::small);
constexpr int16_t current_line_top = GuiDefaults::ScreenHeight - current_line_height - 1;
constexpr uint16_t grid_height = current_line_top - 1 - grid_top;

constexpr uint16_t tile_width = (grid_width - (X_HBL_COUNT - 1) * gap) / X_HBL_COUNT;
constexpr uint16_t tile_height = (grid_height - (Y_HBL_COUNT - 1) * gap) / Y_HBL_COUNT;
static_assert(tile_height >= 4 + 2 * height(Font::small) + height(Font::big), "tile must fit number, temperature and target");

constexpr uint16_t padding = 4;

constexpr uint32_t update_period_ms = 1000;

Rect16 tile_rect(uint8_t column, uint8_t row) {
    // Row 0 (the front of the bed) is at the bottom, as in the footer bed icon
    return Rect16(grid_left + column * (tile_width + gap), grid_top + (Y_HBL_COUNT - 1 - row) * (tile_height + gap), tile_width, tile_height);
}

/// Right end of the number row of a tile, wide enough for "100%"
Rect16 duty_rect(Rect16 tile) {
    constexpr uint16_t duty_width = 4 * width(Font::small);
    return Rect16(tile.Left() + tile.Width() - 2 - padding - duty_width, tile.Top() + 2, duty_width, height(Font::small));
}

} // namespace

void WindowBedTile::set_values(const Values &values) {
    if (values != values_) {
        Values same_duty = values_;
        same_duty.duty = values.duty;
        // The duty changes every second while heating and a full redraw would make the temperature flicker,
        // so a valid background marks that only the duty is to be redrawn
        const bool duty_only = HasValidBackground() && values == same_duty;
        values_ = values;
        Invalidate();
        if (duty_only) {
            ValidateBackground();
        }
    }
}

void WindowBedTile::draw_duty() {
    std::array<char, 8> buffer;
    snprintf(buffer.data(), buffer.size(), "%u%%", static_cast<unsigned>(values_.duty));
    render_text_align(duty_rect(GetRect()), string_view_utf8::MakeRAM(buffer.data()), Font::small, COLOR_BLACK, values_.duty > 0 ? COLOR_WHITE : COLOR_GRAY, {}, text_flags(Align_t::RightCenter()));
}

void WindowBedTile::unconditionalDraw() {
    if (HasValidBackground()) {
        // Only the duty changed
        draw_duty();
        return;
    }

    const Rect16 rect = GetRect();
    const Rect16 inner = Rect16(rect.Left() + 2, rect.Top() + 2, rect.Width() - 4, rect.Height() - 4);

    display::fill_rect(inner, COLOR_BLACK);
    if (values_.enabled) {
        display::draw_rect(rect, COLOR_BRAND);
        display::draw_rect(Rect16(rect.Left() + 1, rect.Top() + 1, rect.Width() - 2, rect.Height() - 2), COLOR_BRAND);
    } else {
        display::draw_rect(rect, COLOR_DARK_GRAY);
        display::draw_rect(Rect16(rect.Left() + 1, rect.Top() + 1, rect.Width() - 2, rect.Height() - 2), COLOR_BLACK);
    }

    const int16_t content_left = inner.Left() + padding;
    const uint16_t content_width = inner.Width() - 2 * padding;

    std::array<char, 24> buffer;

    snprintf(buffer.data(), buffer.size(), "#%u", static_cast<unsigned>(number_));
    render_text_align(Rect16(content_left, inner.Top(), content_width, height(Font::small)), string_view_utf8::MakeRAM(buffer.data()),
        Font::small, COLOR_BLACK, COLOR_GRAY, {}, text_flags(Align_t::LeftCenter()));

    snprintf(buffer.data(), buffer.size(), "%d\xC2\xB0\x43", values_.temp);
    render_text_align(Rect16(content_left, inner.Top() + height(Font::small), content_width, height(Font::big)), string_view_utf8::MakeRAM(buffer.data()),
        Font::big, COLOR_BLACK, COLOR_WHITE, {}, text_flags(Align_t::Center()));

    const Rect16 target_rect(content_left, inner.Top() + inner.Height() - height(Font::small), content_width, height(Font::small));
    if (values_.target > 0) {
        // No unit, so that "Target 100" fits the tile
        StringBuilder sb(buffer);
        sb.append_string_view(_("Target"));
        sb.append_printf(" %d", values_.target);
        render_text_align(target_rect, string_view_utf8::MakeRAM(buffer.data()), Font::small, COLOR_BLACK, COLOR_WHITE, {}, text_flags(Align_t::Center()));
    } else {
        render_text_align(target_rect, _("Off"), Font::small, COLOR_BLACK, COLOR_GRAY, {}, text_flags(Align_t::Center()));
    }

    draw_duty();
}

ScreenBedTiles::ScreenBedTiles()
    : screen_t()
    , header(this, _("BED TILES"))
    , current_text(this, Rect16(grid_left, current_line_top, grid_width, current_line_height), is_multiline::no) {
    ClrMenuTimeoutClose();

    current_text.set_font(Font::small);
    current_text.SetAlignment(Align_t::LeftCenter());
    current_buffer[0] = '\0';
    current_text.SetText(string_view_utf8::MakeRAM(current_buffer.data()));

    for (uint8_t row = 0; row < Y_HBL_COUNT; row++) {
        for (uint8_t column = 0; column < X_HBL_COUNT; column++) {
            WindowBedTile &tile = tiles[row * X_HBL_COUNT + column];
            tile.SetRect(tile_rect(column, row));
            tile.set_number(buddy::puppies::bedlet_idx_to_board_number(advanced_modular_bed->idx(column, row)));
            RegisterSubWin(tile);
        }
    }

    update();
}

void ScreenBedTiles::update() {
    // Like the footer: the enabled bedlets matter only while the bed is heating
    const uint16_t enabled_mask = (marlin_vars().target_bed > 0) ? marlin_vars().enabled_bedlet_mask.get() : 0;

    for (uint8_t row = 0; row < Y_HBL_COUNT; row++) {
        for (uint8_t column = 0; column < X_HBL_COUNT; column++) {
            const uint16_t idx = advanced_modular_bed->idx(column, row);
            tiles[row * X_HBL_COUNT + column].set_values({
                .temp = static_cast<int16_t>(std::lround(advanced_modular_bed->get_temp(column, row))),
                .target = static_cast<int16_t>(std::lround(buddy::puppies::modular_bed.get_written_target(column, row))),
                .enabled = (enabled_mask & (1 << idx)) != 0,
                .duty = buddy::puppies::modular_bed.get_heater_duty_percent(column, row),
            });
        }
    }

    const int32_t current_tenths = std::max<int32_t>(0, std::lround(buddy::puppies::modular_bed.get_heater_current() * 10.0f));
    if (current_tenths != shown_current_tenths) {
        shown_current_tenths = current_tenths;
        StringBuilder sb(current_buffer);
        sb.append_string_view(_("Heater current"));
        sb.append_printf(" %ld.%ld A", static_cast<long>(current_tenths / 10), static_cast<long>(current_tenths % 10));
        current_text.Invalidate();
    }
}

void ScreenBedTiles::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    switch (event) {

    case GUI_event_t::CLICK:
    case GUI_event_t::TOUCH_SWIPE_LEFT:
    case GUI_event_t::TOUCH_SWIPE_RIGHT:
        sound::play(SoundType::button_echo);
        close_screen();
        return;

    case GUI_event_t::LOOP: {
        const uint32_t now = ticks_ms();
        if (ticks_diff(now, last_update_ms) >= static_cast<int32_t>(update_period_ms)) {
            last_update_ms = now;
            update();
        }
        break;
    }

    default:
        break;
    }

    screen_t::windowEvent(sender, event, param);
}
