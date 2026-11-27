#include "home_shortcut.hpp"

#include "MItem_filament.hpp"
#include "MItem_menus.hpp"
#include "MItem_tools.hpp"
#include "ScreenHandler.hpp"
#include "screen_menu_enclosure.hpp"
#include "screen_menu_info.hpp"
#include "screen_menu_macros.hpp"
#include "screen_menu_maintenance.hpp"
#include "screen_menu_move.hpp"
#include "screen_menu_statistics.hpp"
#include "screen_move_z.hpp"
#include "user_macro.hpp"

#include <config_store/store_instance.hpp>
#include <gui/screen/screen_tool_pick_park.hpp>
#include <i18n.h>
#include <marlin_client.hpp>
#include <window_msgbox.hpp>

#include <algorithm>
#include <string_view>

namespace home_shortcut {

static_assert(slot_count == decltype(config_store().home_shortcuts)::item_count);

namespace {

    constexpr const char *printer_busy_text = N_("Printer is busy. Please try repeating the action later.");

    constexpr const char *fixed_label(Kind kind) {
        switch (kind) {
        case Kind::none:
            return N_("None");
        case Kind::auto_home:
            return N_("Auto Home");
        case Kind::pick_park:
            return N_("Pick/Park");
        case Kind::move_axis:
            return N_("Move Axis");
        case Kind::motors_off:
            return N_("Motors Off");
        case Kind::wipe_nozzle:
            return N_("Wipe Now");
        case Kind::macro:
            break;
        case Kind::temperature:
            return N_("Temp/Fans");
        case Kind::move_z:
            return N_("Move Z");
        case Kind::bed_mesh:
            return N_("Bed Mesh");
        case Kind::bed_tiles:
            return N_("Bed Tiles");
        case Kind::tools_overview:
            return N_("Tools Info");
        case Kind::statistics:
            return N_("Statistics");
        case Kind::maintenance:
            return N_("Maintain");
        case Kind::sensor_info:
            return N_("Sensors");
        case Kind::network_status:
            return N_("Net Status");
        case Kind::prusalink:
            return N_("PrusaLink");
        case Kind::lights:
            return N_("Lights");
        case Kind::enclosure:
            return N_("Enclosure");
        case Kind::wiper_setup:
            return N_("Wiper");
        case Kind::calibrations:
            return N_("Calibrate");
        case Kind::filament_sensors:
            return N_("Fil Sensor");
        case Kind::macros_screen:
            return N_("Macros");
        case Kind::load_all:
            return N_("Load All");
        case Kind::unload_all:
            return N_("Unload All");
        case Kind::change_all:
            return N_("Change All");
        case Kind::tool_settings:
            return N_("Tool Setup");
        case Kind::input_shaper:
            return N_("Inp Shaper");
        }
        return nullptr;
    }

    static_assert(std::ranges::all_of(actions, [](const Action &action) {
        return action.kind == Kind::macro || std::string_view { fixed_label(action.kind) }.size() <= home_label_length;
    }));

    /// A print can start remotely while a question is shown
    bool still_idle() {
        if (marlin_client::is_idle()) {
            return true;
        }
        MsgBoxWarning(_(printer_busy_text), Responses_Ok);
        return false;
    }

    bool confirmed(const string_view_utf8 &question) {
        return MsgBoxQuestion(question, Responses_YesNo, 1) == Response::Yes && still_idle();
    }

} // namespace

string_view_utf8 label(const Action &action, StringViewUtf8ParamBase &params, size_t max_length) {
    if (action.kind == Kind::macro) {
        return user_macro::label(action.macro, params, max_length);
    }
    return _(fixed_label(action.kind));
}

void run(uint8_t id) {
    const Action &action = find(id);
    switch (action.kind) {

    case Kind::none:
        return;

    case Kind::auto_home:
        if (confirmed(_("Home all axes?"))) {
            MI_AUTO_HOME::run();
        }
        return;

    case Kind::pick_park:
        Screens::Access()->Open(screen_tool_pick_park_creator());
        return;

    case Kind::move_axis:
        Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuMove>);
        return;

    case Kind::motors_off:
        if (confirmed(_("Turn the motors off?"))) {
            MI_DISABLE_MOTORS::run();
        }
        return;

    case Kind::wipe_nozzle:
        if (MsgBoxWarning(_("The bed will rise to the wiper. Remove any print from the bed first. Continue?"), Responses_YesNo, 1) == Response::Yes && still_idle()) {
            gui_try_gcode_with_msg("G12");
        }
        return;

    case Kind::macro:
        user_macro::run(action.macro, true);
        return;

    case Kind::temperature:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuTemperatureAndFans>::get());
        return;

    case Kind::move_z:
        open_move_z_screen();
        return;

    case Kind::bed_mesh:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenBedMesh>::get());
        return;

    case Kind::bed_tiles:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenBedTiles>::get());
        return;

    case Kind::tools_overview:
        Screens::Access()->Open(MI_TOOLS_OVERVIEW::creator());
        return;

    case Kind::statistics:
        Screens::Access()->Open(screen_statistics_creator());
        return;

    case Kind::maintenance:
        open_maintenance_screen();
        return;

    case Kind::sensor_info:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuSensorInfo>::get());
        return;

    case Kind::network_status:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuNetworkStatus>::get());
        return;

    case Kind::prusalink:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuPrusaLink>::get());
        return;

    case Kind::lights:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuLights>::get());
        return;

    case Kind::enclosure:
        Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuEnclosure>);
        return;

    case Kind::wiper_setup:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuNozzleWiper>::get());
        return;

    case Kind::calibrations:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuSTSCalibrations>::get());
        return;

    case Kind::filament_sensors:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuFilamentSensors>::get());
        return;

    case Kind::macros_screen:
        Screens::Access()->Open(screen_macros_creator());
        return;

    case Kind::load_all:
        MI_LOAD_ALL::run();
        return;

    case Kind::unload_all:
        MI_UNLOAD_ALL::run();
        return;

    case Kind::change_all:
        MI_CHANGEALL::run();
        return;

    case Kind::tool_settings:
        MI_TOOLHEAD_SETTINGS::run();
        return;

    case Kind::input_shaper:
        Screens::Access()->Open(MI_SCREEN_CTOR<ScreenMenuInputShaper>::get());
        return;
    }
}

} // namespace home_shortcut
