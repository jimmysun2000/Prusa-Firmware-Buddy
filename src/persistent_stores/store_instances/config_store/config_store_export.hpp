/// @file
/// Settings backup: writes the config store to a text file. Export only, nothing ever reads such a file back.
#pragma once

#include <array>
#include <cstddef>

namespace config_store_export {

/// Buffer for the file name of a backup (without the directory), including the terminating zero
using FileName = std::array<char, 32>;

/// Writes the backup to @p path, replacing the file if it exists.
/// Passwords and tokens are written as "redacted:" without their value.
/// A partially written file is deleted.
/// @returns whether the whole file was written
bool export_to_file(const char *path);

/// Writes the backup to the first free /usb/settings_backup_<n>.ini (n = 1, 2, ...).
/// @param file_name receives the name of the file (without the directory)
/// @returns whether the whole file was written
bool export_to_usb(FileName &file_name);

} // namespace config_store_export
