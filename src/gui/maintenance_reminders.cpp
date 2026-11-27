#include "maintenance_reminders.hpp"
#include "maintenance_reminder_math.hpp"

#include <config_store/store_instance.hpp>
#include <common/marlin_vars.hpp>
#include <odometer.hpp>
#include <utils/string_builder.hpp>
#include <window_msgbox.hpp>
#include <ScreenHandler.hpp>
#include <DialogHandler.hpp>

#include <cinttypes>
#include <ctime>

namespace maintenance_reminders {

namespace {
    constexpr const char *txt_due { N_("Maintenance due: %s - %s since last reset (interval %s).") };

    struct MetricInfo {
        /// Counting units in one setting unit
        uint32_t unit_size;

        /// Translatable setting unit
        const char *unit;
    };

    constexpr MetricInfo metric_info(Metric metric) {
        switch (metric) {
        case Metric::print_hours:
            return { 3600, N_("h") };
        case Metric::filament_m:
            return { 1000, N_("m") };
        case Metric::tool_changes:
            return { 1, N_("changes") };
        case Metric::months:
            return { seconds_per_month, N_("months") };
        }
        return { 1, "" };
    }

    /// RAM only: counter value from which a postponed slot shows again, 0 = not postponed
    std::array<uint32_t, slot_count> postponed_until_counter {};

    /// @returns the counter of @p slot in the counting unit: of the odometer, or of the clock for calendar rows (0 = unknown)
    uint32_t counter(const Slot &slot) {
        auto &odometer = Odometer_s::instance();
        const auto &tool = slot.tool;
        switch (items[slot.item].metric) {
        case Metric::print_hours:
            return tool ? odometer.get_tool_time(*tool) : odometer.get_time();
        case Metric::filament_m: {
            const float mm = tool ? odometer.get_extruded(*tool) : odometer.get_extruded_all();
            // Keeps the float to integer conversion in range
            return static_cast<uint32_t>(std::clamp(mm, 0.0f, 4.0e9f));
        }
        case Metric::tool_changes:
            return tool ? odometer.get_toolpick(*tool) : odometer.get_toolpick_all();
        case Metric::months:
            return date_counter(time(nullptr));
        }
        return 0;
    }

    uint32_t usage_in_counts(const Slot &slot, uint32_t counter_value) {
        const uint32_t baseline = config_store().maintenance_baseline.get(slot.index());
        return (items[slot.item].metric == Metric::months) ? date_usage(counter_value, baseline) : usage(counter_value, baseline);
    }

    /// Calls @p f(slot) for every slot of row @p item, including disabled tools, until it returns false
    template <typename F>
    bool for_each_slot_of(uint8_t item, F &&f) {
        if (items[item].scope == Scope::printer) {
            return f(Slot { item, std::nullopt });
        }
        for (const auto tool : PhysicalToolIndex::all()) {
            if (!f(Slot { item, tool })) {
                return false;
            }
        }
        return true;
    }

    /// Calls @p f(slot) for every visible slot until it returns false
    template <typename F>
    void for_each_visible_slot(F &&f) {
        for (uint8_t item = 0; item < item_count; ++item) {
            const bool go_on = for_each_slot_of(item, [&](const Slot &slot) {
                return (slot.tool && !slot.tool->is_enabled()) || f(slot);
            });
            if (!go_on) {
                return;
            }
        }
    }

    void append_amount(StringBuilder &sb, uint32_t amount, Metric metric) {
        sb.append_printf("%" PRIu32 " ", amount);
        sb.append_string_view(_(metric_info(metric).unit));
    }

    bool is_idle_without_dialog() {
        const screen_t *screen = Screens::Access()->Get();
        return marlin_vars().print_state.get() == marlin_server::State::Idle
            && !DialogHandler::Access().IsOpen()
            && screen && !screen->GetFirstDialog();
    }

    Response show_reminder(const Slot &slot, uint32_t used, uint32_t slot_interval) {
        const Metric metric = items[slot.item].metric;

        ArrayStringBuilder<40> name;
        format_name(name, slot);
        ArrayStringBuilder<24> used_text;
        append_amount(used_text, used, metric);
        ArrayStringBuilder<24> interval_text;
        append_amount(interval_text, slot_interval, metric);

        // Holds the three texts above, each at most its builder size including the terminator
        StringViewUtf8Parameters<96> params;

        return MsgBoxBuilder {
            .type = MsgBoxType::info,
            .text = _(txt_due).formatted(params, name.str_nocheck(), used_text.str_nocheck(), interval_text.str_nocheck()),
            .responses = { Response::Done, Response::NotNow, Response::_none, Response::_none },
            .default_button = MsgBoxDefaultButton::b1,
            .loop_callback = [] {
                // A print started remotely: get out of its way without an answer
                if (marlin_vars().print_state.get() != marlin_server::State::Idle) {
                    Screens::Access()->close_dialog();
                }
            },
        }
            .exec();
    }
} // namespace

size_t Slot::index() const {
    return first_slot(item) + (tool ? tool->to_raw() : 0);
}

size_t visible_slot_count() {
    size_t result = 0;
    for_each_visible_slot([&](const Slot &) {
        ++result;
        return true;
    });
    return result;
}

Slot visible_slot(size_t n) {
    std::optional<Slot> result;
    for_each_visible_slot([&](const Slot &slot) {
        if (n == 0) {
            result = slot;
            return false;
        }
        --n;
        return true;
    });
    return result.value_or(Slot { 0, std::nullopt });
}

const char *unit_name(Metric metric) {
    return metric_info(metric).unit;
}

uint32_t interval(const Slot &slot) {
    auto &store = config_store();
    return slot.tool ? store.maintenance_nozzle_interval.get(slot.tool->to_raw()) : store.maintenance_interval.get(slot.item);
}

void set_interval(const Slot &slot, uint32_t new_interval) {
    auto &store = config_store();
    const uint32_t old_interval = interval(slot);
    if (new_interval == old_interval) {
        return;
    }

    auto transaction = store.get_backend().transaction_guard();

    // While Off, the baseline is not maintained: a usage would include everything since it was last set
    if (starts_counting(old_interval, new_interval)) {
        store.maintenance_baseline.set(slot.index(), counter(slot));
    }
    if (slot.tool) {
        store.maintenance_nozzle_interval.set(slot.tool->to_raw(), new_interval);
    } else {
        store.maintenance_interval.set(slot.item, new_interval);
    }

    postponed_until_counter[slot.index()] = 0;
}

void apply_defaults() {
    auto &store = config_store();

    // Older firmware stored one nozzle wear interval for all tools: it becomes the interval of each tool without one.
    // The baselines stay, the tools keep counting.
    if (const uint32_t row_interval = store.maintenance_interval.get(nozzle_wear_item); row_interval != 0) {
        auto transaction = store.get_backend().transaction_guard();
        for (const auto tool : PhysicalToolIndex::all()) {
            if (store.maintenance_nozzle_interval.get(tool.to_raw()) == 0) {
                store.maintenance_nozzle_interval.set(tool.to_raw(), row_interval);
            }
        }
        store.maintenance_interval.set(nozzle_wear_item, 0);
    }

    const size_t applied = store.maintenance_defaults_applied.get();
    const size_t first_row = first_row_to_default(applied, item_count);
    if (first_row == item_count) {
        return;
    }

    auto transaction = store.get_backend().transaction_guard();
    for (uint8_t row = static_cast<uint8_t>(first_row); row < item_count; ++row) {
        const Item &item = items[row];
        const Slot slot { row, std::nullopt };
        if (item.scope == Scope::printer && item.default_interval != 0 && interval(slot) == 0) {
            set_interval(slot, item.default_interval);
        }
    }
    store.maintenance_defaults_applied.set(static_cast<uint8_t>(item_count));
}

uint32_t usage(const Slot &slot) {
    return usage_in_counts(slot, counter(slot)) / metric_info(items[slot.item].metric).unit_size;
}

bool is_due(const Slot &slot) {
    return is_due(usage_in_counts(slot, counter(slot)), interval(slot), metric_info(items[slot.item].metric).unit_size);
}

void reset(const Slot &slot) {
    config_store().maintenance_baseline.set(slot.index(), counter(slot));
    postponed_until_counter[slot.index()] = 0;
}

void format_name(StringBuilder &sb, const Slot &slot) {
    sb.append_string_view(_(items[slot.item].name));
    if (slot.tool) {
        sb.append_char(' ');
        sb.append_std_string_view(slot.tool->compact_display_name());
    }
}

void format_progress(StringBuilder &sb, const Slot &slot) {
    const uint32_t slot_interval = interval(slot);
    if (slot_interval == 0) {
        sb.append_string_view(_("Off"));
        return;
    }
    if (items[slot.item].metric == Metric::months && counter(slot) == 0) {
        sb.append_string_view(_("no clock"));
        return;
    }
    sb.append_printf("%" PRIu32 "/", usage(slot));
    append_amount(sb, slot_interval, items[slot.item].metric);
}

bool prompt_due_items() {
    if (!is_idle_without_dialog()) {
        return false;
    }

    apply_defaults();

    for_each_visible_slot([](const Slot &slot) {
        if (!is_idle_without_dialog()) {
            return false;
        }

        const uint32_t slot_interval = interval(slot);
        const uint32_t unit_size = metric_info(items[slot.item].metric).unit_size;
        const uint32_t counter_value = counter(slot);

        auto &baselines = config_store().maintenance_baseline;
        if (items[slot.item].metric == Metric::months && slot_interval != 0 && needs_date_stamp(counter_value, baselines.get(slot.index()))) {
            // Turned on or reset while the clock was unknown: the counting starts at the first known date
            baselines.set(slot.index(), counter_value);
            return true;
        }

        const uint32_t used = usage_in_counts(slot, counter_value);
        if (!is_due(used, slot_interval, unit_size) || counter_value < postponed_until_counter[slot.index()]) {
            return true;
        }

        switch (show_reminder(slot, used / unit_size, slot_interval)) {
        case Response::Done:
            reset(slot);
            return true;
        case Response::NotNow:
            postponed_until_counter[slot.index()] = postponed_until(counter_value, slot_interval, unit_size);
            return true;
        default:
            // Closed without an answer, a print started
            return false;
        }
    });
    return true;
}

} // namespace maintenance_reminders
