#pragma once

#include "segmented_json.h"

#include <cstdint>
#include <optional>

namespace nhttp::handler {

/// Reads of the XL enclosure for the enclosure route. They are called from the HTTP thread, so each read
/// is lock-free or goes through the config store.
namespace enclosure_data {

    /// @returns whether the enclosure is enabled in the settings
    bool enabled();

    /// @returns the estimated temperature inside the enclosure [°C], nullopt while there is no estimate
    std::optional<float> temperature();

    /// @returns the measured speed of the enclosure fan [RPM], 0 if not measured
    uint16_t fan_rpm();

} // namespace enclosure_data

/// Renders the XL enclosure, for an EmptyRenderer:
///
/// {"enabled":true,"temp":31.5,"fan_rpm":1450}
///
/// temp is the estimated temperature inside the enclosure [°C], null while there is no estimate (the
/// printer makes one only after some minutes of printing); fan_rpm is the measured speed of the
/// enclosure fan. Every value is read when it is output.
json::JsonResult render_enclosure(size_t resume_point, json::JsonOutput &output);

} // namespace nhttp::handler
