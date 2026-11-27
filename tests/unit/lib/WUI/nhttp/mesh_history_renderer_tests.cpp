#include <mesh_history_renderer.h>

#include <common/bed_mesh_history.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

using namespace json;
using namespace bed_mesh_history;
using bed_mesh_view::Info;
using nhttp::handler::MeshHistoryRenderer;

namespace {

// Same geometry as the XL: 36 x 36 points over 0 to 360 mm, major points 1, 4, ..., 34
constexpr Info xl_info { .nx = 36, .ny = 36, .border = 1, .major_step = 3, .min_x = 0, .min_y = 0, .max_x = 360, .max_y = 360 };

float mesh[36][36];
bool probed_marks[36][36];
Clock clock_now { .uptime_s = 5630, .time_s = 1795750935 };

float mesh_z(uint8_t ix, uint8_t iy) {
    return mesh[ix][iy];
}

bool mesh_probed(uint8_t ix, uint8_t iy) {
    return probed_marks[ix][iy];
}

constexpr uint8_t grid_index(uint8_t i) {
    return 1 + 3 * i;
}

/// Major point ix, iy holds (ix + iy + offset) µm, except the given lowest and highest point
void fill_mesh(int offset, float lowest, float highest) {
    for (auto &column : mesh) {
        for (auto &point : column) {
            point = 0.05f;
        }
    }
    for (uint8_t ix = 0; ix < points_x; ix++) {
        for (uint8_t iy = 0; iy < points_y; iy++) {
            mesh[grid_index(ix)][grid_index(iy)] = (ix + iy + offset) * 0.001f;
        }
    }
    mesh[grid_index(5)][grid_index(5)] = lowest;
    mesh[grid_index(6)][grid_index(6)] = highest;
}

/// Marks the major points of the square [first, last] on both axes as probed
void mark_probed(uint8_t first, uint8_t last) {
    for (auto &column : probed_marks) {
        for (auto &mark : column) {
            mark = false;
        }
    }
    for (uint8_t ix = first; ix <= last; ix++) {
        for (uint8_t iy = first; iy <= last; iy++) {
            probed_marks[grid_index(ix)][grid_index(iy)] = true;
        }
    }
}

/// A fresh history in place of the one of the printer, which the renderer reads
void reset_history() {
    // Store has no const or reference members, so references to the old object refer to the new one
    std::destroy_at(&store());
    std::construct_at(&store());
}

void push(const Meta &meta) {
    REQUIRE(store().push_if_measured(xl_info, mesh_z, mesh_probed, meta));
}

/// The two entries of the example in the documentation of the renderer
void push_example() {
    // A whole bed measurement at room temperature before the clock was set
    fill_mesh(0, -0.201f, 0.156f);
    mark_probed(0, 11);
    push(Meta { .uptime_s = 1210, .time_s = 0, .bed_target = 0, .bed_temp = 246, .tool = 0, .printing = false });

    // Print area probing of 6 x 6 points
    fill_mesh(0, -0.112f, 0.094f);
    mark_probed(3, 8);
    push(Meta { .uptime_s = 5025, .time_s = 1795750330, .bed_target = 60, .bed_temp = 598, .tool = 0, .printing = true });
}

struct Rendered {
    JsonResult result;
    std::string json;
};

/// Renders in chunks of @p chunk_size; @p between is called after each chunk with the output so far
template <class Between>
Rendered render(uint16_t id, size_t chunk_size, Between between) {
    MeshHistoryRenderer renderer(id);
    Rendered rendered { JsonResult::Incomplete, {} };
    while (rendered.result == JsonResult::Incomplete) {
        std::string buffer(chunk_size, '\0');
        const auto [result, written] = renderer.render(reinterpret_cast<uint8_t *>(buffer.data()), chunk_size);
        REQUIRE(written <= chunk_size);
        REQUIRE((written > 0 || result != JsonResult::Incomplete));
        rendered.json.append(buffer.data(), written);
        rendered.result = result;
        between(rendered.json);
    }
    CHECK(freertos::Mutex::locked_mutex_count == 0);
    return rendered;
}

std::string render(uint16_t id, size_t chunk_size) {
    const Rendered rendered = render(id, chunk_size, [](const std::string &) {});
    REQUIRE(rendered.result == JsonResult::Complete);
    return rendered.json;
}

size_t count(std::string_view text, std::string_view what) {
    size_t found = 0;
    for (size_t pos = text.find(what); pos != std::string_view::npos; pos = text.find(what, pos + 1)) {
        found++;
    }
    return found;
}

constexpr std::string_view expected_list = "{\"uptime\":5630,\"time\":1795750935,\"capacity\":8,\"points\":144,\"entries\":["
                                           "{\"id\":2,\"uptime\":5025,\"time\":1795750330,\"bed\":{\"target\":60,\"temp\":59.8},\"tool\":0,"
                                           "\"printing\":true,\"probed_points\":36,\"z_min\":-0.112,\"z_max\":0.094},"
                                           "{\"id\":1,\"uptime\":1210,\"time\":null,\"bed\":{\"target\":0,\"temp\":24.6},\"tool\":0,"
                                           "\"printing\":false,\"probed_points\":144,\"z_min\":-0.201,\"z_max\":0.156}]}";

/// The JSON of entry 2 of push_example()
std::string expected_entry() {
    std::string json = "{\"id\":2,\"uptime\":5025,\"time\":1795750330,\"bed\":{\"target\":60,\"temp\":59.8},\"tool\":0,"
                       "\"printing\":true,\"probed_points\":36,\"z_min\":-0.112,\"z_max\":0.094,"
                       "\"grid\":{\"x\":12,\"y\":12},\"min\":{\"x\":10.286,\"y\":10.286},\"max\":{\"x\":349.714,\"y\":349.714},"
                       "\"major\":{\"border\":0,\"step\":1},\"state\":\"complete\",\"z\":[";
    for (int iy = 0; iy < 12; iy++) {
        json += iy > 0 ? ",[" : "[";
        for (int ix = 0; ix < 12; ix++) {
            char value[16];
            if (ix == 5 && iy == 5) {
                snprintf(value, sizeof(value), "-0.112");
            } else if (ix == 6 && iy == 6) {
                snprintf(value, sizeof(value), "0.094");
            } else {
                snprintf(value, sizeof(value), "0.%03d", ix + iy);
            }
            json += ix > 0 ? "," : "";
            json += value;
        }
        json += "]";
    }
    json += "],\"probed\":[";
    for (int iy = 0; iy < 12; iy++) {
        json += iy > 0 ? ",[" : "[";
        for (int ix = 0; ix < 12; ix++) {
            json += ix > 0 ? "," : "";
            json += (ix >= 3 && ix <= 8 && iy >= 3 && iy <= 8) ? "1" : "0";
        }
        json += "]";
    }
    json += "]}";
    return json;
}

} // namespace

namespace bed_mesh_view {

Info info() {
    return xl_info;
}

} // namespace bed_mesh_view

namespace bed_mesh_history {

Clock now() {
    return clock_now;
}

} // namespace bed_mesh_history

TEST_CASE("Mesh history JSON - empty") {
    reset_history();
    clock_now = { .uptime_s = 5630, .time_s = 1795750935 };
    CHECK(render(0, 4096) == "{\"uptime\":5630,\"time\":1795750935,\"capacity\":8,\"points\":144,\"entries\":[]}");

    clock_now.time_s = 0;
    CHECK(render(0, 4096) == "{\"uptime\":5630,\"time\":null,\"capacity\":8,\"points\":144,\"entries\":[]}");
}

TEST_CASE("Mesh history JSON - list") {
    reset_history();
    clock_now = { .uptime_s = 5630, .time_s = 1795750935 };
    push_example();
    CHECK(render(0, 4096) == expected_list);
    for (size_t chunk : { 200, 256, 512 }) {
        CAPTURE(chunk);
        CHECK(render(0, chunk) == expected_list);
    }
}

TEST_CASE("Mesh history JSON - entry") {
    reset_history();
    push_example();
    const std::string expected = expected_entry();
    CHECK(render(2, 4096) == expected);
    for (size_t chunk : { 200, 256, 512 }) {
        CAPTURE(chunk);
        CHECK(render(2, chunk) == expected);
    }
    CHECK(render(1, 4096).find("\"time\":null") != std::string::npos);
}

TEST_CASE("Mesh history JSON - values without a number") {
    reset_history();
    fill_mesh(0, -32.767f, 32.767f);
    mark_probed(0, 0);
    push(Meta { .uptime_s = 4294967295u, .time_s = 4294967295u, .bed_target = -32768, .bed_temp = no_temperature, .tool = no_tool, .printing = false });
    CHECK(render(1, 4096).starts_with("{\"id\":1,\"uptime\":4294967295,\"time\":4294967295,\"bed\":{\"target\":-32768,\"temp\":null},"
                                      "\"tool\":null,\"printing\":false,\"probed_points\":1,\"z_min\":-32.767,\"z_max\":32.767,"));

    // The longest entry: every token fits a buffer of 192 B, so it always fits an empty send buffer
    fill_mesh(0, -32.767f, -32.767f);
    for (auto &column : mesh) {
        for (auto &point : column) {
            point = -32.767f;
        }
    }
    mark_probed(0, 11);
    push(Meta { .uptime_s = 4294967295u, .time_s = 4294967295u, .bed_target = -32767, .bed_temp = -32767, .tool = 254, .printing = false });
    CHECK(render(0, 192).find("\"temp\":-3276.7},\"tool\":254,\"printing\":false,\"probed_points\":144,\"z_min\":-32.767,\"z_max\":-32.767}") != std::string::npos);
    CHECK(render(2, 192).find("[-32.767,-32.767,-32.767,-32.767,-32.767,-32.767,-32.767,-32.767,-32.767,-32.767,-32.767,-32.767]") != std::string::npos);
}

TEST_CASE("Mesh history JSON - entry not held") {
    reset_history();
    push_example();
    for (uint16_t id : { 3, 9, 65535 }) {
        CAPTURE(id);
        const Rendered rendered = render(id, 4096, [](const std::string &) {});
        CHECK(rendered.result == JsonResult::Abort);
    }
}

TEST_CASE("Mesh history JSON - entry replaced between chunks") {
    reset_history();
    push_example();
    bool replaced = false;
    const Rendered rendered = render(2, 256, [&](const std::string &json) {
        if (!replaced && json.find("\"z\":[") != std::string::npos) {
            // Entries 3 to 10; entry 10 takes the slot of entry 2
            for (int i = 1; i <= capacity; i++) {
                fill_mesh(i, -0.1f, 0.1f);
                push(Meta { .uptime_s = 6000u + i });
            }
            replaced = true;
        }
    });
    REQUIRE(replaced);
    CHECK(rendered.result == JsonResult::Abort);
}

TEST_CASE("Mesh history JSON - entry added during the list") {
    reset_history();
    for (int i = 1; i <= capacity; i++) {
        fill_mesh(i, -0.1f, 0.1f);
        mark_probed(0, 11);
        push(Meta { .uptime_s = 100u * i, .time_s = 1795750000u + i, .bed_target = 60, .bed_temp = 600, .tool = 1, .printing = true });
    }
    bool added = false;
    const Rendered rendered = render(0, 200, [&](const std::string &json) {
        if (!added && json.find("\"entries\":[") != std::string::npos) {
            // Entry 9 replaces entry 1
            fill_mesh(100, -0.1f, 0.1f);
            push(Meta { .uptime_s = 999 });
            added = true;
        }
    });
    REQUIRE(added);
    REQUIRE(rendered.result == JsonResult::Complete);
    CHECK(rendered.json.ends_with("}]}"));
    CHECK(count(rendered.json, "{\"id\":") == capacity - 1);
    for (int id = 2; id <= capacity; id++) {
        CAPTURE(id);
        CHECK(count(rendered.json, "{\"id\":" + std::to_string(id) + ",") == 1);
    }
    CHECK(count(rendered.json, "{\"id\":1,") == 0);
    CHECK(count(rendered.json, "{\"id\":9,") == 0);
    CHECK(count(rendered.json, "},{") == capacity - 2);
}
