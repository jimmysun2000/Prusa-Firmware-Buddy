#include <feature/chamber/chamber_requirements.hpp>

#include <catch2/catch_test_macros.hpp>

using buddy::ChamberRequirements;

namespace {

/// Chamber minimum, maximum and target of the stock presets
struct Preset {
    uint8_t min;
    uint8_t max;
    uint8_t target;
};

constexpr Preset pla { 15, 38, 20 };
constexpr Preset petg { 15, 45, 30 };
constexpr Preset asa { 40, 75, 70 };
constexpr Preset pc { 40, 80, 75 };

/// The threshold the firmware arms with (Chamber::closed_chamber_target_threshold)
constexpr float threshold = 45;

void add(ChamberRequirements &requirements, const Preset &preset) {
    requirements.add(preset.min, preset.max, preset.target);
}

} // namespace

TEST_CASE("chamber_requirements::empty") {
    const ChamberRequirements requirements;
    CHECK(!requirements.target.has_value());
    CHECK(!requirements.soak_goal.has_value());
    CHECK(!requirements.limit.has_value());
    CHECK(!requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::single_filament") {
    ChamberRequirements requirements;
    add(requirements, asa);
    CHECK(requirements.target == 70);
    CHECK(requirements.soak_goal == 40);
    CHECK(requirements.limit == 75);
    CHECK(requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::cool_filaments_do_not_arm") {
    for (const auto &preset : { pla, petg }) {
        ChamberRequirements requirements;
        add(requirements, preset);
        CHECK(!requirements.arms_heating(threshold));
    }
}

TEST_CASE("chamber_requirements::target_at_threshold_does_not_arm") {
    ChamberRequirements requirements;
    requirements.add(15, 50, 45);
    CHECK(!requirements.arms_heating(threshold));

    requirements.add(15, 50, 46);
    CHECK(requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::aggregates_over_tools") {
    ChamberRequirements requirements;
    add(requirements, asa);
    add(requirements, pc);
    CHECK(requirements.target == 75);
    CHECK(requirements.soak_goal == 40);
    CHECK(requirements.limit == 75);
    CHECK(requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::conflicting_maximum_disarms") {
    // PLA's maximum is below ASA's minimum: the chamber cannot suit both
    ChamberRequirements requirements;
    add(requirements, asa);
    add(requirements, pla);
    CHECK(requirements.target == 70);
    CHECK(requirements.soak_goal == 40);
    CHECK(requirements.limit == 38);
    CHECK(!requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::maximum_equal_to_goal_arms") {
    ChamberRequirements requirements;
    requirements.add(50, 80, 70);
    requirements.add(15, 50, 30);
    CHECK(requirements.limit == 50);
    CHECK(requirements.soak_goal == 50);
    CHECK(requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::missing_parameters_place_no_requirement") {
    ChamberRequirements requirements;
    requirements.add(std::nullopt, std::nullopt, std::nullopt);
    CHECK(!requirements.target.has_value());
    CHECK(!requirements.arms_heating(threshold));

    add(requirements, asa);
    requirements.add(std::nullopt, std::nullopt, std::nullopt);
    CHECK(requirements.target == 70);
    CHECK(requirements.soak_goal == 40);
    CHECK(requirements.limit == 75);
    CHECK(requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::zero_places_no_requirement") {
    // What the filament detail screen saves for unset chamber temperatures
    ChamberRequirements requirements;
    requirements.add(0, 0, 0);
    CHECK(!requirements.target.has_value());
    CHECK(!requirements.soak_goal.has_value());
    CHECK(!requirements.limit.has_value());

    add(requirements, asa);
    requirements.add(0, 0, 0);
    CHECK(requirements.target == 70);
    CHECK(requirements.soak_goal == 40);
    CHECK(requirements.limit == 75);
    CHECK(requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::target_without_minimum_arms") {
    ChamberRequirements requirements;
    requirements.add(std::nullopt, 60, 55);
    CHECK(!requirements.soak_goal.has_value());
    CHECK(requirements.arms_heating(threshold));
}
