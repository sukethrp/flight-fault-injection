#include "msg_slots.h"

#include <cstdio>

static int fail(const char* msg) {
    std::fprintf(stderr, "%s\n", msg);
    return 1;
}

int main() {
    const int64_t loop_ns = 4000000;  // 250 Hz
    const int64_t lim     = 3;

    const int64_t imu_limit = kImuPeriodNs * lim;  // 7.5 ms
    const int64_t pos_limit = kPosPeriodNs * lim;  // 60 ms
    const int64_t gps_limit = kGpsPeriodNs * lim;  // 600 ms
    if (kImuPeriodNs != 2500000) return fail("imu period");
    if (kPosPeriodNs != 20000000) return fail("pos period");
    if (kGpsPeriodNs != 200000000) return fail("gps period");
    if (imu_limit != 7500000) return fail("imu limit 7.5 ms");
    if (pos_limit != 60000000) return fail("pos limit 60 ms");
    if (gps_limit != 600000000) return fail("gps limit 600 ms");

    const int64_t imu_floor = staleness_floor_ns(imu_limit, loop_ns);
    const int64_t pos_floor = staleness_floor_ns(pos_limit, loop_ns);
    const int64_t gps_floor = staleness_floor_ns(gps_limit, loop_ns);
    if (imu_floor != 8000000) return fail("imu floor 8 ms");
    if (pos_floor != 60000000) return fail("pos floor 60 ms");
    if (gps_floor != 600000000) return fail("gps floor 600 ms");

    // one silent 4 ms tick is under the IMU limit; two tick boundaries are the floor.
    if (!(loop_ns < imu_limit && 2 * loop_ns == imu_floor)) {
        return fail("first IMU miss should sit under the limit");
    }
    if (staleness_floor_ns(loop_ns, loop_ns) != loop_ns) {
        return fail("exact multiple should not round up");
    }

    // shared last_seq: IMU, POS, GPS, IMU with consecutive component seq is zero gaps.
    MsgSlots s{};
    if (note_component_seq(s, 0) != 0) return fail("first seq is not a gap");
    if (note_component_seq(s, 1) != 0) return fail("IMU then POS is not a gap");
    if (note_component_seq(s, 2) != 0) return fail("then GPS is not a gap");
    if (note_component_seq(s, 3) != 0) return fail("next IMU is not a gap");
    if (s.seq_gaps != 0) return fail("shared last_seq accumulated a false gap");
    if (!s.seq_valid || s.last_seq != 3) return fail("shared last_seq not updated");

    // the rejected alternative: last_seq on ImuSlot only. POS/GPS consume seq
    // 1 and 2, so the next IMU at seq 3 looks like two drops.
    uint8_t imu_last = 0;
    bool imu_seq_valid = true;
    const uint8_t next_imu = 3;
    const uint8_t false_gap = static_cast<uint8_t>((next_imu - imu_last - 1) & 0xFF);
    if (false_gap != 2) return fail("per-slot IMU last_seq would report 2 false gaps");
    (void)imu_seq_valid;

    // a real drop still shows up on the shared tracker (seq 3 then 5).
    if (note_component_seq(s, 5) != 1) return fail("shared tracker missed a real drop");
    if (s.seq_gaps != 1) return fail("seq_gaps should hold the real drop");

    return 0;
}
