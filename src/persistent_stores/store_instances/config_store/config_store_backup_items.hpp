/// @file
/// Entries of the settings backup: names, value kinds and the redaction rule, shared by the export and the import.
/// Only declarations here; this header does not need the config store.
#pragma once

#include "config_store_export_format.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace config_store_backup {

/// How the value of an entry is written
enum class Kind : uint8_t {
    boolean,
    unsigned_integer,
    signed_integer,
    floating_point,
    string,
    raw,
};

namespace detail {
    template <typename T>
    inline constexpr bool is_char_array = false;

    template <size_t N>
    inline constexpr bool is_char_array<std::array<char, N>> = true;
} // namespace detail

/// Kind of a stored value of type @p T. Enums are written as their underlying type, anything else as raw bytes.
template <typename T>
consteval Kind kind_of() {
    if constexpr (detail::is_char_array<T>) {
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

/// Id of the first element for items stored as an array under one name
template <typename Item>
constexpr std::optional<uint16_t> array_first_id() {
    if constexpr (requires { Item::hashed_id_first; }) {
        return Item::hashed_id_first;
    } else {
        return std::nullopt;
    }
}

/// Key of the entry @p id of a current store item.
/// @param array_first_id id of the first element for items stored as an array under one name
config_store_export::Key item_key(uint16_t id, std::optional<uint16_t> array_first_id);

/// Whether the backup leaves out the value of the entry @p id: passwords and tokens, and strings not on the allow-list
bool is_redacted(uint16_t id, Kind kind);

} // namespace config_store_backup
