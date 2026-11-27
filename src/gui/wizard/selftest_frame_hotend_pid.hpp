#pragma once

#include <optional>

#include "selftest_frame.hpp"
#include "status_footer.hpp"
#include "window_wizard_progress.hpp"
#include <selftest/selftest_hotend_pid_type.hpp>

/// Shows the hotend PID tuning, see CSelftestPart_HotendPid
class SelftestFrameHotendPid : public SelftestFrameNamedWithRadio {
    FooterLine footer;
    window_wizard_progress_t progress;
    window_text_t text_phase;
    window_text_t text_detail; ///< Target temperature | question | what happened to the stored gains
    char target_temp_text[10];
    StringViewUtf8Parameters<96> text_phase_params;

    /// Last shown data, the server sends the same data every selftest loop
    std::optional<SelftestHotendPid_t> shown_data;

    void show_rejected(const SelftestHotendPid_t &data);

protected:
    void change() override;

public:
    SelftestFrameHotendPid(window_t *parent, PhasesSelftest ph, fsm::PhaseData data);
};
