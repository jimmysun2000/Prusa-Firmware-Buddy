
#include "core/serial.h"
#include "gcode/parser.h"
#include "inc/MarlinConfig.h"
#include "PrusaGcodeSuite.hpp"

#include <option/has_spool_join.h>
#if HAS_SPOOL_JOIN()
    #include <module/prusa/spool_join.hpp>
#endif

static_assert(HAS_SPOOL_JOIN());

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M864: Spool join <a href="https://reprap.org/wiki/G-code#M864_Spool_join">M864 Spool join</a>
 *
 * Only MK3.5/S, MK3.9/S, MK4/S with MMU and XL
 *
 *#### Usage
 *
 *    M864 [ J | A | B | R | D ]
 *
 *#### Parameters
 *
 * - `J` - Add a join, together with `A` and `B`
 * - `A` - Tool that runs out of filament
 * - `B` - Tool that continues; joined at the end of the chain `A` is in
 * - `R` - Remove all joins
 * - `D` - Remove this tool from the joins; the tools before and after it stay joined
 *
 *#### Examples
 *
 *    M864 J A1 B2 ; When tool 1 runs out of filament, continue with tool 2
 *    M864 D2      ; Remove tool 2 from the joins
 *    M864 R       ; reset any settings
 *    M864         ; Print current join settings
 *
 * Without parameters prints the current Spool join mapping
 *
 * Tools are numbered from 0.
 */
void PrusaGcodeSuite::M864() {
    if (parser.seen('J') && parser.seen("A") && parser.seen("B")) {
        // map logical tool to physical
        const uint8_t spool_1 = parser.byteval('A');
        const uint8_t spool_2 = parser.byteval('B');

        bool res = spool_join.add_join(spool_1, spool_2);
        if (!res) {
            SERIAL_ERROR_MSG("Invalid join");
        }
    } else if (parser.seen('R')) {
        // reset spool join settings
        spool_join.reset();
    } else if (parser.seenval('D')) {
        const uint8_t spool = parser.byteval('D');
        if (spool >= VirtualToolIndex::count) {
            SERIAL_ERROR_MSG("Invalid tool");
        } else {
            spool_join.reroute_joins_containing(VirtualToolIndex::from_raw(spool));
        }
    } else {
        // print current spool join settings
        SERIAL_ECHOLN("Spool joins: ");
        for (size_t i = 0; i < spool_join.get_num_joins(); i++) {
            SpoolJoin::join_config_t join = spool_join.get_join_nr(i);
            SERIAL_ECHOLNPAIR("  When tool ", join.spool_1, " runs out, ", join.spool_2, " will continue");
        }
    }
}

/** @}*/
