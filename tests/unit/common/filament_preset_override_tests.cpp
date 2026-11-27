/// @file
#include <catch2/catch_test_macros.hpp>
#include <filament_preset_override.hpp>

#include <array>

using namespace filament_preset_override;

namespace {

/// Built-in values to test against; the XL presets use 15 to 80, and a preset could have none
constexpr std::array<std::optional<uint8_t>, 4> built_ins { 15, 40, 70, std::nullopt };

} // namespace

TEST_CASE("filament_preset_override: as built in reads as the built-in value") {
    for (const auto built_in : built_ins) {
        CHECK(decode(as_built_in, built_in) == built_in);
    }
}

TEST_CASE("filament_preset_override: zero stays zero") {
    // The filament detail screen saves None as 0, which places no chamber requirement
    for (const auto built_in : built_ins) {
        CHECK(encode(0, built_in) == 0);
        CHECK(decode(0, built_in) == std::optional<uint8_t>(0));
    }
}

TEST_CASE("filament_preset_override: equal or empty is stored as built in") {
    for (const auto built_in : built_ins) {
        CHECK(encode(std::nullopt, built_in) == as_built_in);
        CHECK(encode(built_in, built_in) == as_built_in);
    }
}

TEST_CASE("filament_preset_override: other values are stored as they are") {
    CHECK(encode(35, 40) == 35);
    CHECK(encode(41, 40) == 41);
    CHECK(encode(100, 70) == 100);
    CHECK(encode(1, std::nullopt) == 1);
}

TEST_CASE("filament_preset_override: round trip") {
    for (const auto built_in : built_ins) {
        for (int value = 0; value <= 254; value++) {
            const auto set = std::optional<uint8_t>(static_cast<uint8_t>(value));
            const uint8_t stored = encode(set, built_in);
            CHECK(decode(stored, built_in) == set);

            // A stored value is either as built in or differs from the built-in value
            CHECK((stored == as_built_in) == (set == built_in));
        }
    }
}
