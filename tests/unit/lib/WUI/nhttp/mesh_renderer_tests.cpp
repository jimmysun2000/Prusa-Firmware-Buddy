#include <mesh_renderer.h>

#include <common/bed_mesh_view.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <string_view>

using namespace json;
using nhttp::handler::MeshRenderer;

namespace {

// Small fake mesh in place of the UBL one: 4 x 3 points, point x, y holds (x + 10 * y) / 1000 mm
constexpr uint8_t nx = 4;
constexpr uint8_t ny = 3;
float mesh[nx][ny];

void fill_mesh() {
    for (uint8_t x = 0; x < nx; x++) {
        for (uint8_t y = 0; y < ny; y++) {
            mesh[x][y] = (x + 10 * y) * 0.001f;
        }
    }
}

std::string render(size_t chunk_size) {
    MeshRenderer renderer;
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

constexpr std::string_view expected_complete = "{\"grid\":{\"x\":4,\"y\":3},"
                                               "\"min\":{\"x\":-1.500,\"y\":2.000},"
                                               "\"max\":{\"x\":100.000,\"y\":200.250},"
                                               "\"major\":{\"border\":1,\"step\":3},"
                                               "\"state\":\"complete\","
                                               "\"z\":[[0.000,0.001,0.002,0.003],[0.010,0.011,0.012,0.013],[0.020,0.021,0.022,0.023]]}";

} // namespace

namespace bed_mesh_view {

Info info() {
    return Info { .nx = nx, .ny = ny, .border = 1, .major_step = 3, .min_x = -1.5f, .min_y = 2, .max_x = 100, .max_y = 200.25f };
}

float z(uint8_t ix, uint8_t iy) {
    return (ix < nx && iy < ny) ? mesh[ix][iy] : NAN;
}

State state() {
    return compute_state(info(), z);
}

uint32_t revision() {
    return compute_revision(info(), z);
}

} // namespace bed_mesh_view

TEST_CASE("Mesh JSON - one buffer") {
    fill_mesh();
    CHECK(render(4096) == expected_complete);
}

TEST_CASE("Mesh JSON - resumed in small chunks") {
    fill_mesh();
    // 19 is the smallest chunk that fits the longest field ("state":"complete" and the terminating zero);
    // small chunks force a resume at nearly every value
    for (size_t chunk : { 19, 20, 23, 40, 100 }) {
        CAPTURE(chunk);
        CHECK(render(chunk) == expected_complete);
    }
}

TEST_CASE("Mesh JSON - missing points") {
    fill_mesh();
    mesh[1][0] = NAN;
    mesh[3][2] = NAN;
    const std::string out = render(20);
    CHECK(out.find("\"state\":\"partial\"") != std::string::npos);
    CHECK(out.find("[0.000,null,0.002,0.003]") != std::string::npos);
    CHECK(out.find("[0.020,0.021,0.022,null]") != std::string::npos);
}

TEST_CASE("Mesh JSON - point turns NaN between chunks") {
    fill_mesh();
    MeshRenderer renderer;
    std::string response;
    bool changed = false;
    auto result = JsonResult::Incomplete;
    while (result == JsonResult::Incomplete) {
        uint8_t buffer[24];
        const auto [result_partial, written] = renderer.render(buffer, sizeof(buffer));
        response.append(reinterpret_cast<const char *>(buffer), written);
        result = result_partial;
        // Change the mesh in the middle of the response, as the printer may do while probing
        if (!changed && response.find("\"z\":[") != std::string::npos) {
            mesh[3][2] = NAN;
            changed = true;
        }
    }
    REQUIRE(result == JsonResult::Complete);
    CHECK(response.find("[0.020,0.021,0.022,null]]}") != std::string::npos);
}

TEST_CASE("Mesh JSON - no mesh") {
    for (auto &column : mesh) {
        for (auto &point : column) {
            point = 0;
        }
    }
    const std::string out = render(4096);
    CHECK(out.find("\"state\":\"none\"") != std::string::npos);
    CHECK(out.find("[0.000,0.000,0.000,0.000]") != std::string::npos);
}
