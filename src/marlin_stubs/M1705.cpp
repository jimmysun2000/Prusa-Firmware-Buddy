#include "PrusaGcodeSuite.hpp"

#include <logging/log.hpp>
#include <feature/auto_retract/auto_retract.hpp>
#include <gcode/gcode_parser.hpp>
#include <module/planner.h>
#include <bsod/bsod.h>
#include <option/has_toolchanger.h>
#include <printers.h>

LOG_COMPONENT_REF(PRUSA_GCODE);

namespace PrusaGcodeSuite {

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M1705: Autoretract
 *
 *#### Usage
 *
 *    M1705 [N | A]
 *
 *#### Parameters
 *
 * - `N` - Do not park over the wastebin before ramming.
 * - `A` - XL only: retract every loaded tool in front of its dock, heating them all at once, and leave all tools parked.
 *         Skips empty, flexible and already fully retracted tools. Does nothing during a print or with Auto Retract off.
 *
 *#### Examples
 *
 *    M1705 A ; Retract all loaded tools
 */
void M1705() {
#if HAS_TOOLCHANGER() && PRINTER_IS_PRUSA_XL()
    GCodeParser2 parser;
    if (!parser.parse_marlin_command()) {
        return;
    }
    if (parser.option<bool>('A').value_or(false)) {
        buddy::auto_retract().retract_all_parked_tools();
        return;
    }
#endif

    if (std::holds_alternative<NoTool>(PhysicalToolIndex::currently_selected())) {
        log_error(PRUSA_GCODE, "autoretract on invalid tool");
        debug_assert(false);
        return;
    }

    buddy::auto_retract_detail::RetractFromNozzleParams params;
#if HAS_WASTEBIN()
    GCodeParser2 parser;
    if (!parser.parse_marlin_command()) {
        return;
    }
    params.park_over_wastebin = !(parser.option<bool>('N').value_or(false));
#endif
    buddy::auto_retract().maybe_retract_from_nozzle(params);
    planner.synchronize();
}

/** @}*/

} // namespace PrusaGcodeSuite
