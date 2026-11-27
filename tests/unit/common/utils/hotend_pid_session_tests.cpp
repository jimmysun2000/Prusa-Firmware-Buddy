#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <hotend_pid_session.hpp>

using namespace hotend_pid;

namespace {

/// Tools of the XL
constexpr uint8_t TOOL_COUNT = 5;

constexpr std::array<int16_t, 4> TUNING_TEMPS { 170, 215, 260, 305 };

constexpr std::array<TurnStage, 2> STAGES { TurnStage::moving_to_front, TurnStage::measuring };

std::vector<uint8_t> walk(ToolSet set) {
    std::vector<uint8_t> tools;
    for (std::optional<uint8_t> tool = first_tool(set); tool.has_value(); tool = next_tool(set, *tool)) {
        tools.push_back(*tool);
    }
    return tools;
}

} // namespace

TEST_CASE("hotend_pid_session: tools are walked in ascending order, unchosen ones skipped") {
    CHECK(walk(0b00000) == std::vector<uint8_t> {});
    CHECK(walk(0b00100) == std::vector<uint8_t> { 2 });
    CHECK(walk(0b01101) == std::vector<uint8_t> { 0, 2, 3 });
    CHECK(walk(0b11111) == std::vector<uint8_t> { 0, 1, 2, 3, 4 });
    CHECK(walk(0b10010) == std::vector<uint8_t> { 1, 4 });
    CHECK(walk(0b10000000) == std::vector<uint8_t> { 7 });

    CHECK(!first_tool(0).has_value());
    CHECK(!next_tool(0b01101, 3).has_value());
    CHECK(next_tool(0b01101, 1) == 2);
    CHECK(!next_tool(0xFF, 7).has_value());
}

TEST_CASE("hotend_pid_session: membership, count and position") {
    SECTION("one tool") {
        constexpr ToolSet set = 0b00100;
        CHECK(tool_count(set) == 1);
        CHECK(contains(set, 2));
        CHECK(!contains(set, 0));
        CHECK(position_of(set, 2) == 1);
        CHECK(position_of(set, 3) == 0);
    }

    SECTION("three tools") {
        constexpr ToolSet set = 0b01101;
        CHECK(tool_count(set) == 3);
        CHECK(position_of(set, 0) == 1);
        CHECK(position_of(set, 2) == 2);
        CHECK(position_of(set, 3) == 3);
        CHECK(position_of(set, 1) == 0);
        CHECK(position_of(set, 4) == 0);
        CHECK(!contains(set, 1));
    }

    SECTION("five tools") {
        constexpr ToolSet set = 0b11111;
        CHECK(tool_count(set) == 5);
        for (uint8_t tool = 0; tool < TOOL_COUNT; ++tool) {
            CHECK(contains(set, tool));
            CHECK(position_of(set, tool) == tool + 1);
        }
    }

    CHECK(!contains(0xFF, TOOL_SET_CAPACITY));
    CHECK(position_of(0xFF, TOOL_SET_CAPACITY) == 0);
}

TEST_CASE("hotend_pid_session: waiting targets of tools 1, 3 and 4 at 260 C") {
    constexpr ToolSet set = 0b01101;
    constexpr int16_t temp = 260;

    // Tool 1 in turn: tool 3 waits at the waiting temperature until tool 1 is measured
    CHECK(waiting_target(set, 0, TurnStage::moving_to_front, 2, temp) == WAITING_TEMP);
    CHECK(waiting_target(set, 0, TurnStage::moving_to_front, 3, temp) == WAITING_TEMP);
    CHECK(waiting_target(set, 0, TurnStage::measuring, 2, temp) == temp);
    CHECK(waiting_target(set, 0, TurnStage::measuring, 3, temp) == WAITING_TEMP);

    // Tool 3 in turn: tool 1 is finished, tool 4 is next
    CHECK(waiting_target(set, 2, TurnStage::moving_to_front, 0, temp) == 0);
    CHECK(waiting_target(set, 2, TurnStage::moving_to_front, 3, temp) == WAITING_TEMP);
    CHECK(waiting_target(set, 2, TurnStage::measuring, 3, temp) == temp);

    // Tool 4 in turn: nobody waits any more
    for (const TurnStage stage : STAGES) {
        for (uint8_t tool = 0; tool < TOOL_COUNT; ++tool) {
            CHECK(waiting_target(set, 3, stage, tool, temp) == 0);
        }
    }

    // Unchosen tools never heat
    for (const uint8_t in_turn : std::array<uint8_t, 3> { 0, 2, 3 }) {
        for (const TurnStage stage : STAGES) {
            CHECK(waiting_target(set, in_turn, stage, 1, temp) == 0);
            CHECK(waiting_target(set, in_turn, stage, 4, temp) == 0);
        }
    }
}

TEST_CASE("hotend_pid_session: waiting targets of every session") {
    for (unsigned raw_set = 1; raw_set < (1u << TOOL_COUNT); ++raw_set) {
        const ToolSet set = static_cast<ToolSet>(raw_set);
        const std::vector<uint8_t> order = walk(set);
        REQUIRE(static_cast<int>(order.size()) == tool_count(set));

        for (const uint8_t in_turn : order) {
            for (const TurnStage stage : STAGES) {
                for (const int16_t temp : TUNING_TEMPS) {
                    // Waiting tools above the waiting temperature, which can only be the tuning temperature
                    int raised = 0;
                    for (uint8_t tool = 0; tool < TOOL_COUNT; ++tool) {
                        const int16_t target = waiting_target(set, in_turn, stage, tool, temp);
                        CAPTURE(raw_set, static_cast<int>(in_turn), static_cast<int>(stage), static_cast<int>(tool), temp, target);

                        CHECK(target <= temp);
                        CHECK(target >= 0);
                        if (!contains(set, tool) || tool == in_turn || position_of(set, tool) < position_of(set, in_turn)) {
                            CHECK(target == 0);
                        } else {
                            CHECK((target == WAITING_TEMP || target == temp));
                        }
                        if (target > WAITING_TEMP) {
                            CHECK(target == temp);
                            ++raised;
                        }
                    }
                    CHECK(raised <= 1);
                    if (stage == TurnStage::moving_to_front) {
                        CHECK(raised == 0);
                    }
                }
            }
        }

        // Once the last tool is in turn, no tool waits any more
        for (const TurnStage stage : STAGES) {
            for (uint8_t tool = 0; tool < TOOL_COUNT; ++tool) {
                CHECK(waiting_target(set, order.back(), stage, tool, 260) == 0);
            }
        }
    }
}

TEST_CASE("hotend_pid_session: a session of one tool heats no other tool") {
    for (uint8_t in_turn = 0; in_turn < TOOL_COUNT; ++in_turn) {
        const ToolSet set = static_cast<ToolSet>(1u << in_turn);
        for (const TurnStage stage : STAGES) {
            for (uint8_t tool = 0; tool < TOOL_COUNT; ++tool) {
                CHECK(waiting_target(set, in_turn, stage, tool, 215) == 0);
            }
        }
    }
}
