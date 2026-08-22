#include "detectors.h"

#include <cmath>

namespace {

int bit_index(uint32_t bit) {
    int i = 0;
    while (bit > 1u) {
        bit >>= 1;
        ++i;
    }
    return i;
}

}  // namespace

Detectors::Detectors(DetectorConfig cfg) : cfg_(cfg) {
    if (cfg_.skew_window > kSkewCap) cfg_.skew_window = kSkewCap;
    if (cfg_.skew_window < 2) cfg_.skew_window = 2;
    if (cfg_.stuck_window > kStuckCap) cfg_.stuck_window = kStuckCap;
    if (cfg_.stuck_window < 2) cfg_.stuck_window = 2;
    if (cfg_.overrun_streak < 1) cfg_.overrun_streak = 1;
    reset();
}

void Detectors::reset() {
    prev_mask_     = 0;
    overrun_run_   = 0;
    skew_i_        = 0;
    skew_n_        = 0;
    stuck_i_       = 0;
    stuck_n_       = 0;
    for (int i = 0; i < 8; ++i) fire_mono_ns_[i] = 0;
    for (int i = 0; i < kSkewCap; ++i) {
        skew_t_[i] = 0;
        skew_y_[i] = 0.0;
    }
    for (int i = 0; i < kStuckCap; ++i)
        for (int j = 0; j < 3; ++j) stuck_x_[i][j] = 0.0f;
}

DetectorFloors Detectors::floors() const {
    DetectorFloors f{};
    const int64_t lim = static_cast<int64_t>(cfg_.staleness_limit_periods);
    const int64_t T   = cfg_.loop_period_ns;
    f.stale_imu_ns = staleness_floor_ns(cfg_.imu_period_ns * lim, T);
    f.stale_pos_ns = staleness_floor_ns(cfg_.pos_period_ns * lim, T);
    f.stale_gps_ns = staleness_floor_ns(cfg_.gps_period_ns * lim, T);
    // gap is observed on the tick that receives the successor frame.
    f.seq_gap_ns = T;
    // slope needs a full window of skew samples before it can trip.
    f.clock_skew_ns = static_cast<int64_t>(cfg_.skew_window) * T;
    f.deadline_miss_ns = static_cast<int64_t>(cfg_.overrun_streak) * T;
    f.stuck_sensor_ns = static_cast<int64_t>(cfg_.stuck_window) * T;
    // trace(P) is sampled once per tick; an instantaneous jump clears on the
    // next evaluate.
    f.est_diverge_ns = T;
    return f;
}

float Detectors::fit_skew_ppm() {
    if (skew_n_ < cfg_.skew_window) return 0.0f;
    // Ordinary least squares of y = skew_ns on x = (t - t0) seconds.
    // slope has units ns/s = 1e-9; ppm = slope_ns_per_ns * 1e6.
    const uint16_t n = cfg_.skew_window;
    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
    // oldest sample in the filled window: skew_i_ points at next write.
    const uint16_t start = static_cast<uint16_t>((skew_i_ + kSkewCap - n) % kSkewCap);
    const int64_t t0 = skew_t_[start];
    for (uint16_t k = 0; k < n; ++k) {
        const uint16_t i = static_cast<uint16_t>((start + k) % kSkewCap);
        const double x = static_cast<double>(skew_t_[i] - t0) * 1e-9;
        const double y = skew_y_[i];
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }
    const double denom = static_cast<double>(n) * sum_xx - sum_x * sum_x;
    if (!(std::fabs(denom) > 0.0)) return 0.0f;
    // dy/dx in ns per second; divide by 1e9 for ns/ns, then *1e6 → ppm.
    const double slope_ns_per_s =
        (static_cast<double>(n) * sum_xy - sum_x * sum_y) / denom;
    const double slope_ns_per_ns = slope_ns_per_s * 1e-9;
    return static_cast<float>(slope_ns_per_ns * 1e6);
}

float Detectors::stuck_variance() const {
    if (stuck_n_ < cfg_.stuck_window) return 1.0f;  // not yet eligible
    const uint16_t n = cfg_.stuck_window;
    const uint16_t start =
        static_cast<uint16_t>((stuck_i_ + kStuckCap - n) % kStuckCap);
    float var_sum = 0.0f;
    for (int ax = 0; ax < 3; ++ax) {
        float mean = 0.0f;
        for (uint16_t k = 0; k < n; ++k) {
            const uint16_t i = static_cast<uint16_t>((start + k) % kStuckCap);
            mean += stuck_x_[i][ax];
        }
        mean /= static_cast<float>(n);
        float acc = 0.0f;
        for (uint16_t k = 0; k < n; ++k) {
            const uint16_t i = static_cast<uint16_t>((start + k) % kStuckCap);
            const float d = stuck_x_[i][ax] - mean;
            acc += d * d;
        }
        var_sum += acc / static_cast<float>(n);
    }
    return var_sum;
}

DetectorOutput Detectors::evaluate(const DetectorInput& in) {
    DetectorOutput out{};
    out.nis_rejects = in.nis_rejects;
    out.trace_p     = in.trace_p;
    uint32_t mask   = 0;

    const int64_t lim = static_cast<int64_t>(cfg_.staleness_limit_periods);
    if (in.age_imu_ns != kAgeNone && cfg_.imu_period_ns > 0 &&
        in.age_imu_ns > cfg_.imu_period_ns * lim) {
        mask |= DET_STALE_IMU;
    }
    if (in.age_pos_ns != kAgeNone && cfg_.pos_period_ns > 0 &&
        in.age_pos_ns > cfg_.pos_period_ns * lim) {
        mask |= DET_STALE_POS;
    }
    if (in.age_gps_ns != kAgeNone && cfg_.gps_period_ns > 0 &&
        in.age_gps_ns > cfg_.gps_period_ns * lim) {
        mask |= DET_STALE_GPS;
    }

    if (in.seq_gaps >= cfg_.seq_gap_min) mask |= DET_SEQ_GAP;

    if (in.skew_ns != kSkewNone) {
        skew_t_[skew_i_] = in.mono_ns;
        skew_y_[skew_i_] = static_cast<double>(in.skew_ns);
        skew_i_ = static_cast<uint16_t>((skew_i_ + 1) % kSkewCap);
        if (skew_n_ < kSkewCap) ++skew_n_;
        out.skew_ppm = fit_skew_ppm();
        if (skew_n_ >= cfg_.skew_window &&
            std::fabs(out.skew_ppm) > cfg_.skew_ppm_limit) {
            mask |= DET_CLOCK_SKEW;
        }
    } else {
        out.skew_ppm = fit_skew_ppm();
    }

    if (in.flags & FLAG_OVERRUN) {
        if (overrun_run_ < 0xffff) ++overrun_run_;
    } else {
        overrun_run_ = 0;
    }
    if (overrun_run_ >= cfg_.overrun_streak) mask |= DET_DEADLINE_MISS;

    if (in.sensor_sample) {
        stuck_x_[stuck_i_][0] = in.sensor[0];
        stuck_x_[stuck_i_][1] = in.sensor[1];
        stuck_x_[stuck_i_][2] = in.sensor[2];
        stuck_i_ = static_cast<uint16_t>((stuck_i_ + 1) % kStuckCap);
        if (stuck_n_ < kStuckCap) ++stuck_n_;
        out.sensor_var = stuck_variance();
        // window of N samples already below eps is the trip; floor is N periods.
        if (stuck_n_ >= cfg_.stuck_window && out.sensor_var < cfg_.stuck_var_eps) {
            mask |= DET_STUCK_SENSOR;
        }
    } else {
        out.sensor_var = stuck_variance();
    }

    if (in.trace_p > cfg_.trace_p_limit) mask |= DET_EST_DIVERGE;

    out.mask   = mask;
    out.rising = mask & ~prev_mask_;
    for (uint32_t bit = 1u; bit <= DET_EST_DIVERGE; bit <<= 1) {
        const int idx = bit_index(bit);
        if (out.rising & bit) fire_mono_ns_[idx] = in.mono_ns;
        out.fire_mono_ns[idx] = fire_mono_ns_[idx];
    }
    prev_mask_ = mask;
    return out;
}
