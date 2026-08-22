// Open-loop plant fixture. Replaces udp_sender when the loop needs a vehicle
// that falls, not just a MAVLink firehose. udp_sender stays for transport tests.
#include "plant_dyn.h"
#include "rt_platform.h"
#include "udp_rx.h"

#include "common/mavlink.h"

#include <arpa/inet.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

struct Args {
    int      seconds       = 60;
    int      sensor_port   = 14555;
    int      setpoint_port = 14556;
    uint32_t seed          = 1;
    double   tau           = plant::kTauDefault;
    double   mass          = plant::kMassDefault;
    double   amax          = plant::kAmaxDefault;
    double   acc_noise     = 0.02;    // m/s²
    double   gyro_noise    = 0.001;   // rad/s
    double   pos_noise     = 0.05;    // m
    double   gps_noise     = 2.0;     // m
    double   acc_bias[3]   = {0.05, -0.02, 0.03};
    bool     rt            = false;
    std::string truth_out  = "results/plant_truth.csv";
};

// same bound as loop_bench: a 20 ms burst at 400 Hz is 8 datagrams. setpoints
// arrive much slower (250 Hz controller into a 1 kHz plant).
constexpr uint16_t kMaxMsgsPerTick = 8;

// same origin as tools/udp_sender.cpp so swapping the fixture is not a datum jump.
constexpr double kHomeLatDeg = 37.4419;
constexpr double kHomeLonDeg = -122.1430;
constexpr double kHomeAltM   = 30.0;
constexpr double kEarthR     = 6371000.0;

struct TruthRow {
    int64_t t_ns;
    double  p[3];
    double  v[3];
    double  a[3];
    double  a_cmd[3];
};

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string f = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", f.c_str()); std::exit(2); }
            return argv[++i];
        };
        if      (f == "--seconds")       a.seconds       = std::atoi(next());
        else if (f == "--sensor-port")   a.sensor_port   = std::atoi(next());
        else if (f == "--setpoint-port") a.setpoint_port = std::atoi(next());
        else if (f == "--seed")          a.seed          = static_cast<uint32_t>(std::strtoul(next(), nullptr, 10));
        else if (f == "--tau")           a.tau           = std::atof(next());
        else if (f == "--mass")          a.mass          = std::atof(next());
        else if (f == "--amax")          a.amax          = std::atof(next());
        else if (f == "--acc-noise")     a.acc_noise     = std::atof(next());
        else if (f == "--gyro-noise")    a.gyro_noise    = std::atof(next());
        else if (f == "--pos-noise")     a.pos_noise     = std::atof(next());
        else if (f == "--gps-noise")     a.gps_noise     = std::atof(next());
        else if (f == "--acc-bias-n")    a.acc_bias[0]   = std::atof(next());
        else if (f == "--acc-bias-e")    a.acc_bias[1]   = std::atof(next());
        else if (f == "--acc-bias-d")    a.acc_bias[2]   = std::atof(next());
        else if (f == "--truth-out")     a.truth_out     = next();
        else if (f == "--rt")            a.rt            = true;
        else { std::fprintf(stderr, "unknown flag %s\n", f.c_str()); return false; }
    }
    return a.seconds > 0 && a.sensor_port > 0 && a.setpoint_port > 0 &&
           a.tau > 0.0 && a.mass > 0.0 && a.amax > 0.0;
}

void send_msg(int fd, const sockaddr_in& dst, mavlink_message_t& msg, uint8_t* buf) {
    const uint16_t n = mavlink_msg_to_send_buffer(buf, &msg);
    sendto(fd, buf, n, 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
}

void ned_to_llh(double n, double e, double d, int32_t& lat, int32_t& lon, int32_t& alt_mm) {
    const double lat0 = kHomeLatDeg * 3.14159265358979323846 / 180.0;
    const double lat_rad = lat0 + n / kEarthR;
    const double lon_rad = kHomeLonDeg * 3.14159265358979323846 / 180.0 +
                           e / (kEarthR * std::cos(lat0));
    lat    = static_cast<int32_t>(lat_rad * (180.0 / 3.14159265358979323846) * 1e7);
    lon    = static_cast<int32_t>(lon_rad * (180.0 / 3.14159265358979323846) * 1e7);
    alt_mm = static_cast<int32_t>((kHomeAltM - d) * 1000.0);
}

bool write_truth(const std::string& path, const std::vector<std::string>& meta,
                 const TruthRow* rows, size_t n) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    for (const auto& m : meta) std::fprintf(f, "# %s\n", m.c_str());
    std::fprintf(f, "t_ns,px,py,pz,vx,vy,vz,ax,ay,az,ax_cmd,ay_cmd,az_cmd\n");
    for (size_t i = 0; i < n; ++i) {
        const TruthRow& r = rows[i];
        std::fprintf(f,
            "%lld,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g,%.10g\n",
            static_cast<long long>(r.t_ns),
            r.p[0], r.p[1], r.p[2], r.v[0], r.v[1], r.v[2],
            r.a[0], r.a[1], r.a[2], r.a_cmd[0], r.a_cmd[1], r.a_cmd[2]);
    }
    std::fclose(f);
    return true;
}

}

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) return 2;

    std::fprintf(stderr, "seed=%u\n", a.seed);

    const int64_t period_ns = 1000000000LL / plant::kHz;
    const size_t  total     = static_cast<size_t>(plant::kHz) * static_cast<size_t>(a.seconds);
    const size_t  n_truth   = static_cast<size_t>(50) * static_cast<size_t>(a.seconds);

    plant::Params par{};
    par.tau  = a.tau;
    par.mass = a.mass;
    par.amax = a.amax;
    plant::State st{};
    double a_cmd[3] = {0.0, 0.0, 0.0};

    std::mt19937 rng(a.seed);
    std::normal_distribution<double> unit(0.0, 1.0);
    auto noise = [&](double sigma) { return sigma <= 0.0 ? 0.0 : sigma * unit(rng); };

    std::vector<TruthRow> truth(n_truth);
    size_t n_logged = 0;

    rt::RtConfig cfg;
    cfg.period_ns           = period_ns;
    cfg.scheduler_requested = a.rt;
    cfg.lock_memory         = a.rt;
    // timeshare plant injects sensor-timing jitter, which is what produced the
    // drain_full bunching measured in 2a. two competing time-constraint threads
    // is its own confound. --rt is opt-in; the campaign choice is the author's.
    const rt::RtStatus rst = rt::apply(cfg);

    const int tx = socket(AF_INET, SOCK_DGRAM, 0);
    if (tx < 0) { std::fprintf(stderr, "sensor socket failed\n"); return 1; }
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(static_cast<uint16_t>(a.sensor_port));
    inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);

    const int rx = udp_bind_nonblocking(a.setpoint_port);
    if (rx < 0) {
        std::fprintf(stderr, "could not bind setpoint UDP %d\n", a.setpoint_port);
        close(tx);
        return 1;
    }

    char rxbuf[512]{};
    mavlink_message_t mav_msg{};
    mavlink_status_t  mav_status{};
    uint8_t outbuf[MAVLINK_MAX_PACKET_LEN];

    int imu_acc = 0, pos_acc = 0, gps_acc = 0;
    rt::prefault_stack();
    int64_t next = rt::now_ns() + period_ns;

    for (size_t i = 0; i < total; ++i) {
        rt::sleep_until_ns(next);
        const int64_t now = rt::now_ns();

        uint16_t drained = 0;
        while (drained < kMaxMsgsPerTick) {
            const long n = udp_try_recv(rx, rxbuf, sizeof(rxbuf));
            if (n <= 0) break;
            ++drained;
            for (long b = 0; b < n; ++b) {
                if (!mavlink_parse_char(MAVLINK_COMM_0, static_cast<uint8_t>(rxbuf[b]),
                                        &mav_msg, &mav_status)) {
                    continue;
                }
                if (mav_msg.msgid != MAVLINK_MSG_ID_SET_POSITION_TARGET_LOCAL_NED) continue;
                mavlink_set_position_target_local_ned_t sp{};
                mavlink_msg_set_position_target_local_ned_decode(&mav_msg, &sp);
                if (sp.coordinate_frame != MAV_FRAME_LOCAL_NED) continue;
                const bool force = (sp.type_mask & POSITION_TARGET_TYPEMASK_FORCE_SET) != 0;
                const double scale = force ? (1.0 / par.mass) : 1.0;
                if (!(sp.type_mask & POSITION_TARGET_TYPEMASK_AX_IGNORE))
                    a_cmd[0] = static_cast<double>(sp.afx) * scale;
                if (!(sp.type_mask & POSITION_TARGET_TYPEMASK_AY_IGNORE))
                    a_cmd[1] = static_cast<double>(sp.afy) * scale;
                if (!(sp.type_mask & POSITION_TARGET_TYPEMASK_AZ_IGNORE))
                    a_cmd[2] = static_cast<double>(sp.afz) * scale;
            }
        }

        plant::rk4_step(st, a_cmd, par, plant::kDt);

        const uint64_t t_us = static_cast<uint64_t>(now / 1000);
        mavlink_message_t msg{};

        // 400 does not divide 1000. Bresenham, not kHz/400: IMU cadence is 2 or
        // 3 ms and averages 400 Hz. accelerometer is a_filt+g, not specific
        // force; the estimator is 6-state pos/vel.
        imu_acc += 400;
        if (imu_acc >= plant::kHz) {
            imu_acc -= plant::kHz;
            mavlink_msg_highres_imu_pack(
                1, 1, &msg, t_us,
                static_cast<float>(st.a[0] + a.acc_bias[0] + noise(a.acc_noise)),
                static_cast<float>(st.a[1] + a.acc_bias[1] + noise(a.acc_noise)),
                static_cast<float>(st.a[2] + plant::kGravity + a.acc_bias[2] + noise(a.acc_noise)),
                static_cast<float>(noise(a.gyro_noise)),
                static_cast<float>(noise(a.gyro_noise)),
                static_cast<float>(noise(a.gyro_noise)),
                0.0f, 0.0f, 0.0f, 1013.25f, 0.0f, 0.0f, 0.0f,
                0xFFFF, 1);
            send_msg(tx, dst, msg, outbuf);
        }

        pos_acc += 50;
        if (pos_acc >= plant::kHz) {
            pos_acc -= plant::kHz;
            if (n_logged < truth.size()) {
                TruthRow& row = truth[n_logged++];
                row.t_ns = now;
                for (int k = 0; k < 3; ++k) {
                    row.p[k] = st.p[k];
                    row.v[k] = st.v[k];
                    row.a[k] = st.a[k] + (k == 2 ? plant::kGravity : 0.0);
                    row.a_cmd[k] = a_cmd[k];
                }
            }
            mavlink_msg_local_position_ned_pack(
                1, 1, &msg,
                static_cast<uint32_t>(t_us / 1000),
                static_cast<float>(st.p[0] + noise(a.pos_noise)),
                static_cast<float>(st.p[1] + noise(a.pos_noise)),
                static_cast<float>(st.p[2] + noise(a.pos_noise)),
                static_cast<float>(st.v[0]),
                static_cast<float>(st.v[1]),
                static_cast<float>(st.v[2]));
            send_msg(tx, dst, msg, outbuf);
        }

        gps_acc += 5;
        if (gps_acc >= plant::kHz) {
            gps_acc -= plant::kHz;
            const double pn = st.p[0] + noise(a.gps_noise);
            const double pe = st.p[1] + noise(a.gps_noise);
            const double pd = st.p[2] + noise(a.gps_noise);
            int32_t lat = 0, lon = 0, alt_mm = 0;
            ned_to_llh(pn, pe, pd, lat, lon, alt_mm);
            const double hvel = std::hypot(st.v[0], st.v[1]);
            mavlink_msg_gps_raw_int_pack(
                1, 1, &msg, t_us,
                GPS_FIX_TYPE_3D_FIX,
                lat, lon, alt_mm,
                90, 120,
                static_cast<uint16_t>(hvel * 100.0 > 65535.0 ? 65535.0 : hvel * 100.0),
                UINT16_MAX, 11,
                alt_mm, 1500, 2000, 200, 0, 0);
            send_msg(tx, dst, msg, outbuf);
        }

        next += period_ns;
    }

    close(tx);
    close(rx);

    const std::vector<std::string> meta = {
        std::string("platform=") + rt::platform_name(),
        "seed=" + std::to_string(a.seed),
        "hz=" + std::to_string(plant::kHz),
        "seconds=" + std::to_string(a.seconds),
        "tau=" + std::to_string(a.tau),
        "mass=" + std::to_string(a.mass),
        "amax=" + std::to_string(a.amax),
        "gravity=" + std::to_string(plant::kGravity),
        "sensor_port=" + std::to_string(a.sensor_port),
        "setpoint_port=" + std::to_string(a.setpoint_port),
        "acc_noise=" + std::to_string(a.acc_noise),
        "gyro_noise=" + std::to_string(a.gyro_noise),
        "pos_noise=" + std::to_string(a.pos_noise),
        "gps_noise=" + std::to_string(a.gps_noise),
        "acc_bias_n=" + std::to_string(a.acc_bias[0]),
        "acc_bias_e=" + std::to_string(a.acc_bias[1]),
        "acc_bias_d=" + std::to_string(a.acc_bias[2]),
        "scheduler_requested=" + std::string(a.rt ? "1" : "0"),
        "scheduler_applied=" + std::string(rst.scheduler_applied ? "1" : "0"),
        "memory_locked=" + std::string(rst.memory_locked ? "1" : "0"),
        "note=" + rst.note,
    };
    if (!a.truth_out.empty() && !write_truth(a.truth_out, meta, truth.data(), n_logged)) {
        std::fprintf(stderr, "could not write %s\n", a.truth_out.c_str());
        return 1;
    }
    return 0;
}
