#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// One row per loop iteration. POD, small, written from inside the hot loop.
struct Sample {
    int64_t  deadline_ns;
    int32_t  wake_err_ns;   // actual wake minus deadline. scheduler lateness.
    int32_t  exec_ns;       // how long the work in this period took
    uint32_t seq;
    uint32_t flags;
};

// Wake error and budget violations are different failure modes. A 50 ms stall
// followed by a rebase leaves no trace in wake_err at all, so the flags are the
// only record that it happened.
enum : uint32_t {
    FLAG_OVERRUN = 1u << 0,  // work ran past the following deadline
    FLAG_REBASED = 1u << 1,  // deadline skipped forward, periods were dropped
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
        std::fprintf(f, "seq,deadline_ns,wake_err_ns,exec_ns,flags\n");
        for (size_t i = 0; i < n_; ++i) {
            const Sample& s = buf_[i];
            std::fprintf(f, "%u,%lld,%d,%d,%u\n", s.seq,
                         static_cast<long long>(s.deadline_ns), s.wake_err_ns,
                         s.exec_ns, s.flags);
        }
        std::fclose(f);
        return true;
    }

  private:
    std::vector<Sample> buf_;
    size_t              n_;
};
