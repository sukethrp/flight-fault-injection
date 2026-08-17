#include "ring_log.h"
#include "rt_platform.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Args {
    int         hz      = 250;
    int         seconds = 60;
    int         warmup  = 2000;
    int         load_us = 0;
    std::string label;
    std::string out = "results/loop.csv";
};

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string f = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", f.c_str()); std::exit(2); }
            return argv[++i];
        };
        if      (f == "--hz")      a.hz      = std::atoi(next());
        else if (f == "--seconds") a.seconds = std::atoi(next());
        else if (f == "--warmup")  a.warmup  = std::atoi(next());
        else if (f == "--load-us") a.load_us = std::atoi(next());
        else if (f == "--label")   a.label   = next();
        else if (f == "--out")     a.out     = next();
        else { std::fprintf(stderr, "unknown flag %s\n", f.c_str()); return false; }
    }
    return a.hz > 0 && a.seconds > 0;
}

inline void busy_ns(int64_t ns) {
    if (ns <= 0) return;
    const int64_t until = rt::now_ns() + ns;
    while (rt::now_ns() < until) {
    }
}

}

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) return 2;

    const int64_t period_ns = 1000000000LL / a.hz;
    const int64_t load_ns   = static_cast<int64_t>(a.load_us) * 1000LL;
    const size_t  total     = static_cast<size_t>(a.hz) * a.seconds + a.warmup;

    RingLog  log(total - a.warmup + 16);
    uint32_t overruns = 0;
    uint32_t rebases  = 0;

    int64_t next = rt::now_ns() + period_ns;

    // HOT PATH BEGIN
    for (size_t i = 0; i < total; ++i) {
        rt::sleep_until_ns(next);
        const int64_t woke = rt::now_ns();

        busy_ns(load_ns);

        const int64_t done = rt::now_ns();

        Sample s{};
        s.deadline_ns = next;
        s.wake_err_ns = static_cast<int32_t>(woke - next);
        s.exec_ns     = static_cast<int32_t>(done - woke);
        s.seq         = static_cast<uint32_t>(i);
        if (done > next + period_ns) { s.flags |= FLAG_OVERRUN; }

        next += period_ns;

        const int64_t now = rt::now_ns();
        if (next <= now) {
            // without +1, lag smaller than one period leaves next in the past
            // and sleep_until_ns returns immediately.
            const int64_t missed = (now - next) / period_ns + 1;
            next += missed * period_ns;
            s.flags |= FLAG_REBASED;
        }

        if (i >= static_cast<size_t>(a.warmup)) {
            if (s.flags & FLAG_OVERRUN) ++overruns;
            if (s.flags & FLAG_REBASED) ++rebases;
            log.push(s);
        }
    }
    // HOT PATH END

    const std::vector<std::string> meta = {
        std::string("platform=") + rt::platform_name(),
        "label=" + (a.label.empty() ? std::string(rt::platform_name()) : a.label),
        "hz=" + std::to_string(a.hz),
        "period_ns=" + std::to_string(period_ns),
        "load_us=" + std::to_string(a.load_us),
        "warmup_discarded=" + std::to_string(a.warmup),
        "overruns=" + std::to_string(overruns),
        "rebases=" + std::to_string(rebases),
    };

    if (!log.write_csv(a.out, meta)) {
        std::fprintf(stderr, "could not write %s\n", a.out.c_str());
        return 1;
    }
    std::fprintf(stderr, "%s: %zu samples, %u overruns, %u rebases\n", a.out.c_str(),
                 log.size(), overruns, rebases);
    return 0;
}
