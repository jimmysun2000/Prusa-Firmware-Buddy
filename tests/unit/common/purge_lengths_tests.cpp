/// @file
#include <catch2/catch_test_macros.hpp>
#include <purge_lengths.hpp>

#include <limits>
#include <set>

TEST_CASE("purge_lengths: every pair comes back") {
    std::set<PurgeLengths::Code> codes;
    for (uint16_t total = 1; total <= PurgeLengths::max_total; total++) {
        for (uint16_t purged = 0; purged <= total; purged++) {
            const PurgeLengths lengths { .purged = purged, .total = total };
            const auto code = lengths.encode();
            CHECK(code != PurgeLengths::no_purge);
            CHECK(PurgeLengths::decode(code) == lengths);
            codes.insert(code);
        }
    }

    // One code per pair
    CHECK(codes.size() == size_t(PurgeLengths::max_total) * (PurgeLengths::max_total + 3) / 2);
}

TEST_CASE("purge_lengths: no purge") {
    CHECK(PurgeLengths::decode(PurgeLengths::no_purge) == std::nullopt);
}

TEST_CASE("purge_lengths: out of range is clamped") {
    // Purged beyond the total shows as fully purged
    CHECK(PurgeLengths::decode(PurgeLengths { .purged = 40, .total = 27 }.encode()) == PurgeLengths { .purged = 27, .total = 27 });

    // A zero total still is a purge
    CHECK(PurgeLengths::decode(PurgeLengths { .purged = 0, .total = 0 }.encode()) == PurgeLengths { .purged = 0, .total = 1 });

    CHECK(PurgeLengths::decode(PurgeLengths { .purged = 500, .total = 500 }.encode()) == PurgeLengths { .purged = PurgeLengths::max_total, .total = PurgeLengths::max_total });
}

TEST_CASE("purge_lengths: codes above the last pair are refused") {
    const auto last = PurgeLengths { .purged = PurgeLengths::max_total, .total = PurgeLengths::max_total }.encode();
    for (uint32_t code = last + 1; code <= std::numeric_limits<PurgeLengths::Code>::max(); code++) {
        CHECK(PurgeLengths::decode(static_cast<PurgeLengths::Code>(code)) == std::nullopt);
    }
}
