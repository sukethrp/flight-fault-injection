#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rt {

const char* platform_name();
int64_t now_ns();
void sleep_until_ns(int64_t deadline_ns);

struct RtConfig {
    int     priority    = 80;   // SCHED_FIFO priority, Linux only
    int     core        = -1;   // -1 means do not pin
    int64_t period_ns        = 0;  // required by the Darwin time-constraint policy
    int64_t computation_ns   = 0;  // 0: derive as a fraction of period_ns
    int64_t constraint_ns    = 0;  // 0: derive as a fraction of period_ns
    bool    lock_memory          = true;
    bool    scheduler_requested  = false;
};

// What actually stuck. Goes in the CSV header so a figure can never claim a
// configuration the process did not have.
struct RtStatus {
    bool        scheduler_applied = false;
    bool        memory_locked     = false;
    bool        affinity_set      = false;
    int64_t     computation_ns    = 0;  // Darwin constraint computation; 0 if unused
    int64_t     constraint_ns     = 0;  // Darwin constraint deadline; 0 if unused
    int         preemptible       = 0;  // Darwin THREAD_TIME_CONSTRAINT_POLICY; 0 if unused
    std::string note;
};

// Best effort, never aborts. The same binary runs unprivileged on a laptop and
// privileged on the RT host.
RtStatus apply(const RtConfig& cfg);

void prefault_stack(size_t bytes = 512 * 1024);

}
