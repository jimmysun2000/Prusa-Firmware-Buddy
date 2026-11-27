/// @file
#pragma once

#include <MItem_menus.hpp>

/// Info > Maintenance: usage of each maintenance reminder since its last reset, reset on click,
/// and an Intervals submenu to set or turn off the reminders.
class MI_MAINTENANCE final : public MI_SCREEN_BASE {
public:
    MI_MAINTENANCE();
};

/// Opens the screen of MI_MAINTENANCE, with the default intervals applied as the item applies them
void open_maintenance_screen();
