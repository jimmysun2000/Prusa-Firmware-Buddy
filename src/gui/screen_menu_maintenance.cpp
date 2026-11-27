/// @file
#include "screen_menu_maintenance.hpp"

#include "maintenance_reminders.hpp"

#include <ScreenHandler.hpp>
#include <WindowMenuInfo.hpp>
#include <WindowMenuItems.hpp>
#include <WindowMenuSpin.hpp>
#include <window_msgbox.hpp>
#include <utils/string_builder.hpp>
#include <gui/menu_item/menu_item_select_menu.hpp>
#include <gui/screen/screen_menu_virtual.hpp>

#include <algorithm>

namespace {

using maintenance_reminders::Metric;
using maintenance_reminders::nozzle_types;
using maintenance_reminders::NozzleType;
using maintenance_reminders::NozzleTypeInfo;
using maintenance_reminders::Slot;

using LabelBuffer = std::array<char, 32>;

const NumericInputConfig hours_interval_config {
    .max_value = 10000,
    .step = 10,
    .special_value = 0,
};

const NumericInputConfig filament_interval_config {
    .max_value = 500000,
    .step = 100,
    .special_value = 0,
};

const NumericInputConfig tool_changes_interval_config {
    .max_value = 1000000,
    .step = 1000,
    .special_value = 0,
};

const NumericInputConfig months_interval_config {
    .max_value = 24,
    .step = 1,
    .special_value = 0,
};

const NumericInputConfig &interval_config(Metric metric) {
    switch (metric) {
    case Metric::print_hours:
        return hours_interval_config;
    case Metric::filament_m:
        return filament_interval_config;
    case Metric::months:
        return months_interval_config;
    case Metric::tool_changes:
        break;
    }
    return tool_changes_interval_config;
}

class MenuItemMaintenanceSlot final : public WI_INFO_t {
public:
    MenuItemMaintenanceSlot(const Slot &slot)
        : WI_INFO_t(string_view_utf8 {})
        , slot_(slot) {
        auto sb = StringBuilder::from_ptr(label_.data(), label_.size());
        maintenance_reminders::format_name(sb, slot_);
        SetLabel(string_view_utf8::MakeRAM(label_.data()));
        update();
    }

    void click(IWindowMenu &) override {
        if (maintenance_reminders::interval(slot_) == 0) {
            return;
        }

        StringViewUtf8Parameters<std::tuple_size_v<LabelBuffer>> params;
        if (MsgBoxQuestion(_("Reset the %s maintenance counter?").formatted(params, label_.data()), Responses_YesNo, 1) == Response::Yes) {
            maintenance_reminders::reset(slot_);
            update();
        }
    }

private:
    void update() {
        ArrayStringBuilder<GetInfoLen()> sb;
        maintenance_reminders::format_progress(sb, slot_);
        ChangeInformation(sb.str_nocheck());
    }

    Slot slot_;
    LabelBuffer label_ {};
};

class MenuItemMaintenanceInterval final : public WiSpin {
public:
    MenuItemMaintenanceInterval(const Slot &slot)
        : WiSpin(static_cast<float>(maintenance_reminders::interval(slot)), interval_config(maintenance_reminders::items[slot.item].metric), string_view_utf8 {}, nullptr, is_settable(slot) ? is_enabled_t::yes : is_enabled_t::no)
        , slot_(slot) {
        auto sb = StringBuilder::from_ptr(label_.data(), label_.size());
        maintenance_reminders::format_name(sb, slot_);
        sb.append_string(" (");
        sb.append_string_view(_(maintenance_reminders::unit_name(maintenance_reminders::items[slot.item].metric)));
        sb.append_char(')');
        SetLabel(string_view_utf8::MakeRAM(label_.data()));
    }

    /// Shows the stored interval again, after the nozzle type changed it
    void reload() {
        set_value(static_cast<float>(maintenance_reminders::interval(slot_)));
        set_enabled(is_settable(slot_));
    }

protected:
    void OnClick() override {
        maintenance_reminders::set_interval(slot_, static_cast<uint32_t>(value()));
    }

private:
    /// A tool's nozzle wear interval waits for its nozzle type
    static bool is_settable(const Slot &slot) {
        return !slot.tool || maintenance_reminders::nozzle_type(*slot.tool) != NozzleType::not_set;
    }

    Slot slot_;
    LabelBuffer label_ {};
};

/// Nozzle type of a tool, for its nozzle wear reminder only: "-" until the user picks one
class MenuItemNozzleType final : public MenuItemSelectMenu {
public:
    MenuItemNozzleType(PhysicalToolIndex tool)
        : MenuItemSelectMenu(string_view_utf8 {})
        , tool_(tool) {
        auto sb = StringBuilder::from_ptr(label_.data(), label_.size());
        sb.append_string_view(_("Nozzle type"));
        sb.append_char(' ');
        sb.append_std_string_view(tool.compact_display_name());
        SetLabel(string_view_utf8::MakeRAM(label_.data()));
        update();
    }

    int item_count() const override {
        return static_cast<int>(nozzle_types.size()) + (has_unset_entry_ ? 1 : 0);
    }

    string_view_utf8 build_item_text(int index, StringViewUtf8ParamBase &) const override {
        const int type_index = index - (has_unset_entry_ ? 1 : 0);
        return (type_index < 0) ? string_view_utf8::MakeCPUFLASH("-") : _(nozzle_types[type_index].name);
    }

protected:
    bool on_item_selected(const OnItemSelectedArgs &args) override {
        const int type_index = args.new_index - (has_unset_entry_ ? 1 : 0);
        if (type_index < 0) {
            return false;
        }

        // The first type starts the counting by itself
        const bool reset_counter = !has_unset_entry_
            && MsgBoxQuestion(_("New nozzle fitted? Reset the counter?"), Responses_YesNo, 1) == Response::Yes;
        maintenance_reminders::set_nozzle_type(tool_, nozzle_types[type_index].type, reset_counter);
        update();

        // The intervals menu puts the tool's nozzle wear interval right after its type
        if (const auto index = args.menu.item_index(this)) {
            if (auto *interval_item = args.menu.item_at(*index + 1)) {
                static_cast<MenuItemMaintenanceInterval *>(interval_item)->reload();
            }
        }

        // update() selected the new type already: with the "-" entry gone, the index in args points one too far
        return false;
    }

private:
    void update() {
        const NozzleType type = maintenance_reminders::nozzle_type(tool_);
        has_unset_entry_ = (type == NozzleType::not_set);
        const auto info = std::ranges::find(nozzle_types, type, &NozzleTypeInfo::type);
        // Forced: the entries move when the "-" entry goes
        force_set_current_item(has_unset_entry_ ? 0 : static_cast<int>(info - nozzle_types.begin()));
    }

    PhysicalToolIndex tool_;
    bool has_unset_entry_ = false;
    LabelBuffer label_ {};
};

/// Entry of the intervals menu: the interval of a visible slot, and right before a tool's
/// nozzle wear interval its nozzle type
struct IntervalsEntry {
    Slot slot;
    bool is_nozzle_type;
};

int entry_count_of(const Slot &slot) {
    return slot.tool ? 2 : 1;
}

int intervals_entry_count() {
    int result = 0;
    for (size_t i = 0, count = maintenance_reminders::visible_slot_count(); i < count; ++i) {
        result += entry_count_of(maintenance_reminders::visible_slot(i));
    }
    return result;
}

IntervalsEntry intervals_entry(int index) {
    for (size_t i = 0, count = maintenance_reminders::visible_slot_count(); i < count; ++i) {
        const Slot slot = maintenance_reminders::visible_slot(i);
        if (index < entry_count_of(slot)) {
            return { slot, slot.tool && index == 0 };
        }
        index -= entry_count_of(slot);
    }
    return { maintenance_reminders::visible_slot(0), false };
}

constexpr screen_menu_virtual::Configuration intervals_menu {
    .item_count = []() -> int {
        return intervals_entry_count() + 1; // + MI_RETURN
    },
    .item_constructor = [](WindowMenuVirtual::ItemVariant &variant, int index) {
        if (index == 0) {
            variant.emplace<MI_RETURN>();
            return;
        }

        const IntervalsEntry entry = intervals_entry(index - 1);
        if (entry.is_nozzle_type) {
            variant.emplace<MenuItemNozzleType>(*entry.slot.tool);
        } else {
            variant.emplace<MenuItemMaintenanceInterval>(entry.slot);
        } //
    },
    .title = N_("MAINTENANCE INTERVALS"),
};

class MI_MAINTENANCE_INTERVALS final : public MI_SCREEN_BASE {
public:
    MI_MAINTENANCE_INTERVALS()
        : MI_SCREEN_BASE(ScreenFactory::ScreenWithArg<ScreenMenuVirtual>(&intervals_menu), N_("Intervals")) {}
};

constexpr int fixed_item_count = 2; // MI_RETURN, MI_MAINTENANCE_INTERVALS

constexpr screen_menu_virtual::Configuration maintenance_menu {
    .item_count = []() -> int {
        return static_cast<int>(maintenance_reminders::visible_slot_count()) + fixed_item_count;
    },
    .item_constructor = [](WindowMenuVirtual::ItemVariant &variant, int index) {
        switch (index) {
        case 0:
            variant.emplace<MI_RETURN>();
            break;
        case 1:
            variant.emplace<MI_MAINTENANCE_INTERVALS>();
            break;
        default:
            variant.emplace<MenuItemMaintenanceSlot>(maintenance_reminders::visible_slot(index - fixed_item_count));
            break;
        } //
    },
    .title = N_("MAINTENANCE"),
};

ScreenFactory::Creator maintenance_screen_creator() {
    return ScreenFactory::ScreenWithArg<ScreenMenuVirtual>(&maintenance_menu);
}

} // namespace

MI_MAINTENANCE::MI_MAINTENANCE()
    : MI_SCREEN_BASE(maintenance_screen_creator(), N_("Maintenance")) {
    // The home screen applies the defaults only while idle, this menu shows them anyway
    maintenance_reminders::apply_defaults();
}

void open_maintenance_screen() {
    maintenance_reminders::apply_defaults();
    Screens::Access()->Open(maintenance_screen_creator());
}
