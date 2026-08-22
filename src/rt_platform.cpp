#include "rt_platform.h"

#if defined(__APPLE__)

#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <sys/mman.h>
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
    // round up. truncating down puts the tick deadline up to 41 ns before the
    // requested nanosecond, so wake_err can go slightly negative.
    return (static_cast<uint64_t>(ns) * tb.denom + tb.numer - 1) / tb.numer;
}

void sleep_until_ns(int64_t deadline_ns) {
    mach_wait_until(ns_to_ticks(deadline_ns));
}

RtStatus apply(const RtConfig& cfg) {
    RtStatus st;

    if (cfg.scheduler_requested && cfg.period_ns > 0) {
        thread_time_constraint_policy_data_t pol{};
        // 3/16 computation, 1/2 constraint. 750 µs / 2 ms of a 4 ms period
        // was 1.47x p2_socket exec 511.3 µs. at 1 kHz that claim is 188 µs
        // and 511 µs of work misses every tick.
        int64_t computation_ns = cfg.computation_ns > 0
            ? cfg.computation_ns : cfg.period_ns * 3 / 16;
        int64_t constraint_ns = cfg.constraint_ns > 0
            ? cfg.constraint_ns : cfg.period_ns / 2;
        if (constraint_ns > cfg.period_ns) {
            constraint_ns = cfg.period_ns;
            st.note += "constraint_ns clamped to period_ns; ";
        }
        pol.period      = static_cast<uint32_t>(ns_to_ticks(cfg.period_ns));
        pol.computation = static_cast<uint32_t>(ns_to_ticks(computation_ns));
        pol.constraint  = static_cast<uint32_t>(ns_to_ticks(constraint_ns));
        pol.preemptible = 0;  // seq 3884 was descheduled inside busy_ns

        const kern_return_t kr = thread_policy_set(
            pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
            reinterpret_cast<thread_policy_t>(&pol), THREAD_TIME_CONSTRAINT_POLICY_COUNT);
        if (kr == KERN_SUCCESS) {
            st.scheduler_applied = true;
            st.computation_ns    = computation_ns;
            st.constraint_ns     = constraint_ns;
            st.preemptible       = 0;
        } else {
            st.note += "THREAD_TIME_CONSTRAINT_POLICY rejected; ";
        }
    } else if (cfg.scheduler_requested) {
        st.note += "no period given, scheduler policy skipped; ";
    }

    if (cfg.lock_memory) {
        if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) st.memory_locked = true;
        else st.note += "mlockall denied (expected without root on macOS); ";
    }

    if (cfg.core >= 0) st.note += "core pinning unavailable on macOS; ";
    return st;
}

}

#elif defined(__linux__)

#include <sched.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>

namespace rt {

const char* platform_name() { return "linux"; }

int64_t now_ns() {
    // untested: no Linux host exists for this project. retained so the
    // platform layer stays honest about what it was designed for.
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void sleep_until_ns(int64_t deadline_ns) {
    // untested: no Linux host exists for this project. retained so the
    // platform layer stays honest about what it was designed for.
    timespec ts{};
    ts.tv_sec  = static_cast<time_t>(deadline_ns / 1000000000LL);
    ts.tv_nsec = static_cast<long>(deadline_ns % 1000000000LL);
    // clock_nanosleep returns the error number directly, it does not set errno
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
    }
}

RtStatus apply(const RtConfig& cfg) {
    // untested: no Linux host exists for this project. retained so the
    // platform layer stays honest about what it was designed for.
    RtStatus st;

    if (cfg.scheduler_requested) {
        sched_param p{};
        p.sched_priority = cfg.priority;
        if (sched_setscheduler(0, SCHED_FIFO, &p) == 0) st.scheduler_applied = true;
        else st.note += "SCHED_FIFO denied (needs root or CAP_SYS_NICE); ";
    }

    if (cfg.lock_memory) {
        if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) st.memory_locked = true;
        else st.note += "mlockall denied; ";
    }

    if (cfg.core >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cfg.core, &set);
        if (sched_setaffinity(0, sizeof(set), &set) == 0) st.affinity_set = true;
        else st.note += "affinity failed; ";
    }
    return st;
}

}

#else
#error unsupported platform
#endif

namespace rt {

void prefault_stack(size_t bytes) {
    volatile char buf[65536];
    for (size_t i = 0; i < sizeof(buf); i += 4096) buf[i] = 0;
    if (bytes > sizeof(buf)) prefault_stack(bytes - sizeof(buf));
    // keep this frame live. clang -O2 otherwise emits `b prefault_stack`
    // and only the first 64 KB is ever dirtied.
    buf[0] = 0;
}

}
