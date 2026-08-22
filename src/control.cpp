#include "control.h"

#include <cmath>

namespace {

constexpr float kGravity = 9.80665f;  // NED z-down; feedforward cancels plant g

inline float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

}

Controller::Controller(float amax, ControllerGains g)
    : gains_(g), amax_(amax), have_vel_prev_(false) {
    reset();
}

void Controller::reset() {
    for (int i = 0; i < 3; ++i) {
        integ_[i]     = 0.0f;
        vel_prev_[i]  = 0.0f;
    }
    have_vel_prev_ = false;
}

AccelCmd Controller::update(const float pos[3], const float vel[3],
                            const float pos_sp[3], float dt) {
    AccelCmd cmd{0.0f, 0.0f, 0.0f};
    if (!(dt > 0.0f)) return cmd;

    const float inv_dt = 1.0f / dt;
    for (int i = 0; i < 3; ++i) {
        // Outer: position P → velocity setpoint.
        const float v_sp = gains_.kp_pos * (pos_sp[i] - pos[i]);
        const float e_v  = v_sp - vel[i];

        // D on measured velocity, not on e_v. d(setpoint)/dt of a step is an
        // impulse; differentiating the measurement keeps a step in pos_sp
        // from slamming the accel command.
        float v_dot = 0.0f;
        if (have_vel_prev_) {
            v_dot = (vel[i] - vel_prev_[i]) * inv_dt;
        }
        vel_prev_[i] = vel[i];

        float a_raw = gains_.kp_vel * e_v + gains_.ki_vel * integ_[i]
                      - gains_.kd_vel * v_dot;
        // Hover: plant adds +g on vdot for axis 2; cancel it in the command.
        if (i == 2) a_raw -= kGravity;

        const float a_sat = clampf(a_raw, -amax_, amax_);

        // Freeze the integrator while saturated. Without this, a sustained
        // accel limit (GPS dropout, Phase 5) winds Ki and the unload transient
        // dominates TTR.
        if (a_sat == a_raw) {
            integ_[i] += e_v * dt;
        }

        if (i == 0) cmd.ax = a_sat;
        else if (i == 1) cmd.ay = a_sat;
        else cmd.az = a_sat;
    }
    have_vel_prev_ = true;
    return cmd;
}
