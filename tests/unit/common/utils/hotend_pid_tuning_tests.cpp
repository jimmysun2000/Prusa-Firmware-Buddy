#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <hotend_pid_tuning.hpp>

using namespace hotend_pid;

namespace {

constexpr float TARGET = 215.0f;
constexpr float RELAY_HALF_OUTPUT = 127.5f;
constexpr uint32_t SAMPLE_PERIOD_MS = 250;

/// Feeds the recorder with whole-degree samples of @p temp_at, as the Dwarf reports them
void feed(RelayOscillationRecorder &recorder, const std::function<float(float)> &temp_at, uint32_t duration_ms) {
    for (uint32_t time_ms = 0; time_ms <= duration_ms; time_ms += SAMPLE_PERIOD_MS) {
        recorder.add_sample(time_ms, std::floor(temp_at(static_cast<float>(time_ms) / 1000.0f)));
    }
}

/// Heats up linearly from 25 °C, then oscillates as a sine around the target
std::function<float(float)> heat_up_then_sine(float amplitude, float period_s) {
    constexpr float heat_up_s = 60.0f;
    return [=](float time_s) {
        if (time_s < heat_up_s) {
            return 25.0f + (TARGET - 25.0f) * time_s / heat_up_s;
        }
        return TARGET + amplitude * std::sin(2 * std::numbers::pi_v<float> * (time_s - heat_up_s) / period_s);
    };
}

std::array<OscillationCycle, RelayOscillationRecorder::record_cycles> uniform_cycles(uint32_t period_ms, float max_temp, float min_temp) {
    std::array<OscillationCycle, RelayOscillationRecorder::record_cycles> cycles;
    cycles.fill({ .period_ms = period_ms, .max_temp = max_temp, .min_temp = min_temp });
    return cycles;
}

} // namespace

TEST_CASE("hotend_pid_tuning: nothing is recorded before the target is crossed") {
    RelayOscillationRecorder recorder(TARGET);
    feed(
        recorder, [](float time_s) { return 25.0f + 3.0f * time_s; }, 60'000);

    CHECK_FALSE(recorder.has_reached_target());
    CHECK(recorder.cycle_count() == 0);
    CHECK(recorder.recorded_cycles().empty());
}

TEST_CASE("hotend_pid_tuning: one degree of noise around the target is not a cycle") {
    RelayOscillationRecorder recorder(TARGET);
    recorder.add_sample(0, TARGET - 1);
    recorder.add_sample(1'000, TARGET + 1);
    CHECK(recorder.has_reached_target());

    for (uint32_t time_ms = 2'000; time_ms < 60'000; time_ms += 1'000) {
        recorder.add_sample(time_ms, (time_ms / 1'000) % 2 ? TARGET : TARGET + 1);
    }
    CHECK(recorder.cycle_count() == 0);
}

TEST_CASE("hotend_pid_tuning: a hot start waits for the temperature to fall below the target") {
    RelayOscillationRecorder recorder(TARGET);
    recorder.add_sample(0, TARGET + 20);
    recorder.add_sample(1'000, TARGET + 5);
    CHECK_FALSE(recorder.has_reached_target());

    recorder.add_sample(2'000, TARGET - 1);
    recorder.add_sample(3'000, TARGET + 1);
    CHECK(recorder.has_reached_target());
}

TEST_CASE("hotend_pid_tuning: a sine is split into its cycles") {
    RelayOscillationRecorder recorder(TARGET);
    feed(recorder, heat_up_then_sine(3.0f, 20.0f), 600'000);

    REQUIRE(recorder.is_complete());
    CHECK(recorder.cycle_count() == RelayOscillationRecorder::total_cycles);
    REQUIRE(recorder.recorded_cycles().size() == RelayOscillationRecorder::record_cycles);
    for (const OscillationCycle &cycle : recorder.recorded_cycles()) {
        CHECK(cycle.period_ms >= 20'000 - SAMPLE_PERIOD_MS);
        CHECK(cycle.period_ms <= 20'000 + SAMPLE_PERIOD_MS);
        CHECK(cycle.max_temp == TARGET + 3);
        CHECK(cycle.min_temp == TARGET - 3);
    }
}

TEST_CASE("hotend_pid_tuning: the settling cycles are not recorded") {
    // The first rise through the target is at 20 s; the two cycles from there swing by
    // 10 °C, the following ones by 3 °C
    const auto temp_at = [](float time_s) {
        const float amplitude = time_s < 60.0f ? 10.0f : 3.0f;
        return TARGET + amplitude * std::sin(2 * std::numbers::pi_v<float> * time_s / 20.0f);
    };
    RelayOscillationRecorder recorder(TARGET);
    feed(recorder, temp_at, 600'000);

    REQUIRE(recorder.is_complete());
    for (const OscillationCycle &cycle : recorder.recorded_cycles()) {
        CHECK(cycle.max_temp == TARGET + 3);
        CHECK(cycle.min_temp == TARGET - 3);
    }
}

TEST_CASE("hotend_pid_tuning: an asymmetric oscillation keeps its period and swing") {
    // Rises 1 °C/s from 212 to 219 °C in 7 s, falls back in 20 s
    const auto temp_at = [](float time_s) {
        constexpr float rise_s = 7.0f;
        constexpr float period_s = rise_s + 20.0f;
        const float phase_s = std::fmod(time_s, period_s);
        return phase_s < rise_s ? 212.0f + phase_s : 219.0f - 7.0f * (phase_s - rise_s) / 20.0f;
    };
    RelayOscillationRecorder recorder(TARGET);
    feed(recorder, temp_at, 600'000);

    REQUIRE(recorder.is_complete());
    for (const OscillationCycle &cycle : recorder.recorded_cycles()) {
        CHECK(cycle.period_ms == Catch::Approx(27'000.0).margin(SAMPLE_PERIOD_MS));
        CHECK(cycle.peak_to_peak() >= 6.0f);
        CHECK(cycle.peak_to_peak() <= 7.0f);
    }

    const TuningResult result = compute_gains(recorder.recorded_cycles(), RELAY_HALF_OUTPUT);
    CHECK(result.failure == TuningFailure::none);
}

TEST_CASE("hotend_pid_tuning: samples after completion are ignored") {
    RelayOscillationRecorder recorder(TARGET);
    feed(recorder, heat_up_then_sine(3.0f, 20.0f), 600'000);
    REQUIRE(recorder.is_complete());
    const OscillationCycle last = recorder.recorded_cycles().back();

    recorder.add_sample(700'000, TARGET - 50);
    recorder.add_sample(701'000, TARGET + 50);
    CHECK(recorder.cycle_count() == RelayOscillationRecorder::total_cycles);
    CHECK(recorder.recorded_cycles().back().max_temp == last.max_temp);
    CHECK(recorder.recorded_cycles().back().min_temp == last.min_temp);
}

TEST_CASE("hotend_pid_tuning: gains follow the Ziegler-Nichols rule") {
    const auto cycles = uniform_cycles(20'000, TARGET + 3, TARGET - 3);
    const TuningResult result = compute_gains(cycles, RELAY_HALF_OUTPUT);

    // a = 3 °C, Tu = 20 s
    const double ku = 4 * 127.5 / (std::numbers::pi * 3);
    const double kp = 0.6 * ku;
    REQUIRE(result.failure == TuningFailure::none);
    CHECK(result.gains.kp == Catch::Approx(kp));
    CHECK(result.gains.ki == Catch::Approx(2 * kp / 20));
    CHECK(result.gains.kd == Catch::Approx(kp * 20 / 8));
    CHECK(is_valid(result.gains));
}

TEST_CASE("hotend_pid_tuning: a small swing is rejected") {
    CHECK(compute_gains(uniform_cycles(20'000, TARGET + 2, TARGET - 1), RELAY_HALF_OUTPUT).failure == TuningFailure::amplitude_too_small);
    CHECK(compute_gains(uniform_cycles(20'000, TARGET + 2, TARGET - 2), RELAY_HALF_OUTPUT).failure != TuningFailure::amplitude_too_small);
}

TEST_CASE("hotend_pid_tuning: cycles with different periods are rejected") {
    auto cycles = uniform_cycles(20'000, TARGET + 3, TARGET - 3);
    cycles[2].period_ms = 24'500;
    CHECK(compute_gains(cycles, RELAY_HALF_OUTPUT).failure == TuningFailure::cycles_disagree);

    cycles[2].period_ms = 23'500;
    CHECK(compute_gains(cycles, RELAY_HALF_OUTPUT).failure == TuningFailure::none);

    cycles[2].period_ms = 0;
    CHECK(compute_gains(cycles, RELAY_HALF_OUTPUT).failure == TuningFailure::cycles_disagree);
}

TEST_CASE("hotend_pid_tuning: cycles with different swings are rejected") {
    auto cycles = uniform_cycles(20'000, TARGET + 3, TARGET - 3);
    cycles[1].max_temp = TARGET + 5;
    CHECK(compute_gains(cycles, RELAY_HALF_OUTPUT).failure == TuningFailure::none);

    cycles[1].max_temp = TARGET + 6;
    CHECK(compute_gains(cycles, RELAY_HALF_OUTPUT).failure == TuningFailure::cycles_disagree);
}

TEST_CASE("hotend_pid_tuning: gains outside the limits are rejected but reported") {
    // a = 2 °C, Tu = 10 s: Kp = 48.7 and Ki = 9.7, above its limit of 5
    const TuningResult result = compute_gains(uniform_cycles(10'000, TARGET + 2, TARGET - 2), RELAY_HALF_OUTPUT);
    CHECK(result.failure == TuningFailure::out_of_limits);
    CHECK(result.gains.ki > HotendPIDGains::ki_max);
    CHECK(result.gains.kp == Catch::Approx(0.6 * 4 * 127.5 / (std::numbers::pi * 2)));
}

TEST_CASE("hotend_pid_tuning: no cycles give no gains") {
    CHECK(compute_gains({}, RELAY_HALF_OUTPUT).failure == TuningFailure::cycles_disagree);
}
