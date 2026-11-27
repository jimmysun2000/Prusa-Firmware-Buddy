#include "selftest_frame_hotend_pid.hpp"

#include <algorithm>
#include <cstdio>

#include "i18n.h"
#include <guiconfig/wizard_config.hpp>
#include <marlin_server_extended_fsm_data.hpp>

using hotend_pid::RelayOscillationRecorder;
using hotend_pid::TuningFailure;

static constexpr size_t col_texts = WizardDefaults::MarginLeft;
static constexpr size_t txt_h = WizardDefaults::txt_h;
static constexpr size_t row_2 = WizardDefaults::row_1 + WizardDefaults::progress_row_h;

static constexpr int relay_time_limit_min = hotend_pid::RELAY_TIME_LIMIT_MS / 60'000;

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
    shown_data = data;

    const int tool = data.tool + 1;
    snprintf(target_temp_text, std::size(target_temp_text), "%d\xC2\xB0\x43", data.target_temp); // \xB0\x43 == degree Celsius
    progress.set_progress_percent(0);

    switch (phase_current) {

    case PhasesSelftest::HotendPid_confirm_start:
        text_phase.SetText(_("Tool %d will be picked and moved to the front. Its heater then switches fully on and off for up to %d min, print fan at %d%%. New values are stored only after you confirm. Target:")
                               .formatted(text_phase_params, tool, relay_time_limit_min, (data.print_fan_pwm * 100 + 127) / 255));
        text_detail.SetText(string_view_utf8::MakeRAM(target_temp_text));
        text_detail.Show();
        break;

    case PhasesSelftest::HotendPid_moving_to_load:
        text_phase.SetText(_("Picking tool %d and moving it to the front.").formatted(text_phase_params, tool));
        text_detail.Hide();
        break;

    case PhasesSelftest::HotendPid_heating:
        text_phase.SetText(_("Heating tool %d to the target temperature:").formatted(text_phase_params, tool));
        text_detail.SetText(string_view_utf8::MakeRAM(target_temp_text));
        text_detail.Show();
        break;

    case PhasesSelftest::HotendPid_measuring:
        progress.set_progress_percent(100.0f * data.cycle_count / RelayOscillationRecorder::total_cycles);
        text_phase.SetText(_("Measuring the temperature swing of tool %d, cycle %d of %d, around:")
                               .formatted(text_phase_params, tool, std::min<int>(data.cycle_count + 1, RelayOscillationRecorder::total_cycles), static_cast<int>(RelayOscillationRecorder::total_cycles)));
        text_detail.SetText(string_view_utf8::MakeRAM(target_temp_text));
        text_detail.Show();
        break;

    case PhasesSelftest::HotendPid_result_accepted:
        progress.set_progress_percent(100);
        text_phase.SetText(_("Tuning of tool %d finished.\nKp: %.2f -> %.2f\nKi: %.3f -> %.3f\nKd: %.1f -> %.1f")
                               .formatted(text_phase_params, tool,
                                   static_cast<double>(data.old_gains.kp), static_cast<double>(data.new_gains.kp),
                                   static_cast<double>(data.old_gains.ki), static_cast<double>(data.new_gains.ki),
                                   static_cast<double>(data.old_gains.kd), static_cast<double>(data.new_gains.kd)));
        text_detail.SetText(_("Store the new values?"));
        text_detail.Show();
        break;

    case PhasesSelftest::HotendPid_result_rejected:
        show_rejected(data);
        break;

    case PhasesSelftest::HotendPid_parking:
        text_phase.SetText(_("Parking tool %d.").formatted(text_phase_params, tool));
        text_detail.Hide();
        break;

    default:
        text_phase.Hide();
        text_detail.Hide();
    }

    // The formatted texts share one parameter buffer, so setting one is not seen as a change of the text
    text_phase.Invalidate();
}

void SelftestFrameHotendPid::show_rejected(const SelftestHotendPid_t &data) {
    switch (data.failure) {
    case TuningFailure::invalid_request:
        text_phase.SetText(_("Tuning not started, the tool or the target temperature is not allowed."));
        break;
    case TuningFailure::not_at_load_position:
        text_phase.SetText(_("Tuning not started, the tool could not be brought to the load position."));
        break;
    case TuningFailure::timeout:
        text_phase.SetText(_("Tuning stopped, the temperature swing was not recorded within %d minutes.").formatted(text_phase_params, relay_time_limit_min));
        break;
    case TuningFailure::amplitude_too_small:
        text_phase.SetText(_("Tuning rejected, the temperature swing was too small to measure."));
        break;
    case TuningFailure::cycles_disagree:
        text_phase.SetText(_("Tuning rejected, the temperature swings were not regular enough."));
        break;
    case TuningFailure::out_of_limits:
        text_phase.SetText(_("Tuning rejected, the computed values are outside the allowed range:\nKp %.2f, Ki %.3f, Kd %.1f")
                               .formatted(text_phase_params, static_cast<double>(data.new_gains.kp), static_cast<double>(data.new_gains.ki), static_cast<double>(data.new_gains.kd)));
        break;
    case TuningFailure::none:
        text_phase.SetText(_("Tuning failed."));
        break;
    }
    text_detail.SetText(_("Nothing was stored."));
    text_detail.Show();
}
