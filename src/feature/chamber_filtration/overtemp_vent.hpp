#pragma once

#include <optional>

namespace buddy {

/// Decides when to vent a chamber that got warmer than the printed filaments allow.
/// Starts above the limit, stops once the chamber is more than hysteresis below it.
class OvertempVent {
public:
    static constexpr float hysteresis = 3;

    /// \param current chamber temperature, nullopt when unknown
    /// \param limit lowest chamber maximum of the printed filaments, nullopt when there is none
    /// \returns whether to vent; never while either is unknown
    constexpr bool update(std::optional<float> current, std::optional<float> limit) {
        if (!current.has_value() || !limit.has_value()) {
            venting_ = false;
        } else if (*current > *limit) {
            venting_ = true;
        } else if (*current < *limit - hysteresis) {
            venting_ = false;
        }
        return venting_;
    }

    constexpr bool is_venting() const {
        return venting_;
    }

private:
    bool venting_ = false;
};

} // namespace buddy
