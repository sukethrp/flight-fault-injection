#include "msg_slots.h"

#include <cstdio>

int main() {
    const int64_t limit_ns  = kImuPeriodNs * 3;  // 7.5 ms
    const int64_t period_ns = 4000000;           // 250 Hz
    const int64_t floor_ns  = staleness_floor_ns(limit_ns, period_ns);
    if (limit_ns != 7500000) {
        std::fprintf(stderr, "limit %lld want 7500000\n",
                     static_cast<long long>(limit_ns));
        return 1;
    }
    if (floor_ns != 8000000) {
        std::fprintf(stderr, "floor %lld want 8000000\n",
                     static_cast<long long>(floor_ns));
        return 1;
    }
    // one silent 4 ms tick is under the limit; two tick boundaries are the floor.
    if (!(period_ns < limit_ns && 2 * period_ns == floor_ns)) {
        std::fprintf(stderr, "first miss should sit under the limit\n");
        return 1;
    }
    if (staleness_floor_ns(period_ns, period_ns) != period_ns) {
        std::fprintf(stderr, "exact multiple should not round up\n");
        return 1;
    }
    return 0;
}
