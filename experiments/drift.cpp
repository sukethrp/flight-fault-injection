#include "rt_platform.h"

#include <cstdio>
#include <ctime>

namespace {

constexpr int64_t kPeriodNs = 4000000;  // 250 Hz
constexpr int     kIters    = 2500;     // 10 seconds

double run_relative() {
    const int64_t t0 = rt::now_ns();
    for (int i = 0; i < kIters; ++i) {
        timespec req{0, static_cast<long>(kPeriodNs)};
        nanosleep(&req, nullptr);
    }
    return (rt::now_ns() - t0) / 1e9;
}

double run_absolute() {
    const int64_t t0 = rt::now_ns();
    int64_t next = t0;
    for (int i = 0; i < kIters; ++i) {
        next += kPeriodNs;
        rt::sleep_until_ns(next);
    }
    return (rt::now_ns() - t0) / 1e9;
}

}

int main() {
    const double rel = run_relative();
    const double abs_ = run_absolute();
    std::printf("target           10.000 s\n");
    std::printf("relative sleep   %.3f s   (%+.0f ms, %.2f%% slow)\n",
                rel, (rel - 10.0) * 1e3, (rel - 10.0) * 10.0);
    std::printf("absolute sleep   %.3f s   (%+.0f ms)\n", abs_, (abs_ - 10.0) * 1e3);
    return 0;
}
