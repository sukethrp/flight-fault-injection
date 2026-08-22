#pragma once

#include "msg_slots.h"
#include "ring_log.h"

#include <cstdint>

// Bitmask of which detectors are tripped this tick. NIS reject count is a
// counter, not a bit: the gate lives in ekf.cpp and is only mirrored here.
enum : uint32_t {
    DET_STALE_IMU     = 1u << 0,
    DET_STALE_POS     = 1u << 1,
    DET_STALE_GPS     = 1u << 2,
    DET_SEQ_GAP       = 1u << 3,
    DET_CLOCK_SKEW    = 1u << 4,
    DET_DEADLINE_MISS = 1u << 5,
    DET_STUCK_SENSOR  = 1u << 6,
    DET_EST_DIVERGE   = 1u << 7,
};

struct DetectorConfig {
    int64_t loop_period_ns = 4000000;  // 250 Hz
    int     staleness_limit_periods = 3;
    int64_t imu_period_ns = kImuPeriodNs;
    int64_t pos_period_ns = kPosPeriodNs;
    int64_t gps_period_ns = kGpsPeriodNs;

    // any missing seq this tick. floor is one loop period (sampled at tick).
    uint16_t seq_gap_min = 1;

    // slope of skew_ns vs rx_mono_ns, in ppm. measured single-host baseline
    // is 44 ppb (~4500x under this threshold).
    uint16_t skew_window = 64;
    float    skew_ppm_limit = 200.0f;

    uint16_t overrun_streak = 3;

    uint16_t stuck_window = 25;       // 100 ms at 250 Hz
    float    stuck_var_eps = 1e-8f;   // (m/s²)²; frozen channel sits under this

    float    trace_p_limit = 50.0f;
};

// One tick of observations. No ownership; all pointers/arrays are caller-owned.
struct DetectorInput {
    int64_t  mono_ns;          // tick wake, same clock as inject_mono_ns
    int64_t  age_imu_ns;       // kAgeNone if slot never filled
    int64_t  age_pos_ns;
    int64_t  age_gps_ns;
    uint16_t seq_gaps;         // gaps observed this tick
    int32_t  skew_ns;          // kSkewNone if no IMU this tick
    uint32_t flags;            // FLAG_OVERRUN etc.
    bool     sensor_sample;    // true when sensor[3] is a fresh IMU accel
    float    sensor[3];        // e.g. IMU accel xyz for stuck-sensor variance
    float    trace_p;          // Ekf::trace_p()
    uint32_t nis_rejects;      // Ekf::reject_count(); mirrored, not recomputed
};

struct DetectorFloors {
    int64_t stale_imu_ns;
    int64_t stale_pos_ns;
    int64_t stale_gps_ns;
    int64_t seq_gap_ns;
    int64_t clock_skew_ns;
    int64_t deadline_miss_ns;
    int64_t stuck_sensor_ns;
    int64_t est_diverge_ns;
};

struct DetectorOutput {
    uint32_t mask;             // DET_* bits currently tripped
    uint32_t rising;           // bits that transitioned 0→1 this tick
    uint32_t nis_rejects;      // passthrough of input.nis_rejects
    float    skew_ppm;         // last fitted slope; 0 if window not full
    float    sensor_var;       // last rolling variance (sum of per-axis vars)
    float    trace_p;
    int64_t  fire_mono_ns[8];  // first rising edge mono_ns per DET_* bit index; 0 if never
};

class Detectors {
  public:
    explicit Detectors(DetectorConfig cfg = {});

    void reset();

    // Fixed floors from config. Same ceil formula as staleness_floor_ns.
    DetectorFloors floors() const;

    DetectorOutput evaluate(const DetectorInput& in);

    const DetectorConfig& config() const { return cfg_; }

  private:
    static constexpr int kSkewCap  = 128;
    static constexpr int kStuckCap = 64;

    DetectorConfig cfg_;
    uint32_t       prev_mask_;
    uint16_t       overrun_run_;

    // skew regression: circular (t_ns, skew_ns) pairs. fixed cap, no heap.
    int64_t  skew_t_[kSkewCap];
    double   skew_y_[kSkewCap];
    uint16_t skew_i_;
    uint16_t skew_n_;

    // stuck sensor: circular per-axis samples.
    float    stuck_x_[kStuckCap][3];
    uint16_t stuck_i_;
    uint16_t stuck_n_;

    int64_t  fire_mono_ns_[8];

    float fit_skew_ppm();
    float stuck_variance() const;
};
