#pragma once

#include "screen_toolhead_settings_common.hpp"
#include <option/has_selftest.h>
#include <option/has_tool_offset_pin_calibration.h>

namespace screen_toolhead_settings {

class MI_NOZZLE_OFFSET_COMPONENT : public MI_TOOLHEAD_SPECIFIC_SPIN {
public:
    MI_NOZZLE_OFFSET_COMPONENT(uint8_t component, Toolhead toolhead = default_toolhead);
    float read_value_impl(PhysicalToolIndex ix) final;
    void store_value_impl(PhysicalToolIndex ix, float set) final;

private:
    const uint8_t component_;
    StringViewUtf8Parameters<2> label_params_;
};

#if HAS_SELFTEST() && HAS_TOOL_OFFSET_PIN_CALIBRATION()
/// Runs the pin-based offset calibration for this tool only
class MI_NOZZLE_OFFSET_CALIBRATE : public MI_TOOLHEAD_SPECIFIC_BASE<IWindowMenuItem> {
public:
    MI_NOZZLE_OFFSET_CALIBRATE(Toolhead toolhead = default_toolhead);
    void click(IWindowMenu &);
    void update() final;
};
#endif

using ScreenToolheadDetailNozzleOffset_ = ScreenMenu<EFooter::Off,
    MI_RETURN,
    WithConstructorArgs<MI_NOZZLE_OFFSET_COMPONENT, 0>,
    WithConstructorArgs<MI_NOZZLE_OFFSET_COMPONENT, 1>,
    WithConstructorArgs<MI_NOZZLE_OFFSET_COMPONENT, 2> //
#if HAS_SELFTEST() && HAS_TOOL_OFFSET_PIN_CALIBRATION()
    ,
    MI_NOZZLE_OFFSET_CALIBRATE
#endif
    >;

class ScreenToolheadDetailNozzleOffset : public ScreenToolheadDetailNozzleOffset_ {
public:
    ScreenToolheadDetailNozzleOffset(Toolhead toolhead = default_toolhead);

private:
    const Toolhead toolhead;
};

} // namespace screen_toolhead_settings
