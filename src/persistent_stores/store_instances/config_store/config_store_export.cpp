#include "config_store_export.hpp"
#include "config_store_export_format.hpp"
#include "store_instance.hpp"
#include "store_journal.hpp"

#include <persistent_stores/journal/gen_journal_hashes.hpp>
#include <common/crc32.h>
#include <common/otp.hpp>
#include <common/printer_model.hpp>
#include <raii/scope_guard.hpp>
#include <unique_file_ptr.hpp>
#include <version/version.hpp>

#include <algorithm>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <optional>
#include <type_traits>
#include <unistd.h>
#include <utility>

namespace config_store_export {

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
            ok = ok && !name.empty() && name.size() <= max_name_length;
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

    enum class Kind : uint8_t {
        boolean,
        unsigned_integer,
        signed_integer,
        floating_point,
        string,
        raw,
    };

    template <typename T>
    inline constexpr bool is_char_array = false;

    template <size_t N>
    inline constexpr bool is_char_array<std::array<char, N>> = true;

    template <typename T>
    consteval Kind kind_of() {
        if constexpr (is_char_array<T>) {
            return Kind::string;
        } else if constexpr (std::is_same_v<T, bool>) {
            return Kind::boolean;
        } else if constexpr (std::is_enum_v<T>) {
            return kind_of<std::underlying_type_t<T>>();
        } else if constexpr (std::is_integral_v<T> && sizeof(T) <= sizeof(uint32_t)) {
            return std::is_signed_v<T> ? Kind::signed_integer : Kind::unsigned_integer;
        } else if constexpr (std::is_same_v<T, float>) {
            return Kind::floating_point;
        } else {
            return Kind::raw;
        }
    }

    /// Type-independent part of writing one entry
    [[gnu::noinline]] void write_entry(LineSink &sink, std::optional<uint16_t> array_first_id, uint16_t id, Kind kind, std::span<const std::byte> bytes, const char *comment) {
        const Key key = array_first_id.has_value()
            ? Key { .id = id, .name = item_name(*array_first_id), .index = static_cast<uint8_t>(id - *array_first_id) }
            : Key { .id = id, .name = item_name(id) };

        if (is_redacted(id, kind == Kind::string, secret_ids, allowed_string_ids)) {
            write_redacted(sink, key);
            return;
        }

        switch (kind) {

        case Kind::boolean:
            write_bool(sink, key, bytes[0] != std::byte { 0 });
            return;

        case Kind::unsigned_integer: {
            uint32_t value = 0; // Little endian
            memcpy(&value, bytes.data(), bytes.size());
            write_unsigned(sink, key, value, bytes.size());
            return;
        }

        case Kind::signed_integer: {
            uint32_t raw = 0;
            memcpy(&raw, bytes.data(), bytes.size());
            const unsigned shift = 32 - 8 * bytes.size();
            write_signed(sink, key, static_cast<int32_t>(raw << shift) >> shift, bytes.size());
            return;
        }

        case Kind::floating_point: {
            float value;
            memcpy(&value, bytes.data(), sizeof(value));
            write_float(sink, key, value);
            return;
        }

        case Kind::string:
            write_string(sink, key, { reinterpret_cast<const char *>(bytes.data()), bytes.size() });
            return;

        case Kind::raw:
            write_raw(sink, key, bytes, comment ? comment : "");
            return;
        }
    }

    using Comment = std::array<char, 64>;

    [[gnu::noinline]] void format_comment(Comment &comment, const ToolOffset &value) {
        snprintf(comment.data(), comment.size(), "x=%g y=%g z=%g", static_cast<double>(value.x), static_cast<double>(value.y), static_cast<double>(value.z));
    }

    [[gnu::noinline]] void format_comment(Comment &comment, const DockPosition &value) {
        snprintf(comment.data(), comment.size(), "x=%g y=%g", static_cast<double>(value.x), static_cast<double>(value.y));
    }

#if HAS_SHEET_PROFILES()
    [[gnu::noinline]] void format_comment(Comment &comment, const Sheet &value) {
        snprintf(comment.data(), comment.size(), "name=%.*s z=%g", static_cast<int>(std::size(value.name)), value.name, static_cast<double>(value.z_offset));
    }
#endif

    struct EntryWriter {
        LineSink &sink;

        /// Id of the first element for items stored as an array under one name
        std::optional<uint16_t> array_first_id;

        template <typename T>
        void operator()(uint16_t id, journal::ItemFlags, const T &value) const {
            const auto bytes = std::as_bytes(std::span(&value, 1));
            if constexpr (requires(Comment &c) { format_comment(c, value); }) {
                Comment comment;
                format_comment(comment, value);
                write_entry(sink, array_first_id, id, kind_of<T>(), bytes, comment.data());
            } else {
                write_entry(sink, array_first_id, id, kind_of<T>(), bytes, nullptr);
            }
        }
    };

    template <typename Item>
    constexpr std::optional<uint16_t> array_first_id() {
        if constexpr (requires { Item::hashed_id_first; }) {
            return Item::hashed_id_first;
        } else {
            return std::nullopt;
        }
    }

    template <typename Item>
    void export_item(LineSink &sink, Item &item) {
        item.for_each_entry(EntryWriter { .sink = sink, .array_first_id = array_first_id<Item>() });
    }

    class FileSink final : public LineSink {
    public:
        explicit FileSink(FILE *file)
            : file_(file) {}

        void write_line(std::string_view line) override {
            if (!ok_) {
                return;
            }
            if (fwrite(line.data(), 1, line.size(), file_) != line.size() || fputc('\n', file_) == EOF) {
                ok_ = false;
                return;
            }
            if (counting_) {
                crc_ = crc32_sw(reinterpret_cast<const uint8_t *>(line.data()), line.size(), crc_);
                crc_ = crc32_sw(reinterpret_cast<const uint8_t *>("\n"), 1, crc_);
                line_count_++;
            }
        }

        [[gnu::format(printf, 2, 3)]] void write_printf(const char *format, ...) {
            std::array<char, max_line_length + 1> buffer;
            va_list args;
            va_start(args, format);
            const int written = vsnprintf(buffer.data(), buffer.size(), format, args);
            va_end(args);
            write_line(std::string_view(buffer.data(), std::clamp<int>(written, 0, max_line_length)));
        }

        /// Lines written while counting are covered by the line count and the CRC
        void set_counting(bool set) {
            counting_ = set;
        }

        bool ok() const {
            return ok_;
        }

        uint32_t crc() const {
            return crc_;
        }

        uint32_t line_count() const {
            return line_count_;
        }

    private:
        FILE *file_;
        bool ok_ = true;
        bool counting_ = false;
        uint32_t crc_ = 0;
        uint32_t line_count_ = 0;
    };

} // namespace

bool export_to_file(const char *path) {
    ScopeGuard delete_file_guard = [&] { unlink(path); };

    unique_file_ptr file(fopen(path, "w"));
    if (!file) {
        return false;
    }

    FileSink sink(file.get());

    sink.write_line("# Prusa settings backup (export only)");
    sink.write_line("# Item key: <journal id, hex>[+<byte offset>] <name>; the id is authoritative, the name informational.");
    sink.write_line("# Value types: bool, u8/u16/u32, i8/i16/i32, f32, str, hex (raw bytes), redacted (secret, not included).");
    sink.write_printf("# Raw values longer than %u bytes continue on lines keyed <id>+<byte offset>.", static_cast<unsigned>(hex_bytes_per_line));
    sink.write_line("# [end]: items = number of lines in [items], crc32 = CRC-32 (zlib) of those lines, each with its newline.");
    sink.write_line("[backup]");
    sink.write_line("format = 1");
    sink.write_printf("printer = %s", PrinterModelInfo::current().id_str);
    sink.write_printf("firmware = %s", version::project_version_full);
    sink.write_printf("config_version = %u", static_cast<unsigned>(config_store().config_version.get()));

    serial_nr_t serial_nr {};
    if (otp_get_serial_nr(serial_nr) > 0) {
        serial_nr.back() = '\0';
        sink.write_printf("serial = %s", serial_nr.data());
    } else {
        sink.write_line("serial = unknown");
    }

    sink.write_line("[items]");
    sink.set_counting(true);
    config_store_journal().visit_items([&sink](auto &item) {
        export_item(sink, item);
    });
    sink.set_counting(false);

    sink.write_line("[end]");
    sink.write_printf("items = %" PRIu32, sink.line_count());
    sink.write_printf("crc32 = %08" PRIX32, sink.crc());

    if (!sink.ok() || fclose(file.release()) != 0) {
        return false;
    }

    delete_file_guard.disarm();
    return true;
}

bool export_to_usb(FileName &file_name) {
    static constexpr const char *directory = "/usb/";
    std::array<char, 6 + std::tuple_size_v<FileName>> path;

    for (unsigned n = 1; n < 1000; n++) {
        snprintf(file_name.data(), file_name.size(), "settings_backup_%u.ini", n);
        snprintf(path.data(), path.size(), "%s%s", directory, file_name.data());
        if (access(path.data(), F_OK) != 0) {
            return export_to_file(path.data());
        }
    }

    file_name[0] = '\0';
    return false;
}

} // namespace config_store_export
