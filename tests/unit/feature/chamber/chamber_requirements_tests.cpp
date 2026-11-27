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

TEST_CASE("chamber_requirements::cooldown_rate_global_only") {
    ChamberRequirements requirements;
    CHECK(requirements.effective_cooldown_rate(10) == 10);

    requirements.add_cooldown_rate(std::nullopt);
    requirements.add_cooldown_rate(0);
    CHECK(!requirements.cooldown_rate.has_value());
    CHECK(requirements.uses_global_cooldown_rate);
    CHECK(requirements.effective_cooldown_rate(10) == 10);
    CHECK(requirements.effective_cooldown_rate(25) == 25);
}

TEST_CASE("chamber_requirements::cooldown_rate_slowest_wins") {
    ChamberRequirements requirements;
    requirements.add_cooldown_rate(20);
    requirements.add_cooldown_rate(8);
    requirements.add_cooldown_rate(15);
    CHECK(requirements.cooldown_rate == 8);
    CHECK(!requirements.uses_global_cooldown_rate);

    // All filaments have their own rate, the setting does not matter
    CHECK(requirements.effective_cooldown_rate(5) == 8);
    CHECK(requirements.effective_cooldown_rate(30) == 8);

    // A filament using the setting brings it in
    requirements.add_cooldown_rate(std::nullopt);
    CHECK(requirements.effective_cooldown_rate(5) == 5);
    CHECK(requirements.effective_cooldown_rate(10) == 8);
}

TEST_CASE("chamber_requirements::cooldown_rate_off") {
    // Cooldown Rate Off turns the cool-down off, whatever the filaments say
    ChamberRequirements requirements;
    CHECK(requirements.effective_cooldown_rate(0) == 0);

    requirements.add_cooldown_rate(12);
    CHECK(requirements.effective_cooldown_rate(0) == 0);

    requirements.add_cooldown_rate(std::nullopt);
    CHECK(requirements.effective_cooldown_rate(0) == 0);
}

TEST_CASE("chamber_requirements::target_without_minimum_arms") {
    ChamberRequirements requirements;
    requirements.add(std::nullopt, 60, 55);
    CHECK(!requirements.soak_goal.has_value());
    CHECK(requirements.arms_heating(threshold));
}

// Values the user can set for a preset on the printer, None being saved as 0

TEST_CASE("chamber_requirements::edited_maximum_below_minimum_does_not_arm") {
    ChamberRequirements requirements;
    add(requirements, { .min = 60, .max = 50, .target = 70 });
    CHECK(requirements.soak_goal == 60);
    CHECK(requirements.limit == 50);
    CHECK(!requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::edited_minimum_none_arms_without_soak") {
    ChamberRequirements requirements;
    add(requirements, { .min = 0, .max = 75, .target = 70 });
    CHECK(!requirements.soak_goal.has_value());
    CHECK(requirements.limit == 75);
    CHECK(requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::edited_maximum_none_arms_without_limit") {
    ChamberRequirements requirements;
    add(requirements, { .min = 40, .max = 0, .target = 70 });
    CHECK(requirements.soak_goal == 40);
    CHECK(!requirements.limit.has_value());
    CHECK(requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::edited_nominal_none_does_not_arm") {
    ChamberRequirements requirements;
    add(requirements, { .min = 40, .max = 75, .target = 0 });
    CHECK(!requirements.target.has_value());
    CHECK(!requirements.arms_heating(threshold));
}

TEST_CASE("chamber_requirements::edited_pla_nominal_arms") {
    // Any preset with a Nominal above the threshold heats, PLA included
    ChamberRequirements requirements;
    add(requirements, { .min = pla.min, .max = pla.max, .target = 50 });
    CHECK(requirements.target == 50);
    CHECK(requirements.arms_heating(threshold));
}
