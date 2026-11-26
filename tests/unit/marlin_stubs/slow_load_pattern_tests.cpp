#include <slow_load_pattern.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <vector>

using buddy::SlowLoadPattern;

namespace {

using Moves = std::vector<float>;

struct Config {
    float total;
    SlowLoadPattern::Peck peck;
};

Moves moves_of(const Config &config) {
    SlowLoadPattern pattern(config.total, config.peck);
    Moves moves;
    while (const auto move = pattern.next()) {
        moves.push_back(*move);
    }
    return moves;
}

float sum_of(const Moves &moves) {
    float sum = 0;
    for (const float move : moves) {
        sum += move;
    }
    return sum;
}

float travel_of(const Moves &moves) {
    float travel = 0;
    for (const float move : moves) {
        travel += move < 0 ? -move : move;
    }
    return travel;
}

} // namespace

TEST_CASE("SlowLoadPattern: XL defaults") {
    const Moves moves = moves_of({ .total = 30, .peck = { .start = 3, .end = 30, .forward = 3, .back = 1 } });

    Moves expected { 3 };
    for (int i = 0; i < 13; ++i) {
        expected.push_back(3);
        expected.push_back(-1);
    }
    expected.push_back(1);

    CHECK(moves == expected);
    CHECK(moves.size() == 28);
    CHECK(sum_of(moves) == 30);
    CHECK(travel_of(moves) == 56);
}

TEST_CASE("SlowLoadPattern: invariants") {
    const Config config = GENERATE(
        Config { 30, { 3, 30, 3, 1 } },
        Config { 30, { 3, 20, 3, 1 } },
        Config { 40, { 3, 30, 3, 1 } },
        Config { 20, { 3, 30, 3, 1 } },
        Config { 30, { 0, 30, 3, 1 } },
        Config { 30, { 5, 30, 4, 2 } },
        Config { 30, { 3, 30, 2.5f, 0.5f } },
        Config { 30, { 3, 30, 27, 1 } });

    const Moves moves = moves_of(config);
    const auto &peck = config.peck;

    REQUIRE(!moves.empty());
    CHECK(sum_of(moves) == config.total);
    CHECK(moves.front() > 0);
    CHECK(moves.back() > 0);

    float position = 0;
    for (size_t i = 0; i < moves.size(); ++i) {
        CHECK(moves[i] != 0);
        if (moves[i] < 0) {
            // A back move only follows a full forward nudge
            REQUIRE(i > 0);
            CHECK(moves[i - 1] == peck.forward);
            CHECK(-moves[i] == peck.back);
        }

        position += moves[i];
        CHECK(position <= config.total);
        CHECK(position >= std::min(peck.start, config.total));
        if (moves[i] < 0) {
            CHECK(position + peck.back <= std::min(peck.end, config.total));
        }
    }
}

TEST_CASE("SlowLoadPattern: pecking stops at the end") {
    const Moves moves = moves_of({ .total = 30, .peck = { .start = 3, .end = 20, .forward = 3, .back = 1 } });

    Moves expected { 3 };
    for (int i = 0; i < 8; ++i) {
        expected.push_back(3);
        expected.push_back(-1);
    }
    expected.push_back(11);

    CHECK(moves == expected);
}

TEST_CASE("SlowLoadPattern: no pecking yields a single move") {
    const Config config = GENERATE(
        // Start at the end
        Config { 30, { 30, 30, 3, 1 } },
        // No room for a forward nudge
        Config { 30, { 3, 5, 3, 1 } },
        Config { 5, { 3, 30, 3, 1 } },
        Config { 30, { 40, 40, 3, 1 } },
        // Back nudge not between zero and the forward nudge
        Config { 30, { 3, 30, 3, 0 } },
        Config { 30, { 3, 30, 3, 3 } },
        Config { 30, { 3, 30, 3, 4 } },
        Config { 30, { 3, 30, 3, -1 } });

    CHECK(moves_of(config) == Moves { config.total });
}

TEST_CASE("SlowLoadPattern: zero length yields no move") {
    CHECK(moves_of({ .total = 0, .peck = { .start = 3, .end = 30, .forward = 3, .back = 1 } }).empty());
}
