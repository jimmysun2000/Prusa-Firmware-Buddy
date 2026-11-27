#include "home_shortcut.hpp"

#include "MItem_tools.hpp"
#include "ScreenHandler.hpp"
#include "screen_menu_move.hpp"
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
            return N_("Wipe Nozzle");
        case Kind::macro:
            break;
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
    }
}

} // namespace home_shortcut
