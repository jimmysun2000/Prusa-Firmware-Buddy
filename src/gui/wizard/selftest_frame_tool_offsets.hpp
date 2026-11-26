#pragma once

#include "selftest_frame.hpp"
#include "window_icon.hpp"
#include "window_wizard_progress.hpp"
#include "radio_button.hpp"
#include "status_footer.hpp"

struct SelftestToolOffsets_t;

class SelftestFrameToolOffsets : public SelftestFrameNamedWithRadio {
    FooterLine footer;
    window_wizard_progress_t progress;
    window_text_t text_phase;
    window_text_t text_detail; ///< Estimate (how long it will take) | calibration target temperature
    char target_temp_text[10];
    StringViewUtf8Parameters<32> text_phase_params;
    StringViewUtf8Parameters<3> text_detail_params;

    void show_rejected(const SelftestToolOffsets_t &data);

protected:
    virtual void change() override;

public:
    SelftestFrameToolOffsets(window_t *parent, PhasesSelftest ph, fsm::PhaseData data);
};
