#pragma once

#include <algorithm>
#include <optional>

namespace buddy {

/// Relative extruder moves of the cold slow load, which pushes a new filament
/// through the gears and pecks its tip into the nozzle tube.
///
/// One forward move up to Peck::start, then forward nudges, each followed by
/// a back nudge, as long as a full forward nudge fits up to Peck::end, then
/// one forward move for the rest. The moves always add up to the total length.
///
/// A back move only follows a full forward nudge, so the filament never moves
/// back below Peck::start, never goes beyond the total length, and the first
/// and the last move are forward.
class SlowLoadPattern {
public:
    struct Peck {
        float start; ///< Pushed in one move, without back moves
        float end; ///< No forward nudge goes beyond this
        float forward; ///< Forward nudge
        float back; ///< Back nudge
    };

    /// Pecking needs 0 < back < forward and room for at least one forward
    /// nudge between start and min(end, total); otherwise the pattern is a
    /// single move of \p total.
    /// \p total must not be negative; zero yields no move.
    constexpr SlowLoadPattern(float total, const Peck &peck)
        : total_(total)
        , end_(std::min(peck.end, total))
        , forward_(peck.forward)
        , back_(peck.back) {
        const bool pecks = 0 < peck.back && peck.back < peck.forward
            && 0 <= peck.start && peck.start + peck.forward <= end_;
        start_ = pecks ? peck.start : total;
    }

    /// \returns the next relative move (negative = back), or nullopt once the pattern is complete
    constexpr std::optional<float> next() {
        if (back_pending_) {
            back_pending_ = false;
            position_ -= back_;
            return -back_;
        }

        if (position_ >= total_) {
            return std::nullopt;
        }

        float target = total_;
        if (position_ < start_) {
            target = start_;
        } else if (position_ + forward_ <= end_) {
            target = position_ + forward_;
            back_pending_ = true;
        }

        const float move = target - position_;
        position_ = target;
        return move;
    }

private:
    float total_;
    float start_ = 0;
    float end_;
    float forward_;
    float back_;

    float position_ = 0;
    bool back_pending_ = false;
};

} // namespace buddy
