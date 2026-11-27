/// @file
#pragma once

#include <WindowMenuItems.hpp>
#include <gui/menu_item/menu_item_virtual_submenu.hpp>
#include <tool_index.hpp>

#include <option/has_dwarf.h>
static_assert(HAS_DWARF());

/// Spool join state of one tool during a print, refreshed once a second.
/// Only waiting tools can be changed: clicking a tool that is joined offers to remove it,
/// clicking a tool that is not joined offers to add it after a printing tool, behind the same checks as before a print.
/// Changes go through M864 on the Marlin thread; this item only reads the spool join and the tool mapping.
class MI_SPOOL_JOIN_TOOL : public WiInfo<28> {
public:
    MI_SPOOL_JOIN_TOOL(VirtualToolIndex tool);

protected:
    void click(IWindowMenu &) override;
    void Loop() override;

private:
    void update();

    const VirtualToolIndex tool_;
    uint32_t last_update_ms_ = 0;
    StringViewUtf8Parameters<4> label_params_;
};

/// Opens the per-tool spool join rows.
/// Hidden unless the toolchanger is enabled and the print can use tool mapping.
class MI_SPOOL_JOIN : public MenuItemVirtualSubmenu<N_("Spool Join"), MI_SPOOL_JOIN_TOOL, VirtualToolIndex::count, VirtualToolIndex::from_raw> {
public:
    MI_SPOOL_JOIN();
};
