#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

constexpr int32_t kSkewNone = INT32_MIN;  // no IMU this tick; not a skew sample
constexpr int64_t kAgeNone  = INT64_MIN;  // slot never filled; not an age sample

// Decimation class for this tick. Bitmask so control+telem co-issue on the 25th.
enum : uint8_t {
    TICK_CTRL  = 1u << 0,  // every 5th tick at 250 Hz → 50 Hz
    TICK_TELEM = 1u << 1,  // every 25th tick → 10 Hz
};

// One row per loop iteration. POD, small, written from inside the hot loop.
struct Sample {
    int64_t  deadline_ns;
    int32_t  wake_err_ns;   // actual wake minus deadline. scheduler lateness.
    int32_t  exec_ns;       // how long the work in this period took
    int32_t  rx_ns;         // drain+parse+slot as one receive path. not parse_ns; ekf_ns lands beside this.
    int32_t  ctrl_ns;       // control+setpoint stage. 0 when TICK_CTRL is clear; analysis drops those.
    uint32_t seq;
    uint32_t flags;
    uint16_t rx_count;      // datagrams drained this tick. 0 with no socket.
    uint16_t seq_gaps;      // missing MAVLink seq numbers this tick. wrapping uint8_t. component-wide.
    int32_t  skew_ns;       // sender_us*1000 - rx_mono_ns. detector uses the slope; PX4 offset is arbitrary. kSkewNone if silent.
    int64_t  age_imu_ns;    // woke - rx_mono_ns at read. kAgeNone if the slot was never filled.
    int64_t  age_pos_ns;
    int64_t  age_gps_ns;
    uint8_t  tick_class;    // TICK_CTRL / TICK_TELEM bits. separates expensive ticks in analysis.
};

// Wake error and budget violations are different failure modes. A 50 ms stall
// followed by a rebase leaves no trace in wake_err at all, so the flags are the
// only record that it happened.
enum : uint32_t {
    FLAG_OVERRUN    = 1u << 0,  // work ran past the following deadline
    FLAG_REBASED    = 1u << 1,  // deadline skipped forward, periods were dropped
    FLAG_DRAIN_FULL = 1u << 2,  // recv hit its per-tick bound; leftover or bound too tight
    FLAG_STALE      = 1u << 3,  // any slot age > its expected_period * limit. instrumentation; detector is step 6
};

// Fixed capacity. No allocation, no locks, no I/O in push().
class RingLog {
  public:
    explicit RingLog(size_t capacity) : buf_(capacity), n_(0) {}

    inline void push(const Sample& s) noexcept {
        if (n_ < buf_.size()) buf_[n_++] = s;
    }

    size_t size() const { return n_; }

    bool write_csv(const std::string& path, const std::vector<std::string>& meta) const {
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return false;
        for (const auto& m : meta) std::fprintf(f, "# %s\n", m.c_str());
        std::fprintf(f, "seq,deadline_ns,wake_err_ns,exec_ns,rx_ns,ctrl_ns,flags,rx_count,seq_gaps,skew_ns,age_imu_ns,age_pos_ns,age_gps_ns,tick_class\n");
        for (size_t i = 0; i < n_; ++i) {
            const Sample& s = buf_[i];
            std::fprintf(f, "%u,%lld,%d,%d,%d,%d,%u,%u,%u,%d,%lld,%lld,%lld,%u\n", s.seq,
                         static_cast<long long>(s.deadline_ns), s.wake_err_ns,
                         s.exec_ns, s.rx_ns, s.ctrl_ns, s.flags,
                         static_cast<unsigned>(s.rx_count),
                         static_cast<unsigned>(s.seq_gaps), s.skew_ns,
                         static_cast<long long>(s.age_imu_ns),
                         static_cast<long long>(s.age_pos_ns),
                         static_cast<long long>(s.age_gps_ns),
                         static_cast<unsigned>(s.tick_class));
        }
        std::fclose(f);
        return true;
    }

  private:
    std::vector<Sample> buf_;
    size_t              n_;
};
