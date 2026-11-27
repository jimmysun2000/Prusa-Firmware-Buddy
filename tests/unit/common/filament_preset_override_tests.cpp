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

namespace {

constexpr Adjustable values(uint8_t min, uint8_t max, uint8_t target, std::optional<uint8_t> cooldown_rate = std::nullopt) {
    return Adjustable {
        .chamber_min_temperature = min,
        .chamber_max_temperature = max,
        .chamber_target_temperature = target,
        .cooldown_rate = cooldown_rate,
    };
}

constexpr Adjustable asa_built_in = values(40, 75, 70);
constexpr Adjustable asa_edited = values(35, 75, 70);

} // namespace

TEST_CASE("filament_preset_override: a copy with the values from before the edit follows") {
    CHECK(copy_follows_edit("ASA", asa_built_in, "ASA", asa_built_in, asa_edited));

    // And back, on restoring the built-in values
    CHECK(copy_follows_edit("ASA", asa_edited, "ASA", asa_edited, asa_built_in));
}

TEST_CASE("filament_preset_override: nothing follows without a change") {
    CHECK(!copy_follows_edit("ASA", asa_built_in, "ASA", asa_built_in, asa_built_in));
}

TEST_CASE("filament_preset_override: a copy of another name keeps its values") {
    // Renamed by the user, or copied from another filament
    CHECK(!copy_follows_edit("ASA2", asa_built_in, "ASA", asa_built_in, asa_edited));
    CHECK(!copy_follows_edit("CUSTOM", asa_built_in, "ASA", asa_built_in, asa_edited));
}

TEST_CASE("filament_preset_override: a copy the user changed keeps its values") {
    for (const auto changed : { values(45, 75, 70), values(40, 80, 70), values(40, 75, 0) }) {
        CHECK(!copy_follows_edit("ASA", changed, "ASA", asa_built_in, asa_edited));
    }
}

TEST_CASE("filament_preset_override: a copy made after the edit is left alone") {
    CHECK(!copy_follows_edit("ASA", asa_edited, "ASA", asa_built_in, asa_edited));
}

TEST_CASE("filament_preset_override: None counts as a value") {
    // The filament detail screen saves None as 0
    constexpr Adjustable no_soak = values(0, 75, 70);
    CHECK(copy_follows_edit("ASA", no_soak, "ASA", no_soak, asa_built_in));
    CHECK(!copy_follows_edit("ASA", asa_built_in, "ASA", no_soak, asa_edited));
}

TEST_CASE("filament_preset_override: the bed cool-down rate counts as a value") {
    constexpr Adjustable slow = values(40, 75, 70, 5);

    // An edit of the rate alone is carried
    CHECK(copy_follows_edit("ASA", asa_built_in, "ASA", asa_built_in, slow));

    // A copy whose rate the user set keeps it, also when the chamber temperatures are edited
    CHECK(!copy_follows_edit("ASA", slow, "ASA", asa_built_in, asa_edited));
}
