#pragma once

namespace plant {

// NED, z-down. gravity sits on vdot, not inside the lag, so a_cmd=0 is
// constant-g free-fall. RK4 of that polynomial is exact in real arithmetic;
// leftover error is rounding.
constexpr double kGravity = 9.80665;
constexpr int    kHz      = 1000;
constexpr double kDt      = 1.0 / kHz;

// 50 ms is faster than the 250 Hz loop and slower than a motor electrical
// time constant. mass cancels while amax is an acceleration limit; it is
// still the F=ma scale if FORCE_SET arrives.
constexpr double kTauDefault  = 0.05;  // s
constexpr double kMassDefault = 1.5;   // kg
constexpr double kAmaxDefault = 20.0;  // m/s² per axis

struct State {
    double p[3];
    double v[3];
    double a[3];  // lagged commanded accel, gravity applied in vdot only
};

struct Params {
    double tau  = kTauDefault;
    double mass = kMassDefault;
    double amax = kAmaxDefault;
};

inline double clamp(double x, double lo, double hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

inline void deriv(const State& s, const double a_cmd[3], const Params& par, State& d) {
    const double inv_tau = 1.0 / par.tau;
    for (int i = 0; i < 3; ++i) {
        d.p[i] = s.v[i];
        d.v[i] = s.a[i] + (i == 2 ? kGravity : 0.0);
        const double Fcmd = par.mass * clamp(a_cmd[i], -par.amax, par.amax);
        d.a[i] = (Fcmd / par.mass - s.a[i]) * inv_tau;
    }
}

inline State axpy(const State& s, const State& d, double h) {
    State o{};
    for (int i = 0; i < 3; ++i) {
        o.p[i] = s.p[i] + h * d.p[i];
        o.v[i] = s.v[i] + h * d.v[i];
        o.a[i] = s.a[i] + h * d.a[i];
    }
    return o;
}

inline void rk4_step(State& s, const double a_cmd[3], const Params& par, double dt) {
    State k1{}, k2{}, k3{}, k4{};
    deriv(s, a_cmd, par, k1);
    const State s2 = axpy(s, k1, dt * 0.5);
    deriv(s2, a_cmd, par, k2);
    const State s3 = axpy(s, k2, dt * 0.5);
    deriv(s3, a_cmd, par, k3);
    const State s4 = axpy(s, k3, dt);
    deriv(s4, a_cmd, par, k4);
    const double s6 = dt / 6.0;
    for (int i = 0; i < 3; ++i) {
        s.p[i] += s6 * (k1.p[i] + 2.0 * k2.p[i] + 2.0 * k3.p[i] + k4.p[i]);
        s.v[i] += s6 * (k1.v[i] + 2.0 * k2.v[i] + 2.0 * k3.v[i] + k4.v[i]);
        s.a[i] += s6 * (k1.a[i] + 2.0 * k2.a[i] + 2.0 * k3.a[i] + k4.a[i]);
    }
}

}  // namespace plant
