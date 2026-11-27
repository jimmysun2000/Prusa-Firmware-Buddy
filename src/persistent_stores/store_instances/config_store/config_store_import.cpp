#include "config_store_import.hpp"
#include "config_store_backup_items.hpp"
#include "config_store_export_format.hpp"
#include "config_store_import_format.hpp"
#include "store_instance.hpp"
#include "store_journal.hpp"

#include <common/otp.hpp>
#include <common/printer_model.hpp>
#include <ini.h>
#include <unique_file_ptr.hpp>
#include <utils/algorithm_extensions.hpp>
#include <version/version.hpp>

#include <option/has_e2ee_support.h>
#include <option/has_phase_stepping.h>
#include <option/has_toolchanger.h>
#if HAS_TOOLCHANGER()
    #include <module/prusa/toolchanger.h>
#endif

#include <algorithm>
#include <bitset>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace config_store_import {

namespace {

    using config_store_backup::Kind;
    using config_store_ns::CurrentStore;
    using config_store_ns::ItemFlag;

    /// The special items, and the factory reset groups that are never restored
    constexpr journal::ItemFlags never_restored = []() {
        journal::ItemFlags flags = ItemFlag::special | ItemFlag::printer_state;
#if HAS_E2EE_SUPPORT()
        flags |= ItemFlag::security;
#endif
        return flags;
    }();

    /// Items never restored whatever their flags.
    /// Another base printer model forces a factory reset at boot; the phase stepping result belongs to calibration files that are not in the backup.
    constexpr std::array never_restored_ids {
        decltype(CurrentStore::last_boot_base_printer_model)::hashed_id,
#if HAS_PHASE_STEPPING()
            decltype(CurrentStore::selftest_result_phase_stepping)::hashed_id,
#endif
    };

    static_assert(
        []() consteval {
            std::array<journal::ItemFlags, 256> sets {};
            size_t count = 0;
            ConfigStore store;
            visit_all_struct_fields(static_cast<CurrentStore &>(store), [&]<typename Item>(Item &) {
                if (std::find(sets.begin(), sets.begin() + count, Item::flags) == sets.begin() + count) {
                    sets[count++] = Item::flags;
                }
            });
            return count <= max_flag_sets;
        }(),
        "Summary cannot count all distinct item flags; increase max_flag_sets");

    static_assert(journal::Backend::MAX_ITEM_SIZE <= max_value_size);

    /// What the restore needs to know about the stored type of an item
    struct ItemInfo {
        Kind kind;
        uint16_t size;
        std::optional<uint16_t> array_first_id;
        bool ram_only;
    };

    /// Arrays have no RAM-only variant
    template <typename Item>
    constexpr bool is_ram_only() {
        if constexpr (requires { Item::is_ram_only; }) {
            return Item::is_ram_only;
        } else {
            return false;
        }
    }

    template <typename Item, typename T>
    constexpr ItemInfo item_info {
        .kind = config_store_backup::kind_of<T>(),
        .size = sizeof(T),
        .array_first_id = config_store_backup::array_first_id<Item>(),
        .ram_only = is_ram_only<Item>(),
    };

    /// One entry of [items], from its first line
    struct Entry {
        uint16_t id;
        std::string_view key;

        /// Kind of the tag; empty for "redacted:"
        std::optional<Kind> kind;

        /// The whole value, joined from all its lines
        std::span<const std::byte> bytes;

        /// Value text of "f32:"
        std::string_view text;

        uint32_t line;
    };

#if HAS_TOOLCHANGER()
    /// The limits the tool offset calibration enforces
    bool is_valid_tool_offset(const ToolOffset &offset) {
        return offset.x >= X_MIN_OFFSET && offset.x <= X_MAX_OFFSET
            && offset.y >= Y_MIN_OFFSET && offset.y <= Y_MAX_OFFSET
            && offset.z >= Z_MIN_OFFSET && offset.z <= Z_MAX_OFFSET;
    }

    /// The check of the dock calibration, or the default of an uncalibrated dock
    bool is_valid_dock_position(uint16_t id, const DockPosition &position) {
        const auto index = stdext::index_of_opt(CurrentStore::dock_position_hashes, id);
        if (!index.has_value()) {
            return false;
        }
        const auto tool = static_cast<uint8_t>(*index);
        if (position == decltype(CurrentStore::dock_positions)::get_default_val(tool)) {
            return true;
        }
        return prusa_toolchanger.is_tool_info_valid(PhysicalToolIndex::from_raw(tool), PrusaToolInfo { .dock_x = position.x, .dock_y = position.y });
    }
#endif

    enum class Mode : uint8_t {
        scan,
        write,
    };

    /// Sections in the order the export writes them
    enum class Section : uint8_t {
        none,
        backup,
        items,
        end,
        invalid,
    };

    Section section_of(std::string_view name) {
        if (name == "backup") {
            return Section::backup;
        } else if (name == "items") {
            return Section::items;
        } else if (name == "end") {
            return Section::end;
        } else {
            return Section::invalid;
        }
    }

    /// Text of a value in [end], kept until the whole file is read
    struct EndValue {
        bool present = false;

        /// Anything longer than the buffer cannot be what the export writes
        bool fits = false;

        std::array<char, 16> text {};

        void set(std::string_view value) {
            present = true;
            fits = value.size() < text.size();
            if (fits) {
                std::ranges::copy(value, text.begin());
                text[value.size()] = '\0';
            }
        }

        bool equals(const char *expected) const {
            return present && fits && strcmp(text.data(), expected) == 0;
        }
    };

    /// One pass through the file
    class Reading {
    public:
        Reading(FILE *file, Summary &summary, Mode mode, journal::ItemFlags skip_flags)
            : file_(file)
            , summary_(summary)
            , mode_(mode)
            , skip_flags_(skip_flags | never_restored) {}

        /// ini_reader: reads a line and checks it as read
        static char *read_line(char *buffer, int size, void *self);

        /// ini_handler
        static int handle_line(void *self, const char *section, const char *name, const char *value);

        /// Checks the end of the file and the checksum; @p ini_result is what inih returned
        void finish(int ini_result);

        bool written() const {
            return written_;
        }

        const Entry &entry() const {
            return *entry_;
        }

        /// Type-independent part of visiting the item of the current entry: checks the key and the value,
        /// and fills @p value with the entry's bytes.
        /// @returns whether the entry may be restored; otherwise it is counted or the error is recorded
        [[gnu::noinline]] bool check(const ItemInfo &info, journal::ItemFlags flags, std::span<std::byte> value);

        /// Whether the entry's @p value differs from the @p current one. Both the typed comparison and the bytes must differ,
        /// for strings the text, for floats the text as the export writes it.
        [[gnu::noinline]] bool differs(Kind kind, bool typed_differs, std::span<const std::byte> current, std::span<const std::byte> value) const;

        /// Counts a differing value of an item with @p flags, or, when writing, decides whether to write it
        [[gnu::noinline]] bool take(journal::ItemFlags flags);

        [[gnu::noinline]] void fail_entry(Error error);

    private:
        void fail(Error error, uint32_t line);
        void fail(Error error) {
            fail(error, line_);
        }

        void handle(std::string_view section, std::string_view name, std::string_view value);
        bool enter(Section section);
        void handle_header(std::string_view name, std::string_view value);
        void handle_item(std::string_view name, std::string_view value);
        void handle_end(std::string_view name, std::string_view value);

        /// Processes the joined "hex:" entry, if any
        void finish_entry();

        void process(const Entry &entry);

        enum HeaderKey : uint8_t {
            header_format = 1 << 0,
            header_printer = 1 << 1,
            header_firmware = 1 << 2,
            header_config_version = 1 << 3,
            header_serial = 1 << 4,
            header_all = (1 << 5) - 1,
        };

        FILE *file_;
        Summary &summary_;
        const Mode mode_;
        const journal::ItemFlags skip_flags_;

        LineChecker checker_;
        uint32_t line_ = 0;

        /// A line failed the line checker; reading stopped there
        bool stopped_ = false;

        bool written_ = false;
        Section section_ = Section::none;
        uint8_t header_seen_ = 0;
        EndValue end_items_;
        EndValue end_crc_;

        ItemAssembler assembler_;
        uint32_t assembler_line_ = 0;

        /// Entries seen so far, by config_store_backup::entry_position()
        std::bitset<config_store_backup::max_entry_count> seen_;

        const Entry *entry_ = nullptr;
    };

    template <typename Item>
    struct EntryVisitor {
        Reading &reading;

        template <typename T, typename Set>
        void operator()(journal::ItemFlags flags, const T &current, Set &&set) const {
            T value;
            if (!reading.check(item_info<Item, T>, flags, std::as_writable_bytes(std::span(&value, 1)))) {
                return;
            }

#if HAS_TOOLCHANGER()
            if constexpr (std::is_same_v<Item, decltype(CurrentStore::tool_offsets)>) {
                if (!is_valid_tool_offset(value)) {
                    reading.fail_entry(Error::tool_offset);
                    return;
                }
            } else if constexpr (std::is_same_v<Item, decltype(CurrentStore::dock_positions)>) {
                if (!is_valid_dock_position(reading.entry().id, value)) {
                    reading.fail_entry(Error::dock_position);
                    return;
                }
            }
#endif

            const bool typed_differs = !(value == current);
            if (reading.differs(item_info<Item, T>.kind, typed_differs, std::as_bytes(std::span(&current, 1)), std::as_bytes(std::span(&value, 1))) && reading.take(flags)) {
                set(value);
            }
        }
    };

    char *Reading::read_line(char *buffer, int size, void *self) {
        auto &reading = *static_cast<Reading *>(self);
        // When writing, stop at the first error; when scanning, read on for the checksum, which takes precedence
        if (reading.stopped_ || (reading.mode_ == Mode::write && reading.summary_.error != Error::none)) {
            return nullptr;
        }
        if (!fgets(buffer, size, reading.file_)) {
            return nullptr;
        }
        reading.line_++;
        if (!reading.checker_.add(std::string_view(buffer, strlen(buffer)))) {
            reading.fail(Error::format);
            reading.stopped_ = true;
            return nullptr;
        }
        return buffer;
    }

    int Reading::handle_line(void *self, const char *section, const char *name, const char *value) {
        static_cast<Reading *>(self)->handle(section, name, value);
        // Errors are recorded in the summary
        return 1;
    }

    void Reading::fail(Error error, uint32_t line) {
        if (summary_.error == Error::none) {
            summary_.error = error;
            summary_.line = line;
        }
    }

    void Reading::fail_entry(Error error) {
        if (summary_.error == Error::none) {
            summary_.error = error;
            summary_.line = entry_->line;
            summary_.id = entry_->id;
        }
    }

    void Reading::handle(std::string_view section, std::string_view name, std::string_view value) {
        const Section line_section = section_of(section);
        if (line_section != section_ && !enter(line_section)) {
            fail(Error::format);
            return;
        }

        switch (section_) {

        case Section::backup:
            handle_header(name, value);
            break;

        case Section::items:
            handle_item(name, value);
            break;

        case Section::end:
            handle_end(name, value);
            break;

        case Section::none:
        case Section::invalid:
            break;
        }
    }

    bool Reading::enter(Section section) {
        if (std::to_underlying(section) != std::to_underlying(section_) + 1 || section == Section::invalid) {
            return false;
        }
        if (section_ == Section::backup && header_seen_ != header_all) {
            fail(Error::format);
        }
        if (section_ == Section::items) {
            finish_entry();
        }
        section_ = section;
        return true;
    }

    void Reading::handle_header(std::string_view name, std::string_view value) {
        if (summary_.error != Error::none) {
            return;
        }

        const auto first = [&](HeaderKey key) {
            if (header_seen_ & key) {
                fail(Error::format);
                return false;
            }
            header_seen_ |= key;
            return true;
        };

        if (name == "format") {
            if (first(header_format) && value != "1") {
                fail(Error::format_version);
            }

        } else if (name == "printer") {
            if (first(header_printer) && value != PrinterModelInfo::current().id_str) {
                fail(Error::printer_model);
            }

        } else if (name == "firmware") {
            if (first(header_firmware)) {
                const auto length = std::min(value.size(), summary_.firmware.size() - 1);
                std::ranges::copy(value.substr(0, length), summary_.firmware.begin());
                summary_.firmware[length] = '\0';
                summary_.firmware_differs = value != version::project_version_full;
            }

        } else if (name == "config_version") {
            std::array<char, 8> expected;
            snprintf(expected.data(), expected.size(), "%u", static_cast<unsigned>(CurrentStore::newest_config_version));
            if (first(header_config_version) && value != expected.data()) {
                fail(Error::config_version);
            }

        } else if (name == "serial") {
            if (first(header_serial)) {
                serial_nr_t serial_nr {};
                const bool known = otp_get_serial_nr(serial_nr) > 0;
                serial_nr.back() = '\0';
                if (!known || value != serial_nr.data()) {
                    fail(Error::serial_number);
                }
            }

        } else {
            fail(Error::format);
        }
    }

    void Reading::handle_item(std::string_view name, std::string_view value) {
        if (summary_.error != Error::none) {
            return;
        }

        const auto key = parse_key(name);
        const auto parsed = parse_value(value);
        if (!key.has_value() || !parsed.has_value()) {
            fail(Error::format);
            return;
        }
        const auto chunk = std::span(parsed->bytes).first(parsed->size);

        if (key->byte_offset > 0) {
            if (parsed->kind != Kind::raw || !assembler_.append(*key, chunk)) {
                fail(Error::format);
            }
            return;
        }

        finish_entry();
        if (summary_.error != Error::none) {
            return;
        }

        if (parsed->kind == Kind::raw) {
            // Wait for its continuation lines
            if (!assembler_.start(name, *key, chunk)) {
                fail(Error::format);
            }
            assembler_line_ = line_;
            return;
        }

        process({ .id = key->id, .key = name, .kind = parsed->kind, .bytes = chunk, .text = parsed->text, .line = line_ });
    }

    void Reading::handle_end(std::string_view name, std::string_view value) {
        EndValue *end_value = nullptr;
        if (name == "items") {
            end_value = &end_items_;
        } else if (name == "crc32") {
            end_value = &end_crc_;
        }
        if (!end_value || end_value->present) {
            fail(Error::format);
            return;
        }
        end_value->set(value);
    }

    void Reading::finish_entry() {
        if (!assembler_.has_entry()) {
            return;
        }
        process({ .id = assembler_.id(), .key = assembler_.key(), .kind = Kind::raw, .bytes = assembler_.bytes(), .text = {}, .line = assembler_line_ });
        assembler_.clear();
    }

    void Reading::process(const Entry &entry) {
        if (summary_.error != Error::none) {
            return;
        }

        const auto position = config_store_backup::entry_position(entry.id);
        if (!position.has_value()) {
            summary_.unknown_count++;
            return;
        }

        entry_ = &entry;
        if (seen_.test(*position)) {
            fail_entry(Error::duplicate_entry);
        } else {
            seen_.set(*position);
            bool found = false;
            config_store_journal().visit_items([&]<typename Item>(Item &item) {
                if (!found) {
                    found = item.visit_entry(entry.id, EntryVisitor<Item> { *this });
                }
            });
        }
        entry_ = nullptr;
    }

    bool Reading::check(const ItemInfo &info, journal::ItemFlags flags, std::span<std::byte> value) {
        const Entry &entry = *entry_;

        // An id reused by another item may mean another layout
        config_store_export::KeyText expected_text;
        std::string_view expected = config_store_export::format_key(expected_text, config_store_backup::item_key(entry.id, info.array_first_id));
        while (expected.ends_with(' ')) {
            // inih strips trailing whitespace from the key
            expected.remove_suffix(1);
        }
        if (expected != entry.key) {
            summary_.unknown_count++;
            return false;
        }

        if (!entry.kind.has_value()) {
            // "redacted:" in the file
            summary_.not_restorable_count++;
            return false;
        }

        if (!fits_item(*entry.kind, entry.bytes, info.kind, info.size)) {
            fail_entry(Error::invalid_value);
            return false;
        }

        if ((flags & never_restored) != 0 || flags == 0 || info.ram_only || std::ranges::find(never_restored_ids, entry.id) != never_restored_ids.end()
            || config_store_backup::is_redacted(entry.id, info.kind)) {
            summary_.not_restorable_count++;
            return false;
        }

        // A string is its text and zeros
        std::ranges::fill(value, std::byte { 0 });
        std::ranges::copy(entry.bytes, value.begin());
        return true;
    }

    bool Reading::differs(Kind kind, bool typed_differs, std::span<const std::byte> current, std::span<const std::byte> value) const {
        if (!typed_differs || std::ranges::equal(current, value)) {
            return false;
        }

        switch (kind) {

        case Kind::string: {
            // The stored array may hold anything after its terminating zero
            const auto text = [](std::span<const std::byte> bytes) {
                const auto chars = reinterpret_cast<const char *>(bytes.data());
                return std::string_view(chars, strnlen(chars, bytes.size()));
            };
            return text(current) != text(value);
        }

        case Kind::floating_point: {
            // The backup has the float in the precision of its text only
            float current_value;
            memcpy(&current_value, current.data(), sizeof(current_value));
            config_store_export::FloatText current_text;
            return config_store_export::format_float(current_text, current_value) != entry_->text;
        }

        default:
            return true;
        }
    }

    bool Reading::take(journal::ItemFlags flags) {
        if (mode_ == Mode::write) {
            if ((flags & skip_flags_) != 0) {
                return false;
            }
            written_ = true;
            return true;
        }

        auto &sets = summary_.differing;
        const auto end = sets.begin() + summary_.differing_flag_sets;
        const auto it = std::ranges::find(sets.begin(), end, flags, &Summary::FlagSetCount::flags);
        if (it != end) {
            it->count++;
        } else {
            // The number of distinct flags is checked at compile time
            *it = { .flags = flags, .count = 1 };
            summary_.differing_flag_sets++;
        }
        return false;
    }

    void Reading::finish(int ini_result) {
        if (section_ == Section::items) {
            finish_entry();
        }
        if (ferror(file_)) {
            summary_.error = Error::read_failed;
            summary_.line = line_;
            summary_.id = 0;
            return;
        }
        if (stopped_) {
            return;
        }
        if (ini_result != 0) {
            // A line inih cannot read
            fail(Error::format, static_cast<uint32_t>(ini_result));
        }
        if (mode_ == Mode::write && summary_.error != Error::none) {
            // Reading stopped at the error
            return;
        }
        if (section_ != Section::end || !end_items_.present || !end_crc_.present) {
            fail(Error::incomplete);
            return;
        }

        summary_.crc = checker_.item_crc();
        std::array<char, 16> expected_items;
        snprintf(expected_items.data(), expected_items.size(), "%" PRIu32, checker_.item_line_count());
        std::array<char, 16> expected_crc;
        snprintf(expected_crc.data(), expected_crc.size(), "%08" PRIX32, checker_.item_crc());
        if (!end_items_.equals(expected_items.data()) || !end_crc_.equals(expected_crc.data())) {
            // A damaged file explains any other error
            summary_.error = Error::checksum;
            summary_.line = 0;
            summary_.id = 0;
        }
    }

    /// @returns whether anything was written
    bool read_file(const char *path, Summary &summary, Mode mode, journal::ItemFlags skip_flags) {
        unique_file_ptr file(fopen(path, "r"));
        if (!file) {
            summary.error = Error::file_not_found;
            return false;
        }
        Reading reading(file.get(), summary, mode, skip_flags);
        reading.finish(ini_parse_stream(&Reading::read_line, &reading, &Reading::handle_line, &reading));
        return reading.written();
    }

} // namespace

uint16_t Summary::changes(journal::ItemFlags skip_flags) const {
    uint16_t count = 0;
    for (size_t i = 0; i < differing_flag_sets; i++) {
        if ((differing[i].flags & (skip_flags | never_restored)) == 0) {
            count += differing[i].count;
        }
    }
    return count;
}

uint16_t Summary::differing_with_any(journal::ItemFlags flags) const {
    uint16_t count = 0;
    for (size_t i = 0; i < differing_flag_sets; i++) {
        if ((differing[i].flags & flags) != 0) {
            count += differing[i].count;
        }
    }
    return count;
}

journal::ItemFlags never_restored_flags() {
    return never_restored;
}

Summary scan(const char *path) {
    Summary summary;
    read_file(path, summary, Mode::scan, 0);
    return summary;
}

ApplyResult apply(const char *path, journal::ItemFlags skip_flags, uint32_t expected_crc, bool (*may_write)()) {
    {
        const Summary check = scan(path);
        if (check.error != Error::none) {
            return { .error = check.error, .written = false };
        }
        if (check.crc != expected_crc) {
            return { .error = Error::changed, .written = false };
        }
    }

    if (!may_write()) {
        return { .error = Error::busy, .written = false };
    }

    Summary summary;
    bool written = false;
    {
        auto transaction = config_store().get_backend().transaction_guard();
        written = read_file(path, summary, Mode::write, skip_flags);
    }
    if (summary.error == Error::none && summary.crc != expected_crc) {
        summary.error = Error::changed;
    }
    return { .error = summary.error, .written = written };
}

} // namespace config_store_import
