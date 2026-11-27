#include "enclosure_renderer.h"

#include <config_store/store_instance.hpp>
#include <fanctl.hpp>
#include <option/xl_enclosure_support.h>
#include <xl_enclosure.hpp>

static_assert(XL_ENCLOSURE_SUPPORT());

// These run on the HTTP thread. Nothing here takes a lock other than the config store's, which is held
// only for the copy.

namespace nhttp::handler::enclosure_data {

bool enabled() {
    // The setting, which the enclosure writes together with its own flag; the flag itself is a plain
    // member of the Marlin thread
    return config_store().xl_enclosure_enabled.get();
}

std::optional<float> temperature() {
    // An atomic, written by the enclosure loop on the Marlin thread
    return xl_enclosure.get_enclosure_temperature();
}

uint16_t fan_rpm() {
    // An atomic, written by the tachometer in the 1 kHz timer interrupt
    return Fans::enclosure().get_actual_rpm();
}

} // namespace nhttp::handler::enclosure_data
