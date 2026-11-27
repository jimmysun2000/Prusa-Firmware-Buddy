#include "selftest_frame_tool_offsets.hpp"
#include "i18n.h"
#include "img_resources.hpp"
#include <guiconfig/wizard_config.hpp>
#include "selftest_tool_offsets_type.hpp"
#include "marlin_client.hpp"
#include <array>
#include <module/prusa/toolchanger.h>

static constexpr size_t col_texts = WizardDefaults::MarginLeft;
static constexpr size_t txt_h = WizardDefaults::txt_h;
static constexpr size_t row_2 = WizardDefaults::row_1 + WizardDefaults::progress_row_h;

SelftestFrameToolOffsets::SelftestFrameToolOffsets(window_t *parent, PhasesSelftest ph, fsm::PhaseData data)
    : SelftestFrameNamedWithRadio(parent, ph, data, _("Tool Offsets Calibration"), 1)
    , footer(this, 0, footer::Item::all_nozzles, footer::Item::bed)
    , progress(this, WizardDefaults::row_1)
    , text_phase(this, Rect16(col_texts, row_2, WizardDefaults::X_space, txt_h * 5), is_multiline::yes)
    , text_detail(this, Rect16(col_texts, row_2 + 5 * txt_h, WizardDefaults::X_space, txt_h * 2), is_multiline::yes) {
    change();
}

void SelftestFrameToolOffsets::change() {
    const SelftestToolOffsets_t data = SelftestToolOffsets_t::Deserialize(data_current);
    const bool full_mode = data.mode == selftest::ToolOffsetsMode::full;
    const int reference_tool = selftest::TOOL_OFFSETS_REFERENCE_TOOL.display_index();
    const int other_tool = data.other_tool + 1;

    switch (phase_current) {
    case PhasesSelftest::ToolOffsets_wait_user_confirm_start:
        switch (data.mode) {
        case selftest::ToolOffsetsMode::full:
            text_phase.SetText(_("We will need your help with this calibration. You will be asked to screw in a calibration pin."));
            break;
        case selftest::ToolOffsetsMode::single_tool:
            text_phase.SetText(_("Tool %d will be calibrated against tool %d. Tool %d must be unchanged since its last calibration. You will be asked to screw in a calibration pin.").formatted(text_phase_params, other_tool, reference_tool, reference_tool));
            break;
        case selftest::ToolOffsetsMode::rebase:
            text_phase.SetText(_("Tool %d will be calibrated against tool %d and the offsets of the other tools shifted with it. Tool %d must be unchanged since its last calibration. You will be asked to screw in a calibration pin.").formatted(text_phase_params, reference_tool, other_tool, other_tool));
            break;
        }

        // Quick user can
        //  1:15 - Home and install pin
        //  2:15 - Calibrate tool
        //  0:15 - Remove pin and install sheet
        // For 2 tool printer - at least 6 minutes
        // For 5 tool printer - at least 12:45
        // A partial calibration measures two tools
        text_detail.SetText(_("The calibration should take a little over %d minutes.").formatted(text_detail_params, (!full_mode || prusa_toolchanger.get_num_enabled_tools() <= 2) ? 7 : 14));
        text_detail.Show();
        break;

    case PhasesSelftest::ToolOffsets_wait_user_clean_nozzle_cold:
    case PhasesSelftest::ToolOffsets_wait_user_clean_nozzle_hot:
        if (full_mode) {
            text_phase.SetText(_("Nozzle have to be perfectly clean for good calibration results.\n- Clean all nozzles\n- Clean parking plate\n- Press continue when done"));
        } else {
            text_phase.SetText(_("Nozzles have to be perfectly clean for good calibration results.\n- Clean nozzles of tools %d and %d\n- Clean parking plate\n- Press continue when done").formatted(text_phase_params, reference_tool, other_tool));
        }
        text_detail.Hide();
        break;

    case PhasesSelftest::ToolOffsets_wait_user_install_sheet:
        text_phase.SetText(_("Install sheet on heatbed."));
        text_detail.Hide();
        break;

    case PhasesSelftest::ToolOffsets_pin_install_prepare:
        text_phase.SetText(_("Preparing for calibration pin installation."));
        text_detail.Hide();
        break;

    case PhasesSelftest::ToolOffsets_wait_user_install_pin:
        text_phase.SetText(_("- Remove heatbed sheet.\n- Install calibration pin."));
        text_detail.Hide();
        break;

    case PhasesSelftest::ToolOffsets_wait_stable_temp:
        text_phase.SetText(_("Waiting for hotends to stabilize at calibration temperature:"));
        snprintf(target_temp_text, std::size(target_temp_text), "%d\xC2\xB0\x43", SelftestToolOffsets_t::TOOL_CALIBRATION_TEMPERATURE); // \xB0\x43 == degree Celsius
        text_detail.SetText(string_view_utf8::MakeRAM(target_temp_text));
        text_detail.Show();
        break;

    case PhasesSelftest::ToolOffsets_wait_calibrate:
        if (full_mode) {
            text_phase.SetText(_("Calibrating tool offsets."));
        } else {
            text_phase.SetText(_("Calibrating tool offsets of tools %d and %d.").formatted(text_phase_params, reference_tool, other_tool));
        }
        text_detail.Hide();
        break;

    case PhasesSelftest::ToolOffsets_wait_move_away:
        text_phase.SetText(_("Moving away."));
        text_detail.Hide();
        break;

    case PhasesSelftest::ToolOffsets_wait_user_remove_pin:
        text_phase.SetText(_("- Remove calibration pin.\n- Install sheet on heatbed."));
        text_detail.Hide();
        break;

    case PhasesSelftest::ToolOffsets_result_rejected:
        show_rejected(data);
        break;

    case PhasesSelftest::ToolOffsets_result_rejected_retry:
        show_rejected(data);
        show_retry_hint(data);
        break;

    default:
        text_phase.Hide();
        text_detail.Hide();
    }
};

void SelftestFrameToolOffsets::show_rejected(const SelftestToolOffsets_t &data) {
    const pin_tool_offset::Failure &failure = data.failure;
    const int tool = failure.tool.has_value() ? failure.tool->display_index() : 0;

    switch (failure.reason) {
    case pin_tool_offset::FailureReason::not_homed:
        text_phase.SetText(_("Calibration stopped, the printer is not homed."));
        break;
    case pin_tool_offset::FailureReason::invalid_request:
        text_phase.SetText(_("Calibration stopped, it cannot run with the current tool setup."));
        break;
    case pin_tool_offset::FailureReason::toolchange_failed:
        if (failure.tool.has_value()) {
            text_phase.SetText(_("Calibration stopped, tool change of tool %d failed.").formatted(text_phase_params, tool));
        } else {
            text_phase.SetText(_("Calibration stopped, tool change failed."));
        }
        break;
    case pin_tool_offset::FailureReason::center_not_found:
        text_phase.SetText(_("Calibration stopped, tool %d did not find the center of the calibration pin.").formatted(text_phase_params, tool));
        break;
    case pin_tool_offset::FailureReason::out_of_bounds:
        text_phase.SetText(_("Calibration rejected, the %c offset of tool %d would be %.2f mm, which is outside the allowed range.").formatted(text_phase_params, "XYZ"[failure.axis], tool, static_cast<double>(failure.value)));
        break;
    case pin_tool_offset::FailureReason::unknown:
        text_phase.SetText(_("Calibration failed."));
        break;
    }

    if (data.mode == selftest::ToolOffsetsMode::full) {
        text_detail.Hide();
    } else {
        text_detail.SetText(_("The stored offsets were not changed."));
        text_detail.Show();
    }
}

void SelftestFrameToolOffsets::show_retry_hint(const SelftestToolOffsets_t &data) {
    const std::optional<PhysicalToolIndex> &tool = data.failure.tool;
    const int reference_tool = selftest::TOOL_OFFSETS_REFERENCE_TOOL.display_index();
    const int other_tool = data.other_tool + 1;

    // A re-base can reject the shifted offset of a tool that was not measured
    const bool tool_measured = tool.has_value()
        && (data.mode == selftest::ToolOffsetsMode::full || *tool == selftest::TOOL_OFFSETS_REFERENCE_TOOL || tool->to_raw() == data.other_tool);

    if (tool_measured) {
        text_detail.SetText(_("Clean the nozzle of tool %d and check the pin, then retry.").formatted(text_detail_params, tool->display_index()));
    } else {
        text_detail.SetText(_("Clean the nozzles of tools %d and %d and check the pin, then retry.").formatted(text_detail_params, reference_tool, other_tool));
    }
    text_detail.Show();
}
