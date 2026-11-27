#include "screen_restore_settings.hpp"

#include <ScreenFactory.hpp>
#include <ScreenHandler.hpp>
#include <WindowMenuInfo.hpp>
#include <WindowMenuItems.hpp>
#include <WindowMenuSwitch.hpp>
#include <common/marlin_client.hpp>
#include <common/marlin_vars.hpp>
#include <common/sys.hpp>
#include <common/utils/dynamic_index_mapping.hpp>
#include <config_store/config_store_import.hpp>
#include <config_store/store_instance.hpp>
#include <feature/factory_reset/factory_reset.hpp>
#include <img_resources.hpp>
#include <screen_menu.hpp>
#include <utils/string_builder.hpp>
#include <window_dlg_wait.hpp>
#include <window_menu_callback_item.hpp>
#include <window_menu_virtual.hpp>
#include <window_msgbox.hpp>

#include <array>
#include <cstring>
#include <span>
#include <string_view>

namespace {

using config_store_import::Error;
using config_store_ns::ItemFlag;
using Item = FactoryReset::Item;
using ItemBitset = FactoryReset::ItemBitset;

/// What the preview shows; the screen argument is too small to carry it
config_store_import::Summary summary;

/// Groups restored unless skipped; all others start skipped
constexpr ItemBitset default_selection = FactoryReset::item_bitset({
    Item::network,
    Item::user_interface,
    Item::stats,
    Item::features,
    Item::user_profiles,
});

constexpr const char *printer_busy_text = N_("Printer is busy. Please try repeating the action later.");

bool is_printer_idle() {
    return marlin_client::is_idle() && !marlin_vars().is_processing;
}

/// Groups whose items the restore never writes
bool is_locked(Item item) {
    const journal::ItemFlags flags = FactoryReset::items_config[item].item_flags;
    return flags == 0 || (flags & config_store_import::never_restored_flags()) != 0;
}

/// File name without the directory
const char *file_name() {
    const char *slash = strrchr(config_store_import::file_path, '/');
    return slash ? slash + 1 : config_store_import::file_path;
}

void show_error(Error error, uint16_t id, uint32_t line) {
    StringViewUtf8Parameters<32> params;
    const auto entry_text = [&](const string_view_utf8 &text) {
        return text.formatted(params, static_cast<unsigned>(id), static_cast<unsigned>(line));
    };

    string_view_utf8 text;
    switch (error) {

    case Error::none:
        return;

    case Error::file_not_found:
        text = _("No %s found on the USB drive. Copy a settings backup there under this name and try again.").formatted(params, file_name());
        break;

    case Error::read_failed:
        text = _("Failed to read the settings backup. Check the USB drive and try again.");
        break;

    case Error::format:
        text = _("The settings backup is damaged or not in the expected format (line %u).").formatted(params, static_cast<unsigned>(line));
        break;

    case Error::incomplete:
        text = _("The settings backup is incomplete.");
        break;

    case Error::checksum:
        text = _("The settings backup is damaged: its checksum does not match.");
        break;

    case Error::format_version:
        text = _("The settings backup is in a format this firmware does not know.");
        break;

    case Error::printer_model:
        text = _("The settings backup is from a different printer model.");
        break;

    case Error::serial_number:
        text = _("The settings backup is from a different printer, or the serial number of this printer cannot be read.");
        break;

    case Error::config_version:
        text = _("The settings backup is from a firmware with a different settings layout and cannot be restored.");
        break;

    case Error::duplicate_entry:
        text = entry_text(_("The settings backup contains entry %04X twice (line %u)."));
        break;

    case Error::invalid_value:
        text = entry_text(_("The value of entry %04X in the settings backup does not fit this firmware (line %u)."));
        break;

    case Error::tool_offset:
        text = entry_text(_("Entry %04X in the settings backup is a tool offset outside the calibration limits (line %u)."));
        break;

    case Error::dock_position:
        text = entry_text(_("Entry %04X in the settings backup is a dock position too far from the dock (line %u)."));
        break;

    case Error::changed:
        text = _("The settings backup changed since it was checked. Open the restore again.");
        break;

    case Error::busy:
        text = _(printer_busy_text);
        break;
    }
    MsgBoxError(text, Responses_Ok);
}

/// Shows why nothing was restored
void show_apply_error(Error error) {
    switch (error) {

    case Error::file_not_found:
    case Error::read_failed:
    case Error::busy:
    case Error::changed:
        show_error(error, 0, 0);
        return;

    default:
        // The file passed these checks in the preview
        show_error(Error::changed, 0, 0);
        return;
    }
}

class MenuItemRestoreGroup final : public MenuItemSwitch {
    static constexpr auto item_texts = std::to_array<const char *>({
        N_("Skip"),
        N_("Restore"),
    });
    static constexpr auto locked_texts = std::to_array<const char *>({
        N_("Never"),
    });

    const Item item_;
    ItemBitset &selection_;

public:
    MenuItemRestoreGroup(const string_view_utf8 &label, Item item, ItemBitset &selection)
        : MenuItemSwitch(label, is_locked(item) ? std::span<const char *const>(locked_texts) : std::span<const char *const>(item_texts), selection.test(std::to_underlying(item)))
        , item_(item)
        , selection_(selection) {
        set_behavior(Behavior::quick_cycle);
        set_enabled(!is_locked(item));
    }

private:
    bool on_item_selected(const OnItemSelectedArgs &args) override {
        MenuItemSwitch::on_item_selected(args);
        selection_.set(std::to_underlying(item_), args.new_index == 1);
        return true;
    }

    Color resolved_value_text_color(Color base_color) const override {
        return selection_.test(std::to_underlying(item_)) ? COLOR_GREEN : base_color;
    }
};

/// Number of values the current selection would write
class MenuItemWillChange final : public WI_INFO_t {
public:
    explicit MenuItemWillChange(const ItemBitset &selection)
        : WI_INFO_t(_("Will change"))
        , selection_(selection) {}

    void Loop() override {
        ArrayStringBuilder<8> builder;
        builder.append_printf("%u", static_cast<unsigned>(summary.changes(FactoryReset::excluded_flags(selection_))));
        ChangeInformation(builder.str());
    }

private:
    const ItemBitset &selection_;
};

enum class Row {
    return_,
    firmware,
    unknown,
    not_restorable,
    group,
    will_change,
    execute,
};

constexpr auto rows = std::to_array<DynamicIndexMappingRecord<Row>>({
    { Row::return_ },
    { Row::firmware },
    { Row::unknown },
    { Row::not_restorable },
    { Row::group, DynamicIndexMappingType::static_section, FactoryReset::items_config.size() },
    { Row::will_change },
    { Row::execute },
});

constexpr DynamicIndexMapping<rows> row_mapping;

/// Preview of the restore: what the backup is, which groups to restore and how many values that changes
class WindowMenuRestoreSettings final : public WindowMenuVirtual {
public:
    WindowMenuRestoreSettings(window_t *parent, Rect16 rect)
        : WindowMenuVirtual(parent, rect, CloseScreenReturnBehavior::yes)
        , selection_(default_selection) {
        for (size_t i = 0; i < labels_.size(); i++) {
            const auto item = static_cast<Item>(i);
            const auto &config = FactoryReset::items_config[item];
            StringBuilder builder(labels_[i]);
            builder.append_string_view(_(config.title));
            if (!is_locked(item)) {
                // Values that are not restored if this group is skipped
                builder.append_printf(" (%u)", static_cast<unsigned>(summary.differing_with_any(config.item_flags)));
            }
        }
        setup_items();
    }

    int item_count() const final {
        return row_mapping.total_item_count();
    }

protected:
    void setup_item(ItemVariant &variant, int index) final {
        const auto mapping = row_mapping.from_index(index);

        switch (mapping.item) {

        case Row::return_:
            variant.emplace<MI_RETURN>();
            break;

        case Row::firmware:
            variant.emplace<WiInfo<std::tuple_size_v<decltype(summary.firmware)>>>(_("Backup firmware"), std::string_view(summary.firmware.data()));
            break;

        case Row::unknown:
            variant.emplace<WI_INFO_t>(summary.unknown_count, _("Unknown values"));
            break;

        case Row::not_restorable:
            variant.emplace<WI_INFO_t>(summary.not_restorable_count, _("Not restorable"));
            break;

        case Row::group:
            variant.emplace<MenuItemRestoreGroup>(string_view_utf8::MakeRAM(labels_[mapping.pos_in_section].data()), static_cast<Item>(mapping.pos_in_section), selection_);
            break;

        case Row::will_change:
            variant.emplace<MenuItemWillChange>(selection_);
            break;

        case Row::execute:
            variant.emplace<WindowMenuCallbackItem>(
                _("Restore and Restart"), [this] { execute(); }, &img::ok_16x16);
            break;
        }
    }

private:
    void execute() {
        const journal::ItemFlags skip_flags = FactoryReset::excluded_flags(selection_);
        const uint16_t changes = summary.changes(skip_flags);
        if (changes == 0) {
            MsgBoxInfo(_("No setting in the selected groups differs from the backup."), Responses_Ok);
            return;
        }

        constexpr journal::ItemFlags hardware_flags = ItemFlag::hw_config | ItemFlag::calibrations;
        const bool restores_hardware = summary.changes(skip_flags | hardware_flags) != changes;
        StringViewUtf8Parameters<8> params;
        const string_view_utf8 question = restores_hardware
            ? _("Restore %u settings from the backup, including HW configuration or calibration values? These are only valid if no hardware was changed, moved or replaced since the backup. The printer restarts right after. This cannot be undone.").formatted(params, static_cast<unsigned>(changes))
            : _("Restore %u settings from the backup? The printer restarts right after. This cannot be undone.").formatted(params, static_cast<unsigned>(changes));
        if (MsgBoxWarning(question, Responses_YesNo, 1) != Response::Yes) {
            return;
        }

        if (!is_printer_idle()) {
            MsgBoxWarning(_(printer_busy_text), Responses_Ok);
            return;
        }

        config_store_import::ApplyResult result { .error = Error::none, .written = false };
        window_dlg_wait_t::wait_until(_("Restoring settings..."), [&] {
            result = config_store_import::apply(config_store_import::file_path, skip_flags, summary.crc, &is_printer_idle);
            if (result.written && result.error == Error::none) {
                // Values cached at boot are stale now
                sys_reset();
            }
            return true;
        });

        if (result.written) {
            MsgBoxError(_("Reading the settings backup failed after some settings were restored. The printer restarts now. Check the USB drive and run the restore again."), Responses_Ok);
            sys_reset();
        }

        if (result.error == Error::none) {
            MsgBoxInfo(_("No setting in the selected groups differs from the backup."), Responses_Ok);
            return;
        }
        show_apply_error(result.error);
    }

    ItemBitset selection_;

    /// Group titles with their numbers of differing values
    std::array<std::array<char, 48>, std::to_underlying(Item::_cnt)> labels_ {};
};

class ScreenRestoreSettings final : public ScreenMenuBase<WindowMenuRestoreSettings> {
public:
    ScreenRestoreSettings()
        : ScreenMenuBase(nullptr, _("RESTORE SETTINGS"), EFooter::Off) {}
};

} // namespace

namespace screen_restore_settings {

void open() {
    if (!is_printer_idle()) {
        MsgBoxWarning(_(printer_busy_text), Responses_Ok);
        return;
    }

    window_dlg_wait_t::wait_until(_("Checking the settings backup..."), [] {
        summary = config_store_import::scan(config_store_import::file_path);
        return true;
    });

    if (summary.error != Error::none) {
        show_error(summary.error, summary.id, summary.line);
        return;
    }

    if (summary.firmware_differs) {
        StringViewUtf8Parameters<64> params;
        if (MsgBoxWarning(_("The settings backup was made with firmware %s. Its settings layout matches this firmware, but values may mean something else. Continue anyway?").formatted(params, summary.firmware.data()), Responses_YesNo, 1) != Response::Yes) {
            return;
        }
    }

    Screens::Access()->Open(ScreenFactory::Screen<ScreenRestoreSettings>);
}

} // namespace screen_restore_settings
