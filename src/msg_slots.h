#pragma once

#include "common/mavlink.h"

#include <cstdint>

// 400 Hz HIGHRES_IMU fixture. three periods is 7.5 ms; two silent 4 ms ticks trip it.
constexpr int64_t kImuPeriodNs = 2500000;

// age is sampled once per loop period, so TTD cannot be the limit itself.
inline int64_t staleness_floor_ns(int64_t limit_ns, int64_t loop_period_ns) {
    if (limit_ns <= 0 || loop_period_ns <= 0) return 0;
    return ((limit_ns + loop_period_ns - 1) / loop_period_ns) * loop_period_ns;
}

// Named members, not a 256-entry msgid table: 255 unused cache lines, and the
// messages the loop depends on are visible here. Phase 6 walks these.
struct ImuSlot {
    mavlink_highres_imu_t payload;
    int64_t  rx_mono_ns;           // tick woke, not a fresh now_ns(); one timebase per tick
    uint64_t sender_us;            // payload time_usec
    int64_t  age_ns;               // woke - rx_mono_ns, filled at read. drain-time age is 0.
    int64_t  expected_period_ns;   // per message type; IMU is kImuPeriodNs
    uint32_t seq_gaps;
    uint8_t  last_seq;
    bool     valid;
};

// GpsSlot and PosSlot land in 2e when PX4 starts streaming them.
struct MsgSlots {
    ImuSlot imu;
};
