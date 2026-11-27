#pragma once

namespace PrusaGcodeSuite {

/// Soaks the XL enclosure with the bed after the frame has absorbed the heat.
///
/// Runs only during a print that heats the enclosure through the bed (see
/// Chamber::is_heating_through_bed()) with the bed on, Max Soak Time above 0
/// and a chamber minimum among the used filaments, and not in a dry run.
/// Waits until the chamber estimate is within 3 C of the highest chamber
/// minimum of the used filaments. Ends early on skip, abort, when the bed or
/// the enclosure heating is switched off, and at Max Soak Time. The chamber
/// estimate is only available after 5 minutes of printing; until then only
/// the time limit ends the wait. Does not change any target temperature.
/// With Soak Fan set, runs the print fan of the picked tool at that power
/// while soaking and restores its speed afterwards.
void chamber_soak();

} // namespace PrusaGcodeSuite
