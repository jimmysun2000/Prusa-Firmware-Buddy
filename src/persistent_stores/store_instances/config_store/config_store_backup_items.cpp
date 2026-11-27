#include "config_store_backup_items.hpp"
#include "store_instance.hpp"
#include "store_journal.hpp"

#include <persistent_stores/journal/gen_journal_hashes.hpp>

#include <algorithm>
#include <string_view>

namespace config_store_backup {

namespace {

    using config_store_ns::CurrentStore;

    constexpr std::array secret_ids {
        decltype(CurrentStore::wifi_ap_password)::hashed_id,
        decltype(CurrentStore::prusalink_password)::hashed_id,
        decltype(CurrentStore::connect_token)::hashed_id,
    };

    /// String items that are written in clear text; any other string item is redacted
    constexpr std::array allowed_string_ids {
        decltype(CurrentStore::hostname)::hashed_id,
        decltype(CurrentStore::wifi_ap_ssid)::hashed_id,
        decltype(CurrentStore::connect_host)::hashed_id,
        decltype(CurrentStore::connect_proxy_host)::hashed_id,
        decltype(CurrentStore::metrics_host)::hashed_id,
    };

    template <typename Item>
    struct LegacyArrayIds {
        static constexpr bool is_legacy_array = false;
    };

    template <journal::StoreItemDataC DataT, auto default_val, journal::ItemFlags flags, auto backend, auto hashed_ids>
    struct LegacyArrayIds<journal::JournalItemLegacyArray<DataT, default_val, flags, backend, hashed_ids>> {
        static constexpr bool is_legacy_array = true;
        static constexpr auto ids = hashed_ids;
    };

    /// Calls f(id) for the id of every name the backup uses: one per current item, one per element of legacy arrays
    template <typename F>
    consteval void for_each_named_id(F f) {
        ConfigStore store;
        visit_all_struct_fields(static_cast<CurrentStore &>(store), [&f]<typename Item>(Item &) {
            if constexpr (LegacyArrayIds<Item>::is_legacy_array) {
                // Each element has a name of its own
                for (const uint16_t id : LegacyArrayIds<Item>::ids) {
                    f(id);
                }
            } else if constexpr (requires { Item::hashed_id_first; }) {
                f(Item::hashed_id_first);
            } else {
                f(Item::hashed_id);
            }
        });
    }

    /// Item names exist only in the generated hash table. Only for use in constant evaluation.
    constexpr std::string_view generated_name(uint16_t id) {
        for (const auto &pair : journal::generated_hashes) {
            if (pair.hashed == id) {
                return pair.name;
            }
        }
        return {};
    }

    constexpr size_t name_count = []() consteval {
        size_t count = 0;
        for_each_named_id([&count](uint16_t) { count++; });
        return count;
    }();

    constexpr size_t name_chars = []() consteval {
        size_t chars = 0;
        for_each_named_id([&chars](uint16_t id) { chars += generated_name(id).size() + 1; });
        return chars;
    }();

    static_assert(name_chars <= UINT16_MAX);

    static_assert([]() consteval {
        bool ok = true;
        for_each_named_id([&ok](uint16_t id) {
            const auto name = generated_name(id);
            ok = ok && !name.empty() && name.size() <= config_store_export::max_name_length;
        });
        return ok;
    }());

    /// Names of the current items only, sorted by id, zero-terminated in one block
    struct NameTable {
        struct Entry {
            uint16_t id;
            uint16_t offset;
        };
        std::array<Entry, name_count> entries;
        std::array<char, name_chars> chars;
    };

    constexpr NameTable name_table = []() consteval {
        NameTable table {};
        size_t index = 0;
        size_t offset = 0;
        for_each_named_id([&](uint16_t id) {
            const auto name = generated_name(id);
            table.entries[index++] = { .id = id, .offset = static_cast<uint16_t>(offset) };
            std::ranges::copy(name, table.chars.begin() + offset);
            offset += name.size();
            table.chars[offset++] = '\0';
        });
        std::ranges::sort(table.entries, {}, &NameTable::Entry::id);
        return table;
    }();

    std::string_view item_name(uint16_t id) {
        const auto it = std::ranges::lower_bound(name_table.entries, id, {}, &NameTable::Entry::id);
        return (it != name_table.entries.end() && it->id == id) ? std::string_view(name_table.chars.data() + it->offset) : std::string_view {};
    }

    /// Calls f(id) for the id of every entry: one per current item, one per element of array items
    template <typename F>
    consteval void for_each_entry_id(F f) {
        ConfigStore store;
        visit_all_struct_fields(static_cast<CurrentStore &>(store), [&f]<typename Item>(Item &) {
            if constexpr (LegacyArrayIds<Item>::is_legacy_array) {
                for (const uint16_t id : LegacyArrayIds<Item>::ids) {
                    f(id);
                }
            } else if constexpr (requires { Item::hashed_id_first; }) {
                for (size_t i = 0; i < Item::item_count; i++) {
                    f(static_cast<uint16_t>(Item::hashed_id_first + i));
                }
            } else {
                f(Item::hashed_id);
            }
        });
    }

    constexpr size_t entry_count = []() consteval {
        size_t count = 0;
        for_each_entry_id([&count](uint16_t) { count++; });
        return count;
    }();

    static_assert(entry_count <= max_entry_count, "Increase max_entry_count");

    /// Ids of all entries, sorted
    constexpr auto entry_ids = []() consteval {
        std::array<uint16_t, entry_count> ids {};
        size_t index = 0;
        for_each_entry_id([&](uint16_t id) { ids[index++] = id; });
        std::ranges::sort(ids);
        return ids;
    }();

    static_assert(std::ranges::adjacent_find(entry_ids) == entry_ids.end());

} // namespace

config_store_export::Key item_key(uint16_t id, std::optional<uint16_t> array_first_id) {
    return array_first_id.has_value()
        ? config_store_export::Key { .id = id, .name = item_name(*array_first_id), .index = static_cast<uint8_t>(id - *array_first_id) }
        : config_store_export::Key { .id = id, .name = item_name(id) };
}

bool is_redacted(uint16_t id, Kind kind) {
    return config_store_export::is_redacted(id, kind == Kind::string, secret_ids, allowed_string_ids);
}

std::optional<uint16_t> entry_position(uint16_t id) {
    const auto it = std::ranges::lower_bound(entry_ids, id);
    if (it == entry_ids.end() || *it != id) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(it - entry_ids.begin());
}

} // namespace config_store_backup
