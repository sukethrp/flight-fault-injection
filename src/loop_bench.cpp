#include "control.h"
#include "msg_slots.h"
#include "ring_log.h"
#include "rt_platform.h"
#include "setpoint_tx.h"
#include "trajectory.h"
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
    int         setpoint_port = 0;
    int         computation_us = 0;
    int         constraint_us  = 0;
    int         staleness_limit_periods = 3;
    bool        rt      = false;
    std::string label;
    std::string out = "results/loop.csv";
};

// 400 Hz sender / 250 Hz loop = 1.6 datagrams per tick. 8 is 5x that mean,
// enough to absorb a 20 ms burst (400 x 0.020) without an unbounded recv.
constexpr uint16_t kMaxMsgsPerTick = 8;
constexpr uint16_t kCtrlEvery      = 5;   // 250/5 = 50 Hz
constexpr uint16_t kTelemEvery     = 25;  // 250/25 = 10 Hz

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string f = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", f.c_str());
                std::exit(2);
            }
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
        else if (f == "--port")    a.port    = std::atoi(next());
        else if (f == "--setpoint-port") a.setpoint_port = std::atoi(next());
        else if (f == "--computation-us") a.computation_us = std::atoi(next());
        else if (f == "--constraint-us")  a.constraint_us  = std::atoi(next());
        else if (f == "--staleness-limit-periods") {
            a.staleness_limit_periods = std::atoi(next());
        } else {
            std::fprintf(stderr, "unknown flag %s\n", f.c_str());
            return false;
        }
    }
    return a.hz > 0 && a.seconds > 0 && a.staleness_limit_periods > 0;
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
    const float   ctrl_dt   = static_cast<float>(period_ns * kCtrlEvery) * 1e-9f;

    RingLog  log(total - static_cast<size_t>(a.warmup) + 16);
    uint32_t overruns    = 0;
    uint32_t rebases     = 0;
    uint32_t rx_total    = 0;
    uint32_t drain_fulls = 0;
    uint32_t parse_ok    = 0;
    uint32_t seq_gaps    = 0;
    uint32_t stales      = 0;
    uint32_t stale_imu   = 0;
    uint32_t stale_pos   = 0;
    uint32_t stale_gps   = 0;
    uint32_t ctrl_ticks      = 0;
    uint32_t telem_ticks     = 0;
    uint32_t setpoint_tx     = 0;  // successful sends; compare to plant setpoint_rx
    uint32_t setpoint_tx_drop = 0; // EAGAIN / short write; not inferred from tick math

    rt::RtConfig cfg{};
    cfg.priority            = a.prio;
    cfg.core                = a.core;
    cfg.period_ns           = period_ns;
    cfg.computation_ns      = static_cast<int64_t>(a.computation_us) * 1000LL;
    cfg.constraint_ns       = static_cast<int64_t>(a.constraint_us) * 1000LL;
    cfg.scheduler_requested = a.rt;
    const rt::RtStatus st = rt::apply(cfg);

    char rxbuf[512]{};
    // parser state spans ticks. a per-iteration {} drops a frame that
    // straddled two recv bursts.
    mavlink_message_t mav_msg{};
    mavlink_status_t  mav_status{};
    MsgSlots slots{};
    slots.imu.expected_period_ns = kImuPeriodNs;
    slots.pos.expected_period_ns = kPosPeriodNs;
    slots.gps.expected_period_ns = kGpsPeriodNs;
    const int64_t stale_lim = static_cast<int64_t>(a.staleness_limit_periods);
    const int64_t imu_floor_ns = staleness_floor_ns(kImuPeriodNs * stale_lim, period_ns);
    const int64_t pos_floor_ns = staleness_floor_ns(kPosPeriodNs * stale_lim, period_ns);
    const int64_t gps_floor_ns = staleness_floor_ns(kGpsPeriodNs * stale_lim, period_ns);

    int sock = -1;
    if (a.port > 0) {
        sock = udp_bind_nonblocking(a.port);
        if (sock < 0) {
            std::fprintf(stderr, "could not bind UDP %d\n", a.port);
            return 1;
        }
    }

    Controller controller;
    SetpointTx sp_tx;
    if (a.setpoint_port > 0) {
        if (!sp_tx.open(a.setpoint_port)) {
            std::fprintf(stderr, "could not open setpoint UDP %d\n", a.setpoint_port);
            if (sock >= 0) close(sock);
            return 1;
        }
    }

    uint16_t ctrl_div  = 0;
    uint16_t telem_div = 0;

    rt::prefault_stack();
    int64_t next = rt::now_ns() + period_ns;

    // HOT PATH BEGIN
    for (size_t i = 0; i < total; ++i) {
        rt::sleep_until_ns(next);
        const int64_t woke = rt::now_ns();

        uint16_t rx = 0;
        uint16_t parsed = 0;
        uint16_t gaps = 0;
        int32_t  rx_ns = 0;
        if (sock >= 0) {
            const int64_t t0 = rt::now_ns();
            while (rx < kMaxMsgsPerTick) {
                const long n = udp_try_recv(sock, rxbuf, sizeof(rxbuf));
                if (n <= 0) break;
                ++rx;
                for (long b = 0; b < n; ++b) {
                    if (mavlink_parse_char(MAVLINK_COMM_0,
                            static_cast<uint8_t>(rxbuf[b]),
                            &mav_msg, &mav_status)) {
                        ++parsed;
                        // seq is uint8_t; without the mask, 255->0 is -256 not 0.
                        // component-wide: per-slot last_seq would flag IMU->POS as a drop.
                        gaps = static_cast<uint16_t>(
                            gaps + note_component_seq(slots, mav_msg.seq));
                        switch (mav_msg.msgid) {
                        case MAVLINK_MSG_ID_HIGHRES_IMU: {
                            ImuSlot& imu = slots.imu;
                            mavlink_msg_highres_imu_decode(&mav_msg, &imu.payload);
                            imu.rx_mono_ns = woke;
                            imu.sender_us  = imu.payload.time_usec;
                            imu.valid      = true;
                            break;
                        }
                        case MAVLINK_MSG_ID_LOCAL_POSITION_NED: {
                            PosSlot& pos = slots.pos;
                            mavlink_msg_local_position_ned_decode(&mav_msg, &pos.payload);
                            pos.rx_mono_ns = woke;
                            pos.sender_us  = static_cast<uint64_t>(pos.payload.time_boot_ms) * 1000ull;
                            pos.valid      = true;
                            break;
                        }
                        case MAVLINK_MSG_ID_GPS_RAW_INT: {
                            GpsSlot& gps = slots.gps;
                            mavlink_msg_gps_raw_int_decode(&mav_msg, &gps.payload);
                            gps.rx_mono_ns = woke;
                            gps.sender_us  = gps.payload.time_usec;
                            gps.valid      = true;
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }
            rx_ns = static_cast<int32_t>(rt::now_ns() - t0);
        }

        uint8_t tick_class = 0;
        int32_t ctrl_ns = 0;
        ++ctrl_div;
        ++telem_div;
        if (ctrl_div >= kCtrlEvery) {
            ctrl_div = 0;
            tick_class = static_cast<uint8_t>(tick_class | TICK_CTRL);
            const int64_t c0 = rt::now_ns();
            float pos[3] = {0.0f, 0.0f, 0.0f};
            float vel[3] = {0.0f, 0.0f, 0.0f};
            if (slots.pos.valid) {
                pos[0] = slots.pos.payload.x;
                pos[1] = slots.pos.payload.y;
                pos[2] = slots.pos.payload.z;
                vel[0] = slots.pos.payload.vx;
                vel[1] = slots.pos.payload.vy;
                vel[2] = slots.pos.payload.vz;
            }
            // ctrl_ticks is the trajectory clock: same index → same setpoint,
            // independent of when the process started.
            float pos_sp[3];
            trajectory_setpoint(ctrl_ticks, pos_sp);
            const AccelCmd cmd = controller.update(pos, vel, pos_sp, ctrl_dt);
            if (a.setpoint_port > 0) {
                // Count the socket result, not the tick. Tick arithmetic can
                // match plant rx while still hiding EAGAIN drops.
                if (sp_tx.send_accel_setpoint(cmd.ax, cmd.ay, cmd.az)) {
                    ++setpoint_tx;
                } else {
                    ++setpoint_tx_drop;
                }
            }
            ctrl_ns = static_cast<int32_t>(rt::now_ns() - c0);
            ++ctrl_ticks;
        }
        if (telem_div >= kTelemEvery) {
            telem_div = 0;
            tick_class = static_cast<uint8_t>(tick_class | TICK_TELEM);
            ++telem_ticks;
        }

        busy_ns(load_ns);

        const int64_t done = rt::now_ns();

        Sample s{};
        s.deadline_ns = next;
        s.wake_err_ns = static_cast<int32_t>(woke - next);
        s.exec_ns     = static_cast<int32_t>(done - woke);
        s.rx_ns       = rx_ns;
        s.ctrl_ns = ctrl_ns;
        s.seq         = static_cast<uint32_t>(i);
        s.rx_count    = rx;
        s.seq_gaps    = gaps;
        s.skew_ns     = kSkewNone;
        s.age_imu_ns  = kAgeNone;
        s.age_pos_ns  = kAgeNone;
        s.age_gps_ns  = kAgeNone;
        s.tick_class = tick_class;
        if (slots.imu.valid && slots.imu.rx_mono_ns == woke) {
            s.skew_ns = static_cast<int32_t>(
                static_cast<int64_t>(slots.imu.sender_us * 1000ull) - slots.imu.rx_mono_ns);
        }
        if (slots.imu.valid) {
            slots.imu.age_ns = woke - slots.imu.rx_mono_ns;
            s.age_imu_ns = slots.imu.age_ns;
            if (slots.imu.expected_period_ns > 0 &&
                slots.imu.age_ns > slots.imu.expected_period_ns * stale_lim) {
                s.flags |= FLAG_STALE;
            }
        }
        if (slots.pos.valid) {
            slots.pos.age_ns = woke - slots.pos.rx_mono_ns;
            s.age_pos_ns = slots.pos.age_ns;
            if (slots.pos.expected_period_ns > 0 &&
                slots.pos.age_ns > slots.pos.expected_period_ns * stale_lim) {
                s.flags |= FLAG_STALE;
            }
        }
        if (slots.gps.valid) {
            slots.gps.age_ns = woke - slots.gps.rx_mono_ns;
            s.age_gps_ns = slots.gps.age_ns;
            if (slots.gps.expected_period_ns > 0 &&
                slots.gps.age_ns > slots.gps.expected_period_ns * stale_lim) {
                s.flags |= FLAG_STALE;
            }
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
            if (s.flags & FLAG_REBASED)  ++rebases;
            if (s.flags & FLAG_DRAIN_FULL)   ++drain_fulls;
            if (s.flags & FLAG_STALE)   ++stales;
            if (s.age_imu_ns != kAgeNone &&
                slots.imu.expected_period_ns > 0 &&
                s.age_imu_ns > slots.imu.expected_period_ns * stale_lim) ++stale_imu;
            if (s.age_pos_ns != kAgeNone &&
                slots.pos.expected_period_ns > 0 &&
                s.age_pos_ns > slots.pos.expected_period_ns * stale_lim) ++stale_pos;
            if (s.age_gps_ns != kAgeNone &&
                slots.gps.expected_period_ns > 0 &&
                s.age_gps_ns > slots.gps.expected_period_ns * stale_lim) ++stale_gps;
            rx_total += rx;
            parse_ok += parsed;
            seq_gaps += gaps;
            log.push(s);
        }
    }
    // HOT PATH END

    if (sock >= 0) close(sock);
    sp_tx.close();

    const std::vector<std::string> meta = {
        std::string("platform=") + rt::platform_name(),
        "label=" + (a.label.empty() ? std::string(rt::platform_name()) : a.label),
        "hz=" + std::to_string(a.hz),
        "period_ns=" + std::to_string(period_ns),
        "load_us=" + std::to_string(a.load_us),
        "warmup_discarded=" + std::to_string(a.warmup),
        "port=" + std::to_string(a.port),
        "setpoint_port=" + std::to_string(a.setpoint_port),
        "ctrl_every=" + std::to_string(kCtrlEvery),
        "telem_every=" + std::to_string(kTelemEvery),
        "ctrl_ticks=" + std::to_string(ctrl_ticks),
        "telem_ticks=" + std::to_string(telem_ticks),
        "setpoint_tx=" + std::to_string(setpoint_tx),
        "setpoint_tx_drop=" + std::to_string(setpoint_tx_drop),
        "max_msgs_per_tick=" + std::to_string(kMaxMsgsPerTick),
        "overruns=" + std::to_string(overruns),
        "rebases=" + std::to_string(rebases),
        "rx_total=" + std::to_string(rx_total),
        "drain_full=" + std::to_string(drain_fulls),
        "parse=" + std::string(a.port > 0 ? "1" : "0"),
        "parse_ok=" + std::to_string(parse_ok),
        "seq_gaps=" + std::to_string(seq_gaps),
        "staleness_limit_periods=" + std::to_string(a.staleness_limit_periods),
        "staleness_floor_imu_ns=" + std::to_string(imu_floor_ns),
        "staleness_floor_pos_ns=" + std::to_string(pos_floor_ns),
        "staleness_floor_gps_ns=" + std::to_string(gps_floor_ns),
        "imu_period_ns=" + std::to_string(kImuPeriodNs),
        "pos_period_ns=" + std::to_string(kPosPeriodNs),
        "gps_period_ns=" + std::to_string(kGpsPeriodNs),
        "stale=" + std::to_string(stales),
        "stale_imu=" + std::to_string(stale_imu),
        "stale_pos=" + std::to_string(stale_pos),
        "stale_gps=" + std::to_string(stale_gps),
        "computation_ns=" + std::to_string(st.computation_ns),
        "constraint_ns=" + std::to_string(st.constraint_ns),
        "preemptible=" + std::to_string(st.preemptible),
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
                 "%s: %zu samples, %u overruns, %u rebases, %u rx, %u drain_full, "
                 "%u parse_ok, %u seq_gaps, %u stale, %u stale_imu, %u stale_pos, "
                 "%u stale_gps, %u ctrl, %u telem, %u setpoint_tx, %u setpoint_tx_drop\n",
                 a.out.c_str(), log.size(), overruns, rebases, rx_total, drain_fulls,
                 parse_ok, seq_gaps, stales, stale_imu, stale_pos, stale_gps,
                 ctrl_ticks, telem_ticks, setpoint_tx, setpoint_tx_drop);
    return 0;
}
