// Test fixture. HIGHRES_IMU v2 frames at a target rate, so the loop under test
// has real MAVLink traffic to drain. Stands in for PX4 SITL until 2e.
#include "rt_platform.h"

#include "common/mavlink.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    int hz = 400, port = 14555, seconds = 30;
    for (int i = 1; i < argc; ++i) {
        const std::string f = argv[i];
        if (i + 1 >= argc) break;
        if      (f == "--hz")      hz      = std::atoi(argv[++i]);
        else if (f == "--port")    port    = std::atoi(argv[++i]);
        else if (f == "--seconds") seconds = std::atoi(argv[++i]);
    }
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);

    mavlink_message_t msg{};
    uint8_t buf[MAVLINK_MAX_PACKET_LEN];
    const int64_t period = 1000000000LL / hz;
    int64_t next = rt::now_ns() + period;
    const int64_t total = static_cast<int64_t>(hz) * seconds;
    uint16_t frame_len = 0;
    for (int64_t i = 0; i < total; ++i) {
        rt::sleep_until_ns(next);
        // id is the last payload byte. v2 trims trailing zeros, so id=0
        // encodes 32 bytes instead of the 75 PX4 sends.
        mavlink_msg_highres_imu_pack(
            1, 1, &msg,
            static_cast<uint64_t>(rt::now_ns() / 1000),
            0.01f, -0.02f, 9.81f,
            0.001f, -0.002f, 0.003f,
            0.21f, 0.02f, 0.41f,
            1013.25f, 1.0f, 12.0f, 21.5f,
            0xFFFF, 1);
        frame_len = mavlink_msg_to_send_buffer(buf, &msg);
        sendto(fd, buf, frame_len, 0,
               reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
        next += period;
    }
    close(fd);
    std::fprintf(stderr, "sent %lld HIGHRES_IMU frames of %u bytes at %d Hz\n",
                 static_cast<long long>(total), frame_len, hz);
    return 0;
}
