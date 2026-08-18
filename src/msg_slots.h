#pragma once

#include "common/mavlink.h"

#include <cstdint>

// Named members, not a 256-entry msgid table: 255 unused cache lines, and the
// messages the loop depends on are visible here. Phase 6 walks these.
struct ImuSlot {
    mavlink_highres_imu_t payload;
    int64_t  rx_mono_ns;    // tick woke, not a fresh now_ns(); one timebase per tick
    uint64_t sender_us;     // payload time_usec
    uint32_t seq_gaps;
    uint8_t  last_seq;
    bool     valid;
};

// GpsSlot and PosSlot land in 2e when PX4 starts streaming them.
struct MsgSlots {
    ImuSlot imu;
};
