#include <initializer_list>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <hotend_pid_gains.hpp>

using namespace hotend_pid;

namespace {

constexpr HotendPIDGains STOCK_GAINS { 14.0f, 1.0f, 100.0f };

constexpr float NAN_VALUE = std::numeric_limits<float>::quiet_NaN();
constexpr float INF_VALUE = std::numeric_limits<float>::infinity();

/// Stands in for HotendPIDConfig, which has members beyond the three gains
struct Config {
    float Kp = -1.0f;
    float Ki = -1.0f;
    float Kd = -1.0f;
    float Kc = 7.0f;
};

HotendPIDGains with_kp(float value) {
    HotendPIDGains gains = STOCK_GAINS;
    gains.kp = value;
    return gains;
}

HotendPIDGains with_ki(float value) {
    HotendPIDGains gains = STOCK_GAINS;
    gains.ki = value;
    return gains;
}

HotendPIDGains with_kd(float value) {
    HotendPIDGains gains = STOCK_GAINS;
    gains.kd = value;
    return gains;
}

} // namespace

TEST_CASE("hotend_pid: stock gains are valid") {
    CHECK(is_valid(STOCK_GAINS));
}

TEST_CASE("hotend_pid: limits are inclusive") {
    CHECK(is_valid(with_kp(1.0f)));
    CHECK(is_valid(with_kp(50.0f)));
    CHECK(is_valid(with_ki(0.0f)));
    CHECK(is_valid(with_ki(5.0f)));
    CHECK(is_valid(with_kd(0.0f)));
    CHECK(is_valid(with_kd(500.0f)));
}

TEST_CASE("hotend_pid: gains outside the limits are rejected") {
    CHECK_FALSE(is_valid(with_kp(0.99f)));
    CHECK_FALSE(is_valid(with_kp(50.01f)));
    CHECK_FALSE(is_valid(with_kp(-14.0f)));
    CHECK_FALSE(is_valid(with_ki(-0.001f)));
    CHECK_FALSE(is_valid(with_ki(5.001f)));
    CHECK_FALSE(is_valid(with_kd(-0.1f)));
    CHECK_FALSE(is_valid(with_kd(500.1f)));
    CHECK_FALSE(is_valid(HotendPIDGains { 1000000.0f, 0.0f, 0.0f }));
    CHECK_FALSE(is_valid(HotendPIDGains { 0.0f, 0.0f, 0.0f }));
}

TEST_CASE("hotend_pid: non-finite gains are rejected") {
    for (const float value : { NAN_VALUE, INF_VALUE, -INF_VALUE }) {
        CHECK_FALSE(is_valid(with_kp(value)));
        CHECK_FALSE(is_valid(with_ki(value)));
        CHECK_FALSE(is_valid(with_kd(value)));
    }
}

TEST_CASE("hotend_pid: to_config scales like M301") {
    constexpr float pid_dt = 0.16f;
    const Config config = to_config<Config>(STOCK_GAINS, pid_dt);

    CHECK(config.Kp == 14.0f);
    CHECK(config.Ki == 1.0f * pid_dt);
    CHECK(config.Kd == 100.0f / pid_dt);
    CHECK(config.Kd == Catch::Approx(625.0f));
    CHECK(config.Kc == 7.0f);
}
