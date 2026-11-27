#pragma once

#include <MItem_menus.hpp>

/// Control > Macros: runs a user macro file, a row is active when its file exists.
/// The last row opens the settings (name, home screen icon, file) of a macro.
class MI_MACROS final : public MI_SCREEN_BASE {
public:
    MI_MACROS();
};
