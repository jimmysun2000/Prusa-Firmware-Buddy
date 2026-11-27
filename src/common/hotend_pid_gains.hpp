#pragma once

/// Hotend PID gains in M301 units: Ki per second, Kd in seconds
struct HotendPIDGains {
    /// Gains outside these inclusive ranges are never applied
    static constexpr float kp_min = 1.0f;
    static constexpr float kp_max = 50.0f;
    static constexpr float ki_min = 0.0f;
    static constexpr float ki_max = 5.0f;
    static constexpr float kd_min = 0.0f;
    static constexpr float kd_max = 500.0f;

    float kp;
    float ki;
    float kd;

    constexpr bool operator==(const HotendPIDGains &) const = default;
};

namespace hotend_pid {

/// @returns whether all three gains are within their ranges; NaN and infinities never are
constexpr bool is_valid(const HotendPIDGains &gains) {
    return gains.kp >= HotendPIDGains::kp_min && gains.kp <= HotendPIDGains::kp_max
        && gains.ki >= HotendPIDGains::ki_min && gains.ki <= HotendPIDGains::ki_max
        && gains.kd >= HotendPIDGains::kd_min && gains.kd <= HotendPIDGains::kd_max;
}

/// Converts the gains to a regulator config (HotendPIDConfig, PID_t), pre-scaling Ki and Kd
/// for the regulator period @p pid_dt [s] the way M301 does with scalePID_i and scalePID_d.
/// Members of @p Config other than Kp, Ki and Kd keep their defaults.
template <class Config>
constexpr Config to_config(const HotendPIDGains &gains, float pid_dt) {
    Config config {};
    config.Kp = gains.kp;
    config.Ki = gains.ki * pid_dt;
    config.Kd = gains.kd / pid_dt;
    return config;
}

/// Inverse of to_config
template <class Config>
constexpr HotendPIDGains from_config(const Config &config, float pid_dt) {
    return {
        .kp = config.Kp,
        .ki = config.Ki / pid_dt,
        .kd = config.Kd * pid_dt,
    };
}

} // namespace hotend_pid
