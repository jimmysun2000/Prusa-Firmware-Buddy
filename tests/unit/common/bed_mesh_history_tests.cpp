#include <common/bed_mesh_history.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using namespace bed_mesh_history;
using bed_mesh_view::Info;

namespace {

// Same geometry as the XL: 36 x 36 points, major points 1, 4, ..., 34
constexpr Info xl_info { .nx = 36, .ny = 36, .border = 1, .major_step = 3, .min_x = 0, .min_y = 0, .max_x = 360, .max_y = 360 };

float mesh[36][36];
bool probed_marks[36][36];

float mesh_z(uint8_t ix, uint8_t iy) {
    return mesh[ix][iy];
}

bool mesh_probed(uint8_t ix, uint8_t iy) {
    return probed_marks[ix][iy];
}

constexpr uint8_t grid_index(uint8_t i) {
    return 1 + 3 * i;
}

/// Major point ix, iy holds (ix + 100 * iy + offset) µm, every major point is probed
void fill_mesh(int offset = 0) {
    for (uint8_t x = 0; x < 36; x++) {
        for (uint8_t y = 0; y < 36; y++) {
            mesh[x][y] = 0.5f;
            probed_marks[x][y] = false;
        }
    }
    for (uint8_t ix = 0; ix < points_x; ix++) {
        for (uint8_t iy = 0; iy < points_y; iy++) {
            mesh[grid_index(ix)][grid_index(iy)] = (ix + 100 * iy + offset) * 0.001f;
            probed_marks[grid_index(ix)][grid_index(iy)] = true;
        }
    }
}

Meta make_meta(uint32_t uptime_s) {
    return Meta { .uptime_s = uptime_s, .time_s = 1795750330, .bed_target = 60, .bed_temp = 598, .tool = 2, .printing = true };
}

bool push(Store &store, uint32_t uptime_s = 100) {
    const bool pushed = store.push_if_measured(xl_info, mesh_z, mesh_probed, make_meta(uptime_s));
    CHECK(freertos::Mutex::locked_mutex_count == 0);
    return pushed;
}

} // namespace

TEST_CASE("bed_mesh_history::to_um") {
    CHECK(to_um(0) == 0);
    CHECK(to_um(0.1234f) == 123);
    CHECK(to_um(0.1236f) == 124);
    CHECK(to_um(-0.0456f) == -46);
    CHECK(to_um(-0.0004f) == 0);
    CHECK(to_um(1.5f) == 1500);
    CHECK(to_um(32.767f) == 32767);
    CHECK(to_um(-32.767f) == -32767);
    CHECK(to_um(40) == 32767);
    CHECK(to_um(-40) == -32767);
    CHECK(to_um(INFINITY) == 32767);
    CHECK(to_um(-INFINITY) == -32767);
    CHECK(to_um(NAN) == no_value_um);
}

TEST_CASE("bed_mesh_history::Store - empty") {
    auto store = std::make_unique<Store>();
    Summary summary;
    Row row;
    CHECK(store->newest_id() == 0);
    CHECK_FALSE(store->summary(0, summary));
    CHECK_FALSE(store->summary(1, summary));
    CHECK_FALSE(store->row(1, 0, row));
    CHECK(freertos::Mutex::locked_mutex_count == 0);
}

TEST_CASE("bed_mesh_history::Store - a measured mesh is stored") {
    auto store = std::make_unique<Store>();
    fill_mesh();
    // Some points probed: only the marks of the major points count
    probed_marks[grid_index(3)][grid_index(0)] = false;
    probed_marks[0][0] = true;
    mesh[grid_index(5)][grid_index(7)] = -0.25f;
    mesh[grid_index(11)][grid_index(11)] = 1.2f;

    REQUIRE(push(*store, 1234));
    CHECK(store->newest_id() == 1);

    Summary summary;
    REQUIRE(store->summary(1, summary));
    CHECK(summary.id == 1);
    CHECK(summary.meta.uptime_s == 1234);
    CHECK(summary.meta.time_s == 1795750330);
    CHECK(summary.meta.bed_target == 60);
    CHECK(summary.meta.bed_temp == 598);
    CHECK(summary.meta.tool == 2);
    CHECK(summary.meta.printing);
    CHECK(summary.probed_count == points - 1);
    CHECK(summary.z_min_um == -250);
    CHECK(summary.z_max_um == 1200);

    Row row;
    REQUIRE(store->row(1, 0, row));
    for (uint8_t ix = 0; ix < points_x; ix++) {
        CHECK(row.z_um[ix] == ix);
    }
    CHECK(row.probed == (0xFFF & ~(1 << 3)));

    REQUIRE(store->row(1, 7, row));
    CHECK(row.z_um[4] == 704);
    CHECK(row.z_um[5] == -250);
    CHECK(row.probed == 0xFFF);

    REQUIRE(store->row(1, 11, row));
    CHECK(row.z_um[11] == 1200);

    CHECK_FALSE(store->row(1, points_y, row));
    CHECK_FALSE(store->summary(2, summary));
    CHECK_FALSE(store->row(2, 0, row));
    CHECK(freertos::Mutex::locked_mutex_count == 0);
}

TEST_CASE("bed_mesh_history::Store - meshes that are not a measurement are refused") {
    auto store = std::make_unique<Store>();

    SECTION("all zero") {
        for (auto &column : mesh) {
            for (auto &point : column) {
                point = 0;
            }
        }
        for (auto &column : probed_marks) {
            for (auto &mark : column) {
                mark = true;
            }
        }
        CHECK_FALSE(push(*store));
    }

    SECTION("a point without a value") {
        fill_mesh();
        // Not even a major point
        mesh[0][35] = NAN;
        CHECK_FALSE(push(*store));
    }

    SECTION("no probed major point") {
        fill_mesh();
        for (auto &column : probed_marks) {
            for (auto &mark : column) {
                mark = false;
            }
        }
        // Points that are not major ones are not stored
        probed_marks[0][0] = true;
        probed_marks[2][2] = true;
        CHECK_FALSE(push(*store));
    }

    SECTION("a different geometry") {
        fill_mesh();
        Info info = xl_info;
        info.nx = 33;
        CHECK_FALSE(store->push_if_measured(info, mesh_z, mesh_probed, make_meta(1)));
    }

    CHECK(store->newest_id() == 0);
}

TEST_CASE("bed_mesh_history::Store - an unchanged mesh is refused") {
    auto store = std::make_unique<Store>();
    fill_mesh();
    REQUIRE(push(*store));
    CHECK_FALSE(push(*store, 200));

    // Points that are not stored do not count, nor do differences below 0.5 µm
    mesh[0][0] = 3;
    mesh[2][2] = -3;
    mesh[grid_index(1)][grid_index(1)] += 0.0003f;
    CHECK_FALSE(push(*store, 300));
    CHECK(store->newest_id() == 1);

    SECTION("a changed height") {
        mesh[grid_index(6)][grid_index(2)] += 0.001f;
        CHECK(push(*store, 400));
        CHECK(store->newest_id() == 2);
    }

    SECTION("a changed probed mark") {
        probed_marks[grid_index(6)][grid_index(2)] = false;
        CHECK(push(*store, 400));
        CHECK(store->newest_id() == 2);
    }
}

TEST_CASE("bed_mesh_history::Store - the oldest entry is replaced") {
    auto store = std::make_unique<Store>();
    for (int i = 1; i <= capacity + 1; i++) {
        fill_mesh(i);
        REQUIRE(push(*store, i * 10));
    }
    CHECK(store->newest_id() == capacity + 1);

    Summary summary;
    Row row;
    CHECK_FALSE(store->summary(1, summary));
    CHECK_FALSE(store->row(1, 0, row));
    for (uint16_t id = 2; id <= capacity + 1; id++) {
        CAPTURE(id);
        REQUIRE(store->summary(id, summary));
        CHECK(summary.id == id);
        CHECK(summary.meta.uptime_s == id * 10u);
        REQUIRE(store->row(id, 0, row));
        // Point 0, 0 holds the offset the mesh was filled with
        CHECK(row.z_um[0] == id);
    }
    CHECK_FALSE(store->summary(capacity + 2, summary));
    CHECK(freertos::Mutex::locked_mutex_count == 0);
}
