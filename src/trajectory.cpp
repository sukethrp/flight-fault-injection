#include "trajectory.h"

#include <cmath>

namespace {

// Synthetic time from the control tick only. 50 Hz matches kCtrlEvery at 250 Hz.
constexpr float    kCtrlDt     = 0.02f;
constexpr float    kRadius     = 2.0f;   // m
constexpr float    kAlt        = -1.0f;  // m, NED z-down → 1 m above origin
// Integer tick periods keep the path bit-identical across runs. A pure circle
// never holds station; Phase 5 needs a stationary window to separate drift
// from tracking lag.
constexpr uint64_t kLapTicks   = 1000;  // 20 s → ω = 2π/20
constexpr uint64_t kHoldTicks  = 200;   // 4 s station-keeping between laps
constexpr uint64_t kCycleTicks = kLapTicks + kHoldTicks;

}

void trajectory_setpoint(uint64_t ctrl_tick, float pos_sp[3]) {
    const uint64_t phase = ctrl_tick % kCycleTicks;
    if (phase < kHoldTicks) {
        // Hold at θ=0 so the lap starts continuous with the hold.
        pos_sp[0] = kRadius;
        pos_sp[1] = 0.0f;
        pos_sp[2] = kAlt;
        return;
    }
    const float lap_t = static_cast<float>(phase - kHoldTicks) * kCtrlDt;
    const float omega = (2.0f * std::acos(-1.0f)) / (kLapTicks * kCtrlDt);
    const float th    = omega * lap_t;
    pos_sp[0] = kRadius * std::cos(th);
    pos_sp[1] = kRadius * std::sin(th);
    pos_sp[2] = kAlt;
}
