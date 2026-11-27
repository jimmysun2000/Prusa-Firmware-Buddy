/// @file
#pragma once

namespace screen_restore_settings {

/// Checks the settings backup in config_store_import::file_path and opens the restore preview, or tells why not.
/// Only while the printer is idle.
void open();

} // namespace screen_restore_settings
