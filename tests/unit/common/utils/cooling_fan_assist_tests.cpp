#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <cooling_fan_assist.hpp>

using namespace cooling_fan_assist;

namespace {

/// The fan value and every write the guard made to it
struct FakeFan {
    explicit FakeFan(uint8_t value = 0)
        : value(value) {}

    uint8_t value;
    std::vector<uint8_t> writes;
};

struct FakePort {
    FakeFan *fan = nullptr;

    uint8_t get() const {
        return fan->value;
    }
    void set(uint8_t pwm) const {
        fan->value = pwm;
        fan->writes.push_back(pwm);
    }
};

using FakeGuard = Guard<FakePort>;

constexpr float COOL_TO = 50.0f;
constexpr int16_t HEATER_OFF = 0;
constexpr float HOT = 215.0f;

} // namespace

static_assert(!std::is_copy_constructible_v<FakeGuard> && !std::is_copy_assignable_v<FakeGuard>);
static_assert(!std::is_move_constructible_v<FakeGuard> && !std::is_move_assignable_v<FakeGuard>);
static_assert(!detail::creatable_with_new<FakeGuard> && !detail::creatable_with_new<FakeGuard[1]>);
// The check above would pass for any type if it could not see a new-expression at all
static_assert(detail::creatable_with_new<FakeFan> && detail::creatable_with_new<FakeFan[1]>);

TEST_CASE("cooling_fan_assist: the fan is wanted only above the wait temperature with the heater not above it") {
    static_assert(fan_wanted(HOT, HEATER_OFF, COOL_TO));

    CHECK(fan_wanted(HOT, HEATER_OFF, COOL_TO));
    CHECK(fan_wanted(50.1f, HEATER_OFF, COOL_TO));
    CHECK_FALSE(fan_wanted(50.0f, HEATER_OFF, COOL_TO));
    CHECK_FALSE(fan_wanted(20.0f, HEATER_OFF, COOL_TO));

    // Heater target at the wait temperature, as on the wiper
    CHECK(fan_wanted(200.0f, 140, 143.0f));
    CHECK(fan_wanted(HOT, 50, COOL_TO));
    CHECK_FALSE(fan_wanted(HOT, 51, COOL_TO));
    CHECK_FALSE(fan_wanted(HOT, 215, COOL_TO));

    CHECK_FALSE(fan_wanted(std::numeric_limits<float>::quiet_NaN(), HEATER_OFF, COOL_TO));
}

TEST_CASE("cooling_fan_assist: a nozzle never above the wait temperature leaves the fan alone") {
    FakeFan fan { 77 };
    {
        FakeGuard guard { FakePort { &fan } };
        guard.update(40.0f, HEATER_OFF, COOL_TO);
        guard.update(COOL_TO, HEATER_OFF, COOL_TO);
        guard.update(HOT, 215, COOL_TO);
        guard.release();
    }
    CHECK(fan.writes.empty());
    CHECK(fan.value == 77);
}

TEST_CASE("cooling_fan_assist: a hot nozzle sets full power once") {
    FakeFan fan;
    FakeGuard guard { FakePort { &fan } };
    for (float temp = HOT; temp > 60.0f; temp -= 10.0f) {
        guard.update(temp, HEATER_OFF, COOL_TO);
    }
    CHECK(fan.writes == std::vector<uint8_t> { fan_pwm });
    CHECK(fan.value == 255);
}

TEST_CASE("cooling_fan_assist: reaching the wait temperature puts back the value found") {
    FakeFan fan { 77 };
    {
        FakeGuard guard { FakePort { &fan } };
        guard.update(HOT, HEATER_OFF, COOL_TO);
        CHECK(fan.value == 255);
        guard.update(COOL_TO, HEATER_OFF, COOL_TO);
        CHECK(fan.value == 77);
        guard.update(30.0f, HEATER_OFF, COOL_TO);
    }
    CHECK(fan.writes == std::vector<uint8_t> { 255, 77 });
    CHECK(fan.value == 77);
}

TEST_CASE("cooling_fan_assist: a heater target raised above the wait temperature puts back the value found") {
    FakeFan fan { 77 };
    FakeGuard guard { FakePort { &fan } };
    guard.update(HOT, HEATER_OFF, COOL_TO);
    guard.update(HOT, 215, COOL_TO);
    CHECK(fan.writes == std::vector<uint8_t> { 255, 77 });
    CHECK(fan.value == 77);
}

TEST_CASE("cooling_fan_assist: ending the guard while engaged puts back the value found") {
    FakeFan fan { 77 };
    {
        FakeGuard guard { FakePort { &fan } };
        guard.update(HOT, HEATER_OFF, COOL_TO);
        CHECK(fan.value == 255);
    }
    CHECK(fan.writes == std::vector<uint8_t> { 255, 77 });
    CHECK(fan.value == 77);
}

TEST_CASE("cooling_fan_assist: release is idempotent") {
    FakeFan fan { 77 };
    {
        FakeGuard guard { FakePort { &fan } };
        guard.update(HOT, HEATER_OFF, COOL_TO);
        guard.release();
        guard.release();
    }
    CHECK(fan.writes == std::vector<uint8_t> { 255, 77 });
}

TEST_CASE("cooling_fan_assist: engaging again remembers the value of that moment") {
    FakeFan fan { 77 };
    {
        FakeGuard guard { FakePort { &fan } };
        guard.update(HOT, HEATER_OFF, COOL_TO);
        guard.update(HOT, 215, COOL_TO);
        fan.value = 100;
        guard.update(HOT, HEATER_OFF, COOL_TO);
    }
    CHECK(fan.writes == std::vector<uint8_t> { 255, 77, 255, 100 });
    CHECK(fan.value == 100);
}

TEST_CASE("cooling_fan_assist: a value written while engaged is overwritten on release") {
    FakeFan fan { 77 };
    {
        FakeGuard guard { FakePort { &fan } };
        guard.update(HOT, HEATER_OFF, COOL_TO);
        fan.value = 128;
        guard.update(HOT, HEATER_OFF, COOL_TO);
    }
    CHECK(fan.writes == std::vector<uint8_t> { 255, 77 });
    CHECK(fan.value == 77);
}
