#include "config_store_import_format.hpp"

#include <common/crc32.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace config_store_import {

namespace {

    using config_store_export::hex_bytes_per_line;
    using config_store_export::max_key_length;
    using config_store_export::max_line_length;

    constexpr bool is_digit(char c) {
        return c >= '0' && c <= '9';
    }

    constexpr std::optional<uint8_t> hex_digit_value(char c) {
        if (is_digit(c)) {
            return c - '0';
        } else if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        } else {
            return std::nullopt;
        }
    }

    constexpr bool is_plain_string_char(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || is_digit(c) || c == '.' || c == '_' || c == '-';
    }

    /// Decimal number without sign and leading zeros, at most @p max
    std::optional<uint32_t> parse_decimal(std::string_view text, uint32_t max) {
        if (text.empty() || text.size() > 10 || (text.size() > 1 && text[0] == '0') || !std::ranges::all_of(text, is_digit)) {
            return std::nullopt;
        }
        uint64_t value = 0;
        for (const char c : text) {
            value = value * 10 + static_cast<uint64_t>(c - '0');
        }
        if (value > max) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(value);
    }

    void store_little_endian(ParsedValue &result, uint32_t value, size_t size) {
        for (size_t i = 0; i < size; i++) {
            result.bytes[i] = static_cast<std::byte>(value >> (8 * i));
        }
        result.size = size;
    }

    /// Width tag "8", "16" or "32", in bytes
    std::optional<size_t> integer_size(std::string_view width) {
        if (width == "8") {
            return 1;
        } else if (width == "16") {
            return 2;
        } else if (width == "32") {
            return 4;
        } else {
            return std::nullopt;
        }
    }

    bool parse_unsigned(ParsedValue &result, size_t size, std::string_view text) {
        const uint32_t max = static_cast<uint32_t>((uint64_t { 1 } << (8 * size)) - 1);
        const auto value = parse_decimal(text, max);
        if (!value.has_value()) {
            return false;
        }
        store_little_endian(result, *value, size);
        return true;
    }

    bool parse_signed(ParsedValue &result, size_t size, std::string_view text) {
        const bool negative = text.starts_with('-');
        const uint32_t limit = uint32_t { 1 } << (8 * size - 1);
        const auto magnitude = parse_decimal(negative ? text.substr(1) : text, negative ? limit : limit - 1);
        if (!magnitude.has_value() || (negative && *magnitude == 0)) {
            return false;
        }
        store_little_endian(result, negative ? (~*magnitude + 1) : *magnitude, size);
        return true;
    }

    bool parse_float(ParsedValue &result, std::string_view text) {
        // Digits, sign, point and exponent only: refuses "nan", "inf", hex floats and whitespace
        const auto is_float_char = [](char c) { return is_digit(c) || c == '-' || c == '+' || c == '.' || c == 'e'; };
        config_store_export::FloatText buffer;
        if (text.empty() || text.size() >= buffer.size() || !std::ranges::all_of(text, is_float_char)) {
            return false;
        }
        std::ranges::copy(text, buffer.begin());
        buffer[text.size()] = '\0';

        char *end = nullptr;
        const float value = strtof(buffer.data(), &end);
        if (end != buffer.data() + text.size() || !std::isfinite(value)) {
            return false;
        }
        memcpy(result.bytes.data(), &value, sizeof(value));
        result.size = sizeof(value);
        return true;
    }

    bool parse_hex(ParsedValue &result, std::string_view text) {
        if (text.size() % 2 != 0 || text.size() / 2 > hex_bytes_per_line) {
            return false;
        }
        for (size_t i = 0; i < text.size() / 2; i++) {
            const auto high = hex_digit_value(text[2 * i]);
            const auto low = hex_digit_value(text[2 * i + 1]);
            if (!high.has_value() || !low.has_value()) {
                return false;
            }
            result.bytes[i] = static_cast<std::byte>((*high << 4) | *low);
        }
        result.size = text.size() / 2;
        return true;
    }

    bool parse_string(ParsedValue &result, std::string_view text) {
        if (text.size() > max_line_value_size || !std::ranges::all_of(text, is_plain_string_char)) {
            return false;
        }
        std::ranges::copy(std::as_bytes(std::span(text)), result.bytes.begin());
        result.size = text.size();
        return true;
    }

} // namespace

std::optional<ParsedKey> parse_key(std::string_view key) {
    if (key.size() < 4) {
        return std::nullopt;
    }
    uint16_t id = 0;
    for (const char c : key.substr(0, 4)) {
        const auto digit = hex_digit_value(c);
        if (!digit.has_value()) {
            return std::nullopt;
        }
        id = static_cast<uint16_t>((id << 4) | *digit);
    }

    std::string_view rest = key.substr(4);
    uint16_t byte_offset = 0;
    if (rest.starts_with('+')) {
        const auto offset_end = std::min(rest.find(' '), rest.size());
        const auto offset = parse_decimal(rest.substr(1, offset_end - 1), UINT16_MAX);
        if (!offset.has_value() || *offset == 0) {
            return std::nullopt;
        }
        byte_offset = static_cast<uint16_t>(*offset);
        rest = rest.substr(offset_end);
    }

    if (!rest.empty() && rest[0] != ' ') {
        return std::nullopt;
    }
    return ParsedKey { .id = id, .byte_offset = byte_offset, .rest = rest };
}

std::optional<ParsedValue> parse_value(std::string_view value) {
    const auto colon = value.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view tag = value.substr(0, colon);
    const std::string_view text = value.substr(colon + 1);

    ParsedValue result;
    result.text = text;

    bool ok = false;
    if (tag == "bool") {
        result.kind = Kind::boolean;
        ok = (text == "0" || text == "1");
        store_little_endian(result, text == "1", 1);
    } else if (tag.starts_with('u') && integer_size(tag.substr(1))) {
        result.kind = Kind::unsigned_integer;
        ok = parse_unsigned(result, *integer_size(tag.substr(1)), text);
    } else if (tag.starts_with('i') && integer_size(tag.substr(1))) {
        result.kind = Kind::signed_integer;
        ok = parse_signed(result, *integer_size(tag.substr(1)), text);
    } else if (tag == "f32") {
        result.kind = Kind::floating_point;
        ok = parse_float(result, text);
    } else if (tag == "str") {
        result.kind = Kind::string;
        ok = parse_string(result, text);
    } else if (tag == "hex") {
        result.kind = Kind::raw;
        ok = parse_hex(result, text);
    } else if (tag == "redacted") {
        result.kind = std::nullopt;
        ok = text.empty();
    }

    if (!ok) {
        return std::nullopt;
    }
    return result;
}

bool fits_item(Kind kind, std::span<const std::byte> bytes, Kind item_kind, size_t item_size) {
    switch (item_kind) {

    case Kind::boolean:
    case Kind::unsigned_integer:
    case Kind::signed_integer:
    case Kind::floating_point:
    case Kind::raw:
        return kind == item_kind && bytes.size() == item_size;

    case Kind::string:
        return (kind == Kind::string || kind == Kind::raw) && bytes.size() < item_size && std::ranges::find(bytes, std::byte { 0 }) == bytes.end();
    }
    return false;
}

bool ItemAssembler::start(std::string_view key, const ParsedKey &parsed_key, std::span<const std::byte> chunk) {
    has_entry_ = false;
    if (parsed_key.byte_offset != 0 || key.size() > max_key_length || chunk.size() > max_value_size) {
        return false;
    }
    std::ranges::copy(key, key_.begin());
    key_length_ = key.size();
    id_ = parsed_key.id;
    std::ranges::copy(chunk, bytes_.begin());
    size_ = chunk.size();
    last_chunk_size_ = chunk.size();
    has_entry_ = true;
    return true;
}

bool ItemAssembler::append(const ParsedKey &key, std::span<const std::byte> chunk) {
    // The key of the first line is "<id><rest>"
    const bool continues = has_entry_
        && key.id == id_
        && key.rest == this->key().substr(4)
        && last_chunk_size_ == hex_bytes_per_line
        && key.byte_offset == size_
        && !chunk.empty()
        && size_ + chunk.size() <= max_value_size;
    if (!continues) {
        has_entry_ = false;
        return false;
    }
    std::ranges::copy(chunk, bytes_.begin() + size_);
    size_ += chunk.size();
    last_chunk_size_ = chunk.size();
    return true;
}

bool LineChecker::add(std::string_view line) {
    const bool terminated = line.ends_with('\n');
    std::string_view content = terminated ? line.substr(0, line.size() - 1) : line;
    if (content.ends_with('\r')) {
        content.remove_suffix(1);
    }

    if (unterminated_ || content.size() > max_line_length || line.starts_with(' ') || line.starts_with('\t')) {
        return false;
    }
    unterminated_ = !terminated;

    switch (region_) {

    case Region::before_items:
        if (line == "[items]\n") {
            region_ = Region::items;
        }
        break;

    case Region::items:
        if (line == "[end]\n") {
            region_ = Region::after_items;
            break;
        }
        item_crc_ = crc32_sw(reinterpret_cast<const uint8_t *>(line.data()), line.size(), item_crc_);
        item_line_count_++;
        break;

    case Region::after_items:
        break;
    }
    return true;
}

} // namespace config_store_import
