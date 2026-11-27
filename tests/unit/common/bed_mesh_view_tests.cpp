#include <common/bed_mesh_view.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <string_view>

using namespace bed_mesh_view;
using Catch::Matchers::WithinAbs;

namespace {

// Same geometry as the XL: 36 x 36 points, major points 1, 4, ..., 34
constexpr Info xl_info { .nx = 36, .ny = 36, .border = 1, .major_step = 3, .min_x = 0, .min_y = 0, .max_x = 350, .max_y = 350 };

float mesh[36][36];
bool probed_marks[36][36];

float mesh_z(uint8_t ix, uint8_t iy) {
    return mesh[ix][iy];
}

bool mesh_probed(uint8_t ix, uint8_t iy) {
    return probed_marks[ix][iy];
}

void fill_mesh(float value) {
    for (auto &column : mesh) {
        for (auto &point : column) {
            point = value;
        }
    }
}

void clear_probed_marks() {
    for (auto &column : probed_marks) {
        for (auto &mark : column) {
            mark = false;
        }
    }
}

} // namespace

TEST_CASE("bed_mesh_view::is_major") {
    int count = 0;
    for (uint8_t i = 0; i < 36; i++) {
        if (is_major(i, 36, 1, 3)) {
            CHECK((i - 1) % 3 == 0);
            count++;
        }
    }
    CHECK(count == 12);
    CHECK_FALSE(is_major(0, 36, 1, 3));
    CHECK(is_major(1, 36, 1, 3));
    CHECK(is_major(34, 36, 1, 3));
    CHECK_FALSE(is_major(35, 36, 1, 3));
}

TEST_CASE("bed_mesh_view::compute_state") {
    fill_mesh(0);
    CHECK(compute_state(xl_info, mesh_z) == State::none);

    mesh[10][20] = 0.05f;
    CHECK(compute_state(xl_info, mesh_z) == State::complete);

    mesh[0][0] = NAN;
    CHECK(compute_state(xl_info, mesh_z) == State::partial);

    fill_mesh(NAN);
    CHECK(compute_state(xl_info, mesh_z) == State::partial);
}

TEST_CASE("bed_mesh_view::compute_revision") {
    fill_mesh(0);
    const uint32_t zero = compute_revision(xl_info, mesh_z);

    // All points turning NaN must be noticed (an even number of equal words would cancel out in a plain XOR)
    fill_mesh(NAN);
    CHECK(compute_revision(xl_info, mesh_z) != zero);

    // Swapping two different points must be noticed as well
    fill_mesh(0);
    mesh[1][1] = 0.1f;
    const uint32_t a = compute_revision(xl_info, mesh_z);
    mesh[1][1] = 0;
    mesh[4][4] = 0.1f;
    CHECK(compute_revision(xl_info, mesh_z) != a);

    CHECK(compute_revision(xl_info, mesh_z) == compute_revision(xl_info, mesh_z));

    // A probed mark changes the view without changing any value
    clear_probed_marks();
    const uint32_t unmarked = compute_revision(xl_info, mesh_z, mesh_probed);
    probed_marks[4][4] = true;
    const uint32_t marked = compute_revision(xl_info, mesh_z, mesh_probed);
    CHECK(marked != unmarked);
    probed_marks[4][4] = false;
    probed_marks[1][1] = true;
    CHECK(compute_revision(xl_info, mesh_z, mesh_probed) != marked);
    CHECK(compute_revision(xl_info, mesh_z, mesh_probed) == compute_revision(xl_info, mesh_z, mesh_probed));
}

TEST_CASE("bed_mesh_view::count_probed") {
    clear_probed_marks();
    CHECK(count_probed(xl_info, mesh_probed) == 0);

    probed_marks[0][0] = true;
    probed_marks[35][35] = true;
    probed_marks[1][34] = true;
    CHECK(count_probed(xl_info, mesh_probed) == 3);

    for (auto &column : probed_marks) {
        for (auto &mark : column) {
            mark = true;
        }
    }
    CHECK(count_probed(xl_info, mesh_probed) == 36 * 36);
}

TEST_CASE("bed_mesh_view::compute_stats") {
    SECTION("only major points count") {
        fill_mesh(0);
        // Minor and border points are ignored, however extreme
        mesh[0][0] = 5;
        mesh[2][2] = -5;
        mesh[35][35] = 5;
        mesh[1][1] = 0.2f;
        mesh[34][4] = -0.4f;

        const auto stats = compute_stats(xl_info, mesh_z);
        CHECK(stats.count == 144);
        CHECK_THAT(stats.min, WithinAbs(-0.4, 1e-6));
        CHECK_THAT(stats.max, WithinAbs(0.2, 1e-6));
        CHECK_THAT(stats.mean, WithinAbs(-0.2 / 144, 1e-6));
        CHECK_THAT(stats.full_scale, WithinAbs(0.4 - 0.2 / 144, 1e-6));
    }

    SECTION("NaN points are skipped") {
        fill_mesh(NAN);
        mesh[1][1] = 0.01f;
        mesh[4][1] = 0.03f;
        const auto stats = compute_stats(xl_info, mesh_z);
        CHECK(stats.count == 2);
        CHECK_THAT(stats.mean, WithinAbs(0.02, 1e-6));
        // Deviations are tiny, the scale does not go below its minimum
        CHECK(stats.full_scale == min_full_scale);
    }

    SECTION("no values") {
        fill_mesh(NAN);
        const auto stats = compute_stats(xl_info, mesh_z);
        CHECK(stats.count == 0);
        CHECK(stats.full_scale == min_full_scale);
    }
}

TEST_CASE("bed_mesh_view::color_step") {
    constexpr float fs = 0.1f;
    CHECK(color_step(0, fs) == color_steps / 2);
    CHECK(color_step(-0.001f, fs) == color_steps / 2 - 1);
    CHECK(color_step(fs, fs) == color_steps - 1);
    CHECK(color_step(-fs, fs) == 0);
    CHECK(color_step(10 * fs, fs) == color_steps - 1);
    CHECK(color_step(-10 * fs, fs) == 0);
    CHECK(color_step(NAN, fs) == color_steps / 2);
    CHECK(color_step(0.05f, 0) == color_steps / 2);

    // Monotonic
    uint8_t last = 0;
    for (int i = -120; i <= 120; i++) {
        const uint8_t step = color_step(i * 0.001f, fs);
        CHECK(step >= last);
        last = step;
    }

    // Symmetric around zero (away from the step boundaries, where the half-open steps differ)
    for (int i = 1; i < 100; i++) {
        const float dev = (i + 0.3f) * 0.001f;
        CHECK(color_step(dev, fs) + color_step(-dev, fs) == color_steps - 1);
    }
}

TEST_CASE("bed_mesh_view::color_of_step") {
    const Rgb lowest = color_of_step(0);
    const Rgb highest = color_of_step(color_steps - 1);
    CHECK(lowest.b == 255);
    CHECK(lowest.r == 0);
    CHECK(highest.r == 255);
    CHECK(highest.b == 0);

    // The middle steps are close to white, the outer ones saturated
    const Rgb mid_low = color_of_step(color_steps / 2 - 1);
    const Rgb mid_high = color_of_step(color_steps / 2);
    CHECK(mid_low.r > 200);
    CHECK(mid_low.g > 200);
    CHECK(mid_high.g > 200);
    CHECK(mid_high.b > 200);

    // Out of range clamps
    const Rgb clamped = color_of_step(200);
    CHECK(clamped.r == highest.r);
    CHECK(clamped.g == highest.g);
    CHECK(clamped.b == highest.b);
}

TEST_CASE("bed_mesh_view::format_json_value") {
    JsonValueBuffer buffer;
    CHECK(std::string_view(format_json_value(NAN, buffer)) == "null");
    CHECK(std::string_view(format_json_value(INFINITY, buffer)) == "null");
    CHECK(std::string_view(format_json_value(-INFINITY, buffer)) == "null");
    CHECK(std::string_view(format_json_value(1e9f, buffer)) == "null");
    CHECK(std::string_view(format_json_value(0, buffer)) == "0.000");
    CHECK(std::string_view(format_json_value(-0.0004f, buffer)) == "0.000");
    CHECK(std::string_view(format_json_value(0.1234f, buffer)) == "0.123");
    CHECK(std::string_view(format_json_value(-0.0456f, buffer)) == "-0.046");
    CHECK(std::string_view(format_json_value(-1.5f, buffer)) == "-1.500");
    CHECK(std::string_view(format_json_value(12.0006f, buffer)) == "12.001");
    CHECK(std::string_view(format_json_value(-999999.0f, buffer)) == "-999999.000");
}

TEST_CASE("bed_mesh_view::to_str") {
    CHECK(std::string_view(to_str(State::none)) == "none");
    CHECK(std::string_view(to_str(State::partial)) == "partial");
    CHECK(std::string_view(to_str(State::complete)) == "complete");
}
