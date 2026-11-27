#include <tools_renderer.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

using namespace json;
using nhttp::handler::ToolsRenderer;
using nhttp::handler::tools_data::Tool;

namespace {

// Three tool positions in place of the printer's: two tools and an empty position
constexpr uint8_t tool_count = 3;
Tool tools[tool_count];
std::optional<uint8_t> active_tool;

Tool make_tool(float temp, float target, const char *filament, bool picked, bool parked, std::optional<float> retracted, float nozzle_diameter) {
    Tool tool;
    tool.enabled = true;
    tool.temp = temp;
    tool.target = target;
    strncpy(tool.filament.data(), filament, tool.filament.size() - 1);
    tool.picked = picked;
    tool.parked = parked;
    tool.retracted = retracted;
    tool.nozzle_diameter = nozzle_diameter;
    return tool;
}

void fill_tools() {
    tools[0] = make_tool(215.04f, 215, "PLA", true, false, 0.0f, 0.4f);
    tools[1] = make_tool(NAN, 0, "", false, true, std::nullopt, 0.6f);
    tools[2] = Tool {};
    active_tool = 0;
}

std::string render(size_t chunk_size) {
    ToolsRenderer renderer;
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

constexpr std::string_view first_tool = "{\"enabled\":true,\"temp\":215.0,\"target\":215.0,\"filament\":\"PLA\",\"picked\":true,"
                                        "\"parked\":false,\"retracted\":0,\"nozzle_diameter\":0.40}";
constexpr std::string_view second_tool = "{\"enabled\":true,\"temp\":null,\"target\":0.0,\"filament\":null,\"picked\":false,"
                                         "\"parked\":true,\"retracted\":null,\"nozzle_diameter\":0.60}";

std::string expected_complete() {
    return "{\"active\":0,\"tools\":[" + std::string(first_tool) + "," + std::string(second_tool) + ",{\"enabled\":false}]}";
}

} // namespace

namespace nhttp::handler::tools_data {

uint8_t count() {
    return tool_count;
}

std::optional<uint8_t> active() {
    return active_tool;
}

Tool tool(uint8_t index) {
    return index < tool_count ? tools[index] : Tool {};
}

} // namespace nhttp::handler::tools_data

TEST_CASE("Tools JSON - one buffer") {
    fill_tools();
    CHECK(render(4096) == expected_complete());
}

TEST_CASE("Tools JSON - resumed in small chunks") {
    fill_tools();
    // A tool entry is output in one piece: the smallest chunk holds the longest entry, its leading
    // comma and the terminating zero
    const size_t smallest = std::max(first_tool.size(), second_tool.size()) + 2;
    for (size_t chunk : { smallest, smallest + 1, smallest + 7, size_t { 200 }, size_t { 400 } }) {
        CAPTURE(chunk);
        CHECK(render(chunk) == expected_complete());
    }
}

TEST_CASE("Tools JSON - no tool picked") {
    fill_tools();
    active_tool = std::nullopt;
    CHECK(render(4096).starts_with("{\"active\":null,\"tools\":["));
}

TEST_CASE("Tools JSON - filament name is escaped") {
    fill_tools();
    tools[0] = make_tool(20, 0, "a\"b\\c", false, true, 20.4f, 0.25f);
    const std::string out = render(4096);
    CHECK(out.find("\"filament\":\"a\\\"b\\\\c\"") != std::string::npos);
    CHECK(out.find("\"retracted\":20,\"nozzle_diameter\":0.25}") != std::string::npos);
}

TEST_CASE("Tools JSON - longest filament name") {
    fill_tools();
    tools[1] = make_tool(20, 0, "\"\"\"\"\"\"\"", false, true, std::nullopt, 0.4f);
    CHECK(render(4096).find("\"filament\":\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\"") != std::string::npos);
}

TEST_CASE("Tools JSON - tool changes between chunks") {
    fill_tools();
    ToolsRenderer renderer;
    std::string response;
    bool changed = false;
    auto result = JsonResult::Incomplete;
    while (result == JsonResult::Incomplete) {
        uint8_t buffer[160];
        const auto [result_partial, written] = renderer.render(buffer, sizeof(buffer));
        response.append(reinterpret_cast<const char *>(buffer), written);
        result = result_partial;
        // The first chunk ends after the first tool; the second one is read when it is output
        if (!changed) {
            REQUIRE(response.find("\"nozzle_diameter\":0.60") == std::string::npos);
            tools[1].temp = 30;
            tools[1].parked = false;
            changed = true;
        }
    }
    REQUIRE(result == JsonResult::Complete);
    CHECK(response.find("{\"enabled\":true,\"temp\":30.0,\"target\":0.0,\"filament\":null,\"picked\":false,\"parked\":false,") != std::string::npos);
}
