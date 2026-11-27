/// @file
/// Settings restore: writes the values of a settings backup (see config_store_export.hpp) back to the config store.
#pragma once

#include <journal/concepts.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace config_store_import {

/// The only file the restore reads. The owner copies a backup there under this name.
inline constexpr const char *file_path = "/usb/settings_restore.ini";

/// Why a backup is refused
enum class Error : uint8_t {
    none,

    /// The file cannot be opened (no USB drive or no such file)
    file_not_found,

    /// Reading the file failed
    read_failed,

    /// A line the export cannot have written (see Summary::line)
    format,

    /// The file ends before [end] is complete
    incomplete,

    /// The number or the CRC of the lines in [items] does not match [end]. Takes precedence over the errors below.
    checksum,

    /// Another version of the file format
    format_version,

    /// The backup is from another printer model
    printer_model,

    /// The backup is from another printer, or the serial number of this one cannot be read
    serial_number,

    /// The backup is from a firmware with other config store migrations
    config_version,

    /// An entry appears twice (see Summary::id)
    duplicate_entry,

    /// The value of an entry does not fit its item (see Summary::id)
    invalid_value,

    /// A tool offset outside the limits of the tool offset calibration (see Summary::id)
    tool_offset,

    /// A dock position too far from where the dock can be (see Summary::id)
    dock_position,

    /// The file is not the one that was checked before
    changed,

    /// The printer is busy, nothing was written
    busy,
};

/// Upper bound of the number of distinct flag combinations of the store items
inline constexpr size_t max_flag_sets = 32;

/// Result of reading a backup
struct Summary {
    Error error = Error::none;

    /// Line of the file the error is about, 0 if none
    uint32_t line = 0;

    /// Journal id of the entry the error is about, if any
    uint16_t id = 0;

    /// Firmware version that wrote the backup, cut to fit
    std::array<char, 48> firmware {};

    /// The backup was written by another firmware build than the running one
    bool firmware_differs = false;

    /// CRC of the lines in [items]
    uint32_t crc = 0;

    /// Entries whose id this firmware does not know, or whose name differs from this firmware's for the id
    uint16_t unknown_count = 0;

    /// Entries that are never written back: special and RAM-only items, secrets, printer state, ...
    uint16_t not_restorable_count = 0;

    struct FlagSetCount {
        journal::ItemFlags flags;
        uint16_t count;
    };

    /// Number of restorable entries whose value differs from the current one, per distinct flags of their items
    std::array<FlagSetCount, max_flag_sets> differing {};
    uint8_t differing_flag_sets = 0;

    /// Number of differing entries that apply() writes when skipping the items having any of @p skip_flags
    uint16_t changes(journal::ItemFlags skip_flags) const;

    /// Number of differing entries whose items have any of @p flags
    uint16_t differing_with_any(journal::ItemFlags flags) const;
};

/// Flags of items that are never written back, whatever the selection
journal::ItemFlags never_restored_flags();

/// Reads the backup at @p path and compares it with the current values. Writes nothing.
/// The file is refused as a whole (Summary::error) unless its header matches this printer and firmware,
/// its checksum matches and every line is valid.
Summary scan(const char *path);

struct ApplyResult {
    Error error;

    /// Some values were written: the printer has to restart
    bool written;
};

/// Checks the backup at @p path again like scan(). If it passes and is still the file with @p expected_crc,
/// and @p may_write() allows it right before the first write, writes in one transaction every differing value
/// of the items having none of @p skip_flags (nor never_restored_flags()), through the items' own setters.
/// The printer has to restart after anything was written, also on an error, as values cached at boot are stale.
ApplyResult apply(const char *path, journal::ItemFlags skip_flags, uint32_t expected_crc, bool (*may_write)());

} // namespace config_store_import
