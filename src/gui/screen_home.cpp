// screen_home.cpp
#include "screen_home.hpp"
#include "stdio.h"
#include "file_sort.hpp"

#include "config.h"
#include <option/has_power_panic.h>
#include <option/signature_oak.h>

#include "marlin_client.hpp"
#include <common/marlin_vars.hpp>
#include "screen_filebrowser.hpp"
#include "print_utils.hpp"
#include "filename_type.hpp"
#include "settings_ini.hpp"
#include <utils/string_builder.hpp>
#include <buddy/filename_defs.hpp>
#include <sys/unistd.h>
#include <wui_api.h>
#include <version/version.hpp>

#if HAS_POWER_PANIC()
    #include "power_panic.hpp"
#endif

#include "ScreenHandler.hpp"
#include "ScreenFactory.hpp"
#include "gui_media_events.hpp"
#include "DialogHandler.hpp"
#include "img_resources.hpp"
#include "tasks.hpp"

#include "screen_printing.hpp"
#include <feature/filament_sensor/filament_sensors_handler.hpp>

#include <raii/auto_restore.hpp>
#include "lazyfilelist.hpp"
#include "i18n.h"
#include "i2c.hpp"
#include "netdev.h"
#include "ini.h"

#include <option/has_loadcell.h>
#include <option/developer_mode.h>
#include <device/peripherals.hpp>
#include <option/has_mmu2.h>
#include <option/has_human_interactions.h>
#include <option/has_selftest.h>
#include <option/has_embedded_esp32.h>

#include "screen_menu_settings.hpp"
#include "screen_menu_filament.hpp"
#include "screen_menu_control.hpp"
#include <screen_menu_info.hpp>

#if HAS_MMU2()
    #include "screen_menu_filament_mmu.hpp"
#endif

#include <find_error.hpp>
#include <transfers/transfer_file_check.hpp>
#include <guiconfig/guiconfig.h>

#include "usb_host.h"

#if HAS_TOOLCHANGER() && !HAS_INDX()
    #include "maintenance_reminders.hpp"
#endif

bool screen_home_data_t::ever_been_opened = false;

#if HAS_MINI_DISPLAY()
    #define GEN_ICON_NAMES(ICON) \
        { img::ICON##_64x64, img::ICON##_64x64_focused, img::ICON##_64x64_disabled }
#endif
#if HAS_LARGE_DISPLAY()
    #define GEN_ICON_NAMES(ICON) \
        { img::ICON##_80x80, img::ICON##_80x80_focused, img::ICON##_80x80_disabled }
#endif

static constexpr const WindowMultiIconButton::Pngs icons[] = {
    GEN_ICON_NAMES(print),
    GEN_ICON_NAMES(preheat),
    GEN_ICON_NAMES(spool),
    GEN_ICON_NAMES(calibrate),
    GEN_ICON_NAMES(settings),
    GEN_ICON_NAMES(info),
    GEN_ICON_NAMES(spools)
};

constexpr size_t labelPrintId = 0;
constexpr size_t labelNoUSBId = 6;
constexpr size_t iconNonMMUId = 2;
constexpr size_t iconMMUId = 6;
constexpr size_t buttonFilamentIndex = 2;

#if HAS_MINI_DISPLAY()
constexpr size_t buttonsXSpacing = 15;
constexpr size_t buttonTextWidth = 80;
constexpr size_t buttonTextHeight = 13; // font_regular_7x13

constexpr size_t buttonTopOffset = 88;
constexpr size_t buttonTextTopOffset = 155;

constexpr Rect16 logoRect = Rect16(41, 31, 158, 40);
#endif

#if HAS_LARGE_DISPLAY()
constexpr size_t buttonsXSpacing = 40;
constexpr size_t buttonTextWidth = 99;
constexpr size_t buttonTextHeight = 23;

constexpr size_t buttonTopOffset = 53;
constexpr size_t buttonTextTopOffset = buttonTopOffset + GuiDefaults::ButtonIconSize + 5;
#endif

constexpr size_t buttonTextSpacing = GuiDefaults::ButtonIconSize + buttonsXSpacing - buttonTextWidth;
constexpr size_t buttonsLeftOffset = (GuiDefaults::ScreenWidth - 3 * GuiDefaults::ButtonIconSize - 2 * buttonsXSpacing) / 2;
constexpr size_t buttonsTextsLeftOffset = (GuiDefaults::ScreenWidth - 3 * buttonTextWidth - 2 * buttonTextSpacing) / 2;

static constexpr Rect16 buttonRect(size_t col, size_t row) {
    return Rect16(
        buttonsLeftOffset + (buttonsXSpacing + GuiDefaults::ButtonIconSize) * col,
        buttonTopOffset + (GuiDefaults::ButtonIconVerticalSpacing + GuiDefaults::ButtonIconSize) * row,
        GuiDefaults::ButtonIconSize,
        GuiDefaults::ButtonIconSize);
}

static constexpr Rect16 buttonTextRect(size_t col, size_t row) {
    return Rect16(
        buttonsTextsLeftOffset + (buttonsXSpacing + GuiDefaults::ButtonIconSize) * col,
        buttonTextTopOffset + (GuiDefaults::ButtonIconVerticalSpacing + GuiDefaults::ButtonIconSize) * row,
        buttonTextWidth,
        buttonTextHeight);
}

#if PRINTER_IS_PRUSA_XL()
namespace {

// With shortcuts, a fourth column fits by narrowing the gaps between the columns
constexpr size_t shortcut_layout_columns = 4;
constexpr size_t shortcut_layout_x_spacing = 30;
constexpr size_t shortcut_layout_text_spacing = GuiDefaults::ButtonIconSize + shortcut_layout_x_spacing - buttonTextWidth;
constexpr size_t shortcut_layout_left_offset = (GuiDefaults::ScreenWidth - shortcut_layout_columns * GuiDefaults::ButtonIconSize - (shortcut_layout_columns - 1) * shortcut_layout_x_spacing) / 2;
constexpr size_t shortcut_layout_texts_left_offset = (GuiDefaults::ScreenWidth - shortcut_layout_columns * buttonTextWidth - (shortcut_layout_columns - 1) * shortcut_layout_text_spacing) / 2;

constexpr Rect16 shortcut_layout_button_rect(size_t col, size_t row) {
    return Rect16(
        shortcut_layout_left_offset + (shortcut_layout_x_spacing + GuiDefaults::ButtonIconSize) * col,
        buttonTopOffset + (GuiDefaults::ButtonIconVerticalSpacing + GuiDefaults::ButtonIconSize) * row,
        GuiDefaults::ButtonIconSize,
        GuiDefaults::ButtonIconSize);
}

constexpr Rect16 shortcut_layout_text_rect(size_t col, size_t row) {
    return Rect16(
        shortcut_layout_texts_left_offset + (shortcut_layout_x_spacing + GuiDefaults::ButtonIconSize) * col,
        buttonTextTopOffset + (GuiDefaults::ButtonIconVerticalSpacing + GuiDefaults::ButtonIconSize) * row,
        buttonTextWidth,
        buttonTextHeight);
}

constexpr WindowMultiIconButton::Pngs shortcut_icon_home GEN_ICON_NAMES(home);
// The XL packs no disabled variant of this one; the shortcuts are never shadowed
constexpr WindowMultiIconButton::Pngs shortcut_icon_mapping { img::mapping_80x80, img::mapping_80x80_focused, img::mapping_80x80 };
constexpr WindowMultiIconButton::Pngs shortcut_icon_calibrate GEN_ICON_NAMES(calibrate);
constexpr WindowMultiIconButton::Pngs shortcut_icon_stop GEN_ICON_NAMES(stop);
constexpr WindowMultiIconButton::Pngs shortcut_icon_preheat GEN_ICON_NAMES(preheat);
constexpr WindowMultiIconButton::Pngs shortcut_icon_resume GEN_ICON_NAMES(resume);

const WindowMultiIconButton::Pngs &shortcut_icon(home_shortcut::Kind kind) {
    using Kind = home_shortcut::Kind;
    switch (kind) {
    case Kind::none:
    case Kind::auto_home:
        return shortcut_icon_home;
    case Kind::pick_park:
        return shortcut_icon_mapping;
    case Kind::move_axis:
        return shortcut_icon_calibrate;
    case Kind::motors_off:
        return shortcut_icon_stop;
    case Kind::wipe_nozzle:
        return shortcut_icon_preheat;
    case Kind::macro:
        return shortcut_icon_resume;
    }
    return shortcut_icon_home;
}

} // namespace
#endif

const char *labels[] = {
    N_("Print"),
    N_("Preheat"),
    N_("Filament"),
    N_("Control"),
    N_("Settings"),
    N_("Info"),
    N_("No USB") // label variant for first button
};

bool screen_home_data_t::usbWasAlreadyInserted = false;
bool screen_home_data_t::need_check_wifi_credentials = true;

[[maybe_unused]] static bool find_latest_gcode(char *fpath, int fpath_len) {
    auto sb = StringBuilder::from_ptr(fpath, fpath_len);
    sb.append_string("/usb/");

    Directory dir { fpath };
    if (!dir) {
        return false;
    }

    // prepare the item at the zeroth position according to sort policy
    FileSort::Entry entry;

    while (dirent *fno = FileSort::find_next(dir)) {
        const FileSort::EntryRef curr(*fno, fpath);

        if (curr.type != FileSort::EntryType::FILE) {
            continue;
        }

        if (entry.is_valid() && !FileSort::less_by_time(curr, entry)) {
            continue;
        }

        entry.CopyFrom(curr);
    }

    if (!entry.is_valid()) {
        return false;
    }

    sb.append_string(entry.name.canonical());
    return sb.is_ok();
}

static void FilamentBtn_cb(window_t &) {
#if HAS_INDX()
    if (config_store().selftest_result_nozzle_cleaner_calibration.get() != TestResult::passed) {
        if (MsgBoxWarning(_("Please calibrate nozzle cleaner first before loading/unloading filaments."), { Response::Ok, Response::Ignore }) != Response::Ignore) {
            return;
        }
    }
#endif
    Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuFilament>);
}

#if HAS_MMU2()
static void FilamentBtnMMU_cb(window_t &) {
    Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuFilamentMMU>);
}
#endif

// clang-format off
screen_home_data_t::screen_home_data_t()
    : screen_t()
    , usbInserted(marlin_vars().media_inserted)
    , header(this)
    , footer(this)
#if HAS_MINI_DISPLAY()
    , logo(this, logoRect, &img::prusa_mini_logo_153x40)
#endif
    , w_buttons {
        { this, Rect16(), nullptr, [](window_t&) { Screens::Access()->Open(ScreenFactory::Screen<screen_filebrowser_data_t>); } },
        { this, Rect16(), nullptr, [](window_t&) { marlin_client::gcode_printf("M1700 T-1"); } },
        { this, Rect16(), nullptr, FilamentBtn_cb },
        { this, Rect16(), nullptr, [](window_t&) { Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuControl>); } },
        { this, Rect16(), nullptr, [](window_t&) { Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuSettings>); } },
        { this, Rect16(), nullptr, [](window_t&) { Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuInfo>); }}
    },
    w_labels {
        { this, Rect16(), is_multiline::no },
        { this, Rect16(), is_multiline::no },
        { this, Rect16(), is_multiline::no },
        { this, Rect16(), is_multiline::no },
        { this, Rect16(), is_multiline::no },
        { this, Rect16(), is_multiline::no }
    }
#if PRINTER_IS_PRUSA_XL()
    , w_shortcut_buttons {
        { this, Rect16(), nullptr, [](window_t&) {} },
        { this, Rect16(), nullptr, [](window_t&) {} }
    },
    w_shortcut_labels {
        { this, Rect16(), is_multiline::no },
        { this, Rect16(), is_multiline::no }
    }
#endif
    {
    // clang-format on

    EnableLongHoldScreenAction();
    window_frame_t::ClrMenuTimeoutClose();
    window_frame_t::ClrOnSerialClose(); // don't close on Serial print

#if !HAS_MINI_DISPLAY()
    header.SetIcon(&img::home_shape_16x16);
#endif

    {
        StringBuilder sb(header_text);
#if SIGNATURE_OAK()
        // #error dead code found by automatic analyses (see BFW-5461)
        sb.append_string("SIGNATURE OAK ");
#else
        sb.append_string("PRUSA ");
        sb.append_string(PrinterModelInfo::current().display_str());
        sb.append_string(" ");
#endif
        sb.append_string(version::project_version);
        sb.append_string(version::project_version_suffix_short);
#if DEVELOPER_MODE()
        // #error dead code found by automatic analyses (see BFW-5461)
        sb.append_string(" DEV");
#endif
#ifdef _DEBUG
        sb.append_string(" DBG");
#endif
        header.SetText(string_view_utf8::MakeRAM(header_text.data()));
    }

    for (uint8_t row = 0; row < 2; row++) {
        for (uint8_t col = 0; col < 3; col++) {
            const size_t i = row * 3 + col;
            w_buttons[i].SetRect(buttonRect(col, row));
            w_buttons[i].SetRes(&icons[i]);
            w_labels[i].SetRect(buttonTextRect(col, row));
            w_labels[i].set_font(Font::small);
            w_labels[i].SetAlignment(Align_t::Center());
            w_labels[i].SetPadding({ 0, 0, 0, 0 });
            w_labels[i].SetText(_(labels[i]));
        }
    }

#if PRINTER_IS_PRUSA_XL()
    setup_shortcuts();
#endif

    filamentBtnSetState();

    if (!usbInserted) {
        printBtnDis();
    } else {
        usbWasAlreadyInserted = true;
    }
    ever_been_opened = true;
}

screen_home_data_t::~screen_home_data_t() {
    GuiMediaEventsHandler::ConsumeOneClickPrinting();
}

#if HAS_NFC()
void screen_home_data_t::update_nfc_state() {
    if (GetLastDialog()) {
        nfc_enable.reset();
    } else {
        if (!nfc_enable) {
            nfc_enable.emplace();
        }
    }
}
#endif

void screen_home_data_t::filamentBtnSetState() {
#if HAS_MMU2()
    const MMU2::xState new_state = MMU2::xState(marlin_vars().mmu2_state.get());
    if (new_state != mmu_state) {
        mmu_state = new_state;

        // did not want to include MMU
        // it might be good idea to move mmu enum to extra header
        // TODO move this code
        switch (mmu_state) {
        case MMU2::xState::Active:
            w_buttons[buttonFilamentIndex].SetRes(&icons[iconMMUId]);
            w_buttons[buttonFilamentIndex].SetAction(FilamentBtnMMU_cb);
            w_buttons[buttonFilamentIndex].Unshadow();
            w_buttons[buttonFilamentIndex].Enable();
            break;
        case MMU2::xState::Connecting:
        case MMU2::xState::Bootloader:
            w_buttons[buttonFilamentIndex].SetRes(&icons[iconMMUId]);
            if (w_buttons[buttonFilamentIndex].IsFocused()) {
                w_buttons[buttonFilamentIndex - 1].SetFocus();
            }
            w_buttons[buttonFilamentIndex].Shadow();
            w_buttons[buttonFilamentIndex].Disable();
            break;
        case MMU2::xState::Stopped:
            w_buttons[buttonFilamentIndex].SetRes(&icons[iconNonMMUId]);
            w_buttons[buttonFilamentIndex].SetAction(FilamentBtn_cb);
            w_buttons[buttonFilamentIndex].Unshadow();
            w_buttons[buttonFilamentIndex].Enable();
            break;
        }
    }
#endif
}

#if PRINTER_IS_PRUSA_XL()
void screen_home_data_t::setup_shortcuts() {
    std::array<const home_shortcut::Action *, home_shortcut::slot_count> actions;
    bool any_shown = false;
    for (size_t slot = 0; slot < actions.size(); ++slot) {
        actions[slot] = &home_shortcut::find(config_store().home_shortcuts.get(slot));
        any_shown = any_shown || actions[slot]->kind != home_shortcut::Kind::none;
    }

    if (any_shown) {
        for (size_t i = 0; i < button_count; ++i) {
            w_buttons[i].SetRect(shortcut_layout_button_rect(i % 3, i / 3));
            w_labels[i].SetRect(shortcut_layout_text_rect(i % 3, i / 3));
        }
    }

    for (size_t slot = 0; slot < actions.size(); ++slot) {
        const home_shortcut::Action &action = *actions[slot];
        WindowMultiIconButton &button = w_shortcut_buttons[slot];
        window_text_t &label = w_shortcut_labels[slot];

        if (action.kind == home_shortcut::Kind::none) {
            // A hidden window can still take touch and knob focus while it is enabled
            button.Disable();
            button.Hide();
            label.Hide();
            continue;
        }

        button.SetRect(shortcut_layout_button_rect(shortcut_layout_columns - 1, slot));
        button.SetRes(&shortcut_icon(action.kind));
        button.SetAction([id = action.id](window_t &) { home_shortcut::run(id); });
        label.SetRect(shortcut_layout_text_rect(shortcut_layout_columns - 1, slot));
        label.set_font(Font::small);
        label.SetAlignment(Align_t::Center());
        label.SetPadding({ 0, 0, 0, 0 });
        label.SetText(home_shortcut::label(action, shortcut_label_params[slot]));
    }
}
#endif

namespace {
struct Config {
    enum class Status { missing,
        equal,
        not_equal };

    Status ssid_status = Status::missing;
    Status psk_status = Status::missing;

    Status get_status() {
        if (ssid_status == Status::missing || psk_status == Status::missing) {
            return Status::missing;
        } else if (ssid_status == Status::not_equal || psk_status == Status::not_equal) {
            return Status::not_equal;
        }
        return Status::equal;
    }
};

int ini_handler(void *user, const char *section, const char *name, const char *value) {
    if (user == nullptr || section == nullptr || name == nullptr || value == nullptr) {
        return 0;
    }

    if (strcmp("wifi", section)) {
        return 1; // do I return 0 or 1 ??? I have no clue what would 0 do.
    }

    auto *config = reinterpret_cast<Config *>(user);
    size_t len = strlen(value);

    if (strcmp(name, "ssid") == 0) {
        char buffer[config_store_ns::wifi_max_ssid_len];
        if (len <= sizeof(buffer)) {
            config->ssid_status = strncmp(value, config_store().wifi_ap_ssid.get_c_str(), sizeof(buffer)) ? Config::Status::not_equal : Config::Status::equal;
        }
    } else if (strcmp(name, "psk") == 0) {
        char buffer[config_store_ns::wifi_max_passwd_len];
        if (len <= sizeof(buffer)) {
            config->psk_status = strncmp(value, config_store().wifi_ap_password.get_c_str(), sizeof(buffer)) ? Config::Status::not_equal : Config::Status::equal;
        }
    }

    return 1;
}

Config::Status name_and_psk_status() {
    Config config;
    bool ok = ini_parse(settings_ini::file_name, ini_handler, &config) == 0;
    if (!ok) {
        return Config::Status::missing;
    }
    return config.get_status();
}
} // namespace

void screen_home_data_t::handle_wifi_credentials() {
#if HAS_EMBEDDED_ESP32()
    // The offered setup wizard does not run while the Wi-Fi is off
    if (!netdev_is_wifi_usable()) {
        return;
    }
#endif

    const bool has_wifi_credentials = access(settings_ini::file_name, R_OK) == 0;
    if (has_wifi_credentials && (name_and_psk_status() == Config::Status::not_equal) && !option::developer_mode) {
        if (MsgBoxInfo(_("Wi-Fi credentials (SSID and password) discovered on the USB flash drive. Would you like to connect your printer to Wi-Fi now?"), Responses_YesNo, 1)
            == Response::Yes) {
            marlin_client::gcode("M1703 I");
            return;
        }
    }
}

void screen_home_data_t::windowEvent(window_t *sender, GUI_event_t event, void *param) {
    // TODO: This easily freezes home screen when flash action fails to start.
    // There are several places in the code where executing a flash gcode can
    // result in no-op and home screen stays active with events disabled.
    if (event == GUI_event_t::MEDIA) { // Also stores during windowEvent recursion
        media_event = MediaState_t(int(param));
    }

    if (event_in_progress) {
        return;
    }

    AutoRestore avoid_recursion(event_in_progress, true);

    if (media_event != MediaState_t::unknown) {
        switch (MediaState_t(media_event)) {
        case MediaState_t::inserted:
            if (!usbInserted) {
                usbInserted = true;
                printBtnEna();
                if (!usbWasAlreadyInserted) {
                    w_buttons[0].SetFocus(); // print button
                    usbWasAlreadyInserted = true;
                }
            }
            break;
        case MediaState_t::removed:
            if (usbInserted) {
                usbInserted = false;
                printBtnDis();
            }
            break;
        default:
            break;
        }
        media_event = MediaState_t::unknown;
    }

    if (event == GUI_event_t::LOOP) {
        filamentBtnSetState();

#if HAS_POWER_PANIC()
        if (TaskDeps::check(TaskDeps::Dependency::autostart_done) && !power_panic::is_power_panic_resuming())
#endif
        { // every time usb is inserted we check wifi credentials
            if (usbInserted) {
                if (need_check_wifi_credentials) {
                    need_check_wifi_credentials = false;
                    handle_wifi_credentials();
                }
            } else {
                need_check_wifi_credentials = true; // usb is not inserted, when it gets inserted we want to recheck credentials file
            }

#if HAS_TOOLCHANGER() && !HAS_INDX()
            // The counters grow mostly while other screens replace this one (printing, menus): once per instance is enough
            if (!maintenance_reminders_checked) {
                maintenance_reminders_checked = maintenance_reminders::prompt_due_items();
            }
#endif
        }

#if HAS_SELFTEST()
        if (!DialogHandler::Access().IsOpen()) {
            if (HAS_HUMAN_INTERACTIONS() &&
    #if HAS_POWER_PANIC()
                TaskDeps::check(TaskDeps::Dependency::autostart_done) && !power_panic::is_power_panic_resuming() &&
    #endif
                GuiMediaEventsHandler::ConsumeOneClickPrinting() && !usbh_power_cycle::block_one_click_print()) {
                // TODO this should be done in main thread before media is reported as inserted
                // if it is not the latest gcode might not be selected

                std::array<char, filename_defs::path_buffer_size> filepath;
                if (find_latest_gcode(filepath.data(), filepath.size())) {
                    marlin_client::print_start(filepath.data());
                }
            }
        }
#endif // HAS_SELFTEST
    }

    screen_t::windowEvent(sender, event, param);

#if HAS_NFC()
    // This is to handle the Preheat dialog, that is put above this screen
    // instead of replacing it, leaving NFC enabled.
    update_nfc_state();
#endif
}

void screen_home_data_t::printBtnEna() {
    w_buttons[0].Unshadow();
    w_buttons[0].Enable(); // can be focused
    w_buttons[0].Invalidate();
    w_labels[0].SetText(_(labels[labelPrintId]));
}

void screen_home_data_t::printBtnDis() {
    // move to preheat when Print is focused
    if (w_buttons[0].IsFocused()) {
        w_buttons[1].SetFocus();
    }

    w_buttons[0].Shadow();
    w_buttons[0].Disable(); // cant't be focused
    w_buttons[0].Invalidate();
    w_labels[0].SetText(_(labels[labelNoUSBId]));
}

void screen_home_data_t::InitState(screen_init_variant var) {
    if (!var.GetPosition()) {
        return;
    }

    size_t pos = *(var.GetPosition());
#if PRINTER_IS_PRUSA_XL()
    // Shortcuts come after the stock buttons; a shortcut set to None meanwhile does not take the focus
    if (pos >= button_count && pos < button_count + home_shortcut::slot_count) {
        w_shortcut_buttons[pos - button_count].SetFocus();
        return;
    }
#endif
    if (pos >= button_count) {
        return;
    }

    w_buttons[pos].SetFocus();
}

screen_init_variant screen_home_data_t::GetCurrentState() const {
    screen_init_variant ret;
    for (size_t i = 0; i < button_count; ++i) {
        if (w_buttons[i].IsFocused()) {
            ret.SetPosition(i);
            return ret;
        }
    }
#if PRINTER_IS_PRUSA_XL()
    for (size_t slot = 0; slot < home_shortcut::slot_count; ++slot) {
        if (w_shortcut_buttons[slot].IsFocused()) {
            ret.SetPosition(button_count + slot);
            return ret;
        }
    }
#endif
    return ret;
}
