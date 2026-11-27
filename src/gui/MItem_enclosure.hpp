#pragma once
#include "WindowMenuItems.hpp"
#include "WindowItemFormatableLabel.hpp"
#include <WindowMenuSpin.hpp>

class MI_ENCLOSURE : public IWindowMenuItem {
    constexpr static const char *label = N_("Enclosure Settings");

public:
    MI_ENCLOSURE();

protected:
    virtual void click(IWindowMenu &windowMenu) override;
};

class MI_ENCLOSURE_ENABLE : public WI_ICON_SWITCH_OFF_ON_t {
    static constexpr const char *const label = N_("Enclosure");
    static constexpr const char *const wait_str = N_("Testing enclosure fan");

public:
    MI_ENCLOSURE_ENABLE();

protected:
    void OnChange(size_t old_index) override;
};

/// Heat the enclosure through the bed for filaments that need a warm chamber
class MI_ENCLOSURE_BED_HEATING : public WI_ICON_SWITCH_OFF_ON_t {
public:
    MI_ENCLOSURE_BED_HEATING();

protected:
    void OnChange(size_t old_index) override;
};

class MI_ENCLOSURE_SOAK_MAX : public WiSpin {
public:
    MI_ENCLOSURE_SOAK_MAX();

protected:
    void OnClick() override;
    void Loop() override;
};

class MI_ENCLOSURE_COOLDOWN_RATE : public WiSpin {
public:
    MI_ENCLOSURE_COOLDOWN_RATE();

protected:
    void OnClick() override;
    void Loop() override;
};

/// Run the enclosure fan at full power while the chamber is warmer than a printed filament allows
class MI_ENCLOSURE_OVERTEMP_VENTING : public WI_ICON_SWITCH_OFF_ON_t {
public:
    MI_ENCLOSURE_OVERTEMP_VENTING();

protected:
    void OnChange(size_t old_index) override;
};
