#include <bed_tiles_renderer.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using namespace json;
using nhttp::handler::BedTilesRenderer;
using nhttp::handler::bed_tiles_data::Tile;

namespace {

// A bed of 3 x 2 tiles in place of the printer's
constexpr uint8_t columns = 3;
constexpr uint8_t rows = 2;
Tile tiles[columns][rows];
float current;

void fill_tiles() {
    for (uint8_t column = 0; column < columns; column++) {
        for (uint8_t row = 0; row < rows; row++) {
            tiles[column][row] = Tile {
                .number = static_cast<uint8_t>(10 + column + columns * row),
                .temp = 20.5f + column + 10 * row,
                .target = row == 0 ? 60.0f : 0.0f,
                .duty = static_cast<uint8_t>(5 * column),
                .enabled = column == 1,
            };
        }
    }
    current = 2.41f;
}

std::string render(size_t chunk_size) {
    BedTilesRenderer renderer;
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

constexpr std::string_view longest_tile = "{\"number\":12,\"temp\":22.5,\"target\":60.0,\"duty\":10,\"enabled\":false}";

constexpr std::string_view expected_complete = "{\"grid\":{\"x\":3,\"y\":2},\"heater_current\":2.41,\"tiles\":["
                                               "[{\"number\":10,\"temp\":20.5,\"target\":60.0,\"duty\":0,\"enabled\":false},"
                                               "{\"number\":11,\"temp\":21.5,\"target\":60.0,\"duty\":5,\"enabled\":true},"
                                               "{\"number\":12,\"temp\":22.5,\"target\":60.0,\"duty\":10,\"enabled\":false}],"
                                               "[{\"number\":13,\"temp\":30.5,\"target\":0.0,\"duty\":0,\"enabled\":false},"
                                               "{\"number\":14,\"temp\":31.5,\"target\":0.0,\"duty\":5,\"enabled\":true},"
                                               "{\"number\":15,\"temp\":32.5,\"target\":0.0,\"duty\":10,\"enabled\":false}]]}";

} // namespace

namespace nhttp::handler::bed_tiles_data {

uint8_t columns() {
    return ::columns;
}

uint8_t rows() {
    return ::rows;
}

Tile tile(uint8_t column, uint8_t row) {
    return (column < ::columns && row < ::rows) ? tiles[column][row] : Tile {};
}

float heater_current() {
    return current;
}

} // namespace nhttp::handler::bed_tiles_data

TEST_CASE("Bed tiles JSON - one buffer") {
    fill_tiles();
    CHECK(render(4096) == expected_complete);
}

TEST_CASE("Bed tiles JSON - resumed in small chunks") {
    fill_tiles();
    // A tile is output in one piece: the smallest chunk holds the longest tile, its leading comma and the
    // terminating zero
    const size_t smallest = longest_tile.size() + 2;
    for (size_t chunk : { smallest, smallest + 1, smallest + 13, size_t { 100 }, size_t { 200 } }) {
        CAPTURE(chunk);
        CHECK(render(chunk) == expected_complete);
    }
}

TEST_CASE("Bed tiles JSON - tile changes between chunks") {
    fill_tiles();
    BedTilesRenderer renderer;
    std::string response;
    bool changed = false;
    auto result = JsonResult::Incomplete;
    while (result == JsonResult::Incomplete) {
        uint8_t buffer[80];
        const auto [result_partial, written] = renderer.render(buffer, sizeof(buffer));
        response.append(reinterpret_cast<const char *>(buffer), written);
        result = result_partial;
        // Change a tile of the second row once the first row is being sent, as the printer may while heating
        if (!changed && response.find("\"tiles\":[") != std::string::npos) {
            REQUIRE(response.find("\"number\":13") == std::string::npos);
            tiles[0][1].temp = 45;
            tiles[0][1].duty = 100;
            changed = true;
        }
    }
    REQUIRE(result == JsonResult::Complete);
    CHECK(response.find("[{\"number\":13,\"temp\":45.0,\"target\":0.0,\"duty\":100,\"enabled\":false},") != std::string::npos);
}
