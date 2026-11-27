/// @file
/// Reading the lines of the settings backup, without any access to the store or the file system.
/// Everything here refuses what the export does not write.
#pragma once

#include "config_store_backup_items.hpp"
#include "config_store_export_format.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace config_store_import {

using config_store_backup::Kind;

/// Largest value of one entry, in bytes
inline constexpr size_t max_value_size = 512;

/// Largest value of one line, in bytes: the text of "str:" or one chunk of "hex:"
inline constexpr size_t max_line_value_size = 2 * config_store_export::hex_bytes_per_line;

/// Key of a line in [items] as the INI reader passes it: "<id>[+<byte offset>][ <name>]"
struct ParsedKey {
    uint16_t id;

    /// Byte offset of a continuation line, 0 on the first line of an entry
    uint16_t byte_offset;

    /// Everything after the id and the offset: empty, or a space and the name
    std::string_view rest;
};

/// Parses a key. The id must be 4 uppercase hex digits, the offset a positive decimal number without leading zeros.
std::optional<ParsedKey> parse_key(std::string_view key);

/// Value of a line in [items]
struct ParsedValue {
    /// Kind of the tag: "str:" is Kind::string, "hex:" Kind::raw. Empty for "redacted:".
    std::optional<Kind> kind;

    /// Number of bytes: the size of the type for "bool:" and the numbers, the length of the text or of the chunk otherwise
    size_t size = 0;

    /// The value; numbers in little endian
    std::array<std::byte, max_line_value_size> bytes {};

    /// The value as written after the tag, for comparing floats in the precision they were written with.
    /// Refers to the text passed to parse_value().
    std::string_view text;
};

/// Parses "<tag>:<value>".
/// Refuses unknown tags, integers out of the range of their type or with a sign, leading zeros or other characters,
/// floats that are not finite, hex that is not uppercase pairs or longer than a line holds,
/// text with characters outside [A-Za-z0-9._-] or longer than a line holds, and "redacted:" with a value.
std::optional<ParsedValue> parse_value(std::string_view value);

/// Whether a value read with the tag of @p kind can be a value of a stored type of @p item_kind and @p item_size:
/// bools and numbers need their own tag of the same size, raw values "hex:" of exactly the size,
/// strings "str:" or "hex:" of at most @p item_size - 1 characters without a zero.
bool fits_item(Kind kind, std::span<const std::byte> bytes, Kind item_kind, size_t item_size);

/// Joins the "hex:" lines of one entry: the first one at byte offset 0,
/// each continuation line keyed "<id>+<offset>" with the same name, where the previous full chunk ended.
class ItemAssembler {
public:
    /// Starts an entry with its first line, dropping the previous entry.
    /// @param key key of the line as the INI reader passes it
    /// @returns false if the key is longer than any key the export writes or the chunk does not fit
    bool start(std::string_view key, const ParsedKey &parsed_key, std::span<const std::byte> chunk);

    /// Adds a continuation line.
    /// @returns false if it does not continue the current entry: no entry, other id or name, previous chunk not full,
    /// byte offset other than where the entry ended, or the value would get too long
    bool append(const ParsedKey &key, std::span<const std::byte> chunk);

    /// Drops the current entry
    void clear() {
        has_entry_ = false;
    }

    bool has_entry() const {
        return has_entry_;
    }

    uint16_t id() const {
        return id_;
    }

    /// Key of the first line
    std::string_view key() const {
        return { key_.data(), key_length_ };
    }

    std::span<const std::byte> bytes() const {
        return std::span(bytes_).first(size_);
    }

private:
    bool has_entry_ = false;
    uint16_t id_ = 0;
    size_t key_length_ = 0;
    size_t size_ = 0;
    size_t last_chunk_size_ = 0;
    config_store_export::KeyText key_ {};
    std::array<std::byte, max_value_size> bytes_ {};
};

/// Checks the lines of the file as read, and computes the number and the CRC of the lines in [items] as written in [end]:
/// the lines strictly between the lines "[items]" and "[end]", each with its line terminator.
class LineChecker {
public:
    /// @param line one line as read from the file, with its "\n" (missing only at the end of the file)
    /// @returns false if the export cannot have written the line: longer than its longest line,
    /// starting with whitespace, or following a line without "\n"
    bool add(std::string_view line);

    uint32_t item_line_count() const {
        return item_line_count_;
    }

    uint32_t item_crc() const {
        return item_crc_;
    }

private:
    enum class Region : uint8_t {
        before_items,
        items,
        after_items,
    };

    Region region_ = Region::before_items;
    bool unterminated_ = false;
    uint32_t item_line_count_ = 0;
    uint32_t item_crc_ = 0;
};

} // namespace config_store_import
