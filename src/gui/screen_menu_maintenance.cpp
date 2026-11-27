/// @file
#include "screen_menu_maintenance.hpp"

#include "maintenance_reminders.hpp"

#include <ScreenHandler.hpp>
#include <WindowMenuInfo.hpp>
#include <WindowMenuItems.hpp>
#include <WindowMenuSpin.hpp>
#include <window_msgbox.hpp>
#include <utils/string_builder.hpp>
#include <gui/screen/screen_menu_virtual.hpp>

namespace {

using maintenance_reminders::Metric;
using maintenance_reminders::Slot;

using LabelBuffer = std::array<char, 32>;

const NumericInputConfig hours_interval_config {
    .max_value = 10000,
    .step = 10,
    .special_value = 0,
};

const NumericInputConfig filament_interval_config {
    .max_value = 100000,
    .step = 100,
    .special_value = 0,
};

const NumericInputConfig tool_changes_interval_config {
    .max_value = 1000000,
    .step = 1000,
    .special_value = 0,
};

const NumericInputConfig &interval_config(Metric metric) {
    switch (metric) {
    case Metric::print_hours:
        return hours_interval_config;
    case Metric::filament_m:
        return filament_interval_config;
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
        if (maintenance_reminders::interval(slot_.item) == 0) {
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
    MenuItemMaintenanceInterval(uint8_t item)
        : WiSpin(static_cast<float>(maintenance_reminders::interval(item)), interval_config(maintenance_reminders::items[item].metric), string_view_utf8 {})
        , item_(item) {
        const auto &table_item = maintenance_reminders::items[item];
        auto sb = StringBuilder::from_ptr(label_.data(), label_.size());
        sb.append_string_view(_(table_item.name));
        sb.append_string(" (");
        sb.append_string_view(_(maintenance_reminders::unit_name(table_item.metric)));
        sb.append_char(')');
        SetLabel(string_view_utf8::MakeRAM(label_.data()));
    }

protected:
    void OnClick() override {
        maintenance_reminders::set_interval(item_, static_cast<uint32_t>(value()));
    }

private:
    uint8_t item_;
    LabelBuffer label_ {};
};

constexpr screen_menu_virtual::Configuration intervals_menu {
    .item_count = []() -> int {
        return maintenance_reminders::item_count + 1; // + MI_RETURN
    },
    .item_constructor = [](WindowMenuVirtual::ItemVariant &variant, int index) {
        if (index == 0) {
            variant.emplace<MI_RETURN>();
        } else {
            variant.emplace<MenuItemMaintenanceInterval>(static_cast<uint8_t>(index - 1));
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

} // namespace

MI_MAINTENANCE::MI_MAINTENANCE()
    : MI_SCREEN_BASE(ScreenFactory::ScreenWithArg<ScreenMenuVirtual>(&maintenance_menu), N_("Maintenance")) {
    // The home screen applies the defaults only while idle, this menu shows them anyway
    maintenance_reminders::apply_defaults();
}
