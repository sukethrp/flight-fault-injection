#include "msg_slots.h"
#include "ring_log.h"
#include "rt_platform.h"
#include "udp_rx.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

struct Args {
    int         hz      = 250;
    int         seconds = 60;
    int         warmup  = 2000;
    int         load_us = 0;
    int         prio    = 80;
    int         core    = -1;
    int         port    = 0;
    bool        rt      = false;
    std::string label;
    std::string out = "results/loop.csv";
};

// 400 Hz sender / 250 Hz loop = 1.6 datagrams per tick. 8 is 5× that mean,
// enough to absorb a 20 ms burst (400 × 0.020) without an unbounded recv.
constexpr uint16_t kMaxMsgsPerTick = 8;

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
        else if (f == "--prio")    a.prio    = std::atoi(next());
        else if (f == "--core")    a.core    = std::atoi(next());
        else if (f == "--rt")      a.rt      = true;
        else if (f == "--label")   a.label   = next();
        else if (f == "--out")     a.out     = next();
        else if (f == "--port")    a.port     = std::atoi(next());
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
    uint32_t overruns    = 0;
    uint32_t rebases     = 0;
    uint32_t rx_total    = 0;
    uint32_t drain_fulls = 0;
    uint32_t parse_ok    = 0;
    uint32_t seq_gaps    = 0;

    rt::RtConfig cfg;
    cfg.priority             = a.prio;
    cfg.core                 = a.core;
    cfg.period_ns            = period_ns;
    cfg.scheduler_requested  = a.rt;
    const rt::RtStatus st = rt::apply(cfg);

    char rxbuf[512]{};
    // parser state spans ticks. a per-iteration {} drops a frame that
    // straddled two recv bursts.
    mavlink_message_t mav_msg{};
    mavlink_status_t  mav_status{};
    MsgSlots slots{};
    int sock = -1;
    if (a.port > 0) {
        sock = udp_bind_nonblocking(a.port);
        if (sock < 0) {
            std::fprintf(stderr, "could not bind UDP %d\n", a.port);
            return 1;
        }
    }

    rt::prefault_stack();

    int64_t next = rt::now_ns() + period_ns;

    // HOT PATH BEGIN
    for (size_t i = 0; i < total; ++i) {
        rt::sleep_until_ns(next);
        const int64_t woke = rt::now_ns();

        uint16_t rx = 0;
        uint16_t parsed = 0;
        uint16_t gaps = 0;
        if (sock >= 0) {
            while (rx < kMaxMsgsPerTick) {
                const long n = udp_try_recv(sock, rxbuf, sizeof(rxbuf));
                if (n <= 0) break;
                ++rx;
                for (long b = 0; b < n; ++b) {
                    if (mavlink_parse_char(MAVLINK_COMM_0,
                            static_cast<uint8_t>(rxbuf[b]),
                            &mav_msg, &mav_status)) {
                        ++parsed;
                        switch (mav_msg.msgid) {
                        case MAVLINK_MSG_ID_HIGHRES_IMU: {
                            ImuSlot& imu = slots.imu;
                            if (imu.valid) {
                                // seq is uint8_t; without the mask, 255->0 is -256 not 0.
                                const uint8_t g = static_cast<uint8_t>(
                                    (mav_msg.seq - imu.last_seq - 1) & 0xFF);
                                imu.seq_gaps += g;
                                gaps = static_cast<uint16_t>(gaps + g);
                            }
                            mavlink_msg_highres_imu_decode(&mav_msg, &imu.payload);
                            imu.rx_mono_ns = woke;
                            imu.sender_us  = imu.payload.time_usec;
                            imu.last_seq   = mav_msg.seq;
                            imu.valid      = true;
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }
        }

        busy_ns(load_ns);

        const int64_t done = rt::now_ns();

        Sample s{};
        s.deadline_ns = next;
        s.wake_err_ns = static_cast<int32_t>(woke - next);
        s.exec_ns     = static_cast<int32_t>(done - woke);
        s.seq         = static_cast<uint32_t>(i);
        s.rx_count    = rx;
        s.seq_gaps    = gaps;
        s.skew_ns     = kSkewNone;
        if (slots.imu.valid && slots.imu.rx_mono_ns == woke) {
            s.skew_ns = static_cast<int32_t>(
                static_cast<int64_t>(slots.imu.sender_us * 1000ull) - slots.imu.rx_mono_ns);
        }
        if (done > next + period_ns) { s.flags |= FLAG_OVERRUN; }
        if (rx == kMaxMsgsPerTick)   { s.flags |= FLAG_DRAIN_FULL; }

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
            if (s.flags & FLAG_OVERRUN)    ++overruns;
            if (s.flags & FLAG_REBASED)    ++rebases;
            if (s.flags & FLAG_DRAIN_FULL) ++drain_fulls;
            rx_total += rx;
            parse_ok += parsed;
            seq_gaps += gaps;
            log.push(s);
        }
    }
    // HOT PATH END

    if (sock >= 0) close(sock);

    const std::vector<std::string> meta = {
        std::string("platform=") + rt::platform_name(),
        "label=" + (a.label.empty() ? std::string(rt::platform_name()) : a.label),
        "hz=" + std::to_string(a.hz),
        "period_ns=" + std::to_string(period_ns),
        "load_us=" + std::to_string(a.load_us),
        "warmup_discarded=" + std::to_string(a.warmup),
        "port=" + std::to_string(a.port),
        "max_msgs_per_tick=" + std::to_string(kMaxMsgsPerTick),
        "overruns=" + std::to_string(overruns),
        "rebases=" + std::to_string(rebases),
        "rx_total=" + std::to_string(rx_total),
        "drain_full=" + std::to_string(drain_fulls),
        "parse=" + std::string(a.port > 0 ? "1" : "0"),
        "parse_ok=" + std::to_string(parse_ok),
        "seq_gaps=" + std::to_string(seq_gaps),
        "computation_ns=" + std::to_string(st.computation_ns),
        "scheduler_requested=" + std::string(cfg.scheduler_requested ? "1" : "0"),
        "scheduler_applied=" + std::string(st.scheduler_applied ? "1" : "0"),
        "memory_locked=" + std::string(st.memory_locked ? "1" : "0"),
        "affinity_set=" + std::string(st.affinity_set ? "1" : "0"),
        "note=" + st.note,
    };

    if (!log.write_csv(a.out, meta)) {
        std::fprintf(stderr, "could not write %s\n", a.out.c_str());
        return 1;
    }
    std::fprintf(stderr,
                 "%s: %zu samples, %u overruns, %u rebases, %u rx, %u drain_full, %u parse_ok, %u seq_gaps\n",
                 a.out.c_str(), log.size(), overruns, rebases, rx_total, drain_fulls, parse_ok, seq_gaps);
    return 0;
}
