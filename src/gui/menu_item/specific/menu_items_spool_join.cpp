#include "menu_items_spool_join.hpp"

#include <bitset>

#include <config_store/store_instance.hpp>
#include <feature/compatibility_checks/gcode_compatibility.hpp>
#include <gcode_info.hpp>
#include <gui/dialogs/dialog_tool_select.hpp>
#include <Marlin/src/module/prusa/toolchanger.h>
#include <MItem_tools.hpp>
#include <module/prusa/spool_join.hpp>
#include <module/prusa/tool_mapper.hpp>
#include <timing.h>
#include <tools_mapping.hpp>
#include <utils/string_builder.hpp>
#include <window_menu_callback_item.hpp>
#include <window_msgbox.hpp>

namespace {

using ToolSet = std::bitset<VirtualToolIndex::count>;

/// @returns tools that print a G-code filament the current print uses
ToolSet printing_tools() {
    ToolSet result;
    const auto &gcode_info = GCodeInfo::getInstance();
    for (const auto gcode_tool : GcodeToolIndex::all()) {
        if (!gcode_info.get_extruder_info(gcode_tool).used()) {
            continue;
        }
        if (const auto tool = stdext::get_optional<VirtualToolIndex>(tool_mapper.to_virtual(gcode_tool))) {
            result.set(tool->to_raw());
        }
    }
    return result;
}

/// @returns the tool that @p tool waits after, if @p tool is in the spool join chain of a printing tool
std::optional<VirtualToolIndex> joined_after(VirtualToolIndex tool, const ToolSet &printing) {
    for (const auto printing_tool : VirtualToolIndex::all()) {
        if (!printing.test(printing_tool.to_raw())) {
            continue;
        }

        VirtualToolIndex previous = printing_tool;
        for (int i = 0; i < VirtualToolIndex::count; i++) {
            const auto next = spool_join.get_spool_2(previous);
            if (!next.has_value()) {
                break;
            }
            if (*next == tool) {
                return previous;
            }
            previous = *next;
        }
    }
    return std::nullopt;
}

/// Runs the checks done before a print on @p tool, as if it was joined at the end of the chain of @p printing_tool.
/// Only the checks of @p tool are shown.
/// @returns true if all passed or the user accepted the warnings
bool gui_confirm_join(VirtualToolIndex printing_tool, VirtualToolIndex tool) {
    SpoolJoin speculative_spool_join;
    speculative_spool_join = spool_join;
    speculative_spool_join.reroute_joins_containing(tool);
    if (!speculative_spool_join.add_join(printing_tool, tool)) {
        MsgBoxError(_("This tool cannot be added to the spool join."), Responses_Ok);
        return false;
    }

    buddy::gcode_compatibility::CompatibilityReport report;
    report.generate_toolmapping_only({ .spool_join = speculative_spool_join });

    buddy::gcode_compatibility::CompatibilityReport tool_report;
    tool_report.failed_virtual_tool_checks[tool] = report.failed_virtual_tool_checks[tool];
    tool_report.filament_check_reports[tool] = report.filament_check_reports[tool];
    tool_report.does_spool_join = report.does_spool_join;
    return tool_report.gui_confirm_all_incompatibilities(Response::Cancel);
}

/// Asks which printing tool @p tool should back up; skipped when only one tool is printing
std::optional<VirtualToolIndex> select_printing_tool(VirtualToolIndex tool, const ToolSet &printing) {
    if (printing.count() == 1) {
        for (const auto printing_tool : VirtualToolIndex::all()) {
            if (printing.test(printing_tool.to_raw())) {
                StringViewUtf8Parameters<8> params;
                if (MsgBoxQuestion(_("Add tool %d to the spool join of tool %d?").formatted(params, tool.display_index(), printing_tool.display_index()), Responses_YesNo) != Response::Yes) {
                    return std::nullopt;
                }
                return printing_tool;
            }
        }
        return std::nullopt;
    }

    return select_tool_dialog({
        .allow_return = true,
        .tool_filter = [&printing](VirtualToolIndex t) { return printing.test(t.to_raw()); },
        .prefix_section_size = 1,
        .prefix_section_ctor = [](WindowMenuVirtual::ItemVariant &item, int) { item.emplace<WindowMenuCallbackItem>(_("Add to the spool join of:"), WindowMenuCallbackItem::Callback {}).set_enabled(false); },
    });
}

} // namespace

MI_SPOOL_JOIN_TOOL::MI_SPOOL_JOIN_TOOL(VirtualToolIndex tool)
    : WiInfo<28>(string_view_utf8 {})
    , tool_(tool) {
    SetLabel(tool_.display_name(label_params_));
    update();
}

void MI_SPOOL_JOIN_TOOL::Loop() {
    if (ticks_diff(ticks_ms(), last_update_ms_) < 1000) {
        return;
    }
    update();
}

void MI_SPOOL_JOIN_TOOL::update() {
    last_update_ms_ = ticks_ms();

    if (!tool_.is_enabled()) {
        set_enabled(false);
        ChangeInformation(_("Not installed"));
        return;
    }

    ArrayStringBuilder<GetInfoLen()> sb;
    sb.append_printf("%-7s ", config_store().get_filament_type(tool_).parameters().name.data());

    const auto printing = printing_tools();
    StringViewUtf8Parameters<8> params;
    if (printing.test(tool_.to_raw())) {
        sb.append_string_view(_("Printing"));
    } else if (const auto after = joined_after(tool_, printing)) {
        sb.append_string_view(_("Joined after %s").formatted(params, after->compact_display_name().data()));
    } else {
        sb.append_string_view(_("Not joined"));
    }

    ChangeInformation(sb.str());
}

void MI_SPOOL_JOIN_TOOL::click(IWindowMenu &) {
    if (!tool_.is_enabled()) {
        return;
    }

    // The tool a G-code filament prints with is never switched here, that would skip the unload, purge and prime
    const auto printing = printing_tools();
    if (printing.test(tool_.to_raw())) {
        return;
    }

    ArrayStringBuilder<16> detach_gcode;
    detach_gcode.append_printf("M864 D%d", tool_.to_raw());

    if (joined_after(tool_, printing).has_value()) {
        StringViewUtf8Parameters<4> params;
        if (MsgBoxQuestion(_("Remove tool %d from the spool join?").formatted(params, tool_.display_index()), Responses_YesNo, 1) == Response::Yes) {
            gui_try_gcode_with_msg(detach_gcode.str());
        }
        return;
    }

    const auto printing_tool = select_printing_tool(tool_, printing);
    if (!printing_tool.has_value() || !gui_confirm_join(*printing_tool, tool_)) {
        return;
    }

    // Detaching first lets a tool that ran out earlier in this print be joined again
    if (!gui_try_gcode_with_msg(detach_gcode.str())) {
        return;
    }

    ArrayStringBuilder<24> join_gcode;
    join_gcode.append_printf("M864 J A%d B%d", printing_tool->to_raw(), tool_.to_raw());
    gui_try_gcode_with_msg(join_gcode.str());
}

MI_SPOOL_JOIN::MI_SPOOL_JOIN() {
    set_is_hidden(!prusa_toolchanger.is_toolchanger_enabled() || !tools_mapping::is_tool_mapping_possible());
}
