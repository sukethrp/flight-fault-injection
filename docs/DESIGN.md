# Design

The repo exists to produce two artifacts: a loop jitter histogram out to
p99.99, and a fault table with time-to-detect and time-to-recover. This
file is the sequencing of work that produces them.

## Phase 1 — instrument

Fixed-rate loop, per-iteration wake error, monotonic clock, absolute
deadlines, RT policy with honest status. Measured. Numbers in
`results/pooled.md`.

## Phase 2 — plant I/O

The plant is PX4 SITL speaking MAVLink over UDP. The measurement that
matters first is what non-blocking UDP I/O does to the histogram. PX4 on
Apple Silicon is a toolchain unknown; this machine already hit that wall
in epuck-edge-ai. Standing SITL up before the drain would put that
unknown in front of the number.

So the socket is characterized against a dummy sender. Any UDP source
answers 2a. PX4 replaces the sender in 2e with no change to the drain.

| | | owner |
|---|---|---|
| 2a | UDP sender fixture + bounded non-blocking drain, jitter re-measured | you (drain) |
| 2b | MAVLink v2 headers, real frame parse, parse cost measured separately | split |
| 2c | latest-value slots, dual timestamps, sequence gaps | you |
| 2d | staleness accounting per message class | split |
| 2e | PX4 SITL replaces the sender fixture | you |
| 2f | final campaign with the real plant | you |

Two measured deltas (socket, then parse), each one variable. Same
discipline as Phase 1.

The drain is bounded. An unbounded `recv` loop turns a traffic burst
into a deadline miss. Latest-value slots in 2c, not a queue: a queue
turns a timing problem into a memory problem and then into an allocation
in the hot path.

## Later

Step 4 estimator, step 5 injector, step 6 detectors and failsafe.
Details land with those steps.
