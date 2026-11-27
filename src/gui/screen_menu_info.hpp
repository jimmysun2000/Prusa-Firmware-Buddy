/// @file
#pragma once

#include <basic_screen_menu.hpp>
#include "MItem_menus.hpp"
#include "MItem_tools.hpp"
#include <screen_menu_statistics.hpp>
#include <gui/menu_item/menu_item_virtual_submenu.hpp>
#include <option/has_toolchanger.h>
#include <option/has_per_tool_temperatures.h>
#include <option/has_indx.h>
#if HAS_TOOLCHANGER() && !HAS_INDX()
    #include <screen_menu_maintenance.hpp>
#endif

#if HAS_TOOLCHANGER() && HAS_PER_TOOL_TEMPERATURES()
using MI_TOOLS_OVERVIEW = MenuItemVirtualSubmenu<N_("Tools Overview"), MI_TOOL_OVERVIEW, PhysicalToolIndex::count, PhysicalToolIndex::from_raw>;
#endif

using ScreenMenuInfo__ = BasicScreenMenu<
    MI_STATISTICS,
#if HAS_TOOLCHANGER() && !HAS_INDX()
    MI_MAINTENANCE,
#endif
    MI_NETWORK_STATUS,
    MI_SENSOR_INFO,
#if HAS_TOOLCHANGER() && HAS_PER_TOOL_TEMPERATURES()
    MI_TOOLS_OVERVIEW,
#endif
#if PRINTER_IS_PRUSA_XL()
    MI_BED_MESH,
#endif
#if HAS_MODULAR_BED()
    MI_BED_TILES,
#endif
    MI_VERSION_INFO,
    MI_HELP_FW_UPDATE>;

class ScreenMenuInfo final : public ScreenMenuInfo__ {
public:
    ScreenMenuInfo();
};
