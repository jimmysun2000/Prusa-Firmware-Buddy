#include "selftest_tool_offsets_interface.hpp"
#include "selftest_part.hpp"
#include "selftest_tool_offsets.hpp"
#include <module/prusa/toolchanger.h>
#include <config_store/store_instance.hpp>
#include <logging/log.hpp>

LOG_COMPONENT_REF(Selftest);

namespace selftest {

namespace {
    SelftestToolOffsets_t staticResultToolOffsets;

    /// A rebase trusts the stored offset of the other tool, so that must come from a passed calibration
    bool is_partial_request_allowed(const ToolOffsetsCalibrationData &request) {
        if (!pin_tool_offset::is_partial_request_valid(request)) {
            return false;
        }
        return request.mode != ToolOffsetsMode::rebase
            || config_store().selftest_result.get().get_tool_offset(request.other_tool) == TestResult::passed;
    }

    void store_partial_result(SelftestResult &eeres, const ToolOffsetsCalibrationData &request, TestResult test_result) {
        const PhysicalToolIndex picked_tool = (request.mode == ToolOffsetsMode::rebase) ? TOOL_OFFSETS_REFERENCE_TOOL : request.other_tool;
        eeres.set_tool_offset(picked_tool, test_result);

        if (request.mode == ToolOffsetsMode::rebase && test_result == TestResult::passed) {
            // Offsets of disabled tools were not shifted with the others
            for (const auto tool : PhysicalToolIndex::all()) {
                if (!tool.is_enabled()) {
                    eeres.set_tool_offset(tool, TestResult::unknown);
                }
            }
        }
    }
} // namespace

TestReturn phaseToolOffsets([[maybe_unused]] const ToolMask tool_mask, IPartHandler *&pToolOffsets, const ToolOffsetsConfig_t &config) {
    const bool full_mode = config.request.mode == ToolOffsetsMode::full;

    if (!pToolOffsets) {
        if (full_mode) {
            pToolOffsets = selftest::Factory::CreateDynamical<CSelftestPart_ToolOffsets>(
                config,
                staticResultToolOffsets,
                &CSelftestPart_ToolOffsets::state_ask_user_confirm_start,
                &CSelftestPart_ToolOffsets::state_wait_user,
                &CSelftestPart_ToolOffsets::state_clean_nozzle_start,
                &CSelftestPart_ToolOffsets::state_move_away,
                &CSelftestPart_ToolOffsets::state_wait_moves_done,
                &CSelftestPart_ToolOffsets::state_clean_nozzle,
                &CSelftestPart_ToolOffsets::state_ask_user_install_sheet,
                &CSelftestPart_ToolOffsets::state_wait_user,
                &CSelftestPart_ToolOffsets::state_home_park,
                &CSelftestPart_ToolOffsets::state_wait_moves_done,
                &CSelftestPart_ToolOffsets::state_ask_user_install_pin,
                &CSelftestPart_ToolOffsets::state_wait_user,
                &CSelftestPart_ToolOffsets::state_wait_stable_temp,
                &CSelftestPart_ToolOffsets::state_calibrate,
                &CSelftestPart_ToolOffsets::state_finish_calibration,
                &CSelftestPart_ToolOffsets::state_wait_moves_done,
                &CSelftestPart_ToolOffsets::state_review_result,
                &CSelftestPart_ToolOffsets::state_wait_review,
                &CSelftestPart_ToolOffsets::state_final_park,
                &CSelftestPart_ToolOffsets::state_wait_moves_done,
                &CSelftestPart_ToolOffsets::state_ask_user_remove_pin,
                &CSelftestPart_ToolOffsets::state_wait_user);
        } else {
            if (!is_partial_request_allowed(config.request)) {
                log_error(Selftest, "Tool offsets: partial calibration of tool %u refused", config.request.other_tool.to_raw());
                return TestReturn(false, true);
            }

            pToolOffsets = selftest::Factory::CreateDynamical<CSelftestPart_ToolOffsets>(
                config,
                staticResultToolOffsets,
                &CSelftestPart_ToolOffsets::state_ask_user_confirm_start,
                &CSelftestPart_ToolOffsets::state_wait_user,
                &CSelftestPart_ToolOffsets::state_clean_nozzle_start,
                &CSelftestPart_ToolOffsets::state_move_away,
                &CSelftestPart_ToolOffsets::state_wait_moves_done,
                &CSelftestPart_ToolOffsets::state_clean_nozzle,
                &CSelftestPart_ToolOffsets::state_ask_user_install_sheet,
                &CSelftestPart_ToolOffsets::state_wait_user,
                &CSelftestPart_ToolOffsets::state_home_park,
                &CSelftestPart_ToolOffsets::state_wait_moves_done,
                &CSelftestPart_ToolOffsets::state_ask_user_install_pin,
                &CSelftestPart_ToolOffsets::state_wait_user,
                &CSelftestPart_ToolOffsets::state_wait_stable_temp,
                &CSelftestPart_ToolOffsets::state_calibrate,
                &CSelftestPart_ToolOffsets::state_finish_calibration,
                &CSelftestPart_ToolOffsets::state_wait_moves_done,
                &CSelftestPart_ToolOffsets::state_review_result,
                &CSelftestPart_ToolOffsets::state_wait_review,
                &CSelftestPart_ToolOffsets::state_final_park,
                &CSelftestPart_ToolOffsets::state_wait_moves_done,
                &CSelftestPart_ToolOffsets::state_ask_user_remove_pin,
                &CSelftestPart_ToolOffsets::state_wait_user,
                &CSelftestPart_ToolOffsets::state_report_result);
        }
    }

    const bool in_progress = pToolOffsets->Loop();
    SelftestToolOffsets_t result_tool_offsets(staticResultToolOffsets);
    marlin_server::fsm_change(IPartHandler::GetFsmPhase(), result_tool_offsets.Serialize());
    if (in_progress) {
        return true;
    }

    SelftestResult eeres = config_store().selftest_result.get();
    if (full_mode) {
        for (const auto physical_tool : PhysicalToolIndex::all().skip_all_disabled()) {

            if (pToolOffsets->GetResult() == TestResult::skipped) {
                continue; // Test was aborted, do not regress
            }

            // Store tool calibration state
            eeres.set_tool_offset(physical_tool, pToolOffsets->GetResult());
        }
    } else if (pToolOffsets->GetResult() != TestResult::skipped) { // Aborted, do not regress
        store_partial_result(eeres, config.request, pToolOffsets->GetResult());
    }
    config_store().selftest_result.set(eeres);

    const bool skipped = pToolOffsets->GetResult() != TestResult::passed; ///< Return value whether to run next test

    delete pToolOffsets;
    pToolOffsets = nullptr;
    return TestReturn(false, skipped);
}

} // namespace selftest
