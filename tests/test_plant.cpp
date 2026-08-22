#include "plant_dyn.h"

#include <cmath>
#include <cstdio>

static int fail(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    return 1;
}

// Lag step: a_cmd = A, a(0)=0 → a(t)=A(1-e^{-t/τ}). Used for the RK4 order
// check; free-fall alone cannot see truncation (RK4 is exact on that poly).
static double a_lag_closed(double A, double tau, double t) {
    return A * (1.0 - std::exp(-t / tau));
}

static double rk4_lag_err(double h, double T, double A, double tau) {
    plant::State s{};
    plant::Params par{};
    par.tau = tau;
    const double a_cmd[3] = {A, 0.0, 0.0};
    const int n = static_cast<int>(std::lround(T / h));
    for (int i = 0; i < n; ++i) plant::rk4_step(s, a_cmd, par, h);
    const double t = static_cast<double>(n) * h;
    return std::fabs(s.a[0] - a_lag_closed(A, tau, t));
}

int main() {
    plant::State s{};
    plant::Params par{};
    const double a_cmd0[3] = {0.0, 0.0, 0.0};
    const int n = plant::kHz * 10;
    for (int i = 0; i < n; ++i) plant::rk4_step(s, a_cmd0, par, plant::kDt);

    const double t = static_cast<double>(n) * plant::kDt;
    const double closed = 0.5 * plant::kGravity * t * t;
    const double pz_err = s.p[2] - closed;
    const double vz_err = s.v[2] - plant::kGravity * t;
    // RK4 is exact for this polynomial; leftover is rounding. 1e-9 m is ~2e-12
    // of the 490.3325 m closed-form drop, well above 10 s of double roundoff.
    const double bound = 1e-9;

    std::fprintf(stderr,
                 "freefall t=%.0f s: pz=%.12f closed=%.12f err=%.3e m\n",
                 t, s.p[2], closed, pz_err);
    std::fprintf(stderr,
                 "RK4 step error over %d steps of dt=%.3f s: |pz_err|=%.3e m, |vz_err|=%.3e m/s\n",
                 n, plant::kDt, std::fabs(pz_err), std::fabs(vz_err));

    if (std::fabs(pz_err) > bound) return fail("pz vs 0.5*g*t^2 exceeds RK4 bound");
    if (std::fabs(vz_err) > bound) return fail("vz vs g*t exceeds RK4 bound");
    if (std::fabs(s.p[0]) > bound || std::fabs(s.p[1]) > bound) return fail("lateral position drifted");
    if (std::fabs(s.v[0]) > bound || std::fabs(s.v[1]) > bound) return fail("lateral velocity drifted");
    if (std::fabs(s.a[0]) > bound || std::fabs(s.a[1]) > bound || std::fabs(s.a[2]) > bound) {
        return fail("filtered accel should stay zero in open loop");
    }

    // Local truncation is C·h⁵ + D·h⁶ + …; global error accumulates as
    // C·h⁴ + D·h⁵ + …. Pure h⁴ predicts (16/4)^4 = 256. The coarse step
    // carries proportionally more of the h⁵ remainder, so the measured ratio
    // sits a few percent above 256 (here ~271). That sign and magnitude are
    // the order check — not a fudge around "close enough." At h=1 ms with
    // large τ, truncation falls under double roundoff and the ratio collapses
    // (same trap as free-fall); 4 ms and 16 ms keep truncation dominant.
    constexpr double kA   = 5.0;
    constexpr double kTau = 0.05;
    constexpr double kT   = 1.0;
    const double err4  = rk4_lag_err(0.004, kT, kA, kTau);
    const double err16 = rk4_lag_err(0.016, kT, kA, kTau);
    const double ratio = err16 / err4;

    std::fprintf(stderr,
                 "RK4 lag step A=%.0f tau=%.2f T=%.0f: err(4ms)=%.3e err(16ms)=%.3e "
                 "ratio=%.1f (h^4 asymptote 256; >256 from h^5)\n",
                 kA, kTau, kT, err4, err16, ratio);

    if (!(err4 > 0.0) || !(err16 > 0.0)) {
        return fail("lag RK4 errors must be positive (truncation, not roundoff)");
    }
    if (ratio < 64.0 || ratio > 1024.0) {
        return fail("lag RK4 error ratio outside [64, 1024]; expected ~256");
    }
    return 0;
}
