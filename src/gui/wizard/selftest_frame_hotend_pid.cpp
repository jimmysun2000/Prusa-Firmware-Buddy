#include "selftest_frame_hotend_pid.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <span>

#include "i18n.h"
#include <guiconfig/wizard_config.hpp>
#include <hotend_pid_session.hpp>
#include <marlin_server_extended_fsm_data.hpp>

using hotend_pid::RelayOscillationRecorder;
using hotend_pid::TuningFailure;

static constexpr size_t col_texts = WizardDefaults::MarginLeft;
static constexpr size_t txt_h = WizardDefaults::txt_h;
static constexpr size_t row_2 = WizardDefaults::row_1 + WizardDefaults::progress_row_h;

static constexpr int relay_time_limit_min = hotend_pid::RELAY_TIME_LIMIT_MS / 60'000;

namespace {

/// Writes the display numbers of the tools of @p set as "1, 3, 4"
void format_tool_list(std::span<char> buffer, hotend_pid::ToolSet set) {
    size_t length = 0;
    buffer[0] = '\0';
    for (std::optional<uint8_t> tool = hotend_pid::first_tool(set); tool.has_value() && length < buffer.size(); tool = hotend_pid::next_tool(set, *tool)) {
        const int written = snprintf(buffer.data() + length, buffer.size() - length, length == 0 ? "%d" : ", %d", *tool + 1);
        if (written < 0) {
            break;
        }
        length += static_cast<size_t>(written);
    }
}

} // namespace

SelftestFrameHotendPid::SelftestFrameHotendPid(window_t *parent, PhasesSelftest ph, fsm::PhaseData data)
    : SelftestFrameNamedWithRadio(parent, ph, data, _("Hotend PID Tuning"), 1)
    , footer(this, 0, footer::Item::all_nozzles)
    , progress(this, WizardDefaults::row_1)
    , text_phase(this, Rect16(col_texts, row_2, WizardDefaults::X_space, txt_h * 5), is_multiline::yes)
    , text_detail(this, Rect16(col_texts, row_2 + 5 * txt_h, WizardDefaults::X_space, txt_h * 2), is_multiline::yes) {
    change();
}

void SelftestFrameHotendPid::change() {
    SelftestHotendPid_t data;
    if (!FSMExtendedDataManager::get(data)) {
        return;
    }
    if (phase_current == phase_previous && shown_data == data) {
        return;
    }
    // Two result pages in a row can share a phase, then only the tool tells them apart
    const bool new_page = phase_current != phase_previous || !shown_data.has_value() || shown_data->tool != data.tool;
    shown_data = data;

    const int tool = data.tool + 1;
    const int session_size = hotend_pid::tool_count(data.tool_mask);
    const int position = hotend_pid::position_of(data.tool_mask, data.tool);
    const bool several_tools = session_size > 1;
    snprintf(target_temp_text, std::size(target_temp_text), "%d\xC2\xB0\x43", data.target_temp); // \xB0\x43 == degree Celsius
    progress.set_progress_percent(0);

    switch (phase_current) {

    case PhasesSelftest::HotendPid_confirm_start: {
        const int fan_percent = (data.print_fan_pwm * 100 + 127) / 255;
        if (several_tools) {
            format_tool_list(tool_list_text, data.tool_mask);
            text_phase.SetText(_("Tools %s are tuned one after another at the front. Each heater switches fully on and off for up to %d min, print fan at %d%%. Target:")
                                   .formatted(text_phase_params, tool_list_text, relay_time_limit_min, fan_percent));
        } else {
            text_phase.SetText(_("Tool %d will be picked and moved to the front. Its heater then switches fully on and off for up to %d min, print fan at %d%%. New values are stored only after you confirm. Target:")
                                   .formatted(text_phase_params, tool, relay_time_limit_min, fan_percent));
        }
        text_detail.SetText(string_view_utf8::MakeRAM(target_temp_text));
        text_detail.Show();
        if (new_page) {
            // Nothing heats or moves unless the user picks Continue on purpose
            radio.SetBtn(Response::Abort);
        }
        break;
    }

    case PhasesSelftest::HotendPid_moving_to_load:
        if (several_tools) {
            text_phase.SetText(_("Picking tool %d (%d of %d) and moving it to the front.").formatted(text_phase_params, tool, position, session_size));
        } else {
            text_phase.SetText(_("Picking tool %d and moving it to the front.").formatted(text_phase_params, tool));
        }
        text_detail.Hide();
        break;

    case PhasesSelftest::HotendPid_heating:
        if (several_tools) {
            text_phase.SetText(_("Heating tool %d (%d of %d) to the target temperature:").formatted(text_phase_params, tool, position, session_size));
        } else {
            text_phase.SetText(_("Heating tool %d to the target temperature:").formatted(text_phase_params, tool));
        }
        text_detail.SetText(string_view_utf8::MakeRAM(target_temp_text));
        text_detail.Show();
        break;

    case PhasesSelftest::HotendPid_measuring: {
        progress.set_progress_percent(100.0f * data.cycle_count / RelayOscillationRecorder::total_cycles);
        const int cycle = std::min<int>(data.cycle_count + 1, RelayOscillationRecorder::total_cycles);
        constexpr int total_cycles = RelayOscillationRecorder::total_cycles;
        if (several_tools) {
            text_phase.SetText(_("Measuring the temperature swing of tool %d (%d of %d), cycle %d of %d, around:")
                                   .formatted(text_phase_params, tool, position, session_size, cycle, total_cycles));
        } else {
            text_phase.SetText(_("Measuring the temperature swing of tool %d, cycle %d of %d, around:")
                                   .formatted(text_phase_params, tool, cycle, total_cycles));
        }
        text_detail.SetText(string_view_utf8::MakeRAM(target_temp_text));
        text_detail.Show();
        break;
    }

    case PhasesSelftest::HotendPid_result_accepted:
        progress.set_progress_percent(100);
        text_phase.SetText(_("Tuning of tool %d finished.\nKp: %.2f -> %.2f\nKi: %.3f -> %.3f\nKd: %.1f -> %.1f")
                               .formatted(text_phase_params, tool,
                                   static_cast<double>(data.old_gains.kp), static_cast<double>(data.new_gains.kp),
                                   static_cast<double>(data.old_gains.ki), static_cast<double>(data.new_gains.ki),
                                   static_cast<double>(data.old_gains.kd), static_cast<double>(data.new_gains.kd)));
        text_detail.SetText(_("Store the new values?"));
        text_detail.Show();
        if (new_page) {
            // Nothing is stored unless the user picks Yes on purpose on this very page
            radio.SetBtn(Response::No);
        }
        break;

    case PhasesSelftest::HotendPid_result_rejected:
        show_rejected(data);
        break;

    case PhasesSelftest::HotendPid_parking:
        if (several_tools) {
            text_phase.SetText(_("Parking tool %d (%d of %d).").formatted(text_phase_params, tool, position, session_size));
        } else {
            text_phase.SetText(_("Parking tool %d.").formatted(text_phase_params, tool));
        }
        text_detail.Hide();
        break;

    case PhasesSelftest::HotendPid_session_stopped:
        text_phase.SetText(_("Session stopped: tool %d could not be picked or parked. All heaters of the session are off. Check the tool and its dock.").formatted(text_phase_params, tool));
        text_detail.Hide();
        break;

    default:
        text_phase.Hide();
        text_detail.Hide();
    }

    // The formatted texts share one parameter buffer and the temperature one buffer of its own,
    // so setting one is not seen as a change of the text
    text_phase.Invalidate();
    text_detail.Invalidate();
}

void SelftestFrameHotendPid::show_rejected(const SelftestHotendPid_t &data) {
    const int tool = data.tool + 1;
    switch (data.failure) {
    case TuningFailure::invalid_request:
        text_phase.SetText(_("Tuning not started, the tool or the target temperature is not allowed."));
        break;
    case TuningFailure::not_at_load_position:
        text_phase.SetText(_("Tool %d: Tuning not started, the tool could not be brought to the load position.").formatted(text_phase_params, tool));
        break;
    case TuningFailure::timeout:
        text_phase.SetText(_("Tool %d: Tuning stopped, the temperature swing was not recorded within %d minutes.").formatted(text_phase_params, tool, relay_time_limit_min));
        break;
    case TuningFailure::amplitude_too_small:
        text_phase.SetText(_("Tool %d: Tuning rejected, the temperature swing was too small to measure.").formatted(text_phase_params, tool));
        break;
    case TuningFailure::cycles_disagree:
        text_phase.SetText(_("Tool %d: Tuning rejected, the temperature swings were not regular enough.").formatted(text_phase_params, tool));
        break;
    case TuningFailure::out_of_limits:
        text_phase.SetText(_("Tool %d: Tuning rejected, the computed values are outside the allowed range:\nKp %.2f, Ki %.3f, Kd %.1f")
                               .formatted(text_phase_params, tool, static_cast<double>(data.new_gains.kp), static_cast<double>(data.new_gains.ki), static_cast<double>(data.new_gains.kd)));
        break;
    case TuningFailure::not_run:
        text_phase.SetText(_("Tool %d was not tuned, the session stopped before its turn.").formatted(text_phase_params, tool));
        break;
    case TuningFailure::none:
        text_phase.SetText(_("Tool %d: Tuning failed.").formatted(text_phase_params, tool));
        break;
    }
    text_detail.SetText(_("Nothing was stored."));
    text_detail.Show();
}
