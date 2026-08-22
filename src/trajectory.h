#pragma once

#include <cstdint>

// Position setpoint as a pure function of the control-tick index. Fault runs
// compare against a bit-identical nominal; wall-clock time would diverge as
// soon as two runs start a millisecond apart.
void trajectory_setpoint(uint64_t ctrl_tick, float pos_sp[3]);
