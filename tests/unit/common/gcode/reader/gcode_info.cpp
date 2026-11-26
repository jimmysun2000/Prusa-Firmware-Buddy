#include "test_files.hpp"

#include <gcode_reader_any.hpp>
#include <gcode_reader_binary.hpp>
#include <gcode_info.hpp>
#include <gcode_buffer.hpp>
#include <gui/print_layer_info.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstring>

namespace {

struct TestFile {
    const char *path;
    bool encrypted;
    // Any info or metadata available.
    bool has_any_info;
};

// Test both on plaintext and binary readers. They behave slightly differently
// on the outside. No need to do different compressions or so, though, these shoud be the same.
const std::vector<TestFile> test_files = {
    { NEW_PLAIN, false, true },
    { NEW_BINARY, false, true },
    { NEW_BINARY_META_AFTER, false, true },
    { NEW_BINARY_META_BEFORE, false, true },
    { NEW_ENCRYPTED, true, true },
    { NEW_ENCRYPTED_MULTI, true, true },
    { NEW_ENCRYPTED_POLY, true, true },
    // With a fully encrypted gcode, we don't "see" the metadata, as it is encrypted.
    { NEW_ENCRYPTED_FULLY, true, false },
    { NEW_SIGNED, /* technically, yes, encrypted */ true, true },
};

} // namespace

TEST_CASE("GCodeInfo") {
    for (const auto &def : test_files) {
        const char *filename = def.path;
        SECTION(std::string("Test-file: ") + filename) {
            AnyGcodeFormatReader reader(filename);
            REQUIRE(reader.is_open());

            GCodeInfo info;
            info.load(*reader.get());
            CHECK(info.is_loaded());
            CHECK(!info.has_error());

            CHECK(info.has_preview_thumbnail() == def.has_any_info);
            CHECK(info.has_progress_thumbnail() == def.has_any_info);
            CHECK(info.has_filament_described() == def.has_any_info);
            if (!def.encrypted) {
                // BUG: These are in the encrypted section. The gcode info
                // doesn't open this (on purpose). But these are not accessible
                // for us in this case.
                CHECK(info.get_bed_preheat_temp() == 60);
                CHECK(info.get_hotend_preheat_temp() == 215);
            }

            if (def.has_any_info) {
                CHECK(info.is_singletool_gcode());
                CHECK(info.UsedExtrudersCount() == 1);
                auto extruder_info = info.get_extruder_info(0);
                CHECK(extruder_info.used());
                CHECK(std::string_view(extruder_info.filament_name) == "PLA");
                CHECK(extruder_info.requires_hardened_nozzle == Tristate::other);
                CHECK(extruder_info.requires_high_flow_nozzle == Tristate::other);
            }
        }
    }
}

TEST_CASE("GCodeInfo::total_toolchanges") {
    auto load = [](const char *filename) {
        AnyGcodeFormatReader reader(filename);
        REQUIRE(reader.is_open());
        GCodeInfo info;
        info.load(*reader.get());
        return info.get_total_toolchanges();
    };

    // Present in the metadata -> parsed.
    CHECK(load(TOTAL_TOOLCHANGES) == 868);
    // Absent (single-tool gcode) -> unset.
    CHECK(load(NEW_PLAIN) == std::nullopt);
}

TEST_CASE("GCodeInfo::layer metadata") {
    for (const auto &def : test_files) {
        const char *filename = def.path;
        SECTION(std::string("Test-file: ") + filename) {
            AnyGcodeFormatReader reader(filename);
            REQUIRE(reader.is_open());
            GCodeInfo info;
            info.load(*reader.get());

            if (def.has_any_info) {
                REQUIRE(info.get_layer_height().has_value());
                REQUIRE(info.get_max_layer_z().has_value());
                CHECK_THAT(*info.get_layer_height(), Catch::Matchers::WithinAbs(0.2, 0.0001));
                CHECK_THAT(*info.get_max_layer_z(), Catch::Matchers::WithinAbs(18.0, 0.0001));
            } else {
                CHECK(info.get_layer_height() == std::nullopt);
                CHECK(info.get_max_layer_z() == std::nullopt);
            }
        }
    }

    SECTION("Absent") {
        AnyGcodeFormatReader reader(TOTAL_TOOLCHANGES);
        REQUIRE(reader.is_open());
        GCodeInfo info;
        info.load(*reader.get());
        CHECK(info.get_layer_height() == std::nullopt);
        CHECK(info.get_max_layer_z() == std::nullopt);
    }
}

TEST_CASE("estimate_layers") {
    auto layers = [](float layer_height, float max_layer_z, float printed_z) {
        const auto result = estimate_layers(layer_height, max_layer_z, printed_z);
        REQUIRE(result.has_value());
        return std::pair { result->current, result->total };
    };

    // Box sample: 18 mm at 0.2 mm
    CHECK(layers(0.2f, 18.0f, 0.2f) == std::pair<uint16_t, uint16_t> { 1, 90 });
    CHECK(layers(0.2f, 18.0f, 7.4f) == std::pair<uint16_t, uint16_t> { 37, 90 });
    CHECK(layers(0.2f, 18.0f, 18.0f) == std::pair<uint16_t, uint16_t> { 90, 90 });

    // Rounding to the nearest layer absorbs float error and small Z offsets
    CHECK(layers(0.15f, 4.5f, 2.9999f) == std::pair<uint16_t, uint16_t> { 20, 30 });
    CHECK(layers(0.2f, 18.0f, 7.45f) == std::pair<uint16_t, uint16_t> { 37, 90 });

    // A 0.32 mm first layer under 89 layers of 0.2 mm: the estimate is one layer high
    CHECK(layers(0.2f, 18.12f, 0.32f) == std::pair<uint16_t, uint16_t> { 2, 91 });

    // Clamped into [1, total]
    CHECK(layers(0.2f, 18.0f, 0.05f) == std::pair<uint16_t, uint16_t> { 1, 90 });
    CHECK(layers(0.2f, 18.0f, 25.0f) == std::pair<uint16_t, uint16_t> { 90, 90 });
    CHECK(layers(0.2f, 0.2f, 0.05f) == std::pair<uint16_t, uint16_t> { 1, 1 });

    // Nothing printed yet or unusable metadata
    CHECK(estimate_layers(0.2f, 18.0f, 0.0f) == std::nullopt);
    CHECK(estimate_layers(0.2f, 0.05f, 0.05f) == std::nullopt);
    CHECK(estimate_layers(0.2f, 18.0f, -1.0f) == std::nullopt);
    CHECK(estimate_layers(0.0f, 18.0f, 1.0f) == std::nullopt);
    CHECK(estimate_layers(0.2f, 0.0f, 1.0f) == std::nullopt);
    CHECK(estimate_layers(NAN, 18.0f, 1.0f) == std::nullopt);
    CHECK(estimate_layers(0.2f, NAN, 1.0f) == std::nullopt);
    CHECK(estimate_layers(0.2f, 18.0f, NAN) == std::nullopt);
    CHECK(estimate_layers(0.2f, INFINITY, 1.0f) == std::nullopt);
    CHECK(estimate_layers(0.0001f, 18.0f, 1.0f) == std::nullopt);
}

TEST_CASE("GCodeInfo::is_valid_for_print", "[slow]") {
    struct stat st;
    REQUIRE(stat(NEW_ENCRYPTED, &st) == 0);

    size_t seen_printable_cnt = 0;
    for (size_t i = 0; i < (size_t)st.st_size - 1000 /* It is printable sooner than at the end */; i++) {
        INFO("Prefix size: " << i);
        unique_file_ptr f(fopen(NEW_ENCRYPTED, "rb"));
        REQUIRE(f);
        PrusaPackGcodeReader reader(std::move(f), st);
        transfers::PartialFile::State validity;
        validity.total_size = st.st_size;
        validity.extend_head(i);

        reader.set_validity(validity);

        GCodeInfo info;
        if (info.check_valid_for_print(reader)) {
            // Twice in a row on the same one should still work.
            if (!info.check_valid_for_print(reader)) {
                INFO("Failed with " << seen_printable_cnt);
                INFO("Error: " << (info.error_str() ?: ""));
                FAIL();
            }

            seen_printable_cnt++;

            if (seen_printable_cnt > 300) {
                // Just to make the test run faster... no need to keep trying
                // deep into the actual gcode area.
                break;
            }

            // Now, retry with decryption allowed, see there's no difference.
            unique_file_ptr f2(fopen(NEW_ENCRYPTED, "rb"));
            REQUIRE(f2);
            PrusaPackGcodeReader reader2(std::move(f2), st, true);

            reader2.set_validity(validity);

            REQUIRE(info.check_valid_for_print(reader2));
        } else {
            // Once the file becomes printable, it _stays_ printable even if we
            // add more bytes to it.
            REQUIRE(seen_printable_cnt == 0);
        }

        // No matter what, it shouldn't result in an error - only that it's not
        // printable _yet_.
        REQUIRE(!info.error_str());
    }

    REQUIRE(seen_printable_cnt > 0);
}
