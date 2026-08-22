#include "control.h"
#include "trajectory.h"

#include <cmath>
#include <cstdio>

static int fail(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    return 1;
}

// A setpoint step must not produce an accel impulse from the D term. D is on
// measured velocity; with vel held constant, a step in pos_sp only moves the
// P channels.
static int test_d_on_measurement() {
    Controller c(20.0f);
    const float pos[3] = {0.0f, 0.0f, -1.0f};
    const float vel[3] = {0.0f, 0.0f, 0.0f};
    float sp0[3] = {0.0f, 0.0f, -1.0f};
    float sp1[3] = {1.0f, 0.0f, -1.0f};
    const float dt = 0.02f;

    // Prime vel_prev so D is active, with zero measured vdot.
    (void)c.update(pos, vel, sp0, dt);
    const AccelCmd before = c.update(pos, vel, sp0, dt);
    const AccelCmd after  = c.update(pos, vel, sp1, dt);

    // |Δax| from the step should be Kp_vel * Kp_pos * 1m = 3 * 1.2 = 3.6,
    // not an unbounded D spike. Bound well below amax.
    const float dax = std::fabs(after.ax - before.ax);
    std::fprintf(stderr, "setpoint step: before.ax=%.3f after.ax=%.3f dax=%.3f\n",
                 before.ax, after.ax, dax);
    if (dax > 5.0f) return fail("setpoint step produced D-like accel spike");
    if (!(dax > 1.0f)) return fail("setpoint step should move the P channel");
    return 0;
}

// Sustained saturation must freeze the velocity integrator. Otherwise a GPS
// dropout winds Ki and the unload transient owns TTR.
static int test_integrator_freeze_on_saturate() {
    ControllerGains g{};
    g.kp_pos = 10.0f;  // large outer gain → huge v_sp
    g.kp_vel = 10.0f;
    g.ki_vel = 50.0f;
    g.kd_vel = 0.0f;
    Controller c(2.0f, g);  // tight amax so we saturate immediately

    const float pos[3] = {0.0f, 0.0f, 0.0f};
    const float vel[3] = {0.0f, 0.0f, 0.0f};
    const float sp[3]  = {100.0f, 0.0f, 0.0f};
    const float dt = 0.02f;

    AccelCmd last{};
    for (int i = 0; i < 50; ++i) {
        last = c.update(pos, vel, sp, dt);
    }
    std::fprintf(stderr, "saturated ax=%.3f (amax=2)\n", last.ax);
    if (std::fabs(std::fabs(last.ax) - 2.0f) > 1e-3f) {
        return fail("expected accel at amax under sustained error");
    }

    // Hold at the same state another 50 steps; if Ki kept integrating, ax
    // would still be at amax but unloading after a setpoint snap-back would
    // overshoot harder. Probe unload: step sp to current pos and require
    // |ax| not to remain pegged from a huge integ.
    const float sp_hold[3] = {0.0f, 0.0f, 0.0f};
    AccelCmd unload = c.update(pos, vel, sp_hold, dt);
    std::fprintf(stderr, "unload ax=%.3f\n", unload.ax);
    // With freeze, integ stayed modest; unload should not slam the opposite rail.
    if (std::fabs(unload.ax) >= 2.0f - 1e-3f) {
        return fail("integrator appears wound: unload still saturates");
    }
    return 0;
}

static int test_trajectory_tick_pure() {
    float a[3], b[3];
    trajectory_setpoint(0, a);
    trajectory_setpoint(0, b);
    if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
        return fail("trajectory_setpoint not bit-identical for same tick");
    }
    // Hold window: same station for early ticks; motion starts after hold.
    trajectory_setpoint(50, b);
    if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
        return fail("hold ticks must share the station-keeping setpoint");
    }
    trajectory_setpoint(250, b);  // past 200-tick hold into the lap
    if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2]) {
        return fail("trajectory should move once the lap phase begins");
    }
    float c[3];
    trajectory_setpoint(250, c);
    if (c[0] != b[0] || c[1] != b[1] || c[2] != b[2]) {
        return fail("trajectory_setpoint(250) not stable across calls");
    }
    return 0;
}

int main() {
    if (int rc = test_d_on_measurement()) return rc;
    if (int rc = test_integrator_freeze_on_saturate()) return rc;
    if (int rc = test_trajectory_tick_pure()) return rc;
    std::fprintf(stderr, "control+trajectory checks ok\n");
    return 0;
}
