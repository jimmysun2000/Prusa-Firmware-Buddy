#include "screen_m600.hpp"
#include <general_response.hpp>
#include <window_msgbox.hpp>
#include <ScreenHandler.hpp>
#include <WindowMenuItems.hpp>
#include <screen_menu.hpp>
#include <img_resources.hpp>
#include <timing.h>
#include <config_store/store_instance.hpp>
#include <common/marlin_vars.hpp>
#include <utils/string_builder.hpp>
#include <gui/menu_item/menu_item_utils.hpp>

#include <option/has_toolchanger.h>
#include <bsod/bsod.h>
#if HAS_TOOLCHANGER()
    #include <Marlin/src/module/prusa/toolchanger.h>
    #include <gui/dialogs/dialog_tool_select.hpp>
#endif

#include <printers.h>
#if PRINTER_IS_PRUSA_XL()
    #include <feature/compatibility_checks/filament_compatibility.hpp>
    #include <filament_list.hpp>
    #include <gcode_info.hpp>
    #include <gui/dialogs/dialog_menu_virtual.hpp>
    #include <MItem_tools.hpp>
    #include <module/prusa/spool_join.hpp>
    #include <module/prusa/tool_mapper.hpp>
#endif

namespace {

#if PRINTER_IS_PRUSA_XL()
/// @returns whether @p tool is picked, prints a G-code filament of the running print, or waits to print one in a spool join
bool is_used_by_print(VirtualToolIndex tool) {
    if (stdext::holds_value(VirtualToolIndex::currently_selected(), tool)) {
        return true;
    }

    const auto &gcode_info = GCodeInfo::getInstance();
    for (const auto gcode_tool : GcodeToolIndex::all()) {
        if (!gcode_info.get_extruder_info(gcode_tool).used()) {
            continue;
        }

        auto chain_tool = stdext::get_optional<VirtualToolIndex>(tool_mapper.to_virtual(gcode_tool));
        for (int i = 0; chain_tool.has_value() && i < VirtualToolIndex::count; i++) {
            if (*chain_tool == tool) {
                return true;
            }
            chain_tool = spool_join.get_spool_2(*chain_tool);
        }
    }
    return false;
}

/// @returns the filament the user picked for @p tool, std::nullopt if cancelled
std::optional<FilamentType> select_filament_dialog(VirtualToolIndex tool) {
    FilamentList filaments;
    generate_filament_list(filaments, { .visible_only = true, .visible_first = true, .compatible_with_tool = tool });

    const dialog_menu_virtual::Config config {
        .title = _("FILAMENT TYPE"),
        .item_count = static_cast<int>(filaments.size()) + 1, // + MI_RETURN
        .setup_f = [&filaments](int index, dialog_menu_virtual::ItemVariant &variant) {
            if (index == 0) {
                variant.emplace<MI_RETURN>();
                return;
            }
            auto &item = variant.emplace<dialog_menu_virtual::MenuItem>();
            item.SetLabel(string_view_utf8::MakeCPUFLASH("%s").formatted(item.params, filaments[index - 1].parameters().name.data()));
        },
    };

    const auto result = dialog_menu_virtual::execute(config);
    if (!result.has_value() || *result == 0) {
        return std::nullopt;
    }
    return filaments[*result - 1]; // - MI_RETURN
}

/// Asks for the filament to load into @p tool and sends the change with it
bool inject_with_filament_type(VirtualToolIndex tool) {
    const auto filament = select_filament_dialog(tool);
    if (!filament.has_value()) {
        return false;
    }

    // The same checks as when picking a filament to load
    const auto filament_params = filament->parameters();
    buddy::filament_compatibility::CompatibilityReport compat;
    compat.generate_noclear({
        .filament = filament_params,
        .tools = tool,
        .assume_filament_already_inserted = false,
    });
    if (!compat.gui_confirm_all_incompatibilities(Response::Cancel)) {
        return false;
    }

    StringViewUtf8Parameters<16> params;
    const auto msg_answer = MsgBoxQuestion(
        _("Change filament now?\n"
          "New filament type: %s")
            .formatted(params, filament_params.name.data()),
        Responses_YesNo);
    if (msg_answer != Response::Yes) {
        return false;
    }

    // Sent through the G-code queue, an injected G-code cannot carry the filament
    ArrayStringBuilder<32> gcode;
    gcode.append_printf("M600 P T%d S\"", tool.to_raw());
    filament->build_encoded_gcode_param(gcode);
    gcode.append_char('"');
    return gui_try_gcode_with_msg(gcode.str());
}
#endif

bool inject(VirtualToolIndex tool) {
#if PRINTER_IS_PRUSA_XL()
    // A tool with no known filament, or one the print does not use, can take a filament of a different type
    if (config_store().get_filament_type(tool) == FilamentType::none || !is_used_by_print(tool)) {
        return inject_with_filament_type(tool);
    }
#endif

    StringViewUtf8Parameters<8> params;
    const auto msg_answer = MsgBoxQuestion(
        _("Change filament now?\n"
          "Use same filament type as currently loaded.\n"
          "Current filament type: %s")
            .formatted(params, config_store().get_filament_type(tool).parameters().name.data()),
        Responses_YesNo);

    if (msg_answer != Response::Yes) {
        return false;
    }
    marlin_client::inject(GCodeLiteral("M600 P T%.0f", static_cast<float>(tool.to_raw())));
    return true;
}

} // namespace

MI_M600::MI_M600()
    : IWindowMenuItem(_(label), nullptr, is_enabled_t::yes, is_hidden_t::no,
#if HAS_TOOLCHANGER()
        (prusa_toolchanger.is_toolchanger_enabled()) ? expands_t::yes :
#endif
                                                     expands_t::no) {
}

void MI_M600::click([[maybe_unused]] IWindowMenu &window_menu) {
#if HAS_TOOLCHANGER()
    if (prusa_toolchanger.is_toolchanger_enabled()) {
        const auto tool = select_tool_dialog({
            .allow_return = true,
        });
        if (tool.has_value() && inject(*tool)) {
            Screens::Access()->close_screen();
        }
        return;
    }
#endif
    match(
        VirtualToolIndex::currently_selected(),
        [](VirtualToolIndex virtual_tool) { inject(virtual_tool); },
        [](NoTool) { debug_assert(false); /* theoretically reachable edge case - do nothing */ });
}

void MI_M600::Loop() {
    loop_gcode_inject_menu_item(*this,
        {
            .update_enabled = true,
            .update_icon = true,
            .enabled = marlin_vars().max_printed_z > 0,
        });
}
