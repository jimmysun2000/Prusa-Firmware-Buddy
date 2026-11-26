/// @file
/// Line format of the settings backup file, without any access to the store or the file system.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace config_store_export {

/// inih reads at most 199 characters per line; no line is longer than this
inline constexpr size_t max_line_length = 190;

/// Raw values longer than this continue on further lines keyed <id>+<byte offset>
inline constexpr size_t hex_bytes_per_line = 48;

/// Item names are cut to this length
inline constexpr size_t max_name_length = 60;

/// Receives the finished lines, without the line terminator
class LineSink {
public:
    virtual void write_line(std::string_view line) = 0;

protected:
    ~LineSink() = default;
};

/// Key of an entry: "<id> <name>[<index>]". The 4-digit hex journal id is authoritative, the name is informational.
struct Key {
    uint16_t id;
    std::string_view name;

    /// Element index for items stored as an array under one name
    std::optional<uint8_t> index = std::nullopt;
};

/// Writes "bool:0" or "bool:1"
void write_bool(LineSink &sink, const Key &key, bool value);

/// Writes "u8:", "u16:" or "u32:" with the value in decimal; @p size is the size of the stored type in bytes
void write_unsigned(LineSink &sink, const Key &key, uint32_t value, size_t size);

/// Writes "i8:", "i16:" or "i32:" with the value in decimal; @p size is the size of the stored type in bytes
void write_signed(LineSink &sink, const Key &key, int32_t value, size_t size);

/// Writes "f32:" with the value printed with 9 significant digits
void write_float(LineSink &sink, const Key &key, float value);

/// Writes "str:<text>" for the text up to the first zero, if it consists of [A-Za-z0-9._-] only and fits on one line.
/// Any other text is written as raw bytes.
void write_string(LineSink &sink, const Key &key, std::span<const char> text);

/// Writes "hex:" with the bytes in uppercase hex, hex_bytes_per_line bytes per line.
/// @p comment (informational) is appended to the first line after " # ", cut to fit.
void write_raw(LineSink &sink, const Key &key, std::span<const std::byte> bytes, std::string_view comment = {});

/// Writes "redacted:" without any value
void write_redacted(LineSink &sink, const Key &key);

/// Secrets are never written.
/// @returns true for the ids in @p secret_ids, and for every string item whose id is not in @p allowed_string_ids
bool is_redacted(uint16_t id, bool is_string, std::span<const uint16_t> secret_ids, std::span<const uint16_t> allowed_string_ids);

} // namespace config_store_export
