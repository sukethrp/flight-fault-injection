# Engineering log

Dated entries, newest at the bottom. One entry per work session. This is the
reasoning trail behind the commits: what was tried, what the numbers were, what
got rejected and why. Rejected approaches are as valuable as accepted ones.

---

## 2026-08-15 - scope, platform, and repo setup

**Shape of the project.** Simulated vehicle as the plant, my own estimator and
controller as a fixed-rate real-time process, a fault injector on the wire
between them. Two deliverables: a loop jitter histogram out to p99.99 in
microseconds, and a fault table with time-to-detect and time-to-recover per
injected fault. The demo is not the artifact; the measurements are.

**Platform problem.** Dev machine is an Apple M5 and PREEMPT_RT does not exist
on Darwin. A Linux VM under the hypervisor is not usable for tail latency:
virtualized timers and host scheduler noise are larger than the effect being
measured. Ordering a small x86 box for bare-metal Ubuntu with the RT kernel, and
writing everything behind a platform layer meanwhile.

Consequence, and it is an improvement: the macOS run stops being a throwaway and
becomes the first of three curves, with the kernel as the only variable.

1. macOS, mach_wait_until plus THREAD_TIME_CONSTRAINT_POLICY, best effort
2. Linux, CFS, default scheduling
3. Linux, PREEMPT_RT, SCHED_FIFO on an isolated core

**Rejected: PX4 EKF2 as the estimator.** Letting the flight stack estimate means
observing someone else's filter degrade rather than authoring the degradation,
which removes the part of the project worth having. PX4 SITL is the plant and
the MAVLink transport only.

**Rejected: ROS 2 on the inner loop.** DDS discovery and executor overhead would
dominate the histogram, so the measurement would be of the middleware rather
than the scheduler. Raw UDP plus MAVLink.

**Rejected: Python for the loop.** GIL and GC pauses put p99.99 in the
milliseconds with no tuning path out. C++17 for the loop, Python for analysis.

**Rejected: a 15-state filter.** Every behaviour the fault table needs (gating,
divergence, dead reckoning through GPS loss, covariance growth as an
observability signal) shows up in 6 states. Starting there.

**Open.** Sample count for a defensible p99.99, and therefore run duration. Ten
samples past the quantile is the minimum, so 100k samples, which at 250 Hz is
about 7 minutes. Planning one hour per configuration for margin.

**Environment.** clang 21, cmake via Homebrew, python 3.13. Commit hook installed
from scripts/pre-commit: blocks agent-shell commits, oversized files,
uncompressed result CSVs, and banned calls between the hot path sentinels.
