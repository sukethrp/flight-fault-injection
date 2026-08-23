#include "ekf.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

constexpr float kGravity = 9.80665f;
// Bound for the zero-noise / perfect-init case after AUTHOR implements
// predict+correct. Position must stay inside this envelope for 60 s of
// simulated double-integrator motion with perfect accel and pos.
constexpr float kZeroNoisePosBoundM = 0.05f;

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

// Constant-accel NED double integrator. IMU specific force includes +g on D.
void step_truth(float p[3], float v[3], const float a_ned[3], float dt) {
    for (int i = 0; i < 3; ++i) {
        p[i] += v[i] * dt + 0.5f * a_ned[i] * dt * dt;
        v[i] += a_ned[i] * dt;
    }
}

int test_zero_noise_bound() {
    EkfConfig cfg;
    cfg.sigma_acc = 0.02f;
    cfg.sigma_pos = 0.05f;
    Ekf ekf(cfg);

    float p[3] = {0.0f, 0.0f, -1.0f};
    float v[3] = {0.0f, 0.0f, 0.0f};
    float x0[6] = {p[0], p[1], p[2], v[0], v[1], v[2]};
    float P0[6] = {1e-6f, 1e-6f, 1e-6f, 1e-6f, 1e-6f, 1e-6f};
    ekf.reset(x0, P0);

    const float dt = 0.004f;  // 250 Hz
    const int   steps = static_cast<int>(60.0f / dt);
    float max_pos_err = 0.0f;
    float a_cmd[3] = {0.5f, -0.3f, 0.0f};  // horizontal motion, hover on D

    for (int i = 0; i < steps; ++i) {
        step_truth(p, v, a_cmd, dt);
        const float accel_body[3] = {
            a_cmd[0],
            a_cmd[1],
            a_cmd[2] + kGravity,  // specific force
        };
        ekf.predict(accel_body, dt);
        if (i % 5 == 4) {  // 50 Hz position
            const float z[3] = {p[0], p[1], p[2]};
            (void)ekf.correct(z);
        }
        const float* x = ekf.state();
        for (int ax = 0; ax < 3; ++ax) {
            const float e = std::fabs(x[ax] - p[ax]);
            if (e > max_pos_err) max_pos_err = e;
        }
    }

    std::fprintf(stderr,
                 "zero-noise: max |pos err|=%.6f m (bound %.3f)\n",
                 max_pos_err, kZeroNoisePosBoundM);
    // Stubs leave x at x0 while truth moves → this fails until AUTHOR fills
    // predict/correct. That failure is the gate, not a flaky pass.
    if (!(max_pos_err < kZeroNoisePosBoundM)) {
        return fail("zero-noise position error exceeded bound");
    }
    return 0;
}

int test_noisy_trace_stable() {
    EkfConfig cfg;
    cfg.sigma_acc = 0.5f;
    cfg.sigma_pos = 0.05f;
    Ekf ekf(cfg);

    float p[3] = {0.0f, 0.0f, -1.0f};
    float v[3] = {0.0f, 0.0f, 0.0f};
    float x0[6] = {p[0], p[1], p[2], v[0], v[1], v[2]};
    float P0[6] = {0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f};
    ekf.reset(x0, P0);

    std::mt19937 rng(42);
    std::normal_distribution<float> acc_n(0.0f, 0.02f);
    std::normal_distribution<float> pos_n(0.0f, 0.05f);

    const float dt = 0.004f;
    const int   steps = static_cast<int>(300.0f / dt);  // 300 s simulated
    const float a_cmd[3] = {0.2f, 0.1f, 0.0f};

    std::vector<float> traces;
    traces.reserve(static_cast<size_t>(steps / 5) + 1);
    const float trace0 = ekf.trace_p();

    for (int i = 0; i < steps; ++i) {
        step_truth(p, v, a_cmd, dt);
        const float accel_body[3] = {
            a_cmd[0] + acc_n(rng),
            a_cmd[1] + acc_n(rng),
            a_cmd[2] + kGravity + acc_n(rng),
        };
        ekf.predict(accel_body, dt);
        if (i % 5 == 4) {
            const float z[3] = {
                p[0] + pos_n(rng),
                p[1] + pos_n(rng),
                p[2] + pos_n(rng),
            };
            (void)ekf.correct(z);
            traces.push_back(ekf.trace_p());
        }
    }

    if (traces.size() < 100) return fail("too few trace samples");

    // Filter must have moved P away from the prior. Empty stubs leave P
    // frozen and would otherwise vacuous-pass a "not growing" check.
    const float trace_end = traces.back();
    if (std::fabs(trace_end - trace0) < 1e-9f) {
        return fail("trace(P) unchanged from prior; predict/correct not implemented?");
    }

    // Monotone growth: every sample strictly above the previous by a hair.
    int rising = 0;
    for (size_t i = 1; i < traces.size(); ++i) {
        if (traces[i] > traces[i - 1] + 1e-9f) ++rising;
    }
    const float rise_frac = static_cast<float>(rising) / static_cast<float>(traces.size() - 1);
    std::fprintf(stderr,
                 "noisy: trace0=%.6f trace_end=%.6f rise_frac=%.3f n=%zu\n",
                 trace0, trace_end, rise_frac, traces.size());
    // A healthy filter's P breathes; a dead reckoner with no updates climbs.
    // Allow some rises; forbid almost-everywhere monotone growth.
    if (rise_frac > 0.95f) {
        return fail("trace(P) monotonically growing over 300 s");
    }
    return 0;
}

}  // namespace

int main() {
    if (!Ekf::is_implemented()) {
        std::fprintf(stderr,
                     "test_ekf: SKIP - Ekf::predict/correct are unimplemented stubs.\n"
                     "          See src/ekf.cpp AUTHOR markers. This is expected on main\n"
                     "          until the filter lands; it is not a broken filter.\n");
        return 77;  // ctest SKIP_RETURN_CODE
    }
    if (int rc = test_zero_noise_bound()) return rc;
    if (int rc = test_noisy_trace_stable()) return rc;
    std::fprintf(stderr, "test_ekf: ok\n");
    return 0;
}
