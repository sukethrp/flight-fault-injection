#include "rt_platform.h"

#if defined(__APPLE__)

#include <mach/mach_time.h>
#include <time.h>

namespace rt {

const char* platform_name() { return "darwin"; }

int64_t now_ns() {
    return static_cast<int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
}

// mach_wait_until takes mach ticks, not nanoseconds. Apple Silicon runs a
// 24 MHz timer, so numer/denom is 125/3 and the units differ by about 40x.
// Fetched once because mach_timebase_info is a syscall.
static uint64_t ns_to_ticks(int64_t ns) {
    static const mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t{};
        mach_timebase_info(&t);
        return t;
    }();
    return static_cast<uint64_t>(ns) * tb.denom / tb.numer;
}

void sleep_until_ns(int64_t deadline_ns) {
    mach_wait_until(ns_to_ticks(deadline_ns));
}

}

#elif defined(__linux__)

#include <errno.h>
#include <time.h>

namespace rt {

const char* platform_name() { return "linux"; }

int64_t now_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void sleep_until_ns(int64_t deadline_ns) {
    timespec ts{};
    ts.tv_sec  = static_cast<time_t>(deadline_ns / 1000000000LL);
    ts.tv_nsec = static_cast<long>(deadline_ns % 1000000000LL);
    // clock_nanosleep returns the error number directly, it does not set errno
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
    }
}

}

#else
#error unsupported platform
#endif
