#include <feature/ramming/ramming_sequence.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace buddy;

TEST_CASE("RammingSequence") {
    static constexpr RammingSequenceArray seq({
        { 8, 995 },
        { -43, 6000 },
        { -8, 3000 },
        { -4, 1800 },
        { 20, 600 },
        { -20, 470 },
        { 55, 1740 },
        { -55, 6000 },
        { 20, 340 },
        { -20, 210 },
    });
    CHECK(seq.retracted_distance() == 55);
}

// The Nextruder unload and runout sequences both retract the whole unload length,
// so the choice between them does not change the unload move that follows
TEST_CASE("RammingSequence: Nextruder unload") {
    static constexpr RammingSequenceArray seq({
        { 8, 995 },
        { -43, 6000 },
        { -8, 3000 },
        { -4, 1800 },
        { 20, 600 },
        { -20, 470 },
        { 55, 1740 },
        { -55, 6000 },
        { 20, 340 },
        { -20, 210 },
        { -50, 2000 },
    });
    CHECK(seq.retracted_distance() == 105);
}

TEST_CASE("RammingSequence: Nextruder runout") {
    static constexpr RammingSequenceArray seq({
        { 7, 1500 },
        { -50, 2700 },
        { -5, 50 },
        { -50, 1500 },
    });
    CHECK(seq.retracted_distance() == 105);
}
