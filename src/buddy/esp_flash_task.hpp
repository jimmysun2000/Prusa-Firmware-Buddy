#pragma once

void start_flash_esp_task();

/// Pretend that the ESP flash task has been run and finished.
/// This is to skip the esp flashing in error screens.
void skip_esp_flashing();

/// Leave the ESP unpowered until reboot, without running the flash task.
///
/// Provides the esp_flashed dependency right away, so the tasks waiting for it start normally.
/// The ESP driver stays idle: it never resets or talks to the ESP.
void skip_esp_power_up();
