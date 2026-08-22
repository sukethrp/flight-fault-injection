#include "plant_dyn.h"

#include <cmath>
#include <cstdio>

static int fail(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    return 1;
}

int main() {
    plant::State s{};
    plant::Params par{};
    const double a_cmd[3] = {0.0, 0.0, 0.0};
    const int n = plant::kHz * 10;
    for (int i = 0; i < n; ++i) plant::rk4_step(s, a_cmd, par, plant::kDt);

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
    return 0;
}
