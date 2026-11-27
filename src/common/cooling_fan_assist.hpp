/// @file
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

/// The print fan of the picked tool helps a waiting loop cool its nozzle and goes
/// back to the value it had as soon as the wait no longer needs it.
namespace cooling_fan_assist {

/// Full power, as in the stock cooling waits
inline constexpr uint8_t fan_pwm = 255;

/// @returns whether the fan helps a nozzle at @p nozzle_temp that is waited for to
/// cool to @p cool_to: it is hotter than that, and its heater target @p heater_target
/// does not hold it there. False for a NaN reading.
constexpr bool fan_wanted(float nozzle_temp, int16_t heater_target, float cool_to) {
    return nozzle_temp > cool_to && heater_target <= cool_to;
}

/// Runs the print fan at fan_pwm while fan_wanted() holds and keeps it at the value
/// it found otherwise.
///
/// Declare it in the function that waits, before the waiting loop, and feed it each
/// reading. Every way out of that function ends the guard, which puts the value found
/// back, so the fan cannot run on after the wait. It can be neither copied, moved nor
/// created with new, so it cannot outlive that function. One guard per wait, used from
/// one thread.
///
/// A value someone else writes to the fan while the guard is engaged is overwritten
/// when it releases.
///
/// @tparam FanPort access to the print fan value: `uint8_t get() const` and `void set(uint8_t) const`
template <typename FanPort>
class Guard {
public:
    explicit Guard(FanPort port = {})
        : port(port) {}

    Guard(const Guard &) = delete;
    Guard(Guard &&) = delete;
    Guard &operator=(const Guard &) = delete;
    Guard &operator=(Guard &&) = delete;

    static void *operator new(std::size_t) = delete;
    static void *operator new[](std::size_t) = delete;

    ~Guard() {
        release();
    }

    /// Engages while fan_wanted() holds: remembers the fan value and sets fan_pwm,
    /// once per engagement. Releases otherwise. Writes the fan only on a change.
    void update(float nozzle_temp, int16_t heater_target, float cool_to) {
        if (fan_wanted(nozzle_temp, heater_target, cool_to)) {
            engage();
        } else {
            release();
        }
    }

    /// Puts back the value found when the guard engaged; does nothing unless engaged
    void release() {
        if (!engaged) {
            return;
        }
        engaged = false;
        port.set(found);
    }

private:
    void engage() {
        if (engaged) {
            return;
        }
        found = port.get();
        port.set(fan_pwm);
        engaged = true;
    }

    FanPort port;
    uint8_t found = 0;
    bool engaged = false;
};

namespace detail {
    /// Stands in for a real port in the checks below
    struct AnyPort {
        uint8_t get() const { return 0; }
        void set(uint8_t) const {}
    };

    template <typename T>
    concept creatable_with_new = requires { new T; };
} // namespace detail

static_assert(!std::is_copy_constructible_v<Guard<detail::AnyPort>> && !std::is_copy_assignable_v<Guard<detail::AnyPort>>);
static_assert(!std::is_move_constructible_v<Guard<detail::AnyPort>> && !std::is_move_assignable_v<Guard<detail::AnyPort>>);
static_assert(!detail::creatable_with_new<Guard<detail::AnyPort>> && !detail::creatable_with_new<Guard<detail::AnyPort>[1]>);

} // namespace cooling_fan_assist
