// MAVLink UDP proxy between plant and loop_bench. Default is passthrough:
// every datagram forwarded unmodified. Faults are optional via --config.
#include "fault.h"

#include "rt_platform.h"
#include "udp_rx.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr uint16_t kMaxDrain   = 8;
constexpr size_t   kMaxDelayQ  = 256;
constexpr size_t   kMaxReorder = 32;

struct Args {
    int         listen_plant  = 14550;  // plant --sensor-port
    int         forward_loop  = 14555;  // loop_bench --port
    int         listen_loop   = 14551;  // loop_bench --setpoint-port
    int         forward_plant = 14556;  // plant --setpoint-port
    int         seconds       = 0;      // 0 = until killed
    uint32_t    seed          = 1;
    std::string config;
    std::string event_out;
    std::string label = "passthrough";
};

struct DelayItem {
    int64_t  release_ns = 0;
    Datagram dg;
    int      out_fd     = -1;
};

struct ReorderBuf {
    std::deque<Datagram> held;
    int                  out_fd = -1;
    int                  win    = 2;
};

struct EventRow {
    int64_t     inject_mono_ns = 0;
    std::string fault_id;
    std::string param_json;
    uint32_t    target_msgid = 0;
    std::string action;
    uint8_t     seq = 0;
};

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
        if (f == "--listen-plant") a.listen_plant = std::atoi(next());
        else if (f == "--forward-loop") a.forward_loop = std::atoi(next());
        else if (f == "--listen-loop") a.listen_loop = std::atoi(next());
        else if (f == "--forward-plant") a.forward_plant = std::atoi(next());
        else if (f == "--seconds") a.seconds = std::atoi(next());
        else if (f == "--seed") a.seed = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (f == "--config") a.config = next();
        else if (f == "--event-out") a.event_out = next();
        else if (f == "--label") a.label = next();
        else {
            std::fprintf(stderr, "unknown flag %s\n", f.c_str());
            return false;
        }
    }
    return a.listen_plant > 0 && a.forward_loop > 0 && a.listen_loop > 0 && a.forward_plant > 0;
}

int udp_connect_nonblocking(int port) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    sockaddr_in dst{};
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(static_cast<uint16_t>(port));
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) != 0) {
        close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool send_dg(int fd, const Datagram& dg) {
    const ssize_t n = send(fd, dg.buf, dg.len, 0);
    return n == static_cast<ssize_t>(dg.len);
}

void fill_mav(Datagram& dg) {
    uint32_t msgid = 0;
    uint8_t  seq   = 0;
    dg.has_mav = peek_mavlink(dg.buf, dg.len, &msgid, &seq);
    if (dg.has_mav) {
        dg.msgid   = msgid;
        dg.mav_seq = seq;
    }
}

bool write_events(const std::string& path, const std::vector<std::string>& meta,
                  const std::vector<EventRow>& rows) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    for (const auto& m : meta) std::fprintf(f, "# %s\n", m.c_str());
    std::fprintf(f, "inject_mono_ns,fault_id,param_json,target_msgid,action,seq\n");
    for (const auto& r : rows) {
        std::fprintf(f, "%lld,%s,%s,%u,%s,%u\n",
                     static_cast<long long>(r.inject_mono_ns),
                     r.fault_id.c_str(),
                     r.param_json.c_str(),
                     r.target_msgid,
                     r.action.c_str(),
                     static_cast<unsigned>(r.seq));
    }
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) return 2;

    uint32_t cfg_seed = a.seed;
    std::vector<FaultParams> params = load_fault_config(a.config, &cfg_seed);
    if (!a.config.empty()) a.seed = cfg_seed;

    std::vector<std::unique_ptr<Fault>> faults;
    faults.reserve(params.size());
    for (const auto& p : params) {
        Fault* f = make_fault(p);
        if (!f) return 2;
        faults.emplace_back(f);
    }

    std::mt19937 rng(a.seed);

    const int rx_plant = udp_bind_nonblocking(a.listen_plant);
    const int rx_loop  = udp_bind_nonblocking(a.listen_loop);
    const int tx_loop  = udp_connect_nonblocking(a.forward_loop);
    const int tx_plant = udp_connect_nonblocking(a.forward_plant);
    if (rx_plant < 0 || rx_loop < 0 || tx_loop < 0 || tx_plant < 0) {
        std::fprintf(stderr, "bind/connect failed (plant listen %d loop listen %d)\n",
                     a.listen_plant, a.listen_loop);
        return 1;
    }

    std::deque<DelayItem> delay_q;
    ReorderBuf reorder_plant_to_loop;
    reorder_plant_to_loop.out_fd = tx_loop;
    ReorderBuf reorder_loop_to_plant;
    reorder_loop_to_plant.out_fd = tx_plant;

    std::vector<EventRow> events;
    events.reserve(4096);

    uint64_t tick      = 0;
    uint32_t fwd_plant = 0;
    uint32_t fwd_loop  = 0;
    uint32_t dropped   = 0;
    uint32_t delayed   = 0;
    uint32_t reordered = 0;
    int64_t  period_ns = 4000000;
    if (!params.empty()) period_ns = params[0].period_ns;

    const int64_t t0       = rt::now_ns();
    const int64_t deadline = a.seconds > 0 ? t0 + static_cast<int64_t>(a.seconds) * 1000000000LL : 0;
    int64_t       next_tick = t0 + period_ns;

    auto log_event = [&](int64_t now, const Fault& f, const Datagram& dg, Action act) {
        if (a.event_out.empty()) return;
        EventRow r;
        r.inject_mono_ns = now;  // rt::now_ns(), same epoch as loop_bench
        r.fault_id       = f.id();
        r.param_json     = f.params().param_json;
        r.target_msgid   = dg.has_mav ? dg.msgid : f.params().target_msgid;
        r.action         = action_name(act);
        r.seq            = dg.mav_seq;
        events.push_back(std::move(r));
    };

    auto flush_delay = [&](int64_t now) {
        while (!delay_q.empty() && delay_q.front().release_ns <= now) {
            DelayItem it = std::move(delay_q.front());
            delay_q.pop_front();
            if (send_dg(it.out_fd, it.dg)) {
                if (it.out_fd == tx_loop) ++fwd_loop;
                else ++fwd_plant;
            }
        }
    };

    auto flush_reorder = [&](ReorderBuf& rb, bool force) {
        if (rb.held.empty()) return;
        if (!force && static_cast<int>(rb.held.size()) < rb.win) return;
        while (!rb.held.empty()) {
            Datagram dg = std::move(rb.held.back());
            rb.held.pop_back();
            if (send_dg(rb.out_fd, dg)) {
                if (rb.out_fd == tx_loop) ++fwd_loop;
                else ++fwd_plant;
            }
        }
    };

    auto handle = [&](Datagram& dg, int out_fd, ReorderBuf& rb) {
        const int64_t now = rt::now_ns();
        fill_mav(dg);
        uint8_t* payload = mav_payload(dg);

        for (auto& fp : faults) fp->set_now(now);

        Action act = Action::Forward;
        Fault* who = nullptr;
        for (auto& fp : faults) {
            if (!fp->should_fire(tick, rng)) continue;
            const Action a1 = fp->apply(dg, payload);
            if (a1 == Action::Forward) continue;
            act = a1;
            who = fp.get();
            break;
        }

        if (act == Action::Drop && who) {
            log_event(now, *who, dg, act);
            ++dropped;
            return;
        }
        if (act == Action::Delay && who) {
            if (delay_q.size() >= kMaxDelayQ) {
                log_event(now, *who, dg, Action::Drop);
                ++dropped;
                return;
            }
            DelayItem it;
            it.release_ns = now + (who->params().delay_ns > 0 ? who->params().delay_ns : 10000000LL);
            it.dg         = dg;
            it.out_fd     = out_fd;
            delay_q.push_back(std::move(it));
            log_event(now, *who, dg, act);
            ++delayed;
            return;
        }
        if (act == Action::Reorder && who) {
            rb.win = who->params().reorder_win > 1 ? who->params().reorder_win : 2;
            if (rb.held.size() >= kMaxReorder) flush_reorder(rb, true);
            rb.held.push_back(dg);
            log_event(now, *who, dg, act);
            ++reordered;
            flush_reorder(rb, false);
            return;
        }

        if (send_dg(out_fd, dg)) {
            if (out_fd == tx_loop) ++fwd_loop;
            else ++fwd_plant;
        }
    };

    auto drain = [&](int rx, int tx, ReorderBuf& rb) {
        uint16_t n = 0;
        while (n < kMaxDrain) {
            Datagram dg;
            const long got = udp_try_recv(rx, dg.buf, sizeof(dg.buf));
            if (got <= 0) break;
            dg.len = static_cast<size_t>(got);
            ++n;
            handle(dg, tx, rb);
        }
    };

    std::fprintf(stderr,
                 "proxy passthrough=%d listen_plant=%d→%d listen_loop=%d→%d faults=%zu seed=%u\n",
                 faults.empty() ? 1 : 0, a.listen_plant, a.forward_loop, a.listen_loop,
                 a.forward_plant, faults.size(), a.seed);

    pollfd pf[2] = {
        {rx_plant, POLLIN, 0},
        {rx_loop, POLLIN, 0},
    };

    while (true) {
        const int64_t now = rt::now_ns();
        if (deadline > 0 && now >= deadline) break;
        if (now >= next_tick) {
            const int64_t missed = (now - next_tick) / period_ns + 1;
            next_tick += missed * period_ns;
            tick += static_cast<uint64_t>(missed);
        }

        flush_delay(now);
        if (static_cast<int>(reorder_plant_to_loop.held.size()) >= reorder_plant_to_loop.win)
            flush_reorder(reorder_plant_to_loop, false);
        if (static_cast<int>(reorder_loop_to_plant.held.size()) >= reorder_loop_to_plant.win)
            flush_reorder(reorder_loop_to_plant, false);

        const int pr = poll(pf, 2, 1);
        if (pr < 0 && errno != EINTR) break;
        drain(rx_plant, tx_loop, reorder_plant_to_loop);
        drain(rx_loop, tx_plant, reorder_loop_to_plant);
    }

    flush_reorder(reorder_plant_to_loop, true);
    flush_reorder(reorder_loop_to_plant, true);
    flush_delay(rt::now_ns() + 3600LL * 1000000000LL);

    close(rx_plant);
    close(rx_loop);
    close(tx_loop);
    close(tx_plant);

    if (!a.event_out.empty()) {
        const std::vector<std::string> meta = {
            std::string("platform=") + rt::platform_name(),
            "label=" + a.label,
            "seed=" + std::to_string(a.seed),
            "listen_plant=" + std::to_string(a.listen_plant),
            "forward_loop=" + std::to_string(a.forward_loop),
            "listen_loop=" + std::to_string(a.listen_loop),
            "forward_plant=" + std::to_string(a.forward_plant),
            "faults=" + std::to_string(faults.size()),
            "fwd_to_loop=" + std::to_string(fwd_loop),
            "fwd_to_plant=" + std::to_string(fwd_plant),
            "dropped=" + std::to_string(dropped),
            "delayed=" + std::to_string(delayed),
            "reordered=" + std::to_string(reordered),
            "passthrough=" + std::string(faults.empty() ? "1" : "0"),
            "clock=rt::now_ns",
        };
        if (!write_events(a.event_out, meta, events)) {
            std::fprintf(stderr, "could not write %s\n", a.event_out.c_str());
            return 1;
        }
    }

    std::fprintf(stderr,
                 "proxy done: fwd_loop=%u fwd_plant=%u drop=%u delay=%u reorder=%u events=%zu\n",
                 fwd_loop, fwd_plant, dropped, delayed, reordered, events.size());
    return 0;
}
