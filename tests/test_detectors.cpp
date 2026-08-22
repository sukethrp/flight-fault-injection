#include "detectors.h"
#include "failsafe.h"
#include "ring_log.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int fails = 0;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++fails;
    }
}

static DetectorInput base_in(int64_t mono) {
    DetectorInput in{};
    in.mono_ns    = mono;
    in.age_imu_ns = 0;
    in.age_pos_ns = 0;
    in.age_gps_ns = 0;
    in.seq_gaps   = 0;
    in.skew_ns    = kSkewNone;
    in.flags      = 0;
    in.sensor_sample = false;
    in.trace_p    = 1.0f;
    in.nis_rejects = 0;
    return in;
}

static int test_floors() {
    DetectorConfig cfg;
    cfg.loop_period_ns = 4000000;
    cfg.staleness_limit_periods = 3;
    cfg.skew_window = 64;
    cfg.overrun_streak = 3;
    cfg.stuck_window = 25;
    Detectors d(cfg);
    const DetectorFloors f = d.floors();
    expect(f.stale_imu_ns == 8000000, "imu floor 8 ms");
    expect(f.stale_pos_ns == 60000000, "pos floor 60 ms");
    expect(f.stale_gps_ns == 600000000, "gps floor 600 ms");
    expect(f.seq_gap_ns == 4000000, "seq gap floor one period");
    expect(f.clock_skew_ns == 64LL * 4000000, "skew floor window*T");
    expect(f.deadline_miss_ns == 3LL * 4000000, "deadline floor N*T");
    expect(f.stuck_sensor_ns == 25LL * 4000000, "stuck floor N*T");
    expect(f.est_diverge_ns == 4000000, "diverge floor one period");
    return 0;
}

static int test_staleness_and_seq() {
    DetectorConfig cfg;
    cfg.loop_period_ns = 4000000;
    cfg.staleness_limit_periods = 3;
    Detectors d(cfg);

    DetectorInput in = base_in(1000000000LL);
    in.age_imu_ns = 8000001;  // past 7.5 ms limit
    DetectorOutput o = d.evaluate(in);
    expect((o.mask & DET_STALE_IMU) != 0, "stale imu fires");
    expect((o.rising & DET_STALE_IMU) != 0, "stale imu rising");
    expect(o.fire_mono_ns[0] == in.mono_ns, "stale imu fire stamp");

    in = base_in(1000004000LL);
    in.age_imu_ns = 8000001;
    o = d.evaluate(in);
    expect((o.rising & DET_STALE_IMU) == 0, "no second rising while held");

    in = base_in(1000008000LL);
    in.age_imu_ns = 0;
    in.seq_gaps = 2;
    o = d.evaluate(in);
    expect((o.mask & DET_SEQ_GAP) != 0, "seq gap fires");
    expect((o.mask & DET_STALE_IMU) == 0, "stale clears");
    return 0;
}

static int test_deadline_streak() {
    DetectorConfig cfg;
    cfg.loop_period_ns = 4000000;
    cfg.overrun_streak = 3;
    Detectors d(cfg);
    for (int i = 0; i < 2; ++i) {
        DetectorInput in = base_in(1000000000LL + i * 4000000LL);
        in.flags = FLAG_OVERRUN;
        DetectorOutput o = d.evaluate(in);
        expect((o.mask & DET_DEADLINE_MISS) == 0, "streak < N quiet");
    }
    DetectorInput in = base_in(1000000000LL + 2 * 4000000LL);
    in.flags = FLAG_OVERRUN;
    DetectorOutput o = d.evaluate(in);
    expect((o.mask & DET_DEADLINE_MISS) != 0, "streak N fires");
    return 0;
}

static int test_stuck_sensor() {
    DetectorConfig cfg;
    cfg.loop_period_ns = 4000000;
    cfg.stuck_window = 8;
    cfg.stuck_var_eps = 1e-8f;
    Detectors d(cfg);
    DetectorOutput o{};
    for (int i = 0; i < 8; ++i) {
        DetectorInput in = base_in(1000000000LL + i * 4000000LL);
        in.sensor_sample = true;
        in.sensor[0] = 1.0f;
        in.sensor[1] = -2.0f;
        in.sensor[2] = 0.5f;
        o = d.evaluate(in);
    }
    expect((o.mask & DET_STUCK_SENSOR) != 0, "frozen accel trips stuck");
    expect(o.sensor_var < cfg.stuck_var_eps, "variance under eps");

    d.reset();
    for (int i = 0; i < 8; ++i) {
        DetectorInput in = base_in(2000000000LL + i * 4000000LL);
        in.sensor_sample = true;
        in.sensor[0] = static_cast<float>(i);
        in.sensor[1] = static_cast<float>(i * i);
        in.sensor[2] = -static_cast<float>(i);
        o = d.evaluate(in);
    }
    expect((o.mask & DET_STUCK_SENSOR) == 0, "moving accel stays quiet");
    return 0;
}

static int test_clock_skew_slope() {
    DetectorConfig cfg;
    cfg.loop_period_ns = 4000000;
    cfg.skew_window = 16;
    cfg.skew_ppm_limit = 200.0f;
    Detectors d(cfg);

    // 1000 ppm: skew grows 1000 ns per 1e6 ns of mono → 1e-3 ns/ns = 1000 ppm.
    DetectorOutput o{};
    for (int i = 0; i < 16; ++i) {
        const int64_t t = 1000000000LL + i * 4000000LL;
        DetectorInput in = base_in(t);
        in.skew_ns = static_cast<int32_t>(i * 4000);  // 4000 ns / 4 ms = 1000 ppm
        o = d.evaluate(in);
    }
    expect(std::fabs(o.skew_ppm) > 200.0f, "fitted slope above 200 ppm");
    expect((o.mask & DET_CLOCK_SKEW) != 0, "clock skew fires");
    return 0;
}

static int test_divergence_and_nis_passthrough() {
    DetectorConfig cfg;
    cfg.trace_p_limit = 50.0f;
    Detectors d(cfg);
    DetectorInput in = base_in(42);
    in.trace_p = 51.0f;
    in.nis_rejects = 7;
    DetectorOutput o = d.evaluate(in);
    expect((o.mask & DET_EST_DIVERGE) != 0, "trace_p trips diverge");
    expect(o.nis_rejects == 7, "NIS reject count mirrored");
    return 0;
}

static int test_failsafe_scaffold() {
    FailsafeEventLog log(16);
    Failsafe fs(FailsafeConfig{}, &log);
    expect(fs.state() == FailsafeState::NOMINAL, "starts NOMINAL");

    FailsafeInput in{};
    in.det_mask = DET_STALE_GPS;
    in.mono_ns = 123456789LL;
    fs.evaluate(in);
    expect(fs.state() == FailsafeState::NOMINAL, "stub evaluate does not transition");
    expect(log.size() == 0, "stub leaves event log empty");

    // transition logging path (same stamp helper the author will call).
    FailsafeEvent e{};
    e.mono_ns = rt::now_ns();
    e.from = static_cast<uint8_t>(FailsafeState::NOMINAL);
    e.to = static_cast<uint8_t>(FailsafeState::DEGRADED);
    e.det_mask = DET_STALE_GPS;
    e.seq = 0;
    log.push(e);
    expect(log.size() == 1, "event log accepts a transition");
    expect(log.data()[0].from == 0 && log.data()[0].to == 1, "NOMINAL→DEGRADED");
    return 0;
}

int main() {
    test_floors();
    test_staleness_and_seq();
    test_deadline_streak();
    test_stuck_sensor();
    test_clock_skew_slope();
    test_divergence_and_nis_passthrough();
    test_failsafe_scaffold();
    if (fails) {
        std::fprintf(stderr, "%d assertion(s) failed\n", fails);
        return 1;
    }
    std::fprintf(stderr, "test_detectors: ok\n");
    return 0;
}
