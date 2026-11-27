/// @file
#include "screen_bed_mesh.hpp"

#include <DialogHandler.hpp>
#include <MItem_tools.hpp>
#include <ScreenFactory.hpp>
#include <ScreenHandler.hpp>
#include <common/bed_mesh_view.hpp>
#include <common/marlin_vars.hpp>
#include <config_store/store_instance.hpp>
#include <dialog_numeric_input.hpp>
#include <display.hpp>
#include <display_helper.h>
#include <i18n.h>
#include <marlin_client.hpp>
#include <marlin_stubs/M9170.hpp>
#include <numeric_input_config_common.hpp>
#include <sound.hpp>
#include <timing.h>
#include <tool_index.hpp>
#include <utils/string_builder.hpp>
#include <window_msgbox.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

namespace {

constexpr Font font = Font::small;
constexpr uint16_t line_height = height(font);

constexpr int16_t body_top = 50;
constexpr Rect16 window_rect(0, body_top, GuiDefaults::ScreenWidth, GuiDefaults::ScreenHeight - body_top);
constexpr Rect16 grid_rect(14, body_top, 252, 252);
constexpr Rect16 front_label_rect(14, body_top + 252 + 1, 252, line_height);

constexpr int16_t panel_left = 276;
constexpr uint16_t panel_width = 200;
constexpr uint16_t label_width = 70;
constexpr int16_t stats_row_step = 22;
constexpr int16_t legend_caption_top = 124;
constexpr Rect16 legend_bar_rect(panel_left, 144, 192, 14);
constexpr int16_t legend_labels_top = 160;
constexpr Rect16 status_rect(panel_left, 196, panel_width, 3 * line_height);
constexpr Rect16 probed_rect(panel_left, 250, panel_width, line_height);
constexpr Rect16 buttons_rect(panel_left, 280, 192, 32);
static_assert(probed_rect.Bottom() < buttons_rect.Top());

constexpr IRadioButton::Responses_t back_only { Response::Back, Response::_none, Response::_none, Response::_none };
constexpr IRadioButton::Responses_t back_and_measure { Response::Back, Response::Calibrate, Response::_none, Response::_none };

constexpr Color no_data_color = COLOR_DARK_GRAY;
constexpr uint16_t probed_dot_size = 3;

constexpr uint32_t update_period_ms = 1000;
constexpr uint32_t auto_close_ms = 10000;

constexpr uint32_t popup_check_period_ms = 250;
/// How long the probing must be over before the popup appears; bridges the short gaps between the G29 calls of the start G-code
constexpr uint32_t popup_idle_ms = 1000;

void draw_text(Rect16 rect, const string_view_utf8 &text, Align_t align, Color color = COLOR_WHITE) {
    render_text_align(rect, text, font, COLOR_BLACK, color, {}, text_flags(align));
}

void draw_number(Rect16 rect, const char *format, float value, Align_t align) {
    std::array<char, 16> buffer;
    snprintf(buffer.data(), buffer.size(), format, static_cast<double>(value));
    draw_text(rect, string_view_utf8::MakeRAM(buffer.data()), align);
}

Color to_color(bed_mesh_view::Rgb rgb) {
    return Color::from_rgb(rgb.r, rgb.g, rgb.b);
}

void draw_grid(const bed_mesh_view::Info &info, const bed_mesh_view::Stats &stats, bool has_data) {
    std::array<Color, bed_mesh_view::color_steps> palette;
    for (uint8_t i = 0; i < palette.size(); i++) {
        palette[i] = to_color(bed_mesh_view::color_of_step(i));
    }

    const uint16_t cell = std::min(grid_rect.Width() / info.nx, grid_rect.Height() / info.ny);
    const auto cell_color = [&](uint8_t ix, uint8_t iy) {
        const float v = bed_mesh_view::z(ix, iy);
        if (!has_data || !std::isfinite(v)) {
            return no_data_color;
        }
        return palette[bed_mesh_view::color_step(v - stats.mean, stats.full_scale)];
    };
    const int16_t dot_offset = (cell - probed_dot_size) / 2;

    for (uint8_t iy = 0; iy < info.ny; iy++) {
        // The front of the bed (y index 0) is at the bottom
        const int16_t top = grid_rect.Top() + (info.ny - 1 - iy) * cell;

        // Cells of the same colour are drawn at once; a display transfer is expensive
        uint8_t run_start = 0;
        Color run_color = cell_color(0, iy);
        for (uint8_t ix = 1; ix <= info.nx; ix++) {
            const Color color = (ix < info.nx) ? cell_color(ix, iy) : run_color;
            if (ix == info.nx || color != run_color) {
                display::fill_rect(Rect16(grid_rect.Left() + run_start * cell, top, (ix - run_start) * cell, cell), run_color);
                run_start = ix;
                run_color = color;
            }
        }

        // A dot tells a measured point from one the leveling filled in
        if (has_data) {
            for (uint8_t ix = 0; ix < info.nx; ix++) {
                if (bed_mesh_view::probed(ix, iy)) {
                    display::fill_rect(Rect16(grid_rect.Left() + ix * cell + dot_offset, top + dot_offset, probed_dot_size, probed_dot_size), COLOR_BLACK);
                }
            }
        }
    }
}

void draw_panel(const bed_mesh_view::Stats &stats, WindowBedMesh::Status status, uint16_t probed_count) {
    using Status = WindowBedMesh::Status;
    const bool has_stats = stats.count > 0 && status != Status::no_mesh;

    const auto draw_stat = [&](int row, const char *label, const char *format, float value) {
        const int16_t top = body_top + row * stats_row_step;
        draw_text(Rect16(panel_left, top, label_width, line_height), _(label), Align_t::LeftCenter());
        const Rect16 value_rect(panel_left + label_width, top, panel_width - label_width, line_height);
        if (has_stats) {
            draw_number(value_rect, format, value, Align_t::RightCenter());
        } else {
            draw_text(value_rect, string_view_utf8::MakeCPUFLASH("-"), Align_t::RightCenter());
        }
    };
    draw_stat(0, N_("Min"), "%+.3f mm", stats.min);
    draw_stat(1, N_("Max"), "%+.3f mm", stats.max);
    draw_stat(2, N_("Range"), "%.3f mm", stats.max - stats.min);

    draw_text(Rect16(panel_left, legend_caption_top, panel_width, line_height), _("Deviation from mean"), Align_t::LeftCenter(), COLOR_GRAY);

    const uint16_t step_width = legend_bar_rect.Width() / bed_mesh_view::color_steps;
    for (uint8_t i = 0; i < bed_mesh_view::color_steps; i++) {
        display::fill_rect(Rect16(legend_bar_rect.Left() + i * step_width, legend_bar_rect.Top(), step_width, legend_bar_rect.Height()), to_color(bed_mesh_view::color_of_step(i)));
    }

    const uint16_t legend_label_width = legend_bar_rect.Width() / 3;
    draw_number(Rect16(legend_bar_rect.Left(), legend_labels_top, legend_label_width, line_height), "%.2f", -stats.full_scale, Align_t::LeftCenter());
    draw_text(Rect16(legend_bar_rect.Left() + legend_label_width, legend_labels_top, legend_label_width, line_height), string_view_utf8::MakeCPUFLASH("mm"), Align_t::Center(), COLOR_GRAY);
    draw_number(Rect16(legend_bar_rect.Left() + 2 * legend_label_width, legend_labels_top, legend_label_width, line_height), "%+.2f", stats.full_scale, Align_t::RightCenter());

    display::fill_rect(status_rect, COLOR_BLACK);
    const char *status_text = nullptr;
    switch (status) {
    case Status::no_mesh:
        status_text = N_("No mesh measured yet.");
        break;
    case Status::probing:
        status_text = N_("Probing in progress...");
        break;
    case Status::incomplete:
        status_text = N_("Mesh is incomplete.");
        break;
    case Status::complete:
        break;
    }
    if (status_text) {
        render_text_align(status_rect, _(status_text), font, COLOR_BLACK, COLOR_WHITE, {}, text_flags(Align_t::LeftTop(), is_multiline::yes));
    }

    std::array<char, 32> probed_buffer;
    StringBuilder sb(probed_buffer);
    sb.append_string_view(_("Probed points"));
    if (probed_count > 0) {
        sb.append_printf(": %u", static_cast<unsigned>(probed_count));
    } else {
        // Also after a power panic resume, which keeps the mesh but not which points were measured
        sb.append_string(": -");
    }
    draw_text(probed_rect, string_view_utf8::MakeRAM(probed_buffer.data()), Align_t::LeftCenter());
}

struct PopupTracker {
    uint32_t last_check_ms = 0;

    /// Last time the mesh probing was seen running
    uint32_t last_busy_ms = 0;

    uint16_t job_id = 0;

    /// Probing was seen during this job
    bool armed = false;

    /// The popup was already shown for this job
    bool done = false;
};

PopupTracker popup_tracker;

constexpr const char *printer_busy_text = N_("Printer is busy. Please try repeating the action later.");

/// No print, pause or other G-code is running
bool printer_free() {
    return marlin_client::is_idle() && !marlin_vars().is_processing.get();
}

/// @returns how many points a measurement of the whole bed probes
unsigned whole_bed_point_count() {
    const auto info = bed_mesh_view::info();
    const auto major_points = [&](uint8_t n) {
        unsigned count = 0;
        for (uint8_t i = 0; i < n; i++) {
            count += bed_mesh_view::is_major(i, n, info.border, info.major_step);
        }
        return count;
    };
    return major_points(info.nx) * major_points(info.ny);
}

/// Asks for the bed temperature and a confirmation, then starts the measurement with the picked tool
void start_measurement() {
    if (!printer_free()) {
        MsgBoxWarning(_(printer_busy_text), Responses_Ok);
        return;
    }

    const std::optional<PhysicalToolIndex> tool = PhysicalToolIndex::currently_selected_opt();
    if (!tool.has_value()) {
        MsgBoxWarning(_("No tool is picked. Pick a tool first."), Responses_Ok);
        return;
    }

    const DialogNumericInput::Result entered = DialogNumericInput::exec(_("Bed temperature for the measurement"), marlin_vars().target_bed.get(), numeric_input_config::bed_temperature);
    if (!entered.has_value()) {
        return;
    }
    const int bed_temperature = static_cast<int>(std::lround(*entered));

    const unsigned tool_number = tool->to_raw() + 1;
    const int nozzle_temperature = bed_mesh_measurement::nozzle_temperature;
    StringViewUtf8Parameters<32> params;
    const string_view_utf8 question = (bed_temperature > 0)
        ? _("The bed will be heated to %d °C and the nozzle of tool %u to %d °C. Then the nozzle travels just above the whole bed and taps it %u times. Remove any print and check the steel sheet. Continue?")
              .formatted(params, bed_temperature, tool_number, nozzle_temperature, whole_bed_point_count())
        : _("The bed heater will be off and the nozzle of tool %u heated to %d °C. Then the nozzle travels just above the whole bed and taps it %u times. Remove any print and check the steel sheet. Continue?")
              .formatted(params, tool_number, nozzle_temperature, whole_bed_point_count());
    if (MsgBoxWarning(question, Responses_YesNo, 1) != Response::Yes) {
        return;
    }

    // A print can start remotely, or the tool can change, while the questions are shown
    if (!printer_free() || PhysicalToolIndex::currently_selected_opt() != tool) {
        MsgBoxWarning(_(printer_busy_text), Responses_Ok);
        return;
    }

    ArrayStringBuilder<sizeof("M9170 S999")> gcode;
    gcode.append_printf("M9170 S%d", bed_temperature);
    gui_try_gcode_with_msg(gcode.str());
}

} // namespace

WindowBedMesh::WindowBedMesh(window_t *parent, Rect16 rect)
    : window_t(parent, rect)
    , revision_(bed_mesh_view::revision())
    , status_(read_status()) {
}

WindowBedMesh::Status WindowBedMesh::read_status() {
    switch (bed_mesh_view::state()) {
    case bed_mesh_view::State::none:
        return Status::no_mesh;
    case bed_mesh_view::State::partial:
        return (marlin_vars().gcode_command.get() == marlin_server::Cmd::G29) ? Status::probing : Status::incomplete;
    case bed_mesh_view::State::complete:
        return Status::complete;
    }
    return Status::no_mesh;
}

void WindowBedMesh::update() {
    const uint32_t revision = bed_mesh_view::revision();
    const Status status = read_status();
    if (revision != revision_ || status != status_) {
        revision_ = revision;
        status_ = status;
        Invalidate();
    }
}

void WindowBedMesh::unconditionalDraw() {
    const auto info = bed_mesh_view::info();
    const auto stats = bed_mesh_view::compute_stats(info, bed_mesh_view::z);

    draw_grid(info, stats, status_ != Status::no_mesh);
    draw_text(front_label_rect, _("FRONT"), Align_t::Center(), COLOR_GRAY);
    draw_panel(stats, status_, bed_mesh_view::count_probed(info, bed_mesh_view::probed));
}

ScreenBedMesh::ScreenBedMesh(bool auto_opened)
    : screen_t()
    , header(this, _("BED MESH"))
    , mesh(this, window_rect)
    , buttons(this, buttons_rect, back_only)
    , opened_ms(ticks_ms())
    , last_update_ms(opened_ms)
    , auto_opened(auto_opened) {
    ClrMenuTimeoutClose();
    CaptureNormalWindow(buttons);
    update_buttons();
}

void ScreenBedMesh::update_buttons() {
    buttons.Change((!auto_opened && marlin_client::is_idle()) ? back_and_measure : back_only);
}

void ScreenBedMesh::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    switch (event) {

    // The buttons also turn a swipe into Back
    case GUI_event_t::CHILD_CLICK:
        sound::play(SoundType::button_echo);
        if (event_conversion_union { .pvoid = param }.response == Response::Calibrate) {
            start_measurement();
        } else {
            close_screen();
        }
        return;

    case GUI_event_t::LOOP: {
        const uint32_t now = ticks_ms();
        if (auto_opened && ticks_diff(now, opened_ms) >= static_cast<int32_t>(auto_close_ms)) {
            close_screen();
            return;
        }
        if (ticks_diff(now, last_update_ms) >= static_cast<int32_t>(update_period_ms)) {
            last_update_ms = now;
            mesh.update();
            update_buttons();
        }
        break;
    }

    default:
        break;
    }

    screen_t::windowEvent(sender, event, param);
}

void bed_mesh_popup_after_probing(bool printing) {
    PopupTracker &tracker = popup_tracker;
    const uint32_t now = ticks_ms();
    if (ticks_diff(now, tracker.last_check_ms) < static_cast<int32_t>(popup_check_period_ms)) {
        return;
    }
    tracker.last_check_ms = now;

    if (!config_store().show_bed_mesh_after_probing.get()) {
        return;
    }

    const uint16_t job_id = marlin_vars().job_id;
    if (job_id != tracker.job_id) {
        tracker = PopupTracker { .last_check_ms = now, .job_id = job_id };
    }
    if (tracker.done) {
        return;
    }

    const auto state = bed_mesh_view::state();
    if (marlin_vars().gcode_command.get() == marlin_server::Cmd::G29 || state == bed_mesh_view::State::partial) {
        tracker.armed = true;
        tracker.last_busy_ms = now;
        return;
    }

    if (!tracker.armed || !printing || state != bed_mesh_view::State::complete) {
        return;
    }
    if (ticks_diff(now, tracker.last_busy_ms) < static_cast<int32_t>(popup_idle_ms) || DialogHandler::Access().IsOpen()) {
        return;
    }

    tracker.done = true;
    Screens::Access()->Open(ScreenFactory::ScreenWithArg<ScreenBedMesh>(true));
}
