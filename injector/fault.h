#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

// Transport and payload faults share one schedule: rate (firings per second),
// duration, and a U(0, period) phase so TTD is not pinned to the loop deadline.
struct FaultParams {
    std::string id;
    std::string type;
    double      rate_hz      = 0.0;
    int64_t     duration_ns  = 0;
    int64_t     period_ns    = 4000000;  // loop period; phase drawn in [0, period)
    uint32_t    target_msgid = 0;
    int64_t     delay_ns     = 0;        // packet_delay
    int         reorder_win  = 2;        // reorder hold depth
    std::string param_json;              // logged verbatim on each injection
};

enum class Action : uint8_t {
    Forward = 0,
    Drop,
    Delay,
    Reorder,
    Modify,
};

struct Datagram {
    uint8_t  buf[512];
    size_t   len       = 0;
    uint32_t msgid     = 0;
    uint8_t  mav_seq   = 0;
    bool     has_mav   = false;
};

class Fault {
  public:
    explicit Fault(FaultParams p);
    virtual ~Fault() = default;

    const FaultParams& params() const { return p_; }
    const char*        id() const { return p_.id.c_str(); }

    // stamp monotonic time before should_fire; same clock as loop_bench.
    void set_now(int64_t now_ns) { now_ns_ = now_ns; }

    // true while the fault window is live (after the randomized phase delay).
    bool should_fire(uint64_t tick, std::mt19937& rng);

    // msg is the wire datagram; payload points at mavlink payload bytes when
    // has_mav, else nullptr. Transport faults ignore payload.
    virtual Action apply(Datagram& msg, uint8_t* payload) = 0;

  protected:
    FaultParams p_;
    int64_t     now_ns_          = 0;
    int64_t     active_from_     = 0;
    int64_t     active_until_    = 0;
    bool        window_live_     = false;
    uint64_t    last_trial_tick_ = UINT64_MAX;
};

Fault* make_fault(const FaultParams& p);

std::vector<FaultParams> load_fault_config(const std::string& path, uint32_t* seed_out);
std::string              make_param_json(const FaultParams& p);
const char*              action_name(Action a);

// Peek MAVLink v1/v2 header without a full parse. false if not a frame.
bool peek_mavlink(const uint8_t* buf, size_t n, uint32_t* msgid, uint8_t* seq);

// Payload start in a MAVLink frame, or nullptr.
uint8_t* mav_payload(Datagram& dg);
