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
    int64_t period_ns   = 0;    // required by the Darwin time-constraint policy
    bool    lock_memory = true;
    bool    scheduler   = false;
};

// What actually stuck. Goes in the CSV header so a figure can never claim a
// configuration the process did not have.
struct RtStatus {
    bool        scheduler_applied = false;
    bool        memory_locked     = false;
    bool        affinity_set      = false;
    std::string note;
};

// Best effort, never aborts. The same binary runs unprivileged on a laptop and
// privileged on the RT host.
RtStatus apply(const RtConfig& cfg);

void prefault_stack(size_t bytes = 512 * 1024);

}
