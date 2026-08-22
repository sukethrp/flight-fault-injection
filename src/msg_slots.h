#pragma once

#include "common/mavlink.h"

#include <cstdint>

// fixture rates. three periods of each is the stale limit at --staleness-limit-periods=3.
constexpr int64_t kImuPeriodNs = 2500000;    // 400 Hz
constexpr int64_t kPosPeriodNs = 20000000;   // 50 Hz
constexpr int64_t kGpsPeriodNs = 200000000;  // 5 Hz

// age is sampled once per loop period, so TTD cannot be the limit itself.
inline int64_t staleness_floor_ns(int64_t limit_ns, int64_t loop_period_ns) {
    if (limit_ns <= 0 || loop_period_ns <= 0) return 0;
    return ((limit_ns + loop_period_ns - 1) / loop_period_ns) * loop_period_ns;
}

// Named members, not a 256-entry msgid table: 255 unused cache lines, and the
// messages the loop depends on are visible here. Phase 6 walks these.
// last_seq / seq_gaps are not per slot: MAVLink seq is per-sending-component.
struct ImuSlot {
    mavlink_highres_imu_t payload;
    int64_t  rx_mono_ns;           // tick woke, not a fresh now_ns(); one timebase per tick
    uint64_t sender_us;            // payload time_usec
    int64_t  age_ns;               // woke - rx_mono_ns, filled at read. drain-time age is 0.
    int64_t  expected_period_ns;   // kImuPeriodNs
    bool     valid;
};

struct PosSlot {
    mavlink_local_position_ned_t payload;
    int64_t  rx_mono_ns;
    uint64_t sender_us;            // time_boot_ms * 1000; the message has no time_usec
    int64_t  age_ns;
    int64_t  expected_period_ns;   // kPosPeriodNs
    bool     valid;
};

struct GpsSlot {
    mavlink_gps_raw_int_t payload;
    int64_t  rx_mono_ns;
    uint64_t sender_us;            // payload time_usec
    int64_t  age_ns;
    int64_t  expected_period_ns;   // kGpsPeriodNs
    bool     valid;
};

struct MsgSlots {
    ImuSlot imu;
    PosSlot pos;
    GpsSlot gps;
    uint8_t  last_seq;
    uint32_t seq_gaps;
    bool     seq_valid;
};

// per-slot last_seq treats IMU seq=0, POS seq=1, IMU seq=2 as a drop of 1.
inline uint8_t note_component_seq(MsgSlots& s, uint8_t seq) {
    uint8_t g = 0;
    if (s.seq_valid) {
        g = static_cast<uint8_t>((seq - s.last_seq - 1) & 0xFF);
        s.seq_gaps += g;
    }
    s.last_seq  = seq;
    s.seq_valid = true;
    return g;
}
