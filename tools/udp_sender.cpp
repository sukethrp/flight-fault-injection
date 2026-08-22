// Test fixture. HIGHRES_IMU, LOCAL_POSITION_NED, GPS_RAW_INT at independent
// rates on one socket. One absolute-deadline loop at lcm(rates); per-type
// decimation, not three threads. Stands in for PX4 SITL until 2e.
#include "rt_platform.h"

#include "common/mavlink.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

static int gcd_int(int a, int b) {
    while (b) { const int t = a % b; a = b; b = t; }
    return a < 0 ? -a : a;
}

static int lcm_int(int a, int b) {
    return a / gcd_int(a, b) * b;
}

static void send_msg(int fd, const sockaddr_in& dst, mavlink_message_t& msg, uint8_t* buf) {
    const uint16_t n = mavlink_msg_to_send_buffer(buf, &msg);
    sendto(fd, buf, n, 0, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst));
}

int main(int argc, char** argv) {
    int imu_hz = 400, pos_hz = 50, gps_hz = 5, port = 14555, seconds = 30;
    for (int i = 1; i < argc; ++i) {
        const std::string f = argv[i];
        if (i + 1 >= argc) break;
        if      (f == "--hz")      imu_hz  = std::atoi(argv[++i]);
        else if (f == "--imu-hz")  imu_hz  = std::atoi(argv[++i]);
        else if (f == "--pos-hz")  pos_hz  = std::atoi(argv[++i]);
        else if (f == "--gps-hz")  gps_hz  = std::atoi(argv[++i]);
        else if (f == "--port")    port    = std::atoi(argv[++i]);
        else if (f == "--seconds") seconds = std::atoi(argv[++i]);
    }
    if (imu_hz <= 0 || pos_hz <= 0 || gps_hz <= 0 || seconds <= 0 || port <= 0) {
        std::fprintf(stderr, "rates, seconds, and port must be positive\n");
        return 2;
    }
    const int tick_hz = lcm_int(lcm_int(imu_hz, pos_hz), gps_hz);
    if (tick_hz % imu_hz || tick_hz % pos_hz || tick_hz % gps_hz) {
        std::fprintf(stderr, "could not find a common tick for %d/%d/%d Hz\n",
                     imu_hz, pos_hz, gps_hz);
        return 2;
    }
    const int imu_every = tick_hz / imu_hz;
    const int pos_every = tick_hz / pos_hz;
    const int gps_every = tick_hz / gps_hz;

    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);

    mavlink_message_t msg{};
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    const int64_t period = 1000000000LL / tick_hz;
    int64_t next = rt::now_ns() + period;
    const int64_t total = static_cast<int64_t>(tick_hz) * seconds;
    int imu_div = 0, pos_div = 0, gps_div = 0;
    int64_t n_imu = 0, n_pos = 0, n_gps = 0;
    for (int64_t i = 0; i < total; ++i) {
        rt::sleep_until_ns(next);
        const uint64_t t_us = static_cast<uint64_t>(rt::now_ns() / 1000);
        if (++imu_div >= imu_every) {
            imu_div = 0;
            // id is the last payload byte. v2 trims trailing zeros, so id=0
            // encodes 32 bytes instead of the 75 PX4 sends.
            mavlink_msg_highres_imu_pack(
                1, 1, &msg, t_us,
                0.01f, -0.02f, 9.81f,
                0.001f, -0.002f, 0.003f,
                0.21f, 0.02f, 0.41f,
                1013.25f, 1.0f, 12.0f, 21.5f,
                0xFFFF, 1);
            send_msg(fd, dst, msg, buf);
            ++n_imu;
        }
        if (++pos_div >= pos_every) {
            pos_div = 0;
            mavlink_msg_local_position_ned_pack(
                1, 1, &msg,
                static_cast<uint32_t>(t_us / 1000),
                10.0f, 2.0f, -5.0f,
                0.1f, 0.0f, 0.0f);
            send_msg(fd, dst, msg, buf);
            ++n_pos;
        }
        if (++gps_div >= gps_every) {
            gps_div = 0;
            mavlink_msg_gps_raw_int_pack(
                1, 1, &msg, t_us,
                GPS_FIX_TYPE_3D_FIX,
                374419000, -1221430000, 30000,
                90, 120, 10, UINT16_MAX, 11,
                28000, 1500, 2000, 200, 0, 0);
            send_msg(fd, dst, msg, buf);
            ++n_gps;
        }
        next += period;
    }
    close(fd);
    std::fprintf(stderr,
                 "sent %lld HIGHRES_IMU @ %d Hz, %lld LOCAL_POSITION_NED @ %d Hz, %lld GPS_RAW_INT @ %d Hz\n",
                 static_cast<long long>(n_imu), imu_hz,
                 static_cast<long long>(n_pos), pos_hz,
                 static_cast<long long>(n_gps), gps_hz);
    return 0;
}
