#include "selftest_hotend_pid_interface.hpp"

#include "marlin_server.hpp"
#include "selftest_part.hpp"

namespace selftest {

namespace {
    SelftestHotendPid_t static_result;
} // namespace

TestReturn phaseHotendPid(IPartHandler *&part, const HotendPidConfig_t &config) {
    if (!part) {
        part = Factory::CreateDynamical<CSelftestPart_HotendPid>(
            config,
            static_result,
            &CSelftestPart_HotendPid::state_check_request,
            &CSelftestPart_HotendPid::state_wait_confirm,
            &CSelftestPart_HotendPid::state_start_relay,
            &CSelftestPart_HotendPid::state_record,
            &CSelftestPart_HotendPid::state_evaluate,
            &CSelftestPart_HotendPid::state_wait_result);
    }

    const bool in_progress = part->Loop();
    marlin_server::fsm_change_extended(IPartHandler::GetFsmPhase(), static_result);
    if (in_progress) {
        return true;
    }

    const bool aborted = part->GetResult() == TestResult::skipped;
    delete part;
    part = nullptr;
    return TestReturn(false, aborted);
}

} // namespace selftest
