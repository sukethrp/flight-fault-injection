#include "fault.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace {

constexpr uint8_t kMav1 = 0xFE;
constexpr uint8_t kMav2 = 0xFD;

double parse_double(const std::string& s) { return std::atof(s.c_str()); }
int64_t parse_i64(const std::string& s) { return static_cast<int64_t>(std::atoll(s.c_str())); }

class GpsDropout final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& msg, uint8_t* /*payload*/) override {
        if (!msg.has_mav) return Action::Forward;
        const uint32_t want = p_.target_msgid ? p_.target_msgid : 24u;  // GPS_RAW_INT
        return msg.msgid == want ? Action::Drop : Action::Forward;
    }
};

class PacketDrop final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& /*msg*/, uint8_t* /*payload*/) override { return Action::Drop; }
};

class PacketDelay final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& /*msg*/, uint8_t* /*payload*/) override { return Action::Delay; }
};

class ReorderFault final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& /*msg*/, uint8_t* /*payload*/) override { return Action::Reorder; }
};

// Payload corruptors: AUTHOR. How you corrupt sets what the detector sees
// (NOTES.md 2026-08-22). Drop/delay/reorder above are fully implemented.

class ImuBitFlip final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& msg, uint8_t* payload) override {
        (void)msg;
        (void)payload;
        // AUTHOR: implement
        return Action::Forward;
    }
};

class ImuBias final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& msg, uint8_t* payload) override {
        (void)msg;
        (void)payload;
        // AUTHOR: implement
        return Action::Forward;
    }
};

class ImuStuck final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& msg, uint8_t* payload) override {
        (void)msg;
        (void)payload;
        // AUTHOR: implement
        return Action::Forward;
    }
};

class GpsJump final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& msg, uint8_t* payload) override {
        (void)msg;
        (void)payload;
        // AUTHOR: implement
        return Action::Forward;
    }
};

class ClockSkew final : public Fault {
  public:
    using Fault::Fault;
    Action apply(Datagram& msg, uint8_t* payload) override {
        (void)msg;
        (void)payload;
        // AUTHOR: implement
        return Action::Forward;
    }
};

}  // namespace

Fault::Fault(FaultParams p) : p_(std::move(p)) {
    if (p_.param_json.empty()) p_.param_json = make_param_json(p_);
}

bool Fault::should_fire(uint64_t tick, std::mt19937& rng) {
    const int64_t now_ns = now_ns_;

    if (window_live_) {
        if (now_ns < active_from_) return false;
        if (p_.duration_ns > 0 && now_ns >= active_until_) {
            window_live_ = false;
        } else {
            return true;
        }
    }

    // duration_ns == 0 and rate > 0: always-on (per-packet Bernoulli or continuous).
    if (p_.duration_ns == 0 && p_.rate_hz > 0.0) {
        if (p_.type == "packet_drop") {
            // rate is drop probability in [0,1] for continuous packet_drop.
            std::uniform_real_distribution<double> u(0.0, 1.0);
            return u(rng) < p_.rate_hz;
        }
        return true;
    }

    if (p_.rate_hz <= 0.0) return false;

    // one Bernoulli trial per loop-period tick, not per drained datagram.
    if (tick == last_trial_tick_) return false;
    last_trial_tick_ = tick;

    // P(start) = rate_hz * period.
    const double p_start = p_.rate_hz * (static_cast<double>(p_.period_ns) * 1e-9);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    if (u(rng) >= p_start) return false;

    // phase U(0, period): fixed offset from the deadline biases TTD by up to one period.
    std::uniform_int_distribution<int64_t> phase(0, p_.period_ns > 0 ? p_.period_ns - 1 : 0);
    active_from_  = now_ns + phase(rng);
    active_until_ = active_from_ + p_.duration_ns;
    window_live_  = true;
    return now_ns >= active_from_;
}

Fault* make_fault(const FaultParams& p) {
    // msgid-selective drop: gps_dropout defaults to GPS_RAW_INT (24);
    // imu_dropout / pos_dropout need msgid=105 / 32 in the config.
    if (p.type == "gps_dropout" || p.type == "imu_dropout" || p.type == "pos_dropout")
        return new GpsDropout(p);
    if (p.type == "packet_drop") return new PacketDrop(p);
    if (p.type == "packet_delay") return new PacketDelay(p);
    if (p.type == "reorder") return new ReorderFault(p);
    if (p.type == "imu_bitflip") return new ImuBitFlip(p);
    if (p.type == "imu_bias") return new ImuBias(p);
    if (p.type == "imu_stuck") return new ImuStuck(p);
    if (p.type == "gps_jump") return new GpsJump(p);
    if (p.type == "clock_skew") return new ClockSkew(p);
    std::fprintf(stderr, "unknown fault type %s\n", p.type.c_str());
    return nullptr;
}

std::string make_param_json(const FaultParams& p) {
    std::ostringstream o;
    o << "{\"type\":\"" << p.type << "\""
      << ",\"rate\":" << p.rate_hz
      << ",\"duration_ns\":" << p.duration_ns
      << ",\"period_ns\":" << p.period_ns;
    if (p.target_msgid) o << ",\"msgid\":" << p.target_msgid;
    if (p.delay_ns) o << ",\"delay_ns\":" << p.delay_ns;
    if (p.reorder_win) o << ",\"reorder_win\":" << p.reorder_win;
    o << "}";
    return o.str();
}

const char* action_name(Action a) {
    switch (a) {
    case Action::Forward: return "forward";
    case Action::Drop:    return "drop";
    case Action::Delay:   return "delay";
    case Action::Reorder: return "reorder";
    case Action::Modify:  return "modify";
    }
    return "unknown";
}

bool peek_mavlink(const uint8_t* buf, size_t n, uint32_t* msgid, uint8_t* seq) {
    if (n < 6 || buf == nullptr || msgid == nullptr || seq == nullptr) return false;
    if (buf[0] == kMav2) {
        if (n < 10) return false;
        *seq   = buf[4];
        *msgid = static_cast<uint32_t>(buf[7]) |
                 (static_cast<uint32_t>(buf[8]) << 8) |
                 (static_cast<uint32_t>(buf[9]) << 16);
        return true;
    }
    if (buf[0] == kMav1) {
        if (n < 6) return false;
        *seq   = buf[2];
        *msgid = buf[5];
        return true;
    }
    return false;
}

uint8_t* mav_payload(Datagram& dg) {
    if (!dg.has_mav || dg.len == 0) return nullptr;
    if (dg.buf[0] == kMav2) {
        // STX, len, incompat, compat, seq, sys, comp, msgid[3], payload...
        if (dg.len < 10) return nullptr;
        return dg.buf + 10;
    }
    if (dg.buf[0] == kMav1) {
        // STX, len, seq, sys, comp, msgid, payload...
        if (dg.len < 6) return nullptr;
        return dg.buf + 6;
    }
    return nullptr;
}

std::vector<FaultParams> load_fault_config(const std::string& path, uint32_t* seed_out) {
    std::vector<FaultParams> out;
    if (path.empty()) return out;

    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "could not open config %s\n", path.c_str());
        std::exit(2);
    }

    uint32_t seed = 1;
    int64_t  period_ns = 4000000;
    std::unordered_map<std::string, FaultParams> by_id;
    std::vector<std::string> order;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());

        if (key == "seed") {
            seed = static_cast<uint32_t>(parse_i64(val));
            continue;
        }
        if (key == "period_ns") {
            period_ns = parse_i64(val);
            continue;
        }
        if (key == "fault") {
            if (!by_id.count(val)) {
                FaultParams fp;
                fp.id   = val;
                fp.type = val;
                order.push_back(val);
                by_id.emplace(val, fp);
            }
            continue;
        }

        const auto dot = key.find('.');
        if (dot == std::string::npos) continue;
        const std::string id  = key.substr(0, dot);
        const std::string field = key.substr(dot + 1);
        if (!by_id.count(id)) {
            FaultParams fp;
            fp.id   = id;
            fp.type = id;
            order.push_back(id);
            by_id.emplace(id, fp);
        }
        FaultParams& fp = by_id[id];
        if (field == "type") fp.type = val;
        else if (field == "rate") fp.rate_hz = parse_double(val);
        else if (field == "duration_ms") fp.duration_ns = parse_i64(val) * 1000000LL;
        else if (field == "duration_ns") fp.duration_ns = parse_i64(val);
        else if (field == "msgid" || field == "target_msgid")
            fp.target_msgid = static_cast<uint32_t>(parse_i64(val));
        else if (field == "delay_ms") fp.delay_ns = parse_i64(val) * 1000000LL;
        else if (field == "delay_ns") fp.delay_ns = parse_i64(val);
        else if (field == "reorder_win" || field == "window")
            fp.reorder_win = static_cast<int>(parse_i64(val));
    }

    for (const auto& id : order) {
        FaultParams fp = by_id[id];
        fp.period_ns   = period_ns;
        fp.param_json  = make_param_json(fp);
        out.push_back(std::move(fp));
    }
    if (seed_out) *seed_out = seed;
    return out;
}
