#pragma once

#include "detectors.h"
#include "rt_platform.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

enum class FailsafeState : uint8_t {
    NOMINAL  = 0,
    DEGRADED = 1,
    SAFE     = 2,
    LOCKED   = 3,
};

inline const char* failsafe_state_name(FailsafeState s) {
    switch (s) {
    case FailsafeState::NOMINAL:  return "NOMINAL";
    case FailsafeState::DEGRADED: return "DEGRADED";
    case FailsafeState::SAFE:     return "SAFE";
    case FailsafeState::LOCKED:   return "LOCKED";
    }
    return "UNKNOWN";
}

// Snapshot the FSM reads each tick. Built from DetectorOutput plus estimator
// error so recovery predicates can see both.
struct FailsafeInput {
    uint32_t det_mask;
    uint32_t det_rising;
    uint32_t nis_rejects;
    float    est_err_m;     // ||pos_hat - pos_meas|| or truth residual
    float    trace_p;
    int64_t  mono_ns;       // rt::now_ns() at evaluate; stamped on transitions
};

struct FailsafeConfig {
    // confirmation: consecutive ticks a predicate must hold before the edge.
    uint16_t confirm_to_degraded = 3;
    uint16_t confirm_to_safe     = 3;
    uint16_t confirm_to_locked   = 5;
    uint16_t confirm_to_nominal  = 10;

    // hysteresis hold times (ns). asymmetric on purpose: climb fast, recover slow.
    int64_t hold_degraded_ns = 200000000;   // 200 ms
    int64_t hold_safe_ns     = 500000000;   // 500 ms
    int64_t hold_locked_ns   = 2000000000;  // 2 s
    int64_t hold_nominal_ns  = 1000000000;  // 1 s before leaving a fault state

    float est_err_recover_m = 0.5f;
};

struct FailsafeEvent {
    int64_t  mono_ns;     // rt::now_ns() at the transition
    uint8_t  from;
    uint8_t  to;
    uint32_t det_mask;    // detector mask that accompanied the edge
    uint32_t seq;         // monotonic event index
};

// Preallocated transition sink. push() is hot-path safe; write_csv after the run.
class FailsafeEventLog {
  public:
    explicit FailsafeEventLog(size_t capacity) : buf_(capacity), n_(0) {}

    inline void push(const FailsafeEvent& e) noexcept {
        if (n_ < buf_.size()) buf_[n_++] = e;
    }

    size_t size() const { return n_; }
    const FailsafeEvent* data() const { return buf_.data(); }

    bool write_csv(const std::string& path, const std::vector<std::string>& meta) const {
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return false;
        for (const auto& m : meta) std::fprintf(f, "# %s\n", m.c_str());
        std::fprintf(f, "mono_ns,from,to,det_mask,seq\n");
        for (size_t i = 0; i < n_; ++i) {
            const FailsafeEvent& e = buf_[i];
            std::fprintf(f, "%lld,%u,%u,%u,%u\n",
                         static_cast<long long>(e.mono_ns),
                         static_cast<unsigned>(e.from),
                         static_cast<unsigned>(e.to),
                         e.det_mask, e.seq);
        }
        std::fclose(f);
        return true;
    }

  private:
    std::vector<FailsafeEvent> buf_;
    size_t                     n_;
};

class Failsafe {
  public:
    Failsafe(FailsafeConfig cfg, FailsafeEventLog* log);

    void reset();

    FailsafeState state() const { return state_; }
    const FailsafeConfig& config() const { return cfg_; }

    // Advances confirmation counters and hysteresis timers. Transition
    // predicates and the hold policy are author-owned (see cpp).
    void evaluate(const FailsafeInput& in);

  private:
    FailsafeConfig   cfg_;
    FailsafeEventLog* log_;
    FailsafeState    state_;
    uint32_t         event_seq_;

    uint16_t confirm_degraded_;
    uint16_t confirm_safe_;
    uint16_t confirm_locked_;
    uint16_t confirm_nominal_;

    int64_t entered_ns_;       // when state_ was entered
    int64_t last_eval_ns_;

    void transition_to(FailsafeState next, const FailsafeInput& in);
};
