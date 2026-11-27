/// @file
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

/// Length purged so far and total length of a running purge, in whole mm, for the purge screen.
///
/// Both have to fit the two bytes left in the load/unload FSM data. A byte each would end at 255 mm,
/// while a purge reaches 300 mm with a high-flow nozzle. So every pair with purged <= total gets
/// a number of its own instead, counting through the totals in order:
/// code = total * (total + 1) / 2 + purged.
struct PurgeLengths {
    uint16_t purged;
    uint16_t total;

    using Code = uint16_t;

    /// No purge is running
    static constexpr Code no_purge = 0;

    /// Largest total that has codes
    static constexpr uint16_t max_total = 360;

    /// Clamps the total to 1..max_total and the purged length to 0..total
    constexpr Code encode() const {
        const uint32_t t = std::clamp<uint32_t>(total, 1, max_total);
        return static_cast<Code>(first_code(t) + std::min<uint32_t>(purged, t));
    }

    /// @returns nullopt for no_purge and for codes encode() does not produce
    static constexpr std::optional<PurgeLengths> decode(Code code) {
        // The largest total whose first code is not above the code
        uint32_t t = 0;
        uint32_t upper = max_total;
        while (t < upper) {
            const uint32_t mid = (t + upper + 1) / 2;
            if (first_code(mid) <= code) {
                t = mid;
            } else {
                upper = mid - 1;
            }
        }

        const uint32_t purged = code - first_code(t);
        if (t == 0 || purged > t) {
            return std::nullopt;
        }
        return PurgeLengths { .purged = static_cast<uint16_t>(purged), .total = static_cast<uint16_t>(t) };
    }

    constexpr bool operator==(const PurgeLengths &) const = default;

private:
    static constexpr uint32_t first_code(uint32_t total) {
        return total * (total + 1) / 2;
    }
};

static_assert(PurgeLengths::decode(PurgeLengths { .purged = PurgeLengths::max_total, .total = PurgeLengths::max_total }.encode())
        == PurgeLengths { .purged = PurgeLengths::max_total, .total = PurgeLengths::max_total },
    "The largest pair must not overflow the code");
