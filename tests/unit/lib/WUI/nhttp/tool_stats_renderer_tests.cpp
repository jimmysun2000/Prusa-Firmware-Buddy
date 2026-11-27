#include <tool_stats_renderer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

using namespace json;
using nhttp::handler::ToolStatsRenderer;
using nhttp::handler::tool_stats_data::ToolStats;

namespace {

// Three tool positions in place of the printer's
constexpr uint8_t tool_count = 3;
ToolStats stats[tool_count];
uint32_t recoveries;

void fill_stats() {
    stats[0] = ToolStats { .filament_used = 1234567.75f, .picks = 321, .print_time = 360000 };
    stats[1] = ToolStats { .filament_used = 0.5f, .picks = 4294967295u, .print_time = 59 };
    stats[2] = ToolStats {};
    recoveries = 2;
}

std::string render(size_t chunk_size) {
    ToolStatsRenderer renderer;
    std::string response;
    auto result = JsonResult::Incomplete;
    while (result == JsonResult::Incomplete) {
        std::string buffer(chunk_size, '\0');
        const auto [result_partial, written] = renderer.render(reinterpret_cast<uint8_t *>(buffer.data()), chunk_size);
        REQUIRE(written <= chunk_size);
        REQUIRE((written > 0 || result_partial == JsonResult::Complete));
        response.append(buffer.data(), written);
        result = result_partial;
    }
    REQUIRE(result == JsonResult::Complete);
    return response;
}

constexpr std::string_view first_entry = "{\"filament_used\":1234567.8,\"picks\":321,\"print_time\":360000}";
constexpr std::string_view second_entry = "{\"filament_used\":0.5,\"picks\":4294967295,\"print_time\":59}";

constexpr std::string_view expected_complete = "{\"tools\":["
                                               "{\"filament_used\":1234567.8,\"picks\":321,\"print_time\":360000},"
                                               "{\"filament_used\":0.5,\"picks\":4294967295,\"print_time\":59},"
                                               "{\"filament_used\":0.0,\"picks\":0,\"print_time\":0}"
                                               "],\"tool_recoveries\":2}";

} // namespace

namespace nhttp::handler::tool_stats_data {

uint8_t count() {
    return tool_count;
}

ToolStats tool(uint8_t index) {
    return index < tool_count ? stats[index] : ToolStats {};
}

uint32_t tool_recoveries() {
    return recoveries;
}

} // namespace nhttp::handler::tool_stats_data

TEST_CASE("Tool stats JSON - one buffer") {
    fill_stats();
    CHECK(render(4096) == expected_complete);
}

TEST_CASE("Tool stats JSON - resumed in small chunks") {
    fill_stats();
    // An entry is output in one piece: the smallest chunk holds the longest entry with its leading comma
    // (none before the first one) and the terminating zero
    const size_t smallest = std::max(first_entry.size() + 1, second_entry.size() + 2);
    for (size_t chunk : { smallest, smallest + 1, smallest + 9, size_t { 100 }, size_t { 150 } }) {
        CAPTURE(chunk);
        CHECK(render(chunk) == expected_complete);
    }
}

TEST_CASE("Tool stats JSON - extruded length not known") {
    fill_stats();
    stats[2].filament_used = NAN;
    CHECK(render(4096).find("{\"filament_used\":null,\"picks\":0,\"print_time\":0}]") != std::string::npos);
}

TEST_CASE("Tool stats JSON - counter changes between chunks") {
    fill_stats();
    ToolStatsRenderer renderer;
    std::string response;
    bool changed = false;
    auto result = JsonResult::Incomplete;
    while (result == JsonResult::Incomplete) {
        uint8_t buffer[80];
        const auto [result_partial, written] = renderer.render(buffer, sizeof(buffer));
        response.append(reinterpret_cast<const char *>(buffer), written);
        result = result_partial;
        // A tool change during the response counts for an entry not sent yet
        if (!changed) {
            REQUIRE(response.find("\"print_time\":59") == std::string::npos);
            stats[1].print_time = 60;
            recoveries = 3;
            changed = true;
        }
    }
    REQUIRE(result == JsonResult::Complete);
    CHECK(response.find("\"print_time\":60},") != std::string::npos);
    CHECK(response.ends_with("\"tool_recoveries\":3}"));
}
